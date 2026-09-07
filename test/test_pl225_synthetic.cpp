// test_pl225_synthetic.cpp - Verifiziert die PL225-Dekodierkette (Sync,
// Reed-Solomon, CRC8, Descrambling, Feldextraktion, PLL-Phasennachfuehrung)
// anhand eines von Hand mit einer echten RS-Kodierung erzeugten,
// garantiert gueltigen 96-Bit-Rahmens - unabhaengig von echten Aufnahmen.
//
// Der Testrahmen wurde mit reed_solomon_gf16.hpp generiert fuer den
// Zeitstempel 2026-09-02 12:00:00 UTC (Sekunden seit 2000-01-01, gedrittelt),
// TZ-Bits=01 (+2h), keine Schaltsekunde, kein Wartungsstatus:
//   55 55 60 A2 1B 22 2D 0B 17 70 3F 39
#include "pl225_decoder.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <array>

using namespace timesignal;

struct TestSink : IDecodedTimeSink {
    std::vector<DecodedTime> results;
    void onDecodedTime(const DecodedTime& t) override { results.push_back(t); }
    void onDecodeError(const std::string& reason) override {
        std::printf("[Hinweis] %s\n", reason.c_str());
    }
};

int main() {
    const std::array<uint8_t, 12> frame = {
        0x55, 0x55, 0x60, 0xA2, 0x1B, 0x22, 0x2D, 0x0B, 0x17, 0x70, 0x3F, 0x39
    };

    const unsigned sampleRate = 12000;
    const double bitRateHz = 50.0;
    const double samplesPerBit = sampleRate / bitRateHz; // 240

    // Bitfolge (MSB first) aus den Frame-Bytes aufbauen.
    std::vector<int> bits;
    for (uint8_t b : frame) {
        for (int i = 7; i >= 0; --i) bits.push_back((b >> i) & 1);
    }

    // Fuer jedes Bit samplesPerBit komplexe Samples erzeugen: bit=1 -> 0 Grad,
    // bit=0 -> -45 Grad (keine Slew-Simulation noetig - der Decoder tastet
    // ohnehin nur die Bitmitte ab).
    std::vector<std::complex<float>> iqSamples;
    // Ein paar Vorlauf-Samples bei Phase 0 (Ruhezustand vor dem Rahmen).
    for (int i = 0; i < 100; ++i) iqSamples.emplace_back(1.0f, 0.0f);

    for (int bit : bits) {
        double phaseRad = (bit == 1) ? 0.0 : (-45.0 * M_PI / 180.0);
        for (int s = 0; s < static_cast<int>(samplesPerBit); ++s) {
            iqSamples.emplace_back(static_cast<float>(std::cos(phaseRad)),
                                    static_cast<float>(std::sin(phaseRad)));
        }
    }
    // Nachlauf: zurueck in Ruhezustand + zweiter (leerer) Rahmenabstand, damit
    // die Sync-Suche nach dem echten Rahmen nicht auf fehlende Daten laeuft.
    for (int i = 0; i < static_cast<int>(samplesPerBit) * 20; ++i) iqSamples.emplace_back(1.0f, 0.0f);

    TestSink sink;
    pl225::Pl225Decoder decoder(sink, sampleRate);

    // In realistischen Blockgroessen zufuehren (100ms), wie es main.cpp auch tut.
    size_t blockSize = sampleRate / 10;
    std::vector<std::complex<float>> block;
    for (size_t i = 0; i < iqSamples.size(); ++i) {
        block.push_back(iqSamples[i]);
        if (block.size() == blockSize || i == iqSamples.size() - 1) {
            decoder.processBlock(block, 0);
            block.clear();
        }
    }

    if (sink.results.empty()) {
        std::printf("TEST FEHLGESCHLAGEN: kein Ergebnis dekodiert.\n");
        return 1;
    }

    const DecodedTime& r = sink.results.front();
    std::printf("Dekodiert: %02d.%02d.20%02d (Wochentag %d, ISO 1=Mo) %02d:%02d:%02d UTC TZ=+%dh Status=%s\n",
                r.day, r.month, r.year2, r.weekday, r.hour, r.minute, r.second,
                r.tzOffsetHours, r.transmitterStateText.c_str());

    bool ok = r.timeValid && r.dateValid &&
              r.year2 == 26 && r.month == 9 && r.day == 2 &&
              r.hour == 12 && r.minute == 0 && r.second == 0 &&
              r.weekday == 3 && // 2026-09-02 ist ein Mittwoch (ISO 3)
              r.tzOffsetHours == 2 &&
              !r.leapSecondAnnounced &&
              r.transmitterStateText == "Normalbetrieb";

    if (!ok) {
        std::printf("TEST FEHLGESCHLAGEN: Werte weichen von den Erwartungswerten ab.\n");
        return 1;
    }

    std::printf("TEST OK: alle Felder stimmen mit den Erwartungswerten ueberein.\n");
    return 0;
}
