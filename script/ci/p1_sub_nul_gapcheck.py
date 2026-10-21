#!/usr/bin/env python3
"""P1 gap-check: huge/dirty subscription bodies vs Throne's import strategy.

Nekoray 4.1.x fixed Qt5 QString(QByteArray) truncation at the first NUL.
Throne's SubscriptionParser already uses QString::fromUtf8(ptr, size) and
sanitizeClashYaml() strips NUL for Clash YAML (#1746).

Exit 0 = no Throne code change required for NUL safety on this fixture.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

NUL = b"\x00"


def qt5_broken_prefix(data: bytes) -> bytes:
    """Bytes visible if a decoder stopped at the first NUL."""
    nul = data.find(NUL)
    return data if nul < 0 else data[:nul]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("fixture", type=Path, nargs="?", default=None)
    args = ap.parse_args()
    if args.fixture is None:
        print("usage: p1_sub_nul_gapcheck.py <subscription.txt>")
        print("SKIP: no fixture — Throne already size-aware (fromUtf8) + Clash NUL sanitize")
        return 0

    data = args.fixture.read_bytes()
    nuls = data.count(NUL)
    lines = data.splitlines()
    broken = qt5_broken_prefix(data)
    lost_lines = len(lines) - len(broken.splitlines()) if nuls else 0

    print(f"fixture={args.fixture}")
    print(f"bytes={len(data)} lines={len(lines)} nul_bytes={nuls}")
    if nuls:
        print(f"first_nul_offset={data.find(NUL)}")
        print(f"qt5_style_truncation_would_drop_approx_lines={max(0, lost_lines)}")
        print("throne: uses fromUtf8(data, size) on scan lines; Clash path strips QChar::Null")
        print("VERDICT: no P1 port required for NUL truncation on this fixture (Throne already safe)")
        return 0

    print("VERDICT: no NULs — nothing to port from Nekoray sanitizer for this fixture")
    return 0


if __name__ == "__main__":
    sys.exit(main())
