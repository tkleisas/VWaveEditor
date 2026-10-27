#include "App.h"

#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace vwave::ui;

int App::laneXForFrame(const SDL_Rect& lane, int64_t frame) const {
    return lane.x + static_cast<int>(std::lround(
                        m_view.pixelAt(frame, std::max(1, lane.w))));
}

int64_t App::laneFrameAtX(const SDL_Rect& lane, int x) const {
    return m_view.frameAt(x - lane.x, std::max(1, lane.w),
                          m_timeline.lengthFrames());
}

SDL_Rect App::selectionScreenRect() const {
    const auto& selection = m_timeline.selection();
    if (!selection.active || m_waveArea.w <= 0) return {};
    SDL_Rect bounds = m_waveArea;
    if (m_timeline.selectionScope() == vwave::SelectionScope::ActiveTrack &&
        m_timeline.activeTrack() >= 0 &&
        m_timeline.activeTrack() <
            static_cast<int>(m_trackLaneRects.size())) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(m_timeline.activeTrack())];
        if (lane.w > 0) bounds = lane;
    }
    const int left = std::clamp(laneXForFrame(bounds, selection.start),
                                bounds.x, bounds.x + bounds.w);
    const int right = std::clamp(laneXForFrame(bounds, selection.end),
                                 bounds.x, bounds.x + bounds.w);
    return {left, bounds.y, std::max(1, right - left), bounds.h};
}

void App::drawWaveform(const SDL_Rect& area) {
    m_trackHeaderRects.clear();
    m_trackLaneRects.clear();
    m_trackHeaderRects.resize(static_cast<size_t>(m_timeline.trackCount()));
    m_trackLaneRects.resize(static_cast<size_t>(m_timeline.trackCount()));
    if (!hasTracks() || !activeDocument().hasClips()) {
        m_clipTool = false;
        m_selectedClip = -1;
    }

    const int margin = 10;
    const int headerWidth = 232;
    const int rulerHeight = 24;
    const int overviewHeight = 30;
    const int rowsTop = area.y + rulerHeight + overviewHeight + 8;
    const int laneX = area.x + margin + headerWidth + 8;
    const int laneWidth = area.w - margin * 2 - headerWidth - 8;
    const int laneHeight = area.h - rulerHeight - overviewHeight - 18;
    m_trackHeaderColumn = {area.x + margin, rowsTop, headerWidth - 8, laneHeight};
    m_tracksArea = {laneX, rowsTop, laneWidth, laneHeight};
    m_waveArea = m_tracksArea;
    m_rulerArea = {laneX, area.y, laneWidth, rulerHeight};
    m_overviewArea = {laneX, area.y + rulerHeight + 2, laneWidth,
                      overviewHeight - 8};

    drawText("TRACKS", area.x + margin + 2, area.y + 8, kAccent, fontSmall());
    drawText(std::to_string(m_timeline.trackCount()) + " track" +
                 (m_timeline.trackCount() == 1 ? "" : "s"),
             area.x + margin + 2, area.y + 24, kTextFaint, fontSmall());

    const double totalFrames = std::max(
        1.0, static_cast<double>(m_timeline.lengthFrames()));
    const int ticks = 10;
    for (int i = 0; i <= ticks; ++i) {
        const double frame = m_view.startFrame + m_view.visibleFrames * i / ticks;
        const int x = laneX + laneWidth * i / ticks;
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 90);
        SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
        drawText(formatSeconds(frame / std::max(1, m_timeline.sampleRate())),
                 x + 3, area.y + 7, kTextFaint, fontSmall());
    }
    if (m_loopEnd > m_loopStart) {
        const int loopLeft = std::clamp(
            laneXForFrame(m_rulerArea, m_loopStart), m_rulerArea.x,
            m_rulerArea.x + m_rulerArea.w);
        const int loopRight = std::clamp(
            laneXForFrame(m_rulerArea, m_loopEnd), m_rulerArea.x,
            m_rulerArea.x + m_rulerArea.w);
        SDL_Rect bar{loopLeft, area.y + rulerHeight - 5,
                     std::max(1, loopRight - loopLeft), 4};
        SDL_SetRenderDrawColor(
            m_renderer, kOrange.r, kOrange.g, kOrange.b,
            m_loopEnabled ? 230 : 90);
        SDL_RenderFillRect(m_renderer, &bar);
    }

    SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                           kPanelRaised.b, 255);
    SDL_RenderFillRect(m_renderer, &m_overviewArea);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_overviewArea);
    ensureOverview();
    if (!m_overviewPeaks.empty() && hasTracks()) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        const double trackStart = static_cast<double>(active.startFrame) /
                                  totalFrames;
        const double trackEnd =
            static_cast<double>(active.startFrame +
                                active.document.audio().numFrames()) /
            totalFrames;
        const int x0 = m_overviewArea.x +
                       static_cast<int>(trackStart * m_overviewArea.w);
        const int x1 = m_overviewArea.x +
                       static_cast<int>(trackEnd * m_overviewArea.w);
        const int center = m_overviewArea.y + m_overviewArea.h / 2;
        SDL_SetRenderDrawColor(m_renderer, kTextDim.r, kTextDim.g, kTextDim.b,
                               110);
        SDL_RenderDrawLine(m_renderer, m_overviewArea.x, center,
                           m_overviewArea.x + m_overviewArea.w, center);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 160);
        const int width = std::max(1, x1 - x0);
        for (int i = 0; i < width; ++i) {
            const size_t index = static_cast<size_t>(
                static_cast<double>(i) / width *
                static_cast<double>(m_overviewPeaks.size()));
            if (index >= m_overviewPeaks.size()) continue;
            const float minimum = m_overviewPeaks[index].minimum;
            const float maximum = m_overviewPeaks[index].maximum;
            const int y0 = center - static_cast<int>(
                                          maximum *
                                          (m_overviewArea.h / 2 - 2));
            const int y1 = center - static_cast<int>(
                                          minimum *
                                          (m_overviewArea.h / 2 - 2));
            SDL_RenderDrawLine(m_renderer, x0 + i, y0, x0 + i, y1);
        }
        const double viewStart = m_view.startFrame / totalFrames;
        const double viewEnd =
            (m_view.startFrame + m_view.visibleFrames) / totalFrames;
        SDL_Rect viewBox{
            m_overviewArea.x + static_cast<int>(viewStart * m_overviewArea.w),
            m_overviewArea.y + 2,
            std::max(2, static_cast<int>((viewEnd - viewStart) *
                                         m_overviewArea.w)),
            m_overviewArea.h - 4};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b, 220);
        SDL_RenderDrawRect(m_renderer, &viewBox);
        if (m_timeline.selection().active) {
            const double selectionStart =
                static_cast<double>(m_timeline.selection().start) / totalFrames;
            const double selectionEnd =
                static_cast<double>(m_timeline.selection().end) / totalFrames;
            SDL_Rect selection{
                m_overviewArea.x +
                    static_cast<int>(selectionStart * m_overviewArea.w),
                m_overviewArea.y + 1,
                std::max(1, static_cast<int>((selectionEnd - selectionStart) *
                                             m_overviewArea.w)),
                m_overviewArea.h - 2};
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b, 80);
            SDL_RenderFillRect(m_renderer, &selection);
        }
        if (m_playhead >= 0 && m_playhead <= m_timeline.lengthFrames()) {
            const int x = m_overviewArea.x + static_cast<int>(
                static_cast<double>(m_playhead) / totalFrames *
                m_overviewArea.w);
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                   kOrange.b, 255);
            SDL_RenderDrawLine(m_renderer, x, m_overviewArea.y, x,
                               m_overviewArea.y + m_overviewArea.h);
        }
    }

    const int rowHeight = std::max(
        112, laneHeight / std::max(1, m_timeline.trackCount()));
    m_trackContentHeight = m_timeline.trackCount() * rowHeight;
    const int maximumScroll = std::max(0, m_trackContentHeight - laneHeight);
    m_trackScroll = std::clamp(m_trackScroll, 0.0,
                               static_cast<double>(maximumScroll));

    m_ui.setControlArea(2);
    SDL_Rect rowsClip{area.x + margin, rowsTop, headerWidth + 8 + laneWidth,
                      laneHeight};
    SDL_RenderSetClipRect(m_renderer, &rowsClip);
    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const int rowY = rowsTop + index * rowHeight -
                         static_cast<int>(std::lround(m_trackScroll));
        if (rowY + rowHeight <= rowsTop || rowY >= rowsTop + laneHeight)
            continue;
        SDL_Rect header{area.x + margin, rowY + 2, headerWidth - 8,
                        rowHeight - 6};
        SDL_Rect lane{laneX, rowY + 2, laneWidth, rowHeight - 6};
        m_trackHeaderRects[static_cast<size_t>(index)] = header;
        m_trackLaneRects[static_cast<size_t>(index)] = lane;
        drawTrackHeader(header, index);

        const auto& track = m_timeline.track(index);
        const bool active = index == m_timeline.activeTrack();
        SDL_SetRenderDrawColor(m_renderer,
                               active ? kLaneActive.r : kLane.r,
                               active ? kLaneActive.g : kLane.g,
                               active ? kLaneActive.b : kLane.b, 255);
        SDL_RenderFillRect(m_renderer, &lane);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b,
                               active ? 220 : 90);
        SDL_RenderDrawRect(m_renderer, &lane);

        const bool audible =
            !track.muted && (!m_timeline.anySolo() || track.solo);
        const vwave::Audio& audio = track.document.audio();
        if (audio.empty()) {
            drawText("Empty track", lane.x + 12, lane.y + lane.h / 2 - 8,
                     kTextFaint, fontSmall());
            continue;
        }
        const int64_t viewStart =
            static_cast<int64_t>(std::floor(m_view.startFrame));
        const int64_t viewEnd = static_cast<int64_t>(
            std::ceil(m_view.startFrame + m_view.visibleFrames));
        const int64_t localStart = std::max<int64_t>(
            0, viewStart - track.startFrame);
        const int64_t localEnd = std::min<int64_t>(
            audio.numFrames(), viewEnd - track.startFrame);
        if (localEnd <= localStart) continue;
        const double timelineStart =
            static_cast<double>(track.startFrame + localStart);
        const int x0 = lane.x + static_cast<int>(
                                   (timelineStart - m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   lane.w);
        const int x1 = lane.x + static_cast<int>(
                                   (static_cast<double>(track.startFrame +
                                                        localEnd) -
                                    m_view.startFrame) /
                                   std::max(1.0, m_view.visibleFrames) *
                                   lane.w);
        const int width = std::max(1, x1 - x0);
        const int channelCount =
            std::min(2, std::max(1, audio.numChannels()));
        const int laneGap = 3;
        const int channelHeight =
            (lane.h - 10 - (channelCount - 1) * laneGap) / channelCount;
        for (int ch = 0; ch < channelCount; ++ch) {
            SDL_Rect channelLane{lane.x, lane.y + 5 +
                                                ch * (channelHeight + laneGap),
                                 lane.w, channelHeight};
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 70);
            SDL_RenderDrawLine(m_renderer, channelLane.x,
                               channelLane.y + channelHeight / 2,
                               channelLane.x + channelLane.w,
                               channelLane.y + channelHeight / 2);
            const std::vector<vwave::PeakBin> peaks =
                vwave::computePeaks(audio, ch, {localStart, localEnd}, width);
            const int center = channelLane.y + channelHeight / 2;
            SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                   kAccent.b,
                                   audible ? (active ? 230 : 150) : 65);
            for (int i = 0; i < width; ++i) {
                if (x0 + i < lane.x || x0 + i >= lane.x + lane.w) continue;
                const float minimum = peaks[static_cast<size_t>(i)].minimum;
                const float maximum = peaks[static_cast<size_t>(i)].maximum;
                int y0 = center - static_cast<int>(maximum *
                                                   (channelHeight / 2 - 3));
                int y1 = center - static_cast<int>(minimum *
                                                   (channelHeight / 2 - 3));
                if (y0 == y1) {
                    --y0;
                    ++y1;
                }
                SDL_RenderDrawLine(m_renderer, x0 + i, y0, x0 + i, y1);
            }
        }
    }
    SDL_RenderSetClipRect(m_renderer, &m_contentRect);
    m_ui.setControlArea(1);

    SDL_RenderSetClipRect(m_renderer, &m_tracksArea);
    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(index)];
        if (lane.w <= 0) continue;
        const auto& track = m_timeline.track(index);
        if (!track.document.hasClips()) continue;
        const auto& clips = track.document.clips();
        for (size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex) {
            const auto& clip = clips[clipIndex];
            const int64_t clipStart = track.startFrame + clip.timelineStart;
            const int64_t clipEnd =
                clipStart + vwave::clipOutputFrames(clip);
            const int x0 = laneXForFrame(lane, clipStart);
            const int x1 = laneXForFrame(lane, clipEnd);
            if (x1 < lane.x || x0 > lane.x + lane.w) continue;
            const int left = std::clamp(x0, lane.x, lane.x + lane.w);
            const int right = std::clamp(x1, lane.x, lane.x + lane.w);
            SDL_Rect box{left, lane.y + 4, std::max(1, right - left),
                         lane.h - 8};
            const bool selected =
                index == m_timeline.activeTrack() &&
                static_cast<int>(clipIndex) == m_selectedClip;
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kTextDim.r,
                selected ? kAccent.g : kTextDim.g,
                selected ? kAccent.b : kTextDim.b,
                selected ? 45 : 22);
            SDL_RenderFillRect(m_renderer, &box);
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kBorder.r,
                selected ? kAccent.g : kBorder.g,
                selected ? kAccent.b : kBorder.b,
                selected ? 220 : 120);
            SDL_RenderDrawRect(m_renderer, &box);
            SDL_Rect bar{box.x, box.y, box.w, 3};
            SDL_SetRenderDrawColor(
                m_renderer, selected ? kAccent.r : kTextDim.r,
                selected ? kAccent.g : kTextDim.g,
                selected ? kAccent.b : kTextDim.b,
                selected ? 230 : 90);
            SDL_RenderFillRect(m_renderer, &bar);
            if (box.w > 48) {
                const std::string name =
                    clip.name.empty()
                        ? "Clip " + std::to_string(clipIndex + 1)
                        : clip.name;
                drawText(truncateText(name, fontSmall(), box.w - 8), box.x + 4,
                         box.y + 5, selected ? kText : kTextDim, fontSmall());
            }
        }
        if (index == m_timeline.activeTrack() && m_clipTool &&
            m_selectedClip >= 0 &&
            static_cast<size_t>(m_selectedClip) < clips.size()) {
            const auto& clip = clips[static_cast<size_t>(m_selectedClip)];
            const int64_t clipEnd =
                track.startFrame + clip.timelineStart +
                vwave::clipOutputFrames(clip);
            const int endX = laneXForFrame(lane, clipEnd);
            if (endX >= lane.x && endX <= lane.x + lane.w) {
                SDL_Rect handle{endX - 8, lane.y + 9, 7, 7};
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 240);
                SDL_RenderFillRect(m_renderer, &handle);
            }
        }
    }

    if (m_loopEnd > m_loopStart) {
        const int loopLeft = std::clamp(
            laneXForFrame(m_waveArea, m_loopStart), m_waveArea.x,
            m_waveArea.x + m_waveArea.w);
        const int loopRight = std::clamp(
            laneXForFrame(m_waveArea, m_loopEnd), m_waveArea.x,
            m_waveArea.x + m_waveArea.w);
        SDL_Rect loop{loopLeft, m_waveArea.y,
                      std::max(1, loopRight - loopLeft), m_waveArea.h};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 20 : 8);
        SDL_RenderFillRect(m_renderer, &loop);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               m_loopEnabled ? 190 : 80);
        SDL_RenderDrawLine(m_renderer, loopLeft, m_waveArea.y, loopLeft,
                           m_waveArea.y + m_waveArea.h);
        SDL_RenderDrawLine(m_renderer, loopRight, m_waveArea.y, loopRight,
                           m_waveArea.y + m_waveArea.h);
        if (m_loopEnabled) {
            drawText("LOOP", loopLeft + 4, m_waveArea.y + 4, kOrange,
                     fontSmall());
        }
    }

    if (m_hasBeats && hasTracks()) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        for (size_t i = 0; i < m_beats.beats.size(); ++i) {
            const int64_t beat =
                m_beats.beats[i] + active.startFrame;
            if (beat < m_view.startFrame ||
                beat > m_view.startFrame + m_view.visibleFrames)
                continue;
            const int x = laneX + static_cast<int>(
                (static_cast<double>(beat) - m_view.startFrame) /
                std::max(1.0, m_view.visibleFrames) * laneWidth);
            SDL_SetRenderDrawColor(m_renderer,
                                   (i % 4 == 0) ? kBlue.r : kBorder.r,
                                   (i % 4 == 0) ? kBlue.g : kBorder.g,
                                   (i % 4 == 0) ? kBlue.b : kBorder.b,
                                   (i % 4 == 0) ? 150 : 80);
            SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
        }
    }

    if (hasTracks() &&
        m_timeline.activeTrack() <
            static_cast<int>(m_trackLaneRects.size())) {
        const auto& active = m_timeline.track(m_timeline.activeTrack());
        const SDL_Rect activeLane =
            m_trackLaneRects[static_cast<size_t>(m_timeline.activeTrack())];
        if (activeLane.w > 0) {
            for (const auto& slice : active.document.slices()) {
                const int64_t frame = slice.range.start + active.startFrame;
                if (frame < m_view.startFrame ||
                    frame > m_view.startFrame + m_view.visibleFrames)
                    continue;
                const int x = laneX + static_cast<int>(
                    (static_cast<double>(frame) - m_view.startFrame) /
                    std::max(1.0, m_view.visibleFrames) * laneWidth);
                SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g,
                                       kAccent.b, 200);
                SDL_RenderDrawLine(m_renderer, x, activeLane.y, x,
                                   activeLane.y + activeLane.h);
            }
            for (const auto& marker : active.document.markers()) {
                const int64_t frame = marker.frame + active.startFrame;
                if (frame < m_view.startFrame ||
                    frame > m_view.startFrame + m_view.visibleFrames)
                    continue;
                const int x = laneX + static_cast<int>(
                    (static_cast<double>(frame) - m_view.startFrame) /
                    std::max(1.0, m_view.visibleFrames) * laneWidth);
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 220);
                SDL_RenderDrawLine(m_renderer, x, activeLane.y, x,
                                   activeLane.y + activeLane.h);
                drawText(marker.name, x + 3, activeLane.y + 3, kOrange,
                         fontSmall());
            }
        }
    }

    for (int index = 0; index < m_timeline.trackCount(); ++index) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(index)];
        if (lane.w <= 0) continue;
        const auto& track = m_timeline.track(index);
        const int64_t frames = track.document.numFrames();
        if (frames <= 0) continue;
        const int startX = laneXForFrame(lane, track.startFrame);
        const int endX = laneXForFrame(lane, track.startFrame + frames);
        const bool active = index == m_timeline.activeTrack();
        if (startX >= lane.x && startX <= lane.x + lane.w) {
            SDL_Rect handle{startX - 4, lane.y + 2, 8, 14};
            SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                   active ? 220 : 120);
            SDL_RenderFillRect(m_renderer, &handle);
        }
        if (endX >= lane.x && endX <= lane.x + lane.w) {
            SDL_Rect trim{endX - 4, lane.y + lane.h - 16, 8, 14};
            SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                   active ? 220 : 120);
            SDL_RenderFillRect(m_renderer, &trim);
            SDL_Rect stretch{endX - 13, lane.y + 2, 11, 11};
            SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                   kOrange.b, active ? 230 : 120);
            SDL_RenderFillRect(m_renderer, &stretch);
        }
    }

    if (m_timeline.selection().active) {
        const SDL_Rect selection = selectionScreenRect();
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 50);
        SDL_RenderFillRect(m_renderer, &selection);
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 220);
        SDL_RenderDrawLine(m_renderer, selection.x, selection.y, selection.x,
                           selection.y + selection.h);
        SDL_RenderDrawLine(m_renderer, selection.x + selection.w - 1,
                           selection.y, selection.x + selection.w - 1,
                           selection.y + selection.h);
        SDL_Rect startHandle{selection.x - 3, selection.y, 7, selection.h};
        SDL_Rect endHandle{selection.x + selection.w - 4, selection.y, 7,
                           selection.h};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b, 150);
        SDL_RenderFillRect(m_renderer, &startHandle);
        SDL_RenderFillRect(m_renderer, &endHandle);
        SDL_Rect repeatHandle{selection.x + selection.w - 6,
                              selection.y + selection.h - 12, 12, 12};
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               230);
        SDL_RenderFillRect(m_renderer, &repeatHandle);
        SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 150);
        SDL_RenderDrawLine(m_renderer, selection.x,
                           selection.y + selection.h - 1,
                           selection.x + selection.w,
                           selection.y + selection.h - 1);
    }

    if (m_waveDrag != WaveDragMode::None && m_waveDragTrack >= 0 &&
        m_waveDragTrack < static_cast<int>(m_trackLaneRects.size())) {
        const auto& lane =
            m_trackLaneRects[static_cast<size_t>(m_waveDragTrack)];
        if (lane.w > 0) {
            const int x = laneXForFrame(lane, m_waveDragPreviewFrame);
            if (m_waveDrag == WaveDragMode::TrackTrimStart ||
                m_waveDrag == WaveDragMode::TrackTrimEnd) {
                SDL_SetRenderDrawColor(m_renderer, kBlue.r, kBlue.g, kBlue.b,
                                       255);
                SDL_RenderDrawLine(m_renderer, x, lane.y, x,
                                   lane.y + lane.h);
            } else if (m_waveDrag == WaveDragMode::TrackStretch ||
                       m_waveDrag == WaveDragMode::ClipMove ||
                       m_waveDrag == WaveDragMode::ClipStretch) {
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 255);
                SDL_RenderDrawLine(m_renderer, x, lane.y, x,
                                   lane.y + lane.h);
            }
        }
        if (m_waveDrag == WaveDragMode::SelectionRepeat &&
            m_timeline.selection().active && m_repeatDragCount > 0) {
            const SDL_Rect selection = selectionScreenRect();
            for (int repeat = 0; repeat < m_repeatDragCount; ++repeat) {
                SDL_Rect block{selection.x + (repeat + 1) * selection.w,
                               selection.y, selection.w, selection.h};
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 70);
                SDL_RenderFillRect(m_renderer, &block);
                SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g,
                                       kOrange.b, 180);
                SDL_RenderDrawRect(m_renderer, &block);
            }
        }
    }

    if (m_playhead >= m_view.startFrame &&
        m_playhead <= m_view.startFrame + m_view.visibleFrames) {
        const int x = laneX + static_cast<int>(
            (static_cast<double>(m_playhead) - m_view.startFrame) /
            std::max(1.0, m_view.visibleFrames) * laneWidth);
        SDL_SetRenderDrawColor(m_renderer, kOrange.r, kOrange.g, kOrange.b,
                               255);
        SDL_RenderDrawLine(m_renderer, x, rowsTop, x, rowsTop + laneHeight);
    }
    SDL_RenderSetClipRect(m_renderer, nullptr);

    if (maximumScroll > 0) {
        SDL_Rect track{m_trackHeaderColumn.x + m_trackHeaderColumn.w - 4,
                       m_trackHeaderColumn.y, 4, m_trackHeaderColumn.h};
        SDL_SetRenderDrawColor(m_renderer, kPanelRaised.r, kPanelRaised.g,
                               kPanelRaised.b, 200);
        SDL_RenderFillRect(m_renderer, &track);
        const int handleHeight = std::max(
            20, static_cast<int>(static_cast<double>(track.h) * laneHeight /
                                 m_trackContentHeight));
        const int handleY = track.y + static_cast<int>(
                                          (static_cast<double>(track.h) -
                                           handleHeight) *
                                          (m_trackScroll / maximumScroll));
        SDL_Rect handle{track.x, handleY, track.w, handleHeight};
        SDL_SetRenderDrawColor(m_renderer, kTextFaint.r, kTextFaint.g,
                               kTextFaint.b, 255);
        SDL_RenderFillRect(m_renderer, &handle);
    }
}

void App::drawTrackHeader(const SDL_Rect& header, int index) {
    const auto& track = m_timeline.track(index);
    const bool active = index == m_timeline.activeTrack();
    SDL_SetRenderDrawColor(m_renderer, active ? kPanelRaised.r : kPanel.r,
                           active ? kPanelRaised.g : kPanel.g,
                           active ? kPanelRaised.b : kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &header);
    SDL_SetRenderDrawColor(m_renderer, active ? kAccent.r : kBorder.r,
                           active ? kAccent.g : kBorder.g,
                           active ? kAccent.b : kBorder.b, active ? 230 : 110);
    SDL_RenderDrawRect(m_renderer, &header);
    if (active) {
        SDL_Rect bar{header.x, header.y, 4, header.h};
        SDL_SetRenderDrawColor(m_renderer, kAccent.r, kAccent.g, kAccent.b,
                               255);
        SDL_RenderFillRect(m_renderer, &bar);
    }
    drawText(truncateText(track.name, fontBold(), header.w - 44), header.x + 12,
             header.y + 5, active ? kText : kTextDim, fontBold());
    addButton("x", header.x + header.w - 22, header.y + 3, 18, 18,
              [this, index] { removeTrack(index); });
    const int buttonY = header.y + 25;
    addButton("M", header.x + 8, buttonY, 24, 18,
              [this, index] {
                  const bool muted = !m_timeline.track(index).muted;
                  m_timeline.setTrackMuted(index, muted);
                  updateMixer();
                  setStatus(muted ? "Track muted" : "Track unmuted");
              },
              false, track.muted);
    addButton("S", header.x + 35, buttonY, 24, 18,
              [this, index] {
                  const bool solo = !m_timeline.track(index).solo;
                  m_timeline.setTrackSolo(index, solo);
                  updateMixer();
                  setStatus(solo ? "Track solo" : "Solo cleared");
              },
              false, track.solo);
    addButton("R", header.x + 62, buttonY, 24, 18,
              [this, index] {
                  const bool armed = !m_timeline.track(index).armed;
                  for (int i = 0; i < m_timeline.trackCount(); ++i)
                      m_timeline.setTrackArmed(i, i == index && armed);
                  setStatus(armed ? "Track armed for recording"
                                  : "Track disarmed");
              },
              false, track.armed);
    addSlider("Vol", formatDb(m_trackControls[static_cast<size_t>(index)].gain.value),
              header.x + 8, header.y + 47, header.w - 16,
              m_trackControls[static_cast<size_t>(index)].gain,
              [this, index](double value) {
                  m_timeline.setTrackGainDb(index, value);
                  updateMixer();
              });
    addSlider("Pan",
              std::to_string(static_cast<int>(std::lround(
                  m_trackControls[static_cast<size_t>(index)].pan.value * 100))) +
                  "%",
              header.x + 8, header.y + 74, header.w - 16,
              m_trackControls[static_cast<size_t>(index)].pan,
              [this, index](double value) {
                  m_timeline.setTrackPan(index, value);
                  updateMixer();
              });
}

void App::ensureOverview() {
    if (!m_overviewDirty && !m_overviewPeaks.empty()) return;
    m_overviewDirty = false;
    m_overviewPeaks.clear();
    const vwave::Audio& audio = activeDocument().audio();
    if (audio.empty()) return;
    m_overviewPeaks = vwave::computePeaks(audio, 0,
                                          {0, audio.numFrames()}, 2048);
}

