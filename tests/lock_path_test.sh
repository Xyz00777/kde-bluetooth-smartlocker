#!/bin/sh
# Verifies the daemon's lock execution path end to end: a configured device that BlueZ does
# not know about must be treated as absent, the away countdown must elapse, and the lock
# command must be executed with the "lock-session" argument.
#
# Safety: the lock command is a recorder script, never loginctl, so the real session cannot
# be locked. The config directory and the D-Bus session are both private to this test.
#
# The test needs a real system bus exposing org.bluez with a powered adapter, because absence
# is deliberately not concluded when no adapter is powered. Where BlueZ is unavailable (for
# example inside a build sandbox) it exits 77 so CTest reports a skip instead of a failure.
set -eu

DAEMON="$1"
if [ -z "$DAEMON" ] || [ ! -x "$DAEMON" ]; then
    echo "usage: $0 /path/to/kde-bluetooth-smartlocker" >&2
    exit 2
fi

# Preconditions: without them the daemon correctly refuses to conclude absence, so a failure
# would say nothing about the code under test.
[ -e /run/dbus/system_bus_socket ] || { echo "SKIP: no system bus socket"; exit 77; }
if ! dbus-send --system --dest=org.bluez --type=method_call --print-reply \
        / org.freedesktop.DBus.Peer.Ping >/dev/null 2>&1; then
    echo "SKIP: org.bluez is not reachable on the system bus"
    exit 77
fi
if ! dbus-send --system --dest=org.bluez --type=method_call --print-reply \
        / org.freedesktop.DBus.ObjectManager.GetManagedObjects 2>/dev/null \
        | grep -A1 '"Powered"' | grep -q 'boolean true'; then
    echo "SKIP: no powered Bluetooth adapter"
    exit 77
fi

WORK=$(mktemp -d)
DPID=""
BUS_PID=""
cleanup() {
    [ -n "$DPID" ] && kill "$DPID" 2>/dev/null
    [ -n "$BUS_PID" ] && kill "$BUS_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

cat > "$WORK/probe" <<EOF
#!/bin/sh
printf '%s\n' "\$*" > "$WORK/invoked"
exit 0
EOF
chmod +x "$WORK/probe"

# A self-contained session bus config, because a build sandbox has no /etc/dbus-1 at all and
# `dbus-run-session` would fail to find session.conf.
cat > "$WORK/session.conf" <<'EOF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <allow_anonymous/>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF

dbus-daemon --config-file="$WORK/session.conf" --nofork --print-address \
    >"$WORK/bus.addr" 2>"$WORK/bus.err" &
BUS_PID=$!

ADDR=""
i=0
while [ "$i" -lt 50 ]; do
    ADDR=$(head -n 1 "$WORK/bus.addr" 2>/dev/null || true)
    [ -n "$ADDR" ] && break
    i=$((i + 1))
    sleep 0.1
done
if [ -z "$ADDR" ]; then
    echo "could not start a private session bus: $(cat "$WORK/bus.err")" >&2
    exit 1
fi

# The address below is deliberately not paired with this machine, so BlueZ never reports it
# and the daemon must conclude it is absent.
DBUS_SESSION_BUS_ADDRESS="$ADDR" \
XDG_CONFIG_HOME="$WORK/config" XDG_DATA_HOME="$WORK/data" XDG_STATE_HOME="$WORK/state" \
    "$DAEMON" --device AA:BB:CC:DD:EE:FF --away-seconds 1 --lock-command "$WORK/probe" \
    >"$WORK/daemon.log" 2>&1 &
DPID=$!

i=0
while [ "$i" -lt 150 ]; do
    [ -f "$WORK/invoked" ] && break
    i=$((i + 1))
    sleep 0.2
done

# The daemon is expected to die from SIGTERM, so a non-zero wait status is not a failure.
kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true
DPID=""

if [ ! -f "$WORK/invoked" ]; then
    echo "lock command was never invoked" >&2
    echo "--- daemon log ---" >&2
    cat "$WORK/daemon.log" >&2 2>/dev/null || true
    exit 1
fi

invoked=$(cat "$WORK/invoked")
echo "lock command invoked with: $invoked"
case "$invoked" in
    *lock-session*) ;;
    *)
        echo "expected the lock command to receive 'lock-session'" >&2
        exit 1
        ;;
esac
echo "lock path verified"