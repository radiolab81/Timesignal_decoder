// msf_decoder.hpp
//
// MSF-Protokolldecoder (Anthorn/UK, 60 kHz, betrieben von NPL).
// Quelle: "NPL Time & Frequency Services - MSF 60 kHz Time and Date Code"
// (offizielles NPL-Datenblatt, Crown Copyright, npl.co.uk/msf-signal).
//
// -------------------------------------------------------------------------
// Grundprinzip (Unterschied zu DCF77):
// -------------------------------------------------------------------------
//   - Minutenmarke: Sekunde 00 = 500ms Trägerabsenkung (direkt erkennbar,
//     NICHT über eine fehlende Absenkung wie bei DCF77).
//   - Jede der restlichen 59 Sekunden traegt ZWEI Bits: "A" und "B".
//     Bitpolarität lt. NPL-Spec: 0 = Träger an, 1 = Träger aus.
//     Kodierung ueber die Dauer/Form der Absenkung zu Sekundenbeginn:
//       100ms durchgehend ab            -> A=0, B=0
//       200ms durchgehend ab            -> A=1, B=0
//       300ms durchgehend ab            -> A=1, B=1
//       100ms ab / 100ms an / 100ms ab  -> A=0, B=1   (Doppelpuls!)
//     Der Doppelpuls ist der Grund, warum der ToneEnvelopeDetector rohe
//     CarrierDipEvents statt fertiger Bits liefert: dieser Decoder muss
//     ggf. zwei Dips derselben Sekunde selbst zusammenfassen.
//
// -------------------------------------------------------------------------
// Bitbelegung (1-basiert, "A"/"B" je Sekunde, MSB-first BCD):
// -------------------------------------------------------------------------
//   01B-16B : DUT1-Code (Differenz UT1-UTC, siehe decodeDut1())
//   01A-16A : aktuell reserviert (0)
//   17A-24A : Jahr, BCD, Gewichte 80,40,20,10,8,4,2,1        (00-99)
//   25A-29A : Monat, BCD, Gewichte 10,8,4,2,1                (01-12)
//   30A-35A : Tag des Monats, BCD, Gewichte 20,10,8,4,2,1    (01-31)
//   36A-38A : Wochentag, BCD, Gewichte 4,2,1                 (0=So..6=Sa!)
//   39A-44A : Stunde, BCD, Gewichte 20,10,8,4,2,1            (00-23)
//   45A-51A : Minute, BCD, Gewichte 40,20,10,8,4,2,1         (00-59)
//   52A     : immer 0 (Praefix des Rahmen-Erkennungsmusters)
//   53A-58A : immer 1 (Rahmen-Erkennungsmuster "01111110" mit 52A/59A)
//   59A     : immer 0 (Suffix des Rahmen-Erkennungsmusters)
//   54B     : gerade... NEIN: ungerade Paritaet zusammen mit 17A-24A (Jahr)
//   55B     : ungerade Paritaet zusammen mit 25A-35A (Monat+Tag)
//   56B     : ungerade Paritaet zusammen mit 36A-38A (Wochentag)
//   57B     : ungerade Paritaet zusammen mit 39A-51A (Stunde+Minute)
//   53B     : Ankuendigung Sommerzeitwechsel (1 = Wechsel in den naechsten
//             61 Minuten, letzte dieser Minuten aendert sich 58B)
//   58B     : Sommerzeit (BST) aktiv, wenn 1; UTC/Winterzeit, wenn 0
//   52B,59B : aktuell reserviert (0)
//
// WICHTIG: MSF zaehlt den Wochentag 0=Sonntag..6=Samstag - das unterscheidet
// sich von DCF77 (1=Montag..7=Sonntag)! In DecodedTime.weekday wird daher
// grundsaetzlich die MSF-eigene Zaehlung (0-6) abgelegt; siehe Kommentar
// bei der Ausgabe in main.cpp.
//
// Bekannte Vereinfachung: Schaltsekunden (60- oder 58-Sekunden-Minuten)
// werden nicht gesondert behandelt, siehe README.

#pragma once

#include <array>
#include <optional>
#include "time_signal_decoder.hpp"

namespace timesignal {

class MsfDecoder : public ITimeSignalDecoder {
public:
    explicit MsfDecoder(IDecodedTimeSink& sink);

    void onCarrierDip(const CarrierDipEvent& ev) override;
    std::string name() const override { return "MSF"; }

private:
    // Bitpuffer je Sekunde 1..59 (Index 0 ungenutzt, entspricht der
    // Minutenmarke, die keine A/B-Daten traegt).
    std::array<int, 60> bitsA_{};
    std::array<int, 60> bitsB_{};

    int secondIndex_ = -1; // -1 = nicht synchronisiert (noch keine Minutenmarke gesehen)

    // Ein noch nicht endgueltig zugeordneter Dip: wir wissen erst beim
    // naechsten Ereignis (naeher dran = Doppelpuls, oder ~1s entfernt =
    // naechste Sekunde), wie er zu interpretieren ist.
    struct PendingDip {
        uint64_t startMs;
        double durationMs;
    };
    std::optional<PendingDip> pendingDip_;

    // Toleranzfenster fuer die vier Dauer-Klassen (siehe Header-Kommentar).
    static constexpr double kDur100Ms = 100.0;
    static constexpr double kDur200Ms = 200.0;
    static constexpr double kDur300Ms = 300.0;
    static constexpr double kDur500Ms = 500.0; // Minutenmarke
    static constexpr double kDurToleranceMs = 40.0;
    static constexpr double kMarkerToleranceMs = 80.0;

    // Innerhalb einer Sekunde liegt der zweite Doppelpuls-Dip ca. 200ms
    // nach dem ersten - deutlich weniger als der ~1000ms-Sekundenabstand.
    static constexpr double kIntraSecondGapMaxMs = 500.0;

    void resetFrame();
    void storeSecond(int second, int bitA, int bitB);
    void finalizePendingAsSingleDip();
    void tryDecodeAndEmit();

    static int bcdWeighted(const std::array<int,60>& bits, int startSecond,
                            const int* weights, int count);
    static bool oddParityOk(const std::array<int,60>& bitsA, int startA, int endAInclusive,
                             const std::array<int,60>& bitsB, int parityBIndex);

    // Dekodiert den DUT1-Code aus den B-Bits 1-16 in Zehntelsekunden
    // (positiv oder negativ), siehe NPL-Tabelle. Vereinfachung: es wird
    // nur die Anzahl gesetzter Bits ab Position 1 (positiv) bzw. 9
    // (negativ) gezaehlt, ohne Lueckenpruefung innerhalb der Gruppe.
    static int decodeDut1DeciSeconds(const std::array<int,60>& bitsB);
};

} // namespace timesignal
