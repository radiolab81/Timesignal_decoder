// iq_wav_audio_source.hpp
//
// Liest eine stereo WAV-Datei (PCM, 16 Bit) als IQ-Datenstrom ein: linker
// Kanal = I, rechter Kanal = Q. Genau das Format, das SDR-Programme (SDR#,
// GQRX, GNU Radio "wav_sink" mit complex->float Interleave, etc.)
// typischerweise als "IQ-Aufnahme" exportieren.
//
// Wird fuer den PL225 (e-CzasPL) Decoder benoetigt: nur mit echten IQ-Daten
// laesst sich die Phasenmodulation des polnischen 225kHz-Senders zweifelsfrei
// rekonstruieren (siehe pl225_decoder.hpp fuer die ausfuehrliche Begruendung,
// warum eine reine Hüllkurven-AM-Aufnahme dafuer nicht ausreicht).

#pragma once

#include <complex>
#include <cstdint>
#include <string>
#include <vector>
#include "iq_block_callback.hpp"

namespace audio {

class IqWavAudioSource {
public:
    explicit IqWavAudioSource(std::string path);

    // Liest und validiert den WAV-Header (muss stereo, PCM, 16 Bit sein).
    void open();

    // Gibt die Datei blockweise als komplexe Samples an den Callback weiter.
    void playbackLoop(unsigned blockSizeSamples, const IqBlockCallback& cb);

    unsigned sampleRate() const { return sampleRate_; }
    uint64_t durationMs() const;

private:
    std::string path_;
    unsigned sampleRate_ = 0;
    unsigned channels_ = 2;
    unsigned bitsPerSample_ = 16;
    std::vector<int16_t> samplesInterleaved_;
    uint64_t totalFrames_ = 0;
};

} // namespace audio
