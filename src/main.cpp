// main.cpp
//
// Verdrahtet die Module zu einer lauffähigen Anwendung:
//
//   Soundkarte (ALSA) --> SpectrumAnalyzer (Feinabstimm-Anzeige, optional)
//                     \-> ToneEnvelopeDetector (Goertzel bei Zielfrequenz)
//                            --> ITimeSignalDecoder (aktuell: Dcf77Decoder)
//                                   --> IDecodedTimeSink (Konsolenausgabe)
//
// Weitere Sender (MSF, JJY, WWVB, ...) werden integriert, indem man eine
// weitere Klasse gegen ITimeSignalDecoder implementiert und sie hier
// zusaetzlich an einen (ggf. zweiten) ToneEnvelopeDetector haengt - der
// gesamte Audio-/Spektrum-Pfad bleibt unveraendert.
//
// Aufruf-Beispiele:
//   ./Timesignal_receiver --device hw:1,0 --tone 1000 --spectrum
//   ./Timesignal_receiver --device default --tone 750
//   ./Timesignal_receiver --wav-file aufnahme.wav --tone 500 --protocol jjy
//
// Praxis-Hinweis zur Feinabstimmung:
//   Den Communication-Receiver auf 77.5 kHz (DCF77) im CW/SSB-Modus
//   einstellen, sodass am NF-Ausgang ein Ton (Beat-Ton, typ. 500-1500 Hz)
//   entsteht. Mit --spectrum die ASCII-Spektrumsanzeige einschalten und
//   den Receiver-BFO/Feinabstimmung so nachjustieren, dass der Peak exakt
//   auf dem mit --tone angegebenen Wert liegt (Toleranz: wenige Hz, sonst
//   verschlechtert sich die Goertzel-Trennschaerfe gegenueber Rauschen).

#include "audio_source.hpp"
#include "wav_audio_source.hpp"
#include "spectrum_analyzer.hpp"
#include "tone_envelope_detector.hpp"
#include "dcf77_decoder.hpp"
#include "msf_decoder.hpp"
#include "jjy_decoder.hpp"
#include "pl225_decoder.hpp"
#include "iq_wav_audio_source.hpp"
#include "mono_to_iq_downconverter.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <atomic>

namespace {

std::atomic<bool> g_stopRequested{false};

void signalHandler(int) {
    g_stopRequested = true;
}

// Konsolen-Sink: gibt dekodierte Minuten und Fehler lesbar aus.
class ConsoleTimeSink : public timesignal::IDecodedTimeSink {
public:
    void onDecodedTime(const timesignal::DecodedTime& t) override {
        // ACHTUNG: die Wochentagszaehlung unterscheidet sich je Protokoll -
        // DCF77 zaehlt 1=Montag..7=Sonntag, MSF und JJY zaehlen 0=Sonntag..6=Samstag.
        // Hier daher je nach sourceTag passend beschriften statt einen
        // gemeinsamen (falschen) Wochentagsnamen zu unterstellen.
        static const char* dcf77Days[] = {"", "Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"};
        static const char* sundayFirstDays[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
        const char* wd = "?";
        if ((t.sourceTag == "DCF77" || t.sourceTag == "PL225") && t.weekday >= 1 && t.weekday <= 7)
            wd = dcf77Days[t.weekday];
        else if ((t.sourceTag == "MSF" || t.sourceTag == "JJY") && t.weekday >= 0 && t.weekday <= 6)
            wd = sundayFirstDays[t.weekday];

        std::printf("\n[%s] Neues Telegramm dekodiert:\n", t.sourceTag.c_str());
        if (t.dateValid) {
            std::printf("  Datum : %02d.%02d.20%02d (%s)", t.day, t.month, t.year2, wd);
            if (t.sourceTag == "JJY") std::printf(" [Tag %d des Jahres]", t.dayOfYear);
            std::printf("\n");
        } else {
            std::printf("  Datum : nicht verfuegbar (unvollstaendiges Telegramm)\n");
        }
        std::printf("  Zeit  : %02d:%02d:%02d %s\n",
                     t.hour, t.minute, t.second, t.dst ? "Sommerzeit" : "Normalzeit");
        if (t.leapSecondAnnounced) {
            std::printf("  Hinweis: Schaltsekunde angekuendigt (%s)\n",
                         t.leapSecondIsRemoval ? "wird entfernt" : "wird eingefuegt");
        }
        if (t.antennaChangeAnnounced) std::printf("  Hinweis: Rufbit gesetzt (Sendeumschaltung/Stoerung)\n");
        if (t.dstChangeAnnounced) std::printf("  Hinweis: Sommerzeitwechsel in den naechsten 61 Minuten angekuendigt\n");
        if (t.sourceTag == "MSF") {
            std::printf("  DUT1  : %+d ms (UT1-UTC)\n", t.dut1DeciSeconds * 100);
        }
        if (t.sourceTag == "PL225") {
            std::printf("  Zeitzone-Offset: +%dh, Sender-Status: %s\n",
                        t.tzOffsetHours, t.transmitterStateText.c_str());
        }
    }

    void onDecodeError(const std::string& reason) override {
        std::fprintf(stderr, "[Decoder-Hinweis] %s\n", reason.c_str());
    }

    void onBitDecoded(int bitIndex, int bitValue) override {
        if (verboseBits_) {
            std::printf("Bit %2d = %d\n", bitIndex, bitValue);
        }
    }

    bool verboseBits_ = false;
};

enum class Protocol { Dcf77, Msf, Jjy, Pl225 };

struct Options {
    std::string device = "default";
    unsigned sampleRate = 48000;
    double targetToneHz = 1000.0;
    bool showSpectrum = false;
    bool verboseBits = false;
    Protocol protocol = Protocol::Dcf77;
    std::string wavFile;   // leer = Soundkarte verwenden, sonst Pfad zur (Mono-)WAV-Datei
    std::string iqWavFile; // nur PL225: Pfad zu einer stereo IQ-WAV-Datei (I=links,Q=rechts)
};

void printUsage(const char* prog) {
    std::printf(
        "Verwendung: %s [Optionen]\n"
        "  --device <alsa-device>   ALSA Aufnahmegeraet (Default: 'default')\n"
        "  --wav-file <pfad.wav>     WAV-Datei statt Soundkarte einlesen (PCM, 16 Bit,\n"
        "                            mono oder stereo). Ueberschreibt --device und --rate;\n"
        "                            die Abtastrate wird aus der Datei uebernommen.\n"
        "  --iq-wav-file <pfad.wav>  Nur --protocol pl225: stereo IQ-WAV-Datei (I=links,\n"
        "                            Q=rechts) statt SSB/Mono-Empfang verwenden.\n"
        "  --rate <Hz>               Abtastrate bei Soundkarten-Aufnahme (Default: 48000)\n"
        "  --tone <Hz>               Zieltonfrequenz/BFO-Frequenz des Empfaenger-NF-Ausgangs\n"
        "                            (Default: 1000). Bei --protocol pl225 im Mono/SSB-Modus:\n"
        "                            die BFO-Frequenz, bei der der 225kHz-Traeger im Audio\n"
        "                            erscheint (siehe Praxis-Hinweis unten).\n"
        "  --protocol <dcf77|msf|jjy|pl225> Zeitzeichenprotokoll (Default: dcf77)\n"
        "                            dcf77 -> 77,5 kHz \n"
        "                            msf   -> 60 kHz \n"
        "                            jjy   -> 40 kHz oder 60 kHz \n"
        "                            pl225 -> 225 kHz ,\n"
        "                                     ODER --iq-wav-file fuer echten IQ-Mitschnitt\n"
        "  --spectrum                Periodische ASCII-Spektrumsanzeige zur Feinabstimmung\n"
        "                            (bei --wav-file: eine einmalige Analyse ueber die\n"
        "                            gesamte Datei statt einer Live-Anzeige; nicht fuer pl225)\n"
        "  --verbose-bits            Jedes einzelne dekodierte Bit ausgeben\n"
        "  --help                    Diese Hilfe anzeigen\n",
        prog);
}

bool parseArgs(int argc, char** argv, Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto needValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "Fehlender Wert fuer %s\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--device") {
            const char* v = needValue("--device"); if (!v) return false;
            opt.device = v;
        } else if (arg == "--wav-file") {
            const char* v = needValue("--wav-file"); if (!v) return false;
            opt.wavFile = v;
        } else if (arg == "--iq-wav-file") {
            const char* v = needValue("--iq-wav-file"); if (!v) return false;
            opt.iqWavFile = v;
        } else if (arg == "--rate") {
            const char* v = needValue("--rate"); if (!v) return false;
            opt.sampleRate = static_cast<unsigned>(std::atoi(v));
        } else if (arg == "--tone") {
            const char* v = needValue("--tone"); if (!v) return false;
            opt.targetToneHz = std::atof(v);
        } else if (arg == "--protocol") {
            const char* v = needValue("--protocol"); if (!v) return false;
            std::string p = v;
            if (p == "dcf77") opt.protocol = Protocol::Dcf77;
            else if (p == "msf") opt.protocol = Protocol::Msf;
            else if (p == "jjy") opt.protocol = Protocol::Jjy;
            else if (p == "pl225") opt.protocol = Protocol::Pl225;
            else {
                std::fprintf(stderr, "Unbekanntes Protokoll: %s (erlaubt: dcf77, msf, jjy, pl225)\n", p.c_str());
                return false;
            }
        } else if (arg == "--spectrum") {
            opt.showSpectrum = true;
        } else if (arg == "--verbose-bits") {
            opt.verboseBits = true;
        } else if (arg == "--help") {
            printUsage(argv[0]);
            return false;
        } else {
            std::fprintf(stderr, "Unbekannte Option: %s\n", arg.c_str());
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------
// PL225 (e-CzasPL, polnischer 225kHz-Sender) - eigener Signalpfad, da
// das Verfahren (kontinuierliche Phasenmodulation statt Amplitudentastung)
// grundlegend anders funktioniert als DCF77/MSF/JJY und daher nicht in die
// ToneEnvelopeDetector/CarrierDipEvent-Pipeline passt. Siehe
// pl225_decoder.hpp fuer die ausfuehrliche Begruendung sowie die Erklaerung,
// warum eine echte PLL/Costas-Loop (statt reiner Huellkurven-AM-Demodulation)
// noetig ist.
// ---------------------------------------------------------------------
int runPl225(const Options& opt) {
    ConsoleTimeSink sink;
    sink.verboseBits_ = opt.verboseBits;

    bool useIq = !opt.iqWavFile.empty();
    bool useMonoWavFile = !useIq && !opt.wavFile.empty();

    if (useIq) {
        audio::IqWavAudioSource iqSrc(opt.iqWavFile);
        try {
            iqSrc.open();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "IQ-WAV-Datei konnte nicht geoeffnet werden: %s\n", e.what());
            return 2;
        }
        std::printf("PL225-Dekodierung (echtes IQ) gestartet aus Datei '%s', Abtastrate %u Hz, "
                    "Dauer %.1f s.\n\n",
                    opt.iqWavFile.c_str(), iqSrc.sampleRate(), iqSrc.durationMs() / 1000.0);

        pl225::Pl225Decoder decoder(sink, iqSrc.sampleRate());
        unsigned blockSizeSamples = iqSrc.sampleRate() / 10; // 100ms
        iqSrc.playbackLoop(blockSizeSamples, [&](const std::vector<std::complex<float>>& iq, uint64_t) {
            decoder.processBlock(iq, 0);
        });
    } else {
        // Mono-Pfad (SSB/USB-Empfang mit BFO, oder direkt als WAV-Datei):
        // ueber MonoToIqDownconverter (feste NCO bei --tone Hz + Tiefpass)
        // in einen komplexen Strom umgewandelt, den Rest erledigt die im
        // Pl225Decoder eingebaute PLL/Costas-Loop (Frequenz-Nachfuehrung).
        if (useMonoWavFile) {
            audio::WavAudioSource wavSrc(opt.wavFile);
            try {
                wavSrc.open();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "WAV-Datei konnte nicht geoeffnet werden: %s\n", e.what());
                return 2;
            }
            pl225::MonoToIqDownconverter dc(wavSrc.sampleRate(), opt.targetToneHz);
            pl225::Pl225Decoder decoder(sink, wavSrc.sampleRate());
            std::printf("PL225-Dekodierung (SSB/Mono, BFO %.1f Hz) gestartet aus Datei '%s', "
                        "Abtastrate %u Hz, Dauer %.1f s.\n\n",
                        opt.targetToneHz, opt.wavFile.c_str(), wavSrc.sampleRate(),
                        wavSrc.durationMs() / 1000.0);

            unsigned blockSizeSamples = wavSrc.sampleRate() / 10;
            wavSrc.playbackLoop(blockSizeSamples, [&](const std::vector<float>& samples, uint64_t) {
                auto iq = dc.process(samples);
                decoder.processBlock(iq, 0);
            });
        } else {
            audio::AlsaAudioSource audioSrc(opt.device, opt.sampleRate, opt.sampleRate / 10);
            try {
                audioSrc.open();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "Audiogeraet konnte nicht geoeffnet werden: %s\n", e.what());
                return 2;
            }
            pl225::MonoToIqDownconverter dc(opt.sampleRate, opt.targetToneHz);
            pl225::Pl225Decoder decoder(sink, opt.sampleRate);
            std::printf("PL225-Empfang (SSB/Mono, BFO %.1f Hz) gestartet auf Geraet '%s'.\n"
                        "Empfaenger auf 225 kHz im USB-Modus einstellen, BFO so waehlen, dass\n"
                        "der Traegerton exakt bei %.1f Hz liegt. Abbruch mit Strg+C.\n\n",
                        opt.targetToneHz, opt.device.c_str(), opt.targetToneHz);

            audioSrc.captureLoop([&](const std::vector<float>& samples, uint64_t) {
                auto iq = dc.process(samples);
                decoder.processBlock(iq, 0);
                if (g_stopRequested) audioSrc.stop();
            });
        }
    }

    std::printf("\nBeendet.\n");
    return 0;
}

int main(int argc, char** argv) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    if (opt.protocol == Protocol::Pl225) {
        return runPl225(opt);
    }

    ConsoleTimeSink sink;
    sink.verboseBits_ = opt.verboseBits;

    // Decoder-Auswahl: beide Implementierungen sprechen dasselbe
    // ITimeSignalDecoder-Interface, der restliche Audio-/Envelope-Pfad
    // bleibt dadurch komplett unveraendert - genau dafuer ist er modular
    // aufgebaut.
    std::unique_ptr<timesignal::ITimeSignalDecoder> decoder;
    double carrierFreqKhzForDisplay = 77.5;
    if (opt.protocol == Protocol::Dcf77) {
        decoder = std::make_unique<timesignal::Dcf77Decoder>(sink);
        carrierFreqKhzForDisplay = 77.5;
    } else if (opt.protocol == Protocol::Msf) {
        decoder = std::make_unique<timesignal::MsfDecoder>(sink);
        carrierFreqKhzForDisplay = 60.0;
    } else {
        decoder = std::make_unique<timesignal::JjyDecoder>(sink);
        carrierFreqKhzForDisplay = 40.0; // oder 60.0, siehe Hinweistext unten
    }

    // blockSize 10ms bei sampleRate -> gute Zeitaufloesung fuer die
    // 100/200/300/500ms-Bitklassifikation, ohne die CPU unnoetig zu belasten.
    // Bei Datei-Eingabe wird die Abtastrate der Datei selbst verwendet
    // (opt.sampleRate wird dann ignoriert).
    bool useWavFile = !opt.wavFile.empty();

    audio::WavAudioSource wavSrc(opt.wavFile);
    std::unique_ptr<audio::AlsaAudioSource> audioSrc; // nur bei Soundkarten-Betrieb angelegt

    unsigned effectiveSampleRate = opt.sampleRate;

    if (useWavFile) {
        try {
            wavSrc.open();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "WAV-Datei konnte nicht geoeffnet werden: %s\n", e.what());
            return 2;
        }
        effectiveSampleRate = wavSrc.sampleRate();
    }

    const unsigned blockSizeSamples = effectiveSampleRate / 100; // 10 ms

    std::unique_ptr<audio::SpectrumAnalyzer> spectrum;
    if (opt.showSpectrum) {
        spectrum = std::make_unique<audio::SpectrumAnalyzer>(4096, effectiveSampleRate);
    }

    audio::ToneEnvelopeDetector envelopeDetector(opt.targetToneHz, effectiveSampleRate);
    envelopeDetector.attachDecoder(decoder.get());

    if (!useWavFile) {
        audioSrc = std::make_unique<audio::AlsaAudioSource>(opt.device, opt.sampleRate, blockSizeSamples);
        try {
            audioSrc->open();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Audiogeraet konnte nicht geoeffnet werden: %s\n", e.what());
            return 2;
        }
    }

    if (useWavFile) {
        std::printf("%s-Dekodierung gestartet aus Datei '%s', Zielton %.1f Hz, Abtastrate %u Hz.\n"
                    "Dauer der Datei: %.1f s.\n\n",
                    decoder->name().c_str(), opt.wavFile.c_str(), opt.targetToneHz,
                    effectiveSampleRate, wavSrc.durationMs() / 1000.0);
    } else {
        std::printf("%s-Empfang gestartet auf Geraet '%s', Zielton %.1f Hz, Abtastrate %u Hz.\n",
                    decoder->name().c_str(), opt.device.c_str(), opt.targetToneHz, effectiveSampleRate);
        if (opt.protocol == Protocol::Jjy) {
            std::printf("Empfaenger auf 40 kHz (Ohtakadoya-yama, Ost-Japan) ODER 60 kHz\n"
                        "(Hagane-yama, West-Japan) einstellen - beide senden denselben Zeitcode.\n");
        } else {
            std::printf("Empfaenger auf %.1f kHz im CW/SSB-Modus einstellen.\n", carrierFreqKhzForDisplay);
        }
        std::printf("Feinabstimmung so waehlen, dass der NF-Ton exakt bei %.1f Hz liegt%s.\n"
                    "Abbruch mit Strg+C.\n\n",
                    opt.targetToneHz, opt.showSpectrum ? " (siehe Spektrumsanzeige)" : "");
    }

    int spectrumPrintCounter = 0;

    // Gemeinsame Block-Verarbeitung fuer beide Quellen (Soundkarte/Datei) -
    // genau dafuer haben AlsaAudioSource und WavAudioSource denselben
    // AudioBlockCallback-Typ.
    auto onBlock = [&](const std::vector<float>& samples, uint64_t blockEndMs) {
        // Ton-/Bit-Erkennung fuer den Decoder
        envelopeDetector.process(samples, blockEndMs);

        // Optionale Spektrumsanzeige zur manuellen Feinabstimmung
        if (spectrum) {
            if (spectrum->feed(samples)) {
                // Nicht bei jedem FFT-Block neu zeichnen (unlesbar/flackernd),
                // sondern ca. alle 1-2 Sekunden (Audiozeit, nicht Wanduhrzeit -
                // bei Datei-Wiedergabe laeuft das entsprechend schneller durch).
                if (++spectrumPrintCounter >= 3) {
                    spectrumPrintCounter = 0;
                    double lo = std::max(50.0, opt.targetToneHz - 500.0);
                    double hi = opt.targetToneHz + 500.0;
                    spectrum->printAsciiBars(lo, hi, 40);
                    std::printf("Pegel (Zielton, geglaettet): %.4f\n",
                                envelopeDetector.currentLevel());
                }
            }
        }

        if (!useWavFile && g_stopRequested) {
            audioSrc->stop();
        }
    };

    if (useWavFile) {
        wavSrc.playbackLoop(blockSizeSamples, onBlock);
    } else {
        audioSrc->captureLoop(onBlock);
    }

    std::printf("\nBeendet.\n");
    return 0;
}
