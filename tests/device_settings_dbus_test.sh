#!/bin/sh
set -eu

if [ "${1:-}" != --session ]; then
    daemon=$1
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    mkdir -p "$work/config/kde-bluetooth-smartlocker"
    cat > "$work/config/kde-bluetooth-smartlocker/daemon.conf" <<'EOF'
[devices]
AA%3ABB%3ACC%3ADD%3AEE%3AFF\rssiThreshold=-55
EOF
    dbus-run-session -- sh "$0" --session "$daemon" "$work"
    exit $?
fi

shift
daemon=$1
work=$2
daemon_pid=""
cleanup() {
    if [ -n "$daemon_pid" ]; then
        kill "$daemon_pid" 2>/dev/null || true
        wait "$daemon_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT

start_daemon() {
    XDG_CONFIG_HOME="$work/config" \
    XDG_DATA_HOME="$work/data" \
    XDG_STATE_HOME="$work/state" \
        "$daemon" --device AA:BB:CC:DD:EE:FF --lock-command /bin/true >"$work/daemon.log" 2>&1 &
    daemon_pid=$!
    attempt=0
    while [ "$attempt" -lt 50 ]; do
        if busctl --address="$DBUS_SESSION_BUS_ADDRESS" list | grep -q org.kde.SmartLocker1; then
            break
        fi
        if ! kill -0 "$daemon_pid" 2>/dev/null; then
            cat "$work/daemon.log" >&2
            exit 1
        fi
        attempt=$((attempt + 1))
        sleep 0.1
    done
    if [ "$attempt" -eq 50 ]; then
        cat "$work/daemon.log" >&2
        exit 1
    fi
}

session_id=${XDG_SESSION_ID:-}
[ -n "$session_id" ] || exit 77
session_reply=$(busctl --system call org.freedesktop.login1 /org/freedesktop/login1 org.freedesktop.login1.Manager GetSession s "$session_id" 2>/dev/null) || exit 77
session_path=${session_reply#*\"}
session_path=${session_path%%\"*}
[ -n "$session_path" ] || exit 77
active=$(busctl --system get-property org.freedesktop.login1 "$session_path" org.freedesktop.login1.Session Active 2>/dev/null) || exit 77
locked_hint=$(busctl --system get-property org.freedesktop.login1 "$session_path" org.freedesktop.login1.Session LockedHint 2>/dev/null) || exit 77
session_state=$(busctl --system get-property org.freedesktop.login1 "$session_path" org.freedesktop.login1.Session State 2>/dev/null) || exit 77
case "$active:$locked_hint:$session_state" in
    *"b true:b false:s \"locking\""*|*"b true:b false:s \"locked\""*) exit 77 ;;
esac
case "$active:$locked_hint" in
    "b true:b false") ;;
    *) exit 77 ;;
esac

start_daemon
settings=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings)
case "$settings" in *'"rssiThreshold":{"type":"i","data":-55}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionRssiThreshold":{"type":"i","data":-55}'*) ;; *) exit 1 ;; esac
connection_threshold_method=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" introspect org.kde.SmartLocker1 /SmartLocker | grep SetDeviceConnectionRssiThreshold)
case "$connection_threshold_method" in *"si"*"b"*) ;; *) exit 1 ;; esac
kill "$daemon_pid"
wait "$daemon_pid" 2>/dev/null || true
start_daemon
settings=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings)
case "$settings" in *'"rssiThreshold":{"type":"i","data":-55}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionRssiThreshold":{"type":"i","data":-55}'*) ;; *) exit 1 ;; esac
ble_result=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceBleEnabled sb AA:BB:CC:DD:EE:FF false)
connection_result=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceConnectionEnabled sb AA:BB:CC:DD:EE:FF false)
connection_threshold_result=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceConnectionRssiThreshold si AA:BB:CC:DD:EE:FF -- -65)
ble_threshold_result=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SetDeviceRssiThreshold si AA:BB:CC:DD:EE:FF -- -45)
case "$ble_result:$connection_result:$connection_threshold_result:$ble_threshold_result" in
    "b true:b true:b true:b true") ;;
    *) exit 1 ;;
esac
settings=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings)
case "$settings" in *'"bleEnabled":{"type":"b","data":false}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionEnabled":{"type":"b","data":false}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionRssiThreshold":{"type":"i","data":-65}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"rssiThreshold":{"type":"i","data":-45}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"bleRssiAvailable":{"type":"b","data":'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionRssiAvailable":{"type":"b","data":'*) ;; *) exit 1 ;; esac

kill "$daemon_pid"
wait "$daemon_pid" 2>/dev/null || true
start_daemon
settings=$(busctl --address="$DBUS_SESSION_BUS_ADDRESS" --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings)
case "$settings" in *'"bleEnabled":{"type":"b","data":false}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionEnabled":{"type":"b","data":false}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"connectionRssiThreshold":{"type":"i","data":-65}'*) ;; *) exit 1 ;; esac
case "$settings" in *'"rssiThreshold":{"type":"i","data":-45}'*) ;; *) exit 1 ;; esac
