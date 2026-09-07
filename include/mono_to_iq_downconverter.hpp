// mono_to_iq_downconverter.hpp
//
// Wandelt ein reelles Mono-Signal, in dem der PL225-Traeger als hoerbarer
// Ton bei einer bekannten NF-Frequenz erscheint (SSB/USB-Empfang mit BFO,
// z.B. Empfaenger auf 224 kHz USB, BFO/Anzeige zeigt "1000 Hz"), in einen
// komplexen IQ-Strom um, der direkt an Pl225Decoder::processBlock()
// weitergereicht werden kann.
//
// -------------------------------------------------------------------------
// Warum das funktioniert (im Gegensatz zu echter Huellkurven-AM):
// -------------------------------------------------------------------------
// SSB-Demodulation ist im Kern eine reine Frequenzverschiebung (Mischung)
// des Empfangssignals - Amplitude UND Phase des urspruenglichen Traegers
// bleiben vollstaendig erhalten, nur um die BFO-Frequenz verschoben. Der
// Traeger erscheint im Audio als stabiler Ton (hier z.B. 1000 Hz), dessen
// PHASE exakt die Phasenmodulation des Senders widerspiegelt. Reine
// Huellkurven-AM-Demodulation dagegen berechnet |A*e^{i*phi}| = A und
// zerstoert phi dabei unwiderruflich - siehe pl225_decoder.hpp fuer die
// ausfuehrliche Herleitung. Ein SI4732-Chip im SSB-Modus tut im Prinzip
// genau das Noetige (Mischung mit BFO), auch wenn er kein explizites IQ
// ausgibt - dieser Downconverter holt sich die Phaseninformation aus genau
// diesem "einkanaligen SSB"-Signal zurueck.
//
// -------------------------------------------------------------------------
// Funktionsweise ("Costas-Loop-Prinzip" in praktikabler Vereinfachung):
// -------------------------------------------------------------------------
// Ein klassischer Costas-Loop regelt die NCO-Frequenz aktiv nach, um exakt
// auf den Traeger einzurasten. Hier wird stattdessen eine FESTE NCO-Frequenz
// (die vom Nutzer angegebene BFO-Frequenz, z.B. 1000 Hz) verwendet, und die
// verbleibende (typischerweise sehr kleine, <1 Hz) Frequenzabweichung wird
// von der bereits im Pl225Decoder eingebauten langsamen Basislinien-
// Nachfuehrung (siehe dortiger Kommentar) toleriert/kompensiert. Das ist
// deutlich einfacher und robuster zu implementieren als eine aktive
// Nachlaufregelung (keine Gefahr von Regelschwingungen), verlangt im
// Gegenzug, dass die tatsaechliche Traegerfrequenz nahe (im Bereich weniger
// Hz) der angegebenen BFO-Frequenz liegt - in der Praxis unproblematisch,
// da der Sender selbst hochstabil ist (vermutlich atomuhrreferenziert) und
// nur die Empfaenger-BFO-Ungenauigkeit relevant ist.
//
// Ablauf pro Sample:
//   1. Mischung mit lokalem NCO (Multiplikation mit cos/-sin der BFO-Phase)
//      -> verschiebt den Zielton exakt auf 0 Hz (Basisband), das Spiegel-
//      band (Summenfrequenz, ~2x BFO) bleibt vorerst im Signal.
//   2. Tiefpassfilterung (Einpol-IIR, Grenzfrequenz so gewaehlt, dass die
//      schnellen 20ms-Bitwechsel des PL225-Signals noch gut durchkommen,
//      das Summenfrequenz-Produkt und ausserhalb liegendes Rundfunk-
//      programm-Audio aber unterdrueckt werden).

#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace pl225 {

class MonoToIqDownconverter {
public:
    // sampleRate: Abtastrate des Mono-Eingangssignals
    // bfoFrequencyHz: NF-Frequenz, bei der der PL225-Traeger im Audio
    //                 erscheint (vom Nutzer am Empfaenger eingestellt/abgelesen)
    // lowpassCutoffHz: Grenzfrequenz des Tiefpassfilters nach der Mischung.
    //                  Default 300 Hz ist ein guter Kompromiss: schnell
    //                  genug fuer die ~20ms-Bitperiode (Anstiegszeit grob
    //                  1/(2*300Hz) ~= 1.7ms), aber schmal genug um das
    //                  Summenfrequenzprodukt (~2*BFO) und den Grossteil
    //                  des Rundfunkprogramms zuverlaessig zu unterdruecken.
    MonoToIqDownconverter(unsigned sampleRate, double bfoFrequencyHz, double lowpassCutoffHz = 300.0)
        : sampleRate_(sampleRate), phaseIncrement_(2.0 * M_PI * bfoFrequencyHz / sampleRate) {
        double dt = 1.0 / sampleRate_;
        double rc = 1.0 / (2.0 * M_PI * lowpassCutoffHz);
        lowpassAlpha_ = dt / (rc + dt);
    }

    // Wandelt einen Block reeller Samples (normiert -1..1) in einen Block
    // komplexer Basisband-Samples um.
    std::vector<std::complex<float>> process(const std::vector<float>& input) {
        std::vector<std::complex<float>> output(input.size());
        for (size_t n = 0; n < input.size(); ++n) {
            double mixI = static_cast<double>(input[n]) * std::cos(ncoPhase_);
            double mixQ = static_cast<double>(input[n]) * (-std::sin(ncoPhase_));

            lpI_ += lowpassAlpha_ * (mixI - lpI_);
            lpQ_ += lowpassAlpha_ * (mixQ - lpQ_);

            output[n] = std::complex<float>(static_cast<float>(lpI_), static_cast<float>(lpQ_));

            ncoPhase_ += phaseIncrement_;
            if (ncoPhase_ > 2.0 * M_PI) ncoPhase_ -= 2.0 * M_PI;
        }
        return output;
    }

private:
    unsigned sampleRate_;
    double phaseIncrement_;
    double ncoPhase_ = 0.0;
    double lowpassAlpha_;
    double lpI_ = 0.0;
    double lpQ_ = 0.0;
};

} // namespace pl225
