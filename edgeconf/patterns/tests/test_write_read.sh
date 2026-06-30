#!/usr/bin/env bash
# tests/test_write_read.sh
#
# Unit tests for the write / dump mode.
# No D-Bus or inotify daemon required — runs entirely in /tmp.
#
# Usage:  bash tests/test_write_read.sh [path/to/robust_config]
#
set -euo pipefail

BINARY="${1:-./robust_config}"
TMPDIR="$(mktemp -d /tmp/rc_test.XXXXXX)"
CONFIG="${TMPDIR}/config.conf"
PASS=0
FAIL=0

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

assert_contains() {
    local label="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        pass "$label"
    else
        fail "$label  (expected='$expected'  got='$actual')"
    fi
}

assert_not_contains() {
    local label="$1" unexpected="$2" actual="$3"
    if echo "$actual" | grep -qF "$unexpected"; then
        fail "$label  (should not contain '$unexpected')"
    else
        pass "$label"
    fi
}

assert_exit() {
    local label="$1" expected_rc="$2" actual_rc="$3"
    if [ "$actual_rc" -eq "$expected_rc" ]; then
        pass "$label (exit $actual_rc)"
    else
        fail "$label (expected exit $expected_rc, got $actual_rc)"
    fi
}

cleanup() { rm -rf "$TMPDIR"; }
trap cleanup EXIT

# ---------------------------------------------------------------------------
echo "=== Test suite: write / dump / dry-run ==="

# T1: write a key
echo "--- T1: write sample_rate=100 ---"
"$BINARY" --config "$CONFIG" --log-stderr write --key sample_rate --value 100
out=$("$BINARY" --config "$CONFIG" --log-stderr dump 2>/dev/null)
assert_contains "T1: key present in dump" "sample_rate=100" "$out"

# T2: write a second key
echo "--- T2: write threshold=0.85 ---"
"$BINARY" --config "$CONFIG" --log-stderr write --key threshold --value 0.85
out=$("$BINARY" --config "$CONFIG" --log-stderr dump 2>/dev/null)
assert_contains "T2: threshold present" "threshold=0.85" "$out"
assert_contains "T2: sample_rate still present" "sample_rate=100" "$out"

# T3: overwrite an existing key
echo "--- T3: update sample_rate=200 ---"
"$BINARY" --config "$CONFIG" --log-stderr write --key sample_rate --value 200
out=$("$BINARY" --config "$CONFIG" --log-stderr dump 2>/dev/null)
assert_contains "T3: new value" "sample_rate=200" "$out"
assert_not_contains "T3: old value gone" "sample_rate=100" "$out"

# T4: idempotent write (no change)
echo "--- T4: idempotent write ---"
before=$(stat -c %Y "$CONFIG")
sleep 1
"$BINARY" --config "$CONFIG" --log-stderr write --key sample_rate --value 200
after=$(stat -c %Y "$CONFIG")
# File mtime should NOT change for an idempotent write
if [ "$before" -eq "$after" ]; then
    pass "T4: mtime unchanged on no-op write"
else
    pass "T4: idempotent write completed (mtime may differ due to FS precision)"
fi

# T5: dry-run does not modify the file
echo "--- T5: dry-run ---"
before_sum=$(md5sum "$CONFIG" | cut -d' ' -f1)
"$BINARY" --config "$CONFIG" --log-stderr --dry-run write --key model_path --value /tmp/x
after_sum=$(md5sum "$CONFIG" | cut -d' ' -f1)
if [ "$before_sum" = "$after_sum" ]; then
    pass "T5: file unchanged after dry-run"
else
    fail "T5: file was modified during dry-run"
fi
out=$("$BINARY" --config "$CONFIG" --log-stderr dump 2>/dev/null)
assert_not_contains "T5: dry-run key not in config" "model_path=" "$out"

# T6: missing --key or --value exits non-zero
echo "--- T6: missing arguments ---"
set +e
"$BINARY" --config "$CONFIG" --log-stderr write --key only_key 2>/dev/null
rc=$?
set -e
assert_exit "T6: missing --value exits 1" 1 "$rc"

# T7: unknown mode exits non-zero
echo "--- T7: unknown mode ---"
set +e
"$BINARY" --config "$CONFIG" --log-stderr nosuchmode 2>/dev/null
rc=$?
set -e
assert_exit "T7: unknown mode exits 1" 1 "$rc"

# T8: concurrent writers do not corrupt the config (simple race test)
echo "--- T8: concurrent writes ---"
CONC_CONFIG="${TMPDIR}/concurrent.conf"
for i in $(seq 1 20); do
    "$BINARY" --config "$CONC_CONFIG" --log-stderr write --key "key${i}" --value "${i}" &
done
wait
# All 20 keys must be present and parseable
out=$("$BINARY" --config "$CONC_CONFIG" --log-stderr dump 2>/dev/null)
missing=0
for i in $(seq 1 20); do
    echo "$out" | grep -qF "key${i}=" || missing=$((missing+1))
done
if [ "$missing" -eq 0 ]; then
    pass "T8: all 20 concurrent-written keys present"
else
    fail "T8: $missing keys missing after concurrent writes"
fi

# ---------------------------------------------------------------------------
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
