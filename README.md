# DCF77/MSF/JJY/PCSK225 Receiver via Sound Card (Debian 13)
🇬🇧 English | [🇩🇪 Deutsch](README.de.md)

[![main](https://github.com/radiolab81/Timesignal_receiver/raw/main/images/timesignal_stations_map.svg)](https://github.com/radiolab81/Timesignal_receiver/blob/main/images/timesignal_stations_map.svg)

Receives longwave/mediumwave time-signal stations (DCF77 on 77.5 kHz, MSF on
60 kHz, JJY on 40/60 kHz, PCSK225/e-CzasPL on 225 kHz) indirectly via the
audio output of a communications receiver (CW/SSB mode) through a sound
card, and decodes the telegram in strict accordance with the respective
protocol (parity check, frame-recognition pattern, Reed–Solomon/CRC error
correction, plausibility checking of all fields). The protocol is selected
via `--protocol dcf77|msf|jjy|pl225`.

## Architecture (modular, prepared for additional stations)

DCF77/MSF/JJY are amplitude-shift-keying (on-off keying) schemes and share a
common signal path; PCSK225 is pure phase modulation and therefore requires
its own, parallel path (see below).

```
ALSA sound card / WAV file
   │
   ├─► SpectrumAnalyzer      (FFT, ASCII display for fine-tuning the receiver)
   │
   └─► ToneEnvelopeDetector  (Goertzel filter on the target tone, detects 100/200 ms dips)
          │
          └─► ITimeSignalDecoder   (interface)
                 └─► Dcf77Decoder  (implemented)
                 └─► MsfDecoder    (implemented)
                 └─► JjyDecoder    (implemented)
                        │
                        └─► IDecodedTimeSink → ConsoleTimeSink (output)

ALSA sound card (mono, SSB/BFO) / mono WAV file
   │
   └─► MonoToIqDownconverter (fixed NCO + low-pass filter -> complex stream)
          │
IQ WAV file (true I/Q) ──────────────────────────┐
          │                                      │
          └─────────────► Pl225Decoder ◄─────────┘
                 (dedicated PLL/Costas-loop phase tracking,
                  sync, Reed–Solomon, CRC8, descrambling)
                        │
                        └─► IDecodedTimeSink → ConsoleTimeSink (output, shared)
```

Every file is individually commented:

| File                                                          | Purpose                                                                                         |
| --------------------------------------------------------------| ------------------------------------------------------------------------------------------------|
| `include/audio_source.hpp/.cpp`                                | ALSA capture, provides mono float blocks                                                        |
| `include/spectrum_analyzer.hpp/.cpp`                           | FFT spectrum for fine-tuning the tone frequency                                                 |
| `include/tone_envelope_detector.hpp/.cpp`                      | Goertzel envelope detection, protocol-agnostic                                                  |
| `include/time_signal_decoder.hpp`                              | Abstract interface for arbitrary time-signal protocols                                          |
| `include/dcf77_decoder.hpp/.cpp`                               | DCF77-specific protocol evaluation (BCD, even parity)                                            |
| `include/msf_decoder.hpp/.cpp`                                 | MSF-specific protocol evaluation (BCD, odd parity, double-pulse bits)                            |
| `include/jjy_decoder.hpp/.cpp`                                 | JJY-specific protocol evaluation (BCD, day-of-year→date, two-marker sync)                        |
| `include/pl225_decoder.hpp/.cpp`                               | PL225/e-CzasPL: phase tracking (PLL/Costas loop), sync, Reed–Solomon, CRC8, descrambling         |
| `include/mono_to_iq_downconverter.hpp`                         | SSB/BFO mono signal → complex stream (fixed mixer + low-pass filter)                             |
| `include/crc8.hpp`                                             | Generic CRC8 implementation (for PL225)                                                          |
| `include/reed_solomon_gf16.hpp`                                | Custom RS(15,9) GF(16) implementation (Berlekamp–Massey, permissively licensed)                  |
| `include/wav_audio_source.hpp/.cpp`                            | Mono WAV file as an alternative to the sound card                                                |
| `include/iq_wav_audio_source.hpp/.cpp`                         | Stereo WAV file as an IQ source (I = left, Q = right)                                            |
| `include/audio_block_callback.hpp` / `iq_block_callback.hpp`  | Shared, dependency-free callback types                                                           |
| `test/test_msf_synthetic.cpp`                                  | Offline test of the MSF state machine using a synthetic minute frame                             |
| `test/test_jjy_synthetic.cpp`                                  | Offline test of the JJY state machine (normal case + Morse-block special case)                   |
| `test/test_pl225_synthetic.cpp`                                | Offline test of the PL225 decoder (PCSK225) with a genuinely RS/CRC-encoded test frame            |
| `test/test_reed_solomon.cpp`                                   | Isolated test of the custom RS(15,9) GF(16) implementation (0–4 errors)                          |
| `src/main.cpp`                                                 | Wiring of all modules, CLI (`--protocol dcf77|msf|jjy|pl225`)                                    |

**Important note on `CarrierDipEvent`:** The `ToneEnvelopeDetector` reports
every individual carrier dip raw to the decoder (start/end time, duration,
distance to the previous dip start) — it does not interpret anything
itself. DCF77 and JJY each require exactly one dip per second/bit; MSF
requires two short dips within the same second (100 ms off / 100 ms on /
100 ms off) for a specific bit combination (A=0, B=1). This separation of
concerns keeps the audio/envelope path protocol-agnostic — the decoder
alone determines how many dips constitute one bit.

**JJY peculiarity:** Unlike DCF77/MSF, JJY encodes bit values via the
duration of FULL carrier power (0.8 s / 0.5 s / 0.2 s per second), not via
the duration of the dip itself — yet our envelope detector fundamentally
measures dips. This is, however, mathematically equivalent: the measured
dip duration simply results as "1 s minus full-power duration," which
directly yields dip durations of roughly 0.2 s / 0.5 s / 0.8 s for the
three JJY symbols (see the comment in `jjy_decoder.hpp`). Minute
synchronization also works differently for JJY: there is no uniquely long
marker (as with MSF's 500 ms) and no missing dip (as with DCF77) — instead,
the decoder recognizes two immediately consecutive marker pulses (second 59
followed by second 0 of the next minute) as an unambiguous minute
rollover.

## Dependencies (Debian 13)

```
sudo apt update
sudo apt install build-essential cmake pkg-config libasound2-dev libfftw3-dev
```

## Building

```
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Finding the audio device

```
arecord -l
```

Yields e.g. `card 1: USB [USB Audio], device 0` → device string `hw:1,0`,
or preferably `plughw:1,0` (performs automatic sample-rate conversion if
the card does not natively support 48 kHz).

## Running

```
# DCF77 (Germany, 77.5 kHz) - default protocol, from the sound card
./Timesignal_receiver --device plughw:1,0 --tone 1000 --spectrum

# MSF (UK/Anthorn, 60 kHz)
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol msf --spectrum

# JJY (Japan, 40 kHz or 60 kHz)
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol jjy --spectrum

# From a WAV file instead of the sound card (e.g. a recorded reception session)
./Timesignal_receiver --wav-file recording.wav --tone 500 --protocol jjy

# PCSK225 (Poland, 225 kHz) - SSB/USB reception with BFO, sound card or mono WAV
./Timesignal_receiver --device plughw:1,0 --tone 1000 --protocol pl225
./Timesignal_receiver --wav-file 225khz_ssb.wav --tone 1000 --protocol pl225

# PCSK225 from a genuine IQ recording (stereo WAV, I=left/Q=right)
./Timesignal_receiver --iq-wav-file 225khz_iq.wav --protocol pl225
```

- `--device`: ALSA device (see above)
- `--wav-file`: reads a (mono or stereo) WAV file instead of the sound
  card. Overrides `--device` and `--rate` — the sample rate is taken from
  the file. Useful for debugging/re-evaluating recorded reception sessions
  without having to run the actual receiver.
- `--iq-wav-file`: only for `--protocol pl225` — a stereo WAV file
  containing a true IQ signal (I = left channel, Q = right channel), e.g.
  from SDR#, GQRX, or GNU Radio. Overrides `--wav-file`/`--device`.
- `--tone`: target tone/BFO frequency in Hz to which the receiver's audio
  output is mixed.
- `--protocol`: `dcf77` (default), `msf`, `jjy`, or `pl225`.
- `--spectrum`: displays an ASCII bar-graph spectrum around the target
  tone every few seconds (audio time), allowing the communications
  receiver to be precisely tuned (the peak must lie exactly on the
  `--tone` value); with `--wav-file` this simply runs as fast as the file
  is processed. Not available/needed with `--protocol pl225`.
- `--verbose-bits`: outputs every decoded raw bit individually (for
  debugging; has no effect with PL225).

## Receiver setup

1. Tune the communications receiver to **77.500 kHz** (DCF77),
   **60.000 kHz** (MSF), **40.000 kHz or 60.000 kHz** (JJY — both transmit
   the same time code), or **225.000 kHz in USB mode** (PCSK225 — see
   below; **do not** use AM mode!), operating mode **CW or SSB**.
2. Enable `--spectrum` (not applicable for PL225) and adjust the
   fine-tuning (RIT/clarifier or VFO) so that the peak in the terminal
   lies exactly at the frequency selected via `--tone` (e.g. 1000 Hz).
3. As soon as the level contrast between "carrier on" and "carrier
   attenuated" is sufficiently large, the `ToneEnvelopeDetector`
   automatically begins dip detection; the respective decoder
   synchronizes after the first detected minute marker. (For PL225, the
   built-in PLL/Costas loop handles this automatically — see below.)

## Protocol conformance

### DCF77

The `Dcf77Decoder` **strictly** checks:

- Bit 0 must be 0 (start of minute)
- Bit 20 must be 1 (time-information start bit)
- Bits 17/18 must be mutually exclusive (CEST xor CET)
- **Even** parity across the minute (21–28), hour (29–35), and date
  (36–58) blocks
- Plausibility of all BCD fields (minute ≤ 59, hour ≤ 23, day 1–31,
  month 1–12, weekday 1–7)

### MSF

The `MsfDecoder` follows the official NPL data sheet ("MSF 60 kHz Time and
Date Code," npl.co.uk/msf-signal) and **strictly** checks:

- The frame-recognition pattern 52A=0, 53A–58A=1 (six times), 59A=0 — this
  unambiguous `01111110` pattern confirms that the second count is
  genuinely phase-aligned with the actual MSF frame (a plain bit counter
  would not detect a false synchronization)
- **Odd** parity across year (17A–24A/54B), month+day (25A–35A/55B),
  weekday (36A–38A/56B), and hour+minute (39A–51A/57B) — MSF deliberately
  uses the opposite parity convention to DCF77
- Plausibility of all BCD fields
- Correct handling of the double-pulse bit (A=0/B=1): two short dips (each
  ~100 ms) within the same second are detected and merged into a single
  bit pair, rather than mistakenly being counted as two seconds

**Note on weekday numbering:** DCF77 counts 1 = Monday … 7 = Sunday, while
MSF and JJY count 0 = Sunday … 6 = Saturday. `DecodedTime.weekday` always
returns the protocol's own numbering; `main.cpp` translates this
appropriately for console output based on `sourceTag`.

### JJY

The `JjyDecoder` follows the official NICT documentation ("JJY – The JJY
Signal," nict.go.jp/en/sts/jjy_signal.html) and checks:

- **Even** parity PA1 (hour, bit 35) and PA2 (minute, bit 36) — if the
  parity is incorrect, the entire telegram is discarded (including an
  otherwise valid time), since per specification PA1/PA2 sit directly
  adjacent to the time fields, and an error there indicates a disturbed
  transmission of precisely this critical block
- Marker/data pattern: a marker (0.2 s dip) is mandatorily expected at
  positions 0, 9, 19, 29, 39, 49, 59, and a data bit everywhere else — any
  deviation triggers resynchronization
- Plausibility of all BCD fields (minute ≤ 59, hour ≤ 23, day of year
  1–366, year 0–99, weekday 0–6)
- Conversion of day-of-year → calendar date (day/month), including leap
  year handling

**Special handling for minutes 15/45 (Morse block):** In these two
minutes, JJY replaces the final portion of the telegram with Morse code
(call sign "JJY") and maintenance-announcement bits. These pulses do not
follow any of the three regular timing patterns. Rather than discarding
the entire synchronization (which would also lose the already correctly
read time), the decoder simply skips the affected second positions and,
at the end of the minute, reports a result with `timeValid=true`
(hour/minute correct) but `dateValid=false` (date fields incomplete). The
independent "two consecutive markers" detection reliably synchronizes to
the next minute.

Every violation causes the affected telegram to be discarded along with
an error message on stderr — unvalidated time values are never output.

### PL225/PCSK225 (e-CzasPL, Poland)

The `Pl225Decoder` **strictly** checks:

- Sync word 0x5555 (16 bits alternating), header byte 0x60, message
  prefix 0b101 — anything else (e.g. the "ENEA" lighting-control messages
  that share the same channel) is cleanly discarded rather than
  misinterpreted
- Reed–Solomon(15,9) error correction over GF(16) (up to 3 correctable
  4-bit symbols) for the 37-bit data field
- CRC8 (polynomial 0x07) over the **scrambled** bytes 3–7 (not over the
  descrambled values — a peculiarity documented by SP6HFE), including
  correction of the single bit not protected by RS (SK1)
- Adaptive threshold (minimum/maximum per frame candidate) instead of a
  fixed 22.5° threshold, since the actual phase deviation achieved after
  low-pass filtering is not always exactly 45°

Output includes, among other things, the time-zone offset (0/+1/+2/+3 h,
including the "swapped" bit assignment documented by PA3FWM), leap-second
announcements (including sign: insertion/deletion), and transmitter
maintenance status.

**Known limitation:** According to PA3FWM himself, the exact instant to
which a frame refers is subject to 100–200 ms of jitter even at the
transmitter side — unproblematic for a wall clock, but unsuitable for an
NTP server.

## Fixed bug: JJY parity error at bit 35 (off-by-one)

Earlier versions of this decoder placed PA1 (hour parity) at bit position
35 and PA2 (minute parity) at 36. This is **incorrect**: according to the
NICT specification and several independent sources (including
Wikipedia/JJY and "Frequency and Time" reference tables), the day-of-year
block (ending at second 33) is followed by **two** reserved positions (34
AND 35), not just one. The correct positions are:

| Bit | Meaning                                                  |
| --- | ----------------------------------------------------------|
| 34  | Reserved (always 0)                                       |
| 35  | Reserved (always 0)                                        |
| 36  | PA1 (even parity over the 6 hour bits)                     |
| 37  | PA2 (even parity over the 7 minute bits)                    |
| 38  | SU1 (daylight-saving-time change announcement, currently always 0) |
| 39  | P4 (position marker)                                        |
| 40  | SU2 (daylight saving time active, currently always 0)      |

Due to this one-position shift, the old mapping effectively read a
permanently-zero reserved bit as PA1, which inevitably triggered a parity
error on every genuine telegram with an odd hour-bit sum — regardless of
signal quality, exactly as observed. The fix was verified against an
actual 60 kHz JJY recording (see `test/test_jjy_synthetic.cpp` for the
reconstructed regression test, and the commit history for the analysis
steps).

## Offline tests of the state machines

Since the special cases in particular (MSF double pulse, JJY Morse block)
are difficult to verify without genuine reception, synthetic tests exist
that manually construct complete minute frames and check them against the
expected output values:

```
cd build
make test_msf_synthetic test_jjy_synthetic test_pl225_synthetic
./test_msf_synthetic
./test_jjy_synthetic
./test_pl225_synthetic
# or all together:
ctest
```

Expected output, respectively: `TEST(CASE) OK: ...` / `All JJY tests
passed.` / `TEST OK: all fields match the expected values.` (PL225)

## Extending to additional stations (e.g. WWVB/USA, BPC/China)

1. Create a new file `include/<name>_decoder.hpp`, deriving from
   `timesignal::ITimeSignalDecoder` (see `dcf77_decoder.hpp` for
   single-dip protocols, `msf_decoder.hpp` for multiple dips per second,
   or `jjy_decoder.hpp` for protocols where "full power encodes the
   duration" rather than "attenuation encodes the duration," as a
   template).
2. Evaluate protocol-specific timing thresholds in `onCarrierDip(...)`.
3. In `main.cpp`, add a further branch to the decoder selection (`if
   (opt.protocol == ...)`); if necessary, allow an additional
   `--protocol` value in `parseArgs`.
4. `CMakeLists.txt`: add the new `.cpp` file to `add_executable(...)`.

The audio/spectrum/envelope path itself does **not** need to be modified
for this — it already delivers protocol-neutral `CarrierDipEvent`s.

For phase/IQ-based methods (analogous to PL225), use
`pl225_decoder.hpp`/`mono_to_iq_downconverter.hpp` as a reference instead,
and wire up a separate, parallel path in `main.cpp` analogous to
`runPl225(...)`.

## Known limitations

- **DCF77:** The leap-second special case (60 instead of 59 bit
  positions) is detected as a special case (a larger minute gap), but is
  not explicitly stored as a distinct 61st bit — test again against the
  actual transmission ahead of an announced leap second (June/December).
- **MSF:** Leap seconds (59- or 61-second minutes) are not specifically
  handled; DUT1 decoding (`decodeDut1DeciSeconds`) simplistically assumes
  a gap-free "thermometer code" sequence, without checking for gaps
  within the group.
- **JJY:** The complete LS1/LS2 announcement logic (timing/duration of
  the announcement within the month) is only read out as a raw bit, not
  interpreted. The year-00 ambiguity (2000 vs. 2100) is not handled — the
  two-digit year is always interpreted as 20xx (see the comment in
  `jjy_decoder.hpp`/`dayOfYearToMonthDay`). The ST1–ST6
  maintenance-announcement block (relevant only in minutes 15/45) is not
  decoded.
- **PL225/PCSK225:** Leap seconds are not handled by dedicated calendar
  logic (the transmitted timestamp excludes them by design, according to
  PA3FWM). The two-digit year is always interpreted as 20xx. The
  `MonoToIqDownconverter` uses a FIXED NCO frequency (`--tone`); if the
  actual BFO deviation exceeds a few Hz, the PLL may miss its lock-in
  range — in this case, adjust `--tone` more precisely to the BFO
  frequency actually set on the receiver. Live IQ reception directly from
  the sound card (stereo, e.g. for direct-sampling SDRs) is not yet
  implemented — only IQ WAV files (`--iq-wav-file`) are supported; for
  live operation, the SSB/mono path is currently recommended.
- The adaptive threshold in the `ToneEnvelopeDetector` assumes a
  relatively clean audio signal; under strong noise, a narrowband
  receiver filter (CW filter, a few hundred Hz) is recommended.
