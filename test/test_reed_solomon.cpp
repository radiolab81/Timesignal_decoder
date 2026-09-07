// test_reed_solomon.cpp - Isolierter Regressionstest der eigenen
// RS(15,9) GF(16)-Implementierung (reed_solomon_gf16.hpp): prueft
// Kodierung/Dekodierung ohne Fehler, mit 1 und mit der maximal
// korrigierbaren Anzahl (3) Fehlern, korrekte Erkennung eines
// UNKORRIGIERBAREN Falls (4 Fehler - darf NICHT still falsch "korrigiert"
// werden), sowie einen breiteren Zufallstest.
#include "reed_solomon_gf16.hpp"
#include <cstdio>
#include <random>

using namespace pl225rs;

int main() {
    ReedSolomon15_9 rs;
    bool allOk = true;

    ReedSolomon15_9::Message msg = {1,2,3,4,5,6,7,8,9};
    for (auto& m : msg) m &= 0x0F;

    auto codeword = rs.generateCodeword(msg);
    printf("Codewort: ");
    for (auto c : codeword) printf("%X ", c);
    printf("\n");

    auto checkMessage = [&](const ReedSolomon15_9::Codeword& cw) {
        for (int i = 0; i < 9; i++) if (cw[i] != msg[i]) return false;
        return true;
    };

    {
        auto cw = codeword;
        bool failed = rs.recoverCodeword(cw);
        bool ok = !failed && checkMessage(cw);
        printf("Test 1 (keine Fehler): %s\n", ok ? "OK" : "FEHLER");
        allOk &= ok;
    }
    {
        auto cw = codeword;
        cw[3] ^= 0x05;
        bool failed = rs.recoverCodeword(cw);
        bool ok = !failed && checkMessage(cw);
        printf("Test 2 (1 Fehler): %s\n", ok ? "OK" : "FEHLER");
        allOk &= ok;
    }
    {
        auto cw = codeword;
        cw[0] ^= 0x03;
        cw[5] ^= 0x0A;
        cw[12] ^= 0x07;
        bool failed = rs.recoverCodeword(cw);
        bool ok = !failed && checkMessage(cw);
        printf("Test 3 (3 Fehler, max korrigierbar): %s\n", ok ? "OK" : "FEHLER");
        allOk &= ok;
    }
    {
        auto cw = codeword;
        cw[0] ^= 0x03; cw[2] ^= 0x0A; cw[5] ^= 0x07; cw[9] ^= 0x0C;
        bool failed = rs.recoverCodeword(cw);
        printf("Test 4 (4 Fehler, muss als unkorrigierbar erkannt werden): %s\n",
               failed ? "OK (erkannt)" : "FEHLER (still falsch korrigiert!)");
        allOk &= failed;
    }

    std::mt19937 rng(42);
    int successes = 0, trials = 500;
    for (int t = 0; t < trials; t++) {
        ReedSolomon15_9::Message m2;
        for (auto& v : m2) v = rng() % 16;
        auto cw2 = rs.generateCodeword(m2);
        int numErrors = rng() % 4; // 0-3, alles muss korrigierbar sein
        for (int e = 0; e < numErrors; e++) {
            int pos = rng() % 15;
            cw2[pos] ^= (rng() % 15) + 1;
        }
        bool failed = rs.recoverCodeword(cw2);
        bool ok = !failed;
        for (int i = 0; i < 9; i++) ok = ok && (cw2[i] == m2[i]);
        if (ok) successes++;
    }
    printf("Zufallstest (0-3 Fehler, muessen IMMER korrigierbar sein): %d/%d erfolgreich\n",
           successes, trials);
    allOk &= (successes == trials);

    if (!allOk) {
        printf("TEST FEHLGESCHLAGEN.\n");
        return 1;
    }
    printf("TEST OK: Kodierung/Dekodierung/Fehlererkennung stimmen ueberein.\n");
    return 0;
}
