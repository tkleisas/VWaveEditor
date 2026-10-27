#include "App.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "ui/UiTheme.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

using namespace vwave::app;
using namespace vwave::ui;

void App::openTrackContextMenu(int trackIndex, int x, int y) {
    if (trackIndex < 0 || trackIndex >= m_timeline.trackCount()) return;
    selectTrack(trackIndex);
    if (trackIndex < static_cast<int>(m_trackLaneRects.size()) &&
        m_trackLaneRects[static_cast<size_t>(trackIndex)].w > 0) {
        const auto& lane = m_trackLaneRects[static_cast<size_t>(trackIndex)];
        const auto& track = m_timeline.track(trackIndex);
        if (x >= lane.x && x <= lane.x + lane.w &&
            track.document.hasClips()) {
            const int64_t local = std::clamp<int64_t>(
                laneFrameAtX(lane, x) - track.startFrame, 0,
                track.document.numFrames());
            m_selectedClip = track.document.clipIndexAt(local);
        }
    }
    m_contextMenuTrack = trackIndex;
    m_contextMenuPosition = {x, y};
    m_contextMenuItems.clear();
    auto add = [this](std::string label, std::function<void()> action,
                      bool enabled = true) {
        MenuItem item;
        item.label = std::move(label);
        item.action = std::move(action);
        item.enabled = enabled;
        m_contextMenuItems.push_back(std::move(item));
    };
    auto separator = [this] {
        MenuItem item;
        item.separator = true;
        m_contextMenuItems.push_back(std::move(item));
    };
    add(m_timeline.selectionScope() ==
                vwave::SelectionScope::AllTracks
            ? "Select Active Track Only"
            : "Select All Tracks",
        [this] { toggleSelectionScope(); });
    separator();
    const bool hasSelection = m_timeline.selection().active;
    add("Zero-Cross Cut", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        zeroCrossCut();
    }, hasSelection);
    add("Snap Selection to Zero Crossings", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        snapActiveSelection();
    }, hasSelection);
    add("Copy Selection to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveActiveSelectionToPlayhead(true);
    }, hasSelection);
    add("Move Selection to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveActiveSelectionToPlayhead(false);
    }, hasSelection);
    add("Repeat Selection x2", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        repeatActiveSelection(2);
    }, hasSelection);
    separator();
    add("Crop Track to Selection", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        cropActiveTrack();
    }, hasSelection);
    add("Shift to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        const int64_t start =
            clampFrame(m_playhead, m_timeline.lengthFrames());
        if (m_timeline.setTrackStartFrame(trackIndex, start)) {
            clearAnalysis();
            m_view.clamp(m_timeline.lengthFrames());
            setStatus("Shifted track to playhead");
        }
    });
    add("Shift Left 1 s", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        shiftActiveTrack(-1.0);
    });
    add("Shift Right 1 s", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        shiftActiveTrack(1.0);
    });
    add("Stretch Track In Place", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        stretchActiveTrack();
    }, m_timeline.track(trackIndex).document.numFrames() > 0);
    separator();
    const bool stereo =
        m_timeline.track(trackIndex).document.numChannels() == 2;
    const bool mono =
        m_timeline.track(trackIndex).document.numChannels() == 1;
    add("Split Stereo to Mono Tracks", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        splitActiveStereoTrack();
    }, stereo);
    add("Combine with Previous Mono", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        combineActiveMono(-1);
    }, mono && trackIndex > 0 &&
           m_timeline.track(trackIndex - 1).document.numChannels() == 1);
    add("Combine with Next Mono", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        combineActiveMono(1);
    }, mono && trackIndex + 1 < m_timeline.trackCount() &&
           m_timeline.track(trackIndex + 1).document.numChannels() == 1);
    add("Stereo Padding: " +
            std::string(vwave::stereoPaddingName(m_stereoPadding)),
        [this] { cycleStereoPadding(); });
    separator();
    const bool clipMode =
        m_timeline.track(trackIndex).document.hasClips();
    add(clipMode ? "Flatten Clips" : "Make Clips", [this, trackIndex, clipMode] {
        m_timeline.setActiveTrack(trackIndex);
        if (clipMode)
            flattenClips();
        else
            makeClips();
    }, true);
    add("Clip Tool: On/Off", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        toggleClipTool();
    }, clipMode);
    add("Split Clip at Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        splitClipAtPlayhead();
    }, clipMode);
    add("Delete Clip", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        deleteSelectedClip();
    }, clipMode);
    add("Repeat Clip x2", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        repeatSelectedClip(2);
    }, clipMode);
    add("Move Clip to Playhead", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        moveSelectedClipToPlayhead();
    }, clipMode);
    add("Stretch Clip", [this, trackIndex] {
        m_timeline.setActiveTrack(trackIndex);
        stretchSelectedClip();
    }, clipMode);
    separator();
    add("Delete Track", [this, trackIndex] { removeTrack(trackIndex); });
    m_contextMenuOpen = true;
}

void App::closeContextMenu() {
    m_contextMenuOpen = false;
    m_contextMenuItems.clear();
    m_contextMenuItemRects.clear();
    m_contextMenuTrack = -1;
}

void App::drawContextMenu() {
    m_contextMenuItemRects.clear();
    if (!m_contextMenuOpen || m_contextMenuItems.empty()) return;
    int width = 190;
    for (const auto& item : m_contextMenuItems) {
        if (item.separator) continue;
        width = std::max(width, textWidth(item.label, fontNormal()) + 34);
    }
    int height = 8;
    for (const auto& item : m_contextMenuItems)
        height += item.separator ? 7 : 24;
    height += 8;
    int x = m_contextMenuPosition.x;
    int y = m_contextMenuPosition.y;
    int windowW = 0;
    int windowH = 0;
    SDL_GetWindowSize(m_window, &windowW, &windowH);
    if (x + width > windowW) x = windowW - width;
    if (y + height > windowH) y = windowH - height;
    x = std::max(0, x);
    y = std::max(0, y);
    m_contextMenuRect = {x, y, width, height};
    SDL_SetRenderDrawColor(m_renderer, kPanel.r, kPanel.g, kPanel.b, 255);
    SDL_RenderFillRect(m_renderer, &m_contextMenuRect);
    SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    SDL_RenderDrawRect(m_renderer, &m_contextMenuRect);
    int mouseX = 0;
    int mouseY = 0;
    SDL_GetMouseState(&mouseX, &mouseY);
    SDL_Point mouse{mouseX, mouseY};
    int rowY = y + 8;
    m_contextMenuItemRects.resize(m_contextMenuItems.size());
    for (size_t index = 0; index < m_contextMenuItems.size(); ++index) {
        const MenuItem& item = m_contextMenuItems[index];
        if (item.separator) {
            SDL_SetRenderDrawColor(m_renderer, kBorder.r, kBorder.g,
                                   kBorder.b, 100);
            SDL_RenderDrawLine(m_renderer, x + 8, rowY + 3,
                               x + width - 8, rowY + 3);
            m_contextMenuItemRects[index] = {};
            rowY += 7;
            continue;
        }
        SDL_Rect row{x + 1, rowY, width - 2, 24};
        m_contextMenuItemRects[index] = row;
        if (item.enabled && SDL_PointInRect(&mouse, &row)) {
            SDL_SetRenderDrawColor(m_renderer, kAccentDark.r, kAccentDark.g,
                                   kAccentDark.b, 255);
            SDL_RenderFillRect(m_renderer, &row);
        }
        drawText(item.label, row.x + 12, row.y + 4,
                 item.enabled ? kText : kTextFaint, fontNormal());
        rowY += 24;
    }
}

