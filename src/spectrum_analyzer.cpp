// spectrum_analyzer.cpp
#include "spectrum_analyzer.hpp"

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace audio {

SpectrumAnalyzer::SpectrumAnalyzer(size_t fftSize, unsigned sampleRate)
    : fftSize_(fftSize), sampleRate_(sampleRate), ringBuffer_(fftSize, 0.0) {
    fftIn_  = fftw_alloc_real(fftSize_);
    fftOut_ = fftw_alloc_complex(fftSize_ / 2 + 1);
    plan_   = fftw_plan_dft_r2c_1d(static_cast<int>(fftSize_), fftIn_, fftOut_, FFTW_MEASURE);
    magnitude_.resize(fftSize_ / 2 + 1, 0.0);
}

SpectrumAnalyzer::~SpectrumAnalyzer() {
    fftw_destroy_plan(plan_);
    fftw_free(fftIn_);
    fftw_free(fftOut_);
}

bool SpectrumAnalyzer::feed(const std::vector<float>& samples) {
    bool computed = false;
    for (float s : samples) {
        ringBuffer_[writePos_] = static_cast<double>(s);
        writePos_ = (writePos_ + 1) % fftSize_;
        if (writePos_ == 0) bufferFull_ = true;

        // Sobald der Ringpuffer einmal komplett gefüllt wurde, bei jedem
        // vollen Durchlauf ein neues Spektrum berechnen (kein Overlap,
        // für eine Abstimm-Anzeige ausreichend).
        if (writePos_ == 0 && bufferFull_) {
            // Hann-Fenster gegen Spectral Leakage
            for (size_t n = 0; n < fftSize_; ++n) {
                double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * n / (fftSize_ - 1));
                fftIn_[n] = ringBuffer_[n] * w;
            }
            fftw_execute(plan_);
            for (size_t k = 0; k < magnitude_.size(); ++k) {
                double re = fftOut_[k][0];
                double im = fftOut_[k][1];
                magnitude_[k] = std::sqrt(re * re + im * im);
            }
            computed = true;
        }
    }
    return computed;
}

double SpectrumAnalyzer::binToHz(size_t bin) const {
    return static_cast<double>(bin) * sampleRate_ / static_cast<double>(fftSize_);
}

double SpectrumAnalyzer::peakFrequency(double minHz, double maxHz) const {
    size_t lo = static_cast<size_t>(std::max(0.0, minHz * fftSize_ / sampleRate_));
    size_t hi = static_cast<size_t>(std::min(static_cast<double>(magnitude_.size() - 1),
                                              maxHz * fftSize_ / sampleRate_));
    size_t bestBin = lo;
    double bestMag = -1.0;
    for (size_t k = lo; k <= hi; ++k) {
        if (magnitude_[k] > bestMag) {
            bestMag = magnitude_[k];
            bestBin = k;
        }
    }
    return binToHz(bestBin);
}

double SpectrumAnalyzer::magnitudeAt(double hz) const {
    double binF = hz * fftSize_ / sampleRate_;
    size_t bin0 = static_cast<size_t>(std::floor(binF));
    size_t bin1 = std::min(bin0 + 1, magnitude_.size() - 1);
    double frac = binF - bin0;
    if (bin0 >= magnitude_.size()) return 0.0;
    return magnitude_[bin0] * (1.0 - frac) + magnitude_[bin1] * frac;
}

void SpectrumAnalyzer::printAsciiBars(double minHz, double maxHz, int numBuckets) const {
    // Frequenzbereich in numBuckets Terminal-Zeilen zusammenfassen.
    double maxMag = 1e-9;
    std::vector<double> buckets(numBuckets, 0.0);
    for (int b = 0; b < numBuckets; ++b) {
        double f0 = minHz + (maxHz - minHz) * b / numBuckets;
        double f1 = minHz + (maxHz - minHz) * (b + 1) / numBuckets;
        double m = std::max(magnitudeAt(f0), magnitudeAt((f0 + f1) / 2.0));
        m = std::max(m, magnitudeAt(f1));
        buckets[b] = m;
        maxMag = std::max(maxMag, m);
    }

    std::printf("\n--- Spektrum %.0f-%.0f Hz (Peak: %.1f Hz) ---\n",
                minHz, maxHz, peakFrequency(minHz, maxHz));
    for (int b = 0; b < numBuckets; ++b) {
        double f = minHz + (maxHz - minHz) * (b + 0.5) / numBuckets;
        int barLen = static_cast<int>(40.0 * buckets[b] / maxMag);
        std::printf("%6.0f Hz | %.*s\n", f, barLen, "########################################");
    }
}

} // namespace audio
