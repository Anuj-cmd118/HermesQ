"""
ir_main.py — IR Remote Hub backend for HermesQ (Arduino App Lab / UNO Q)

Ported from butcher/python/main.py and integrated into the HermesQ
python/ folder.  Imported by main.py at startup, which registers all
RPC handlers into the shared Bridge.

RPC methods registered:
  ir_list_remotes  ()                          → "name1|name2|..."  or ""
  ir_list_buttons  (remote)                    → "btn1|btn2|..."    or ""
  ir_get_packet    (remote:button)             → "dur0|dur1|..."    or ""
  ir_save_packet   (remote:button:dur0|dur1…)  → "ok"  or "err:…"

All user interaction is through the OLED + physical buttons on the MCU.
This layer only provides persistent SQLite storage and packet routing.
"""

from arduino.app_utils import Bridge
from arduino.app_bricks.dbstorage_sqlstore import SQLStore

# ─── Database ────────────────────────────────────────────────────────────────
_ir_db = SQLStore("ir_remotes.db")
_ir_db.create_table("ir_remotes", {
    "remote_name": "TEXT",
    "button_name": "TEXT",
    "packet_data": "TEXT",   # pipe-delimited uint16 timings: "9000|4500|560|…"
})
print("[ir_main] Database ready.")


# ─── Internal helpers ────────────────────────────────────────────────────────

def _all_rows() -> list:
    return _ir_db.read("ir_remotes") or []


def _unique_remotes() -> list:
    """Ordered unique remote names, preserving insertion order."""
    seen: list = []
    for r in _all_rows():
        if r["remote_name"] not in seen:
            seen.append(r["remote_name"])
    return seen


def _buttons_for(remote: str) -> list:
    return [r["button_name"] for r in _all_rows() if r["remote_name"] == remote]


def _upsert_packet(remote: str, button: str, packet_pipe: str) -> None:
    """Save or overwrite a learned IR packet (pipe-delimited timing string)."""
    for row in _all_rows():
        if row["remote_name"] == remote and row["button_name"] == button:
            _ir_db.update(
                "ir_remotes",
                {"packet_data": packet_pipe},
                f"remote_name = '{remote}' AND button_name = '{button}'",
            )
            return
    _ir_db.store("ir_remotes", {
        "remote_name": remote,
        "button_name": button,
        "packet_data": packet_pipe,
    })


# ─── RPC handlers ────────────────────────────────────────────────────────────

def rpc_ir_list_remotes(_arg: str) -> str:
    """
    Arduino: Bridge.call("ir_list_remotes")
    Returns: "Remote1|Remote2"  or  "" if none.
    """
    names  = _unique_remotes()
    result = "|".join(names)
    print(f"[ir_main] ir_list_remotes → {result or '(empty)'}")
    return result


def rpc_ir_list_buttons(arg: str) -> str:
    """
    Arduino: Bridge.call("ir_list_buttons", remoteName)
    Returns: "btn1|btn2"  or  "" if none.
    """
    remote  = arg.strip()
    buttons = _buttons_for(remote)
    result  = "|".join(buttons)
    print(f"[ir_main] ir_list_buttons({remote}) → {result or '(empty)'}")
    return result


def rpc_ir_get_packet(arg: str) -> str:
    """
    Arduino: Bridge.call("ir_get_packet", "Remote:Button")
    Returns: "dur0|dur1|dur2|…"  or  "" if not found.

    Packet is stored as a pipe-delimited string and returned verbatim —
    no JSON on the hot path.
    """
    parts = arg.split(":", 1)
    if len(parts) != 2:
        print(f"[ir_main] ir_get_packet bad arg: {arg!r}")
        return ""
    remote, button = parts[0].strip(), parts[1].strip()
    for row in _all_rows():
        if row["remote_name"] == remote and row["button_name"] == button:
            pkt = row["packet_data"]
            print(f"[ir_main] ir_get_packet({remote}/{button}) → "
                  f"{len(pkt.split('|'))} timings")
            return pkt
    print(f"[ir_main] ir_get_packet — not found: {remote}/{button}")
    return ""


def rpc_ir_save_packet(arg: str) -> str:
    """
    Arduino: Bridge.call("ir_save_packet", "Remote:Button:dur0|dur1|…")
    Returns: "ok"  or  "err:<reason>"

    Split on the first two colons only so the packet data (which uses '|')
    is preserved intact.  Minimum 20 timings required — shorter bursts are
    noise or a missed capture.
    """
    parts = arg.split(":", 2)
    if len(parts) != 3:
        msg = f"bad format — expected remote:button:packet, got {arg[:40]!r}"
        print(f"[ir_main] ir_save_packet ERROR: {msg}")
        return f"err:{msg}"

    remote, button, packet_pipe = (p.strip() for p in parts)

    if not remote or not button:
        print("[ir_main] ir_save_packet ERROR: empty remote or button name")
        return "err:empty name"

    timings = [t for t in packet_pipe.split("|") if t]
    if len(timings) < 20:
        print(f"[ir_main] ir_save_packet REJECT: only {len(timings)} timings (min 20)")
        return "err:too short"

    try:
        _upsert_packet(remote, button, packet_pipe)
        print(f"[ir_main] ir_save_packet OK — {remote}/{button}, {len(timings)} timings")
        return "ok"
    except Exception as exc:
        print(f"[ir_main] ir_save_packet EXCEPTION: {exc}")
        return f"err:{exc}"


# ─── Register RPC handlers ───────────────────────────────────────────────────
Bridge.provide("ir_list_remotes", rpc_ir_list_remotes)
Bridge.provide("ir_list_buttons", rpc_ir_list_buttons)
Bridge.provide("ir_get_packet",   rpc_ir_get_packet)
Bridge.provide("ir_save_packet",  rpc_ir_save_packet)

print("[ir_main] IR RPC handlers registered.")
