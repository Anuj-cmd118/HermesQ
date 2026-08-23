"""
web_assets.py — Locate the HermesQ dashboard for WebUI.

Resolution order (first match wins):
  1. python/static/index.html  — the real dashboard, committed alongside this file
  2. /app/assets/index.html    — App Lab deployment copy
  3. ../assets/index.html      — repo-root assets/ folder

The old "last resort" HID fallback page has been removed entirely.
If none of the above exist, a clear error is raised so the problem
is immediately visible in the logs instead of silently serving the
wrong UI.
"""

import os


def ensure_web_assets() -> str:
    """
    Return the directory path that contains index.html.
    Raises RuntimeError if the dashboard cannot be found.
    """
    here = os.path.dirname(os.path.abspath(__file__))

    candidates = [
        # 1. Committed alongside this file — always present in the repo
        os.path.join(here, "static"),
        # 2. App Lab copies assets/ to /app/assets at deploy time
        os.path.join(os.getenv("APP_HOME", "/app"), "assets"),
        "/app/assets",
        # 3. Repo-root assets/ folder (for local dev with a different layout)
        os.path.join(os.path.dirname(here), "assets"),
    ]

    for directory in candidates:
        index = os.path.join(directory, "index.html")
        if os.path.isfile(index):
            print(f"[HermesQ] Web assets: {directory}")
            return os.path.abspath(directory)

    searched = "\n  ".join(candidates)
    raise RuntimeError(
        f"[HermesQ] index.html not found. Searched:\n  {searched}\n"
        "Make sure python/static/index.html exists in the project."
    )
