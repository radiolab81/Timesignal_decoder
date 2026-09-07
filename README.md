# DCF77/MSF/JJY/PCSK225-Empfänger über Soundkarte (Debian 13)

![main](https://github.com/radiolab81/Timesignal_receiver/blob/main/images/timesignal_stations_map.svg)

Empfängt Langwellen-/Mittelwellen-Zeitzeichensender (DCF77 auf 77,5 kHz, MSF
auf 60 kHz, JJY auf 40/60 kHz, PCSK225/e-CzasPL auf 225 kHz) indirekt über den
NF-Ausgang eines Kommunikationsempfängers (CW/SSB-Modus) via Soundkarte und
dekodiert das Telegramm streng nach dem jeweiligen Protokoll (Paritätsprüfung,
Rahmen-Erkennungsmuster, Reed-Solomon/CRC-Fehlerkorrektur, Plausibilitäts-
prüfung aller Felder). Das Protokoll wird per `--protocol dcf77|msf|jjy|pl225`
gewählt.



## Architektur (modular, für weitere Sender vorbereitet)

DCF77/MSF/JJY sind Amplitudentastungs-Verfahren und teilen sich einen
gemeinsamen Signalpfad; PCSK225 ist reine Phasenmodulation und braucht daher
einen eigenen, parallelen Pfad (siehe unten).

```
ALSA-Soundkarte / WAV-Datei
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

ALSA-Soundkarte (mono, SSB/BFO) / Mono-WAV-Datei
   │
   └─► MonoToIqDownconverter (feste NCO + Tiefpass -> komplexer Strom)
          │
IQ-WAV-Datei (echtes I/Q) ─────────────────────┐
          │                                     │
          └─────────────► Pl225Decoder ◄────────┘
                 (eigene PLL/Costas-Loop-Phasennachfuehrung,
                  Sync, Reed-Solomon, CRC8, Descrambling)
                        │
                        └─► IDecodedTimeSink → ConsoleTimeSink (Ausgabe, gemeinsam genutzt)
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
| `include/pl225_decoder.hpp/.cpp` | PL225/e-CzasPL: Phasennachführung (PLL/Costas-Loop), Sync, Reed-Solomon, CRC8, Descrambling |
| `include/mono_to_iq_downconverter.hpp` | SSB/BFO-Mono-Signal → komplexer Strom (fester Mischer + Tiefpass) |
| `include/crc8.hpp` | Generische CRC8-Implementierung (für PL225) |
| `include/reed_solomon_gf16.hpp` | Eigene RS(15,9) GF(16)-Implementierung (Berlekamp-Massey, freie Lizenz) |
| `include/wav_audio_source.hpp/.cpp` | Mono-WAV-Datei als Alternative zur Soundkarte |
| `include/iq_wav_audio_source.hpp/.cpp` | Stereo-WAV-Datei als IQ-Quelle (I=links, Q=rechts) |
| `include/audio_block_callback.hpp` / `iq_block_callback.hpp` | Gemeinsame, abhängigkeitsfreie Callback-Typen |
| `test/test_msf_synthetic.cpp` | Offline-Test der MSF-Zustandsmaschine mit synthetischem Minutenrahmen |
| `test/test_jjy_synthetic.cpp` | Offline-Test der JJY-Zustandsmaschine (Normalfall + Morseblock-Sonderfall) |
| `test/test_pl225_synthetic.cpp` | Offline-Test des PL225-Decoders (PCSK225) mit echt RS/CRC-kodiertem Testrahmen |
| `test/test_reed_solomon.cpp` | Isolierter Test der eigenen RS(15,9) GF(16)-Implementierung (0-4 Fehler) |
| `src/main.cpp` | Verdrahtung aller Module, CLI (`--protocol dcf77\|msf\|jjy\|pl225`) |

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

# PCSK225 (Polen, 225 kHz) - SSB/USB-Empfang mit BFO, Soundkarte oder Mono-WAV
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol pl225
./Timesignal_receiver --wav-file 225khz_ssb.wav --tone 1000 --protocol pl225

# PCSK225 aus einer echten IQ-Aufnahme (stereo WAV, I=links/Q=rechts)
./Timesignal_receiver --iq-wav-file 225khz_iq.wav --protocol pl225
```

- `--device` : ALSA-Gerät (siehe oben)
- `--wav-file` : liest eine (Mono- oder Stereo-)WAV-Datei statt der Soundkarte
  ein. Überschreibt `--device` und `--rate` - die Abtastrate wird aus der
  Datei übernommen. Nützlich zum Debuggen/erneuten Auswerten aufgezeichneter
  Empfangssitzungen, ohne den Empfänger laufen lassen zu müssen
- `--iq-wav-file` : nur `--protocol pl225` - stereo WAV-Datei mit echtem
  IQ-Signal (I=linker Kanal, Q=rechter Kanal), z.B. aus SDR#, GQRX oder
  GNU Radio. Überschreibt `--wav-file`/`--device`
- `--tone`   : Zielton/BFO-Frequenz in Hz, auf die der Empfänger-NF-Ausgang
  gemischt wird
- `--protocol` : `dcf77` (Default), `msf`, `jjy` oder `pl225`
- `--spectrum` : zeigt alle paar Sekunden (Audiozeit) eine ASCII-Balkenanzeige
  des Spektrums um den Zielton, damit man den Communication-Receiver exakt
  darauf feinabstimmen kann (Peak muss auf dem `--tone`-Wert liegen); bei
  `--wav-file` läuft das einfach so schnell durch, wie die Datei verarbeitet
  wird. Nicht verfügbar/nicht nötig bei `--protocol pl225`
- `--verbose-bits` : gibt jedes dekodierte Rohbit einzeln aus (Debugging;
  bei PL225 ohne Wirkung)

## Empfänger-Einstellung

1. Communication-Receiver auf **77,500 kHz** (DCF77), **60,000 kHz**
   (MSF), **40,000 kHz oder 60,000 kHz** (JJY - beide senden denselben
   Zeitcode) bzw. **225,000 kHz im USB-Modus** (PCSK225 - siehe unten,
   **nicht** AM-Modus verwenden!), Betriebsart **CW oder SSB**.
2. `--spectrum` einschalten (nicht bei PL225) und die Feinabstimmung
   (RIT/Clarifier oder VFO) so justieren, dass der Peak im Terminal exakt
   bei der mit `--tone` gewählten Frequenz liegt (z. B. 1000 Hz).
3. Sobald der Pegelkontrast zwischen "Träger an" und "Träger abgesenkt"
   ausreichend groß ist, beginnt der `ToneEnvelopeDetector` automatisch
   mit der Dip-Erkennung; nach der ersten erkannten Minutenmarke
   synchronisiert sich der jeweilige Decoder. (Bei PL225 übernimmt das
   die eingebaute PLL/Costas-Loop automatisch, siehe unten.)

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

### PL225/PCSK225 (e-CzasPL, Polen)

Der `Pl225Decoder` prüft **strikt**:

- Sync-Wort 0x5555 (16 Bit alternierend), Header-Byte 0x60, Nachrichten-
  präfix 0b101 - alles andere (z.B. die "ENEA"-Lichtsteuerungsnachrichten,
  die denselben Kanal mitnutzen) wird sauber verworfen, nicht fehlinterpretiert
- Reed-Solomon(15,9)-Fehlerkorrektur über GF(16) (bis zu 3 korrigierbare
  4-Bit-Symbole) für das 37-Bit-Datenfeld
- CRC8 (Polynom 0x07) über die **verschlüsselten** ("gescrambelten") Bytes
  3-7 (nicht über die entschlüsselten Werte - eine von SP6HFE dokumentierte
  Besonderheit) inkl. Korrektur des einzigen nicht RS-geschützten Bits (SK1)
- adaptiver Schwellenwert (Minimum/Maximum je Rahmen-Kandidat) statt eines
  festen 22,5°-Schwellenwerts, da der tatsächlich erreichte Phasenhub nach
  Tiefpassfilterung nicht immer exakt 45° beträgt

Ausgegeben werden u.a. Zeitzonen-Offset (0/+1/+2/+3h, inkl. der von PA3FWM
dokumentierten "vertauschten" Bit-Zuordnung), Schaltsekunden-Ankündigung
(inkl. Vorzeichen: Einfügen/Entfernen) und Sender-Wartungsstatus.

**Bekannte Einschränkung:** Der genaue Zeitpunkt, auf den sich ein Frame
bezieht, ist laut PA3FWM selbst senderseitig mit 100-200ms Jitter behaftet
- für eine Wanduhr unproblematisch, für einen NTP-Server nicht geeignet.


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
make test_msf_synthetic test_jjy_synthetic test_pl225_synthetic
./test_msf_synthetic
./test_jjy_synthetic
./test_pl225_synthetic
# oder alle zusammen:
ctest
```

Erwartete Ausgabe jeweils: `TEST(FALL) OK: ...` / `Alle JJY-Tests erfolgreich.` /
`TEST OK: alle Felder stimmen mit den Erwartungswerten überein.` (PL225)

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

Für Phasen-/IQ-basierte Verfahren (analog zu PL225) orientiert man sich
stattdessen an `pl225_decoder.hpp`/`mono_to_iq_downconverter.hpp` und
verdrahtet in `main.cpp` analog zu `runPl225(...)` einen eigenen,
parallelen Pfad.

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
- **PL225/PCSK225:** Schaltsekunden werden nicht durch eigene Kalenderlogik
  behandelt (der gesendete Zeitstempel schließt sie laut PA3FWM ohnehin
  aus). Das zweistellige Jahr wird immer als 20xx interpretiert. Der
  `MonoToIqDownconverter` nutzt eine FESTE NCO-Frequenz (`--tone`); liegt
  die tatsächliche BFO-Abweichung außerhalb weniger Hz, kann die PLL den
  Einrastbereich verfehlen - in diesem Fall `--tone` genauer an die am
  Empfänger tatsächlich eingestellte BFO-Frequenz anpassen. Live-IQ-Empfang
  direkt von der Soundkarte (stereo, z.B. bei Direktabtast-SDRs) ist noch
  nicht implementiert, nur IQ-WAV-Dateien (`--iq-wav-file`) - für Live-Betrieb
  daher aktuell der SSB/Mono-Pfad empfohlen.
- Die adaptive Schwelle im `ToneEnvelopeDetector` geht von einem relativ
  sauberen NF-Signal aus; bei starkem Rauschen empfiehlt sich ein
  schmalbandiges Empfänger-Filter (CW-Filter, wenige hundert Hz).
