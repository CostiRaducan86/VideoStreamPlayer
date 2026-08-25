"""VilsSharpX automation SDK (v1).

Thin Python client for the localhost REST automation API exposed by the
VilsSharpX WPF application. Uses only the Python standard library.
"""

from .client import VilsClient
from .exceptions import VilsApiError
from .models import ComparisonStats

__all__ = ["ComparisonStats", "VilsApiError", "VilsClient"]
__version__ = "1.0.0"
