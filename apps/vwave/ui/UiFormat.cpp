#include "ui/UiFormat.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace vwave::ui {

std::string formatSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0.000 s";
    std::ostringstream out;
    if (seconds >= 60.0) {
        const int minutes = static_cast<int>(seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        out << minutes << ':'
            << std::setw(6) << std::setfill('0') << std::fixed
            << std::setprecision(3) << rest;
    } else {
        out << std::fixed << std::setprecision(3) << seconds << " s";
    }
    return out.str();
}

std::string formatTimeInput(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0.000";
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << seconds;
    return out.str();
}

std::string formatRate(int sampleRate) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(sampleRate) / 1000.0 << " kHz";
    return out.str();
}

std::string formatRatio(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << "x";
    return out.str();
}

std::string formatDb(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (value > 0.0) out << '+';
    out << value << " dB";
    return out.str();
}

std::string formatHz(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(value >= 1000.0 ? 2 : 0);
    if (value >= 1000.0)
        out << value / 1000.0 << " kHz";
    else
        out << value << " Hz";
    return out.str();
}

std::string formatInt(double value) {
    return std::to_string(static_cast<int>(std::lround(value)));
}

std::string formatMilliseconds(double value) {
    return std::to_string(static_cast<int>(std::lround(value))) + " ms";
}

std::string formatSecondsControl(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value << " s";
    return out.str();
}

std::string formatSemitones(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    if (value > 0.0) out << '+';
    out << value << " st";
    return out.str();
}

std::string formatMorph(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string formatMidiNote(double value) {
    static const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                  "F#", "G",  "G#", "A",  "A#", "B"};
    const int note = std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    std::ostringstream out;
    out << names[note % 12] << (note / 12 - 1) << " (" << note << ')';
    return out.str();
}

std::string truncateText(const std::string& text, TTF_Font* font,
                         int maxWidth) {
    if (!font || maxWidth <= 0) return text;
    int width = 0;
    if (TTF_SizeUTF8(font, text.c_str(), &width, nullptr) != 0) return text;
    if (width <= maxWidth) return text;
    std::string result = text;
    while (!result.empty()) {
        result.pop_back();
        if (TTF_SizeUTF8(font, (result + "...").c_str(), &width, nullptr) != 0)
            break;
        if (width <= maxWidth) return result + "...";
    }
    return "...";
}

int64_t clampFrame(int64_t frame, int64_t frames) {
    return std::clamp<int64_t>(frame, 0, frames);
}

} // namespace vwave::ui
