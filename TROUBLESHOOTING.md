## Troubleshooting

### Moo0-dock says "Waiting for Moo0..." but Moo0 is running

The wrapper finds Moo0 by looking for its window class name and title. If neither matches, it can't latch on. This is the most likely issue on first run.

**Step 1: Check if the title match is working.**

Right-click the Moo0-dock tray icon. If it still says "Waiting for Moo0...", the window title fallback isn't matching either. This can happen if your version of Moo0 uses a different title format.

**Step 2: Find Moo0's actual window class name.**

You need a window inspection tool. The easiest free option is [WinSpy](https://github.com/nickvdp/winspy/releases) (standalone exe, no install). Alternatively, [Spy++](https://learn.microsoft.com/en-us/visualstudio/ide/using-spy-increment) comes with Visual Studio (including the free Community edition).

1. Open Moo0 System Monitor so it's visible on screen.
2. Open WinSpy (or Spy++).
3. In WinSpy, there's a crosshair/finder icon. Click and drag it onto the Moo0 window.
4. WinSpy will display the window's properties. Look for **Class Name** (currently expected to be `MahobiWindowClass`).
5. Also note the **Window Title** shown.

**Step 3: Update the constants.**

Open `src/main.cpp` and find these two lines near the top:

```cpp
static const wchar_t* MOO0_CLASS      = L"MahobiWindowClass";
static const wchar_t* MOO0_TITLE_FRAG = L"System Monitor";
```

Replace the values with what WinSpy reported. For the class name, use the exact string. For the title fragment, use a distinctive substring of the window title.

Rebuild with `cmake --build build --config Release` and try again.

---

### Moo0-dock is running but Moo0 doesn't move

Moo0 System Monitor runs as an elevated (admin) process because it uses a kernel driver for hardware sensor access. Windows UIPI security prevents non-elevated programs from moving elevated windows — the `SetWindowPos` calls silently fail.

Moo0-dock embeds a UAC manifest that requests admin elevation automatically. If you're not seeing a UAC prompt when launching, or if Moo0 isn't responding to the dock:

1. Make sure you're running the Release build (`build/Release/moo0-dock.exe`), not a Debug build that may lack the manifest.
2. Try right-clicking `moo0-dock.exe` and selecting **Run as administrator** manually.
3. If that works, the UAC manifest may not be embedded correctly. Rebuild with `cmake -B build && cmake --build build --config Release`.

---

### Moo0 docks but doesn't hide with the taskbar

Make sure taskbar auto-hide is actually enabled: **Settings > Personalization > Taskbar > Taskbar behaviors > Automatically hide the taskbar**. Moo0-dock only tracks slide animations when auto-hide is on. If auto-hide is off, Moo0 stays visible at all times (which is the correct behavior).

If auto-hide is on and Moo0 still isn't hiding, the slide detection might be missing short transitions. Open `src/main.cpp` and find `POLL_IDLE_MS`. Try lowering it from `200` to `100`. This makes the idle polling more frequent so it's less likely to miss the start of a slide.

---

### Moo0 doesn't hide during fullscreen apps

Moo0-dock uses two methods to detect fullscreen apps:

1. **Polling:** Every 200ms, it checks whether the foreground window covers the entire monitor that the taskbar is on.
2. **AppBar notifications:** Windows sends `ABN_FULLSCREENAPP` events for true exclusive fullscreen apps.

The polling method excludes desktop and shell windows (`Progman`, `WorkerW`, taskbar) so that an empty desktop isn't mistakenly treated as fullscreen.

This should catch most cases, but it does **not** trigger for:

- Maximized windows (even if they cover the taskbar)
- Some borderless windowed games (these are technically regular windows that may not cover the full monitor rect)

If fullscreen detection isn't working at all, check if another instance of Moo0-dock is running (the mutex prevents duplicates). Quit all instances and restart.

---

### Moo0 appears on the wrong monitor

Right-click the Moo0-dock tray icon and check the **Monitor** submenu. Switch between "Auto (primary)", "Primary monitor", and "Secondary monitor" to target the correct taskbar. The setting takes effect immediately.

If you have three or more monitors, only the primary and first secondary are currently supported. The "Secondary monitor" option targets the first `Shell_SecondaryTrayWnd` window that Windows reports.

---

### Ctrl+Shift+M hotkey doesn't work

The hotkey may conflict with another application that has registered the same global hotkey. Common conflicts include:

- Some Logitech or Razer keyboard software
- Clipboard managers
- Other system utilities

If the hotkey doesn't respond, use the tray icon instead: right-click and toggle "Lock position". To change the hotkey, find `HOTKEY_LOCK_ID` in `src/main.cpp` and modify the `RegisterHotKey` call to use a different key combination.

---

### Moo0 flickers or jumps during taskbar slide

This can happen if another application is also trying to manage Moo0's window position, or if Moo0's own "Always On Top" setting conflicts with the wrapper's SetWindowPos calls.

Try this: in Moo0 System Monitor, right-click and make sure **"Always On Top"** is enabled. Both Moo0 and the wrapper want the window to be topmost, and having both agree prevents fighting.

If flickering persists, the animation timer may be running too slowly for your display's refresh rate. Open `src/main.cpp` and find `POLL_ANIM_MS`. If you have a high-refresh-rate monitor (144Hz+), try lowering it from `16` to `8`.

---

### The config file is missing or corrupt

Delete `moo0-dock.cfg` (in the same folder as the exe). The wrapper will recreate it with default values on the next settings change. Your position and monitor preferences will reset to defaults.
