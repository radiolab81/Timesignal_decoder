// dcf77_decoder.cpp
#include "dcf77_decoder.hpp"

#include <cmath>
#include <sstream>

namespace timesignal {

Dcf77Decoder::Dcf77Decoder(IDecodedTimeSink& sink) : ITimeSignalDecoder(sink) {
    resetTelegram();
}

void Dcf77Decoder::resetTelegram() {
    bits_.fill(-1);
    expectedIndex_ = -1; // erst nach erkannter Minutenmarke gueltig
}

void Dcf77Decoder::onCarrierDip(const CarrierDipEvent& ev) {
    // 1. Aktuelles Bit aus der Dip-Dauer klassifizieren.
    int bitValue = -1;
    if (std::fabs(ev.durationMs - kBit0NominalMs) <= kBitToleranceMs) {
        bitValue = 0;
    } else if (std::fabs(ev.durationMs - kBit1NominalMs) <= kBitToleranceMs) {
        bitValue = 1;
    }
    // ev.durationMs ausserhalb beider Fenster -> bitValue bleibt -1 (ungueltig,
    // z.B. Stoerimpuls oder Fehlklassifikation durch die Hüllkurvenerkennung).

    // 2. Minutenmarke erkennen: ungewoehnlich grosser Abstand zum Start des
    //    letzten Dips bedeutet, dass in der Vorsekunde (Sekunde 59)
    //    KEINE Absenkung stattfand - das ist laut Protokoll exakt der
    //    Minutenwechsel.
    bool isMinuteMark = (ev.sinceLastDipStartMs > kMinuteGapMs);

    if (isMinuteMark) {
        // Falls zuvor ein vollstaendiges Telegramm (Sekunden 0..58) vorlag,
        // jetzt auswerten, bevor der Puffer fuer die neue Minute geleert wird.
        if (expectedIndex_ == 59) {
            tryDecodeAndEmit();
        } else if (expectedIndex_ > 0) {
            // Telegramm wurde vorzeitig abgebrochen (z.B. Empfangsstoerung).
            sink_.onDecodeError("DCF77: Minutenmarke vor vollstaendigem Telegramm "
                                 "(nur " + std::to_string(expectedIndex_) + " Bits empfangen)");
        }
        resetTelegram();
        expectedIndex_ = 0;
        // Das Bit dieses Events selbst ist Sekunde 0 (Minutenbeginn, muss lt.
        // Protokoll 0 sein - Abweichung wird in tryDecodeAndEmit geprueft).
        handleBit(bitValue);
        return;
    }

    // 3. Normalfall: laufendes Telegramm fortschreiben, aber nur wenn wir
    //    bereits synchronisiert sind (nach der ersten erkannten Minutenmarke).
    if (expectedIndex_ < 0) {
        return; // noch nicht synchronisiert - auf naechste Minutenmarke warten
    }

    if (bitValue < 0) {
        // Unklassifizierbare Low-Dauer: Synchronisation verwerfen und auf die
        // naechste Minutenmarke warten, statt mit falschen Daten weiterzumachen.
        std::ostringstream oss;
        oss << "DCF77: unklassifizierbare Bitdauer " << ev.durationMs
            << " ms bei Sekunde " << expectedIndex_ << " - Sync verworfen";
        sink_.onDecodeError(oss.str());
        resetTelegram();
        return;
    }

    handleBit(bitValue);
}

void Dcf77Decoder::handleBit(int bitValue) {
    if (expectedIndex_ < 0 || expectedIndex_ >= 60) return;
    bits_[expectedIndex_] = bitValue;
    sink_.onBitDecoded(expectedIndex_, bitValue);
    expectedIndex_++;
    if (expectedIndex_ > 59) {
        // Sicherheitsnetz: sollte durch die Minutenmarken-Erkennung eigentlich
        // nie passieren; falls doch, auf Resync warten statt Puffer zu ueberlaufen.
        sink_.onDecodeError("DCF77: 60 Bits ohne erkannte Minutenmarke - Sync verworfen");
        resetTelegram();
    }
}

int Dcf77Decoder::bcdGroup(const std::array<int,60>& bits, int startBit, int count) {
    // BCD-Gruppen werden LSB-first uebertragen: startBit hat Wertigkeit 1,
    // startBit+1 Wertigkeit 2, usw. (klassische DCF77-Bitreihenfolge).
    int value = 0;
    int weight = 1;
    for (int i = 0; i < count; ++i) {
        int b = bits[startBit + i];
        if (b < 0) return -1; // fehlendes Bit -> Gruppe ungueltig
        value += b * weight;
        weight *= 2;
    }
    return value;
}

bool Dcf77Decoder::evenParityOk(const std::array<int,60>& bits, int startBit, int endBitInclusive, int parityBit) {
    int ones = 0;
    for (int i = startBit; i <= endBitInclusive; ++i) {
        if (bits[i] < 0) return false; // unvollstaendig -> Paritaet nicht pruefbar
        ones += bits[i];
    }
    if (bits[parityBit] < 0) return false;
    ones += bits[parityBit];
    return (ones % 2) == 0; // gerade Paritaet: Summe aller Bits inkl. Paritaetsbit ist gerade
}

void Dcf77Decoder::tryDecodeAndEmit() {
    // --- Pflichtfelder pruefen, die laut Protokoll fest definiert sind ---
    if (bits_[0] != 0) {
        sink_.onDecodeError("DCF77: Bit 0 (Minutenbeginn) ist nicht 0 - Telegramm verworfen");
        return;
    }
    if (bits_[20] != 1) {
        sink_.onDecodeError("DCF77: Bit 20 (Startbit Zeitinfo) ist nicht 1 - Telegramm verworfen");
        return;
    }

    bool dstActive = (bits_[17] == 1);
    bool cetActive  = (bits_[18] == 1);
    if (bits_[17] < 0 || bits_[18] < 0 || dstActive == cetActive) {
        // Exakt eines von beiden muss gesetzt sein (MESZ xor MEZ).
        sink_.onDecodeError("DCF77: Zeitzonenbits 17/18 inkonsistent (MESZ/MEZ) - Telegramm verworfen");
        return;
    }

    // --- Minute (Bit 21-27, Paritaet Bit 28) ---
    int minUnits = bcdGroup(bits_, 21, 4);
    int minTens  = bcdGroup(bits_, 25, 3);
    if (minUnits < 0 || minTens < 0 || !evenParityOk(bits_, 21, 27, 28)) {
        sink_.onDecodeError("DCF77: Minuten-Paritaetsfehler (Bit 21-28) - Telegramm verworfen");
        return;
    }
    int minute = minTens * 10 + minUnits;
    if (minute > 59) {
        sink_.onDecodeError("DCF77: dekodierte Minute " + std::to_string(minute) + " ausserhalb 0-59");
        return;
    }

    // --- Stunde (Bit 29-34, Paritaet Bit 35) ---
    int hourUnits = bcdGroup(bits_, 29, 4);
    int hourTens  = bcdGroup(bits_, 33, 2);
    if (hourUnits < 0 || hourTens < 0 || !evenParityOk(bits_, 29, 34, 35)) {
        sink_.onDecodeError("DCF77: Stunden-Paritaetsfehler (Bit 29-35) - Telegramm verworfen");
        return;
    }
    int hour = hourTens * 10 + hourUnits;
    if (hour > 23) {
        sink_.onDecodeError("DCF77: dekodierte Stunde " + std::to_string(hour) + " ausserhalb 0-23");
        return;
    }

    // --- Datum (Bit 36-58, Paritaet Bit 58) ---
    if (!evenParityOk(bits_, 36, 57, 58)) {
        sink_.onDecodeError("DCF77: Datums-Paritaetsfehler (Bit 36-58) - Telegramm verworfen");
        return;
    }
    int dayUnits   = bcdGroup(bits_, 36, 4);
    int dayTens    = bcdGroup(bits_, 40, 2);
    int weekday    = bcdGroup(bits_, 42, 3);
    int monthUnits = bcdGroup(bits_, 45, 4);
    int monthTens  = bcdGroup(bits_, 49, 1);
    int yearUnits  = bcdGroup(bits_, 50, 4);
    int yearTens   = bcdGroup(bits_, 54, 4);

    if (dayUnits < 0 || dayTens < 0 || weekday < 0 || monthUnits < 0 ||
        monthTens < 0 || yearUnits < 0 || yearTens < 0) {
        sink_.onDecodeError("DCF77: unvollstaendige Datumsfelder - Telegramm verworfen");
        return;
    }

    int day   = dayTens * 10 + dayUnits;
    int month = monthTens * 10 + monthUnits;
    int year2 = yearTens * 10 + yearUnits;

    if (day < 1 || day > 31 || month < 1 || month > 12 || weekday < 1 || weekday > 7) {
        sink_.onDecodeError("DCF77: Datumsfeld ausserhalb des gueltigen Wertebereichs");
        return;
    }

    // --- Ergebnis zusammensetzen und ausliefern ---
    DecodedTime result;
    result.sourceTag = "DCF77";
    result.timeValid = true;
    result.dateValid = true;
    result.second = 0; // per Definition beginnt die dekodierte Minute bei Sekunde 0
    result.minute = minute;
    result.hour = hour;
    result.day = day;
    result.weekday = weekday;
    result.month = month;
    result.year2 = year2;
    result.dst = dstActive;
    result.leapSecondAnnounced = (bits_[19] == 1);
    result.antennaChangeAnnounced = (bits_[15] == 1);

    sink_.onDecodedTime(result);
}

} // namespace timesignal
