# Pebble Peaks

A vertical climbing game for Pebble Time 2. Race upward from ledge to ledge while a rising mist threatens from below. Collect gems, unlock upgrades, and reach the summit at 1000 meters.

## Download

Get **Pebble Peaks** on Pebble Platform (link coming soon) or sideload via `pebble install --cloudpebble`.

## Gameplay

Jump your way up a mountain in real time:
- **UP** = jump left-up | **DOWN** = jump right-up
- **SELECT** + upgraded rope = rescue bounce (return to last ledge)
- **BACK** = pause

Watch out for:
- **Ice ledges** — you slide toward the edge (watch your footing!)
- **Crumble ledges** — vanish after you land
- **Falling rocks** — knock you down to a lower ledge
- **Rising mist** — game over if it catches you

## Upgrades

Spend collected gems to buy three upgrades at the shop (up to 5 levels each):

1. **Grip Soles** — Slower ice slides, longer-lasting crumble ledges
2. **Down Jacket** — Mist starts higher and rises more slowly
3. **Climbing Rope** — Emergency rescue charges (SELECT to bounce back)

Unlock all 5 levels of all three upgrades to earn enough advantage for the peak.

## Features

- **Parallax mountain** with procedurally generated peaks and snow couloirs
- **Physics verified**: exact jump reachability tested across hundreds of random ledge layouts
- **Multilingual**: English, Dutch, French, German, Spanish
- **HUD**: real-time height, mist-distance warning, gem counter
- **Persistent progress**: lifelong gem total, height record, purchased upgrades, summit trophy

## Development & Technical

**For developers:** See [`plan.md`](plan.md) for design and [`COMMON.md`](COMMON.md) for shared conventions.

**Physics validation**: Jump reachability is proven correct via `tools/sim_climb.py` — a fixed-point simulation that mirrors the game's exact arithmetic (500+ random seeds, zero failures).

**Platform**: Pebble Time 2 (emery), SDK 3, 4-color grayscale (2 bits per channel).

## Build

```bash
cd PebblePeaks
pebble build
```

Install to real hardware:
```bash
pebble install --cloudpebble
```

Install to emulator:
```bash
export PEBBLE_QEMU_PATH=/path/to/PebblePeaks/tools/qemu-icount-wrapper.sh
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
pebble install --emulator emery
```

## License

MIT
