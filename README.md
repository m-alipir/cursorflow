# CursorFlow

An ultra-smooth, customizable cursor motion blur and trailing effect for Windows and Linux (X11).

CursorFlow draws two things on top of your real cursor: a fixed **front cursor** with an instant, always-on-top shape of your choosing, and a **ghost trail** that lags behind it with physics-based motion, rotation, and fading motion blur. It's an OS-level version of the cursor-follow effects you've seen on fancy websites.

## Preview

<img width="425" height="188" alt="CursorFlow preview" src="https://github.com/user-attachments/assets/e61bd13a-efb0-479e-b214-89ee37987afe" />

## How It Works

- **Layer 1 (front cursor):** overrides the OS cursor with a fixed shape — a thin or thick cross, a dot, or your own custom cursor file — optionally in a high-contrast "invert" mode that stays visible against any background.
- **Layer 2 (ghost trail):** a separate transparent overlay window renders a spring-lagged copy of your cursor, with configurable follow speed, rotation on fast movement, motion blur, and a fading trail of past positions.
- The overlay automatically hides itself while a fullscreen game or another excluded process is in the foreground, so it never gets in the way or trips up anti-cheat software.

## Features

- **Fully custom front cursor** — thin cross, thick cross, dot, or your own `.cur`/`.ani`/`.ico` file, with an optional color-invert mode.
- **Physics-based ghost trail** — adjustable blur intensity, trail length, size, rotation power, and follow speed ("snappiness"), all live-previewed in the settings window.
- **Per-app auto-disable** — list process names (games, anti-cheat-protected apps) that should make the overlay disable itself automatically.
- **Run at startup** — one checkbox to launch CursorFlow automatically at login.
- **Crash-safe** — a lightweight watchdog restores your real cursor if the overlay is ever killed unexpectedly.
- Cross-platform: native Win32/Direct2D on Windows, X11/Cairo on Linux.

## Installation

### Windows

1. Download the latest `CursorFlow-windows-x64.zip` from the [Releases](../../releases) page and extract it anywhere.
2. Run `CursorFlow.exe`. It starts silently in the system tray.
3. Double-click the tray icon (or right-click → **Settings**) to open the settings window shown below.

### Linux (X11 only — not Wayland)

1. Download the latest `cursorflow-linux-x64.tar.gz` from the [Releases](../../releases) page, or build from source (below).
2. Requires `libx11`, `libxfixes`, `libxi`, `libxext`, `libxcursor`, `cairo`, and `gtk3` to already be installed (all standard on most desktop distros).
3. Run `./CursorFlow` from the extracted folder, and `./CursorFlowSettings` to open the settings window.
4. To run at login, check **Run at Startup** in the settings window — it adds a standard XDG autostart entry under `~/.config/autostart/`.

## Configuration

<img width="442" height="893" alt="CursorFlowSettings_kHYhGrIIlH" src="https://github.com/user-attachments/assets/315b0d7a-2b8f-467a-8fe6-1b449ad576c7" />

| Option | Description | Default |
| :--- | :--- | :--- |
| **Blur Intensity** | Strength of the motion blur on the trail | `1.00x` |
| **Trail Length** | Number of fading trail points | `24` |
| **Ghost Size** | Overall size of the ghost trail | `1.00x` |
| **Rotation Power** | How much the ghost leans into fast movement | `1.00x` |
| **Follow Speed (Snappiness)** | How quickly the ghost catches up to the real cursor | `1.00x` |
| **Front Cursor Shape** | Thin cross, thick cross, dot, or a custom cursor file | Thick Cross |
| **Invert Colors** | High-contrast color-invert mode for the front cursor | On |
| **Run at Startup** | Launch CursorFlow automatically at login | Off |
| **Excluded Processes** | Comma-separated process names that auto-disable the overlay | *(empty)* |

Every option applies live, within about a second, with no restart needed.

## Building from Source

Requires CMake 3.25+ and a C++20 compiler.

```bash
cmake --preset windows-debug   # or linux-debug on Linux
cmake --build --preset windows-debug
```

See [CMakeLists.txt](CMakeLists.txt) for the exact dependencies per platform.

## License

Distributed under the MIT License. See [LICENSE](LICENSE) for more information.
