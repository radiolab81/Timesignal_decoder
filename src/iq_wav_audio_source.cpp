// iq_wav_audio_source.cpp
#include "iq_wav_audio_source.hpp"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace audio {

namespace {
uint32_t readU32LE(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16LE(const unsigned char* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
} // namespace

IqWavAudioSource::IqWavAudioSource(std::string path) : path_(std::move(path)) {}

void IqWavAudioSource::open() {
    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        throw std::runtime_error("IQ-WAV-Datei konnte nicht geoeffnet werden: " + path_);
    }

    unsigned char riffHeader[12];
    f.read(reinterpret_cast<char*>(riffHeader), 12);
    if (!f || std::memcmp(riffHeader, "RIFF", 4) != 0 || std::memcmp(riffHeader + 8, "WAVE", 4) != 0) {
        throw std::runtime_error("Datei ist keine gueltige RIFF/WAVE-Datei: " + path_);
    }

    bool haveFmt = false;
    uint16_t formatTag = 0;
    uint32_t dataBytes = 0;
    std::streampos dataPos{};

    while (f) {
        unsigned char chunkHeader[8];
        f.read(reinterpret_cast<char*>(chunkHeader), 8);
        if (!f) break;
        char chunkId[5] = {0};
        std::memcpy(chunkId, chunkHeader, 4);
        uint32_t chunkSize = readU32LE(chunkHeader + 4);

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            std::vector<unsigned char> fmt(chunkSize);
            f.read(reinterpret_cast<char*>(fmt.data()), chunkSize);
            if (chunkSize < 16) {
                throw std::runtime_error("WAV fmt-Chunk zu klein/ungueltig: " + path_);
            }
            formatTag = readU16LE(&fmt[0]);
            channels_ = readU16LE(&fmt[2]);
            sampleRate_ = readU32LE(&fmt[4]);
            bitsPerSample_ = readU16LE(&fmt[14]);
            haveFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            dataBytes = chunkSize;
            dataPos = f.tellg();
            f.seekg(chunkSize, std::ios::cur);
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 != 0) {
            f.seekg(1, std::ios::cur);
        }
    }

    if (!haveFmt || dataBytes == 0) {
        throw std::runtime_error("WAV-Datei enthaelt keinen fmt- oder data-Chunk: " + path_);
    }
    if (formatTag != 1) {
        throw std::runtime_error("Nur unkomprimiertes PCM wird unterstuetzt (Format-Tag " +
                                  std::to_string(formatTag) + " in " + path_ + ")");
    }
    if (bitsPerSample_ != 16) {
        throw std::runtime_error("Nur 16 Bit PCM wird unterstuetzt (Datei hat " +
                                  std::to_string(bitsPerSample_) + " Bit): " + path_);
    }
    if (channels_ != 2) {
        throw std::runtime_error("IQ-Datei muss genau 2 Kanaele haben (I=links, Q=rechts), "
                                  "diese Datei hat " + std::to_string(channels_) + ": " + path_);
    }

    f.clear();
    f.seekg(dataPos);
    size_t numSamplesTotal = dataBytes / sizeof(int16_t);
    samplesInterleaved_.resize(numSamplesTotal);
    f.read(reinterpret_cast<char*>(samplesInterleaved_.data()),
           static_cast<std::streamsize>(numSamplesTotal * sizeof(int16_t)));
    if (!f && !f.eof()) {
        throw std::runtime_error("Fehler beim Lesen der WAV-Nutzdaten: " + path_);
    }

    totalFrames_ = numSamplesTotal / channels_;
}

uint64_t IqWavAudioSource::durationMs() const {
    if (sampleRate_ == 0) return 0;
    return static_cast<uint64_t>(1000.0 * totalFrames_ / sampleRate_);
}

void IqWavAudioSource::playbackLoop(unsigned blockSizeSamples, const IqBlockCallback& cb) {
    std::vector<std::complex<float>> block(blockSizeSamples);
    uint64_t framesProcessed = 0;

    while (framesProcessed < totalFrames_) {
        unsigned n = static_cast<unsigned>(
            std::min<uint64_t>(blockSizeSamples, totalFrames_ - framesProcessed));

        for (unsigned i = 0; i < n; ++i) {
            uint64_t frameIdx = framesProcessed + i;
            float I = static_cast<float>(samplesInterleaved_[frameIdx * 2]) / 32768.0f;
            float Q = static_cast<float>(samplesInterleaved_[frameIdx * 2 + 1]) / 32768.0f;
            block[i] = std::complex<float>(I, Q);
        }
        block.resize(n);

        framesProcessed += n;
        uint64_t blockEndMs = static_cast<uint64_t>(1000.0 * framesProcessed / sampleRate_);
        cb(block, blockEndMs);

        block.resize(blockSizeSamples);
    }
}

} // namespace audio
