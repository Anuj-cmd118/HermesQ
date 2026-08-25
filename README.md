# HermesQ

**A modular multitool platform for NFC, IR, Sub-GHz, BLE HID, and on-device AI voice command execution — built on the Arduino UNO Q.**

HermesQ consolidates five traditionally separate hardware tools — an NFC reader/writer/emulator, a universal IR remote and learner, a Sub-GHz (CC1101) transceiver, a real BLE HID keyboard/mouse injector, and a natural-language AI command agent — into one pocket-sized device with a single consistent menu-driven UI. Say or type something like *"launch my dev environment"* and HermesQ turns it into a real, multi-step BLE HID automation on a paired computer or phone.

- **Team:** HermesQ (independent maker, solo team) — Warangal, Telangana
- **Track:** Smart Homes & Consumer AI — Arduino Physical AI Challenge India 2026
- **Team ID:** APC-2026-TS-33269
- **Demo video:** [Watch here](https://drive.google.com/file/d/1_zr69EiC1LX8uxBzatmlo1LW8VjJ4KFl/view?usp=share_link)

---

## ✨ Features

| Module | What it does |
|---|---|
| 📡 **NFC** | Read/write Mifare tags, save them to a library, and emulate a saved tag as a virtual card (PN532 emulate mode). |
| 🔴 **Infrared** | Learn a remote button via IR capture, store it, and re-transmit on dual IR-LED channels. |
| 📻 **Sub-GHz** | CC1101-based spectrum monitor, frequency scan, and raw transmit. |
| ⌨️ **BLE HID** | Fully functional real BLE keyboard/mouse — injects key combos, typed text, and mouse actions into a paired device. |
| 🤖 **AI Agent (typed)** | Free-text command → LLM-powered intent interpretation → structured, multi-step action plan. |
| 🎙️ **AI Agent (voice)** | Push-to-talk on a dedicated ESP32-S3 audio co-processor → offline speech-to-text → the same LLM intent pipeline → BLE HID execution → audio/LED feedback. |
| 📝 **Notes** | Browse, create, and search short text notes, stored server-side. |
| 🧮 **Engineering Toolkit** | 8 offline calculators — Ohm's law, LED resistor, voltage divider, battery runtime, UART baud, PWM frequency, RF wavelength, quarter-wave. |
| 🎮 **Apps** | Calculator, Pomodoro timer, Snake, Space Invaders — fully offline. |
| 🖥️ **Web Dashboard** | Browser-based view into stored tags, remotes, notes, and live voice-command status. |
| 🎬 **Kiosk / Autoplay Demo** | Self-driving booth mode that tours every screen unattended for unmanned presentation. |

---

## 🧩 System Architecture

HermesQ spans four cooperating execution contexts:

```
┌─────────────────────────┐        RouterBridge RPC        ┌──────────────────────────────┐
│   STM32U585 MCU core     │ ◄─────────────────────────────► │   App Lab (Linux/Docker)     │
│   (Zephyr / Arduino)     │                                  │   on the UNO Q               │
│                           │                                  │                               │
│ • OLED + rotary encoder   │                                  │ • SQLite storage (NFC/IR/notes)│
│ • PN532 NFC driver        │                                  │ • LLM-powered AI Agent         │
│ • IR RX/TX                │                                  │ • Real BLE HID execution       │
│ • CC1101 Sub-GHz driver   │                                  │ • Web dashboard                │
│ • Menu state machine      │                                  │ • REST endpoint /api/agent/voice│
└─────────────────────────┘                                  └───────────────┬───────────────┘
                                                                                │ HTTP (localhost:7000)
                                                                                │
┌─────────────────────────┐        Framed UART protocol       ┌───────────────┴───────────────┐
│   ESP32-S3 Zero           │ ◄─────────────────────────────► │   host_daemon/ (UNO Q Debian   │
│   audio co-processor      │                                  │   host, outside App Lab)       │
│                           │                                  │                               │
│ • I2S mic capture         │                                  │ • Owns the S3's UART link      │
│ • I2S speaker/tone output │                                  │ • Offline speech-to-text (Vosk)│
│ • Push-to-talk trigger    │                                  │ • POSTs transcript to App Lab  │
│ • Status LED              │                                  │ • Sends tone-cue feedback      │
└─────────────────────────┘                                  └───────────────────────────────┘
```

**Why this split?** The UNO Q's dual-core design keeps deterministic, time-critical radio/NFC/IR/encoder I/O on the STM32 MCU, while storage, BLE HID, AI parsing, and the dashboard run on-device on the companion Linux side. The UNO Q doesn't expose I2S pins, so a second microcontroller (ESP32-S3 Zero) handles audio capture/playback. And because App Lab's own Docker container can't access the host's `/dev/tty*` device nodes, the serial link to the S3 and the speech-to-text step run as an independent daemon directly on the UNO Q's Debian host, talking to the App Lab app over its existing HTTP dashboard port.

### Voice command flow

1. Hold the push-to-talk button on the ESP32-S3 Zero → streams framed PCM audio over UART.
2. `host_daemon/audio_bridge.py` reassembles the audio and runs offline speech-to-text (Vosk) on release.
3. The daemon POSTs the transcript to App Lab's `POST /api/agent/voice` endpoint over HTTP.
4. `ai_agent.py` sends the text to an LLM API and combines the result with JSON-defined workflows (`workflows.json`) to build a structured action plan.
5. `hid_executor.py` replays the plan through a real BLE HID keyboard/mouse.
6. The daemon plays a confirmation/error tone and LED cue back on the S3.
7. The OLED's **Settings → Voice / Audio** screen polls `audio_status` and shows the outcome.

---

## 🛠️ Hardware (BOM)

| Component | Qty |
|---|---|
| Arduino UNO Q (STM32U585 + Linux SoC) | 1 |
| 128×64 OLED, SH1106 driver, I2C | 1 |
| Rotary encoder with push switch (Gray-code decoded) | 1 |
| Tactile push button — BACK | 1 |
| Tactile push button — LEARN | 1 |
| PN532 NFC module, SPI | 1 |
| VS1838B IR receiver | 1 |
| IR LED (dual-channel TX) | 2 |
| CC1101 Sub-GHz transceiver module | 1 |
| Waveshare ESP32-S3 Zero (audio co-processor) | 1 |
| I2S MEMS microphone (e.g. INMP441) | 1 |
| I2S speaker / class-D amp (e.g. MAX98357A) | 1 |
| Li-Po battery + charge/protection circuit | 1 |

Full pin-assignment tables for both the UNO Q and the S3 Zero are in the technical documentation.

---

## 📁 Repository Structure

```
hermesq/
├── sketch/                      # STM32 firmware (Zephyr/Arduino core)
│   ├── sketch.ino               # Entry point, menu state machine
│   ├── hizmos_input.h           # Debounced encoder/buttons, Gray-code decode
│   ├── hizmos_display.h / _ui.h / _mainmenu.h   # Shared OLED/UI primitives
│   ├── hizmos_nfc.h / _ir.h / _rf.h             # NFC, IR, Sub-GHz engines
│   ├── hizmos_settings.h        # Settings incl. Voice / Audio status screen
│   └── hizmos_bridge.h          # RouterBridge RPC wrapper
│
├── sketch_esp32s3_audio/        # ESP32-S3 audio co-processor firmware
│   └── sketch_esp32s3_audio.ino # I2S mic/speaker, push-to-talk, UART framing
│
├── python/                      # App Lab (Linux runtime) app
│   ├── main.py                  # Bridge handlers, REST routes
│   ├── ai_agent.py              # LLM-backed interpret_command()
│   ├── workflows.json           # User-editable automation chains
│   ├── hid_executor.py          # Real BLE HID action-plan execution
│   ├── nfc_main.py / ir_main.py / rf_main.py / notes_main.py
│   └── web_assets.py            # Web dashboard
│
└── host_daemon/                 # Runs on the UNO Q's Debian host, outside App Lab
    ├── audio_bridge.py          # UART reassembly, offline STT, HTTP relay
    ├── requirements.txt
    └── hermesq-audio.service    # systemd unit for boot-time startup
```

---

## 🚀 Getting Started

### 1. Flash the STM32 firmware
Open `sketch/sketch.ino` in the Arduino IDE (UNO Q / Zephyr core installed) and upload to the UNO Q's MCU.

### 2. Flash the ESP32-S3 audio co-processor
Open `sketch_esp32s3_audio/sketch_esp32s3_audio.ino`, select the ESP32-S3 board profile, and upload.

### 3. Deploy the App Lab app
Push the `python/` app to the UNO Q through App Lab. It will start automatically and expose the web dashboard on port `7000`.

### 4. Configure the AI Agent
The AI Agent calls an external LLM API. Set your API key as an environment variable (or in App Lab's app configuration) before starting:

```bash
export HERMESQ_LLM_API_KEY="your-api-key-here"
```

### 5. Set up the voice daemon (optional, for spoken commands)
SSH into the UNO Q's Debian host and install/run the daemon directly (not through App Lab):

```bash
cd host_daemon
pip install -r requirements.txt --break-system-packages
export HERMESQ_VOSK_MODEL=/path/to/vosk-model     # optional, for offline STT
python audio_bridge.py
# or install as a boot-time service:
sudo cp hermesq-audio.service /etc/systemd/system/
sudo systemctl enable --now hermesq-audio
```

---

## 🧗 Challenges We Faced

- The UNO Q doesn't expose I2S pins, so a mic/speaker couldn't be wired directly to it — solved with a dedicated ESP32-S3 Zero audio co-processor, relayed over a separate UART link.
- App Lab's Python app runs in a Docker container with no access to the host's `/dev/tty*` nodes, so it couldn't open the S3's serial device — solved by moving the serial link and speech-to-text into a standalone host-side daemon that talks to the App Lab app over HTTP.
- App Lab's build environment initially had no C compiler for `dbus-python`/`pycairo`, blocking a real Bluetooth/HID stack — worked around by adjusting the BLE HID library setup, resulting in a fully functional real BLE HID keyboard/mouse.
- The PN532 (NFC) and CC1101 (Sub-GHz) share one SPI bus — resolved by remapping the CC1101's CSN/GDO0 pins off their defaults to avoid conflicts.
- Several libraries were originally written for other Arduino boards and needed tweaking to run correctly on the UNO Q's STM32U585/Zephyr core.
- The UNO Q's Wi-Fi chip showed fluctuating, unstable behaviour on battery power, requiring extra care around power delivery for wireless connectivity.

---

## 🗺️ Roadmap

- [ ] Move the host-side voice daemon back inside the App Lab app once device passthrough for USB-serial nodes is supported.
- [ ] Explore an offline/on-device LLM option to reduce dependency on network connectivity and an external API key.

---

## 📄 License

Add your license of choice here (e.g. MIT).

## 👤 Author

**Katikaneni Anuj Kishan** — Maker, Warangal, Telangana
