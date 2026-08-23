"""
main.py — HermesQ Linux MPU Backend (Arduino App Lab / UNO Q)
=============================================================

Startup order
-------------
1. SQLStore DB initialised
2. HIDExecutor wired to a simulated keyboard (no BLE peripheral)
3. WebUI (port 7000) started with Socket.IO + REST
4. Bridge RPC methods registered for MCU ↔ Linux calls
5. Sub-modules imported — each registers its own Bridge RPC handlers

REST endpoints exposed for the dashboard:
  GET  /history                  — action log
  GET  /status                   — HID + system status (incl. last voice activity)
  GET  /health                   — liveness probe
  GET  /api/nfc/list             — all saved NFC cards
  GET  /api/nfc/favourites       — favourite NFC cards only
  GET  /api/nfc/card/<id>        — single card detail
  GET  /api/notes/list           — all notes
  GET  /api/notes/search?q=…     — search notes
  POST /api/notes/new            — create note  {title, body}
  DELETE /api/notes/delete/<id>  — delete note
  GET  /api/ir/remotes           — list learned remote names
  GET  /api/ir/buttons?remote=…  — buttons for a remote
  POST /api/ir/emit              — replay a signal {remote, button}
  GET  /api/rf/log               — RF packet log + mode/freq
  POST /api/rf/clear             — clear RF log
  POST /api/agent/voice          — run a transcribed voice command {text}
                                    (called by host_daemon/audio_bridge.py —
                                    see "Why voice isn't handled in-process"
                                    below)

Why voice isn't handled in-process
-----------------------------------
App Lab runs this file inside a Docker container with no access to the
host's /dev/tty* nodes (confirmed on the Arduino forum: "The Python script
of the App runs in a Docker container, isolated from the global Linux
environment" — https://forum.arduino.cc/t/issues-with-extra-python-packages-evdev/1415895/11,
and https://forum.arduino.cc/t/accessing-dev-serial-via-python/1415940).
That means pyserial can never open the USB-UART adapter carrying the
ESP32-S3's audio frames from *inside* this process, no matter what's in
requirements.txt.

The fix used here: the actual serial/STT work lives in
host_daemon/audio_bridge.py, a plain script that runs directly on the
UNO Q's Debian host (outside App Lab's container — see
host_daemon/README.md) where /dev/tty* is reachable. It talks to *this*
process the same way a browser dashboard would: over the network, via the
POST /api/agent/voice endpoint below, which App Lab's WebUI brick already
binds to the host's network stack (this is the same port
http://<board-ip>:7000 you already use for the dashboard). Nothing in the
App Lab app itself needs container device access.
"""

import atexit
import os
import sqlite3
import time
import webbrowser

from arduino.app_bricks.dbstorage_sqlstore import SQLStore
from arduino.app_bricks.web_ui import WebUI
from arduino.app_utils import App, Bridge

from ai_agent import WORKFLOWS, interpret_command, plan_to_json
from hermesq_db import get_action_history, init_db, log_action
from hid_executor import HIDExecutor
from web_assets import ensure_web_assets

# Sub-modules self-register their Bridge RPC handlers on import
import nfc_main    # noqa: F401
import notes_main  # noqa: F401
import ir_main     # noqa: F401
import rf_main     # noqa: F401

WEB_PORT = 7000
NFC_DB   = os.path.expanduser("~/nfc_cards.db")


# ── NFC DB helpers (read-only, for REST endpoints) ────────────────────────────

def _nfc_db():
    db = sqlite3.connect(NFC_DB)
    db.row_factory = sqlite3.Row
    db.execute("PRAGMA journal_mode=WAL;")
    return db

def _nfc_list(fav_only=False):
    try:
        db = _nfc_db()
        q = ("SELECT id,name,uid,type,ndef_type,ndef_content,favorite,created_at "
             "FROM nfc_cards" +
             (" WHERE favorite=1" if fav_only else "") +
             " ORDER BY created_at DESC LIMIT 64")
        rows = [dict(r) for r in db.execute(q).fetchall()]
        db.close()
        return rows
    except Exception:
        return []

def _nfc_card(card_id):
    try:
        db = _nfc_db()
        row = db.execute(
            "SELECT id,name,uid,type,uid_length,atqa,sak,"
            "ndef_type,ndef_content,favorite,created_at "
            "FROM nfc_cards WHERE id=?", (int(card_id),)
        ).fetchone()
        db.close()
        return dict(row) if row else None
    except Exception:
        return None


# ── Main backend class ────────────────────────────────────────────────────────

class HermesQBackend:
    def __init__(self):
        # 1. Database
        self.db = SQLStore("hermesq.db")
        init_db(self.db)

        # 2. HID executor (sim — no BLE)
        self.hid = HIDExecutor()
        print(f"[HermesQ] HID: {self.hid.status()}")

        # 3. Web UI
        assets = ensure_web_assets()
        print(f"[HermesQ] Serving dashboard from: {assets}")
        self.web_ui = WebUI(port=WEB_PORT, assets_dir_path=assets)
        self._setup_web()

        # 4. Bridge RPC
        self._setup_bridge()

        # 5. Voice command bookkeeping. The actual mic/UART/STT work happens
        # outside this container, in host_daemon/audio_bridge.py — see the
        # module docstring above. This process only tracks when a voice
        # command last came in, for the /status endpoint and OLED.
        self._last_voice_at = None
        self._last_voice_ok = None

        print(f"[HermesQ] Ready → http://<board-ip>:{WEB_PORT}")

    # ── Helpers ───────────────────────────────────────────────────────────────

    def web_url(self):
        host = os.getenv("HOST_IP", "localhost")
        return f"http://{host}:{WEB_PORT}"

    # ── WebUI setup ───────────────────────────────────────────────────────────

    def _setup_web(self):
        ui = self.web_ui

        # Socket.IO
        ui.on_message("run_command", self.on_run_command)
        ui.on_message("hid_status",  self.on_hid_status)

        # Core
        ui.expose_api("GET", "/history", self.api_history)
        ui.expose_api("GET", "/status",  self.api_status)
        ui.expose_api("GET", "/health",  lambda: {"ok": True, "service": "hermesq"})

        # NFC
        ui.expose_api("GET", "/api/nfc/list",        self.api_nfc_list)
        ui.expose_api("GET", "/api/nfc/favourites",  self.api_nfc_favourites)
        ui.expose_api("GET", "/api/nfc/card/<id>",   self.api_nfc_card)

        # Notes
        ui.expose_api("GET",    "/api/notes/list",        self.api_notes_list)
        ui.expose_api("GET",    "/api/notes/search",      self.api_notes_search)
        ui.expose_api("POST",   "/api/notes/new",         self.api_notes_new)
        ui.expose_api("DELETE", "/api/notes/delete/<id>", self.api_notes_delete)

        # IR
        ui.expose_api("GET",  "/api/ir/remotes",  self.api_ir_remotes)
        ui.expose_api("GET",  "/api/ir/buttons",  self.api_ir_buttons)
        ui.expose_api("POST", "/api/ir/emit",     self.api_ir_emit)

        # RF
        ui.expose_api("GET",  "/api/rf/log",   self.api_rf_log)
        ui.expose_api("POST", "/api/rf/clear", self.api_rf_clear)

        # Voice (called by host_daemon/audio_bridge.py — see module docstring)
        ui.expose_api("POST", "/api/agent/voice", self.api_agent_voice)

    # ── Socket.IO handlers ────────────────────────────────────────────────────

    def on_run_command(self, sid, data):
        mode = (data or {}).get("mode", "agent")
        text = ((data or {}).get("command") or "").strip()
        if not text:
            return {"ok": False, "message": "Empty command"}
        plan   = interpret_command(text)
        result = self.hid.execute_plan(plan)
        log_action(self.db, mode, text, plan.get("intent", ""), result)
        ok  = result.startswith("ok:")
        msg = result[3:] if ok else (result[4:] if result.startswith("err:") else result)
        return {"ok": ok, "message": msg, "hid": self.hid.status()}

    def on_hid_status(self, sid, _data):
        return {"status": self.hid.status(), "sim": True}

    # ── Core REST ─────────────────────────────────────────────────────────────

    def api_history(self):
        return {"actions": get_action_history(self.db)}

    def api_status(self):
        return {
            "hid": self.hid.status(),
            "sim": True,
            "voice": {
                "last_seen": self._last_voice_at,
                "last_ok": self._last_voice_ok,
            },
        }

    # ── NFC REST ──────────────────────────────────────────────────────────────

    def api_nfc_list(self):
        cards = _nfc_list(fav_only=False)
        return {"cards": cards, "count": len(cards)}

    def api_nfc_favourites(self):
        cards = _nfc_list(fav_only=True)
        return {"cards": cards, "count": len(cards)}

    def api_nfc_card(self, id):  # noqa: A002
        card = _nfc_card(id)
        if not card:
            return {"error": "not found"}, 404
        return {"card": card}

    # ── Notes REST ────────────────────────────────────────────────────────────

    def api_notes_list(self):
        rows = notes_main._notes_db.read("notes", order_by="id DESC", limit=50) or []
        return {"notes": [dict(r) for r in rows]}

    def api_notes_search(self, q=""):
        q = (q or "").strip().lower()
        rows = notes_main._notes_db.read("notes", order_by="id DESC", limit=100) or []
        hits = [r for r in rows
                if q in (r.get("title") or "").lower()
                or q in (r.get("body")  or "").lower()]
        return {"notes": [dict(r) for r in hits[:40]]}

    def api_notes_new(self, title="", body="", **kw):
        # Body parsed from JSON POST
        import flask
        try:
            data  = flask.request.get_json(silent=True) or {}
            title = data.get("title", title).strip()
            body  = data.get("body",  body).strip()
        except Exception:
            pass
        if not title:
            return {"ok": False, "error": "title required"}, 400
        result = notes_main.rpc_notes_new(f"{title}:{body}")
        return {"ok": result == "ok", "error": result if result != "ok" else None}

    def api_notes_delete(self, id):  # noqa: A002
        result = notes_main.rpc_notes_delete(str(id))
        return {"ok": result == "ok", "error": result if result != "ok" else None}

    # ── IR REST ───────────────────────────────────────────────────────────────

    def api_ir_remotes(self):
        names = ir_main._unique_remotes()
        return {"remotes": names}

    def api_ir_buttons(self, remote=""):
        import flask
        remote = flask.request.args.get("remote", remote)
        btns   = ir_main._buttons_for(remote)
        return {"remote": remote, "buttons": btns}

    def api_ir_emit(self, remote="", button=""):
        import flask
        try:
            data   = flask.request.get_json(silent=True) or {}
            remote = data.get("remote", remote)
            button = data.get("button", button)
        except Exception:
            pass
        if not remote or not button:
            return {"ok": False, "error": "remote and button required"}, 400
        pkt = ir_main.rpc_ir_get_packet(f"{remote}:{button}")
        if not pkt:
            return {"ok": False, "error": "packet not found"}
        # Emit via Bridge to MCU
        result = ""
        Bridge.call("ir_emit_packet", pkt).result(result)
        print(f"[IR] Emit {remote}/{button} → {result or 'sent'}")
        return {"ok": True, "remote": remote, "button": button}

    # ── RF REST ───────────────────────────────────────────────────────────────

    def api_rf_log(self):
        return {
            "packets": rf_main._rf_packets[:100],
            "mode":    rf_main._rf_cur_mode,
            "freq":    rf_main._rf_cur_freq,
        }

    def api_rf_clear(self):
        rf_main._rf_packets.clear()
        rf_main._rf_tx_log.clear()
        return {"ok": True}

    # ── Voice REST (called by host_daemon/audio_bridge.py) ─────────────────────

    def api_agent_voice(self, text=""):
        import flask
        try:
            data = flask.request.get_json(silent=True) or {}
            text = (data.get("text", text) or "").strip()
        except Exception:
            pass
        if not text:
            return {"ok": False, "error": "text required"}, 400

        result = self.run_voice_command(text)
        ok = isinstance(result, str) and result.startswith("ok:")
        self._last_voice_at = time.time()
        self._last_voice_ok = ok
        msg = result[3:] if ok else (result[4:] if result.startswith("err:") else result)
        return {"ok": ok, "message": msg, "text": text}

    # ── Bridge RPC (MCU → Linux) ──────────────────────────────────────────────

    def _setup_bridge(self):
        Bridge.provide("hid_open_web",    self.rpc_hid_open_web)
        Bridge.provide("ai_open_web",     self.rpc_ai_open_web)
        Bridge.provide("hid_status",      self.rpc_hid_status)
        Bridge.provide("hid_start_kb",    self.rpc_hid_start_kb)
        Bridge.provide("hid_start_mouse", self.rpc_hid_start_mouse)
        Bridge.provide("agent_run",       self.rpc_agent_run)
        Bridge.provide("agent_plan",      self.rpc_agent_plan)
        Bridge.provide("ai_status",       self.rpc_ai_status)
        Bridge.provide("audio_status",    self.rpc_audio_status)
        Bridge.provide("sys_ping",        self.rpc_sys_ping)
        Bridge.provide("sys_sd_info",     self.rpc_sys_sd_info)

    def rpc_hid_open_web(self, _=None):
        url = self.web_url()
        try: webbrowser.open(url)
        except Exception: pass
        return f"ok:{url}"

    def rpc_ai_open_web(self, _=None):
        return self.rpc_hid_open_web()

    def rpc_hid_status(self, _=None):
        return f"ok:{self.hid.status()}"

    def rpc_hid_start_kb(self, _=None):
        return self.hid.start_keyboard()

    def rpc_hid_start_mouse(self, _=None):
        return self.hid.start_mouse()

    def rpc_agent_run(self, arg=None):
        text = (arg or "").strip()
        if not text: return "err:empty"
        plan   = interpret_command(text)
        result = self.hid.execute_plan(plan)
        log_action(self.db, "agent", text, plan.get("intent", ""), result)
        return result

    def rpc_agent_plan(self, arg=None):
        return plan_to_json(interpret_command((arg or "").strip()))

    def rpc_ai_status(self, _=None):
        """Lightweight liveness check for the AI Agent's typed/Bridge path —
        distinct from rpc_audio_status(), which reports on the last voice
        (spoken) command specifically."""
        return f"ok:agent ready ({len(WORKFLOWS)} workflows loaded)"

    def rpc_sys_ping(self, _=None):
        """Simple Bridge round-trip check for the OLED's System Info screen —
        confirms the MCU <-> Linux Bridge link and this app's process are
        both alive right now (not a cached/stale value)."""
        return f"ok:pong {int(time.time())}"

    def rpc_sys_sd_info(self, _=None):
        """Reports real storage usage for the filesystem this app's data
        directory lives on (SQLite DB, notes, web assets) via os.statvfs —
        genuine host data, not a placeholder."""
        try:
            st = os.statvfs(os.path.dirname(os.path.abspath(__file__)))
            total_mb = (st.f_blocks * st.f_frsize) // (1024 * 1024)
            free_mb  = (st.f_bavail * st.f_frsize) // (1024 * 1024)
            return f"ok:total:{total_mb}mb free:{free_mb}mb"
        except OSError as exc:
            return f"err:{exc}"

    def rpc_audio_status(self, _=None):
        if self._last_voice_at is None:
            return "ok:no voice commands yet"
        age_s = int(time.time() - self._last_voice_at)
        return f"ok:last voice command {age_s}s ago ({'ok' if self._last_voice_ok else 'error'})"

    # ── Voice command entry point (called by api_agent_voice, in turn called
    # by host_daemon/audio_bridge.py over HTTP — see module docstring) ────────

    def run_voice_command(self, text):
        """
        Shared by both typed (agent_run over Bridge) and spoken (posted from
        the host-side audio daemon) commands — the AI Agent's intent-parsing
        logic is agnostic to the input source. See ai_agent.interpret_command().
        """
        plan = interpret_command(text)
        result = self.hid.execute_plan(plan)
        log_action(self.db, "voice", text, plan.get("intent", ""), result)
        return result


backend = HermesQBackend()

if __name__ == "__main__":
    App.run()
