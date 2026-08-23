#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
host_daemon/audio_bridge.py — HermesQ voice pipeline, run OUTSIDE App Lab
============================================================================

WHY THIS LIVES HERE AND NOT IN python/
----------------------------------------
App Lab's python/main.py runs inside a Docker container that has no access
to the host's /dev/tty* device nodes. This is a confirmed, current platform
limitation, not a bug in HermesQ:

  "The Python script of the App runs in a Docker container, isolated
   from the global Linux environment. So the problem is that the
   environment of that container doesn't provide what is required..."
  — Arduino staff, https://forum.arduino.cc/t/issues-with-extra-python-packages-evdev/1415895/11

  Same failure reproduced directly with pyserial + a USB-serial device:
  https://forum.arduino.cc/t/accessing-dev-serial-via-python/1415940
  ("FileNotFoundError: [Errno 2] No such file or directory: '/dev/ttyACM0'"
  even though the same path works fine over SSH, outside the container.)

  An Arduino team member confirms device passthrough is planned but not
  available as of this writing: "Future releases will allow specifying a
  list of devices from the host to be shared with the container environment."

So this script is NOT part of the App Lab app (app.yaml, python/, sketch/).
It's a plain Python script you run directly on the UNO Q's Debian host
(the same shell you get over SSH), where /dev/ttyUSB*/ttyACM* work
normally. It talks to the actual App Lab app the same way your browser
dashboard does — over HTTP, to the WebUI brick's REST port (7000), which
IS reachable from the host because that's how you already load the
dashboard at http://<board-ip>:7000.

WHAT IT DOES
------------
1. Opens the UART to the ESP32-S3 audio co-processor (see
   sketch_esp32s3_audio/) and reads the same framed protocol described
   there (AUDIO_START / AUDIO_CHUNK / AUDIO_END, TONE_ACK / TONE_ERROR /
   TONE_LISTENING).
2. Buffers PCM per push-to-talk utterance.
3. Runs offline speech-to-text (Vosk) on the complete utterance.
4. POSTs the transcribed text to the running App Lab app's
   POST /api/agent/voice endpoint (see python/main.py).
5. Relays the ok/err result back to the S3 as a tone cue.

RUNNING IT
----------
    ssh arduino@<board-ip>
    cd host_daemon
    pip install -r requirements.txt --break-system-packages
    export HERMESQ_VOSK_MODEL=/home/arduino/vosk-model-small-en-us-0.15
    python3 audio_bridge.py

To run it automatically on boot, see the systemd unit in this folder
(hermesq-audio.service) — install it once with:
    sudo cp hermesq-audio.service /etc/systemd/system/
    sudo systemctl enable --now hermesq-audio

Note: the App Lab app must already be running (so port 7000 is up) before
this daemon can successfully POST to it. It retries quietly if not.
"""

import json
import os
import threading
import time
import urllib.error
import urllib.request

try:
    import serial  # pyserial
except ImportError:
    serial = None

# ─── Protocol constants (must match sketch_esp32s3_audio.ino) ────────────────

MAGIC_0 = 0xA5
MAGIC_1 = 0x5A

FRAME_AUDIO_START    = 0x01
FRAME_AUDIO_CHUNK    = 0x02
FRAME_AUDIO_END      = 0x03
FRAME_TONE_ACK       = 0x10
FRAME_TONE_ERROR     = 0x11
FRAME_TONE_LISTENING = 0x12

SAMPLE_RATE_HZ = 16000
SAMPLE_WIDTH_BYTES = 2  # 16-bit PCM

DEFAULT_PORT = os.environ.get("HERMESQ_AUDIO_UART", "/dev/ttyUSB0")
DEFAULT_BAUD = 115200
DEFAULT_APP_URL = os.environ.get("HERMESQ_APP_URL", "http://localhost:7000")


def _send_frame(ser, frame_type, payload=b""):
    checksum = 0
    for b in payload:
        checksum ^= b
    header = bytes([MAGIC_0, MAGIC_1, frame_type,
                     len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
    ser.write(header + payload + bytes([checksum]))


class _FrameReader:
    """Byte-at-a-time framer for the S3 -> host direction."""

    def __init__(self):
        self._state = 0
        self._type = 0
        self._len = 0
        self._buf = bytearray()

    def feed(self, byte):
        """Feed one byte in; returns (frame_type, payload) or None."""
        if self._state == 0:
            self._state = 1 if byte == MAGIC_0 else 0
        elif self._state == 1:
            self._state = 2 if byte == MAGIC_1 else 0
        elif self._state == 2:
            self._type = byte
            self._state = 3
        elif self._state == 3:
            self._len = byte
            self._state = 4
        elif self._state == 4:
            self._len |= (byte << 8)
            self._buf = bytearray()
            self._state = 6 if self._len == 0 else 5
        elif self._state == 5:
            self._buf.append(byte)
            if len(self._buf) >= self._len:
                self._state = 6
        elif self._state == 6:
            self._state = 0
            return (self._type, bytes(self._buf))
        return None


class AudioBridge:
    """
    Owns the UART link to the ESP32-S3 audio co-processor and drives the
    capture -> STT -> HTTP POST -> tone-feedback pipeline for voice
    commands. Runs standalone, on the host, outside App Lab.
    """

    def __init__(self, port=DEFAULT_PORT, baud=DEFAULT_BAUD, app_url=DEFAULT_APP_URL):
        self._port_name = port
        self._baud = baud
        self._app_url = app_url.rstrip("/")
        self._ser = None
        self._reader = _FrameReader()
        self._pcm_buf = bytearray()
        self._capturing = False
        self._stop = threading.Event()
        self._vosk_model = None
        self._vosk_ready = False
        self._init_stt()

    # ── STT setup ────────────────────────────────────────────────────────

    def _init_stt(self):
        model_path = os.environ.get("HERMESQ_VOSK_MODEL", "")
        if not model_path:
            print("[AudioBridge] HERMESQ_VOSK_MODEL not set — voice capture "
                  "will work, but transcription is disabled. See this "
                  "script's docstring to enable it.")
            return
        try:
            import vosk  # noqa: local import — optional dependency
            self._vosk_model = vosk.Model(model_path)
            self._vosk = vosk
            self._vosk_ready = True
            print(f"[AudioBridge] Vosk STT ready ({model_path})")
        except Exception as e:
            print(f"[AudioBridge] Vosk STT unavailable ({e}); voice capture "
                  f"still works, transcription disabled.")

    def _speech_to_text(self, pcm_bytes):
        if not self._vosk_ready or not pcm_bytes:
            return ""
        try:
            rec = self._vosk.KaldiRecognizer(self._vosk_model, SAMPLE_RATE_HZ)
            rec.AcceptWaveform(pcm_bytes)
            result = json.loads(rec.FinalResult())
            return (result.get("text") or "").strip()
        except Exception as e:
            print(f"[AudioBridge] STT error: {e}")
            return ""

    # ── App Lab REST call ───────────────────────────────────────────────

    def _post_command(self, text):
        """POST to the App Lab app's /api/agent/voice — reachable over the
        network exactly like the dashboard is, no container device access
        needed on either side."""
        url = f"{self._app_url}/api/agent/voice"
        body = json.dumps({"text": text}).encode("utf-8")
        req = urllib.request.Request(
            url, data=body, method="POST",
            headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                data = json.loads(resp.read().decode("utf-8"))
                return bool(data.get("ok"))
        except urllib.error.URLError as e:
            print(f"[AudioBridge] Could not reach {url}: {e}. "
                  f"Is the HermesQ App Lab app running?")
            return False
        except Exception as e:
            print(f"[AudioBridge] Unexpected error posting command: {e}")
            return False

    # ── Lifecycle ────────────────────────────────────────────────────────

    def start(self):
        if serial is None:
            print("[AudioBridge] pyserial not installed — run: "
                  "pip install -r host_daemon/requirements.txt "
                  "--break-system-packages")
            return False
        try:
            self._ser = serial.Serial(self._port_name, self._baud, timeout=0.05)
        except Exception as e:
            print(f"[AudioBridge] Could not open {self._port_name}: {e}. "
                  f"Check the S3 is plugged in and the port name is right "
                  f"(ls /dev/ttyUSB* /dev/ttyACM*).")
            return False
        print(f"[AudioBridge] Listening on {self._port_name} @ {self._baud}, "
              f"posting to {self._app_url}")
        self._run()
        return True

    def stop(self):
        self._stop.set()
        if self._ser:
            self._ser.close()

    # ── Main loop (blocking — call from __main__, or run in a thread) ─────

    def _run(self):
        while not self._stop.is_set():
            try:
                chunk = self._ser.read(256)
            except Exception as e:
                print(f"[AudioBridge] UART read error: {e}")
                time.sleep(0.5)
                continue
            for b in chunk:
                frame = self._reader.feed(b)
                if frame is not None:
                    self._handle_frame(*frame)

    def _handle_frame(self, frame_type, payload):
        if frame_type == FRAME_AUDIO_START:
            self._capturing = True
            self._pcm_buf = bytearray()
            _send_frame(self._ser, FRAME_TONE_LISTENING)

        elif frame_type == FRAME_AUDIO_CHUNK and self._capturing:
            self._pcm_buf.extend(payload)

        elif frame_type == FRAME_AUDIO_END:
            self._capturing = False
            pcm_bytes = bytes(self._pcm_buf)
            self._pcm_buf = bytearray()
            duration_s = len(pcm_bytes) / (SAMPLE_RATE_HZ * SAMPLE_WIDTH_BYTES)
            print(f"[AudioBridge] Captured {duration_s:.2f}s of audio "
                  f"({len(pcm_bytes)} bytes)")
            self._process_utterance(pcm_bytes)

    def _process_utterance(self, pcm_bytes):
        text = self._speech_to_text(pcm_bytes)
        if not text:
            print("[AudioBridge] No transcription available "
                  "(empty audio or STT not configured)")
            _send_frame(self._ser, FRAME_TONE_ERROR)
            return

        print(f"[AudioBridge] Heard: \"{text}\"")
        ok = self._post_command(text)
        _send_frame(self._ser, FRAME_TONE_ACK if ok else FRAME_TONE_ERROR)


if __name__ == "__main__":
    bridge = AudioBridge()
    try:
        bridge.start()
    except KeyboardInterrupt:
        pass
    finally:
        bridge.stop()
