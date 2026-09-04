// jjy_decoder.hpp
//
// JJY-Protokolldecoder (Japan, 40 kHz Ohtakadoya-yama / 60 kHz Hagane-yama,
// betrieben von NICT - National Institute of Information and Communications
// Technology). Beide Frequenzen tragen denselben Zeitcode.
// Quelle: NICT "JJY - The JJY Signal" (https://www.nict.go.jp/en/sts/jjy_signal.html)
//
// -------------------------------------------------------------------------
// Grundprinzip (Unterschied zu DCF77/MSF):
// -------------------------------------------------------------------------
//   Bei JJY steigt der Träger zu Sekundenbeginn auf 100% und faellt NACH
//   einer bestimmten Dauer auf 10% ab, bis zur naechsten Sekunde. Es ist
//   also (anders als bei DCF77/MSF) die Dauer der VOLLEN Leistung, die das
//   Bit kodiert - unser ToneEnvelopeDetector misst aber die Dauer der
//   ABSENKUNG (Dip). Das ist rechnerisch aequivalent (Dip-Dauer = 1s minus
//   Volldauer) und ergibt fuer die drei JJY-Symbole folgende, direkt am Dip
//   ablesbare Dauer:
//       Dip ~0.2s  -> Symbol "0" (0.8s volle Leistung, 0.2s abgesenkt)
//       Dip ~0.5s  -> Symbol "1" (0.5s volle Leistung, 0.5s abgesenkt)
//       Dip ~0.8s  -> Marker "M"/"P0".."P5" (0.2s volle Leistung, 0.8s ab)
//   Es gibt - anders als bei MSF - pro Sekunde immer genau EINEN Dip, keine
//   Doppelpulse.
//
//   Minutensynchronisation: JJY sendet KEINEN eindeutig langen Marker und
//   auch keine fehlende Absenkung. Stattdessen erscheinen Marker (0.2s
//   Dip) an den Positionen 0 (M), 9,19,29,39,49 (P1-P5) und 59 (P0) -
//   also alle 10 Sekunden. Zwei UNMITTELBAR AUFEINANDERFOLGENDE Marker
//   (P0 bei Sekunde 59, dann M bei Sekunde 0 der Folgeminute) sind die
//   einzige Stelle, an der das vorkommt - das identifiziert eindeutig den
//   Minutenwechsel (siehe NICT-Dokumentation).
//
// -------------------------------------------------------------------------
// Bitbelegung (0-basiert, Sekunde = Bitindex):
// -------------------------------------------------------------------------
//   00      : M  (Minutenmarke)
//   01-03,05-08 : Minute, BCD, Gewichte 40,20,10,(4 unbenutzt),8,4,2,1  (7 Bit)
//   09      : P1
//   10-11   : unbenutzt (immer 0)
//   12-13,15-18 : Stunde, BCD, Gewichte 20,10,(14 unbenutzt),8,4,2,1   (6 Bit)
//   19      : P2
//   20-21   : unbenutzt (immer 0)
//   22-23,25-28 : Tag des Jahres (hoeherwertiger Teil), Gewichte 200,100,80,40,20,10
//   29      : P3
//   30-33   : Tag des Jahres (niederwertiger Teil), Gewichte 8,4,2,1
//   34-35   : unbenutzt (immer 0) - ZWEI reservierte Positionen, nicht nur eine!
//   36      : PA1 - gerade Paritaet ueber die 6 Stundenbits
//   37      : PA2 - gerade Paritaet ueber die 7 Minutenbits
//   38      : SU1 - Ankuendigung Sommerzeitwechsel (aktuell immer 0, reserviert)
//   39      : P4
//   40      : SU2 - Sommerzeit aktiv (aktuell immer 0, reserviert)
//   41-48   : Jahr, BCD, Gewichte 80,40,20,10,8,4,2,1                  (00-99)
//   49      : P5
//   50-52   : Wochentag, BCD, Gewichte 4,2,1                          (0=So..6=Sa)
//   53      : LS1 - Schaltsekunde in diesem Monat angekuendigt
//   54      : LS2 - 1=Sekunde wird eingefuegt, 0=entfernt (nur relevant wenn LS1=1)
//   55-58   : reserviert (aktuell immer 0)
//   59      : P0
//
// Sonderfall Minute 15/45: hier wird der Schlussblock durch Morsecode (Rufzeichen
// "JJY") sowie Wartungsankuendigungs-Bits ersetzt. Diese Pulse folgen keinem der
// drei regulaeren Dauer-Muster. Der Decoder behandelt jede unklassifizierbare
// Dip-Dauer nicht als harten Synchronisationsfehler, sondern ueberspringt nur
// die betroffene Sekundenposition (bleibt auf -1 = "nicht verfuegbar"). Dadurch
// bleiben frueh im Rahmen uebertragene Felder (insbesondere Minute und Stunde)
// erhalten; nur die spaeter im Rahmen liegenden Datumsfelder werden dann als
// dateValid=false gemeldet. Die "zwei Marker in Folge"-Erkennung laeuft davon
// unberuehrt weiter und synchronisiert zuverlaessig zur naechsten echten Minute.

#pragma once

#include <array>
#include <vector>
#include "time_signal_decoder.hpp"

namespace timesignal {

class JjyDecoder : public ITimeSignalDecoder {
public:
    explicit JjyDecoder(IDecodedTimeSink& sink);

    void onCarrierDip(const CarrierDipEvent& ev) override;
    std::string name() const override { return "JJY"; }

private:
    enum class Symbol { Zero, One, Marker, Invalid };

    // -1 = noch kein gueltiges Bit fuer diese Sekundenposition empfangen.
    // An Markerpositionen bleibt der Wert stets -1 (keine Nutzdaten dort).
    std::array<int, 60> bits_{};

    int secondIndex_ = -1; // -1 = nicht synchronisiert

    // Zustand fuer die Zwei-Marker-in-Folge Minutenerkennung; wird bei
    // JEDEM Dip aktualisiert, unabhaengig vom Sync-Status, damit die
    // Erkennung auch waehrend Resync-Phasen oder dem Morseblock weiterlaeuft.
    bool lastDipWasMarker_ = false;
    uint64_t lastDipStartMs_ = 0;
    bool haveLastDip_ = false;

    static constexpr double kDur0Ms = 200.0;   // Symbol "0"-Dip (0.8s voll, 0.2s ab)
    static constexpr double kDur1Ms = 500.0;   // Symbol "1"-Dip (0.5s voll, 0.5s ab)
    static constexpr double kDurMarkerMs = 800.0; // Marker-Dip (0.2s voll, 0.8s ab)
    static constexpr double kDurToleranceMs = 60.0;
    static constexpr double kMinuteGapNominalMs = 1000.0;
    static constexpr double kMinuteGapToleranceMs = 100.0;

    void resetFrame();
    Symbol classify(double durationMs) const;
    static bool isMarkerPosition(int second);

    void storeSecond(int second, Symbol sym);
    void tryDecodeAndEmit();

    static int bcdFromIndices(const std::array<int,60>& bits,
                               const std::vector<int>& indices,
                               const std::vector<int>& weights);
    static bool evenParityOk(const std::array<int,60>& bits,
                              const std::vector<int>& dataIndices, int parityIndex);

    // Wandelt (zweistelliges Jahr, Tag des Jahres) in Monat/Tag um.
    // Schaltjahrregel vereinfacht auf "Jahr durch 4 teilbar", gueltig
    // innerhalb 2000-2099 (siehe README fuer die Jahrhundert-Problematik
    // bei Jahr "00").
    static bool dayOfYearToMonthDay(int year2, int dayOfYear, int& month, int& day);
};

} // namespace timesignal
