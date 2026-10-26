#!/usr/bin/python3
"""Read firmware/features and optionally observe host/link notifications only."""
import argparse
import os
from pathlib import Path
import select
import struct
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--device", default="/dev/hidraw7")
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--listen", type=int, default=0)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--analytics", action="store_true",
                        help="Enable non-diverting analytics reports for host-switch keys")
    modes.add_argument("--disable-analytics", action="store_true",
                       help="Disable the temporary host-key analytics reports")
    args = parser.parse_args()
    fd = os.open(args.device, os.O_RDWR | os.O_NONBLOCK)
    indices = set()
    analytics_index = None

    def receive(timeout):
        if select.select([fd], [], [], timeout)[0]:
            return os.read(fd, 64)
        return b""

    def request(feature, function=0, data=b""):
        address = (function << 4) | 0x0e
        for attempt in range(3):
            packet = bytes((0x11, args.slot, feature, address)) + data
            os.write(fd, packet.ljust(20, b"\0"))
            deadline = time.monotonic() + 1
            while time.monotonic() < deadline:
                report = receive(max(0, deadline - time.monotonic()))
                if len(report) < 7 or report[0] not in (0x10, 0x11) or report[1] != args.slot:
                    continue
                if report[2:4] == bytes((feature, address)):
                    return report[4:]
                if report[2] == 0xff and report[3:5] == bytes((feature, address)):
                    raise RuntimeError("HID++ error " + report.hex(" "))
        raise TimeoutError(f"Feature {feature:02x} function {function} did not answer")

    try:
        for feature_id in (0x0003, 0x1814, 0x1b04, 0x1d4b):
            info = request(0, data=feature_id.to_bytes(2, "big"))
            index, flags, version = info[:3]
            print(f"FEATURE {feature_id:04x}: index={index:02x} version={version} flags={flags:02x}", flush=True)
            indices.add(index)
            if feature_id == 0x1b04 and index:
                analytics_index = index
                count = request(index)[0]
                print("CONTROL COUNT", count, flush=True)
                for number in range(count):
                    control = request(index, 1, bytes((number,)))
                    cid = int.from_bytes(control[:2], "big")
                    task = int.from_bytes(control[2:4], "big")
                    print("CONTROL", number, control.hex(" "), flush=True)
                    if task not in (0xae, 0xaf, 0xb0):
                        continue
                    print("HOST KEY", f"{cid:04x}", "info:", control.hex(" "), flush=True)
                    reporting = request(index, 2, cid.to_bytes(2, "big"))
                    print("REPORTING", reporting.hex(" "), flush=True)
                    if args.analytics or args.disable_analytics:
                        # Low flags = 0 (no diversion changes), remap = 0 (unchanged),
                        # high flags = analytics enabled + analytics-valid.
                        flags = 3 if args.analytics else 2
                        response = request(index, 3, cid.to_bytes(2, "big") + b"\0\0\0" + bytes((flags,)))
                        print("ANALYTICS", response.hex(" "), flush=True)
                        print("READBACK", request(index, 2, cid.to_bytes(2, "big")).hex(" "), flush=True)
            if feature_id == 0x0003 and index:
                count = request(index)[0]
                for entity in range(count):
                    fw = request(index, 1, bytes((entity,)))
                    if fw[0] & 0x0f in (0, 1):
                        name, major, minor, build = struct.unpack("!3sBBH", fw[1:8])
                        print(f"FIRMWARE {name.decode()}: {major:02x}.{minor:02x}.B{build:04x}", flush=True)
        print("READY: observing host/link events (no key reports)", flush=True)
        deadline = time.monotonic() + args.listen
        while time.monotonic() < deadline:
            report = receive(min(1, max(0, deadline - time.monotonic())))
            if (len(report) >= 7 and report[0] in (0x10, 0x11) and report[1] == args.slot and
                    (report[2] in (0x40, 0x41) or report[3] & 0x0f == 0)):
                # Do not log reprogrammable-key events (which may include keys).
                if report[2] == analytics_index and not args.analytics:
                    continue
                print(time.strftime("%H:%M:%S"), report.hex(" "), flush=True)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
