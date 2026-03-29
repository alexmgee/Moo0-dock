# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What This Is

Moo0-dock is a Windows system tray app (~45KB) that docks Moo0 System Monitor to the Windows 11 taskbar. It manipulates Moo0's window externally via Win32 APIs — no code injection, no external dependencies beyond the Windows SDK. Requires admin elevation (UAC) because Moo0 runs elevated (WinRing0 kernel driver), and UIPI blocks non-elevated processes from manipulating elevated windows.

## Build

Requires CMake 3.16+ and MSVC (Visual Studio 2019+) or MinGW-w64.

```
# MSVC
cmake -B build
cmake --build build --config Release
# Output: build/Release/moo0-dock.exe

# MinGW
cmake -B build -G "MinGW Makefiles"
cmake --build build
# Output: build/moo0-dock.exe
```

No tests, no linter, no package manager. Zero external dependencies.

## Releasing

Push a tag matching `v*` (e.g. `git tag v1.0.0 && git push --tags`) to trigger the GitHub Actions workflow. It builds on `windows-latest` and attaches `moo0-dock.exe` to a GitHub Release automatically.

## Architecture

Single file (`src/main.cpp`, ~850 lines). All mutable state lives in the global `g_app` (AppState struct). Single-threaded Win32 message pump, no classes, no headers.

### Core Loop

Two mutually exclusive Win32 timers drive everything:
- **TIMER_POLL (200ms):** Idle states — searching for Moo0, docked, hidden.
- **TIMER_ANIM (16ms, ~60fps):** Active only during taskbar slide transitions.

When a slide starts, POLL is killed and ANIM starts. When the slide ends, ANIM is killed and POLL resumes.

### Visibility State Machine

`DockState` enum: SEARCHING → DOCKED ↔ SLIDING_IN/SLIDING_OUT ↔ HIDDEN, plus FULLSCREEN (polling + AppBar push) and UNLOCKED (user repositioning).

### Key Components (all in main.cpp)

| Section | What it does |
|---|---|
| Window Finder (`findMoo0Window`) | EnumWindows matching `MahobiWindowClass` + title containing "System Monitor" |
| Taskbar Tracker (`updateTaskbarInfo`) | GetWindowRect on Shell_TrayWnd / Shell_SecondaryTrayWnd |
| Notify Area Detector (`updateNotifyAreaLeft`) | Finds system tray left edge via TrayNotifyWnd (primary) or fallback estimation (secondary) |
| Position Engine (`positionMoo0`) | Right-aligns Moo0 against the notification area boundary, clamps to taskbar bounds |
| Fullscreen Detection (`isFullscreenAppActive`) | Polling-based check + AppBar push notifications (belt-and-suspenders) |
| Config Manager (`Config` struct) | Plain-text key=value file (moo0-dock.cfg) next to exe |
| Tray Menu (`showTrayMenu`) | Right-click context menu for all user settings |

### Adding a Tray Menu Option

1. Add enum value to `MenuID`
2. Add `AppendMenuW` call in `showTrayMenu()`
3. Add `case` in `WM_COMMAND` handler in `wndProc()`
4. If persistent, add field to `Config` struct and update `load()`/`save()`

### Notification Area Detection

The position engine right-aligns Moo0 against the notification area (system tray), not the taskbar edge. Detection uses a three-tier fallback:

1. **TrayNotifyWnd** — found via `findChildDeep()` recursive search. Works on primary taskbar. Does not exist on `Shell_SecondaryTrayWnd`.
2. **Shell_NotifyIconGetRect** — queries our own tray icon position. Only valid if the icon is on the target taskbar (Y bounds must be within taskbar rect; rejects overflow popup coordinates).
3. **Hardcoded estimate** — 300px for primary, 150px for secondary. Last resort.

## Coding Conventions

- **C++17**, Win32 API, zero external dependencies
- **Unicode always:** `UNICODE`/`_UNICODE` defined, all string literals use `L""`, all Win32 calls use `W` suffix
- **Naming:** camelCase functions/variables, UPPER_CASE constants, PascalCase structs/enums
- **Error handling:** Win32 return values checked, failures handled silently (app keeps polling). No exceptions.
- **Window style:** Do NOT modify Moo0's window styles (WS_CAPTION, WS_POPUP, etc.) — Moo0 manages its own appearance. The wrapper only controls position and visibility.
- **Elevation:** The exe embeds a UAC manifest via `/MANIFESTUAC:level=requireAdministrator` in CMakeLists.txt. Required because Moo0 runs elevated.

## Constants

| Constant | Value | Notes |
|---|---|---|
| `MOO0_CLASS` | `"MahobiWindowClass"` | Verified via EnumWindows against Moo0 v1.83. Two visible windows with this class; match by title to get the monitor overlay. |
| `MOO0_TITLE_FRAG` | `"System Monitor"` | Title substring match. |
| `POLL_ANIM_MS` | 16 | Do not raise above 20 — breaks slide sync. |

## Known Limitations

- No DPI awareness (mixed-DPI setups may need `SetProcessDpiAwarenessContext` + coordinate scaling)
- No custom tray icon (uses generic `IDI_APPLICATION`)
- Secondary monitor support is single-secondary only (no 3+ monitor enumeration)
- Secondary monitor notification area width is estimated (150px) since `TrayNotifyWnd` doesn't exist on `Shell_SecondaryTrayWnd`
