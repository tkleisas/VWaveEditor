#pragma once

#include "AudioPlayer.h"
#include "vwave/JobSystem.h"
#include "services/UiDispatcher.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Timeline.h"

#include <SDL.h>

#include <functional>
#include <string>

namespace vwave::app {

struct ImageRenderParams {
    double duration = 4.0;
    double minHz = 50.0;
    double maxHz = 12000.0;
    double gamma = 1.0;
    double contrast = 1.0;
    double threshold = 0.0;
    int loops = 1;
    int iterations = 24;
};

class ImageController {
public:
    using StatusSink = std::function<void(const std::string&)>;
    using RenderSink = std::function<void(int64_t start)>;

    ImageController(SDL_Renderer* renderer, vwave::JobSystem& jobs,
                    UiDispatcher& dispatcher, vwave::Timeline& timeline,
                    AudioPlayer& player, StatusSink status,
                    RenderSink rendered);

    const vwave::ImageData& image() const { return m_image; }
    const std::string& path() const { return m_path; }
    SDL_Texture* texture() const { return m_texture; }
    bool valid() const { return m_image.valid(); }

    int frequencyScale() const { return m_frequencyScale; }
    int timeMapping() const { return m_timeMapping; }
    int phaseMode() const { return m_phaseMode; }
    int fftIndex() const { return m_fftIndex; }
    bool invert() const { return m_invert; }
    bool useAlpha() const { return m_useAlpha; }
    bool stereo() const { return m_stereo; }
    bool normalize() const { return m_normalize; }
    bool flipHorizontal() const { return m_flipHorizontal; }
    bool flipVertical() const { return m_flipVertical; }

    void cyclePhaseMode();
    void cycleFrequencyScale();
    void cycleTimeMapping();
    void cycleFftIndex();
    void toggleInvert() { m_invert = !m_invert; }
    void toggleUseAlpha() { m_useAlpha = !m_useAlpha; }
    void toggleStereo() { m_stereo = !m_stereo; }
    void toggleNormalize() { m_normalize = !m_normalize; }
    void toggleFlipHorizontal() { m_flipHorizontal = !m_flipHorizontal; }
    void toggleFlipVertical() { m_flipVertical = !m_flipVertical; }

    bool load(const std::string& path);
    void render(int64_t playhead, const ImageRenderParams& params);
    void cancel();
    void close();

private:
    SDL_Renderer* m_renderer;
    vwave::JobSystem& m_jobs;
    UiDispatcher& m_dispatcher;
    vwave::Timeline& m_timeline;
    AudioPlayer& m_player;
    StatusSink m_status;
    RenderSink m_rendered;
    vwave::ImageData m_image;
    std::string m_path;
    SDL_Texture* m_texture = nullptr;
    vwave::JobHandle m_job;
    uint64_t m_request = 0;
    int m_frequencyScale = 1;
    int m_timeMapping = 0;
    int m_phaseMode = 1;
    int m_fftIndex = 3;
    bool m_invert = false;
    bool m_useAlpha = true;
    bool m_stereo = false;
    bool m_normalize = true;
    bool m_flipHorizontal = false;
    bool m_flipVertical = false;
};

} // namespace vwave::app
