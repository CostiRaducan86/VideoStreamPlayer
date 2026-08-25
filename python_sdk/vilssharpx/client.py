"""Synchronous client for the VilsSharpX localhost automation API.

Mirrors the ACTSmart-style command/response pattern: every call sends a
``{"command": ..., "payload": ...}`` envelope and receives a uniform
``{"ok": ..., "data": ..., "error": ...}`` response.

Uses only the Python standard library (no third-party dependencies).
"""

from __future__ import annotations

import base64
import json
import uuid
from typing import Any
from urllib import error, request

from .exceptions import VilsApiError
from .models import ComparisonStats

DEFAULT_BASE_URL = "http://127.0.0.1:8420"


class VilsClient:
    """Client for driving the VilsSharpX WPF application over REST."""

    def __init__(self, base_url: str = DEFAULT_BASE_URL, timeout: float = 5.0, api_key: str | None = None) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.api_key = api_key

    # ---- core command transport ----

    def send_command(self, command: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        """Sends a raw command and returns the ``data`` block on success."""
        body = json.dumps(
            {
                "requestId": str(uuid.uuid4()),
                "command": command,
                "payload": payload or {},
            }
        ).encode("utf-8")

        req = request.Request(
            f"{self.base_url}/api/v1/command",
            data=body,
            headers=self._build_headers(),
            method="POST",
        )

        try:
            with request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read()
        except error.HTTPError as exc:
            raw = exc.read()
        except error.URLError as exc:
            raise VilsApiError(f"Transport error: {exc.reason}", code="TRANSPORT") from exc

        try:
            parsed = json.loads(raw.decode("utf-8"))
        except json.JSONDecodeError as exc:
            raise VilsApiError("Invalid JSON response from server.", code="BAD_JSON") from exc

        if not parsed.get("ok", False):
            err = parsed.get("error") or {}
            raise VilsApiError(
                err.get("message", "Unknown error"),
                code=err.get("code", "ERROR"),
                details=err.get("details"),
            )

        return parsed.get("data") or {}

    # ---- v1 command wrappers ----

    def ping(self) -> dict[str, Any]:
        return self.send_command("Ping")

    def start_simulation(self, fps: int = 100) -> dict[str, Any]:
        return self.send_command("StartSimulation", {"fps": fps})

    def stop_simulation(self) -> dict[str, Any]:
        return self.send_command("StopSimulation")

    def pause_simulation(self) -> dict[str, Any]:
        return self.send_command("PauseSimulation")

    def resume_simulation(self) -> dict[str, Any]:
        return self.send_command("ResumeSimulation")

    def set_comparison_settings(
        self,
        mode: int | None = None,
        deadband: int | None = None,
        b_delta: int | None = None,
    ) -> dict[str, Any]:
        payload: dict[str, Any] = {}
        if mode is not None:
            payload["mode"] = mode
        if deadband is not None:
            payload["deadband"] = deadband
        if b_delta is not None:
            payload["bDelta"] = b_delta
        return self.send_command("SetComparisonSettings", payload)

    def get_comparison_stats(self) -> ComparisonStats:
        data = self.send_command("GetComparisonStats")
        return ComparisonStats.from_dict(data)

    def get_frame_snapshot(self, pane: str = "D") -> bytes:
        """Returns the decoded PNG bytes for the given pane ('A', 'B' or 'D')."""
        data = self.send_command("GetFrameSnapshot", {"pane": pane, "format": "png-base64"})
        return base64.b64decode(data["image"])

    def save_frame_snapshot(self, path: str, pane: str = "D") -> None:
        """Saves a pane snapshot to a PNG file."""
        with open(path, "wb") as fh:
            fh.write(self.get_frame_snapshot(pane))

    def _build_headers(self) -> dict[str, str]:
        headers = {"Content-Type": "application/json"}
        if self.api_key:
            headers["X-Api-Key"] = self.api_key
        return headers
