"""VilsSharpX automation SDK (v1).

Thin Python client for the localhost REST automation API exposed by the
VilsSharpX WPF application. Uses only the Python standard library.
"""

from .client import VilsClient
from .models import ComparisonStats
from .exceptions import VilsApiError

__all__ = ["VilsClient", "ComparisonStats", "VilsApiError"]
__version__ = "1.0.0"
