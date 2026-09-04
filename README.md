# DCF77/MSF/JJY-Empfänger über Soundkarte (Debian 13)

![main](https://github.com/radiolab81/Timesignal_receiver/blob/main/images/timesignal_stations_map.svg)

Empfängt Langwellen-Zeitzeichensender (DCF77 auf 77,5 kHz, MSF auf 60 kHz,
JJY auf 40/60 kHz) indirekt über den NF-Ausgang eines Kommunikationsempfängers
(CW/SSB-Modus) via Soundkarte und dekodiert das Telegramm streng nach dem
jeweiligen Protokoll (Paritätsprüfung, Rahmen-Erkennungsmuster,
Plausibilitätsprüfung aller Felder). Das Protokoll wird per
`--protocol dcf77|msf|jjy` gewählt.

## Architektur (modular, für weitere Sender vorbereitet)

```
ALSA-Soundkarte
   │
   ├─► SpectrumAnalyzer      (FFT, ASCII-Anzeige zur Feinabstimmung des Receivers)
   │
   └─► ToneEnvelopeDetector  (Goertzel-Filter auf Zielton, erkennt 100/200ms-Absenkungen)
          │
          └─► ITimeSignalDecoder   (Interface)
                 └─► Dcf77Decoder  (implementiert)
                 └─► MsfDecoder    (implementiert)
                 └─► JjyDecoder    (implementiert)
                        │
                        └─► IDecodedTimeSink → ConsoleTimeSink (Ausgabe)
```

Alle Dateien sind einzeln kommentiert:

| Datei | Zweck |
|---|---|
| `include/audio_source.hpp/.cpp` | ALSA-Aufnahme, liefert Mono-Float-Blöcke |
| `include/spectrum_analyzer.hpp/.cpp` | FFT-Spektrum zur Feinabstimmung der Tonlage |
| `include/tone_envelope_detector.hpp/.cpp` | Goertzel-Hüllkurvenerkennung, protokollunabhängig |
| `include/time_signal_decoder.hpp` | Abstraktes Interface für beliebige Zeitzeichenprotokolle |
| `include/dcf77_decoder.hpp/.cpp` | DCF77-spezifische Protokollauswertung (BCD, gerade Parität) |
| `include/msf_decoder.hpp/.cpp` | MSF-spezifische Protokollauswertung (BCD, ungerade Parität, Doppelpuls-Bits) |
| `include/jjy_decoder.hpp/.cpp` | JJY-spezifische Protokollauswertung (BCD, Tag-des-Jahres→Datum, zwei-Marker-Sync) |
| `include/wav_audio_source.hpp/.cpp` | WAV-Datei als Alternative zur Soundkarte (gleicher Callback-Typ wie ALSA) |
| `include/audio_block_callback.hpp` | Gemeinsamer Callback-Typ für ALSA- und WAV-Quelle (abhängigkeitsfrei) |
| `test/test_msf_synthetic.cpp` | Offline-Test der MSF-Zustandsmaschine mit synthetischem Minutenrahmen |
| `test/test_jjy_synthetic.cpp` | Offline-Test der JJY-Zustandsmaschine (Normalfall + Morseblock-Sonderfall) |
| `src/main.cpp` | Verdrahtung aller Module, CLI (`--protocol dcf77\|msf\|jjy`) |

**Wichtig zum `CarrierDipEvent`:** Der `ToneEnvelopeDetector` meldet jede
einzelne Trägerabsenkung ("Dip") roh an den Decoder (Start-/Endzeit,
Dauer, Abstand zum vorherigen Dip-Start) - er interpretiert selbst nichts.
DCF77 und JJY brauchen genau einen Dip pro Sekunde/Bit; MSF braucht für
eine bestimmte Bitkombination (A=0,B=1) zwei kurze Dips innerhalb
derselben Sekunde (100ms ab/100ms an/100ms ab). Diese Aufteilung hält den
Audio-/Envelope-Pfad protokollunabhängig - der Decoder entscheidet, wie
viele Dips zu einem Bit gehören.

**Besonderheit JJY:** Anders als DCF77/MSF kodiert JJY die Bitwerte über
die Dauer der VOLLEN Trägerleistung (0,8s/0,5s/0,2s je Sekunde), nicht
über die Dauer der Absenkung selbst - unser Envelope-Detector misst aber
grundsätzlich Absenkungen. Das ist aber rechnerisch äquivalent: die
gemessene Dip-Dauer ergibt sich einfach als "1s minus Volldauer", was
direkt ablesbare Dip-Dauern von ~0,2s/~0,5s/~0,8s für die drei
JJY-Symbole liefert (siehe Kommentar in `jjy_decoder.hpp`). Auch die
Minutensynchronisation läuft bei JJY anders: es gibt keinen eindeutig
langen Marker (wie MSFs 500ms) und keine fehlende Absenkung (wie bei
DCF77) - stattdessen erkennt der Decoder zwei unmittelbar
aufeinanderfolgende Markerpulse (Sekunde 59 gefolgt von Sekunde 0 der
Folgeminute) als eindeutigen Minutenwechsel.

## Abhängigkeiten (Debian 13)

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libasound2-dev libfftw3-dev
```

## Bauen

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Audiogerät finden

```bash
arecord -l
```

Liefert z.B. `card 1: USB [USB Audio], device 0` → Gerätestring `hw:1,0`
oder besser `plughw:1,0` (macht automatisch Sample-Rate-Konvertierung,
falls die Karte 48 kHz nicht nativ unterstützt).

## Ausführen

```bash
# DCF77 (Deutschland, 77,5 kHz) - Default-Protokoll, von der Soundkarte
./Timesignal_receiver --device plughw:1,0 --tone 1000 --spectrum

# MSF (UK/Anthorn, 60 kHz)
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol msf --spectrum

# JJY (Japan, 40 kHz oder 60 kHz)
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol jjy --spectrum

# Aus einer WAV-Datei statt der Soundkarte (z.B. aufgezeichnete Empfangssitzung)
./Timesignal_receiver --wav-file aufnahme.wav --tone 500 --protocol jjy
```

- `--device` : ALSA-Gerät (siehe oben)
- `--wav-file` : liest eine WAV-Datei (PCM, 16 Bit, mono oder stereo) statt
  der Soundkarte ein. Überschreibt `--device` und `--rate` - die
  Abtastrate wird aus der Datei übernommen. Nützlich zum Debuggen/erneuten
  Auswerten aufgezeichneter Empfangssitzungen, ohne den Empfänger laufen
  lassen zu müssen
- `--tone`   : Zielton in Hz, auf den der Empfänger-NF-Ausgang gemischt wird
- `--protocol` : `dcf77` (Default), `msf` oder `jjy`
- `--spectrum` : zeigt alle paar Sekunden (Audiozeit) eine ASCII-Balkenanzeige
  des Spektrums um den Zielton, damit man den Communication-Receiver exakt
  darauf feinabstimmen kann (Peak muss auf dem `--tone`-Wert liegen); bei
  `--wav-file` läuft das einfach so schnell durch, wie die Datei verarbeitet wird
- `--verbose-bits` : gibt jedes dekodierte Rohbit einzeln aus (Debugging)

## Empfänger-Einstellung

1. Communication-Receiver auf **77,500 kHz** (DCF77), **60,000 kHz**
   (MSF) bzw. **40,000 kHz oder 60,000 kHz** (JJY - beide senden
   denselben Zeitcode), Betriebsart **CW oder SSB**.
2. `--spectrum` einschalten und die Feinabstimmung (RIT/Clarifier oder
   VFO) so justieren, dass der Peak im Terminal exakt bei der mit
   `--tone` gewählten Frequenz liegt (z. B. 1000 Hz).
3. Sobald der Pegelkontrast zwischen "Träger an" und "Träger abgesenkt"
   ausreichend groß ist, beginnt der `ToneEnvelopeDetector` automatisch
   mit der Dip-Erkennung; nach der ersten erkannten Minutenmarke
   synchronisiert sich der jeweilige Decoder.

## Protokolltreue

### DCF77
Der `Dcf77Decoder` prüft **strikt**:

- Bit 0 muss 0 sein (Minutenbeginn)
- Bit 20 muss 1 sein (Startbit Zeitinfo)
- Bit 17/Bit 18 müssen sich gegenseitig ausschließen (MESZ xor MEZ)
- **gerade** Parität über Minuten- (21–28), Stunden- (29–35) und
  Datumsblock (36–58)
- Plausibilität aller BCD-Felder (Minute ≤ 59, Stunde ≤ 23, Tag 1–31,
  Monat 1–12, Wochentag 1–7)

### MSF
Der `MsfDecoder` folgt dem offiziellen NPL-Datenblatt ("MSF 60 kHz Time
and Date Code", npl.co.uk/msf-signal) und prüft **strikt**:

- Rahmen-Erkennungsmuster 52A=0, 53A–58A=1 (sechsmal), 59A=0 — dieses
  eindeutige Muster `01111110` sichert ab, dass die Sekundenzählung
  tatsächlich phasenrichtig zum realen MSF-Rahmen steht (ein reiner
  Bitzähler würde eine falsche Synchronisation nicht bemerken)
- **ungerade** Parität über Jahr (17A–24A/54B), Monat+Tag (25A–35A/55B),
  Wochentag (36A–38A/56B), Stunde+Minute (39A–51A/57B) — MSF verwendet
  bewusst die entgegengesetzte Paritätskonvention zu DCF77
- Plausibilität aller BCD-Felder
- korrekte Behandlung des Doppelpuls-Bits (A=0/B=1): zwei kurze Dips
  (je ~100ms) innerhalb derselben Sekunde werden erkannt und zu einem
  Bitpaar zusammengeführt, statt fälschlich als zwei Sekunden gezählt
  zu werden

**Wochentagszählung beachten:** DCF77 zählt 1=Montag…7=Sonntag, MSF und
JJY zählen 0=Sonntag…6=Samstag. `DecodedTime.weekday` gibt jeweils die
protokolleigene Zählung zurück; `main.cpp` übersetzt das für die
Konsolenausgabe passend anhand von `sourceTag`.

### JJY
Der `JjyDecoder` folgt der offiziellen NICT-Dokumentation
("JJY - The JJY Signal", nict.go.jp/en/sts/jjy_signal.html) und prüft:

- **gerade** Parität PA1 (Stunde, Bit 35) und PA2 (Minute, Bit 36) -
  ohne korrekte Parität wird das gesamte Telegramm verworfen (auch die
  ansonsten gültige Uhrzeit), da PA1/PA2 laut Spezifikation direkt neben
  den Zeitfeldern liegen und ein Fehler dort auf eine gestörte Übertragung
  genau dieses kritischen Blocks hindeutet
- Marker-/Datenmuster: an den Positionen 0, 9, 19, 29, 39, 49, 59 wird
  zwingend ein Marker (0,2s Dip) erwartet, überall sonst ein Datenbit -
  eine Abweichung löst eine Neusynchronisation aus
- Plausibilität aller BCD-Felder (Minute ≤ 59, Stunde ≤ 23, Tag des
  Jahres 1-366, Jahr 0-99, Wochentag 0-6)
- Umrechnung Tag-des-Jahres → Kalenderdatum (Tag/Monat) inklusive
  Schaltjahrbehandlung

**Sonderbehandlung Minute 15/45 (Morseblock):** JJY ersetzt in diesen
beiden Minuten den Schlussteil des Telegramms durch Morsecode (Rufzeichen
"JJY") sowie Wartungsankündigungs-Bits. Diese Pulse folgen keinem der
drei regulären Zeitmuster. Statt die komplette Synchronisation zu
verwerfen (wodurch auch die bereits korrekt gelesene Uhrzeit verloren
ginge), überspringt der Decoder nur die betroffenen Sekundenpositionen
und meldet am Minutenende ein Ergebnis mit `timeValid=true` (Stunde/
Minute korrekt) aber `dateValid=false` (Datumsfelder unvollständig). Die
unabhängige "zwei Marker in Folge"-Erkennung synchronisiert zuverlässig
zur nächsten Minute.

Jede Verletzung führt zum Verwerfen des betroffenen Telegramms samt
Fehlermeldung auf stderr — es werden nie unvalidierte Zeitwerte
ausgegeben.

## Behobener Fehler: JJY-Paritätsfehler an Bit 35 (Off-by-one)

Frühere Versionen dieses Decoders platzierten PA1 (Stunden-Parität) auf
Bitposition 35 und PA2 (Minuten-Parität) auf 36. Das ist **falsch**: nach
dem Tag-des-Jahres-Block (endet bei Sekunde 33) folgen laut NICT-Spezifikation
und mehreren unabhängigen Quellen (u.a. Wikipedia/JJY, "Frequency and
Time"-Referenztabellen) **zwei** reservierte Positionen (34 UND 35), nicht
nur eine. Die korrekten Positionen sind:

| Bit | Bedeutung |
|-----|-----------|
| 34  | reserviert (immer 0) |
| 35  | reserviert (immer 0) |
| 36  | PA1 (gerade Parität über die 6 Stundenbits) |
| 37  | PA2 (gerade Parität über die 7 Minutenbits) |
| 38  | SU1 (Sommerzeitwechsel-Ankündigung, aktuell immer 0) |
| 39  | P4 (Positionsmarke) |
| 40  | SU2 (Sommerzeit aktiv, aktuell immer 0) |

Durch die Verschiebung um eine Position wurde bei der alten Zuordnung
tatsächlich ein permanent auf 0 stehendes Reservebit als PA1 gelesen,
was bei jedem echten Telegramm mit ungerader Stunden-Bitsumme zwangsläufig
einen Paritätsfehler auslöste — unabhängig von der Signalqualität, exakt
wie beobachtet. Verifiziert wurde die Korrektur an einer realen 60 kHz-
JJY-Aufnahme (siehe `test/test_jjy_synthetic.cpp` für den nachgebauten
Regressionstest sowie die Commit-Historie für die Analyse-Schritte).

## Offline-Tests der Zustandsmaschinen

Da insbesondere die Sonderfälle (MSF-Doppelpuls, JJY-Morseblock) ohne
echten Empfang schwer zu verifizieren sind, gibt es synthetische Tests,
die vollständige Minutenrahmen von Hand konstruieren und gegen die
erwarteten Ausgabewerte prüfen:

```bash
cd build
make test_msf_synthetic test_jjy_synthetic
./test_msf_synthetic
./test_jjy_synthetic
# oder alle zusammen:
ctest
```

Erwartete Ausgabe jeweils: `TEST(FALL) OK: ...` / `Alle JJY-Tests erfolgreich.`

## Erweiterung um weitere Sender (z.B. WWVB/USA, BPC/China)

1. Neue Datei `include/<name>_decoder.hpp` anlegen, von
   `timesignal::ITimeSignalDecoder` ableiten (siehe `dcf77_decoder.hpp`
   für Einzel-Dip-Protokolle, `msf_decoder.hpp` für Mehrfach-Dips pro
   Sekunde, oder `jjy_decoder.hpp` für Protokolle mit "voller Leistung
   kodiert die Dauer" statt "Absenkung kodiert die Dauer" als Vorlage).
2. Protokollspezifische Timing-Schwellen in `onCarrierDip(...)`
   auswerten.
3. In `main.cpp` bei der Decoder-Auswahl (`if (opt.protocol == ...)`)
   einen weiteren Zweig ergänzen; ggf. weiteren `--protocol`-Wert in
   `parseArgs` zulassen.
4. `CMakeLists.txt`: neue `.cpp`-Datei in `add_executable(...)` ergänzen.

Der Audio-/Spektrum-/Envelope-Pfad muss dafür **nicht** verändert werden -
er liefert bereits protokollneutrale `CarrierDipEvent`s.

## Bekannte Einschränkungen

- **DCF77:** Die Schaltsekunden-Sonderbehandlung (60 statt 59
  Bit-Positionen) wird als Sonderfall erkannt (größerer Minuten-Gap),
  aber nicht als eigenes 61. Bit explizit gespeichert — vor einer
  angekündigten Schaltsekunde (Juni/Dezember) noch einmal gegen die
  reale Aussendung testen.
- **MSF:** Schaltsekunden (59- oder 61-Sekunden-Minuten) werden nicht
  gesondert behandelt; die DUT1-Dekodierung (`decodeDut1DeciSeconds`)
  geht vereinfachend von einer lückenlosen "Thermometer-Code"-Folge aus,
  ohne Lückenprüfung innerhalb der Gruppe.
- **JJY:** Die vollständige LS1/LS2-Ankündigungslogik (Zeitpunkt/Dauer
  der Ankündigung innerhalb des Monats) wird nur als Rohbit ausgelesen,
  nicht interpretiert. Die Jahr-00-Problematik (2000 vs. 2100) wird nicht
  behandelt - das zweistellige Jahr wird immer als 20xx interpretiert
  (siehe Kommentar in `jjy_decoder.hpp`/`dayOfYearToMonthDay`). Der
  ST1-ST6-Wartungsankündigungsblock (nur relevant in Minute 15/45) wird
  nicht dekodiert.
- Die adaptive Schwelle im `ToneEnvelopeDetector` geht von einem relativ
  sauberen NF-Signal aus; bei starkem Rauschen empfiehlt sich ein
  schmalbandiges Empfänger-Filter (CW-Filter, wenige hundert Hz).
