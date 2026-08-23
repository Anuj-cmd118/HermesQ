# host_daemon/ — voice pipeline, run outside App Lab

## Why this folder is separate from `python/`

App Lab's `python/main.py` runs inside a **Docker container** on the UNO Q,
isolated from the host's `/dev/*` device nodes. This is a confirmed,
current platform limitation — not something wrong in HermesQ's code:

- Arduino staff, on the forum: *"The Python script of the App runs in a
  Docker container, isolated from the global Linux environment... the
  environment of that container doesn't provide what is required."*
  ([source](https://forum.arduino.cc/t/issues-with-extra-python-packages-evdev/1415895/11))
- Reproduced directly with `pyserial` opening a USB-serial device from
  inside an App Lab app: `FileNotFoundError: [Errno 2] No such file or
  directory: '/dev/ttyACM0'` — even though the same path works fine over
  SSH, outside the container.
  ([source](https://forum.arduino.cc/t/accessing-dev-serial-via-python/1415940))
- An Arduino team member, same thread: *"Future releases will allow
  specifying a list of devices from the host to be shared with the
  container environment"* — i.e. not available today.

So `audio_bridge.py` **cannot** live in `python/` and be deployed through
App Lab's Run button — it would fail to open the S3's serial port the
moment it tried, regardless of what's in `requirements.txt`.

## The fix

`audio_bridge.py` here is a plain script you run directly on the UNO Q's
Debian host — the same shell you get over SSH — where `/dev/ttyUSB*` /
`/dev/ttyACM*` work normally. It talks to the App Lab app (still running
normally, unmodified, through App Lab) over HTTP, the same way your
browser reaches the dashboard at `http://<board-ip>:7000`. Specifically it
POSTs transcribed text to `POST /api/agent/voice`, a small REST endpoint
added to `python/main.py` for exactly this purpose.

Everything else about HermesQ — NFC, IR, Sub-GHz, BLE HID sim, typed AI
Agent commands, the web dashboard — is untouched and still runs entirely
inside App Lab as normal. Only the voice capture/STT step needed to move
outside the container.

## Setup

```bash
ssh arduino@<board-ip>

# copy this folder to the board if you haven't already, e.g.:
#   scp -r host_daemon arduino@<board-ip>:~/hermesq/host_daemon

cd ~/hermesq/host_daemon
pip install -r requirements.txt --break-system-packages

# optional: download a Vosk model for real transcription
# (without one, capture/framing/REST-post still work, transcription
# just no-ops with a clear log line)
# e.g. https://alphacephei.com/vosk/models -> vosk-model-small-en-us-0.15

export HERMESQ_AUDIO_UART=/dev/ttyUSB0   # check with: ls /dev/ttyUSB* /dev/ttyACM*
export HERMESQ_VOSK_MODEL=~/vosk-model-small-en-us-0.15
python3 audio_bridge.py
```

Make sure the App Lab app is already running (Run button, or set to
launch on boot) — this daemon needs port 7000 to be up before a voice
command can be delivered.

## Run on boot

```bash
sudo cp hermesq-audio.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now hermesq-audio
sudo systemctl status hermesq-audio     # check it's running
journalctl -u hermesq-audio -f          # tail logs
```

Edit the paths/env vars in `hermesq-audio.service` first if your board's
username or install location differs from the defaults (`arduino`,
`/home/arduino/hermesq/host_daemon`).

## Protocol

Matches `sketch_esp32s3_audio/sketch_esp32s3_audio.ino` exactly — see that
sketch's README for the full frame format. In short: framed binary over
UART, `AUDIO_START/CHUNK/END` from the S3, `TONE_ACK/ERROR/LISTENING`
back to it.
