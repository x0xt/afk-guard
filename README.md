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

- Linux with a kernel that has `uinput` support (virtually all modern distros)
- `gcc` and `make`
- Wayland or X11

## Install

```bash
git clone https://github.com/x0xt/afk-guard
cd afk-guard
./install.sh
```

The script will:
1. Build the binary
2. Install it to `~/.local/bin/`
3. Add your user to the `input` group (needed for `/dev/uinput` and evdev access)
4. Install and enable a systemd user service so it starts automatically on login

> **Note:** If your user wasn't already in the `input` group, log out and back in after running the script for the group change to take effect.

## Usage

After install, afk-guard runs automatically in the background as a systemd user service.

```bash
# check status
systemctl --user status afk-guard

# view live logs
journalctl --user -u afk-guard -f

# stop
systemctl --user stop afk-guard

# disable autostart
systemctl --user disable afk-guard
```

### Manual / custom run

```bash
afk-guard [--idle SECS] [--max-interval SECS]
```

| Flag | Default | Description |
|---|---|---|
| `--idle` | `8` | Seconds of no real input before injecting |
| `--max-interval` | `270` | Upper bound for random interval (0–N seconds) |

Example — inject after 5s idle, interval capped at 90s:
```bash
afk-guard --idle 5 --max-interval 90
```

## How it auto-pauses

afk-guard opens every `/dev/input/event*` device and polls for real keyboard, mouse, or controller activity. Any real input resets the idle clock. It will never inject while you're actively playing — only when you've stepped away.
