// jjy_decoder.cpp
#include "jjy_decoder.hpp"

#include <cmath>
#include <sstream>

namespace timesignal {

JjyDecoder::JjyDecoder(IDecodedTimeSink& sink) : ITimeSignalDecoder(sink) {
    resetFrame();
}

void JjyDecoder::resetFrame() {
    bits_.fill(-1);
    secondIndex_ = -1;
}

JjyDecoder::Symbol JjyDecoder::classify(double durationMs) const {
    if (std::fabs(durationMs - kDur0Ms) <= kDurToleranceMs) return Symbol::Zero;
    if (std::fabs(durationMs - kDur1Ms) <= kDurToleranceMs) return Symbol::One;
    if (std::fabs(durationMs - kDurMarkerMs) <= kDurToleranceMs) return Symbol::Marker;
    return Symbol::Invalid;
}

bool JjyDecoder::isMarkerPosition(int second) {
    return second == 0 || second == 9 || second == 19 || second == 29 ||
           second == 39 || second == 49 || second == 59;
}

void JjyDecoder::storeSecond(int second, Symbol sym) {
    if (second < 0 || second > 59) return;
    // An Markerpositionen keine Nutzdaten ablegen (bleibt -1); an
    // Datenpositionen 0/1 aus dem Symbol ableiten.
    if (sym == Symbol::One) bits_[second] = 1;
    else if (sym == Symbol::Zero) bits_[second] = 0;
    sink_.onBitDecoded(second, bits_[second]);
    secondIndex_ = second;
}

void JjyDecoder::onCarrierDip(const CarrierDipEvent& ev) {
    Symbol sym = classify(ev.durationMs);
    bool curIsMarker = (sym == Symbol::Marker);

    // Minutenerkennung: zwei unmittelbar aufeinanderfolgende Marker-Dips
    // im normalen Sekundenabstand (~1000ms) treten laut Protokoll NUR am
    // Uebergang P0(Sekunde59) -> M(Sekunde0) auf.
    bool minuteStart = haveLastDip_ && curIsMarker && lastDipWasMarker_ &&
                        std::fabs(ev.sinceLastDipStartMs - kMinuteGapNominalMs) <= kMinuteGapToleranceMs;

    if (minuteStart) {
        if (secondIndex_ == 59) {
            tryDecodeAndEmit();
        } else if (secondIndex_ > 0) {
            sink_.onDecodeError("JJY: Minutenmarke vor vollstaendigem Telegramm (nur " +
                                 std::to_string(secondIndex_) + " von 59 Sekunden empfangen)");
        }
        resetFrame();
        secondIndex_ = 0;
        lastDipWasMarker_ = true;
        lastDipStartMs_ = ev.dipStartMs;
        haveLastDip_ = true;
        return;
    }

    // Zustand fuer die naechste Marker-Paar-Pruefung aktualisieren -
    // WICHTIG: das muss unabhaengig vom Sync-Status und auch waehrend des
    // Morseblocks geschehen, sonst wird der naechste echte Minutenwechsel
    // nicht erkannt.
    lastDipWasMarker_ = curIsMarker;
    lastDipStartMs_ = ev.dipStartMs;
    haveLastDip_ = true;

    if (secondIndex_ < 0) {
        return; // noch nicht synchronisiert - auf Minutenmarke warten
    }

    if (sym == Symbol::Invalid) {
        // Unklassifizierbare Dip-Dauer (z.B. Morsecode-Rufzeichenblock in
        // Minute 15/45, oder ein Stoerimpuls): NICHT die Synchronisation
        // verwerfen, sondern nur diese eine Sekundenposition als "nicht
        // verfuegbar" ueberspringen. Die zwei-Marker-Erkennung oben laeuft
        // unabhaengig davon weiter und synchronisiert zur naechsten echten
        // Minutenmarke zuverlaessig neu.
        int skippedSecond = secondIndex_ + 1;
        if (skippedSecond > 59) {
            sink_.onDecodeError("JJY: 59 Sekunden ueberschritten ohne Minutenmarke - Sync verworfen");
            resetFrame();
            return;
        }
        sink_.onDecodeError("JJY: unklassifizierbare Dip-Dauer " + std::to_string(ev.durationMs) +
                             " ms bei Sekunde " + std::to_string(skippedSecond) +
                             " - Position wird uebersprungen (evtl. Morseblock)");
        secondIndex_ = skippedSecond; // bits_[skippedSecond] bleibt -1
        return;
    }

    int nextSecond = secondIndex_ + 1;
    if (nextSecond > 59) {
        sink_.onDecodeError("JJY: 59 Sekunden ueberschritten ohne Minutenmarke - Sync verworfen");
        resetFrame();
        return;
    }

    bool expectedMarkerHere = isMarkerPosition(nextSecond);
    if (expectedMarkerHere != curIsMarker) {
        sink_.onDecodeError("JJY: Rahmenposition " + std::to_string(nextSecond) +
                             " passt nicht zum erwarteten Marker-/Datenmuster - Sync verworfen");
        resetFrame();
        return;
    }

    storeSecond(nextSecond, sym);
}

int JjyDecoder::bcdFromIndices(const std::array<int,60>& bits,
                                const std::vector<int>& indices,
                                const std::vector<int>& weights) {
    int value = 0;
    for (size_t i = 0; i < indices.size(); ++i) {
        int b = bits[indices[i]];
        if (b < 0) return -1;
        value += b * weights[i];
    }
    return value;
}

bool JjyDecoder::evenParityOk(const std::array<int,60>& bits,
                               const std::vector<int>& dataIndices, int parityIndex) {
    int ones = 0;
    for (int idx : dataIndices) {
        if (bits[idx] < 0) return false;
        ones += bits[idx];
    }
    if (bits[parityIndex] < 0) return false;
    // Laut NICT: der Paritaetsbitwert IST der Rest der Summe geteilt durch 2
    // (= klassische gerade Paritaet: Datenbits + Paritaetsbit ergeben in
    // Summe eine gerade Zahl).
    return ((ones + bits[parityIndex]) % 2) == 0;
}

bool JjyDecoder::dayOfYearToMonthDay(int year2, int dayOfYear, int& month, int& day) {
    if (dayOfYear < 1 || dayOfYear > 366) return false;
    bool leap = (year2 % 4 == 0); // gueltig fuer 2000-2099, siehe Header-Kommentar
    static const int daysInMonthNormal[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int remaining = dayOfYear;
    for (int m = 0; m < 12; ++m) {
        int dim = daysInMonthNormal[m] + ((m == 1 && leap) ? 1 : 0);
        if (remaining <= dim) {
            month = m + 1;
            day = remaining;
            return true;
        }
        remaining -= dim;
    }
    return false; // dayOfYear zu gross fuer das (Nicht-)Schaltjahr
}

void JjyDecoder::tryDecodeAndEmit() {
    // --- Minute und Stunde: das sind die einzigen Felder, die IMMER
    //     regulaer uebertragen werden (auch in Minute 15/45, da der
    //     Morseblock erst danach folgt) ---
    int minute = bcdFromIndices(bits_, {1,2,3,5,6,7,8}, {40,20,10,8,4,2,1});
    int hour   = bcdFromIndices(bits_, {12,13,15,16,17,18}, {20,10,8,4,2,1});

    if (minute < 0 || hour < 0) {
        sink_.onDecodeError("JJY: unvollstaendige Minuten-/Stundenbits - Telegramm verworfen");
        return;
    }
    if (!evenParityOk(bits_, {12,13,15,16,17,18}, 36)) {
        sink_.onDecodeError("JJY: Paritaetsfehler PA1 (Stunde, Bit 36) - Telegramm verworfen");
        return;
    }
    if (!evenParityOk(bits_, {1,2,3,5,6,7,8}, 37)) {
        sink_.onDecodeError("JJY: Paritaetsfehler PA2 (Minute, Bit 37) - Telegramm verworfen");
        return;
    }
    if (minute > 59 || hour > 23) {
        sink_.onDecodeError("JJY: Minute/Stunde ausserhalb des gueltigen Wertebereichs");
        return;
    }

    DecodedTime result;
    result.sourceTag = "JJY";
    result.timeValid = true;
    result.second = 0;
    result.minute = minute;
    result.hour = hour;

    // --- Datumsfelder: nur verfuegbar, wenn kein Morseblock den
    //     Schlussteil dieser Minute ersetzt hat (dann bleiben die
    //     entsprechenden Bits -1 und werden hier sauber als "nicht
    //     verfuegbar" erkannt statt falsche Werte zu raten). ---
    int dayOfYear = bcdFromIndices(bits_, {22,23,25,26,27,28,30,31,32,33},
                                    {200,100,80,40,20,10,8,4,2,1});
    int year2     = bcdFromIndices(bits_, {41,42,43,44,45,46,47,48},
                                    {80,40,20,10,8,4,2,1});
    int weekday   = bcdFromIndices(bits_, {50,51,52}, {4,2,1});

    if (dayOfYear >= 1 && dayOfYear <= 366 && year2 >= 0 && year2 <= 99 &&
        weekday >= 0 && weekday <= 6) {
        int month = 0, day = 0;
        if (dayOfYearToMonthDay(year2, dayOfYear, month, day)) {
            result.dateValid = true;
            result.dayOfYear = dayOfYear;
            result.year2 = year2;
            result.month = month;
            result.day = day;
            result.weekday = weekday; // 0=Sonntag..6=Samstag, wie bei MSF
        }
    }
    // dateValid bleibt false (Default), wenn die Felder unvollstaendig
    // waren (z.B. wegen des Morseblocks in Minute 15/45) - dann werden
    // trotzdem Stunde/Minute ausgeliefert, siehe main.cpp Ausgabelogik.

    // --- Reservierte/Zusatzbits (nur auswerten, wenn vorhanden) ---
    if (bits_[38] >= 0) result.dstChangeAnnounced = (bits_[38] == 1); // SU1 (aktuell stets 0)
    if (bits_[40] >= 0) result.dst = (bits_[40] == 1);                // SU2 (aktuell stets 0)
    if (bits_[53] >= 0) result.leapSecondAnnounced = (bits_[53] == 1); // LS1
    if (bits_[54] >= 0) result.leapSecondIsRemoval = (bits_[53] == 1) && (bits_[54] == 0); // LS2

    sink_.onDecodedTime(result);
}

} // namespace timesignal
