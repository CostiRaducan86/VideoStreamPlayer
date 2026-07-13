"""Typed data models returned by the VilsSharpX SDK."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict


@dataclass
class ComparisonStats:
    """Comparison statistics from pane D.

    ``total_pixels_dev`` is the count of pixels whose |B-A| exceeds the deadband.
    """

    max_positive_dev: int
    max_negative_dev: int
    average_dev: float
    total_pixels_dev: int
    dark_pixels: int

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "ComparisonStats":
        return cls(
            max_positive_dev=int(data.get("max_positive_dev", 0)),
            max_negative_dev=int(data.get("max_negative_dev", 0)),
            average_dev=float(data.get("average_dev", 0.0)),
            total_pixels_dev=int(data.get("total_pixels_dev", 0)),
            dark_pixels=int(data.get("dark_pixels", 0)),
        )
