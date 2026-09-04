// test_jjy_synthetic.cpp - Verifiziert die JJY-Zustandsmaschine offline.
#include "jjy_decoder.hpp"
#include <cstdio>
#include <vector>
#include <array>

using namespace timesignal;

struct TestSink : IDecodedTimeSink {
    std::vector<DecodedTime> results;
    void onDecodedTime(const DecodedTime& t) override { results.push_back(t); }
    void onDecodeError(const std::string& reason) override {
        std::printf("[ERROR] %s\n", reason.c_str());
    }
};

// Kleiner Helfer: erzeugt fuer eine volle Minute (60 Sekunden inkl. der
// Marker an 0/9/19/29/39/49/59) die passenden CarrierDipEvents und speist
// sie in den Decoder ein. 'dataBits' enthaelt fuer alle Nicht-Marker-
// Positionen den Wert 0/1 (Marker-Positionen werden ignoriert/ueberschrieben).
// Wenn 'simulateMorseFrom' >= 0 ist, werden ab dieser Sekunde (inklusive)
// bis Sekunde 58 statt regulaerer Pulse "kaputte" Dauerwerte (123ms)
// gesendet, die zu keinem der drei JJY-Symbole passen - das simuliert den
// Morsecode-Rufzeichenblock in Minute 15/45.
uint64_t emitJjyMinute(JjyDecoder& decoder, uint64_t t0,
                        const std::array<int,60>& dataBits,
                        int simulateMorseFrom = -1) {
    auto isMarkerPos = [](int s) {
        return s == 0 || s == 9 || s == 19 || s == 29 || s == 39 || s == 49 || s == 59;
    };
    uint64_t t = t0;
    for (int s = 0; s < 60; ++s) {
        double dur;
        if (simulateMorseFrom >= 0 && s >= simulateMorseFrom && s <= 58) {
            dur = 123.0; // unklassifizierbar - simuliert Morsecode-Pulse
        } else if (isMarkerPos(s)) {
            dur = 800.0; // Marker-Dip
        } else if (dataBits[s] == 1) {
            dur = 500.0; // Symbol "1"
        } else {
            dur = 200.0; // Symbol "0"
        }
        CarrierDipEvent ev;
        ev.dipStartMs = t;
        ev.dipEndMs = t + static_cast<uint64_t>(dur);
        ev.durationMs = dur;
        ev.sinceLastDipStartMs = 1000.0;
        decoder.onCarrierDip(ev);
        t += 1000;
    }
    return t;
}

int main() {
    // ============================================================
    // Testfall 1: normaler Rahmen ohne Morseblock
    //   Datum: Tag 246 des Jahres 2026 (= 03.09.2026), Donnerstag (4),
    //   14:37 Uhr, keine Schaltsekunde angekuendigt.
    // ============================================================
    {
        TestSink sink;
        JjyDecoder decoder(sink);
        std::array<int,60> bits{};
        bits.fill(0);
        // Minute 37 -> 40,20,10,(4unused),8,4,2,1 = 0,1,1,0,1,1,1 an idx1,2,3,5,6,7,8
        bits[1]=0; bits[2]=1; bits[3]=1; bits[5]=0; bits[6]=1; bits[7]=1; bits[8]=1;
        // Stunde 14 -> 20,10,(14unused),8,4,2,1 = 0,1,0,1,0,0 an idx12,13,15,16,17,18
        bits[12]=0; bits[13]=1; bits[15]=0; bits[16]=1; bits[17]=0; bits[18]=0;
        // Paritaeten (PA1=Bit36, PA2=Bit37 - NICHT 35/36, siehe Header-Kommentar!)
        bits[36] = 0; // PA1 (Stunde: 2 Einsen -> gerade -> Paritaetsbit 0)
        bits[37] = 1; // PA2 (Minute: 5 Einsen -> ungerade -> Paritaetsbit 1)
        // Tag des Jahres 246 -> 200,100,80,40,20,10,8,4,2,1 = 1,0,0,1,0,0,0,1,1,0
        int doy[10] = {1,0,0,1,0,0,0,1,1,0};
        int doyIdx[10] = {22,23,25,26,27,28,30,31,32,33};
        for (int i = 0; i < 10; ++i) bits[doyIdx[i]] = doy[i];
        // Jahr 26 -> 80,40,20,10,8,4,2,1 = 0,0,1,0,0,1,1,0
        int yr[8] = {0,0,1,0,0,1,1,0};
        int yrIdx[8] = {41,42,43,44,45,46,47,48};
        for (int i = 0; i < 8; ++i) bits[yrIdx[i]] = yr[i];
        // Wochentag 4 (Donnerstag) -> 4,2,1 = 1,0,0
        bits[50]=1; bits[51]=0; bits[52]=0;
        // LS1/LS2/SU1/SU2 = 0
        bits[38]=0; bits[40]=0; bits[53]=0; bits[54]=0;

        uint64_t t = 0;
        t = emitJjyMinute(decoder, t, bits); // 1. Durchlauf: synchronisiert noch nicht (kein Vorgaenger-Marker)
        t = emitJjyMinute(decoder, t, bits); // 2. Durchlauf: wird durch Marker-Paar am Anfang synchronisiert und gefuellt
        emitJjyMinute(decoder, t, bits);     // 3. Durchlauf: dessen Startmarker loest die Dekodierung von Durchlauf 2 aus

        if (sink.results.empty()) {
            std::printf("TESTFALL 1 FEHLGESCHLAGEN: kein Ergebnis dekodiert.\n");
            return 1;
        }
        const DecodedTime& r = sink.results.front();
        std::printf("Testfall 1 dekodiert: Tag %d, %02d.%02d.20%02d (Wochentag %d, 0=So), %02d:%02d\n",
                    r.dayOfYear, r.day, r.month, r.year2, r.weekday, r.hour, r.minute);
        bool ok = r.timeValid && r.dateValid &&
                  r.minute == 37 && r.hour == 14 &&
                  r.dayOfYear == 246 && r.year2 == 26 &&
                  r.month == 9 && r.day == 3 && r.weekday == 4 &&
                  !r.leapSecondAnnounced;
        if (!ok) {
            std::printf("TESTFALL 1 FEHLGESCHLAGEN: Werte weichen ab.\n");
            return 1;
        }
        std::printf("TESTFALL 1 OK.\n");
    }

    // ============================================================
    // Testfall 2: Minute 15 (Morseblock) - Datumsfelder duerfen fehlen,
    // Stunde/Minute muessen trotzdem korrekt ankommen.
    // ============================================================
    {
        TestSink sink;
        JjyDecoder decoder(sink);
        std::array<int,60> bits{};
        bits.fill(0);
        // Minute 15 -> 40,20,10,(4unused),8,4,2,1: 15=10+4+1 -> 0,0,1,0,0,1,0,1
        bits[1]=0; bits[2]=0; bits[3]=1; bits[5]=0; bits[6]=1; bits[7]=0; bits[8]=1;
        // Stunde 8 -> 20,10,(14unused),8,4,2,1: 8 -> 0,0,1,0,0,0
        bits[12]=0; bits[13]=0; bits[15]=1; bits[16]=0; bits[17]=0; bits[18]=0;
        // Paritaeten: Minute 15 hat Bits {3,6,8}=1 -> 3 Einsen (ungerade) -> PA2=1
        bits[37] = 1;
        // Stunde 8 hat Bit{15}=1 -> 1 Eins (ungerade) -> PA1=1
        bits[36] = 1;
        // Rest (Datumsfelder) bleibt bei den Default-Werten 0, wird aber wegen
        // simulateMorseFrom sowieso durch unklassifizierbare Pulse ueberschrieben.

        uint64_t t = 0;
        // Morseblock simuliert ab Sekunde 39 (typischer Bereich des Schlussblocks)
        t = emitJjyMinute(decoder, t, bits);                              // 1. Durchlauf: noch unsynchronisiert
        t = emitJjyMinute(decoder, t, bits, /*simulateMorseFrom=*/39);    // 2. Durchlauf: synchronisiert, Morseblock am Ende
        emitJjyMinute(decoder, t, bits);                                  // 3. Durchlauf: Startmarker loest Dekodierung von Durchlauf 2 aus

        if (sink.results.empty()) {
            std::printf("TESTFALL 2 FEHLGESCHLAGEN: kein Ergebnis dekodiert (Sync nach Morseblock verloren).\n");
            return 1;
        }
        const DecodedTime& r = sink.results.front();
        std::printf("Testfall 2 dekodiert: timeValid=%d dateValid=%d %02d:%02d\n",
                    r.timeValid, r.dateValid, r.hour, r.minute);
        bool ok = r.timeValid && !r.dateValid && r.hour == 8 && r.minute == 15;
        if (!ok) {
            std::printf("TESTFALL 2 FEHLGESCHLAGEN: erwartet timeValid=1,dateValid=0,08:15.\n");
            return 1;
        }
        std::printf("TESTFALL 2 OK: Uhrzeit trotz Morseblock korrekt, Datum korrekt als ungueltig markiert.\n");
    }

    std::printf("\nAlle JJY-Tests erfolgreich.\n");
    return 0;
}
