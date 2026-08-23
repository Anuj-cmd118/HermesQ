"""
rf_main.py — CC1101 Sub-GHz backend for HermesQ (Arduino App Lab / UNO Q)

Ported from cc1101_rf_monitor/python/main.py and integrated into the
HermesQ python/ folder.  Imported by main.py at startup.

The MCU pushes events to Python via Bridge.notify(); Python logs them
and exposes them via Bridge RPC for any future consumer (e.g. UART
debug, file logging).  There is no web server in this build — all user
interaction is through the OLED menu.

Bridge.notify handlers registered (MCU → Python):
  rf_packet       ({data, rssi, lqi, crc, len, freq})
  rf_tx_sent      ({data, freq})
  rf_mode_change  ({mode})
  rf_freq_update  ({freq})

Bridge.provide RPC registered (available for MCU calls or future use):
  rf_status       ()              → "ok:RX|433.92"
  rf_get_log      ()              → JSON array of last 100 packets
  rf_clear_log    ()              → "ok"
"""

import json
import datetime

from arduino.app_utils import Bridge

# ─── Global state ─────────────────────────────────────────────────────────────
MAX_PACKETS = 100
_rf_packets:   list = []   # RX packet log (newest first)
_rf_tx_log:    list = []   # TX log
_rf_cur_mode:  str  = "RX"
_rf_cur_freq:  float = 433.92


# ─── Bridge.notify handlers (MCU → Python) ────────────────────────────────────

def _on_rf_packet(data: str):
    global _rf_packets
    try:
        pkt = json.loads(data)
        pkt["time"] = datetime.datetime.now().strftime("%H:%M:%S")
        _rf_packets.insert(0, pkt)
        if len(_rf_packets) > MAX_PACKETS:
            _rf_packets.pop()
        crc = "OK" if pkt.get("crc") else "FAIL"
        print(f"[rf_main] RX {pkt['time']} "
              f"data={pkt.get('data')!r} rssi={pkt.get('rssi')}dBm "
              f"crc={crc} freq={pkt.get('freq')}MHz")
    except Exception as e:
        print(f"[rf_main] rf_packet parse error: {e}")


def _on_rf_tx_sent(data: str):
    global _rf_tx_log
    try:
        pkt = json.loads(data)
        pkt["time"] = datetime.datetime.now().strftime("%H:%M:%S")
        _rf_tx_log.insert(0, pkt)
        if len(_rf_tx_log) > 50:
            _rf_tx_log.pop()
        print(f"[rf_main] TX {pkt['time']} "
              f"data={pkt.get('data')!r} freq={pkt.get('freq')}MHz")
    except Exception as e:
        print(f"[rf_main] rf_tx_sent parse error: {e}")


def _on_rf_mode_change(data: str):
    global _rf_cur_mode
    try:
        obj = json.loads(data)
        _rf_cur_mode = obj.get("mode", "RX")
        print(f"[rf_main] Mode → {_rf_cur_mode}")
    except Exception as e:
        print(f"[rf_main] rf_mode_change parse error: {e}")


def _on_rf_freq_update(data: str):
    global _rf_cur_freq
    try:
        obj = json.loads(data)
        _rf_cur_freq = float(obj.get("freq", 433.92))
        print(f"[rf_main] Freq → {_rf_cur_freq} MHz")
    except Exception as e:
        print(f"[rf_main] rf_freq_update parse error: {e}")


Bridge.provide("rf_packet",      _on_rf_packet)
Bridge.provide("rf_tx_sent",     _on_rf_tx_sent)
Bridge.provide("rf_mode_change", _on_rf_mode_change)
Bridge.provide("rf_freq_update", _on_rf_freq_update)


# ─── Bridge.provide RPC (available for MCU calls) ─────────────────────────────

def rpc_rf_status(_arg: str = "") -> str:
    """Returns current mode and frequency as a compact string."""
    return f"ok:{_rf_cur_mode}|{_rf_cur_freq:.2f}"


def rpc_rf_get_log(_arg: str = "") -> str:
    """Returns last 100 received packets as JSON array string."""
    return json.dumps(_rf_packets[:MAX_PACKETS])


def rpc_rf_clear_log(_arg: str = "") -> str:
    _rf_packets.clear()
    _rf_tx_log.clear()
    print("[rf_main] Packet log cleared.")
    return "ok"


Bridge.provide("rf_status",    rpc_rf_status)
Bridge.provide("rf_get_log",   rpc_rf_get_log)
Bridge.provide("rf_clear_log", rpc_rf_clear_log)

print("[rf_main] RF RPC handlers registered.")


