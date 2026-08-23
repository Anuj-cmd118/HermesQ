"""
HermesQ AI Agent — action-oriented orchestration layer.

Interprets natural-language commands into structured HID actions.
Not a chatbot: returns action dicts for execution, not conversational text.
"""

import json
import os
import re

WORKFLOWS = {
    "dev_environment": [
        {"type": "open_url", "url": "https://github.com"},
        {"type": "delay_ms", "ms": 800},
        {"type": "open_url", "url": "https://docs.python.org/3/"},
        {"type": "delay_ms", "ms": 800},
        {"type": "key_combo", "keys": ["SUPER"]},
        {"type": "delay_ms", "ms": 300},
        {"type": "type_text", "text": "code"},
        {"type": "key", "key": "ENTER"},
    ],
    "presentation_mode": [
        {"type": "key_combo", "keys": ["SUPER", "D"]},
        {"type": "delay_ms", "ms": 500},
        {"type": "open_url", "url": "https://docs.google.com/presentation"},
        {"type": "delay_ms", "ms": 1000},
        {"type": "key", "key": "F5"},
    ],
    "open_tabs": [
        {"type": "open_url", "url": "https://github.com"},
        {"type": "delay_ms", "ms": 600},
        {"type": "key_combo", "keys": ["CTRL", "T"]},
        {"type": "delay_ms", "ms": 400},
        {"type": "open_url", "url": "https://stackoverflow.com"},
        {"type": "delay_ms", "ms": 600},
        {"type": "key_combo", "keys": ["CTRL", "T"]},
        {"type": "delay_ms", "ms": 400},
        {"type": "open_url", "url": "https://youtube.com"},
    ],
}

_INTENT_RULES = [
    (re.compile(r"\byoutube\b", re.I), {"type": "open_url", "url": "https://youtube.com"}),
    (re.compile(r"\bgithub\b", re.I), {"type": "open_url", "url": "https://github.com"}),
    (re.compile(r"\bgoogle\b", re.I), {"type": "open_url", "url": "https://google.com"}),
    (re.compile(r"\bvs\s*code\b|\bvisual\s*studio\s*code\b", re.I),
     {"type": "workflow", "name": "dev_environment"}),
    (re.compile(r"\bdevelopment\s+environment\b|\bdev\s+environment\b|\blaunch\s+my\s+dev", re.I),
     {"type": "workflow", "name": "dev_environment"}),
    (re.compile(r"\bpresentation\s+mode\b|\bprepare\s+presentation\b", re.I),
     {"type": "workflow", "name": "presentation_mode"}),
    (re.compile(r"\bopen\s+(\d+)\s+tabs?\b", re.I), {"type": "workflow", "name": "open_tabs"}),
    (re.compile(r"\bopen\s+10\s+tabs?\b", re.I), {"type": "workflow", "name": "open_tabs"}),
]


def _load_workflows():
    path = os.path.join(os.path.dirname(__file__), "workflows.json")
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as f:
                WORKFLOWS.update(json.load(f))
        except Exception as exc:
            print(f"[AI Agent] workflows.json skipped: {exc}")


_load_workflows()


def interpret_command(text):
    text = (text or "").strip()
    if not text:
        return {"intent": "empty", "actions": [], "summary": "No command provided"}

    m = re.search(r"\btype\s+(.+)", text, re.I)
    if m:
        return {
            "intent": "type_text",
            "actions": [{"type": "type_text", "text": m.group(1).strip()}],
            "summary": f"Type: {m.group(1).strip()[:40]}",
        }

    for pattern, action in _INTENT_RULES:
        if pattern.search(text):
            if action.get("type") == "workflow":
                name = action["name"]
                steps = WORKFLOWS.get(name, [])
                return {
                    "intent": f"workflow:{name}",
                    "actions": list(steps),
                    "summary": f"Run workflow: {name} ({len(steps)} steps)",
                }
            return {
                "intent": action.get("type", "unknown"),
                "actions": [dict(action)],
                "summary": f"Action: {action.get('type')} → {action.get('url', '')}",
            }

    if text.startswith("http://") or text.startswith("https://"):
        return {
            "intent": "open_url",
            "actions": [{"type": "open_url", "url": text}],
            "summary": f"Open URL: {text[:50]}",
        }

    return {
        "intent": "type_text",
        "actions": [{"type": "type_text", "text": text}],
        "summary": f"Type text via HID: {text[:40]}",
    }


def plan_to_json(plan):
    return json.dumps(plan, separators=(",", ":"))
