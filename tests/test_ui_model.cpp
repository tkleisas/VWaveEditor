#include "test_framework.h"

#include "vwave/UiModel.h"

#include <cmath>

using namespace vwave;

namespace {

Audio ramp(int64_t frames) {
    Audio audio(2, frames, 48000);
    for (int64_t i = 0; i < frames; ++i) {
        audio.sample(0, i) = static_cast<float>(i) / static_cast<float>(frames);
        audio.sample(1, i) = -static_cast<float>(i) / static_cast<float>(frames);
    }
    return audio;
}

} // namespace

VWTEST("waveform viewport fits and maps pixels") {
    WaveformViewport viewport;
    viewport.fit(10000);
    CHECK_NEAR(viewport.startFrame, 0.0, 1e-9);
    CHECK_NEAR(viewport.visibleFrames, 10000.0, 1e-9);
    CHECK_EQ(viewport.frameAt(0.0, 1000.0, 10000), int64_t{0});
    CHECK_EQ(viewport.frameAt(999.0, 1000.0, 10000), int64_t{9990});
    CHECK_NEAR(viewport.pixelAt(5000, 1000.0), 500.0, 1e-6);
    CHECK_NEAR(viewport.samplesPerPixel(1000.0), 10.0, 1e-9);
}

VWTEST("waveform zoom keeps the cursor anchored") {
    WaveformViewport viewport;
    viewport.fit(10000);
    const double anchorFrame = viewport.startFrame +
                               250.0 * viewport.samplesPerPixel(1000.0);
    viewport.zoomAt(2.0, 250.0, 1000.0, 10000);
    CHECK_NEAR(viewport.visibleFrames, 5000.0, 1e-9);
    CHECK_NEAR(viewport.startFrame, 1250.0, 1e-6);
    CHECK_NEAR(viewport.frameAt(250.0, 1000.0, 10000),
               anchorFrame, 1.0);

    viewport.zoomAt(0.5, 0.0, 1000.0, 10000);
    CHECK_NEAR(viewport.visibleFrames, 10000.0, 1e-9);
    CHECK_NEAR(viewport.startFrame, 0.0, 1e-6);
}

VWTEST("waveform pan clamps to the document") {
    WaveformViewport viewport;
    viewport.fit(1000);
    viewport.zoomAt(4.0, 500.0, 1000.0, 1000);
    viewport.pan(100000.0, 1000.0, 1000);
    CHECK_NEAR(viewport.startFrame, 750.0, 1e-6);
    viewport.pan(-100000.0, 1000.0, 1000);
    CHECK_NEAR(viewport.startFrame, 0.0, 1e-6);
    viewport.zoomAt(1000000.0, 500.0, 1000.0, 1000);
    CHECK_NEAR(viewport.visibleFrames, 16.0, 1e-9);
}

VWTEST("frame mapping clamps to document bounds") {
    WaveformViewport viewport;
    viewport.fit(1000);
    CHECK_EQ(viewport.frameAt(-100.0, 1000.0, 1000), int64_t{0});
    CHECK_EQ(viewport.frameAt(1200.0, 1000.0, 1000), int64_t{1000});
}

VWTEST("spectral viewport maps log frequency") {
    SpectralViewport viewport;
    viewport.minHz = 20.0;
    viewport.maxHz = 20000.0;
    viewport.logarithmic = true;
    CHECK_NEAR(viewport.frequencyAtY(0.0, 500.0), 20000.0, 1e-6);
    CHECK_NEAR(viewport.frequencyAtY(500.0, 500.0), 20.0, 1e-6);
    const double midY = viewport.yAtFrequency(
        std::sqrt(20.0 * 20000.0), 500.0);
    CHECK_NEAR(midY, 250.0, 1e-6);
    CHECK_NEAR(viewport.frequencyAtY(midY, 500.0),
               std::sqrt(20.0 * 20000.0), 1e-6);
}

VWTEST("spectral viewport maps linear frequency") {
    SpectralViewport viewport;
    viewport.minHz = 0.0;
    viewport.maxHz = 1000.0;
    viewport.logarithmic = false;
    CHECK_NEAR(viewport.frequencyAtY(0.0, 100.0), 1000.0, 1e-9);
    CHECK_NEAR(viewport.frequencyAtY(100.0, 100.0), 0.0, 1e-9);
    CHECK_NEAR(viewport.frequencyAtY(25.0, 100.0), 750.0, 1e-9);
}


VWTEST("parameter slider maps linear values") {
    ParameterSlider slider;
    slider.minimum = -24.0;
    slider.maximum = 24.0;
    slider.value = 0.0;
    CHECK_NEAR(slider.normalized(), 0.5, 1e-9);
    slider.setNormalized(0.25);
    CHECK_NEAR(slider.value, -12.0, 1e-9);
    slider.setValue(100.0);
    CHECK_NEAR(slider.value, 24.0, 1e-9);
    slider.setValue(-100.0);
    CHECK_NEAR(slider.value, -24.0, 1e-9);
}

VWTEST("parameter slider maps logarithmic values") {
    ParameterSlider slider;
    slider.minimum = 20.0;
    slider.maximum = 20000.0;
    slider.logarithmic = true;
    slider.setNormalized(0.0);
    CHECK_NEAR(slider.value, 20.0, 1e-6);
    slider.setNormalized(1.0);
    CHECK_NEAR(slider.value, 20000.0, 1e-6);
    slider.setNormalized(0.5);
    CHECK_NEAR(slider.value, std::sqrt(20.0 * 20000.0), 1e-3);
    CHECK_NEAR(slider.normalized(), 0.5, 1e-9);
}

VWTEST("parameter slider rounds stepped values") {
    ParameterSlider slider;
    slider.minimum = 1.0;
    slider.maximum = 8.0;
    slider.step = 1.0;
    slider.setValue(3.6);
    CHECK_NEAR(slider.value, 4.0, 1e-9);
    slider.setValue(7.8);
    CHECK_NEAR(slider.value, 8.0, 1e-9);
    slider.setValue(-10.0);
    CHECK_NEAR(slider.value, 1.0, 1e-9);
}

VWTEST("parameter slider handles degenerate ranges") {
    ParameterSlider slider;
    slider.minimum = 1.0;
    slider.maximum = 1.0;
    slider.value = 1.0;
    CHECK_NEAR(slider.normalized(), 0.0, 1e-9);
    slider.setNormalized(0.5);
    CHECK_NEAR(slider.value, 1.0, 1e-9);
    ParameterSlider bad;
    bad.minimum = 0.0;
    bad.maximum = 10.0;
    bad.logarithmic = true;
    bad.setNormalized(0.5);
    CHECK_NEAR(bad.value, 0.0, 1e-9);
}

VWTEST("spectral viewport zooms and fits the frequency range") {
    SpectralViewport viewport;
    viewport.minHz = 20.0;
    viewport.maxHz = 20000.0;
    viewport.zoom(2.0, std::sqrt(20.0 * 20000.0), 20.0, 24000.0);
    CHECK(viewport.minHz > 20.0);
    CHECK(viewport.maxHz < 20000.0);
    CHECK_NEAR(std::sqrt(viewport.minHz * viewport.maxHz),
               std::sqrt(20.0 * 20000.0), 1.0);
    viewport.zoom(0.5, 1000.0, 20.0, 24000.0);
    CHECK(viewport.minHz >= 20.0);
    CHECK(viewport.maxHz <= 24000.0);
    viewport.fit(20.0, 24000.0);
    CHECK_NEAR(viewport.minHz, 20.0, 1e-9);
    CHECK_NEAR(viewport.maxHz, 24000.0, 1e-9);
    viewport.zoom(10.0, 10.0, 20.0, 24000.0);
    CHECK(viewport.minHz >= 20.0);
    CHECK(viewport.maxHz > viewport.minHz);
}

VWTEST("peak computation produces min and max bins") {
    const Audio audio = ramp(1000);
    const std::vector<PeakBin> peaks =
        computePeaks(audio, 0, {100, 900}, 100);
    CHECK_EQ(peaks.size(), size_t{100});
    CHECK_NEAR(peaks.front().minimum, 0.1, 1e-3);
    CHECK_NEAR(peaks.front().maximum, 0.107, 1e-3);
    CHECK(peaks.back().maximum > 0.89f);
    const std::vector<PeakBin> negative =
        computePeaks(audio, 1, {100, 900}, 100);
    CHECK(negative.front().maximum <= 0.0f);
    CHECK(negative.back().minimum < -0.89f);
}

VWTEST("peak computation handles invalid input") {
    const Audio audio = ramp(100);
    CHECK(computePeaks(audio, 4, {0, 100}, 10).empty());
    CHECK(computePeaks(audio, 0, {0, 100}, 0).empty());
    const std::vector<PeakBin> empty =
        computePeaks(audio, 0, {60, 40}, 10);
    CHECK_EQ(empty.size(), size_t{10});
    for (const auto& peak : empty)
        CHECK_NEAR(peak.minimum, 0.0, 1e-9);
}

VWTEST("wavetable viewport clamps selection") {
    WavetableViewport viewport;
    viewport.selectedFrame = 5;
    viewport.clamp(4);
    CHECK_EQ(viewport.selectedFrame, 3);
    viewport.selectedFrame = -2;
    viewport.clamp(4);
    CHECK_EQ(viewport.selectedFrame, 0);
    viewport.selectedFrame = 2;
    viewport.clamp(0);
    CHECK_EQ(viewport.selectedFrame, 0);
}

VWTEST("timecode parser accepts seconds and colon forms") {
    double seconds = 0.0;
    CHECK(parseTimecode("12.5", seconds));
    CHECK_NEAR(seconds, 12.5, 1e-9);
    CHECK(parseTimecode("1:02.5", seconds));
    CHECK_NEAR(seconds, 62.5, 1e-9);
    CHECK(parseTimecode("1:02:03.5", seconds));
    CHECK_NEAR(seconds, 3723.5, 1e-9);
    CHECK(parseTimecode("12 s", seconds));
    CHECK_NEAR(seconds, 12.0, 1e-9);
    CHECK(!parseTimecode("", seconds));
    CHECK(!parseTimecode("-1", seconds));
    CHECK(!parseTimecode("1:bad", seconds));
    CHECK(!parseTimecode("1:2:3:4", seconds));
}
