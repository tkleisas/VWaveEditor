#include "controllers/ImageController.h"

#include "services/FileService.h"
#include "ui/UiConstants.h"
#include "ui/UiFormat.h"
#include "vwave/ImageSpectrogram.h"
#include "vwave/Recipe.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <utility>

namespace vwave::app {

ImageController::ImageController(SDL_Renderer* renderer,
                                 vwave::JobSystem& jobs,
                                 UiDispatcher& dispatcher,
                                 vwave::Timeline& timeline,
                                 AudioPlayer& player, StatusSink status,
                                 RenderSink rendered, Sink loaded)
    : m_renderer(renderer),
      m_jobs(jobs),
      m_dispatcher(dispatcher),
      m_timeline(timeline),
      m_player(player),
      m_status(std::move(status)),
      m_rendered(std::move(rendered)),
      m_loaded(std::move(loaded)) {}

void ImageController::cyclePhaseMode() { m_phaseMode = (m_phaseMode + 1) % 5; }

void ImageController::cycleFrequencyScale() {
    m_frequencyScale = (m_frequencyScale + 1) % 2;
}

void ImageController::cycleTimeMapping() {
    m_timeMapping = (m_timeMapping + 1) % 3;
}

void ImageController::cycleFftIndex() {
    m_fftIndex = (m_fftIndex + 1) %
                 static_cast<int>(sizeof(ui::kFrameSizes) / sizeof(ui::kFrameSizes[0]));
}

bool ImageController::load(const std::string& path) {
    if (path.empty()) {
        m_status("Image path is empty");
        return false;
    }
    const uint64_t request = ++m_loadRequest;
    if (m_loadJob.valid()) m_loadJob.cancel();
    m_loading = true;
    m_status("Loading image...");
    m_loadJob = m_jobs.submit([this, request, path](vwave::JobControl& control) {
        vwave::ImageData image;
        std::string error;
        try {
            vwave::loadImageFile(path, image, &error);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
        if (control.cancelled()) return;
        m_dispatcher.post([this, request, image = std::move(image),
                           error = std::move(error), path]() mutable {
            if (request != m_loadRequest) return;
            m_loading = false;
            if (!image.valid()) {
                m_status(error.empty() ? "Image load failed" : error);
                return;
            }
            if (m_texture) {
                SDL_DestroyTexture(m_texture);
                m_texture = nullptr;
            }
            m_image = std::move(image);
            m_path = path;
            m_texture = SDL_CreateTexture(
                m_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                m_image.width, m_image.height);
            if (m_texture) {
                SDL_UpdateTexture(m_texture, nullptr, m_image.pixels.data(),
                                  m_image.width * 4);
                SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
            }
            m_loaded();
            m_status("Loaded image " + baseName(path));
        });
    });
    return true;
}

void ImageController::render(int64_t playhead,
                             const ImageRenderParams& params) {
    if (!m_image.valid()) {
        m_status("Load an image first");
        return;
    }
    vwave::ImageSpectrogramOptions options;
    options.sampleRate =
        m_timeline.sampleRate() > 0 ? m_timeline.sampleRate() : 48000;
    options.fftSize = ui::kFrameSizes[m_fftIndex];
    options.hopSize = std::max(64, options.fftSize / 4);
    options.duration = params.duration;
    options.minHz = params.minHz;
    options.maxHz = params.maxHz;
    options.frequencyScale =
        static_cast<vwave::ImageFrequencyScale>(m_frequencyScale);
    options.timeMapping = static_cast<vwave::ImageTimeMapping>(m_timeMapping);
    options.loops = params.loops;
    options.flipHorizontal = m_flipHorizontal;
    options.flipVertical = m_flipVertical;
    options.invert = m_invert;
    options.gamma = params.gamma;
    options.contrast = params.contrast;
    options.threshold = params.threshold;
    options.useAlpha = m_useAlpha;
    options.phaseMode = static_cast<vwave::ImagePhaseMode>(m_phaseMode);
    options.iterations = params.iterations;
    options.channels = m_stereo ? 2 : 1;
    options.normalize = m_normalize;
    options.peak = 0.9;

    const vwave::ImageSpectrogramOptions recipeOptions = options;
    const vwave::ImageData image = m_image;
    const std::string imagePath = m_path;
    const int64_t start = ui::clampFrame(playhead, m_timeline.lengthFrames());
    std::string name = std::filesystem::path(imagePath).stem().string();
    if (name.empty()) name = "Image";
    const uint64_t request = ++m_request;
    if (m_job.valid()) m_job.cancel();

    m_job = m_jobs.submit(
        [this, request, image, options, recipeOptions, imagePath, start,
         name](vwave::JobControl& control) mutable {
            options.cancelled = [&control] { return control.cancelled(); };
            options.progress = [&control](double value) {
                control.setProgress(value);
            };
            std::string error;
            vwave::ImageSpectrogramResult generated;
            try {
                generated = vwave::imageToSpectrogram(image, options, &error);
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            if (control.cancelled()) return;
            m_dispatcher.post(
                [this, request, generated = std::move(generated),
                 error = std::move(error), imagePath, recipeOptions, start,
                 name]() mutable {
                    if (request != m_request) return;
                    if (generated.audio.empty()) {
                        m_status(error.empty() ? "Image render failed" : error);
                        return;
                    }
                    m_player.stop();
                    std::string addError;
                    if (!m_timeline.addAudioTrack(name, generated.audio, start,
                                                  &addError)) {
                        m_status("Cannot add image track: " + addError);
                        return;
                    }
                    const int trackIndex = m_timeline.trackCount() - 1;
                    m_timeline.track(trackIndex).recipe =
                        vwave::makeImageTrackRecipe(imagePath, recipeOptions);
                    m_timeline.setActiveTrack(trackIndex);
                    m_rendered(start);
                    m_status("Rendered image to track (" +
                             ui::formatSeconds(generated.duration) + ")");
                });
        });
    m_status("Rendering image in background...");
}

void ImageController::cancel() {
    ++m_request;
    ++m_loadRequest;
    m_loading = false;
    if (m_job.valid()) m_job.cancel();
    if (m_loadJob.valid()) m_loadJob.cancel();
}

void ImageController::close() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
}

} // namespace vwave::app
