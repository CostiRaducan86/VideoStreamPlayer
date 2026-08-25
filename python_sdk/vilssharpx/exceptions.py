"""Exceptions raised by the VilsSharpX SDK."""

from __future__ import annotations

from typing import Any


class VilsApiError(Exception):
    """Raised when the API returns an error response or the transport fails."""

    def __init__(
        self,
        message: str,
        code: str = "ERROR",
        status_code: int | None = None,
        details: Any = None,
    ) -> None:
        super().__init__(message)
        self.message = message
        self.code = code
        self.status_code = status_code
        self.details = details

    def __str__(self) -> str:  # pragma: no cover - cosmetic
        parts = [f"{self.code}: {self.message}"]
        if self.status_code is not None:
            parts.append(f"(HTTP {self.status_code})")
        return " ".join(parts)
