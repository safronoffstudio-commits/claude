#!/usr/bin/env python3
"""Автотесты SamOS: запускает QEMU без окна, «нажимает пальцем» через QMP
(виртуальный сенсорный планшет) и снимает скриншоты экрана."""
import json, os, socket, subprocess, time
from PIL import Image

W, H = 540, 1170

class Emu:
    def __init__(self, kernel, workdir, extra=()):
        self.workdir = workdir
        os.makedirs(workdir, exist_ok=True)
        self.sock_path = os.path.join(workdir, "qmp.sock")
        if os.path.exists(self.sock_path):
            os.unlink(self.sock_path)
        self.serial = os.path.join(workdir, "serial.log")
        cmd = ["qemu-system-aarch64", "-M", "virt,gic-version=2", "-cpu", "cortex-a72",
               "-m", "512M", "-global", "virtio-mmio.force-legacy=false",
               "-device", "ramfb", "-device", "virtio-tablet-device",
               "-device", "virtio-keyboard-device", "-rtc", "base=utc",
               "-display", "none", "-serial", "file:" + self.serial,
               "-qmp", "unix:%s,server,nowait" % self.sock_path,
               "-kernel", kernel, *extra]
        self.proc = subprocess.Popen(cmd)
        for _ in range(100):
            if os.path.exists(self.sock_path):
                break
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX)
        self.s.connect(self.sock_path)
        self.f = self.s.makefile("rw")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        msg = {"execute": name}
        if args:
            msg["arguments"] = args
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                if "error" in r:
                    raise RuntimeError(r["error"])
                return r["return"]

    def _abs(self, x, y):
        return [{"type": "abs", "data": {"axis": "x", "value": int((x + 0.5) * 32768 / W)}},
                {"type": "abs", "data": {"axis": "y", "value": int((y + 0.5) * 32768 / H)}}]

    def _btn(self, down):
        return [{"type": "btn", "data": {"down": down, "button": "left"}}]

    def tap(self, x, y, wait=0.6):
        self.cmd("input-send-event", events=self._abs(x, y))
        time.sleep(0.05)
        self.cmd("input-send-event", events=self._btn(True))
        time.sleep(0.08)
        self.cmd("input-send-event", events=self._btn(False))
        time.sleep(wait)

    def drag(self, pts, step_delay=0.03, wait=0.8):
        self.cmd("input-send-event", events=self._abs(*pts[0]))
        time.sleep(0.05)
        self.cmd("input-send-event", events=self._btn(True))
        for p in pts[1:]:
            time.sleep(step_delay)
            self.cmd("input-send-event", events=self._abs(*p))
        time.sleep(0.05)
        self.cmd("input-send-event", events=self._btn(False))
        time.sleep(wait)

    def swipe(self, x0, y0, x1, y1, steps=12, **kw):
        pts = [(x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps) for i in range(steps + 1)]
        self.drag(pts, **kw)

    def key(self, qcode, wait=0.15):
        k = {"type": "qcode", "data": qcode}
        self.cmd("input-send-event", events=[{"type": "key", "data": {"down": True, "key": k}}])
        self.cmd("input-send-event", events=[{"type": "key", "data": {"down": False, "key": k}}])
        time.sleep(wait)

    def type_text(self, text):
        names = {" ": "spc", "-": "minus", ".": "dot", ",": "comma", "/": "slash", "\n": "ret"}
        for ch in text:
            self.key(names.get(ch, ch))

    def shot(self, path):
        ppm = os.path.join(self.workdir, "shot.ppm")
        self.cmd("screendump", filename=ppm)
        Image.open(ppm).save(path)
        print("скриншот:", path)

    def close(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.proc.wait(timeout=10)
