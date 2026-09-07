// iq_block_callback.hpp
//
// Callback-Typ fuer IQ-("komplexe") Audioquellen - analog zu
// audio_block_callback.hpp, aber mit std::complex<float> Samples statt
// reeller Mono-Samples. Wird von IqWavAudioSource (und optional einer
// spaeteren IqAlsaAudioSource fuer direkt-abtastende SDR-Dongles, die IQ
// als Stereo-Sound ausgeben) verwendet.

#pragma once

#include <complex>
#include <cstdint>
#include <functional>
#include <vector>

namespace audio {

using IqBlockCallback = std::function<void(const std::vector<std::complex<float>>& samples, uint64_t blockEndMs)>;

} // namespace audio
