# afk-guard

Lightweight C utility that prevents AFK kicks in MMOs and online games by periodically injecting organic-looking input at the kernel level via `uinput`. Works on any Wayland or X11 Linux setup regardless of window focus.

## How it works

afk-guard creates a virtual input device through `/dev/uinput` that the kernel treats as real hardware. It simultaneously monitors all real input devices via `evdev` — the moment you touch your keyboard or mouse, it backs off and does nothing. When it detects you've been idle long enough, it fires one of:

- **Spacebar hold** — 40–200ms, duration normally distributed around 100ms
- **Mouse micro-jitter** — ±1–3px movement that returns imperfectly (not a perfect round-trip, which would look robotic)
- **Both** — space tap followed by a jitter

The interval between injections is uniformly random between 0 and 4m30s, picked fresh each cycle down to the second. No metronomic pattern, no fixed rhythm.

Because input is injected at the kernel level, it works across any game engine or anti-AFK detection method that reads from the input stack — no window focus or X11 access required.

## Requirements

- Linux with `uinput` support (virtually all modern distros)
- `gcc` and `make`
- Wayland or X11

## Install

```bash
git clone https://github.com/x0xt/afk-guard
cd afk-guard
./install.sh
```

The script will:
1. Build the binary and install it to `~/.local/bin/`
2. Add your user to the `input` group (needed for `/dev/uinput` and evdev access)
3. Install the systemd user service
4. Ask if you want afk-guard to **autostart on login**
5. Ask if you want **Steam per-game setup instructions**

> **Note:** If your user wasn't already in the `input` group, log out and back in after running the script.

## Usage

### Run manually

```bash
afk-guard [--idle SECS] [--max-interval SECS]
```

| Flag | Default | Description |
|---|---|---|
| `--idle` | `8` | Seconds of no real input before injecting |
| `--max-interval` | `270` | Upper bound for random interval (0–N seconds) |

Runs until Ctrl+C.

Example — inject after 5s idle, interval capped at 90s:
```bash
afk-guard --idle 5 --max-interval 90
```

### Run as a background service

```bash
# enable autostart on login
systemctl --user enable afk-guard

# start now
systemctl --user start afk-guard

# check status / logs
systemctl --user status afk-guard
journalctl --user -u afk-guard -f

# stop
systemctl --user stop afk-guard
```

### Per-game via Steam

afk-guard ships a `game-wrap` helper that starts afk-guard when a game launches and kills it cleanly when the game exits.

1. Open Steam → right-click your game → **Properties → General**
2. Paste into the **Launch Options** field:

```
game-wrap %command%
```

If you use this, leave the systemd autostart disabled so you don't end up with duplicate instances.

## How it auto-pauses

afk-guard opens every `/dev/input/event*` device and polls for real keyboard, mouse, or controller activity. Any real input resets the idle clock. It will never inject while you're actively playing — only when you've stepped away.
