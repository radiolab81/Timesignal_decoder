// dcf77_decoder.hpp
//
// DCF77-Protokolldecoder (Mainflingen, 77.5 kHz).
//
// Protokoll-Referenz (Kurzfassung, wie strikt umgesetzt):
//   - 1 Telegramm pro Minute, 59 oder 60 Bit-Positionen (Sekunden 0..58/59)
//   - Jede Sekunde: Trägerabsenkung zu Sekundenbeginn
//       ~100ms Absenkung  -> Bit = 0
//       ~200ms Absenkung  -> Bit = 1
//   - Sekunde 59 (bzw. 60. Sekunde bei Schaltsekunde): KEINE Absenkung
//     -> Minutenmarke / Sync-Punkt für den Beginn der nächsten Minute
//   - Bit  0        : Minutenbeginn, immer 0
//   - Bit  1-14     : Wetter-/Zivilschutzdaten (Encrypted, hier nur roh gespeichert)
//   - Bit 15        : Rufbit (Ankündigung Antennenwechsel/Störung)
//   - Bit 16        : Ankündigung Zeitzonenwechsel (MESZ<->MEZ)
//   - Bit 17        : MESZ (Sommerzeit) aktiv, wenn 1
//   - Bit 18        : MEZ (Normalzeit) aktiv, wenn 1  (17 XOR 18 muss gelten)
//   - Bit 19        : Ankündigung Schaltsekunde
//   - Bit 20        : Startbit der Zeitinformation, immer 1
//   - Bit 21-24     : Minuten Einer (BCD, Wertigkeit 1,2,4,8)
//   - Bit 25-27     : Minuten Zehner (BCD, Wertigkeit 10,20,40)
//   - Bit 28        : Parität (gerade) über Bit 21-28
//   - Bit 29-32     : Stunden Einer (BCD, Wertigkeit 1,2,4,8)
//   - Bit 33-34     : Stunden Zehner (BCD, Wertigkeit 10,20)
//   - Bit 35        : Parität (gerade) über Bit 29-35
//   - Bit 36-39     : Tag Einer (BCD)
//   - Bit 40-41     : Tag Zehner (BCD)
//   - Bit 42-44     : Wochentag (1=Montag .. 7=Sonntag, BCD)
//   - Bit 45-48     : Monat Einer (BCD)
//   - Bit 49        : Monat Zehner (BCD)
//   - Bit 50-53     : Jahr Einer (BCD, 2-stellig)
//   - Bit 54-57     : Jahr Zehner (BCD)
//   - Bit 58        : Parität (gerade) über Bit 36-58
//   - Bit 59        : normalerweise keine Absenkung (Minutenmarke);
//                     bei angekündigter Schaltsekunde erscheint hier ein
//                     zusätzliches Bit=0 und die Minutenmarke verschiebt
//                     sich auf Sekunde 60.
//
// Die dekodierte, aktuell empfangene Minute bezieht sich (wie beim echten
// DCF77) auf den Beginn der *nächsten* Minute: das Telegramm, das während
// Minute N gesendet wird, kodiert die Zeit für Minute N+1.

#pragma once

#include <array>
#include <vector>
#include "time_signal_decoder.hpp"

namespace timesignal {

class Dcf77Decoder : public ITimeSignalDecoder {
public:
    explicit Dcf77Decoder(IDecodedTimeSink& sink);

    void onCarrierDip(const CarrierDipEvent& ev) override;
    std::string name() const override { return "DCF77"; }

private:
    // Bitpuffer für die aktuell laufende Minute. Index = Sekunde 0..59.
    // -1 bedeutet "noch kein gültiges Bit für diese Position empfangen".
    std::array<int, 60> bits_{};
    int expectedIndex_ = -1; // -1 = wir sind noch nicht synchronisiert

    // Schwellenwerte zur Klassifikation der Low-Dauer als Bit 0 / Bit 1.
    // DCF77 Sollwerte: 100ms (logisch 0) bzw. 200ms (logisch 1).
    static constexpr double kBit0NominalMs = 100.0;
    static constexpr double kBit1NominalMs = 200.0;
    static constexpr double kBitToleranceMs = 40.0; // grosszuegige Toleranz für Funkempfang

    // Erkennung der Minutenmarke: keine Absenkung -> lowDurationMs ~ 0,
    // UND der Sekundenabstand zur vorherigen Flanke betraegt ~2000ms statt ~1000ms
    // (weil die fehlende Flanke selbst nicht gemeldet wird, sondern die naechste
    // Flanke der Folgeminute doppelt so weit entfernt liegt).
    static constexpr double kMinuteGapMs = 1700.0; // > 1.7s Abstand = Minutenmarke

    void resetTelegram();
    void handleBit(int bitValue);
    void handleMinuteMark();
    void tryDecodeAndEmit();

    // Hilfsfunktionen für die Protokollauswertung
    static int bcdGroup(const std::array<int,60>& bits, int startBit, int count);
    static bool evenParityOk(const std::array<int,60>& bits, int startBit, int endBitInclusive, int parityBit);
};

} // namespace timesignal
