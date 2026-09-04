// tone_envelope_detector.hpp
//
// Misst mit einem Goertzel-Filter (schmalbandiger als eine FFT-Bin-Analyse,
// sehr effizient für genau EINE Zielfrequenz) fortlaufend die Amplitude des
// Zieltons. Zeitzeichensender wie DCF77 senden pro Sekunde eine kurze
// Amplitudenabsenkung des Trägers (100ms = Bit 0, 200ms = Bit 1). Über den
// NF-Ausgang eines Kommunikationsempfängers (AM/CW-Produktdetektor auf den
// Zielton gemischt) erscheint das als kurze Pegelabsenkung des Tons.
//
// Dieses Modul erkennt die fallende/steigende Flanke dieser Absenkung per
// adaptiver Schwelle und meldet pro erkannter steigender Flanke ein
// SecondMarkEvent an den angeschlossenen ITimeSignalDecoder - unabhängig
// vom konkreten Protokoll (DCF77/MSF/JJY teilen sich dieses Modul, nur die
// Zielfrequenz und ggf. Trägerpolarität unterscheiden sich).

#pragma once

#include <vector>
#include <cstdint>
#include <deque>
#include "time_signal_decoder.hpp"

namespace audio {

class ToneEnvelopeDetector {
public:
    // targetHz: Zielton-Frequenz (z.B. 1000 Hz NF-Beat-Ton)
    // sampleRate: Abtastrate der Eingangssamples
    // blockMs: Blockgröße in ms, in der die Goertzel-Amplitude gemessen wird
    //          (10ms ist ein guter Kompromiss aus Zeitauflösung und Rechenlast)
    ToneEnvelopeDetector(double targetHz, unsigned sampleRate, double blockMs = 10.0);

    // Registriert den Decoder, der SecondMarkEvents empfangen soll.
    void attachDecoder(timesignal::ITimeSignalDecoder* decoder) { decoder_ = decoder; }

    // Verarbeitet einen Block Audio-Samples. blockEndMs ist der Zeitstempel
    // (Systemzeit ms) des letzten Samples in diesem Block, wird für die
    // präzise Zeitmarkierung der Flanken verwendet.
    void process(const std::vector<float>& samples, uint64_t blockEndMs);

    // Aktuelle geschätzte Trägeramplitude (für Diagnose/Pegelanzeige).
    double currentLevel() const { return smoothedLevel_; }

private:
    double targetHz_;
    unsigned sampleRate_;
    size_t blockSize_;       // Samples pro Goertzel-Block
    double msPerBlock_;

    // Goertzel-Koeffizienten
    double coeff_ = 0.0;
    std::vector<float> blockBuf_;
    size_t blockFill_ = 0;

    // Hüllkurven-Nachverarbeitung
    double smoothedLevel_ = 0.0;
    double emaAlpha_ = 0.3;

    // Adaptive Schwelle: gleitendes Minimum/Maximum der letzten ~2s
    std::deque<double> recentLevels_;
    size_t recentWindowBlocks_;

    // Zustand der Flankenerkennung
    bool belowThreshold_ = false;
    uint64_t lowStartMs_ = 0;
    uint64_t lastDipStartMs_ = 0;
    bool haveLastDipStart_ = false;

    uint64_t blockStartMs_ = 0; // wird aus blockEndMs zurückgerechnet

    timesignal::ITimeSignalDecoder* decoder_ = nullptr;

    // Berechnet die Goertzel-Magnitude für den aktuellen Block.
    double goertzelMagnitude(const float* data, size_t n) const;

    void handleBlockLevel(double level, uint64_t sampleTimeMs);
};

} // namespace audio
