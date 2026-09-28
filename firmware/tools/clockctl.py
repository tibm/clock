#!/usr/bin/env python3
"""clockctl -- talk to the clock over BLE from a laptop.        [FIRMWARE.md §8]

The reference client for the app link, and the bench tool until the app exists:

    pip install bleak
    tools/clockctl.py scan                      # find clocks; shows which one is pairable
    tools/clockctl.py shell                     # the CLI, over the air (`help` works)
    tools/clockctl.py run "motion goto 07:15"   # one command, exit status = its Status
    tools/clockctl.py status                    # one decoded snapshot
    tools/clockctl.py status --watch --csv log.csv   # every notification, appended as a row
    tools/clockctl.py tones                     # the sound files on the card
    tools/clockctl.py put birds.wav             # upload one (48 kHz mono 16-bit WAV)
    tools/clockctl.py rm birds.wav              # and delete it

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
import time
import wave
import zlib

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover
    sys.exit("clockctl needs bleak:  pip install bleak")

BASE = "7a3e{:04x}-5c1d-4b8e-9f3a-2c6d1e0b9a41"
SVC, CMD, RSP, STATUS, INFO, BLOB = (BASE.format(i) for i in range(1, 7))
# `blob` ATT errors (app/PROTOCOL.md "Sound files")
ERR_BUSY, ERR_OFFSET, ERR_NO_UPLOAD, ERR_FAILED = 0x80, 0x81, 0x82, 0x83
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
    "audio_playing imu_ok imu_link als_ok als_saturated env_ok env_gas_valid env_heat_stable date_valid"
).split()
MOTION = ["uninit", "homing", "idle", "moving", "fault"]
MODE = ["idle", "bell", "alarm", "clock", "volume", "pairing", "ringing", "snoozed"]
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
    # local = UTC + offset (PROTOCOL.md §5 Time); without a date only the time of day is real
    if d["time_valid"]:
        local = datetime.datetime.fromtimestamp(
            (d["epoch_ms"] + d["tz_off_min"] * 60000) / 1000, datetime.timezone.utc)
        d["time"] = local.strftime("%Y-%m-%d %H:%M:%S" if d["date_valid"] else "%H:%M:%S")
    else:
        d["time"] = None
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
        self.kv: list[tuple[str, str]] = []  # the last run()'s `=` pairs, in order

    async def open(self):
        await self.c.start_notify(RSP, lambda _, data: self.q.put_nowait(bytes(data)))

    async def run(self, line: str, echo=print) -> str:
        rid = self.next_id
        self.next_id = self.next_id % 65535 + 1
        await self.c.write_gatt_char(CMD, f"{rid} {line}".encode(), response=True)
        head, partial = str(rid).encode(), b""
        self.kv = []
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
            if kind == "=":
                k, _, v = text.partition("=")
                self.kv.append((k, v))
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


def att_code(e: Exception) -> int | None:
    """The ATT error code inside a bleak write failure, across backends (best effort)."""
    for attr in ("code", "error_code", "att_error"):
        v = getattr(e, attr, None)
        if isinstance(v, int):
            return v
    msg = str(e).lower()
    for code in (ERR_BUSY, ERR_OFFSET, ERR_NO_UPLOAD, ERR_FAILED):
        if f"0x{code:02x}" in msg or f"code={code}" in msg or f"({code})" in msg:
            return code
    return None


async def cmd_tones(a):
    c = await connect(a.addr)
    try:
        link = Link(c)
        await link.open()
        st = await link.run("storage tones", echo=lambda _: None)
        if st != "ok":
            print(f"[{st}]")
            return 1
        for k, v in link.kv:
            if k == "card":
                total, free = (int(x) for x in v.split("/"))
                print(f"card   {total >> 20} MB, {free >> 20} MB free")
            elif k == "alarm":
                print(f"alarm  {v or '(the beep)'}")
            elif k == "tone":
                size, ms, state, name = v.split("/", 3)
                print(f"  {name:32} {int(size):9} B  {int(ms) / 1000:6.1f} s  {state}")
        return 0
    finally:
        await c.disconnect()


async def cmd_rm(a):
    c = await connect(a.addr)
    try:
        link = Link(c)
        await link.open()
        st = await link.run(f"storage rm {a.name}")
        if st != "ok":
            print(f"[{st}]")
        return 0 if st == "ok" else 1
    finally:
        await c.disconnect()


async def cmd_put(a):
    data = open(a.file, "rb").read()
    name = a.name or os.path.basename(a.file)
    try:  # the clock checks too; this just saves an upload that would be refused at the end
        with wave.open(a.file) as w:
            fmt = (w.getframerate(), w.getnchannels(), w.getsampwidth())
        if fmt != (48000, 1, 2):
            sys.exit(f"{a.file}: {fmt[0]} Hz, {fmt[1]} ch, {8 * fmt[2]}-bit -- the clock plays "
                     "48000 Hz mono 16-bit only:\n  ffmpeg -i in -ac 1 -ar 48000 -c:a pcm_s16le "
                     "-bitexact out.wav")
    except wave.Error as e:
        sys.exit(f"{a.file}: not a WAV ({e})")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    c = await connect(a.addr)
    try:
        link = Link(c)
        await link.open()

        async def begin() -> int | None:
            st = await link.run(f"storage put {name} {len(data)} {crc:08x}", echo=lambda _: None)
            if st != "ok":
                print(f"put refused: [{st}]")
                return None
            return int(dict(link.kv).get("next", "0"))

        off = await begin()
        if off is None:
            return 1
        # One ATT write: MTU - 3, less our 4-byte offset.  Never a long write -- they are
        # slower, and a refused one is harder to recover from.
        chunk = max(20, min(508, (c.mtu_size or 23) - 3 - 4))
        t0, last = time.monotonic(), 0.0
        while off < len(data):
            part = data[off:off + chunk]
            try:
                await c.write_gatt_char(BLOB, struct.pack("<I", off) + part, response=True)
                off += len(part)
            except Exception as e:  # noqa: BLE001 -- bleak's error types differ per backend
                code = att_code(e)
                if code == ERR_BUSY:
                    await asyncio.sleep(0.05)
                    continue
                if code == ERR_OFFSET or code is None:
                    # A lost response, or a backend that hides the code: ask where we are.
                    off = await begin()
                    if off is None:
                        return 1
                    continue
                print(f"blob write refused: ATT 0x{code:02x}")
                return 1
            now = time.monotonic()
            if now - last > 0.5 or off == len(data):
                last = now
                rate = off / max(now - t0, 1e-3) / 1024
                print(f"\r{name}: {off}/{len(data)} B  {100 * off // len(data)}%  "
                      f"{rate:.1f} KB/s", end="", flush=True)
        print()
        st = await link.run("storage put end")
        if st != "ok":
            print(f"[{st}]")
        return 0 if st == "ok" else 1
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
    sub.add_parser("tones", help="list /sd/tones").set_defaults(fn=cmd_tones)
    s = sub.add_parser("put", help="upload a 48 kHz mono 16-bit WAV to /sd/tones")
    s.add_argument("file")
    s.add_argument("--name", help="name on the card (default: the file's)")
    s.set_defaults(fn=cmd_put)
    s = sub.add_parser("rm", help="delete a file from /sd/tones")
    s.add_argument("name")
    s.set_defaults(fn=cmd_rm)
    a = p.parse_args()
    try:
        sys.exit(asyncio.run(a.fn(a)) or 0)
    except KeyboardInterrupt:
        sys.exit(130)


if __name__ == "__main__":
    main()
