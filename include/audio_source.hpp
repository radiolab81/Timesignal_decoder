// audio_source.hpp
//
// Kapselt die ALSA-Soundkarten-Aufnahme. Liefert Mono-Float-Samples
// blockweise über einen Callback. Bewusst simpel gehalten (blocking read),
// da für Zeitzeichenauswertung keine Echtzeit-Latenzansprüche bestehen -
// ein paar hundert ms Puffer sind unkritisch.

#pragma once

#include <alsa/asoundlib.h>
#include <string>
#include <vector>
#include <cstdint>
#include "audio_block_callback.hpp"

namespace audio {

class AlsaAudioSource {
public:
    // device: z.B. "default", "hw:1,0", "plughw:CARD=USB,DEV=0"
    // sampleRate: z.B. 48000
    // blockSize: Anzahl Samples pro Callback-Aufruf (z.B. 480 = 10ms bei 48kHz)
    AlsaAudioSource(std::string device, unsigned sampleRate, unsigned blockSize);
    ~AlsaAudioSource();

    // Öffnet das Gerät und konfiguriert es (mono, 16bit signed, interleaved).
    // Wirft std::runtime_error bei Fehlern.
    void open();

    // Blockierende Aufnahme-Schleife. Ruft für jeden vollen Block `cb` auf.
    // Läuft bis running() extern auf false gesetzt wird (siehe stop()).
    void captureLoop(const AudioBlockCallback& cb);

    // Signalisiert der captureLoop, dass sie beenden soll (threadsicher genug
    // für unseren Zweck: einfache atomare Flag-Abfrage zwischen ALSA reads).
    void stop();

    unsigned sampleRate() const { return sampleRate_; }

private:
    std::string device_;
    unsigned sampleRate_;
    unsigned blockSize_;
    snd_pcm_t* pcmHandle_ = nullptr;
    volatile bool running_ = true;
};

} // namespace audio
