// spectrum_analyzer.hpp
//
// FFT-basierte Spektrumsanzeige, mit der man den Communication-Receiver
// fein auf den Zielton (z.B. 1000 Hz bei
// DCF77,...-Empfang über CW/AM-Produktdetektor) einstellen kann, dass die
// Amplitude des Zieltons im Peak liegt. Gibt zusätzlich eine simple
// ASCII-Balkenanzeige der Frequenzumgebung aus, damit man ohne grafisches
// Tool am Terminal abgleichen kann.
//
// Nutzt FFTW3 (libfftw3-dev). Reines Analyse-/Anzeige-Werkzeug, hat mit
// dem eigentlichen Bit-Decoding nichts zu tun (das übernimmt ToneDetector).

#pragma once

#include <vector>
#include <cstddef>
#include <fftw3.h>

namespace audio {

class SpectrumAnalyzer {
public:
    // fftSize: Anzahl Samples pro FFT (Zweierpotenz empfohlen, z.B. 4096)
    // sampleRate: Abtastrate der Eingangssamples
    SpectrumAnalyzer(size_t fftSize, unsigned sampleRate);
    ~SpectrumAnalyzer();

    // Verarbeitet einen Block Samples (akkumuliert intern bis fftSize
    // erreicht ist, danach wird automatisch eine neue FFT berechnet).
    // Gibt true zurück, wenn ein neues Spektrum berechnet wurde.
    bool feed(const std::vector<float>& samples);

    // Liefert die Frequenz (Hz) des stärksten Peaks im letzten Spektrum,
    // eingeschränkt auf [minHz, maxHz] - sinnvoll um nur im relevanten
    // Tonbereich (z.B. 300-3000 Hz NF) zu suchen.
    double peakFrequency(double minHz, double maxHz) const;

    // Magnitude (linear) bei einer bestimmten Frequenz, per lineare
    // Interpolation zwischen den beiden nächsten FFT-Bins.
    double magnitudeAt(double hz) const;

    // Gibt eine einfache Terminal-Balkenanzeige des Spektrums im Bereich
    // [minHz, maxHz] aus (stdout), zur manuellen Feinabstimmung des
    // Empfängers auf den Zielton.
    void printAsciiBars(double minHz, double maxHz, int numBuckets = 60) const;

private:
    size_t fftSize_;
    unsigned sampleRate_;

    std::vector<double> ringBuffer_;
    size_t writePos_ = 0;
    bool bufferFull_ = false;

    double* fftIn_ = nullptr;
    fftw_complex* fftOut_ = nullptr;
    fftw_plan plan_;

    std::vector<double> magnitude_; // letztes berechnetes Spektrum (Betrag)

    double binToHz(size_t bin) const;
};

} // namespace audio
