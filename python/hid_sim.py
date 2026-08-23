"""
hid_sim.py — HermesQ Simulated HID Keyboard
============================================

BLE HID (BlueZ D-Bus peripheral) support has been removed entirely.
It depended on dbus-python / PyGObject, which in turn pull in pycairo —
a package that needs a C compiler that isn't available in the App Lab
build environment, so the deploy build was failing before the app
ever ran.

This module is a drop-in, dependency-free replacement: it exposes the
same surface HIDExecutor / main.py need (status(), device_name,
sim_mode, type_text(), press_key(), key_combo(), get_keyboard()) but
never touches Bluetooth. Every "keystroke" is simply logged to the
console instead of being sent to a paired device.

If real keystroke injection is needed in the future, swap this module
out for something like a USB HID gadget (/dev/hidg0) implementation —
no other file needs to change as long as the same interface is kept.
"""

import threading

DEVICE_NAME = "HermesQ (simulated HID)"

# USB HID usage IDs — kept only so key/key_combo parsing in hid_executor.py
# has concrete numeric codes to log; no hardware ever sees these values.
NAMED_KEYS = {
    "ENTER": 0x28, "ESC": 0x29, "BACKSPACE": 0x2A, "TAB": 0x2B,
    "SPACE": 0x2C, "CAPSLOCK": 0x39,
    "UP": 0x52, "DOWN": 0x51, "LEFT": 0x50, "RIGHT": 0x4F,
    "HOME": 0x4A, "END": 0x4D, "PAGEUP": 0x4B, "PAGEDOWN": 0x4E,
    "DELETE": 0x4C, "INSERT": 0x49,
    "F1": 0x3A, "F2": 0x3B, "F3": 0x3C, "F4": 0x3D, "F5": 0x3E,
    "F6": 0x3F, "F7": 0x40, "F8": 0x41, "F9": 0x42, "F10": 0x43,
    "F11": 0x44, "F12": 0x45,
}

MODIFIER_BITS = {
    "CTRL": 0x01, "SHIFT": 0x02, "ALT": 0x04, "SUPER": 0x08,
}


def _char_to_hid(ch: str):
    """Minimal ASCII letter/digit → (keycode, modifier) helper."""
    if not ch:
        return 0, 0
    c = ch[0]
    if c.isalpha():
        keycode = 0x04 + (ord(c.lower()) - ord("a"))
        modifier = MODIFIER_BITS["SHIFT"] if c.isupper() else 0
        return keycode, modifier
    if c.isdigit():
        n = int(c)
        keycode = 0x27 if n == 0 else 0x1E + (n - 1)
        return keycode, 0
    return 0, 0


class SimKeyboard:
    """Stand-in for the removed BLE HID peripheral. Always runs in sim mode."""

    def __init__(self, device_name: str = DEVICE_NAME):
        self.device_name = device_name
        self.sim_mode = True
        self._ready = threading.Event()
        self._ready.set()

    def status(self) -> str:
        return f"sim:{self.device_name}"

    def type_text(self, text: str, delay_ms: int = 0):
        print(f"[SIM HID] type_text: {text!r}")

    def press_key(self, keycode: int):
        print(f"[SIM HID] press_key: 0x{keycode:02X}")

    def key_combo(self, modifier: int, keycode: int):
        print(f"[SIM HID] key_combo: mod=0x{modifier:02X} key=0x{keycode:02X}")


_singleton: "SimKeyboard | None" = None


def get_keyboard() -> SimKeyboard:
    """Returns the process-wide simulated keyboard singleton."""
    global _singleton
    if _singleton is None:
        _singleton = SimKeyboard()
    return _singleton
