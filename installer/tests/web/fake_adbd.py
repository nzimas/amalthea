#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A stand-in for the stock Brick's ADB daemon, for testing docs/web-install.html without a Brick.

It speaks the ADB wire protocol as an old adbd does (protocol version 0x01000000, 4 KiB payloads, checksums,
no features: no shell v2, `exec:` refused, sync v1 only, no exit codes) and carries out every request on the
simulated Brick of installer/tests (the Docker container made by sim_setup.sh) through mock_adb.py, which
also simulates the reboot into Amalthea's RAM installer. After a reboot the "device" is gone for --boot-seconds.

One port serves three things:
  ws://127.0.0.1:PORT/adb   the ADB transport for the page (?sim=ws://127.0.0.1:PORT/adb stands in for WebUSB)
  http://127.0.0.1:PORT/     docs/ (the site), and /payload.zip: the installer archive given with --zip
and with --tcp PORT2 a plain ADB TCP listener, so the real adb client can check this emulation
(adb connect 127.0.0.1:PORT2).
"""
import argparse, asyncio, base64, hashlib, os, struct, subprocess, sys, tempfile, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
MOCK = str(HERE.parent / "mock_adb.py")
C = os.environ.get("SIM_CONTAINER", "amalthea-sim")
CNXN, AUTH, OPEN, OKAY, CLSE, WRTE = (struct.unpack("<I", s)[0] for s in (b"CNXN", b"AUTH", b"OPEN", b"OKAY", b"CLSE", b"WRTE"))
NAMES = {CNXN: "CNXN", AUTH: "AUTH", OPEN: "OPEN", OKAY: "OKAY", CLSE: "CLSE", WRTE: "WRTE"}
VERSION, MAXDATA = 0x01000000, 4096
ARGS = None
offline_until = 0.0
LOG = lambda *a: print("[adbd]", *a, file=sys.stderr, flush=True)


def checksum(b):
    return sum(b) & 0xFFFFFFFF


def mock(*args, **kw):
    return subprocess.run([sys.executable, MOCK, *args], capture_output=True, **kw)


def sim_state():
    r = subprocess.run(["docker", "exec", C, "cat", "/sim/state"], capture_output=True, text=True)
    return r.stdout.strip()


class Transport:
    """one host connection: packets in (bytes), packets out (via send)"""

    def __init__(self, send, close):
        self._send, self._close = send, close
        self.buf = b""
        self.online = False
        self.streams = {}          # local id -> Stream
        self.next_id = 1
        self.auth_tries = 0
        self.dead = False

    async def packet(self, cmd, a0, a1, data=b""):
        if self.dead:
            return
        hdr = struct.pack("<6I", cmd, a0, a1, len(data), checksum(data), cmd ^ 0xFFFFFFFF)
        await self._send(hdr + data)

    async def feed(self, data):
        self.buf += data
        while len(self.buf) >= 24:
            cmd, a0, a1, n, ck, magic = struct.unpack("<6I", self.buf[:24])
            if len(self.buf) < 24 + n:
                return
            payload, self.buf = self.buf[24:24 + n], self.buf[24 + n:]
            if magic != cmd ^ 0xFFFFFFFF:
                LOG("bad magic, dropping connection"); await self.kill(); return
            if checksum(payload) != ck:            # an old adbd checks every payload's sum
                LOG(f"bad checksum on {NAMES.get(cmd, cmd)}: an old adbd would drop this packet"); continue
            await self.handle(cmd, a0, a1, payload)

    async def handle(self, cmd, a0, a1, payload):
        if cmd == CNXN:
            LOG("CNXN", payload[:80])
            if ARGS.auth:
                await self.packet(AUTH, 1, 0, os.urandom(20))       # TOKEN
            else:
                await self.connected()
        elif cmd == AUTH:
            if a0 == 2 and self.auth_tries == 0:                 # SIGNATURE: refuse the first, as for an unknown key
                self.auth_tries += 1
                await self.packet(AUTH, 1, 0, os.urandom(20))
            else:                                                # SIGNATURE again, or RSAPUBLICKEY: "user accepted"
                LOG("AUTH accepted", "signature" if a0 == 2 else "public key")
                await self.connected()
        elif not self.online:
            LOG("packet before connection", NAMES.get(cmd, cmd))
        elif cmd == OPEN:
            await self.open(a0, payload.rstrip(b"\0").decode())
        elif cmd == OKAY:
            s = self.streams.get(a1)
            if s:
                s.remote = a0
                s.acked.set()
        elif cmd == WRTE:
            s = self.streams.get(a1)
            if s:
                await self.packet(OKAY, s.local, s.remote)
                await s.incoming(payload)
        elif cmd == CLSE:
            s = self.streams.pop(a1, None)
            if s:
                s.closed = True
                s.acked.set()

    async def connected(self):
        self.online = True
        await self.packet(CNXN, VERSION, MAXDATA, b"device::")

    async def open(self, remote, service):
        LOG("OPEN", service)
        lid = self.next_id; self.next_id += 1
        if service.startswith("shell:"):
            s = ShellStream(self, lid, remote, service[6:])
        elif service == "sync:":
            s = SyncStream(self, lid, remote)
        else:                                    # exec:, shell,v2:, reboot: ... are not on this adbd
            LOG("refused", service)
            await self.packet(CLSE, 0, remote)
            return
        self.streams[lid] = s
        await self.packet(OKAY, lid, remote)
        s.task = asyncio.ensure_future(s.run())

    async def kill(self):
        self.dead = True
        for s in self.streams.values():
            s.closed = True; s.acked.set()
        await self._close()


class Stream:
    def __init__(self, t, local, remote):
        self.t, self.local, self.remote = t, local, remote
        self.acked = asyncio.Event(); self.acked.set()
        self.closed = False
        self.inq = asyncio.Queue()

    async def write(self, data):
        for i in range(0, len(data), MAXDATA):
            await self.acked.wait()
            if self.closed:
                raise ConnectionError("closed by host")
            self.acked.clear()
            await self.t.packet(WRTE, self.local, self.remote, data[i:i + MAXDATA])

    async def incoming(self, data):
        await self.inq.put(data)

    async def close(self):
        await self.acked.wait()
        if not self.closed:
            self.closed = True
            self.t.streams.pop(self.local, None)
            await self.t.packet(CLSE, self.local, self.remote)


class ShellStream(Stream):
    def __init__(self, t, local, remote, cmd):
        super().__init__(t, local, remote); self.cmd = cmd

    async def run(self):
        global offline_until
        loop = asyncio.get_running_loop()
        if self.cmd.strip().startswith("reboot"):
            # the real daemon dies with the system: no output, no CLSE, the transport just goes away
            await loop.run_in_executor(None, lambda: mock("shell", self.cmd))
            LOG("rebooted:", sim_state())
            offline_until = time.time() + ARGS.boot_seconds
            await self.t.kill()
            return
        r = await loop.run_in_executor(None, lambda: mock("shell", self.cmd))
        try:
            await self.write(r.stdout)     # (mock_adb already turns \n into \r\n, as the pty does)
            await self.close()
        except ConnectionError:
            pass


class SyncStream(Stream):
    """sync v1: STAT, SEND, RECV, QUIT (LIST is not used by the page)"""

    async def read(self, n):
        while len(self.pending) < n:
            self.pending += await self.inq.get()
        b, self.pending = self.pending[:n], self.pending[n:]
        return b

    async def run(self):
        self.pending = b""
        loop = asyncio.get_running_loop()
        try:
            while True:
                rid, n = struct.unpack("<4sI", await self.read(8))
                arg = await self.read(n) if rid != b"DONE" else b""
                if rid == b"QUIT":
                    await self.close(); return
                if rid == b"STAT":
                    p = arg.decode()
                    r = await loop.run_in_executor(None, lambda: subprocess.run(
                        ["docker", "exec", C, "stat", "-c", "%f %s %Y", p], capture_output=True, text=True))
                    mode, size, mtime = (int(r.stdout.split()[0], 16), *map(int, r.stdout.split()[1:])) if r.returncode == 0 else (0, 0, 0)
                    await self.write(b"STAT" + struct.pack("<III", mode, size, mtime))
                elif rid == b"RECV":
                    await self.recv(arg.decode())
                elif rid == b"SEND":
                    await self.send(arg.decode())
                else:
                    await self.fail(f"unknown sync request {rid!r}"); await self.close(); return
        except ConnectionError:
            pass

    async def fail(self, msg):
        m = msg.encode(); await self.write(b"FAIL" + struct.pack("<I", len(m)) + m)

    async def recv(self, path):
        LOG("pull", path)
        p = await asyncio.create_subprocess_exec("docker", "exec", C, "sh", "-c", f'test -f "{path}" && cat "{path}"',
                                                 stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL)
        sent = 0
        while chunk := await p.stdout.read(65536):
            await self.write(b"DATA" + struct.pack("<I", len(chunk)) + chunk); sent += len(chunk)
        if await p.wait() != 0:
            await self.fail("No such file or directory"); return
        await self.write(b"DONE" + struct.pack("<I", 0))
        LOG("pulled", path, sent, "bytes")

    async def send(self, spec):
        path, _, mode = spec.rpartition(",")
        mode = int(mode) & 0o7777
        with tempfile.NamedTemporaryFile(delete=False) as f:
            tmp = f.name
            while True:
                rid, n = struct.unpack("<4sI", await self.read(8))
                if rid == b"DONE":
                    break
                if rid != b"DATA":
                    await self.fail("expected DATA"); return
                f.write(await self.read(n))
        loop = asyncio.get_running_loop()

        def put():
            d = os.path.dirname(path)
            subprocess.run(["docker", "exec", C, "mkdir", "-p", d], capture_output=True)   # adbd makes parents
            r = mock("push", tmp, path)
            subprocess.run(["docker", "exec", C, "chmod", oct(mode)[2:], path], capture_output=True)
            os.unlink(tmp)
            return r.returncode
        rc = await loop.run_in_executor(None, put)
        LOG("push", path, oct(mode), "ok" if rc == 0 else "FAILED")
        if rc == 0:
            await self.write(b"OKAY" + struct.pack("<I", 0))
        else:
            await self.fail("push failed")


def available():
    return time.time() >= offline_until and sim_state() not in ("bricked", "")


# --- WebSocket + static files on one port ---------------------------------------------------------------
TYPES = {".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml",
         ".png": "image/png", ".zip": "application/zip", ".txt": "text/plain", ".json": "application/json"}


async def http(reader, writer):
    try:
        head = await reader.readuntil(b"\r\n\r\n")
    except Exception:
        writer.close(); return
    lines = head.decode("latin-1").split("\r\n")
    method, path, _ = lines[0].split(" ", 2)
    hdr = {k.lower(): v.strip() for k, v in (l.split(":", 1) for l in lines[1:] if ":" in l)}
    path = path.split("?", 1)[0]
    if method == "POST" and path == "/result":           # the page's report in &auto mode (run.sh waits for it)
        body = await reader.readexactly(int(hdr.get("content-length", 0)))
        if ARGS.result:
            Path(ARGS.result).write_bytes(body)
        LOG("result:", body[:200])
        writer.write(b"HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n"); await writer.drain(); writer.close(); return
    if hdr.get("upgrade", "").lower() == "websocket" and path == "/adb":
        if not available():
            writer.write(b"HTTP/1.1 503 Booting\r\nContent-Length: 0\r\n\r\n"); await writer.drain(); writer.close(); return
        key = base64.b64encode(hashlib.sha1((hdr["sec-websocket-key"] + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        writer.write(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     b"Sec-WebSocket-Accept: " + key + b"\r\n\r\n")
        await writer.drain()
        await websocket(reader, writer)
        return
    if path == "/payload.zip" and ARGS.zip:
        f = Path(ARGS.zip)
    else:
        f = (ARGS.docs / path.lstrip("/")).resolve()
        if f.is_dir():
            f = f / "index.html"
        if ARGS.docs.resolve() not in f.parents and f != ARGS.docs.resolve():
            f = Path("/nonexistent")
    if not f.is_file():
        writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
    else:
        body = f.read_bytes()
        writer.write(f"HTTP/1.1 200 OK\r\nContent-Type: {TYPES.get(f.suffix, 'application/octet-stream')}\r\n"
                     f"Content-Length: {len(body)}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n".encode() + body)
    await writer.drain(); writer.close()


async def websocket(reader, writer):
    lock = asyncio.Lock()

    async def send(b):
        n = len(b)
        h = bytes([0x82]) + (bytes([n]) if n < 126 else bytes([126]) + struct.pack(">H", n) if n < 65536
                             else bytes([127]) + struct.pack(">Q", n))
        async with lock:
            writer.write(h + b); await writer.drain()

    async def close():
        try:
            writer.close()
        except Exception:
            pass
    t = Transport(send, close)
    LOG("host connected (websocket)")
    try:
        while not t.dead:
            b0, b1 = await reader.readexactly(2)
            n = b1 & 0x7F
            if n == 126: n = struct.unpack(">H", await reader.readexactly(2))[0]
            elif n == 127: n = struct.unpack(">Q", await reader.readexactly(8))[0]
            mask = await reader.readexactly(4) if b1 & 0x80 else b"\0\0\0\0"
            data = bytearray(await reader.readexactly(n))
            for i in range(n):
                data[i] ^= mask[i & 3]
            op = b0 & 0x0F
            if op == 8:
                break
            if op == 9:
                continue
            await t.feed(bytes(data))
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    LOG("host disconnected")
    await t.kill()


async def tcp(reader, writer):
    if not available():
        writer.close(); return

    async def send(b):
        writer.write(b); await writer.drain()

    async def close():
        writer.close()
    t = Transport(send, close)
    LOG("host connected (tcp)")
    try:
        while not t.dead and (d := await reader.read(65536)):
            await t.feed(d)
    except ConnectionError:
        pass
    await t.kill()


async def main():
    await asyncio.start_server(http, "127.0.0.1", ARGS.port)
    if ARGS.tcp:
        await asyncio.start_server(tcp, "127.0.0.1", ARGS.tcp)
    LOG(f"listening: http/ws {ARGS.port}" + (f", adb tcp {ARGS.tcp}" if ARGS.tcp else ""))
    await asyncio.Event().wait()


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--tcp", type=int, default=0)
    ap.add_argument("--docs", type=Path, default=HERE.parents[2] / "docs")
    ap.add_argument("--zip", default="")
    ap.add_argument("--result", default="", help="write the page's /result report here")
    ap.add_argument("--auth", action="store_true", help="ask for ADB authentication (refuse the first signature)")
    ap.add_argument("--boot-seconds", type=float, default=12)
    ARGS = ap.parse_args()
    asyncio.run(main())
