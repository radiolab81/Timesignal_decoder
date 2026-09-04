// tone_envelope_detector.cpp
#include "tone_envelope_detector.hpp"

#include <cmath>
#include <algorithm>
#include <numeric>

namespace audio {

ToneEnvelopeDetector::ToneEnvelopeDetector(double targetHz, unsigned sampleRate, double blockMs)
    : targetHz_(targetHz), sampleRate_(sampleRate), msPerBlock_(blockMs) {
    blockSize_ = static_cast<size_t>(sampleRate_ * blockMs / 1000.0);
    if (blockSize_ < 8) blockSize_ = 8;
    blockBuf_.resize(blockSize_);

    // Goertzel-Koeffizient für die Zielfrequenz
    double k = std::round(static_cast<double>(blockSize_) * targetHz_ / sampleRate_);
    double omega = (2.0 * M_PI * k) / static_cast<double>(blockSize_);
    coeff_ = 2.0 * std::cos(omega);

    // Adaptive Schwelle über ein Fenster von ca. 2 Sekunden
    recentWindowBlocks_ = static_cast<size_t>(2000.0 / blockMs);
}

double ToneEnvelopeDetector::goertzelMagnitude(const float* data, size_t n) const {
    double s_prev = 0.0, s_prev2 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double s = static_cast<double>(data[i]) + coeff_ * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }
    double real = s_prev - s_prev2 * std::cos(2.0 * M_PI * std::round(n * targetHz_ / sampleRate_) / n);
    double imag = s_prev2 * std::sin(2.0 * M_PI * std::round(n * targetHz_ / sampleRate_) / n);
    return std::sqrt(real * real + imag * imag) / (n / 2.0);
}

void ToneEnvelopeDetector::process(const std::vector<float>& samples, uint64_t blockEndMs) {
    // Zeitstempel des ersten Samples in diesem Audio-Block (für Interpolation
    // der Flankenzeit innerhalb des Blocks).
    double blockDurationMs = 1000.0 * samples.size() / sampleRate_;
    blockStartMs_ = blockEndMs - static_cast<uint64_t>(blockDurationMs);

    for (size_t i = 0; i < samples.size(); ++i) {
        blockBuf_[blockFill_++] = samples[i];
        if (blockFill_ == blockSize_) {
            double level = goertzelMagnitude(blockBuf_.data(), blockSize_);

            // Zeitstempel der Blockmitte für die Level-Auswertung
            double frac = static_cast<double>(i + 1) / samples.size();
            uint64_t t = blockStartMs_ + static_cast<uint64_t>(frac * blockDurationMs);

            handleBlockLevel(level, t);
            blockFill_ = 0;
        }
    }
}

void ToneEnvelopeDetector::handleBlockLevel(double level, uint64_t sampleTimeMs) {
    // Exponentiell geglätteter Pegel zur Rauschunterdrückung
    smoothedLevel_ = emaAlpha_ * level + (1.0 - emaAlpha_) * smoothedLevel_;

    recentLevels_.push_back(smoothedLevel_);
    if (recentLevels_.size() > recentWindowBlocks_) recentLevels_.pop_front();

    double minLvl = *std::min_element(recentLevels_.begin(), recentLevels_.end());
    double maxLvl = *std::max_element(recentLevels_.begin(), recentLevels_.end());

    // Wenn (noch) kein hinreichender Kontrast vorhanden ist (z.B. Startphase,
    // Empfänger verstimmt), keine Flankenerkennung versuchen.
    if (maxLvl < 1e-6 || (maxLvl - minLvl) < 0.15 * maxLvl) {
        return;
    }

    // Schwelle mittig zwischen Minimum und Maximum der letzten ~2s,
    // mit leichter Hysterese um Flattern um die Schwelle zu vermeiden.
    double threshold = minLvl + 0.5 * (maxLvl - minLvl);
    double hysteresis = 0.08 * (maxLvl - minLvl);

    if (!belowThreshold_ && smoothedLevel_ < (threshold - hysteresis)) {
        // Fallende Flanke: Trägerabsenkung beginnt
        belowThreshold_ = true;
        lowStartMs_ = sampleTimeMs;
    } else if (belowThreshold_ && smoothedLevel_ > (threshold + hysteresis)) {
        // Steigende Flanke: Trägerabsenkung (Dip) endet.
        belowThreshold_ = false;
        double durationMs = static_cast<double>(sampleTimeMs - lowStartMs_);

        double sinceLastDipStartMs = 0.0;
        if (haveLastDipStart_) {
            sinceLastDipStartMs = static_cast<double>(lowStartMs_ - lastDipStartMs_);
        }
        lastDipStartMs_ = lowStartMs_;
        haveLastDipStart_ = true;

        if (decoder_) {
            timesignal::CarrierDipEvent ev{lowStartMs_, sampleTimeMs, durationMs, sinceLastDipStartMs};
            decoder_->onCarrierDip(ev);
        }
    }
}

} // namespace audio
