# Target and Verification Matrix

## Current release status

As of **2026-08-27**, the HID descriptor and specified-Linux source audit is complete for the facts recorded in `FACT_AUDIT.md`. **No physical Android device, USB host/backend combination, or release archive is recorded as verified in this file.**

The matrix begins from "not run" on purpose. A successful compile is not an Android compatibility result; a Linux source review is not a kernel runtime result; and a `getevent` result is not an application-API result.

## Status vocabulary

| Status | Meaning |
|---|---|
| `Source-reviewed` | The named official specification or exact source revision was read. No binary or device was exercised. |
| `CI-passed` | The named workflow run built and tested the exact commit; link to the run and artifact is required. |
| `Hardware-verified` | The four Android evidence layers below were recorded on one identified physical target. |
| `Conditional` | Evidence exists only under the recorded configuration, release, or OEM condition. |
| `Not run` | No evidence has been recorded. This is the current default. |
| `Failed` | A reproducible result did not satisfy the stated gate; link to logs and the exact target is required. |

## Specification and source targets

| Target | Revision/version | Evidence level | What was established | What was not established |
|---|---|---|---|---|
| USB HID | HID 1.11, 2001-05-27 | `Source-reviewed` | Item encoding, signedness, short/extended Usage ranges, Report IDs, reports, field span, Null State | Acceptance by any target parser |
| HID Usage Tables | HUT 1.7, 2026-01-26 | `Source-reviewed` | Audited Usage IDs, Usage types, and semantics listed in `FACT_AUDIT.md` | Linux or Android support for a Usage |
| Linux HID core/input/multitouch | Commit `35556bed836f8dc07ac55f69c8d17dce3e7f0e25`, 2020-09-01 | `Source-reviewed` | Behavior of the exact symbols listed in `FACT_AUDIT.md` | Behavior of a different kernel or an OEM-modified tree |
| Android 17 ACK controller/pen-priority cross-check | `android17-6.18` commit `f67745b7d96806e622db56f4be97af16d6e99850` | `Source-reviewed` | `hid_hat_to_axis`, Generic Desktop Hat/raw-D-pad configuration and dispatch, and the explicit Eraser/Invert/Tip/Pressure/In-Range priority path in `hid-input.c` and `hid-core.c` | Multitouch, battery, pen behavior beyond that ordering, OEM changes, runtime acceptance, or behavior of a later revision |
| Android 17 game-controller contract | [Android 17 CDD §7.2.6.1](https://source.android.com/docs/compatibility/17/android-17-cdd), retrieved 2026-08-27, and the [Android game-controller input page](https://developer.android.com/games/sdk/game-controller/controller-input), page updated 2026-02-26 | `Source-reviewed` | Game Pad Application Collection; Button Page A=`1`, B=`2`, X=`4`, Y=`5`; canonical Hat logical/physical/unit/size contract; the recorded clockwise/value-1 source conflict; axis signs and deadzone guidance | Physical device classification, event delivery, framework mapping, or application-visible results |
| Android touch documentation | Page revision 2026-07-13 | `Source-reviewed` | Documented pressure/hover contract | Behavior of a specific phone or application |
| AOA current-mode request routing | AOA 2.0 page plus kernel/common commits `0e3db17d01c94263b629b089c51c7d0988308232` and `9b03ed2feb9c4cc3f15d44eda080caabb3a6b843` | `Source-reviewed` | AOA HID uses EP0, and the named implementations route requests 54-57 without testing `ACCESSORY_START` state | That every Android or OEM kernel accepts Mode A before `START` |

Retrieval date for every row: 2026-08-27. Official URLs and exact symbols are
in `FACT_AUDIT.md` and, for AOA transport, `SOURCE_CONFLICTS.md`.

## Published release build targets

Under the current C-17 scope in `SOURCE_CONFLICTS.md`, only these four native
targets are intended for GitHub Release publication. A row changes to
`CI-passed` only when the release commit, workflow URL, toolchain, linked libusb
form, archive checksum, and exported-symbol check are recorded.

| Host target | Architecture / C runtime | Required shared artifact | Required static artifact | Current status | Evidence |
|---|---|---|---|---|---|
| Linux | x86_64 / glibc | `libaoahid.so` | `libaoahid.a` | `Not run` | None recorded |
| Linux | aarch64 (ARM64) / glibc | `libaoahid.so` | `libaoahid.a` | `Not run` | None recorded |
| Windows | x86_64 | `aoahid.dll` plus import library | `aoahid_static.lib` | `Not run` | None recorded |
| Windows | aarch64 (ARM64) | `aoahid.dll` plus import library | `aoahid_static.lib` | `Not run` | None recorded |

The filenames above identify the required artifact kind, not proof that an artifact exists. GitHub Actions emulation or cross-compilation does not replace a native load/link smoke test for the release archive.

### CI portability targets

These configurations are intended to compile and test portability. They are not
0.2.0 GitHub Release artifacts under the current C-17 publication scope.

| Host target | Architecture / C runtime | Current status | Evidence |
|---|---|---|---|
| Linux | x86_64 / musl | `Not run` | None recorded |
| Linux | aarch64 (ARM64) / musl | `Not run` | None recorded |
| macOS | x86_64 | `Not run` | None recorded |
| macOS | arm64 | `Not run` | None recorded |

## Android physical-target matrix

Each row represents one exact combination. Do not combine results from two devices into one "Android version" result.

| Android/API target | Device/OEM | Build fingerprint | Kernel revision | USB mode | Host/backend | Kernel input | `dumpsys input` | `getevent` | App API | Overall |
|---|---|---|---|---|---|---|---|---|---|---|
| Android 8 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 10 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 12 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 13 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 14 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 15 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 16 / API 36 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 16 MR1 / platform version 36.1 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 17 / API 37 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 17 MR1 / platform version 37.1 | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |
| Android 17 MR2 preview / platform version 37.2 (preview API 10000) | Not assigned | Not recorded | Not recorded | Not recorded | Not recorded | `Not run` | `Not run` | `Not run` | `Not run` | `Not run` |

Add a separate row for each current USB configuration and ADB state, each host
OS/backend, and each kernel/vendor build that materially changes the path. This
library does not initiate Accessory Mode, so a target that accepts requests
54-57 only after `ACCESSORY_START` is an unsupported connection result, not a
Mode-B fallback candidate. Do not overwrite a failing row with a later success;
add the new build as another row.

Mode A remains **target-conditional** and **unverified on hardware** for every
row above. Removing the Mode-B runtime path did not change any `Not run` status
and did not convert source inspection into device evidence.

The Android 16-and-later labels follow the exact `BAKLAVA`, `BAKLAVA_1`,
`CINNAMON_BUN`, `CINNAMON_BUN_1`, and `CINNAMON_BUN_2` symbols recorded in the
`SOURCE_CONFLICTS.md` primary-source register. Each release remains a separate
target; no later-version row inherits evidence from an earlier one.

## Per-profile hardware status

| Profile | Descriptor/static tests | Audited Linux source path | Physical kernel parse | Android classification | Event stream | Application API | Release wording allowed now |
|---|---|---|---|---|---|---|---|
| Keyboard, full-NKRO bitmap (also reached via the former Barcode/MSR wedge factory) | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; hardware unverified" |
| Mouse | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Implemented; hardware unverified" |
| Toggle: Consumer control | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; target mapping unverified" |
| Toggle: System control | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; target mapping unverified" |
| Toggle: Camera keys (`field_page = 0x90`) | Not recorded | Camera Usage/OSC form `Source-reviewed`; collection placement is guide policy | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; target mapping and interception unverified" |
| Toggle: Telephony keys | Not recorded | Telephony Usage form `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; target mapping and call-policy behavior unverified" |
| Gamepad, canonical Hat | Not recorded | Specified Linux revision, Android 17 ACK, and CDD §7.2.6.1 `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Portable candidate only for Game Pad + canonical Hat + contiguous Button range from 1 with count at least 5; hardware unverified" |
| Gamepad, raw D-pad OOC fields | Not recorded | HUT form plus Android 17 ACK individual-field dispatch `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; D-pad combination behavior unverified" |
| Gamepad, no D-pad | Not recorded | Gamepad collection and declared axis/button paths `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; canonical CDD Hat absent; hardware unverified" |
| Gamepad: Joystick application (`application = AOAHID_CONTROLLER_JOYSTICK`) | Not recorded | HUT Joystick collection and controller state path `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; classification and axis mapping unverified" |
| Touchscreen, fixed MT (the only Multi-Touch form) | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Portable candidate; hardware unverified" |
| Touchscreen: Touchpad application (`touchpad_button_count > 0`) | Not recorded | `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; hardware unverified" |
| Direct pen | Not recorded | Legacy descriptor-order path and Android 17 ACK priority order `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Portable candidate; hardware unverified" |
| Indirect pen/tablet | Not recorded | Legacy descriptor-order path and Android 17 ACK priority order `Source-reviewed` | `Not run` | `Not run` | `Not run` | `Not run` | "Conditional; classification unverified" |
| Battery Strength metadata | Not recorded | `Source-reviewed`; kernel option dependent | `Not run` | `Not run` | Not an ordinary input event | `Not run` | "Conditional metadata; hardware unverified" |
| Raw Input report | Not recorded | Parser rules source-reviewed | `Not run` | Not claimed | Not claimed | Not claimed | "No Android semantic support claimed" |

`Source-reviewed` in the third column means only that the corresponding branch in the specified Linux revision was inspected. It does not mean that `libaoahid` generated bytes have executed through that branch.

## Four required Android evidence layers

### 1. Kernel device and descriptor

Record:

- full `adb shell uname -a` and build fingerprint;
- HID descriptor bytes and a decoded form;
- `/proc/bus/input/devices` entry and exact `/dev/input/eventN` node;
- `adb shell getevent -lp /dev/input/eventN` capabilities, event types, codes, ranges, resolutions, and input properties; and
- kernel log for parser or registration errors.

### 2. Android classification

Record the relevant `adb shell dumpsys input` block, including device sources, keyboard type, touch mode, motion ranges, associated display, and selected IDC/KL/KCM files. A kernel event node without this layer is not an Android classification success.

### 3. Event sequence

Record timestamped `adb shell getevent -lt /dev/input/eventN` output for the profile cases below. If the production build denies access, record the exact denial and use an approved framework event-logging build; do not label missing evidence as passed.

### 4. Application API

Record from an identified test-application commit:

- `InputDevice.getSources()` and every relevant motion range;
- `KeyEvent` scan code, keycode, action, repeat, meta state, device ID, and source;
- `MotionEvent` source, masked action/index, pointer count/IDs, tool types, X/Y, pressure, size, orientation, tilt, axes, and buttons; and
- focus, IME, MediaSession, lock-screen, and system-policy differences where relevant.

## Minimum per-profile cases

| Profile | Cases required before `Hardware-verified` |
|---|---|
| Keyboard | Modifier chords; all-up; a nonmodifier count well beyond six held simultaneously (full-NKRO, no Array slot limit to hit); repeat; layout and IME differences |
| Mouse | Positive and negative X/Y; split delta; all buttons; release; wheel; AC Pan; high-rate stream; pointer capture |
| Toggle: Consumer/System/Camera/Telephony | Every allowed Usage; `down=1` and `down=0`; a guaranteed-observed single click (`down=1`, wait for completion, `down=0`) assertion and zero re-arm where exposed; foreground/background; screen off; system, media, camera, and call-policy interception |
| Gamepad, canonical Hat | Game Pad collection; contiguous Button range beginning at `1` with at least `5` entries and explicit A/B/X/Y checks; every button and axis; explicit neutral; minimum/maximum; all eight Hat directions including value `1` as Up-right; no-direction Null value `15`; Logical `0..7`; Physical `0..315`; Unit Degrees `0x14`; opposite-direction rejection; simultaneous controls |
| Gamepad, raw D-pad | Each independent Up/Down/Right/Left bit; adjacent and opposite simultaneous bits; release to all-zero; target event order and final Android axes/keys |
| Gamepad, no D-pad | Every declared button and axis; explicit neutral; minimum/maximum; absence of Hat and raw D-pad fields; source and Android classification |
| Joystick | Every declared button and axis; explicit neutral; minimum/maximum; selected Hat-or-raw-D-pad form; source and Android classification |
| Touchscreen | One through configured maximum contacts; stable Contact IDs; crossing; explicit Up; multi-packet continuation count zero; edges/corners; pressure, azimuth/rotation, and palm fields when enabled; Scan Time first-frame zero, elapsed 100-microsecond progress, one value across continuation packets, declared-period wrap, and inactivity reset when enabled |
| Touchpad | Tap, click, and drag when the corresponding controls are enabled; configured multi-contact gestures; palm fields when enabled; pointer acceleration; Scan Time continuation reuse and reset after final contact/button release when enabled |
| Pen | Descriptor Variable order Invert, Tip Switch, In Range when eraser selection is enabled; Away wire normalization of In Range, Tip, pressure, Invert, and barrel buttons to zero; remembered-tool re-entry; hover; tip; pen-to-eraser departure/re-entry; four corners; simultaneous touch; pressure, barrel controls, tilt, and Twist when enabled |
| Battery Strength | Kernel `CONFIG_HID_BATTERY_STRENGTH`; declared minimum/midpoint/maximum; raw zero; unknown Null when enabled; `power_supply` metadata; Android association; application battery API |
| Teardown | Neutral report; unregister; in-flight unplug; cancel; reopen; fresh HID ID; Android reboot |

## Evidence record template

Add one record per target/profile combination:

```markdown
### <device> / <build fingerprint> / <profile>

- Date:
- libaoahid commit:
- GitHub Actions run and artifact SHA-256:
- Host OS, architecture, libusb version, and backend:
- Android version/API and build fingerprint:
- Kernel `uname -a` and source revision if available:
- Current physical USB configuration and ADB state:
- Descriptor SHA-256 and decoded descriptor attachment:
- Kernel capabilities attachment:
- `dumpsys input` attachment:
- `getevent -lt` attachment:
- Test-application commit and log attachment:
- Result: Hardware-verified / Conditional / Failed
- Known deviations:
```

## Release gate

The phrase "Android supported" is blocked for a profile/target pair until all four evidence layers are present on the same identified physical target. The following do not remove that block:

- a passing CMake build;
- a passing unit or mock-transport test;
- a descriptor parser or `hid-tools` result;
- inspection of Linux or AOSP source;
- a successful result on an emulator;
- a successful result on another Android version or OEM; or
- a generated DLL, shared object, package, or GitHub Release.

Until evidence is added, release notes use the exact qualified wording in the per-profile table and state that hardware validation has not been performed.
