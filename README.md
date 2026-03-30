# Moo0-dock

Docks [Moo0 System Monitor](https://www.moo0.com/software/SystemMonitor/) to the Windows 11 taskbar so it stays put, hides with the taskbar, and disappears during fullscreen apps.

![Windows 10/11](https://img.shields.io/badge/Windows%2010%2F11-0078D4?logo=windows&logoColor=white)
![C++17](https://img.shields.io/badge/C%2B%2B17-00599C?logo=cplusplus&logoColor=white)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)

## What it does

Moo0 System Monitor normally floats on the desktop as a draggable overlay. Moo0-dock turns it into something that behaves like part of the taskbar:

- **Stays on the taskbar.** Moo0's window locks to a position along the taskbar, right-aligned next to the system tray.
- **Hides with the taskbar.** When your taskbar auto-hides, Moo0 slides away with it in lockstep. When the taskbar comes back, so does Moo0.
- **Disappears during fullscreen.** Games, videos, presentations — Moo0 gets out of the way automatically.
- **Drag to reposition.** Press `Ctrl+Shift+M` to unlock, drag Moo0 where you want it on the taskbar, press the hotkey again to lock it there.
- **Multi-monitor support.** Choose which monitor's taskbar to dock to. Position and tray avoidance adapts per-monitor.
- **Remembers your settings.** Position, monitor choice, and startup preference persist between restarts.

## Download

Go to the [Releases](../../releases) page and download `moo0-dock.exe`. No installer needed — just one file.

## How to use

1. Make sure [Moo0 System Monitor](https://www.moo0.com/software/SystemMonitor/) is running.
2. Run `moo0-dock.exe`. You'll see a UAC prompt — accept it (see [Why admin?](#why-does-it-need-admin) below).
3. It appears as an icon in the system tray. Moo0's window will snap to the taskbar.
4. Right-click the tray icon for settings. To reposition, press `Ctrl+Shift+M` to unlock, drag Moo0, then press the hotkey again to lock.

### Tray menu options

| Option | What it does |
|---|---|
| **Lock position** | Toggle whether Moo0 is locked to the taskbar or free to drag. Hotkey: `Ctrl+Shift+M` |
| **Monitor** | Choose which monitor's taskbar to dock to (auto, primary, or secondary) |
| **Launch at startup** | Start Moo0-dock automatically when Windows starts |
| **Reset position** | Move Moo0 back to its default position (flush against the system tray) |
| **Quit** | Exit Moo0-dock (Moo0 itself keeps running) |

### Tips

- Moo0-dock doesn't modify Moo0 in any way. It only moves and shows/hides the window. Quitting Moo0-dock leaves Moo0 exactly as it was.
- If you start Moo0-dock before Moo0, it will wait quietly and dock Moo0 when it appears.
- The config file (`moo0-dock.cfg`) is saved next to the exe. Delete it to reset all settings.
- Moo0-dock automatically avoids overlapping the system tray / notification area on both primary and secondary monitors.

## How it works

Moo0-dock is a ~45KB system tray app that:

1. **Finds** Moo0's window by enumerating top-level windows and matching its window class (`MahobiWindowClass`) and title.
2. **Tracks** the taskbar's screen position via `GetWindowRect` on `Shell_TrayWnd` / `Shell_SecondaryTrayWnd` at 200ms intervals (or 16ms during slide animations).
3. **Positions** Moo0 right-aligned against the notification area boundary using `SetWindowPos`, clamped to taskbar bounds.
4. **Detects fullscreen** apps via both polling (`GetForegroundWindow` + monitor rect comparison) and AppBar push notifications (`ABN_FULLSCREENAPP`).
5. **Syncs with auto-hide** by tracking the taskbar's Y position frame-by-frame during slide animations, switching between 200ms and 16ms polling as needed.

It does not inject code into Moo0, modify system files, or alter Moo0's window styles. It uses only documented Win32 APIs.

### Why does it need admin?

Moo0 System Monitor runs as an elevated process because it uses a kernel driver (`WinRing0x64.sys`) to read hardware sensors. Windows [UIPI](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#user-interface-privilege-isolation) prevents non-elevated programs from manipulating elevated windows — calls to `SetWindowPos` and `ShowWindow` return `ERROR_ACCESS_DENIED`. Moo0-dock must run at the same privilege level to move and hide Moo0's window.

## Compatibility

- **Windows 11** (22H2, 23H2, 24H2, 25H2). The Windows 11 taskbar auto-hide animation is what this tool is built around.
- **Windows 10** should work but is not the primary target. The taskbar auto-hide behavior differs slightly.
- **Moo0 System Monitor** v1.76 and later (tested with v1.83). Both installed and portable editions.

## Building from source

Requires CMake 3.16+ and a C++ compiler (MSVC recommended, MinGW also supported).

```
cmake -B build
cmake --build build --config Release
```

Output: `build/Release/moo0-dock.exe` (MSVC) or `build/moo0-dock.exe` (MinGW).

The project has zero external dependencies beyond the Windows SDK.

## Troubleshooting

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for help with common issues (Moo0 not detected, fullscreen not hiding, hotkey conflicts, etc.).

## License

MIT
