#!/usr/bin/env python3
"""clockctl -- talk to the clock over BLE from a laptop.        [FIRMWARE.md §8]

The reference client for the app link, and the bench tool until the app exists:

    pip install bleak
    tools/clockctl.py scan                      # find clocks; shows which one is pairable
    tools/clockctl.py shell                     # the CLI, over the air (`help` works)
    tools/clockctl.py run "motion goto 07:15"   # one command, exit status = its Status
    tools/clockctl.py status                    # one decoded snapshot
    tools/clockctl.py status --watch --csv log.csv   # every notification, appended as a row

Pairing: hold the knob 10 s (five pixels breathe blue), or `net ble pair` on the console,
then connect.  macOS pairs on first access to an encrypted characteristic (accept the system
prompt); Linux/Windows pair via bleak's pair().  After that the bond is remembered on both
sides and no window is needed.

The snapshot layout below mirrors components/transport/src/snapshot.cpp -- test_net checks
the C++ side byte for byte; if you change one, change both.
"""

import argparse
import asyncio
import csv
import datetime
import os
import struct
import sys

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover
    sys.exit("clockctl needs bleak:  pip install bleak")

BASE = "7a3e{:04x}-5c1d-4b8e-9f3a-2c6d1e0b9a41"
SVC, CMD, RSP, STATUS, INFO = (BASE.format(i) for i in range(1, 6))
COMPANY = 0xFFFF

# ---- snapshot, schema 1 ------------------------------------------------------------------
LAYOUT = "<BBHIqhBBIIIIHBBhHHIHfH3h3hH6BHIHh8B28siBBbB"
assert struct.calcsize(LAYOUT) == 132
FIELDS = (
    "schema size seq uptime_s epoch_ms tz_off_min reset_reason clk_src flags fw_id "
    "heap_free heap_min vbat_mv soc_pct vbat_src temp_cdeg rh_cpct press_dhpa gas_ohms "
    "env_age_s lux als_age_s gx gy gz yaw pitch roll taps motion_state dial_tick hand_h "
    "hand_m target_h target_m opto motion_faults trims last_trim ui_mode volume alarm_h "
    "alarm_m brightness wake_warm wake_cool ble_state px knob_count bonds wifi_state "
    "wifi_rssi reserved"
).split()
FLAGS = (
    "time_valid time_follow tz_set net_provisioned net_synced net_locked radio_off "
    "ble_connected ble_secure ble_pairing power_ok plugged charging charge_fault full_charge "
    "batt_low homed motor_powered knob_pressed knob_input alarm_armed amp_active "
    "audio_playing imu_ok imu_link als_ok als_saturated env_ok env_gas_valid env_heat_stable"
).split()
MOTION = ["uninit", "homing", "idle", "moving", "fault"]
MODE = ["idle", "bell", "alarm", "clock", "volume", "pairing"]
BLE = ["off", "idle", "pairing", "connected", "secure"]
PIXELS = ["dial0", "dial1", "bell", "alarm", "clock", "vol", "batt"]


def decode(buf: bytes) -> dict:
    if len(buf) < 132 or buf[0] != 1:
        raise ValueError(f"not a schema-1 snapshot ({len(buf)} bytes, schema {buf[:1].hex()})")
    d = dict(zip(FIELDS, struct.unpack_from(LAYOUT, buf)))
    for i, name in enumerate(FLAGS):
        d[name] = bool(d["flags"] >> i & 1)
    # physical units -- what a plot wants
    d["temp_c"] = d["temp_cdeg"] / 100 if d["env_ok"] else None
    d["rh_pct"] = d["rh_cpct"] / 100 if d["env_ok"] else None
    d["press_hpa"] = d["press_dhpa"] / 10 if d["env_ok"] else None
    d["lux"] = d["lux"] if d["als_ok"] else None
    d["soc_pct"] = None if d["soc_pct"] == 0xFF or not d["power_ok"] else d["soc_pct"]
    d["opto"] = d["opto"] / 65535
    d["time"] = (
        datetime.datetime.fromtimestamp(d["epoch_ms"] / 1000, datetime.timezone.utc).isoformat()
        if d["time_valid"] else None
    )
    px = d.pop("px")
    for i, name in enumerate(PIXELS):
        d[f"px_{name}"] = px[4 * i:4 * i + 4].hex()
    return d


def show(d: dict) -> None:
    on = [f for f in FLAGS if d[f]]
    print(f"#{d['seq']} up {d['uptime_s']} s  fw {d['fw_id']:08x}  time {d['time'] or 'not set'}")
    print(f"  power  {d['vbat_mv']} mV  soc {d['soc_pct']}  "
          f"{'plugged' if d['plugged'] else 'battery'}{'  charging' if d['charging'] else ''}")
    print(f"  room   {d['temp_c']} C  {d['rh_pct']} %RH  {d['press_hpa']} hPa  "
          f"gas {d['gas_ohms']}  (age {d['env_age_s']} s)   light {d['lux']} lux")
    print(f"  imu    g ({d['gx']/1000:.3f}, {d['gy']/1000:.3f}, {d['gz']/1000:.3f})  taps {d['taps']}")
    print(f"  hands  {MOTION[d['motion_state']] if d['motion_state'] < 5 else '?'}  "
          f"{d['hand_h']:02}:{d['hand_m']:02} -> {d['target_h']:02}:{d['target_m']:02}  "
          f"opto {d['opto']:.3f}")
    print(f"  ui     {MODE[d['ui_mode']] if d['ui_mode'] < 6 else '?'}  vol {d['volume']}%  "
          f"alarm {d['alarm_h']:02}:{d['alarm_m']:02}  knob {d['knob_count']}")
    print(f"  radio  ble {BLE[d['ble_state']] if d['ble_state'] < 5 else '?'}  bonds {d['bonds']}")
    print(f"  flags  {' '.join(on)}")


# ---- connection --------------------------------------------------------------------------
async def find(addr: str | None, timeout: float = 6.0):
    if addr:
        return addr
    devs = await BleakScanner.discover(timeout=timeout, service_uuids=[SVC], return_adv=True)
    if not devs:
        sys.exit("no clock found (is the radio toggle on?)")
    # Prefer one advertising an open pairing window.
    best = sorted(devs.values(), key=lambda da: -pairable(da[1]))[0]
    return best[0]


def pairable(adv) -> int:
    m = adv.manufacturer_data.get(COMPANY, b"")
    return 1 if m[:1] and m[0] & 1 else 0


async def connect(addr: str | None) -> BleakClient:
    dev = await find(addr)
    c = BleakClient(dev)
    await c.connect()
    try:
        await c.pair()  # a no-op / NotImplemented on macOS, which pairs on first access
    except (NotImplementedError, Exception):  # noqa: BLE001 -- bleak raises backend-specific types
        pass
    return c


class Link:
    """One request at a time over cmd/rsp, reassembling `+` fragments."""

    def __init__(self, client: BleakClient):
        self.c = client
        self.q: asyncio.Queue[bytes] = asyncio.Queue()
        self.next_id = 1

    async def open(self):
        await self.c.start_notify(RSP, lambda _, data: self.q.put_nowait(bytes(data)))

    async def run(self, line: str, echo=print) -> str:
        rid = self.next_id
        self.next_id = self.next_id % 65535 + 1
        await self.c.write_gatt_char(CMD, f"{rid} {line}".encode(), response=True)
        head, partial = str(rid).encode(), b""
        while True:
            f = await asyncio.wait_for(self.q.get(), timeout=130)
            if not f.startswith(head) or len(f) <= len(head):
                continue
            kind, text = chr(f[len(head)]), f[len(head) + 1:]
            if kind == "+":
                partial += text
                continue
            text, partial = (partial + text).decode(errors="replace"), b""
            if kind == "$":
                return text
            echo(text)


async def cmd_scan(a):
    devs = await BleakScanner.discover(timeout=a.timeout, service_uuids=[SVC], return_adv=True)
    if not devs:
        print("no clock found")
    for dev, adv in devs.values():
        print(f"{dev.address}  {adv.local_name or dev.name or '?':16}  rssi {adv.rssi:4}  "
              f"{'PAIRING' if pairable(adv) else ''}")


async def cmd_run(a):
    c = await connect(a.addr)
    try:
        link = Link(c)
        await link.open()
        st = await link.run(" ".join(a.line))
        if st != "ok":
            print(f"[{st}]")
        return 0 if st == "ok" else 1
    finally:
        await c.disconnect()


async def cmd_shell(a):
    c = await connect(a.addr)
    try:
        link = Link(c)
        await link.open()
        info = (await c.read_gatt_char(INFO)).decode(errors="replace")
        print(f"connected: {info}\n`help` lists the groups, ^D quits")
        loop = asyncio.get_running_loop()
        while True:
            line = await loop.run_in_executor(None, lambda: input("clock(ble)> "))
            if not line.strip():
                continue
            st = await link.run(line)
            if st != "ok":
                print(f"[{st}]")
    except (EOFError, KeyboardInterrupt):
        print()
    finally:
        await c.disconnect()


async def cmd_status(a):
    c = await connect(a.addr)
    try:
        if not a.watch:
            d = decode(bytes(await c.read_gatt_char(STATUS)))
            show(d)
            return 0
        writer, fh = None, None
        if a.csv:
            new = not os.path.exists(a.csv)
            fh = open(a.csv, "a", newline="")

        def on_status(_, data):
            nonlocal writer
            d = decode(bytes(data))
            d["host_time"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            if fh:
                if writer is None:
                    writer = csv.DictWriter(fh, fieldnames=list(d))
                    if new:
                        writer.writeheader()
                writer.writerow(d)
                fh.flush()
            show(d)

        if a.period:
            link = Link(c)
            await link.open()
            await link.run(f"net ble period {a.period}", echo=lambda _: None)
        await c.start_notify(STATUS, on_status)
        print("watching; ^C stops")
        while True:
            await asyncio.sleep(3600)
    except KeyboardInterrupt:
        return 0
    finally:
        await c.disconnect()


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--addr", help="device address/UUID (default: scan for the first clock)")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan")
    s.add_argument("--timeout", type=float, default=5.0)
    s.set_defaults(fn=cmd_scan)
    s = sub.add_parser("run")
    s.add_argument("line", nargs="+")
    s.set_defaults(fn=cmd_run)
    sub.add_parser("shell").set_defaults(fn=cmd_shell)
    s = sub.add_parser("status")
    s.add_argument("--watch", action="store_true", help="print every notification")
    s.add_argument("--csv", help="append each snapshot as a CSV row (implies nothing else)")
    s.add_argument("--period", type=int, help="set the notify cadence first, ms")
    s.set_defaults(fn=cmd_status)
    a = p.parse_args()
    try:
        sys.exit(asyncio.run(a.fn(a)) or 0)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
