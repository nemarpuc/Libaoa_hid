# Porting

The public ABI is platform-neutral. Only `src/transport` includes libusb.
Evidence labels and the official-source register for transport and platform
claims are defined in `SOURCE_CONFLICTS.md`.

## Linux

Build against libusb 1.0.30-or-later headers and APIs. Runtime validation is a
separate policy: it accepts exactly the 1.0 version line with micro version 30
or later and rejects a future major/minor line pending an official compatibility
audit. See `SOURCE_CONFLICTS.md` T-16. Grant the invoking user explicit USB
permissions. `udev/51-aoahid.rules` grants access by udev's `uaccess` tag,
which is what this library actually needs: every device it opens keeps its
OEM VID/PID (`aoahid_device_open` uses Mode A, so there is no fixed Google
Accessory VID/PID to match), and most current distributions already grant the
logged-in seat that same access by default. The Google-Accessory-range
(`18d1:2d00`-`2d05`) rule covers a device that `aoahid_accessory_start`
switched; review both against the distribution's group policy before
installing.

A running `adb` server is a much less frequent blocker on Linux than on
Windows (libusb can typically still claim the device even while `adb` is
watching it), but if `aoahid_device_open` fails only while `adb devices` also
lists the phone, run `adb kill-server` and retry before assuming a udev
permission problem.

Do not claim a generic "Linux" binary solely because it ran on one glibc image.
Release artifact names include architecture and build baseline. Inspect ELF
machine type, dependencies, RPATH, and minimum glibc symbol versions during
packaging.

The current Linux release baseline is Ubuntu 22.04 with glibc 2.35. The release
workflow runs GNU `readelf --version-info --wide` over every regular shared
library in both package variants and over every direct `.so` asset. Empty or
failed analysis is fatal, as is any nonnumeric `GLIBC_*` dependency (including
private/ABI namespaces) or numeric requirement newer than `GLIBC_2.35`. Each
Linux archive carries
`share/doc/libaoahid/glibc-requirements.json`; its build metadata and the top
level release manifest repeat the maximum requirement. This is an audited
glibc baseline, not a musl or generic-Linux compatibility claim.

Linux host builds with GCC 12 or newer, or with Clang, can enable ThreadSanitizer with
`AOAHID_TSAN=ON`; the shortest supported invocation is `cmake --preset tsan`,
`cmake --build --preset tsan`, then `ctest --preset tsan`. CMake rejects this
option on non-Linux hosts, with other compiler families, with GNU 11 or older, with
`AOAHID_SANITIZE=ON`, or with `AOAHID_BUILD_FUZZ=ON`. The preset uses the
deterministic fake libusb backend, `RelWithDebInfo`, and
`TSAN_OPTIONS=halt_on_error=1`. Configuration first requires both C and C++ to
compile and link the TSan runtime and requires PIE link support; the library,
tests, and examples are then instrumented. An instrumented `libaoahid` static
archive is covered, but this does not make a fully static libc/libstdc++
executable supported. It is a host concurrency diagnostic, not a portable
Android qualification result and not an artifact configuration for release.
GNU 11 is rejected because its unresolved GCC bug 101978 produces a false
condition-variable double-lock report in this suite; CI uses GCC 12 and keeps
all reports enabled rather than suppressing that diagnostic class.

## Windows

The target device/function needs a serviceable WinUSB binding. **[Windows
requirement]** Device-recipient control transfer routing does not imply physical
USB interface 0. The audited libusb v1.0.30 backend selects a serviceable
interface **[implementation observation]**; the library therefore lets that
backend select unless the caller explicitly names and claims an interface, and
releases only that claim. **[project policy]** See `SOURCE_CONFLICTS.md` T-01.

**A running `adb` server or a manufacturer's USB driver can block this on
Windows.** Windows binds one driver per USB function. In the audited libusb
v1.0.30 backend, an interface is usable only when its driver is WinUSB,
libusbK, or libusb0 (`winusbx_driver_names`); any other driver makes claiming
it fail with `LIBUSB_ERROR_NOT_SUPPORTED` **[implementation observation]**.
The Google USB Driver is WinUSB-based, so it does not cause this by itself,
but while an `adb` server holds the ADB interface, opening or claiming it
fails. Before opening the device:

```powershell
adb kill-server
```

and close Android Studio, Vysor, scrcpy, or anything else that keeps an ADB
connection open, since any of them restarts the server.

Some manufacturers bind their own driver instead of WinUSB. On a Samsung
tablet with Samsung's `dg_ssudbus` driver, HID worked but
`aoahid_channel_open` on the ADB interface failed with `AOAHID_ERR_UNSUPPORTED`
and libusb status `-12`; replacing the ADB or MTP interface's driver did not
help, and replacing the whole device's driver ("SAMSUNG Android" in
[Zadig](https://zadig.akeo.ie/)) with WinUSB did. With the whole device on
WinUSB, libusb reaches every interface through one WinUSB handle
(`WinUsb_GetAssociatedInterface`) **[implementation observation]**, at the
cost of Windows' own functions for that device, such as MTP file transfer.
This was observed on a Samsung Galaxy Tab S11 under Windows 10 x64; the
step-by-step fix is in the aoahid_player
README's Troubleshooting section. None of this means the library implements
or requires the Android Debug Bridge protocol.

The configured Windows x64 and ARM64 builds use native GitHub-hosted runners
**[GitHub Actions contract]** and architecture inspection. Configured release
archives include the import library and the dynamically linked libusb DLL/license
when applicable; `TARGET_MATRIX.md` records whether a particular run passed.

## Other libusb platforms

The transport uses public libusb 1.0.30 APIs, but the project release gate covers
only the targets in `TARGET_MATRIX.md` and the workflows. A new backend requires
official libusb documentation/source review for control-length, claim, cancel,
event, and device-correlation behavior plus integration tests. Do not copy the
Windows or Linux policy by analogy.

## Android target revisions

Gadget-side and input-framework behavior is revisioned. Before adding a target:

1. identify the exact kernel/AOSP/OEM revision;
2. inspect its accessory control handler and HID/input mappings;
3. record any divergence in `FACT_AUDIT.md` or `SOURCE_CONFLICTS.md`;
4. run the four target-matrix observation layers;
5. keep Windows-only requirements out of Android conclusions.
