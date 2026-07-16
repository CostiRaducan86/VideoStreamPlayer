"""Extract text from one or more PDF files using PyMuPDF (fitz).

Usage:
    python pdf_to_text.py <input.pdf> [<input2.pdf> ...] [--out-dir DIR] [--page-markers]

For each input PDF, a UTF-8 ``.txt`` file is written next to the PDF (or into
``--out-dir`` if given), preserving the base name. Page boundaries are annotated
so later analysis can reference "Page N" from the datasheet.

This tool is offline and does not send any content to external services, which
keeps proprietary Infineon/Hella documentation local.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import fitz  # PyMuPDF
except ImportError:
    sys.stderr.write(
        "PyMuPDF is not installed. Install it with: pip install pymupdf\n"
    )
    sys.exit(2)


def convert_pdf(pdf_path: Path, out_dir: Path | None, page_markers: bool) -> Path:
    """Convert a single PDF to a UTF-8 text file and return the output path."""
    if not pdf_path.is_file():
        raise FileNotFoundError(f"Input PDF not found: {pdf_path}")

    target_dir = out_dir if out_dir is not None else pdf_path.parent
    target_dir.mkdir(parents=True, exist_ok=True)
    out_path = target_dir / (pdf_path.stem + ".txt")

    parts: list[str] = []
    with fitz.open(pdf_path) as doc:
        for page_index in range(doc.page_count):
            page = doc.load_page(page_index)
            text = page.get_text("text")
            if page_markers:
                parts.append(f"\n===== PAGE {page_index + 1} / {doc.page_count} =====\n")
            parts.append(text)

    out_path.write_text("".join(parts), encoding="utf-8")
    return out_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract text from PDF files (PyMuPDF).")
    parser.add_argument("inputs", nargs="+", help="One or more input PDF file paths.")
    parser.add_argument(
        "--out-dir",
        default=None,
        help="Optional output directory. Defaults to each PDF's own folder.",
    )
    parser.add_argument(
        "--page-markers",
        action="store_true",
        help="Insert '===== PAGE N / M =====' markers between pages.",
    )
    args = parser.parse_args()

    out_dir = Path(args.out_dir) if args.out_dir else None
    exit_code = 0

    for raw in args.inputs:
        pdf_path = Path(raw)
        try:
            out_path = convert_pdf(pdf_path, out_dir, args.page_markers)
            print(f"OK  {pdf_path.name} -> {out_path}")
        except Exception as exc:  # noqa: BLE001 - report and continue with next file
            sys.stderr.write(f"FAIL {pdf_path}: {exc}\n")
            exit_code = 1

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
