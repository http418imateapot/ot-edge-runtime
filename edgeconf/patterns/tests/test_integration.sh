#!/usr/bin/env bash
# tests/test_integration.sh
#
# End-to-end integration test: write → watch → dashboard
#
# Requires a running D-Bus session bus.  In CI the script starts a temporary
# dbus-daemon automatically; on a desktop system the user's session bus is used.
#
# Usage:  bash tests/test_integration.sh [path/to/robust_config]
#
set -euo pipefail

BINARY="${1:-./robust_config}"
TMPDIR="$(mktemp -d /tmp/rc_inttest.XXXXXX)"
CONFIG="${TMPDIR}/config.conf"
DASHBOARD_LOG="${TMPDIR}/dashboard.log"
WATCH_LOG="${TMPDIR}/watch.log"
PASS=0
FAIL=0

pass() { echo "  [PASS] $1"; PASS=$((PASS+1)); }
fail() { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }

cleanup() {
    kill "${DASHBOARD_PID:-}" "${WATCH_PID:-}" "${DBUS_PID:-}" 2>/dev/null || true
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Start a private dbus-daemon if DBUS_SESSION_BUS_ADDRESS is not set
# ---------------------------------------------------------------------------
if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    echo "Starting temporary dbus-daemon..."
    DBUS_INFO="${TMPDIR}/dbus.env"
    dbus-daemon --session --fork --print-address > "${TMPDIR}/dbus.addr" \
                --print-pid > "${TMPDIR}/dbus.pid" 2>/dev/null || true
    # Some versions of dbus-daemon output address+pid differently
    if [ -f "${TMPDIR}/dbus.addr" ]; then
        export DBUS_SESSION_BUS_ADDRESS="$(cat "${TMPDIR}/dbus.addr" | head -1)"
        DBUS_PID="$(cat "${TMPDIR}/dbus.pid" | head -1)"
    fi
    if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
        # Try dbus-launch as fallback
        eval "$(dbus-launch --sh-syntax)"
        DBUS_PID="${DBUS_SESSION_BUS_PID:-}"
    fi
    sleep 0.5
fi

BUS_OPT="--bus session"
echo "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:-<not set>}"

# ---------------------------------------------------------------------------
echo "=== Integration test suite ==="

# Start watch daemon
"$BINARY" --config "$CONFIG" --log-stderr $BUS_OPT watch \
    > "$WATCH_LOG" 2>&1 &
WATCH_PID=$!
sleep 0.5   # give it time to set up inotify

# Start dashboard (background, capture output)
"$BINARY" --log-stderr $BUS_OPT dashboard \
    > "$DASHBOARD_LOG" 2>&1 &
DASHBOARD_PID=$!
sleep 0.5   # give it time to subscribe

# Write two keys
"$BINARY" --config "$CONFIG" --log-stderr write --key host    --value edge-01
"$BINARY" --config "$CONFIG" --log-stderr write --key port    --value 8080
"$BINARY" --config "$CONFIG" --log-stderr write --key enabled --value true

sleep 1.0   # allow inotify + D-Bus propagation

# Stop watch and dashboard
kill "$WATCH_PID" "$DASHBOARD_PID" 2>/dev/null || true
wait "$WATCH_PID"     2>/dev/null || true
wait "$DASHBOARD_PID" 2>/dev/null || true

# ---------------------------------------------------------------------------
echo "--- Dashboard output ---"
cat "$DASHBOARD_LOG" || true
echo "--- Watch log ---"
cat "$WATCH_LOG" || true
echo "---"

# Validate dashboard received the signals
for key in host port enabled; do
    if grep -q "\"key\":\"${key}\"" "$DASHBOARD_LOG" 2>/dev/null; then
        pass "Dashboard received signal for key=${key}"
    else
        fail "Dashboard did NOT receive signal for key=${key}"
    fi
done

# Validate interface_version is present in payload
if grep -q '"interface_version":1' "$DASHBOARD_LOG" 2>/dev/null; then
    pass "Payload contains interface_version=1"
else
    fail "Payload missing interface_version"
fi

# ---------------------------------------------------------------------------
echo ""
echo "Results: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
