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
OEM VID/PID (this library implements Mode A only, so there is no fixed
Google Accessory VID/PID to match), and most current distributions already
grant the logged-in seat that same access by default. The historical
Google-Accessory-range (`18d1:2d00`-`2d05`) rule is also present but inert,
kept only for a possible future Mode-B implementation; review both against
the distribution's group policy before installing.

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

**A running `adb` server routinely blocks this entirely on Windows.** Windows
binds one driver per USB interface, and the stock Android/Google USB driver
binds its own (non-WinUSB) driver to the interface `adb` uses as soon as the
phone enumerates -- commonly the same composite device this library also needs
to reach over control transfers. With that driver bound, `aoahid_device_open`
fails, typically as `AOAHID_ERR_ACCESS` or a similar libusb open/claim failure,
even though `lsusb`-equivalent enumeration tools can still see the device.
Before opening the device:

```powershell
adb kill-server
```

and close Android Studio, Vysor, scrcpy, or anything else that keeps an ADB
connection open, since any of them restarts the server. If the device still
fails to open after that, use [Zadig](https://zadig.akeo.ie/) to replace the
interface's driver with WinUSB (or libusb-win32/libusbK) for this one
interface only; do not replace the driver Windows uses for its own composite
device enumeration or you can lose Explorer file-transfer access to the
phone. This is a Windows driver-model fact independent of this library's own
Mode A/Mode B behavior: it applies to any libusb-based tool trying to reach a
device `adb` is also watching, and it does not indicate that this library
implements or requires the Android Debug Bridge protocol in any way.

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
