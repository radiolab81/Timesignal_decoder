cmake_minimum_required(VERSION 3.16)
project(Timesignal_receiver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -O2)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(FFTW3 REQUIRED fftw3)

add_executable(Timesignal_receiver
    src/main.cpp
    src/audio_source.cpp
    src/wav_audio_source.cpp
    src/iq_wav_audio_source.cpp
    src/spectrum_analyzer.cpp
    src/tone_envelope_detector.cpp
    src/dcf77_decoder.cpp
    src/msf_decoder.cpp
    src/jjy_decoder.cpp
    src/pl225_decoder.cpp
)

target_include_directories(Timesignal_receiver PRIVATE
    include
    ${ALSA_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
)

target_link_libraries(Timesignal_receiver PRIVATE
    ${ALSA_LIBRARIES}
    ${FFTW3_LIBRARIES}
    m
)

target_compile_options(Timesignal_receiver PRIVATE ${ALSA_CFLAGS_OTHER} ${FFTW3_CFLAGS_OTHER})

# Optionaler Offline-Test der MSF-Zustandsmaschine (kein ALSA/FFTW3 noetig,
# nutzt nur einen synthetisch erzeugten Minutenrahmen). Mit
# 'ctest' bzw. direkt './test_msf_synthetic' ausfuehrbar.
enable_testing()
add_executable(test_msf_synthetic
    test/test_msf_synthetic.cpp
    src/msf_decoder.cpp
)
target_include_directories(test_msf_synthetic PRIVATE include)
add_test(NAME msf_synthetic COMMAND test_msf_synthetic)

add_executable(test_jjy_synthetic
    test/test_jjy_synthetic.cpp
    src/jjy_decoder.cpp
)
target_include_directories(test_jjy_synthetic PRIVATE include)
add_test(NAME jjy_synthetic COMMAND test_jjy_synthetic)

add_executable(test_pl225_synthetic
    test/test_pl225_synthetic.cpp
    src/pl225_decoder.cpp
)
target_include_directories(test_pl225_synthetic PRIVATE include)
add_test(NAME pl225_synthetic COMMAND test_pl225_synthetic)

# Isolierter Test der eigenen RS(15,9) GF(16)-Implementierung
# (reed_solomon_gf16.hpp) - unabhaengig vom Rest des PL225-Decoders.
add_executable(test_reed_solomon
    test/test_reed_solomon.cpp
)
target_include_directories(test_reed_solomon PRIVATE include)
add_test(NAME reed_solomon COMMAND test_reed_solomon)
