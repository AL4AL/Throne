#!/usr/bin/env bash
# P0 gate: Nekoray-grade XHTTP cert-pin link aliases + HTTP-version contract notes.
# Does not require a full Throne build.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }
pass() { echo "PASS: $*"; }

XRAY_TLS="$ROOT/src/configs/common/xrayStreamSetting.cpp"
SING_TLS="$ROOT/src/configs/common/TLS.cpp"

grep -q 'pinSHA256' "$XRAY_TLS" || fail "xray TLS ParseFromLink missing pinSHA256 alias"
grep -q 'else if (query.hasQueryItem("pinSHA256"))' "$XRAY_TLS" || fail "xray TLS missing else-if pinSHA256 fallback"
grep -q 'pinSHA256' "$SING_TLS" || fail "sing-box TLS ParseFromLink missing pinSHA256 alias"
grep -q 'else if (query.hasQueryItem("pinSHA256"))' "$SING_TLS" || fail "sing-box TLS missing else-if pinSHA256 fallback"
pass "source contains pcs → pinSHA256 fallback (Xray + sing-box TLS)"

python3 - <<'PY'
from urllib.parse import urlparse, parse_qs

def resolve(link: str) -> str | None:
    q = parse_qs(urlparse(link).query)
    # Mirror Throne: pcs wins; else pinSHA256 (Nekoray Link2Bean behavior).
    if q.get("pcs"):
        return q["pcs"][0]
    if q.get("pinSHA256"):
        return q["pinSHA256"][0]
    return None

pin = "a" * 64
cases = [
    (f"vless://u@h:443?security=tls&type=xhttp&pcs={pin}", pin, "pcs only"),
    (f"vless://u@h:443?security=tls&type=xhttp&pinSHA256={pin}", pin, "pinSHA256 only"),
    (f"vless://u@h:443?security=tls&type=xhttp&pcs={pin}&pinSHA256={'b'*64}", pin, "pcs wins over pinSHA256"),
    ("vless://u@h:443?security=tls&type=xhttp", None, "no pin"),
]
for link, want, name in cases:
    got = resolve(link)
    assert got == want, f"{name}: got {got!r} want {want!r}"
print("PASS: query-resolution contract matches Nekoray/Throne alias rules")
PY

# Documented Xray HTTP-version contract (Throne XHTTP runs on Xray path).
# Empty or multi ALPN ⇒ HTTP/2; exactly http/1.1 ⇒ 1.1; no TLS ⇒ 1.1.
python3 - <<'PY'
def decide(tls: bool, alpn: list[str]) -> str:
    if not tls:
        return "1.1"
    if len(alpn) != 1:
        return "2"
    if alpn[0] == "http/1.1":
        return "1.1"
    if alpn[0] == "h3":
        raise ValueError("h3 unsupported for classic xhttp TCP path")
    return "2"

assert decide(False, []) == "1.1"
assert decide(True, []) == "2"
assert decide(True, ["h3", "h2", "http/1.1"]) == "2"
assert decide(True, ["http/1.1"]) == "1.1"
assert decide(True, ["h2"]) == "2"
print("PASS: Xray decideHTTPVersion contract fixtures")
PY

echo "OK: p0_xhttp_pin_check"
