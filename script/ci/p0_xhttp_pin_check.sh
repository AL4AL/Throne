#!/usr/bin/env bash
# Smoke check (not a unit test): source still accepts pinSHA256 as pcs, and the
# Xray HTTP-version rules we rely on are documented here as fixtures.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

XRAY_TLS="$ROOT/src/configs/common/xrayStreamSetting.cpp"
SING_TLS="$ROOT/src/configs/common/TLS.cpp"

grep -q 'else if (query.hasQueryItem("pinSHA256"))' "$XRAY_TLS" || fail "xray TLS missing pinSHA256 fallback"
grep -q 'else if (query.hasQueryItem("pinSHA256"))' "$SING_TLS" || fail "sing-box TLS missing pinSHA256 fallback"
pass "pcs → pinSHA256 fallback still in TLS parsers"

python3 - <<'PY'
from urllib.parse import urlparse, parse_qs

def resolve(link: str):
    q = parse_qs(urlparse(link).query)
    if q.get("pcs"):
        return q["pcs"][0]
    if q.get("pinSHA256"):
        return q["pinSHA256"][0]
    return None

pin = "a" * 64
assert resolve(f"vless://u@h:443?pcs={pin}") == pin
assert resolve(f"vless://u@h:443?pinSHA256={pin}") == pin
assert resolve(f"vless://u@h:443?pcs={pin}&pinSHA256={'b'*64}") == pin
assert resolve("vless://u@h:443") is None
print("PASS: link query resolution")
PY

python3 - <<'PY'
def decide(tls: bool, alpn: list[str]) -> str:
    if not tls:
        return "1.1"
    if len(alpn) != 1:
        return "2"
    if alpn[0] == "http/1.1":
        return "1.1"
    return "2"

assert decide(False, []) == "1.1"
assert decide(True, []) == "2"
assert decide(True, ["h3", "h2", "http/1.1"]) == "2"
assert decide(True, ["http/1.1"]) == "1.1"
print("PASS: Xray HTTP-version fixtures")
PY

echo "OK: p0_xhttp_pin_check"
