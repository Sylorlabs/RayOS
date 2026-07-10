#!/usr/bin/env python3
"""rayos_window.py — live interactive window for the RayOS desktop.

Displays desktop/live_frame.ppm (refreshed continuously by rayos_desktop_live)
and injects mouse input directly into the RayOS MMIO input ring at
/tmp/rayos_mmio.bin — the same shared mapping hw_rt.flash uses — so no
separate injector process is needed.

Ring layout (driver/input_chamber.flash), at MMIO offset 0xC000:
  head:i32  tail:i32  capacity:i32  then 64 events of {kind,x,y,btn} i32 each.

Run from the repo root:  python3 tools/rayos_window.py
"""
import mmap
import os
import struct
import tkinter as tk

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRAME = "/tmp/rayos_live_frame.ppm"
MMIO_PATH = "/tmp/rayos_mmio.bin"
MMIO_BYTES = 65536
INPUT_OFF = 0xC000          # RAYOS_INPUT_MMIO_BASE - RAYOS_MMIO_BASE
EVENT_BASE = INPUT_OFF + 12
EVENT_STRIDE = 16
RING_CAP = 64

REFRESH_MS = 40             # frame reload cadence
MOVE_MS = 25                # min interval between injected move events


class InputRing:
    def __init__(self):
        fd = os.open(MMIO_PATH, os.O_RDWR)
        try:
            self.mem = mmap.mmap(fd, MMIO_BYTES)
        finally:
            os.close(fd)

    def _reg(self, off):
        return struct.unpack_from("<i", self.mem, INPUT_OFF + off)[0]

    def queue_len(self):
        head, tail = self._reg(0), self._reg(4)
        cap = self._reg(8) or RING_CAP
        return (tail - head) if tail >= head else (cap - head + tail)

    def push(self, kind, x, y, btn):
        head, tail = self._reg(0), self._reg(4)
        cap = self._reg(8)
        if cap != RING_CAP:  # chamber not initialized yet
            return False
        if self.queue_len() >= cap - 1:
            return False
        slot = tail % cap
        struct.pack_into("<iiii", self.mem, EVENT_BASE + slot * EVENT_STRIDE,
                         kind, x, y, btn)
        struct.pack_into("<i", self.mem, INPUT_OFF + 4, (tail + 1) % cap)
        return True


class RayOsWindow:
    def __init__(self, root):
        self.root = root
        root.title("RayOS — live")
        self.ring = InputRing()
        self.label = tk.Label(root, cursor="crosshair", bd=0)
        self.label.pack()
        self.img = None
        self.last_mtime = 0.0
        self.last_move = 0.0
        self.dragging = False
        self.label.bind("<Motion>", self.on_move)
        self.label.bind("<B1-Motion>", self.on_drag)
        self.label.bind("<ButtonPress-1>", self.on_press)
        self.label.bind("<ButtonRelease-1>", self.on_release)
        root.bind("<KeyPress>", self.on_key)
        self.refresh()

    def refresh(self):
        try:
            mtime = os.path.getmtime(FRAME)
            if mtime != self.last_mtime:
                img = tk.PhotoImage(file=FRAME)
                self.label.configure(image=img)
                self.img = img  # keep a reference
                self.last_mtime = mtime
        except (OSError, tk.TclError):
            pass  # frame mid-write; retry next tick
        self.root.after(REFRESH_MS, self.refresh)

    def _move(self, e, btn):
        # Throttle moves so the one-event-per-frame consumer isn't flooded
        # and pending clicks aren't starved behind stale motion.
        now = e.time / 1000.0
        if now - self.last_move < MOVE_MS / 1000.0:
            return
        if self.ring.queue_len() > 4:
            return
        if self.ring.push(0, e.x, e.y, btn):
            self.last_move = now

    def on_move(self, e):
        if not self.dragging:
            self._move(e, 0)

    def on_drag(self, e):
        # move events (kind 0) carry the drag; the WM tracks them live
        self._move(e, 1)

    def on_press(self, e):
        self.dragging = True
        self.ring.push(1, e.x, e.y, 1)

    def on_release(self, e):
        self.dragging = False
        self.ring.push(1, e.x, e.y, 0)

    SPECIAL_KEYS = {
        "Return": 10, "KP_Enter": 10, "BackSpace": 8, "Tab": 9,
        "Escape": 27, "Left": 128, "Right": 129, "Up": 130, "Down": 131,
    }

    def on_key(self, e):
        # printable ASCII straight through; named keys via the table
        code = None
        if e.keysym in self.SPECIAL_KEYS:
            code = self.SPECIAL_KEYS[e.keysym]
        elif len(e.char) == 1 and 32 <= ord(e.char) <= 126:
            code = ord(e.char)
        if code is not None:
            self.ring.push(2, code, 0, 1)   # kind 2 = key, btn 1 = down


if __name__ == "__main__":
    root = tk.Tk()
    root.resizable(False, False)
    RayOsWindow(root)
    root.mainloop()
