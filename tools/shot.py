#!/usr/bin/env python
"""Screenshot the emery emulator via the qemu monitor (screendump).

Fallback for when the pebble-protocol screenshot service times out.
Usage: tools/shot.py out.png
Interpreter: ~/.local/share/uv/tools/pebble-tool/bin/python (has PIL).
"""
import json
import os
import socket
import sys
import tempfile
import time

from PIL import Image


def monitor_port():
    with open("/tmp/pb-emulator.json") as f:
        state = json.load(f)
    emery = state["emery"]
    ver = next(iter(emery))
    return emery[ver]["qemu"]["monitor"]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "screenshot.png"
    ppm = tempfile.mktemp(suffix=".ppm")
    s = socket.create_connection(("localhost", monitor_port()), timeout=5)
    time.sleep(0.3)
    s.recv(4096)  # banner/prompt
    s.sendall(("screendump %s\n" % ppm).encode())
    time.sleep(1.0)
    s.close()
    for _ in range(20):
        if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
            break
        time.sleep(0.2)
    im = Image.open(ppm)
    im.save(out)
    os.unlink(ppm)
    print("%s %sx%s" % (out, im.size[0], im.size[1]))


if __name__ == "__main__":
    main()
