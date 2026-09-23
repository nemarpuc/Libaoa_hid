# Quickstart: verifying against a real phone

This is a practical "does it actually work" walkthrough, distinct from the
rest of `docs/`, which is written as an engineering/evidence audit. Nothing
here changes any claim made elsewhere; it exists so a first-time user can get
from a fresh checkout to a phone reacting to real HID reports.

## 1. Install build tools

Debian/Ubuntu:

```sh
sudo apt install cmake g++ libusb-1.0-0-dev
```

Arch/CachyOS/Manjaro:

```sh
sudo pacman -S cmake gcc libusb
```

Windows: install CMake, a recent Visual Studio (or the Build Tools) with the
C++ workload, and a libusb 1.0.30+ development package (for example via
vcpkg; `vcpkg.json` in this repository already declares the dependency).

## 2. Cable and hub check (the most common silent failure)

A cable or hub port marked with a battery/lightning-bolt icon is often
**charge-only** and has no data lines connected at all; the phone will never
appear in USB enumeration through it, no matter what the code does. Coming
from a hub, use a port explicitly labeled a data port (for example "USB 3.0"),
not a "PD"/charging-only port meant for feeding power into the hub itself. If
in doubt, plug the phone directly into a USB port on the computer, skipping
any hub, for the first attempt. See "Troubleshooting" below for how to
confirm the phone is even visible before touching this library at all.

## 3. Linux: USB permissions

For normal user access, install the udev rule:

    sudo cp udev/51-aoahid.rules /etc/udev/rules.d/
    sudo udevadm control --reload-rules
    sudo udevadm trigger

Then reconnect the phone.

For a quick test, you can skip the udev setup and run the programs with sudo:

    sudo ./aoahid_example_c
    sudo ./aoahid_verify_keyboard
    sudo ./aoahid_verify_touch
    sudo ./aoahid_verify_mouse
    sudo ./aoahid_verify_toggle
    sudo ./aoahid_verify_battery
    sudo ./aoahid_verify_accessory "Your Company" "Your Model"

If `aoahid_device_open` fails with `AOAHID_ERR_ACCESS`, try running the
program with `sudo` first. If that works, the problem is likely USB
permissions; install the udev rule above if you want to run the program
without sudo.

## 4. Windows: `adb` will block you if it is running

If Android Studio, `adb`, `scrcpy`, Vysor, or anything else has ever
connected to this phone over USB debugging, Windows may have bound its own
(non-WinUSB) driver to the same interface this library needs, and
`aoahid_device_open` will fail. Before trying anything else on Windows:

```powershell
adb kill-server
```

and close any app that might restart it. If it still fails, see
`docs/PORTING.md`'s Windows section for the Zadig/WinUSB driver-swap steps.
This is a Windows driver-model fact, not something this library's Mode A/Mode
B choice controls.

## 5. Build against the real libusb backend

`AOAHID_USE_FAKE_LIBUSB` defaults to a value appropriate for testing; for a
real device it must be explicitly `OFF` (it is the deterministic test double
used by `ctest`, and never touches real USB hardware):

```sh
mkdir build && cd build
cmake .. -DAOAHID_USE_FAKE_LIBUSB=OFF -DAOAHID_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

## 6. Confirm the phone is visible to the host at all

Before running anything from this library:

```sh
lsusb          # Linux
```

or, on Windows, open Device Manager and look for the phone under "Portable
Devices" or similar. If it is not listed here, this library cannot see it
either -- go back to step 2.

## 7. Run the full multi-profile example

```sh
sudo ./aoahid_example_c
```

(On Linux, `sudo` is the fast path if you skipped step 3; on Windows no
elevation is normally required once the driver binding in step 4 is correct.)
A successful run prints the discovered device's bus/address/VID:PID/serial
and exits with status `0`. This program deliberately keeps every state change
brief, because it also runs unattended in CI against zero fake devices; it is
correctness evidence, not a demo you are meant to watch.

## 8. Run the human-observable verification programs

`examples/c/verify/` contains small, heavily commented programs built
specifically to be watched, each printing what to do before it starts and
pausing so you have time to react:

| Program | What to do first | What you should see |
|---|---|---|
| `aoahid_verify_keyboard` | Open a text field (Notes, a search box, anything with a text cursor) | The literal text `hello from libaoahid` typed out, three times |
| `aoahid_verify_mouse` | Nothing required | The cursor moves in a circle for a few seconds -- many phones do not show a visible cursor at all without an accessibility/DeX-style pointer mode enabled; a clean exit with no error is still useful evidence even with nothing visible |
| `aoahid_verify_touch` | Have any screen open | The screen shows a left-right dragging motion, repeated five times |
| `aoahid_verify_toggle` | Unlock the phone and put a media app in the foreground (ideally a paused track) | Play/Pause, Volume Increment, Mute, and "AC New" each fire in turn; watch the media app and/or `adb shell getevent -lt` for the accessory's `/dev/input/eventN` (only Consumer Control Usages are sent -- see the comment at the top of the file for why System Control is deliberately excluded) |
| `aoahid_verify_accessory <manufacturer> <model>` | Open a text field; the two strings are your product values (Android matches them against an app's accessory filter and may show a "no app" prompt) | The phone disconnects and reconnects in AOA accessory mode (`18d1:2d00`, or `2d01` with USB debugging), one `a` is typed, and with `2d01` an ADB Channel opens and closes on the same USB handle. Unplug and replug to leave accessory mode |
| `aoahid_verify_battery` | Nothing required | Nothing shows in `getevent` by design (Battery Strength is kernel `power_supply` metadata, not an input event); check `adb shell dumpsys battery` and `/sys/class/power_supply` instead, as printed by the program and detailed in the comment at the top of the file |

```sh
sudo ./aoahid_verify_keyboard
sudo ./aoahid_verify_touch
sudo ./aoahid_verify_mouse
sudo ./aoahid_verify_toggle
sudo ./aoahid_verify_battery
```

Each one prints a clear error (with the field/reason from
`aoahid_last_error()`) instead of failing silently, and each returns a
nonzero exit status on failure.

## Troubleshooting

| Symptom | Likely cause | What to check |
|---|---|---|
| Phone never appears in `lsusb` / Device Manager at all | Charge-only cable or hub port | Step 2 |
| `AOAHID_ERR_ACCESS` on Linux | Missing udev permission | Step 3, or run with `sudo` first to isolate the cause |
| Device open fails only on Windows | `adb` or another tool holds the interface | Step 4 |
| Program exits `0`, nothing visible happens | No focused text field (keyboard) or normal single-shot API demo (`aoahid_example_c`) rather than a sustained one | Use the programs in step 8 instead, and focus a text field first for the keyboard case |
| A physical-keyboard indicator/icon flickers but no character appears | The Android input pipeline detected the HID keyboard, but no text field was focused at that instant | Refocus a text field and rerun; this is not a library-level failure |
| `AOAHID_ERR_UNSUPPORTED` from `aoahid_device_open` with `AOAHID_START_ACCESSORY_MODE` | Mode B is an intentional, permanent ABI tombstone | Use `AOAHID_START_CURRENT_USB_MODE`; see `README.md` |

None of the "what you should see" descriptions above are a hardware
verification claim in the sense `docs/TARGET_MATRIX.md` uses that phrase; they
are a first sanity check, not a substitute for that matrix's four evidence
layers.
