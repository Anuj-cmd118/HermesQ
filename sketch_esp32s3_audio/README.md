# HermesQ Audio Co-Processor (Waveshare ESP32-S3 Zero)

This sketch is the ESP32-S3 audio co-processor firmware for HermesQ's
voice command pipeline. It is a separate Arduino sketch flashed to a
second board — a Waveshare ESP32-S3 Zero — not the UNO Q itself.

**Important:** the Linux-side listener for this UART is
`host_daemon/audio_bridge.py`, which runs directly on the UNO Q's Debian
host (via SSH), *not* inside the App Lab-managed `python/` app. App Lab's
Python container has no access to `/dev/tty*` USB-serial devices — see
`host_daemon/README.md` for why and how it's wired up instead.

## Why it's a separate board

The Arduino UNO Q does not break out I2S pins, so an I2S microphone and
an I2S speaker can't be wired to it directly. This S3 Zero is a dedicated
audio front-end: it owns the mic and speaker, and relays audio to/from
the UNO Q's **Linux runtime** over a plain UART link — a second, separate
UART from the Arduino RouterBridge channel already used between the
UNO Q's STM32 core and its own Linux side.

The S3 does **no** speech-to-text and **no** intent parsing. It only:

1. Captures mic audio while push-to-talk is held.
2. Streams it to the UNO Q Linux runtime as framed PCM over UART.
3. Plays a short local tone (ack / error / listening) when told to by
   the Linux runtime.

## Wiring

| Signal | S3 Zero pin | Notes |
|---|---|---|
| Mic BCLK | GPIO4 | INMP441 SCK |
| Mic WS | GPIO5 | INMP441 LRCL |
| Mic SD | GPIO6 | INMP441 DOUT |
| Mic L/R | — | tie to GND (mono, left channel) |
| Speaker BCLK | GPIO7 | MAX98357A BCLK |
| Speaker LRC | GPIO15 | MAX98357A LRC |
| Speaker DIN | GPIO16 | MAX98357A DIN |
| UART TX | GPIO17 | -> UNO Q Linux-side RX |
| UART RX | GPIO18 | <- UNO Q Linux-side TX |
| Push-to-talk | GPIO0 (onboard BOOT) | active-LOW, or wire an external button here |
| Status LED | GPIO21 (onboard WS2812) | idle / listening / sending / error colors |

Adjust the `#define` pins at the top of the `.ino` if your build differs —
these are just free GPIOs on the Zero that avoid USB and strapping pins.

**Push-to-talk, not always-on wake word or VAD.** Deliberate choice: more
reliable in the field, and avoids false triggers from the RF/IR modules'
own noise sitting a few centimeters away on the same chassis.

## Protocol (matches `host_daemon/audio_bridge.py`)

Framed binary, no handshake:

```
byte 0-1  magic 0xA5 0x5A
byte 2    frame type
byte 3-4  payload length (uint16, little-endian)
byte 5..N payload
byte N+1  checksum (XOR of payload bytes)
```

Frame types:

| Type | Direction | Meaning |
|---|---|---|
| `0x01` AUDIO_START | S3 -> Linux | push-to-talk pressed |
| `0x02` AUDIO_CHUNK | S3 -> Linux | raw 16-bit PCM, mono, 16 kHz |
| `0x03` AUDIO_END | S3 -> Linux | push-to-talk released |
| `0x10` TONE_ACK | Linux -> S3 | command understood and executed |
| `0x11` TONE_ERROR | Linux -> S3 | STT/intent parse failed or empty |
| `0x12` TONE_LISTENING | Linux -> S3 | capture registered, STT running |

## Flashing

```
arduino-cli compile --profile default sketch_esp32s3_audio
arduino-cli upload --profile default -p /dev/ttyUSB0 sketch_esp32s3_audio
```

## Status note

This firmware is the audio capture/playback + UART relay glue described
in the project documentation (Section 3.3). It is intentionally scoped to
capture/playback only — speech-to-text, intent parsing, and HID execution
all happen on the Linux side in `host_daemon/audio_bridge.py` (STT/relay)
and `python/main.py` (intent parsing + HID execution), which this sketch
talks to indirectly via the host daemon.
