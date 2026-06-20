# UHID Battery Prototype

This is an experimental helper for checking whether a userspace UHID device can
publish Logitech battery state through the kernel HID battery path and then be
seen by UPower.

Build it directly:

```sh
c++ -std=c++20 -Wall -Wextra -O2 tools/uhid-battery-prototype.cpp -o /tmp/logiops-uhid-battery-prototype
```

Run it as root because `/dev/uhid` is usually root-only:

```sh
pkexec modprobe uhid
pkexec /tmp/logiops-uhid-battery-prototype --percent 73 --duration 60
```

While it is running, inspect:

```sh
ls -l /sys/class/power_supply
upower -e
```

The prototype creates a tiny Consumer Control input device so Linux accepts it
through `hid-input`, then exposes battery level as a HID feature report. The
expected kernel-side result is a `hid-*-battery-*` entry under
`/sys/class/power_supply` with `scope=Device` and `capacity` matching the
prototype percentage.
