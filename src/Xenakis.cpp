#include "vwave/Xenakis.h"

#include <xen/engine.h>
#include <xen/page_model.h>
#include <xen/upic.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

namespace vwave {

namespace {

constexpr int kRenderBlock = 512;

} // namespace

bool isXenakisPageFile(const std::string& path) {
    if (!std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path))
        return false;
    const std::string extension =
        std::filesystem::path(path).extension().string();
    if (extension == ".xenpage") return true;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[8] = {};
    in.read(magic, 7);
    return std::string(magic, static_cast<size_t>(in.gcount())) == "XENPAGE";
}

Audio renderXenakisPage(const std::string& pageText,
                        const XenakisRenderOptions& options,
                        std::string* error) {
    if (error) error->clear();

    xen::Page page;
    if (!xen::deserializePage(pageText, page)) {
        if (error) *error = "invalid Xenakis page";
        return {};
    }
    if (page.arcs.empty()) {
        if (error) *error = "Xenakis page has no arcs";
        return {};
    }

    const int sampleRate = options.sampleRate > 0 ? options.sampleRate : 48000;
    const double duration =
        options.duration > 0.0 ? options.duration : page.duration;
    if (!std::isfinite(duration) || duration <= 0.0) {
        if (error) *error = "invalid Xenakis render duration";
        return {};
    }

    const int64_t frames = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(duration * sampleRate)));
    Audio out(1, frames, sampleRate);

    xen::Engine engine;
    engine.prepare(static_cast<double>(sampleRate), kRenderBlock);
    engine.setPage(std::make_shared<const xen::Page>(std::move(page)));
    engine.setTransport(true, options.loop, 1.0, false);
    engine.setGains(0.0f, 0.0f, 0.0f);
    engine.setPlayhead(0.0);

    std::vector<float> left(kRenderBlock);
    std::vector<float> right(kRenderBlock);
    float* destination = out.channel(0);
    const float gain = static_cast<float>(std::max(0.0, options.gain));

    int64_t written = 0;
    while (written < frames) {
        const int count = static_cast<int>(
            std::min<int64_t>(kRenderBlock, frames - written));
        engine.render(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
            destination[written + i] = left[static_cast<size_t>(i)] * gain;
        written += count;
    }

    if (options.normalize) {
        float peak = 0.0f;
        for (int64_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::fabs(destination[i]));
        const float target =
            static_cast<float>(std::clamp(options.peak, 0.0, 1.0));
        if (peak > 1e-6f && target > 0.0f) {
            const float scale = target / peak;
            for (int64_t i = 0; i < frames; ++i)
                destination[i] *= scale;
        }
    }

    return out;
}

Audio renderXenakisPageFile(const std::string& path,
                            const XenakisRenderOptions& options,
                            std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open Xenakis page: " + path;
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return renderXenakisPage(buffer.str(), options, error);
}

} // namespace vwave
