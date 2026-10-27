#include "controllers/PlaybackController.h"

#include "ui/UiFormat.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vwave::app {

PlaybackController::PlaybackController(vwave::Timeline& timeline,
                                       AudioPlayer& player, int64_t& playhead,
                                       LoopController& loop,
                                       StatusSink status, FollowSink follow)
    : m_timeline(timeline),
      m_player(player),
      m_playhead(playhead),
      m_loop(loop),
      m_status(std::move(status)),
      m_follow(std::move(follow)) {}

bool PlaybackController::loopPlaybackActive() const {
    return m_loop.active(m_timeline.lengthFrames());
}

bool PlaybackController::seekToTime(double seconds, std::string* error) {
    if (error) error->clear();
    if (m_timeline.empty() || m_timeline.sampleRate() <= 0) {
        if (error) *error = "No timeline audio";
        return false;
    }
    if (!std::isfinite(seconds) || seconds < 0.0) {
        if (error) *error = "Invalid time";
        return false;
    }
    m_player.stop();
    m_playhead = std::clamp<int64_t>(
        static_cast<int64_t>(std::llround(
            seconds * static_cast<double>(m_timeline.sampleRate()))),
        0, m_timeline.lengthFrames());
    m_follow(m_playhead);
    m_status("Playhead " +
             ui::formatSeconds(static_cast<double>(m_playhead) /
                               m_timeline.sampleRate()));
    return true;
}

void PlaybackController::goToStart() { seekToTime(0.0, nullptr); }

void PlaybackController::goToEnd() {
    if (m_timeline.sampleRate() > 0) {
        seekToTime(static_cast<double>(m_timeline.lengthFrames()) /
                       m_timeline.sampleRate(),
                   nullptr);
    }
}

void PlaybackController::togglePlay() {
    if (m_player.isPlaying()) {
        stop();
        return;
    }
    if (m_timeline.empty()) {
        m_status("Add or open an audio file first");
        return;
    }
    vwave::Range range;
    const bool loop = loopPlaybackActive();
    if (loop) {
        const int64_t loopEnd =
            std::clamp<int64_t>(m_loop.end(), 0, m_timeline.lengthFrames());
        const int64_t loopStart =
            std::clamp<int64_t>(m_loop.start(), 0, m_timeline.lengthFrames());
        int64_t start =
            std::clamp<int64_t>(m_playhead, 0, m_timeline.lengthFrames());
        if (start < loopStart || start >= loopEnd) start = loopStart;
        range = {start, loopEnd};
    } else if (m_timeline.selection().active) {
        range = m_timeline.selectedRange();
    } else {
        int64_t start =
            std::clamp<int64_t>(m_playhead, 0, m_timeline.lengthFrames());
        if (start >= m_timeline.lengthFrames()) start = 0;
        range = {start, m_timeline.lengthFrames()};
    }
    if (range.empty()) {
        m_status("Nothing to play");
        return;
    }
    if (m_player.play(m_timeline, range.start, range.end, loop)) {
        m_playhead = range.start;
        m_status(loop ? "Playing loop" : "Playing mix");
    } else {
        m_status("Audio playback is unavailable");
    }
}

void PlaybackController::stop() {
    m_player.stop();
    if (m_player.sourceStart() <= m_timeline.lengthFrames())
        m_playhead = m_player.sourceStart();
}

void PlaybackController::update() {
    if (!m_player.isPlaying()) return;
    m_playhead = std::clamp<int64_t>(m_player.sourcePosition(), 0,
                                     m_timeline.lengthFrames());
    m_follow(m_playhead);
}

} // namespace vwave::app
