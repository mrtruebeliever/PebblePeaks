#!/usr/bin/env python3
"""Send button presses to the running emery emulator via pypkjs websocket.

Needs libpebble2. Easiest interpreter: the pebble-tool uv venv python, e.g.
~/.local/share/uv/tools/pebble-tool/bin/python tools/press.py ...

Usage: press.py <cmd> [<cmd> ...]
  cmd = back|up|select|down          short press
        hold:<button>:<ms>           hold a button for <ms>
        sleep:<ms>                   wait
"""
import json
import sys
import time

from libpebble2.communication.transports.websocket import (
    MessageTargetPhone, WebsocketTransport)
from libpebble2.communication.transports.websocket.protocol import WebSocketRelayQemu
from libpebble2.communication.transports.qemu.protocol import QemuPacket, QemuButton

BUTTONS = {
    "back": QemuButton.Button.Back,
    "up": QemuButton.Button.Up,
    "select": QemuButton.Button.Select,
    "down": QemuButton.Button.Down,
}


def main():
    with open("/tmp/pb-emulator.json") as f:
        info = json.load(f)
    port = next(iter(info["emery"].values()))["pypkjs"]["port"]
    t = WebsocketTransport("ws://localhost:%d/" % port)
    t.connect()

    def set_state(state):
        btn = QemuButton(state=state)
        packet = QemuPacket(data=btn)
        packet.serialise()
        t.send_packet(WebSocketRelayQemu(protocol=packet.protocol, data=btn.serialise()),
                      target=MessageTargetPhone())

    for cmd in sys.argv[1:]:
        if cmd.startswith("sleep:"):
            time.sleep(int(cmd.split(":")[1]) / 1000.0)
        elif cmd.startswith("hold:"):
            _, name, ms = cmd.split(":")
            set_state(int(BUTTONS[name]))
            time.sleep(int(ms) / 1000.0)
            set_state(0)
        else:
            set_state(int(BUTTONS[cmd]))
            time.sleep(0.08)
            set_state(0)
        time.sleep(0.15)


if __name__ == "__main__":
    main()
