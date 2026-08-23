"""
hid_executor.py — HermesQ HID Executor
=======================================

Executes action plans (dicts produced by ai_agent.py) via the
simulated keyboard defined in hid_sim.py.

BLE HID support has been removed entirely — there is no real
Bluetooth peripheral here. Key/text operations are logged to the
console instead of being sent to a paired device.
"""

import json
import os
import time
import threading

from hid_sim import (
    SimKeyboard,
    NAMED_KEYS,
    MODIFIER_BITS,
    _char_to_hid,
    get_keyboard,
)

# ─── Shared state ─────────────────────────────────────────────────────────────

_hid_connected = False
_hid_mode = "idle"
_last_result = ""
_lock = threading.Lock()


def _set_status(connected: bool, mode: str = "idle"):
    global _hid_connected, _hid_mode
    _hid_connected = connected
    _hid_mode = mode


# ─── HIDExecutor ──────────────────────────────────────────────────────────────

class HIDExecutor:
    def __init__(self, keyboard: SimKeyboard | None = None):
        """
        Pass an already-created SimKeyboard, or leave None to fetch
        the module singleton from hid_sim.get_keyboard().
        """
        self._kbd: SimKeyboard = keyboard or get_keyboard()
        print(f"[HID] mode: {self._kbd.status()}")

    # ── Status ────────────────────────────────────────────────────────────────

    def status(self) -> str:
        conn = "active" if _hid_connected else "idle"
        mode = _hid_mode or "idle"
        hid_status = self._kbd.status()
        return f"hid:{conn}|mode:{mode}|kbd:{hid_status}"

    def start_keyboard(self) -> str:
        global _last_result
        _set_status(True, "keyboard")
        _last_result = f"HID keyboard active ({self._kbd.device_name}, simulated)"
        print(f"[HID] {_last_result}")
        return f"ok:{_last_result}"

    def start_mouse(self) -> str:
        global _last_result
        _set_status(True, "mouse")
        _last_result = "HID mouse mode active (simulated)"
        print(f"[HID] {_last_result}")
        return f"ok:{_last_result}"

    # ── Plan execution ────────────────────────────────────────────────────────

    def execute_plan(self, plan: dict) -> str:
        global _last_result
        actions = plan.get("actions") or []
        if not actions:
            return "err:no actions in plan"

        for step in actions:
            ok, msg = self._execute_step(step)
            if not ok:
                _last_result = msg
                return f"err:{msg}"

        summary = plan.get("summary", f"Executed {len(actions)} HID step(s)")
        _last_result = summary
        print(f"[HID] Plan complete: {summary}")
        return f"ok:{summary}"

    def _execute_step(self, step: dict):
        stype = step.get("type", "")
        try:
            if stype == "delay_ms":
                time.sleep(step.get("ms", 100) / 1000.0)
                return True, "delay"
            elif stype == "open_url":
                return self._open_url(step.get("url", ""))
            elif stype == "type_text":
                return self._type_text(step.get("text", ""))
            elif stype == "key":
                return self._press_named_key(step.get("key", ""))
            elif stype == "key_combo":
                return self._key_combo(step.get("keys") or [])
            else:
                return False, f"unknown action type: {stype}"
        except Exception as exc:
            return False, str(exc)

    # ── HID primitives ────────────────────────────────────────────────────────

    def _type_text(self, text: str):
        if not text:
            return True, "empty text"
        with _lock:
            self._kbd.type_text(text, delay_ms=50)
        print(f"[HID] TYPE: {text!r}")
        return True, f"typed {len(text)} chars"

    def _press_named_key(self, key: str):
        key_upper = key.upper().strip()
        if key_upper not in NAMED_KEYS:
            # Try single character
            if len(key) == 1:
                return self._type_text(key)
            return False, f"unknown key: {key!r}"
        keycode = NAMED_KEYS[key_upper]
        with _lock:
            self._kbd.press_key(keycode)
        print(f"[HID] KEY: {key_upper} (0x{keycode:02X})")
        return True, f"key {key_upper}"

    def _key_combo(self, keys: list):
        if not keys:
            return False, "empty key combo"

        modifier = 0
        keycode = 0

        for key in keys:
            ku = key.upper().strip()
            if ku in MODIFIER_BITS:
                modifier |= MODIFIER_BITS[ku]
            elif ku in NAMED_KEYS:
                keycode = NAMED_KEYS[ku]
            elif len(ku) == 1:
                kc, mod = _char_to_hid(key)
                keycode = kc or keycode
                modifier |= mod
            else:
                return False, f"unknown key in combo: {key!r}"

        with _lock:
            self._kbd.key_combo(modifier, keycode)

        label = "+".join(keys)
        print(f"[HID] COMBO: {label} (mod=0x{modifier:02X} key=0x{keycode:02X})")
        return True, label

    def _open_url(self, url: str):
        """
        Simulates opening a URL via HID actions: focus address bar,
        type the URL, then Enter. This no longer reaches a real device —
        it's logged only, since BLE HID peripheral support was removed.
        """
        if not url:
            return False, "empty url"

        # Win+L on Windows / Cmd+L on macOS (Super = 0x08)
        self._key_combo(["SUPER", "L"])
        time.sleep(0.6)
        self._type_text(url)
        time.sleep(0.2)
        self._press_named_key("ENTER")
        print(f"[HID] open_url: {url}")
        return True, f"opened {url}"


def execute_action_json(action_json: str, executor: HIDExecutor) -> str:
    try:
        plan = json.loads(action_json)
    except json.JSONDecodeError as exc:
        return f"err:invalid json: {exc}"
    return executor.execute_plan(plan)
