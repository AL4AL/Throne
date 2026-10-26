#!/usr/bin/env bash
# Fail if Throne loses size-aware / NUL-safe subscription import.
# Optional arg: a subscription file (default: script/ci/fixtures/sub_with_nul.txt).
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
fail() { echo "FAIL: $*" >&2; exit 1; }

PARSER="$ROOT/src/configs/sub/SubscriptionParser.cpp"
[[ -f $PARSER ]] || fail "missing $PARSER"

grep -F -q 'fromUtf8(s.data(), static_cast<qsizetype>(s.size()))' "$PARSER" \
  || grep -F -q 'fromUtf8(text.data(), static_cast<qsizetype>(text.size()))' "$PARSER" \
  || fail "SubscriptionParser no longer uses fromUtf8(ptr, size)"

grep -q 'sanitizeClashYaml' "$PARSER" || fail "SubscriptionParser missing sanitizeClashYaml"
grep -q 'QChar::Null' "$PARSER" || fail "Clash NUL strip missing"

echo "PASS: subscription import still size-aware and Clash NUL-safe"

FIXTURE=${1:-$ROOT/script/ci/fixtures/sub_with_nul.txt}
if [[ -f $FIXTURE ]]; then
  python3 - "$FIXTURE" <<'PY'
import sys
from pathlib import Path
data = Path(sys.argv[1]).read_bytes()
nul = data.find(b"\x00")
if nul < 0:
    raise SystemExit(f"FAIL: fixture {sys.argv[1]} has no NUL")
lost = len(data.splitlines()) - len(data[:nul].splitlines())
print(f"PASS: fixture {Path(sys.argv[1]).name}: NUL at {nul}, Qt5-style truncate would drop ~{lost} lines")
PY
fi

echo "OK: p1_sub_nul_check"
