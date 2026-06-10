"""Basic end-to-end smoke test for the VilsSharpX automation API.

Run with the WPF application open:

    python examples/basic_test.py
"""

import sys
import time
from pathlib import Path

# Allow running the example without installing the package.
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from vilssharpx import VilsClient, VilsApiError


def main() -> int:
    client = VilsClient()  # http://127.0.0.1:8420

    try:
        print("1) Ping:", client.ping())

        print("2) Start simulation @100 fps")
        client.start_simulation(fps=100)

        print("3) Set comparison settings (mode=0, deadband=5, bDelta=0)")
        client.set_comparison_settings(mode=0, deadband=5, b_delta=0)

        print("4) Poll comparison stats for ~5s")
        for _ in range(5):
            time.sleep(1.0)
            stats = client.get_comparison_stats()
            print(f"   total_pixels_dev={stats.total_pixels_dev} "
                  f"avg={stats.average_pixels_dev:.1f} "
                  f"max+={stats.max_positive_dev} max-={stats.max_negative_dev}")

        print("5) Save a snapshot of pane D")
        client.save_frame_snapshot("paneD.png", pane="D")
        print("   saved paneD.png")

        print("6) Pause / resume")
        client.pause_simulation()
        time.sleep(0.5)
        client.resume_simulation()

        print("7) Stop simulation")
        client.stop_simulation()

        print("Done.")
        return 0

    except VilsApiError as exc:
        print(f"API error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
