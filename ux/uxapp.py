#!/usr/bin/env python3
"""ux -- a window onto clocksim.

Serves ux/web/ and bridges the browser's WebSocket to clocksim's line protocol on
127.0.0.1:4747.  Standard library only, on purpose: this is a dev tool that has to still
work in two years without a lockfile.

    ./build/host-dev/apps/clocksim/clocksim     # terminal 1 -- keeps its CLI
    python3 ux/uxapp.py                         # terminal 2 -- opens the browser

The bridge is a pipe and nothing more.  It does not parse frames, does not hold state and
does not know what a clock is: every byte the page sends is a CLI line, every byte it
receives is one of clocksim's JSON frames.  Anything the page can make the clock do, you can
do by typing the same line into terminal 1.
"""

import argparse
import base64
import hashlib
import http.server
import json
import os
import socket
import socketserver
import struct
import sys
import threading
import webbrowser

UX_DIR = os.path.dirname(os.path.abspath(__file__))
WS_GUID = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_TEXT, OP_CLOSE, OP_PING, OP_PONG = 0x1, 0x8, 0x9, 0xA


def ws_frame(payload: bytes, opcode: int = OP_TEXT) -> bytes:
    """Server -> client: never masked (RFC 6455 §5.1)."""
    head = bytearray([0x80 | opcode])
    n = len(payload)
    if n < 126:
        head.append(n)
    elif n < 65536:
        head.append(126)
        head += struct.pack("!H", n)
    else:
        head.append(127)
        head += struct.pack("!Q", n)
    return bytes(head) + payload


def ws_read(rfile):
    """Client -> server: always masked.  Returns (opcode, payload) or (None, None) at EOF."""
    b = rfile.read(2)
    if len(b) < 2:
        return None, None
    opcode = b[0] & 0x0F
    masked = b[1] & 0x80
    n = b[1] & 0x7F
    if n == 126:
        n = struct.unpack("!H", rfile.read(2))[0]
    elif n == 127:
        n = struct.unpack("!Q", rfile.read(8))[0]
    mask = rfile.read(4) if masked else b"\0\0\0\0"
    data = rfile.read(n)
    if len(data) < n:
        return None, None
    if masked:
        data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
    return opcode, data


class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    sim_addr = ("127.0.0.1", 4747)

    def __init__(self, *a, **kw):
        super().__init__(*a, directory=UX_DIR, **kw)

    def log_message(self, fmt, *args):  # one line per request is noise here
        pass

    def end_headers(self):
        # You will be editing app.js and reloading all afternoon.  A cached stylesheet that
        # needs a cmd-shift-R to shift is a bug report waiting to happen.
        if self.path.split("?")[0] != "/ws":
            self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def translate_path(self, path):
        if path.split("?")[0] in ("/", "/index.html"):
            path = "/web/index.html"
        return super().translate_path(path)

    def do_GET(self):
        if self.path.split("?")[0] == "/ws":
            return self.serve_ws()
        return super().do_GET()

    # ---- the pipe --------------------------------------------------------------------
    def serve_ws(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key:
            self.send_error(400, "not a WebSocket handshake")
            return
        accept = base64.b64encode(hashlib.sha1(key.encode() + WS_GUID).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.close_connection = True

        send_lock = threading.Lock()

        def send(payload: bytes, opcode: int = OP_TEXT):
            with send_lock:
                self.wfile.write(ws_frame(payload, opcode))

        try:
            sim = socket.create_connection(self.sim_addr, timeout=5)
        except OSError as exc:
            # The page shows this rather than an empty dial, so the answer to "why is
            # nothing happening" is on screen instead of in a terminal.
            send(json.dumps({"t": "offline", "msg": str(exc)}).encode())
            send(b"", OP_CLOSE)
            return

        sim.settimeout(None)
        stop = threading.Event()

        def pump_sim_to_browser():
            try:
                with sim.makefile("rb") as f:
                    for line in f:
                        if stop.is_set():
                            break
                        line = line.rstrip(b"\r\n")
                        if line:
                            send(line)
            except OSError:
                pass
            finally:
                stop.set()
                try:
                    send(json.dumps({"t": "offline", "msg": "clocksim closed"}).encode())
                    send(b"", OP_CLOSE)
                except OSError:
                    pass

        pump = threading.Thread(target=pump_sim_to_browser, daemon=True)
        pump.start()

        try:
            while not stop.is_set():
                opcode, data = ws_read(self.rfile)
                if opcode is None or opcode == OP_CLOSE:
                    break
                if opcode == OP_PING:
                    send(data, OP_PONG)
                elif opcode == OP_TEXT:
                    for line in data.split(b"\n"):
                        if line.strip():
                            sim.sendall(line.strip() + b"\n")
        except OSError:
            pass
        finally:
            stop.set()
            try:
                sim.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sim.close()
            pump.join(timeout=1)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def server_bind(self):
        # HTTPServer.server_bind() calls socket.getfqdn() to fill in server_name, and on
        # macOS that is a reverse lookup for 127.0.0.1 which can block for the better part
        # of ten seconds.  We already know what it is called.
        socketserver.TCPServer.server_bind(self)
        self.server_name = "localhost"
        self.server_port = self.server_address[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--sim-host", default="127.0.0.1")
    ap.add_argument("--sim-port", type=int, default=4747, help="clocksim --ui-port")
    ap.add_argument("--http-port", type=int, default=8787)
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()

    Handler.sim_addr = (args.sim_host, args.sim_port)

    try:
        socket.create_connection(Handler.sim_addr, timeout=0.5).close()
        where = "attached"
    except OSError:
        where = "NOT running -- start it and reload the page"
    print(f"clocksim on {args.sim_host}:{args.sim_port}: {where}")

    url = f"http://127.0.0.1:{args.http_port}/"
    srv = Server(("127.0.0.1", args.http_port), Handler)
    print(f"ux on {url}   (ctrl-c to stop)", flush=True)
    if not args.no_browser:
        threading.Timer(0.4, webbrowser.open, [url]).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        srv.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
