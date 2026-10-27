#pragma once

namespace vwave::ui {

inline constexpr const char* kAlgorithmNames[] = {
    "WSOLA", "Phase Vocoder", "PaulStretch"};
inline constexpr int kFrameSizes[] = {256, 512, 1024, 2048, 4096, 8192};
inline constexpr const char* kSliceModeNames[] = {
    "Transients", "Beats", "Markers"};
inline constexpr const char* kMorphModeNames[] = {
    "Linear", "Equal Power", "Phase Aligned", "Spectral", "Harmonic"};
inline constexpr const char* kInterpolationNames[] = {
    "Nearest", "Linear", "Cubic"};
inline constexpr int kWavetableOversampling[] = {1, 2, 4, 8};
inline constexpr const char* kMasteringProfileNames[] = {
    "Balanced", "Definition", "Separation", "Fullness", "Loudness", "Custom"};
inline constexpr const char* kImagePhaseNames[] = {
    "Griffin-Lim", "Fast GL", "Phase Locked", "Random", "Color"};
inline constexpr const char* kImageTimeNames[] = {
    "Direct", "Mirrored", "Looped"};
inline constexpr const char* kImageFrequencyNames[] = {"Linear", "Log"};
inline constexpr const char* kScopeShapeNames[] = {
    "Polyline", "Line", "Circle", "Ellipse", "Lissajous", "Spiral",
    "Figure 8"};
inline constexpr const char* kScopeDirectionNames[] = {
    "Forward", "Reverse", "Ping-Pong"};
inline constexpr int kScopeOversampling[] = {1, 2, 4, 8};

} // namespace vwave::ui
