#!/usr/bin/python3
"""Follow MX Keys Easy-Switch using logid's HostSwitch D-Bus interface."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import logging
from pathlib import Path
import signal
import subprocess
import time

from gi.repository import Gio, GLib, GLibUnix

SERVICE = "pizza.pixl.LogiOps"
DEVICE_IFACE = SERVICE + ".Device"
HOST_IFACE = SERVICE + ".HostSwitch"
DEVICES_PATH = "/pizza/pixl/logiops"
LOG = logging.getLogger("logi-host-switch")


class Controller:
    def __init__(self, config, state_file, backend):
        self.config = config
        self.state_file = Path(state_file)
        self.backend = backend
        self.away = False
        try:
            self.away = bool(json.loads(self.state_file.read_text())["away"])
        except FileNotFoundError:
            pass

    def save(self, away):
        self.state_file.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.state_file.with_suffix(".tmp")
        temporary.write_text(json.dumps({"away": away}) + "\n")
        temporary.replace(self.state_file)
        self.away = away

    def switched(self, previous, target, *, new_link_loss=False):
        # Public config is 1-based; logid's D-Bus indices are 0-based.
        if (previous, target) != (self.config["local_host"] - 1,
                                  self.config["remote_host"] - 1):
            return
        if self.away and not new_link_loss:
            return
        LOG.info("Keyboard HOST%d -> HOST%d", previous + 1, target + 1)
        # Persist before switching inputs so a service restart can restore DP.
        self.save(True)
        if self.config.get("follow_mouse", False):
            try:
                self.backend.send_mouse(target)
            except Exception:
                LOG.exception("Mouse switch failed; continuing with monitor")
        self.backend.set_input(self.config["remote_input"])

    def connected(self):
        if not self.away:
            return
        # In link-state mode the caller has confirmed an active connection to
        # this PC. Querying a just-reconnected keyboard adds avoidable timeouts.
        if (self.config.get("departure_mode", "host_event") != "link_state" and
                self.backend.keyboard_host() != self.config["local_host"] - 1):
            return
        LOG.info("Keyboard returned to HOST%d; restoring DisplayPort",
                 self.config["local_host"])
        self.backend.set_input(self.config["local_input"])
        self.save(False)

    def disconnected(self):
        if self.config.get("departure_mode", "host_event") == "link_state":
            LOG.warning("Keyboard link lost: assuming HOST%d (link_state mode)",
                        self.config["remote_host"])
            # This is a new observed link loss, even if a previous failed DDC
            # operation left a persisted return pending.
            self.switched(self.config["local_host"] - 1, self.config["remote_host"] - 1,
                          new_link_loss=True)


class Backend:
    def __init__(self, config):
        self.config = config
        self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        self.devices = {}
        self.keyboard = None
        self.warned_versions = set()

    def call(self, path, interface, method, params=None):
        return self.bus.call_sync(
            SERVICE, path, interface, method, params, None,
            Gio.DBusCallFlags.NONE, 5000, None).unpack()

    def refresh(self):
        paths, = self.call(DEVICES_PATH, SERVICE + ".Devices", "Enumerate")
        devices = {}
        keyboard = None
        for path in paths:
            try:
                properties, = self.call(path, "org.freedesktop.DBus.Properties",
                                        "GetAll", GLib.Variant("(s)", (DEVICE_IFACE,)))
                devices[path] = properties
                if properties["Name"] == self.config["keyboard"]:
                    if keyboard is not None:
                        raise RuntimeError("Multiple matching keyboards; refusing ambiguous setup")
                    keyboard = path
                    host_properties, = self.call(path, "org.freedesktop.DBus.Properties",
                                                 "GetAll", GLib.Variant("(s)", (HOST_IFACE,)))
                    version = host_properties.get("Version")
                    if (version is not None and version < 2 and
                            self.config.get("departure_mode", "host_event") == "host_event" and
                            properties["Name"] == "MX Keys S" and version not in self.warned_versions):
                        LOG.warning("MX Keys S has CHANGE_HOST v%d; this firmware did not emit "
                                    "target-host notifications in our test. Firmware 81.2.17+ "
                                    "is required for the tested Enhanced Easy-Switch path.", version)
                        self.warned_versions.add(version)
            except GLib.Error as error:
                LOG.debug("Device disappeared during enumeration: %s", error)
        self.devices = devices
        self.keyboard = keyboard

    def keyboard_host(self):
        if not self.keyboard:
            raise RuntimeError("Keyboard is unavailable")
        count, current = self.call(self.keyboard, HOST_IFACE, "GetHostInfo")
        if not 0 <= current < count:
            raise RuntimeError("Invalid current keyboard host")
        return current

    def send_mouse(self, target):
        matches = [path for path, props in self.devices.items()
                   if props["Name"] == self.config["mouse"] and props["Active"]]
        if len(matches) != 1:
            raise RuntimeError("Expected exactly one active " + self.config["mouse"])
        for attempt in range(3):
            try:
                self.call(matches[0], HOST_IFACE, "SetHost", GLib.Variant("(y)", (target,)))
                break
            except GLib.Error:
                # A first HID++ request can time out while an idle device wakes.
                if attempt == 2:
                    raise
                time.sleep(0.15)
        LOG.info("Sent %s to HOST%d", self.config["mouse"], target + 1)

    def set_input(self, value):
        # NVIDIA does not expose a connector/ddc symlink here. Match the hardware
        # adapter name instead of assuming a stable /dev/i2c-N number.
        bus = resolve_bus(self.config["ddc_adapter"])
        command = ["/usr/bin/ddcutil", "--bus", str(bus),
                   "--noverify", "setvcp", "60", value]
        for attempt in range(3):
            try:
                result = subprocess.run(command, capture_output=True, text=True, timeout=10)
                if result.returncode == 0:
                    LOG.info("Monitor input command sent: %s (I2C %d)", value, bus)
                    return
                error = result.stdout.strip() + " " + result.stderr.strip()
            except subprocess.TimeoutExpired:
                error = "ddcutil timed out"
            if attempt < 2:
                time.sleep(1)
        raise RuntimeError("Monitor input switch failed: " + error)


class Daemon:
    def __init__(self, config, state_file):
        self.backend = Backend(config)
        self.controller = Controller(config, state_file, self.backend)
        # DDC and HID++ requests must not block the GLib signal dispatch thread.
        self.worker = ThreadPoolExecutor(max_workers=1)
        self.refresh_pending = False
        self.online = None
        self.subscription = self.backend.bus.signal_subscribe(
            SERVICE, None, None, None, None, Gio.DBusSignalFlags.NONE,
            self.received, None)

    def submit(self, function, *args):
        def run():
            try:
                function(*args)
            except Exception:
                LOG.exception("Event handler failed")
        self.worker.submit(run)

    def received(self, _bus, _sender, path, interface, member, params, _data):
        if interface == HOST_IFACE and member == "Switched":
            self.submit(self.switched, path, *params.unpack())
        elif interface == HOST_IFACE and member == "Connected":
            self.submit(self.connected, path, True)
        elif interface == DEVICE_IFACE and member == "StatusChanged" and params.unpack()[0]:
            self.submit(self.connected, path)
        elif interface == DEVICE_IFACE and member == "StatusChanged":
            self.submit(self.disconnected, path)

    def switched(self, path, previous, target):
        # Resolve names again if the daemon has just started or logid restarted.
        if path not in self.backend.devices:
            self.backend.refresh()
        if path == self.backend.keyboard:
            self.controller.switched(previous, target)

    def connected(self, path, link_event=False):
        if not self.controller.away:
            return
        self.backend.refresh()
        # HostSwitch.Connected is emitted as soon as the link comes back, before
        # logid reapplies remaps and publishes Active=True. The link-state
        # fallback can restore the monitor immediately at that point.
        early_link = (link_event and self.controller.config.get(
            "departure_mode", "host_event") == "link_state")
        if (path == self.backend.keyboard and
                (self.backend.devices[path]["Active"] or early_link)):
            self.controller.connected()

    def disconnected(self, path):
        if self.controller.config.get("departure_mode", "host_event") != "link_state":
            return
        # Ignore very short link glitches; never infer a departure on startup.
        time.sleep(0.4)
        self.backend.refresh()
        if (path == self.backend.keyboard and
                not self.backend.devices[path]["Active"]):
            self.controller.disconnected()

    def refresh(self):
        try:
            self.backend.refresh()
            available = self.backend.keyboard is not None
            if available != self.online:
                LOG.info("Keyboard %s", "discovered" if available else "unavailable")
                self.online = available
            if (self.controller.away and self.backend.keyboard and
                    self.backend.devices[self.backend.keyboard]["Active"]):
                # Also recovers an armed return after logid/service restarts.
                self.controller.connected()
        except GLib.Error as error:
            LOG.debug("Waiting for logid / keyboard: %s", error)
        finally:
            self.refresh_pending = False

    def tick(self):
        if not self.refresh_pending:
            self.refresh_pending = True
            self.submit(self.refresh)
        return GLib.SOURCE_CONTINUE


def resolve_bus(adapter, directory=Path("/sys/class/i2c-dev")):
    matches = [p.parent for p in directory.glob("i2c-*/name")
               if p.read_text().strip() == adapter]
    if len(matches) != 1:
        raise RuntimeError("Expected exactly one DDC adapter named " + adapter)
    return int(matches[0].name[4:])


def load_config(path):
    config = json.loads(Path(path).read_text())
    for key in ("local_host", "remote_host"):
        if type(config[key]) is not int or not 1 <= config[key] <= 3:
            raise ValueError(key + " must be 1, 2 or 3")
    if config["local_host"] == config["remote_host"]:
        raise ValueError("local_host and remote_host must differ")
    if config.get("departure_mode", "host_event") not in ("host_event", "link_state"):
        raise ValueError("departure_mode must be host_event or link_state")
    for key in ("local_input", "remote_input"):
        value = int(config[key], 0)
        if not 0 <= value <= 255:
            raise ValueError("Invalid monitor input: " + key)
    if not isinstance(config["ddc_adapter"], str) or not config["ddc_adapter"]:
        raise ValueError("ddc_adapter must name an I2C hardware adapter")
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/etc/logi-host-switch.json")
    parser.add_argument("--state", default="/var/lib/logi-host-switch/state.json")
    parser.add_argument("--check", action="store_true", help="Validate configuration and exit")
    parser.add_argument("--event", choices=("connected", "disconnected"),
                        help="Handle one native logid connection hook and exit")
    parser.add_argument("--probe", action="store_true", help="Read devices and current hosts")
    parser.add_argument("--test-monitor", action="store_true",
                        help="Select HDMI for three seconds, then restore DisplayPort")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    config = load_config(args.config)
    if args.check:
        LOG.info("Configuration valid")
        return
    if args.event:
        backend = Backend(config)
        if args.event == "disconnected":
            time.sleep(0.4)
        backend.refresh()
        controller = Controller(config, args.state, backend)
        if args.event == "connected":
            controller.connected()
        elif (backend.keyboard and
              backend.devices[backend.keyboard]["Active"]):
            LOG.info("Keyboard already reconnected; ignoring brief link loss")
        else:
            controller.disconnected()
        return
    if args.probe or args.test_monitor:
        backend = Backend(config)
        backend.refresh()
        for path, properties in backend.devices.items():
            try:
                hosts = backend.call(path, HOST_IFACE, "GetHostInfo")
            except GLib.Error as error:
                hosts = str(error)
            LOG.info("%s: %s, Active=%s, hosts=%s", path, properties["Name"],
                     properties["Active"], hosts)
        LOG.info("DDC hardware adapter resolved to I2C %d", resolve_bus(config["ddc_adapter"]))
        if args.test_monitor:
            try:
                backend.set_input(config["remote_input"])
                time.sleep(3)
            finally:
                backend.set_input(config["local_input"])
        return
    LOG.info("Departure detection: %s", config.get("departure_mode", "host_event"))
    daemon = Daemon(config, args.state)
    loop = GLib.MainLoop()
    for number in (signal.SIGTERM, signal.SIGINT):
        GLibUnix.signal_add(GLib.PRIORITY_DEFAULT, number,
                             lambda: (loop.quit(), GLib.SOURCE_REMOVE)[1])
    daemon.tick()
    GLib.timeout_add_seconds(5, daemon.tick)
    try:
        loop.run()
    finally:
        daemon.backend.bus.signal_unsubscribe(daemon.subscription)
        daemon.worker.shutdown(wait=True, cancel_futures=True)


if __name__ == "__main__":
    main()
