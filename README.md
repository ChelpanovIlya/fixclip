# fixclip

Syncs X11 CLIPBOARD selection to PRIMARY. When you copy something (Ctrl+C), it automatically becomes available for middle-click paste.

## Why?

Many programs (browsers, file managers, etc.) copy text to CLIPBOARD via Ctrl+Insert. But terminals typically paste from PRIMARY selection using Shift+Insert. So you copy something with Ctrl+Insert, switch to a terminal, press Shift+Insert — and nothing happens, because the data is in CLIPBOARD, not PRIMARY.

On X11, CLIPBOARD (Ctrl+C/V) and PRIMARY (select + middle-click) are separate selections. This also means copying text with Ctrl+C doesn't make it available for middle-click paste.

`fixclip` bridges this gap by keeping PRIMARY in sync with CLIPBOARD.

## Dependencies

- libX11
- libXfixes

**Debian/Ubuntu:**
```sh
sudo apt install libxfixes-dev
```

**Arch:**
```sh
sudo pacman -S libxfixes
```

**Fedora:**
```sh
sudo dnf install libXfixes-devel
```

## Build & Install

```sh
make
sudo make install
```

## Usage

```
fixclip -f           run in foreground
fixclip -d           run as daemon (background)
fixclip -v           foreground with verbose debug output
fixclip -h           show help
```

**Foreground:**
```sh
fixclip -f
```

**Daemon:**
```sh
fixclip -d
# fixclip: daemon started (pid 12345)
```

**Stop:**
```sh
killall fixclip
```

## Output

Each sync is logged to stderr:
```
[12:34:56] running (pid 12345), watching CLIPBOARD -> PRIMARY
[12:34:58] SYNC  PRIMARY: "(empty)" → "hello world"
[12:35:01] SYNC  PRIMARY: "hello world" → "new clipboard text"
```

## Autostart

Add to your window manager startup or `~/.xprofile`:

```sh
fixclip -d &
```

## License

MIT
