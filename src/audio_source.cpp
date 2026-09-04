// audio_source.cpp
#include "audio_source.hpp"

#include <stdexcept>
#include <chrono>
#include <cstring>

namespace audio {

static uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

AlsaAudioSource::AlsaAudioSource(std::string device, unsigned sampleRate, unsigned blockSize)
    : device_(std::move(device)), sampleRate_(sampleRate), blockSize_(blockSize) {}

AlsaAudioSource::~AlsaAudioSource() {
    if (pcmHandle_) {
        snd_pcm_close(pcmHandle_);
    }
}

void AlsaAudioSource::open() {
    int err = snd_pcm_open(&pcmHandle_, device_.c_str(), SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        throw std::runtime_error("snd_pcm_open fehlgeschlagen fuer '" + device_ +
                                  "': " + std::string(snd_strerror(err)));
    }

    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);
    snd_pcm_hw_params_any(pcmHandle_, hwParams);

    snd_pcm_hw_params_set_access(pcmHandle_, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcmHandle_, hwParams, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcmHandle_, hwParams, 1); // Mono

    unsigned rate = sampleRate_;
    int dir = 0;
    snd_pcm_hw_params_set_rate_near(pcmHandle_, hwParams, &rate, &dir);
    sampleRate_ = rate; // tatsächlich erreichte Rate übernehmen

    // Periodengröße grob an blockSize_ orientieren, ALSA darf abweichen.
    snd_pcm_uframes_t period = blockSize_;
    snd_pcm_hw_params_set_period_size_near(pcmHandle_, hwParams, &period, &dir);

    err = snd_pcm_hw_params(pcmHandle_, hwParams);
    if (err < 0) {
        throw std::runtime_error("snd_pcm_hw_params fehlgeschlagen: " +
                                  std::string(snd_strerror(err)));
    }

    err = snd_pcm_prepare(pcmHandle_);
    if (err < 0) {
        throw std::runtime_error("snd_pcm_prepare fehlgeschlagen: " +
                                  std::string(snd_strerror(err)));
    }
}

void AlsaAudioSource::stop() {
    running_ = false;
}

void AlsaAudioSource::captureLoop(const AudioBlockCallback& cb) {
    std::vector<int16_t> raw(blockSize_);
    std::vector<float> normalized(blockSize_);

    while (running_) {
        snd_pcm_sframes_t frames = snd_pcm_readi(pcmHandle_, raw.data(), blockSize_);

        if (frames == -EPIPE) {
            // Buffer-Overrun: Stream zurücksetzen und weitermachen.
            snd_pcm_prepare(pcmHandle_);
            continue;
        } else if (frames < 0) {
            frames = snd_pcm_recover(pcmHandle_, static_cast<int>(frames), 1);
            if (frames < 0) {
                throw std::runtime_error("snd_pcm_readi nicht erholbarer Fehler: " +
                                          std::string(snd_strerror(static_cast<int>(frames))));
            }
            continue;
        }

        // S16 -> float normiert [-1, 1]
        for (snd_pcm_sframes_t i = 0; i < frames; ++i) {
            normalized[i] = static_cast<float>(raw[i]) / 32768.0f;
        }
        normalized.resize(frames);

        cb(normalized, nowMs());

        normalized.resize(blockSize_);
    }
}

} // namespace audio
