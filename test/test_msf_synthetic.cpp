// test_msf_synthetic.cpp - Verifiziert die MSF-Zustandsmaschine offline,
// ohne Soundkarte, anhand eines von Hand konstruierten Minutenrahmens.
#include "msf_decoder.hpp"
#include <cstdio>
#include <vector>
#include <cassert>

using namespace timesignal;

struct TestSink : IDecodedTimeSink {
    bool gotResult = false;
    DecodedTime last;
    void onDecodedTime(const DecodedTime& t) override { gotResult = true; last = t; }
    void onDecodeError(const std::string& reason) override {
        std::printf("[ERROR] %s\n", reason.c_str());
    }
};

int main() {
    TestSink sink;
    MsfDecoder decoder(sink);

    // Erwartete Werte: 31.12.20**25**, Mittwoch (=3), 23:59, BST aktiv,
    // kein Sommerzeitwechsel angekuendigt, DUT1=0.
    // A/B-Paare je Sekunde 1..59 (0-indiziert in diesem Array ab Sekunde1):
    struct Bit { int a, b; };
    std::vector<Bit> bits(60);
    auto set = [&](int sec, int a, int b) { bits[sec] = {a, b}; };

    // Reserviert 1-16 (A=0,B=0 -> DUT1=0)
    for (int i = 1; i <= 16; ++i) set(i, 0, 0);
    // Jahr 25 -> bits17-24 = 0,0,1,0,0,1,0,1 ; Paritaet 54B=0
    int yearBits[8] = {0,0,1,0,0,1,0,1};
    for (int i = 0; i < 8; ++i) set(17 + i, yearBits[i], 0);
    // Monat 12 -> 25-29 = 1,0,0,1,0
    int monthBits[5] = {1,0,0,1,0};
    for (int i = 0; i < 5; ++i) set(25 + i, monthBits[i], 0);
    // Tag 31 -> 30-35 = 1,1,0,0,0,1
    int dayBits[6] = {1,1,0,0,0,1};
    for (int i = 0; i < 6; ++i) set(30 + i, dayBits[i], 0);
    // Wochentag 3 (Mi) -> 36-38 = 0,1,1 ; Paritaet 56B=1
    int wdBits[3] = {0,1,1};
    for (int i = 0; i < 3; ++i) set(36 + i, wdBits[i], 0);
    // Stunde 23 -> 39-44 = 1,0,0,0,1,1
    int hourBits[6] = {1,0,0,0,1,1};
    for (int i = 0; i < 6; ++i) set(39 + i, hourBits[i], 0);
    // Minute 59 -> 45-51 = 1,0,1,1,0,0,1 ; Paritaet 57B=0
    int minBits[7] = {1,0,1,1,0,0,1};
    for (int i = 0; i < 7; ++i) set(45 + i, minBits[i], 0);

    set(52, 0, 0);
    set(53, 1, 0); // 53A=1 (Rahmenmuster), 53B=0 (kein Sommerzeitwechsel)
    set(54, 1, 0); // 54B = Paritaet Jahr = 0
    set(55, 1, 0); // 55B = Paritaet Monat/Tag = 0
    set(56, 1, 1); // 56B = Paritaet Wochentag = 1
    set(57, 1, 0); // 57B = Paritaet Stunde/Minute = 0
    set(58, 1, 1); // 58B = Sommerzeit aktiv = 1
    set(59, 0, 0);

    // --- Ereignisse generieren ---
    uint64_t t = 0;
    auto emitMinuteMarker = [&]() {
        CarrierDipEvent ev;
        ev.dipStartMs = t;
        ev.dipEndMs = t + 500;
        ev.durationMs = 500;
        ev.sinceLastDipStartMs = 1000; // grosszuegig, wird bei Marker ignoriert
        decoder.onCarrierDip(ev);
        t += 1000; // naechste Sekunde beginnt 1000ms nach Start dieser Sekunde
    };

    auto emitSingleDip = [&](double durationMs) {
        CarrierDipEvent ev;
        ev.dipStartMs = t;
        ev.dipEndMs = t + static_cast<uint64_t>(durationMs);
        ev.durationMs = durationMs;
        ev.sinceLastDipStartMs = 1000;
        decoder.onCarrierDip(ev);
        t += 1000;
    };

    auto emitDoubleDip = [&]() {
        uint64_t secStart = t;
        CarrierDipEvent ev1;
        ev1.dipStartMs = secStart;
        ev1.dipEndMs = secStart + 100;
        ev1.durationMs = 100;
        ev1.sinceLastDipStartMs = 1000;
        decoder.onCarrierDip(ev1);

        CarrierDipEvent ev2;
        ev2.dipStartMs = secStart + 200;
        ev2.dipEndMs = secStart + 300;
        ev2.durationMs = 100;
        ev2.sinceLastDipStartMs = 200; // Abstand innerhalb derselben Sekunde
        decoder.onCarrierDip(ev2);

        t = secStart + 1000;
    };

    emitMinuteMarker(); // Sekunde 0

    for (int sec = 1; sec <= 59; ++sec) {
        Bit b = bits[sec];
        if (b.a == 0 && b.b == 0) emitSingleDip(100);
        else if (b.a == 1 && b.b == 0) emitSingleDip(200);
        else if (b.a == 1 && b.b == 1) emitSingleDip(300);
        else emitDoubleDip(); // a=0,b=1
    }

    emitMinuteMarker(); // Minutenmarke am Ende -> loest Dekodierung aus

    // --- Ergebnis pruefen ---
    if (!sink.gotResult) {
        std::printf("FEHLER: kein Ergebnis dekodiert!\n");
        return 1;
    }
    const DecodedTime& r = sink.last;
    std::printf("Dekodiert: %02d.%02d.20%02d (Wochentag %d, 0=So) %02d:%02d BST=%d Ankuendigung=%d DUT1=%dds\n",
                r.day, r.month, r.year2, r.weekday, r.hour, r.minute, r.dst, r.dstChangeAnnounced, r.dut1DeciSeconds);

    bool ok = true;
    ok &= (r.year2 == 25);
    ok &= (r.month == 12);
    ok &= (r.day == 31);
    ok &= (r.weekday == 3);
    ok &= (r.hour == 23);
    ok &= (r.minute == 59);
    ok &= (r.dst == true);
    ok &= (r.dstChangeAnnounced == false);
    ok &= (r.dut1DeciSeconds == 0);

    if (ok) {
        std::printf("TEST OK: alle Felder stimmen mit den Erwartungswerten ueberein.\n");
        return 0;
    } else {
        std::printf("TEST FEHLGESCHLAGEN: Werte weichen von den Erwartungswerten ab.\n");
        return 1;
    }
}
