// Pure-QML bridge for kde-bluetooth-smartlocker. This deliberately avoids
// org.kde.smartlocker so it works with Quickshell's system Qt; all daemon
// access goes through busctl over the session bus.
import QtQuick
import Quickshell.Io

Item {
    id: root

    // Deliberately not named `state`: Item.state is a state-machine property,
    // and assigning state names that have no matching State object resets it.
    property string stateName: "unavailable"
    property var devices: []
    property int snoozeSeconds: 30
    property string lastError: ""
    property int refreshIntervalSec: 5
    readonly property bool available: stateName !== "unavailable"
    readonly property bool monitoring: stateName === "monitoring" || stateName === "away" || stateName === "snoozed"
    signal changed()

    // Daemon truth and optimistic values are keyed by canonical MAC address.
    property var internalSettings: ({})
    property var pendingEnabled: ({})
    property var pendingThresholds: ({})
    property var pendingState: null
    property var mutationQueue: []
    property var currentMutation: null
    property int mutationSeq: 0
    property var pendingEnabledToken: ({})
    property var pendingThresholdToken: ({})
    property int pendingStateToken: 0
    property bool refreshAgain: false
    property bool refreshExpected: false
    property bool refreshStarted: false

    readonly property string readScript:
        "if ! command -v busctl >/dev/null 2>&1; then printf '__SL_ERROR__busctl is not installed\\n'; exit 127; fi; " +
        "printf '__SL_STATE__\\n'; busctl --user --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 State; printf '\\n'; " +
        "printf '__SL_DEVICES__\\n'; busctl --user --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 Devices; printf '\\n'; " +
        "printf '__SL_SETTINGS__\\n'; busctl --user --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 DeviceSettings; printf '\\n'; " +
        "printf '__SL_SNOOZE__\\n'; busctl --user --json=short call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 SnoozeSeconds"

    Timer {
        interval: Math.max(2, root.refreshIntervalSec) * 1000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.refresh()
    }
    Process {
        id: refreshProcess
        running: false
        command: ["/bin/sh", "-c", root.readScript]
        stdout: StdioCollector { id: refreshStdout; waitForEnd: true }
        stderr: StdioCollector { id: refreshStderr; waitForEnd: true }
        onStarted: root.refreshStarted = true
        onRunningChanged: root.checkRefreshStartFailure()
        onExited: (exitCode) => {
            root.refreshExpected = false;
            root.refreshStarted = false;
            root.applyRefresh(refreshStdout.text || "", refreshStderr.text || "", exitCode);
            if (root.refreshAgain) { root.refreshAgain = false; root.refresh(); }
            else root.pumpMutation();
        }
    }
    Process {
        id: mutationProcess
        running: false
        command: []
        stdout: StdioCollector { id: mutationStdout; waitForEnd: true }
        stderr: StdioCollector { id: mutationStderr; waitForEnd: true }
        onExited: (exitCode) => { root.finishMutation(exitCode); }
    }

    function refresh() {
        if (refreshProcess.running || mutationProcess.running) { root.refreshAgain = true; return; }
        root.refreshAgain = false;
        root.refreshExpected = true;
        root.refreshStarted = false;
        refreshProcess.running = true;
    }
    function clearError() { root.setLastError(""); }
    function setEnabled(enabled) {
        if (!root.available) { root.setLastError("The SmartLocker daemon is not running."); return; }
        var token = root.nextMutationId();
        var previous = root.stateName;
        root.pendingState = enabled ? "monitoring" : "disabled";
        root.pendingStateToken = token;
        root.stateName = root.pendingState;
        root.changed();
        root.enqueueMutation(root.commandFor("SetEnabled", "b", [enabled ? "true" : "false"]),
            function() { root.clearPendingState(token); },
            function() { root.clearPendingState(token, previous); });
    }
    function snooze(seconds) {
        if (!root.available) { root.setLastError("The SmartLocker daemon is not running."); return; }
        var value = Math.round(Number(seconds));
        if (!isFinite(value)) return;
        root.enqueueMutation(root.commandFor("Snooze", "i", [String(value)]), function() {}, function() {});
    }
    function setDeviceEnabled(address, enabled) {
        var addr = String(address || "");
        if (addr === "") return;
        if (!root.available) { root.setLastError("The SmartLocker daemon is not running."); return; }
        var token = root.nextMutationId();
        root.pendingEnabled[addr] = enabled;
        root.pendingEnabledToken[addr] = token;
        root.changed();
        root.enqueueMutation(root.commandFor("SetDeviceEnabled", "sb", [addr, enabled ? "true" : "false"]),
            function() { root.commitDevice(addr, "enabled", enabled); root.clearPending(root.pendingEnabled, root.pendingEnabledToken, addr, token); },
            function() { root.clearPending(root.pendingEnabled, root.pendingEnabledToken, addr, token); });
    }
    function setDeviceRssiThreshold(address, thresholdDbm) {
        var addr = String(address || "");
        var threshold = Math.round(Number(thresholdDbm));
        if (addr === "" || !isFinite(threshold)) return;
        if (!root.available) { root.setLastError("The SmartLocker daemon is not running."); return; }
        var token = root.nextMutationId();
        root.pendingThresholds[addr] = threshold;
        root.pendingThresholdToken[addr] = token;
        root.changed();
        root.enqueueMutation(root.commandFor("SetDeviceRssiThreshold", "si", [addr, String(threshold)]),
            function() { root.commitDevice(addr, "rssiThreshold", threshold); root.clearPending(root.pendingThresholds, root.pendingThresholdToken, addr, token); },
            function() { root.clearPending(root.pendingThresholds, root.pendingThresholdToken, addr, token); });
    }
    function deviceName(address) {
        var settings = root.internalSettings[String(address || "")];
        return settings && typeof settings.name === "string" ? settings.name : "";
    }
    function deviceEnabled(address) {
        var addr = String(address || "");
        if (Object.prototype.hasOwnProperty.call(root.pendingEnabled, addr)) return root.pendingEnabled[addr] === true;
        var settings = root.internalSettings[addr];
        return settings && typeof settings.enabled === "boolean" ? settings.enabled : true;
    }
    function deviceRssiThreshold(address) {
        var addr = String(address || "");
        if (Object.prototype.hasOwnProperty.call(root.pendingThresholds, addr)) {
            var pending = root.pendingThresholds[addr];
            if (typeof pending === "number") return pending;
        }
        var settings = root.internalSettings[addr];
        return settings && typeof settings.rssiThreshold === "number" ? settings.rssiThreshold : -70;
    }
    function applyRefresh(text, stderrText, exitCode) {
        var sections = root.splitSections(text);
        var parsedState = root.parseState((sections.__SL_STATE__ || []).join("\n"));
        var changedNow = false;
        if (parsedState === null) {
            // Unreachable daemon: drop all cached state and report once.
            if (root.stateName !== "unavailable") { root.stateName = "unavailable"; changedNow = true; }
            if (root.devices.length !== 0) { root.devices = []; changedNow = true; }
            if (Object.keys(root.internalSettings).length !== 0) { root.internalSettings = {}; changedNow = true; }
            if (Object.keys(root.pendingEnabled).length !== 0) { root.pendingEnabled = {}; changedNow = true; }
            if (Object.keys(root.pendingThresholds).length !== 0) { root.pendingThresholds = {}; changedNow = true; }
            root.pendingEnabledToken = {};
            root.pendingThresholdToken = {};
            root.pendingState = null;
            root.pendingStateToken = 0;
            var errorLine = (sections.__SL_ERROR__ || [])[0] || "";
            var message = errorLine !== "" ? errorLine.trim() : "The SmartLocker daemon is not running.";
            if (root.lastError !== message)
                console.warn("SmartLockerBridge: state read failed (exit " + exitCode + "): " + root.firstLine(stderrText));
            root.setLastError(message);
            if (changedNow) root.changed();
            return;
        }
        // State parsed: update each section independently and keep last good data.
        var desired = root.pendingState !== null ? root.pendingState : parsedState;
        if (root.stateName !== desired) { root.stateName = desired; changedNow = true; }
        var newDevices = root.parseDevices((sections.__SL_DEVICES__ || []).join("\n"));
        if (newDevices !== null && JSON.stringify(newDevices) !== JSON.stringify(root.devices)) { root.devices = newDevices; changedNow = true; }
        var newSettings = root.parseSettings((sections.__SL_SETTINGS__ || []).join("\n"));
        if (newSettings !== null && JSON.stringify(newSettings) !== JSON.stringify(root.internalSettings)) { root.internalSettings = newSettings; changedNow = true; }
        var newSnooze = root.parseSnooze((sections.__SL_SNOOZE__ || []).join("\n"));
        if (newSnooze !== null && newSnooze !== root.snoozeSeconds) { root.snoozeSeconds = newSnooze; changedNow = true; }
        if (changedNow) root.changed();
    }
    function splitSections(text) {
        var sections = {};
        var current = "";
        var lines = String(text || "").split("\n");
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i];
            if (/^__SL_(STATE|DEVICES|SETTINGS|SNOOZE)__$/.test(line)) { current = line; sections[current] = []; }
            else if (line.indexOf("__SL_ERROR__") === 0) { sections.__SL_ERROR__ = [line.substring(12)]; current = "__SL_ERROR__"; }
            else if (current !== "" && line.trim() !== "") sections[current].push(line);
        }
        return sections;
    }
    // busctl --json=short wraps every value as {"type": ..., "data": [...]}.
    function parseData(text) {
        var trimmed = String(text || "").trim();
        if (trimmed === "") return null;
        var payload;
        try { payload = JSON.parse(trimmed); }
        catch (error) { console.warn("SmartLockerBridge: invalid JSON from busctl: " + error); return null; }
        return payload && Array.isArray(payload.data) && payload.data.length > 0 ? payload.data[0] : null;
    }
    function unwrapVariant(value) {
        if (value && typeof value === "object" && !Array.isArray(value)
                && Object.prototype.hasOwnProperty.call(value, "data")) return value.data;
        return value;
    }
    function parseState(text) {
        var value = root.parseData(text);
        return typeof value === "string" ? value : null;
    }
    function parseDevices(text) {
        var values = root.parseData(text);
        if (!Array.isArray(values)) return null;
        var result = [];
        for (var i = 0; i < values.length; ++i) if (typeof values[i] === "string") result.push(values[i]);
        return result;
    }
    function parseSnooze(text) {
        var value = root.parseData(text);
        return typeof value === "number" && isFinite(value) ? Math.round(value) : null;
    }
    function parseSettings(text) {
        var map = root.parseData(text);
        if (!map || typeof map !== "object" || Array.isArray(map)) return null;
        var result = {};
        var addresses = Object.keys(map);
        for (var i = 0; i < addresses.length; ++i) {
            var address = addresses[i];
            var props = root.unwrapVariant(map[address]);
            if (!props || typeof props !== "object") props = {};
            var name = root.unwrapVariant(props.name);
            var enabled = root.unwrapVariant(props.enabled);
            var threshold = root.unwrapVariant(props.rssiThreshold);
            result[address] = { name: typeof name === "string" ? name : "",
                enabled: typeof enabled === "boolean" ? enabled : true,
                rssiThreshold: typeof threshold === "number" && isFinite(threshold) ? Math.round(threshold) : -70 };
        }
        return result;
    }
    function nextMutationId() { root.mutationSeq += 1; return root.mutationSeq; }
    // busctl needs an explicit signature, and `--` keeps a negative integer
    // from being parsed as an option. Arguments are passed without a shell.
    function commandFor(method, signature, args) {
        var command = ["busctl", "--user", "--json=short", "call",
                       "org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", method, signature, "--"];
        for (var i = 0; i < args.length; ++i) command.push(String(args[i]));
        return command;
    }
    function enqueueMutation(command, onSuccess, onFailure) {
        root.mutationQueue.push({ command: command, onSuccess: onSuccess, onFailure: onFailure });
        root.pumpMutation();
    }
    function pumpMutation() {
        if (mutationProcess.running || refreshProcess.running || root.mutationQueue.length === 0) return;
        var item = root.mutationQueue.shift();
        root.currentMutation = item;
        mutationProcess.command = item.command;
        mutationProcess.running = true;
    }
    function finishMutation(exitCode) {
        var item = root.currentMutation;
        root.currentMutation = null;
        var output = mutationStdout.text || "";
        var errors = mutationStderr.text || "";
        if (item) {
            if (exitCode === 0 && root.parseData(output) === true) { item.onSuccess(); root.clearError(); }
            else { item.onFailure(); root.setLastError(root.mutationFailure(exitCode, output, errors)); }
        }
        root.refresh();
        root.pumpMutation();
    }
    function checkRefreshStartFailure() {
        if (!root.refreshExpected || refreshProcess.running || root.refreshStarted) return;
        root.refreshExpected = false; root.refreshAgain = false;
        root.applyRefresh("", "", -1); root.pumpMutation();
    }
    function mutationFailure(exitCode, output, errors) {
        var detail = root.firstLine(errors);
        if (detail !== "") return "The daemon rejected the change: " + detail;
        if (exitCode !== 0) return "The daemon rejected the change (busctl exit code " + exitCode + ").";
        return root.parseData(output) === false
            ? "The daemon rejected the change: the session may be locked."
            : "The daemon rejected the change.";
    }
    function clearPending(values, tokens, address, token) {
        if (tokens[address] !== token) return;
        delete values[address];
        delete tokens[address];
        root.changed();
    }
    // Commit an accepted toggle/threshold so deviceEnabled()/deviceRssiThreshold()
    // do not fall back to the previous daemon value while the refresh is in flight.
    function commitDevice(address, key, value) {
        var current = root.internalSettings[address];
        var settings = current ? Object.assign({}, current) : { name: "", enabled: true, rssiThreshold: -70 };
        settings[key] = value;
        var next = Object.assign({}, root.internalSettings);
        next[address] = settings;
        root.internalSettings = next;
    }
    function clearPendingState(token, revertTo) {
        if (root.pendingStateToken !== token) return;
        root.pendingStateToken = 0;
        root.pendingState = null;
        if (revertTo !== undefined) root.stateName = revertTo;
        root.changed();
    }
    function setLastError(message) {
        var value = String(message || "");
        if (root.lastError !== value) { root.lastError = value; root.changed(); }
    }
    function firstLine(text) {
        var lines = String(text || "").trim().split("\n").map(function(l) { return l.trim(); })
            .filter(function(l) { return l !== ""; });
        if (lines.length === 0) return "";
        return lines[0].length > 200 ? lines[0].substring(0, 200) : lines[0];
    }
}
