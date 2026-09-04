// msf_decoder.cpp
#include "msf_decoder.hpp"

#include <cmath>
#include <sstream>

namespace timesignal {

MsfDecoder::MsfDecoder(IDecodedTimeSink& sink) : ITimeSignalDecoder(sink) {
    resetFrame();
}

void MsfDecoder::resetFrame() {
    bitsA_.fill(-1);
    bitsB_.fill(-1);
    secondIndex_ = -1;
    pendingDip_.reset();
}

void MsfDecoder::storeSecond(int second, int bitA, int bitB) {
    if (second < 1 || second > 59) return;
    bitsA_[second] = bitA;
    bitsB_[second] = bitB;
    sink_.onBitDecoded(second, bitA); // A-Bit als "Hauptbit" fuers Debugging melden
    secondIndex_ = second;
}

void MsfDecoder::finalizePendingAsSingleDip() {
    if (!pendingDip_.has_value()) return;
    double d = pendingDip_->durationMs;
    int a = -1, b = -1;
    if (std::fabs(d - kDur100Ms) <= kDurToleranceMs) { a = 0; b = 0; }
    else if (std::fabs(d - kDur200Ms) <= kDurToleranceMs) { a = 1; b = 0; }
    else if (std::fabs(d - kDur300Ms) <= kDurToleranceMs) { a = 1; b = 1; }

    if (a < 0) {
        std::ostringstream oss;
        oss << "MSF: unklassifizierbare Dip-Dauer " << d << " ms - Sync verworfen";
        sink_.onDecodeError(oss.str());
        resetFrame();
        return;
    }

    int nextSecond = secondIndex_ + 1;
    storeSecond(nextSecond, a, b);
    pendingDip_.reset();
}

void MsfDecoder::onCarrierDip(const CarrierDipEvent& ev) {
    // 1. Minutenmarke? Bei MSF direkt an der Dauer (~500ms) erkennbar -
    //    im Gegensatz zu DCF77 muss dafuer keine fehlende Absenkung
    //    ausgewertet werden.
    bool isMinuteMark = std::fabs(ev.durationMs - kDur500Ms) <= kMarkerToleranceMs;

    if (isMinuteMark) {
        // Noch offenen Dip der Vorgaenger-Sekunde (falls vorhanden) als
        // einzelnen (nicht-doppelten) Dip abschliessen, BEVOR wir das
        // Telegramm auswerten/zuruecksetzen.
        finalizePendingAsSingleDip();

        if (secondIndex_ == 59) {
            tryDecodeAndEmit();
        } else if (secondIndex_ > 0) {
            sink_.onDecodeError("MSF: Minutenmarke vor vollstaendigem Telegramm (nur " +
                                 std::to_string(secondIndex_) + " von 59 Sekunden empfangen)");
        }

        resetFrame();
        secondIndex_ = 0; // Minutenmarke selbst = Sekunde 0, traegt keine A/B-Daten
        return;
    }

    // 2. Kein Minutenmarker: normaler Daten-Dip. Wenn wir noch nie eine
    //    Minutenmarke gesehen haben, gibt es noch keinen Sekundenbezug -
    //    Ereignis verwerfen und auf die naechste Minutenmarke warten.
    if (secondIndex_ < 0) {
        return;
    }

    if (!pendingDip_.has_value()) {
        // Erster Dip einer (potenziell neuen) Sekunde - erstmal merken,
        // Klassifikation folgt beim naechsten Ereignis (siehe unten).
        pendingDip_ = PendingDip{ev.dipStartMs, ev.durationMs};
        return;
    }

    // Es liegt bereits ein Dip in der Warteschlange. Anhand des Abstands
    // der Startzeiten entscheiden, ob der neue Dip zur selben Sekunde
    // gehoert (Doppelpuls, A=0/B=1) oder die naechste Sekunde einleitet.
    double gapMs = static_cast<double>(ev.dipStartMs - pendingDip_->startMs);

    bool pendingLooksLikeFirstHalfOfDoublePulse =
        std::fabs(pendingDip_->durationMs - kDur100Ms) <= kDurToleranceMs;

    if (gapMs < kIntraSecondGapMaxMs && pendingLooksLikeFirstHalfOfDoublePulse) {
        // Doppelpuls-Muster bestaetigt: 100ms ab / ~100ms an / 100ms ab
        // -> A=0, B=1 fuer diese eine Sekunde. Der zweite Dip liefert
        // keine zusaetzliche Information, wird also nur verworfen.
        int nextSecond = secondIndex_ + 1;
        storeSecond(nextSecond, /*A=*/0, /*B=*/1);
        pendingDip_.reset();
    } else {
        // Der wartende Dip war ein eigenstaendiger (einzelner) Dip einer
        // abgeschlossenen Sekunde; jetzt auswerten, und der neue Dip wird
        // zum wartenden Dip der naechsten Sekunde.
        finalizePendingAsSingleDip();
        if (secondIndex_ >= 0) { // resetFrame() kann in finalizePendingAsSingleDip() erfolgt sein
            pendingDip_ = PendingDip{ev.dipStartMs, ev.durationMs};
        }
    }

    if (secondIndex_ > 59) {
        sink_.onDecodeError("MSF: 59 Sekunden ueberschritten ohne Minutenmarke - Sync verworfen");
        resetFrame();
    }
}

int MsfDecoder::bcdWeighted(const std::array<int,60>& bits, int startSecond,
                             const int* weights, int count) {
    int value = 0;
    for (int i = 0; i < count; ++i) {
        int b = bits[startSecond + i];
        if (b < 0) return -1;
        value += b * weights[i];
    }
    return value;
}

bool MsfDecoder::oddParityOk(const std::array<int,60>& bitsA, int startA, int endAInclusive,
                              const std::array<int,60>& bitsB, int parityBIndex) {
    int ones = 0;
    for (int i = startA; i <= endAInclusive; ++i) {
        if (bitsA[i] < 0) return false;
        ones += bitsA[i];
    }
    if (bitsB[parityBIndex] < 0) return false;
    ones += bitsB[parityBIndex];
    return (ones % 2) == 1; // MSF verwendet UNGERADE Paritaet (lt. NPL-Spec)
}

int MsfDecoder::decodeDut1DeciSeconds(const std::array<int,60>& bitsB) {
    int posCount = 0;
    for (int i = 1; i <= 8; ++i) {
        if (bitsB[i] == 1) posCount++;
        else if (bitsB[i] < 0) return 0; // unvollstaendig -> konservativ 0 annehmen
        else break; // Gruppe ist ein "Thermometer-Code": erstes 0-Bit beendet die Kette
    }
    if (posCount > 0) return posCount * 100;

    int negCount = 0;
    for (int i = 9; i <= 16; ++i) {
        if (bitsB[i] == 1) negCount++;
        else if (bitsB[i] < 0) return 0;
        else break;
    }
    return -negCount * 100;
}

void MsfDecoder::tryDecodeAndEmit() {
    // --- Rahmen-Erkennungsmuster pruefen: 52A=0, 53A-58A=1 (sechsmal), 59A=0 ---
    // Diese eindeutige Sequenz "01111110" sichert ab, dass unsere
    // Sekundenzaehlung tatsaechlich synchron zum realen MSF-Rahmen ist -
    // eine reine Bit-Fuellstandspruefung wuerde Phasenfehler nicht erkennen.
    bool frameOk = (bitsA_[52] == 0) && (bitsA_[59] == 0);
    for (int i = 53; i <= 58 && frameOk; ++i) {
        frameOk = (bitsA_[i] == 1);
    }
    if (!frameOk) {
        sink_.onDecodeError("MSF: Rahmen-Erkennungsmuster (52A..59A) stimmt nicht - "
                             "Sekundensynchronisation vermutlich falsch, Telegramm verworfen");
        return;
    }

    // --- Jahr: 17A-24A, Gewichte 80,40,20,10,8,4,2,1; Paritaet 54B (ungerade) ---
    static const int yearWeights[8] = {80, 40, 20, 10, 8, 4, 2, 1};
    int year2 = bcdWeighted(bitsA_, 17, yearWeights, 8);
    if (year2 < 0 || !oddParityOk(bitsA_, 17, 24, bitsB_, 54)) {
        sink_.onDecodeError("MSF: Jahres-Paritaetsfehler (17A-24A/54B) - Telegramm verworfen");
        return;
    }
    if (year2 > 99) {
        sink_.onDecodeError("MSF: dekodiertes Jahr " + std::to_string(year2) + " ausserhalb 0-99");
        return;
    }

    // --- Monat + Tag: 25A-35A gemeinsam mit Paritaet 55B ---
    if (!oddParityOk(bitsA_, 25, 35, bitsB_, 55)) {
        sink_.onDecodeError("MSF: Monat/Tag-Paritaetsfehler (25A-35A/55B) - Telegramm verworfen");
        return;
    }
    static const int monthWeights[5] = {10, 8, 4, 2, 1};
    static const int dayWeights[6]   = {20, 10, 8, 4, 2, 1};
    int month = bcdWeighted(bitsA_, 25, monthWeights, 5);
    int day   = bcdWeighted(bitsA_, 30, dayWeights, 6);
    if (month < 0 || day < 0 || month < 1 || month > 12 || day < 1 || day > 31) {
        sink_.onDecodeError("MSF: Monat/Tag ausserhalb des gueltigen Wertebereichs");
        return;
    }

    // --- Wochentag: 36A-38A, Paritaet 56B, 0=Sonntag..6=Samstag ---
    if (!oddParityOk(bitsA_, 36, 38, bitsB_, 56)) {
        sink_.onDecodeError("MSF: Wochentag-Paritaetsfehler (36A-38A/56B) - Telegramm verworfen");
        return;
    }
    static const int weekdayWeights[3] = {4, 2, 1};
    int weekday = bcdWeighted(bitsA_, 36, weekdayWeights, 3);
    if (weekday < 0 || weekday > 6) {
        sink_.onDecodeError("MSF: Wochentag " + std::to_string(weekday) + " ausserhalb 0-6");
        return;
    }

    // --- Stunde + Minute: 39A-51A gemeinsam mit Paritaet 57B ---
    if (!oddParityOk(bitsA_, 39, 51, bitsB_, 57)) {
        sink_.onDecodeError("MSF: Stunde/Minute-Paritaetsfehler (39A-51A/57B) - Telegramm verworfen");
        return;
    }
    static const int hourWeights[6]   = {20, 10, 8, 4, 2, 1};
    static const int minuteWeights[7] = {40, 20, 10, 8, 4, 2, 1};
    int hour   = bcdWeighted(bitsA_, 39, hourWeights, 6);
    int minute = bcdWeighted(bitsA_, 45, minuteWeights, 7);
    if (hour < 0 || minute < 0 || hour > 23 || minute > 59) {
        sink_.onDecodeError("MSF: Stunde/Minute ausserhalb des gueltigen Wertebereichs");
        return;
    }

    // --- Sommerzeit-Flags: 58B = BST aktiv, 53B = Wechsel angekuendigt ---
    if (bitsB_[58] < 0 || bitsB_[53] < 0) {
        sink_.onDecodeError("MSF: Sommerzeit-Flagbits (53B/58B) fehlen - Telegramm verworfen");
        return;
    }

    // --- Ergebnis zusammensetzen ---
    DecodedTime result;
    result.sourceTag = "MSF";
    result.timeValid = true;
    result.dateValid = true;
    result.second = 0;
    result.minute = minute;
    result.hour = hour;
    result.day = day;
    result.weekday = weekday; // ACHTUNG: 0=Sonntag..6=Samstag (MSF-eigene Zaehlung!)
    result.month = month;
    result.year2 = year2;
    result.dst = (bitsB_[58] == 1);
    result.dstChangeAnnounced = (bitsB_[53] == 1);
    result.leapSecondAnnounced = false; // MSF kodiert Schaltsekunden nicht per Ankuendigungsbit
    result.antennaChangeAnnounced = false; // kein MSF-Aequivalent
    result.dut1DeciSeconds = decodeDut1DeciSeconds(bitsB_);

    sink_.onDecodedTime(result);
}

} // namespace timesignal
