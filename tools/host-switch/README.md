# Connection scripts and PC/PS5 switching

logid supports per-device `on_connect` and `on_disconnect` commands. See the
[root README](../../README.md) for generic hook semantics. This folder contains
an optional script that uses them for this setup:

- MX Keys S: HOST2 = PC, HOST1 = PS5.
- MX Master 4: matching host slots.
- M34WQ: HDMI1 (`0x11`) for PS5, DisplayPort (`0x0f`) for PC.

## Native hooks (tested fallback)

Keyboard link loss lasting 400 ms sends the mouse to HOST1, then selects HDMI1.
A connection to this PC selects DP when a return is pending. Return uses the link
event immediately, without querying the keyboard or waiting for its remaps to be
reapplied. There is no target-host notification requirement in this mode.

The receiver can report sleep, power-off, radio loss and switching to any other
host in the same way. This fallback therefore assumes every keyboard link loss
means HOST1. The 400 ms debounce starts when the receiver reports the loss;
receiver-side detection and the monitor's input transition can add latency.
Mouse return from PS5 remains manual because PS5 cannot run the return handler.

Requires `ddcutil`, `i2c-dev` and Python with PyGObject (`python-gobject` on Arch).
Enable the monitor's DDC/CI setting.

```sh
cmake --build build -j4
cmake --build build --target connection-hooks-test -j4
./build/connection-hooks-test
python3 tools/host-switch/test_host_switch.py
pkexec /usr/bin/bash "$PWD/tools/host-switch/install.sh" --hooks
```

The installer backs up the original daemon at
`/var/lib/logi-host-switch/logid.before-host-switch`, installs logid and the script,
and disables the companion service to avoid duplicate processing. It preserves
existing `/etc/logid.cfg` and `/etc/logi-host-switch.json` files.

Merge the two hooks from [logid-hooks.example.cfg](logid-hooks.example.cfg) into
your keyboard entry in `/etc/logid.cfg`. Set `"departure_mode": "link_state"`
in `/etc/logi-host-switch.json`, configure the device names, host slots and monitor
adapter, then restart logid:

```sh
pkexec systemctl restart logid.service
journalctl -u logid -f
```

The hooks invoke [run-event.sh](run-event.sh), which supplies writable XDG cache
and state paths for `ddcutil`. Commands for each device run in order; a reconnect
cannot overtake a pending monitor command. Each new link loss triggers departure
even if an earlier DDC failure left a return pending. The return is persisted in
`/var/lib/logi-host-switch/state.json` across restarts.

The daemon's original `/etc/logid.cfg` was preserved while installing this setup;
local additions consist of the two keyboard hooks. Package upgrades need to
include this checkout's changes to retain the interfaces and hooks.

## Monitor and host details

The adapter name resolves the current `/dev/i2c-N` number. This NVIDIA driver
exposes no connector `ddc` symlink; the configured adapter was verified as M34WQ
on `card1-DP-1`. Ambiguous adapter names are rejected. Monitor writes retry up to
three times. Confirm the physical result: a successful write alone does not
prove the display changed inputs. The monitor must keep DP-side DDC reachable
while HDMI1 is selected for the return command to work.

logid exports `pizza.pixl.LogiOps.HostSwitch` on supported device nodes:

- `GetHostInfo() -> (hostCount, currentHost)`.
- `SetHost(y)` sends the command without waiting for a reply that switching may sever.
- `Switched(yy)` carries previous and target host indices.
- `Connected()` reports wakeups, including repeated connection broadcasts.
- `Version` contains the CHANGE_HOST feature version.

D-Bus host indices are zero-based; JSON settings are one-based. D-Bus access
retains logid's existing root-only policy.

## Target-host events and the companion service

An alternative `"departure_mode": "host_event"` only acts on an explicit
HOST2 -> HOST1 notification. That path is reported for MX Keys S firmware 81.2.17
(CHANGE_HOST v2). The keyboard tested here has RBK81.01.B0015 (CHANGE_HOST v1)
and produced no target-host event in a physical test, so it uses the link fallback.
No firmware was installed. Official release notes:
https://support.logi.com/hc/en-us/articles/39066494614423-MX-Keys-S-Firmware-Release-Notes

The optional companion service listens to logid's D-Bus events instead of using
native hooks. To use it, remove the native hooks from your keyboard configuration
and install with `--service`. Do not enable both methods for the same device.
The companion unit also provides writable XDG cache and state directories.

```sh
pkexec /usr/bin/bash "$PWD/tools/host-switch/install.sh" --service
journalctl -u logi-host-switch -f
```

Diagnostics (the monitor test selects HDMI1 for three seconds and restores DP):

```sh
pkexec env XDG_CACHE_HOME=/var/cache/logi-host-switch XDG_STATE_HOME=/var/lib/logi-host-switch /usr/bin/python3 /usr/local/lib/logi-host-switch/logi-host-switch.py --probe
pkexec env XDG_CACHE_HOME=/var/cache/logi-host-switch XDG_STATE_HOME=/var/lib/logi-host-switch /usr/bin/python3 /usr/local/lib/logi-host-switch/logi-host-switch.py --test-monitor
```

[probe-bolt.py](probe-bolt.py) reads firmware and control capabilities directly.
Its optional analytics mode changes temporary, non-diverting notifications for
the three host keys. This diagnostic did not provide a working key interception
path on the tested firmware. Disable analytics after a diagnostic session.
