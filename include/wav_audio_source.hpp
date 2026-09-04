// wav_audio_source.hpp
//
// Liest eine WAV-Datei (PCM, 16 Bit, mono oder stereo) und liefert sie
// blockweise ueber denselben Callback-Typ wie AlsaAudioSource
// (audio::AudioBlockCallback) - main.cpp kann dadurch zwischen Soundkarte
// und Datei umschalten, ohne den restlichen Signalpfad (Spektrum,
// ToneEnvelopeDetector, Decoder) anzupassen.
//
// Nuetzlich zum Debuggen/Nachanalysieren aufgezeichneter Empfangssitzungen
// sowie fuer automatisierte Tests mit realen Aufnahmen.
//
// Einschraenkungen (bewusst einfach gehalten, siehe README):
//   - Nur unkomprimiertes PCM (Format-Tag 1), 16 Bit Sample-Tiefe.
//   - Stereo-Dateien werden durch Mittelung beider Kanaele auf Mono
//     heruntergemischt (fuer Zeitzeichenempfang irrelevant, welcher
//     Kanal - der Ton liegt i.d.R. auf beiden gleich).
//   - Es wird NICHT auf die Zielabtastrate resampled; die Datei wird mit
//     ihrer eigenen Abtastrate verarbeitet (ToneEnvelopeDetector erhaelt
//     die tatsaechliche Rate aus sampleRate(), passt seine Goertzel-
//     Koeffizienten also automatisch an).

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "audio_block_callback.hpp"

namespace audio {

class WavAudioSource {
public:
    explicit WavAudioSource(std::string path);

    // Liest und validiert den WAV-Header. Wirft std::runtime_error bei
    // nicht unterstuetzten Formaten (Kompression, >16 Bit, korrupte Datei).
    void open();

    // Gibt die Datei blockweise (gleiche Blockgroesse wie bei ALSA ueblich,
    // hier an blockSizeSamples uebergeben) an den Callback weiter. Der
    // Zeitstempel je Block wird synthetisch aus der Abtastrate berechnet
    // (0ms fuer das Dateiende des ersten Blocks, danach fortlaufend) -
    // absolute Systemzeit ist fuer eine Datei ohne Bedeutung, nur die
    // relativen Abstaende zaehlen fuer die Bit-Timing-Auswertung.
    void playbackLoop(unsigned blockSizeSamples, const AudioBlockCallback& cb);

    unsigned sampleRate() const { return sampleRate_; }
    uint64_t durationMs() const;

private:
    std::string path_;
    unsigned sampleRate_ = 0;
    unsigned channels_ = 1;
    unsigned bitsPerSample_ = 16;
    std::vector<int16_t> samplesInterleaved_;
    uint64_t totalFrames_ = 0;
};

} // namespace audio
