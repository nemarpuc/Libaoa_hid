# libaoahid

[Canonical repository](https://github.com/nemarpuc/Libaoa_hid) ·
[first-push and release setup](GITHUB_SETUP.md)

## AI disclosure

Most of the code, tests, and documentation in this repository were written by
an AI coding assistant. Architecture and design decisions, hardware
verification steps, debugging, and review of the AI's output were done by a
human. No claim in this repository - including the descriptor/Android
support statuses below - has been independently re-verified beyond what is
described in [TARGET_MATRIX.md](docs/TARGET_MATRIX.md); treat it accordingly,
especially before relying on it for anything security- or safety-relevant.

`libaoahid` is a C++20 host library with a stable C ABI for sending HID **Input**
reports through Android Open Accessory 2.0 control requests. It has no option
initializers, profile presets, or inferred bit widths: callers supply every
product/target choice such as ranges, widths, Report IDs, Usage sets, parser
policies, and event mode. A zero in one of the documented host-transport tuning
fields selects a bounded project-policy fallback; see [API.md](docs/API.md).

No profile in this repository has yet completed the four-level physical-device
gate (`getevent`, `dumpsys input`, application API, and kernel device). The
statuses below are source-backed candidates or conditional paths, not a claim
of hardware verification. See [TARGET_MATRIX.md](docs/TARGET_MATRIX.md).

## Support table

<!-- profile-table:start -->
| Profile | Manifest status | Input | Output | Feature transport | Qualification |
|---|---|---:|---:|---:|---|
| Keyboard, full NKRO bitmap | conditional | yes | no | no | One-bit Variable field per key; target matrix still required |
| Mouse / relative pointer | portable candidate | yes | no | no | Target matrix still required |
| Toggle: Consumer Control fields | conditional | yes | no | no | Sparse allow-list, explicit HUT semantics, and target event evidence |
| Toggle: System Control fields | conditional | yes | no | no | Explicit HUT semantics; system handling and target mapping vary |
| Gamepad | portable candidate | yes | no | no | Caller-declared axes and target mappings |
| Gamepad: Joystick application | conditional | yes | no | no | Caller-declared axes and target mappings; Android classification requires target evidence |
| Touchscreen, fixed MT | portable candidate | yes | no | no | Audited Linux commit and dated Android documentation; target matrix still required |
| Touchscreen: Touchpad application | conditional | yes | no | no | Classification and gestures vary by release/OEM |
| Pen, direct screen | portable candidate | yes | no | no | Invert tool transition; target matrix still required |
| Pen, indirect tablet | conditional | yes | no | no | Target classification and mapping evidence required |
| Toggle: Camera keys | conditional | yes | no | no | HUT Camera Auto-focus/Shutter subset only |
| Toggle: Telephony keys | conditional | yes | no | no | Caller allow-list and target mapping required |
| Battery Strength telemetry | conditional | yes | no | no | Kernel power-supply association and OEM dependent |
| Validated raw descriptor | unknown | yes | no | no | Explicit no-Android-support acknowledgement |
<!-- profile-table:end -->

The descriptor-export test writes a catalog from the actual runtime manifests;
`tools/generate-support-table/generate.py` renders and checks this table from
that catalog. Node and spec manifests expose exact report IDs, wire lengths,
descriptor size, direction capability, and the same evidence-conscious status
at runtime.

### Generated state automation

Generated profiles derive frame mechanics from the immutable Spec and accepted
Node state; callers still choose the product and target contract. This is
**[Guide policy]** over the evidence recorded in
[PROFILES.md](docs/PROFILES.md), [FACT_AUDIT.md](docs/FACT_AUDIT.md), and
[SOURCE_CONFLICTS.md](docs/SOURCE_CONFLICTS.md).

| Profile group | Derived by the library | Still explicit |
|---|---|---|
| Keyboard and barcode wedge | Modifier routing; a full N-Key Rollover (NKRO) Variable bitmap maps each declared Usage to its own bit, so every simultaneously pressed key is reported at once with no Array slot count and no ErrorRollOver overflow encoding. | Key Usage interval, Report ID, intended characters, layout, locale, and IME behavior. |
| Mouse | Independent signed 64-bit pending totals and in-range report fragments for X, Y, Wheel, and AC Pan; only a submitted fragment is consumed on terminal completion. | Axis ranges and widths, button count, Wheel/Pan presence, acceleration, and Report ID. |
| Consumer, System, Camera, and Telephony | One exact allow-listed field is asserted; accepted press/release/tap edges provide the `1` then `0` lifecycle used by Selector, OOC toggle, OOC maintained, MC, OSC, and RTC descriptors. Camera remains the fixed audited OSC subset. | Each allowed Usage and semantic, expected target event, Report ID, and whether Android exposes or intercepts it. |
| Gamepad and joystick | Boolean directions become the eight canonical Hat values plus no-direction Null `15`; adjacent pairs become diagonals and opposite pairs are rejected. Raw D-pad mode keeps four independent bits, and no-D-pad mode invents no direction. | Axis roles/ranges/widths/neutrals, buttons, D-pad representation, target mappings, and Report ID. Only a Game Pad with the canonical Hat and a contiguous Button range from `1` with at least five fields is a portable candidate; raw/no-D-pad forms and every Joystick remain conditional. |
| Touchscreen and touchpad | Contact Count and continuation count, stable-ID Tip=0 Up records, pressure floor, frame Scan Time from the 100-microsecond elapsed-time contract, packet sequencing, and touchpad button/frame guards. | Coordinate/contact/count/time domains, maximum contacts, contacts per report, optional fields, target verification, and gesture policy. |
| Direct and indirect pen | Tip/pressure consistency, Away wire normalization, and an automatic departure before an in-range pen/eraser end switch. | Coordinate and optional-field domains, hover/eraser/barrel choices, display association, and target mapping. |
| Battery Strength | A known zero remains a known value; explicit unknown emits the deterministic out-of-range Null encoding only when the Spec enables it. | Strength range and width, unknown support, Report ID, and target visibility. |
| Raw Input | No semantic state is generated; only the caller's accepted report ID, length, padding, and immutable parsed layout are checked. | Descriptor bytes, report bytes, every Usage/range/ID, and all platform semantics. |

Lifecycle guards retain a changed edge until its first accepted report and
return `AOAHID_ERR_BUSY` rather than coalescing an opposite edge. The library
does not infer field widths, ranges, neutral values, Usage semantics, target
mappings, or periodic repeat/timer policy. Passing host-side tests does not
change any hardware-verification status.

## Build

Requirements are CMake 3.20+, a C++20 compiler, and libusb headers/API at least
1.0.30. Runtime loading accepts the audited libusb 1.0.x line with micro version
30 or later; other major/minor lines are rejected pending a separate audit. The
project builds shared and static libraries by explicit switches.

```sh
cmake -S . -B build -DAOAHID_BUILD_SHARED=ON -DAOAHID_BUILD_STATIC=ON
cmake --build build --config Release
cmake --install build --prefix staging --config Release
```

For deterministic transport tests, configure with
`-DAOAHID_USE_FAKE_LIBUSB=ON -DAOAHID_BUILD_TESTS=ON`, build, and then run
`ctest --test-dir build -C Release --output-on-failure`. The fake backend is
test code and is never enabled in a release build.

### ThreadSanitizer and low-overhead checks

On a Linux host with GNU or Clang, run the first-class ThreadSanitizer preset:

```sh
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan
```

This `RelWithDebInfo` build instruments the library, tests, and examples with
ThreadSanitizer and exercises both caller-poll and internal-event-thread paths
against the deterministic fake backend. `AOAHID_TSAN=ON` cannot be combined
with `AOAHID_SANITIZE=ON` or `AOAHID_BUILD_FUZZ=ON`. It is a diagnostic build,
not a release or performance build; use an ordinary optimized build for timing
and CPU measurements.

The deterministic suite checks a prewarmed caller-poll keyboard update/submit/
completion cycle for zero calls to the instrumented C++ allocation operators
and verifies that this mode selects no Context or Node mutex. The internal
thread suite repeatedly races completion against close under ThreadSanitizer.
Its low-load checks also verify blocking idle waits and deterministic
cancellation/teardown wakeups instead of periodic nonblocking polling.
These tests establish specific host-side invariants, not a physical USB/Android
latency number or a proof that every possible execution is data-race-free. See
[LATENCY.md](docs/LATENCY.md) for the exact claims and tuning tradeoffs.

## Use

New to a real device? [docs/QUICKSTART.md](docs/QUICKSTART.md) is a practical,
step-by-step walkthrough covering cables/hubs, Linux udev permissions, the
Windows `adb`-conflict gotcha, and three small programs under
[examples/c/verify/](examples/c/verify) built specifically to be watched
(typing text, dragging on the touchscreen, moving the mouse in circles) so you
can confirm a real phone reacts before writing product code.

The complete, fully explicit C session is
[examples/c/multi_profile.c](examples/c/multi_profile.c). For a single profile
in isolation, [examples/c/profiles/](examples/c/profiles) has one self-contained
program per HID profile kind, all eight of which run without a device. In
outline:

1. Create a context and explicitly select caller-poll or internal-thread mode.
2. Discover and select a physical USB device.
3. Open in current-USB Mode A. This path is target-conditional: the library
   never sends `ACCESSORY_START` and does not fall back to re-enumeration.
4. Build immutable specs, register each as its own AOA HID ID, update state,
   then submit reports.
5. Close nodes/device so neutral state is completed before request 55.

The C header is [include/aoahid.h](include/aoahid.h). The optional C++ header
adds typed node references without changing policy. Python ctypes, C# P/Invoke,
and Rust `-sys` plus typed wrappers live under `bindings/`; they preserve the C
layout and do not choose overrides. The native C API applies the same documented
zero-value transport fallbacks for every language binding.

The device-option fallbacks are 500 ms for control and report transfers, 64
bytes per descriptor fragment, 8 pool slots, a 1024-byte maximum report buffer,
a 1000 ms close-drain budget, and 20 total first-report attempts with 1000
microseconds between retry attempts. These numbers are **[project policy]**, not
USB/AOA requirements or libusb recommendations. A timeout is the failure
deadline passed to the backend; it does not add delay to a successful transfer.
Explicit nonzero values remain unchanged. Node reservation `0/0` means no
reservation. Exact device, descriptor, target-parser, and product-profile
policies remain mandatory and are never guessed.

For binary compatibility, `AOAHID_START_ACCESSORY_MODE` and the former Mode-B
members of `aoahid_device_options` remain in the public layout as legacy ABI
tombstones. Selecting that mode returns `AOAHID_ERR_UNSUPPORTED` before USB I/O;
the retained members do not restore the removed requests 52, 53, or 58 path.
Devices whose firmware accepts AOA HID requests only after `ACCESSORY_START`
cannot be opened by this release. This source-backed limitation is not a
hardware result; see `SOURCE_CONFLICTS.md` T-07 and `TARGET_MATRIX.md`.

## Releases

A pushed `v*` tag runs the gate before creating a GitHub Release. The release
matrix produces and architecture-checks:

- Linux x86_64 and AArch64 shared objects and static archives;
- Windows x86_64 and ARM64 DLLs, import libraries, and static archives;
- headers, relocatable CMake/pkg-config metadata, notices, checksums, and an
  SPDX SBOM.

For version `X.Y.Z`, the GitHub Release contains exactly 23 uploaded assets:

- eight archives: `libaoahid-X.Y.Z-linux-{x86_64,aarch64}-ubuntu22.04-{shared,static}.tar.gz`
  and `libaoahid-X.Y.Z-windows-{x86_64,arm64}-{shared,static}.zip`;
- eight matching `.spdx.json` sidecars;
- four runtime bundles: the same four target names with `-runtime.tar.gz` on
  Linux and `-runtime.zip` on Windows;
- the deterministic tagged-tree archive `libaoahid-X.Y.Z-source.tar.gz`;
- `release-manifest.json` and `SHA256SUMS`.

GitHub also displays its automatically generated source-code links separately;
the uploaded `*-source.tar.gz` is the release workflow's byte-validated archive
and is covered by `release-manifest.json` and `SHA256SUMS`.

Linux artifacts have the explicit `ubuntu22.04`/glibc 2.35 baseline. Packaging
inspects every regular shared library and each direct `.so`; the archive,
build metadata, and release manifest record the maximum numeric `GLIBC_*`
requirement, reject every nonnumeric `GLIBC_*` dependency, and reject numeric
requirements above `GLIBC_2.35`.

The complete `*-shared` or `*-static` archive is what you build against: it is
the only asset carrying headers, the import library, and CMake/pkg-config
metadata.

The `*-runtime` bundle is for deploying next to an application that is already
built. It carries the libaoahid shared library, the libusb runtime it loads,
and the full license set for both - `LICENSE`, `NOTICE`,
`THIRD_PARTY_NOTICES.md`, libusb's LGPL-2.1 text, and libusb's official
corresponding source archive. No bare `.so` or `.dll` is published on its own,
because an asset that ships the libusb runtime has to carry libusb's license
and corresponding source in the same distribution unit; a single loose file
cannot. Packaging refuses to assemble a release if any runtime bundle is
missing one of those files.

Windows release builds use the MSVC dynamic runtime (`/MD`). Deploy the
architecture-matching supported Visual C++ v14 Redistributable as well as the
libusb runtime included next to the DLL in the complete shared archive.

The same workflow always builds, byte-rebuilds, installs, and validates a pure
Python wheel and a managed-only NuGet package before the native GitHub Release
job can publish. Registry publication is disabled unless the repository
variable `AOAHID_PUBLISH_BINDINGS` is exactly `true`. Before enabling it, the
repository owner must configure PyPI Trusted Publishing for the `aoahid`
project/workflow and a scoped `NUGET_API_KEY` secret with authority for the
`AoaHid` package ID; this source tree does not establish registry ownership.
Neither registry package contains native libraries, so users still install a
version-matched shared archive from the GitHub Release.

Workflow failure cannot undo an already-pushed Git tag; it prevents creation of
the GitHub Release and its assets. See [SOURCE_CONFLICTS.md](docs/SOURCE_CONFLICTS.md).

## Evidence and limits

- [QUICKSTART.md](docs/QUICKSTART.md) - practical real-device setup and verification, distinct from the evidence-audit documents below.
- [FACT_AUDIT.md](docs/FACT_AUDIT.md) - HID/HUT/Linux evidence and conflicts.
- [SOURCE_CONFLICTS.md](docs/SOURCE_CONFLICTS.md) - AOA, libusb, Android, and CI corrections.
- [PROFILES.md](docs/PROFILES.md) and [LIMITS.md](docs/LIMITS.md) - exact boundaries.
- [AOA_HID_GUIDE.md](docs/AOA_HID_GUIDE.md) and [DESIGN.md](docs/DESIGN.md) - current guide and design; byte-identical supplied originals are preserved under `docs/inputs/`.
- [INPUT_PROVENANCE.md](docs/INPUT_PROVENANCE.md) - byte-for-byte source-document checksums.

## License

`libaoahid` itself is **MIT** licensed. The full text is in
[LICENSE](LICENSE); the SPDX identifier is `MIT`. The Python and C# bindings
carry the same MIT license and ship a copy of that file inside their packages.

libusb is a **separate work under LGPL-2.1-or-later**. It is never statically
absorbed into `libaoahid`: every release links it dynamically, including the
archives labelled `static` (that label describes `libaoahid` itself, not
libusb). Each binary archive therefore also carries libusb's own license and
its official source tarball under
`share/doc/libaoahid/third-party/`.

| What you receive | License | Where the text is |
|---|---|---|
| `libaoahid` library, headers, bindings, examples | MIT | `LICENSE` |
| `libusb-1.0` runtime shipped in release archives and runtime bundles | LGPL-2.1-or-later | `share/doc/libaoahid/third-party/libusb-copyright` |
| vcpkg port files bundled in Windows archives | MIT (Microsoft) | `share/doc/libaoahid/third-party/vcpkg-LICENSE.txt` |

Every first-party source file carries its own
`SPDX-License-Identifier: MIT` and copyright line, so a single file stays
identifiable after it is copied out of this repository;
`tools/check_license_headers.py` enforces that in CI. The SPDX sidecar in each
release states the license of every packaged file individually rather than
`NOASSERTION`: first-party output is `MIT`, everything installed from the
libusb dependency is `LGPL-2.1-or-later`, and the pinned vcpkg material is
Microsoft's `MIT`.

Redistributing a release archive unchanged satisfies both licenses as shipped.
If you relink against a modified libusb, the LGPL obligations apply to that
copy. [NOTICE](NOTICE) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) record the exact versions and
checksums.
