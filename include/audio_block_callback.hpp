// audio_block_callback.hpp
//
// Gemeinsamer Callback-Typ, den sowohl AlsaAudioSource (Soundkarte) als
// auch WavAudioSource (Datei-Wiedergabe) verwenden. In einer eigenen,
// bewusst abhaengigkeitsfreien Headerdatei, damit WavAudioSource nicht
// transitiv die ALSA-Header braucht (und umgekehrt spaeter z.B. ein
// Kommandozeilentool nur die Datei-Wiedergabe ohne ALSA-Verlinkung nutzen
// koennte).

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace audio {

// Callback-Signatur: bekommt einen Block Mono-Samples (normiert -1..1)
// sowie den Zeitpunkt (bei Soundkarte: Systemzeit in ms; bei Datei-
// Wiedergabe: synthetische, ab 0 hochlaufende Zeit in ms) des letzten
// Samples im Block.
using AudioBlockCallback = std::function<void(const std::vector<float>& samples, uint64_t blockEndMs)>;

} // namespace audio
