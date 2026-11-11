#!/bin/sh
set -eu

# Both buses, the fake logind/BlueZ services, settings, and lock recorder are private to
# this invocation. In particular, never inherit the host's session identity or system bus.
daemon=$1
fake_services=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/smartlocker-settings.XXXXXX")
bus_pid=""
session_pid=""
system_pid=""
fake_pid=""
daemon_pid=""
cleanup() {
    for pid in "$daemon_pid" "$fake_pid" "$bus_pid" "$session_pid" "$system_pid"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM

start_bus() {
    name=$1
    address=""
    cat > "$work/$name.conf" <<EOF
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig><type>session</type><listen>unix:tmpdir=/tmp</listen><auth>EXTERNAL</auth><allow_anonymous/>
<policy context="default"><allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>
EOF
    : > "$work/$name.addr"
    dbus-daemon --config-file="$work/$name.conf" --nofork --print-address >"$work/$name.addr" 2>"$work/$name.err" &
    bus_pid=$!
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        IFS= read -r address < "$work/$name.addr" || true
        [ -n "${address:-}" ] && return
        if ! kill -0 "$bus_pid" 2>/dev/null; then cat "$work/$name.err" >&2; exit 1; fi
        attempt=$((attempt + 1))
        sleep 0.05
    done
    echo "private $name bus did not publish an address" >&2
    exit 1
}

mkdir -p "$work/home"
: > "$work/locks"
start_bus session
session_address=$address
session_pid=$bus_pid
start_bus system
system_address=$address
system_pid=$bus_pid
bus_pid=""

DBUS_SYSTEM_BUS_ADDRESS="$system_address" "$fake_services" "$work" >"$work/fakes.log" 2>&1 &
fake_pid=$!
attempt=0
while [ "$attempt" -lt 100 ]; do
    names=$(timeout 1 busctl --address="$system_address" call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ListNames 2>/dev/null || true)
    case "$names" in *org.freedesktop.login1*org.bluez*|*org.bluez*org.freedesktop.login1*) break ;; esac
    kill -0 "$fake_pid" 2>/dev/null || { cat "$work/fakes.log" >&2; exit 1; }
    attempt=$((attempt + 1)); sleep 0.05
done
[ "$attempt" -lt 100 ] || { echo "fake system services did not start" >&2; exit 1; }

export DBUS_SESSION_BUS_ADDRESS="$session_address"
export DBUS_SYSTEM_BUS_ADDRESS="$system_address"
export HOME="$work/home"
export XDG_CONFIG_HOME="$work/config"
export XDG_DATA_HOME="$work/data"
export XDG_STATE_HOME="$work/state"
export SMARTLOCKER_LOCK_RECORD="$work/locks"

start_daemon() {
    "$daemon" --device AA:BB:CC:DD:EE:FF --lock-command "$work/record-lock" >"$work/daemon.log" 2>&1 &
    daemon_pid=$!
    attempt=0
    while [ "$attempt" -lt 100 ]; do
        busctl --address="$session_address" list 2>/dev/null | grep -q org.kde.SmartLocker1 && return
        kill -0 "$daemon_pid" 2>/dev/null || { cat "$work/daemon.log" >&2; exit 1; }
        attempt=$((attempt + 1)); sleep 0.05
    done
    cat "$work/daemon.log" >&2
    echo "daemon did not register on isolated session bus" >&2
    exit 1
}
cat > "$work/record-lock" <<'EOF'
#!/bin/sh
printf 'lock\n' >> "$SMARTLOCKER_LOCK_RECORD"
EOF
chmod +x "$work/record-lock"

set_session() {
    busctl --address="$system_address" call org.freedesktop.login1 /org/kde/SmartLockerTest org.kde.SmartLockerTest SetSessionState sb "$1" "$2" >/dev/null
}
set_session active true
start_daemon
busctl --address="$session_address" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceBleEnabled sb AA:BB:CC:DD:EE:FF false >/dev/null
busctl --address="$session_address" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceConnectionEnabled sb AA:BB:CC:DD:EE:FF false >/dev/null
busctl --address="$session_address" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceRssiThreshold si AA:BB:CC:DD:EE:FF -- -45 >/dev/null
busctl --address="$session_address" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceConnectionRssiThreshold si AA:BB:CC:DD:EE:FF -- -65 >/dev/null
set_session closing false
kill "$daemon_pid"; wait "$daemon_pid" 2>/dev/null || true; daemon_pid=""
set_session active true
start_daemon
settings=$(busctl --address="$session_address" --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings)
for expected in '"bleEnabled":{"type":"b","data":false}' '"connectionEnabled":{"type":"b","data":false}' '"rssiThreshold":{"type":"i","data":-45}' '"connectionRssiThreshold":{"type":"i","data":-65}'; do
    case "$settings" in *"$expected"*) ;; *) echo "persisted setting missing ($expected): $settings" >&2; exit 1 ;; esac
done
[ "$(wc -l < "$work/locks")" -eq 0 ] || { echo "lock recorder was unexpectedly invoked" >&2; exit 1; }
calls=$(busctl --address="$system_address" call org.freedesktop.login1 /org/kde/SmartLockerTest org.kde.SmartLockerTest Requests)
case "$calls" in 'u 0') echo "daemon never contacted private system bus" >&2; exit 1 ;; esac
printf 'isolated buses served logind/BlueZ; requests=%s; settings=%s; lock calls=0\n' "$calls" "$settings"
