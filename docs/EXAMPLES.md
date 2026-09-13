# Worked examples

There are deliberately no callable profile presets. The examples show complete
values that can be copied, reviewed for a target, and edited. They explicitly
override every host-transport tuning field for readability even where an
override equals the documented zero-value fallback. Product and target values
must still be supplied; the library never infers them.

Technical evidence labels and their source registers are defined in
`FACT_AUDIT.md` and `SOURCE_CONFLICTS.md`. Labels such as `[Caller example
policy]`, `[Caller example input]`, `[Project design]`, `[Project implementation
contract]`, and `[Project API observation]` are local annotations for selected
example values or repository behavior, not external protocol/platform evidence.
Language and host-platform labels are scoped to the source table below.

## Multi-profile lifecycle examples

The comprehensive examples are `examples/c/multi_profile.c`,
`examples/python/multi_profile.py`,
`examples/csharp/MultiProfile/Program.cs`, and
`examples/rust/src/main.rs`. Each exposes `shared` and `threaded` modes. **[Project
design: DESIGN.md §§3.5, 5, 9, 11]**

In `shared` mode, a runtime-sized collection holds every device from one
point-in-time discovery. The example builds one immutable keyboard, mouse, and
touchscreen spec set, registers all three on each opened device, updates all
devices before a second submit pass, closes only the touchscreen, and then uses
the still-live keyboard and mouse. A `NO_DEVICE` result consumes only that
Device, records its caller-owned bus/port/serial locator, re-runs discovery, and
registers the same specs on the matching device while sibling sessions remain
open. **[Project design: DESIGN.md §§3.4, 3.5, 11.1, 11.2]**

In `threaded` mode, a preliminary context copies one locator per runtime-
discovered device and is then destroyed. Each worker creates and exclusively
drives one context, rediscovers its selected physical identity, and performs the
same lifecycle. No fixed device count appears in storage or scheduling. **[Project
design: DESIGN.md §§5.1, 11.2]**

The close helpers preserve the ABI's two different ownership rules:

- `aoahid_node_close()` returning `AOAHID_CLOSE_PENDING` retains the Node, so
  the examples poll and retry that same Node. **[Project implementation
  contract: `include/aoahid.h`, `aoahid_node_close`]**
- the first `aoahid_device_close()` consumes the Device and all its Nodes even
  when it returns `AOAHID_CLOSE_PENDING`; the examples clear those handles and
  let the Context graveyard drain them. **[Project implementation contract:
  `include/aoahid.h`, `aoahid_device_close`]**
- `aoahid_context_destroy()` is retried only after `AOAHID_CLOSE_PENDING` and a
  poll. Every other result from a valid destroy is treated as terminal and the
  Context is never reused. **[Project implementation contract:
  `include/aoahid.h`, `aoahid_context_destroy`]**

## One example per profile kind

`examples/c/profiles/` holds one small, self-contained program for each of the
eight `aoahid_profile_kind` values. Each file carries the complete option set
for its own profile and nothing else, so it can be copied into a product and
edited directly. None of them creates a Context or opens a device, so they run
anywhere the library links, and `AOAHID_BUILD_TESTS` registers each one as
`aoahid.example-profile-<name>`.

| # | Profile kind | Example file | Factory |
|---:|---|---|---|
| 1 | `AOAHID_PROFILE_KEYBOARD` | `keyboard.c` | `aoahid_spec_create_keyboard` (also reaches the former Barcode/MSR wedge form) |
| 2 | `AOAHID_PROFILE_MOUSE` | `mouse.c` | `aoahid_spec_create_mouse` |
| 3 | `AOAHID_PROFILE_TOGGLE` | `toggle.c` | `aoahid_spec_create_toggle` (Consumer/System/Camera/Telephony, selected by `application_page`/`application_usage`/`field_page`) |
| 4 | `AOAHID_PROFILE_GAMEPAD` | `gamepad.c` | `aoahid_spec_create_gamepad` (also reaches Joystick via `application`) |
| 5 | `AOAHID_PROFILE_TOUCHSCREEN` | `touchscreen.c` | `aoahid_spec_create_touchscreen` (also reaches Touchpad via `touchpad_button_count`) |
| 6 | `AOAHID_PROFILE_PEN` | `pen.c` | `aoahid_spec_create_pen` |
| 7 | `AOAHID_PROFILE_BATTERY` | `battery.c` | `aoahid_spec_create_battery` |
| 8 | `AOAHID_PROFILE_RAW` | `raw.c` | `aoahid_spec_create_raw` |

`profile_example.h` is shared by all eight. It holds only the reporting
helper; it contains no product values, so each example remains the single
source of its own options. That helper prints two things: the descriptor size
and report layout the library derived from the caller's options, and then the
complete call sequence that would send the profile to a real device -
`aoahid_context_create`, `aoahid_discover`, `aoahid_device_open`,
`aoahid_node_open`, the profile's own mutation calls, `aoahid_node_close`, and
`aoahid_device_close`. Each example supplies its own mutation steps, so
`keyboard.c` shows the `aoahid_kbd` press/submit/release/submit cycle while
`touchscreen.c` shows the `aoahid_touch` tip-down/tip-up frame. The sequence is
printed rather than executed because these programs open no device; run
`examples/c/verify/` against a phone to watch the same calls take effect.

The option values are the audited vectors already used by
`tests/hidtools/export_descriptors.cpp`, reduced to one representative form per
kind. The expected target-event strings in the control allow-list examples are
placeholders: a product must supply the event names it actually observed on its
own target. **[Caller example policy; no hardware-support claim]**

The smaller enumerate-only examples remain available at
`examples/python/enumerate.py`, `examples/csharp/Program.cs`, and
`bindings/rust/aoahid-sys/examples/enumerate.rs`. They print bus, current USB
address, physical port path where the comprehensive form is used, VID/PID,
product, and serial exactly as the discovery snapshot reports them. **[Project
API observation; no Android-support claim]**

## Human-observable real-device verification programs

`examples/c/verify/` is a third, separate category from the two above: unlike
`multi_profile.c` (an automated, CI-registered correctness exercise fired
once and gone in milliseconds) and `examples/c/profiles/` (Spec-only, never
opens a device), each program under `verify/` opens a real device and
produces a sustained, sighted effect a person watching the phone can actually
confirm -- typing a message repeatedly (`verify_keyboard.c`), dragging across
the screen five times (`verify_touch.c`), and moving the cursor in a circle
for several seconds (`verify_mouse.c`). They print what to do before they
start (for example, focus a text field) and print a plain-language error
instead of failing silently. `docs/QUICKSTART.md` is the full walkthrough
these programs are built for; `CMakeLists.txt` builds all three whenever
`AOAHID_BUILD_EXAMPLES` is on, but never registers them as `ctest` cases,
since they need a physical phone and are meant to be watched rather than run
unattended.

## Example value ledger

Every number below is visible in each comprehensive example and is selected by
that caller. A zero-valued native tuning field would instead select the bounded
fallback recorded in `API.md`; these examples do not rely on that shorthand.
**[Caller example policy]**

| Example value | Role and evidence label |
|---|---|
| 500 ms | Control, send, poll, and blocking-submit budget selected for the example. Control/send happen to equal the native zero fallback; poll and blocking-submit have separate explicit arguments. A timeout is a failure deadline, not added success latency. **[Caller example policy; project policy for the native fallback]** |
| 1000 ms | Device close-drain budget, explicitly matching the native zero fallback. **[Caller example policy; project policy for the native fallback]** |
| 64 bytes | Descriptor fragment policy, explicitly matching the native zero fallback. The official AOA page fixes ordering and offset, not a fragment size. **[Caller example policy; project policy for the native fallback; AOA 2.0 requirement for ordering]** |
| 8 pool slots; 1 reserved slot per Node | Capacity and reservation choices for this three-profile example. Eight matches the Device fallback; the one-slot Node reservation is explicit because zero/zero means no reservation. **[Caller example policy; project policy for the native fallbacks]** |
| 20 total first-report attempts; 1000 microseconds backoff | Bounded response to the audited asynchronous registration race, explicitly matching the native zero fallbacks. Twenty includes the initial attempt and therefore permits at most 19 backoff intervals. **[Guide/project policy based on implementation observation]** |
| 4096 descriptor/target policies | Portability values tied to the exact target evidence in `docs/FACT_AUDIT.md`; they are not stated as universal AOA limits. **[Caller example policy]** |
| 4088 host/report policy | Deliberately conservative explicit value whose official-source conflict is recorded in `docs/SOURCE_CONFLICTS.md` T-10. It overrides the 1024-byte `maximum_report_bytes` fallback; it remains mandatory as `host_control_buffer_policy_bytes`. **[Caller example policy]** |
| 256 fields, stack depth 4, 12288 usages, 65528 report-data bits, 256 report-size bits | Parser policies copied from the exact audited Linux revision named in `docs/FACT_AUDIT.md`, not generalized to other kernels. **[Implementation observation selected as caller policy]** |
| Keyboard `0x04` through `0x65`, array length 6, width 8 | Keyboard a/A through Keyboard Application comes from HUT 1.7 §10; array length and width are caller choices. **[HUT 1.7 definition; caller example policy]** |
| Mouse axes -127 through 127 at 8 bits; 3 buttons | Complete signed relative range and profile geometry chosen by the caller. **[Caller example policy]** |
| One-contact MT; Contact ID 0 through 15 at 4 bits; coordinates 0 through 32767 at 16 bits | A complete one-contact example geometry; none of these ranges is claimed as an Android or HID universal value. **[Caller example policy]** |
| Key A, mouse delta `(30,-20)`, contact `(1000,2000)`, pointer button 1 | Input performed solely to make every state/submit pass observable. **[Caller example input]** |

`interface_number=-1` accompanies the explicit no-claim policy. Every disabled
optional field is explicitly zero, and every required field is assigned. The
immutable specs stay alive through reconnection and are released only after all
Nodes have either closed or transferred into Device teardown. **[Project design:
DESIGN.md §§3.4, 4, 9]**

Build it by enabling `AOAHID_BUILD_EXAMPLES`, or compile it against an installed
CMake package target `aoahid::aoahid`. With no requested component, an installed
package selects `shared` when present and otherwise selects `static`.
`find_package(aoahid CONFIG REQUIRED COMPONENTS shared)` loads only the shared
export. `COMPONENTS static` loads only the static export and resolves libusb;
the explicit static target is always `aoahid::aoahid_static`. A static-only
installation also provides `aoahid::aoahid` as a compatibility/default alias.
Both components can be requested together.

Release archives also install `lib/pkgconfig/aoahid.pc`. Shared metadata has no
private libusb dependency. Static metadata declares libusb and, on toolchains
that expose it as an implicit link library, the compiler-reported C++ runtime
needed when a C compiler links `libaoahid.a`; the static archive includes a
relocatable `libusb-1.0.pc` for that dependency.

## Creating an option structure

Use zero initialization as storage initialization, then assign every product
and target field whose meaning applies. A Device tuning field may deliberately
remain zero to request its documented fallback; the comprehensive examples set
all eight explicitly. An optional disabled product field must have its enable
flag set to zero and its associated field storage left as the explicitly unused
zero form. Example:

```c
aoahid_mouse_options mouse = {0};
mouse.struct_size = sizeof mouse;
mouse.reserved = 0;
mouse.report_id = (aoahid_report_id_option){0, 0, {0, 0, 0}};
mouse.button_count = 3;
mouse.x = (aoahid_integer_field){-127, 127, 8};
mouse.y = (aoahid_integer_field){-127, 127, 8};
mouse.enable_wheel = 0;
mouse.wheel = (aoahid_integer_field){0, 0, 0};
mouse.enable_pan = 0;
mouse.pan = (aoahid_integer_field){0, 0, 0};
```

The ranges above are a caller selection. The factory proves that relative axes
can represent negative, zero, and positive values; it does not choose them.

## Current-USB Mode A and legacy ABI members

Every example selects `AOAHID_START_CURRENT_USB_MODE`. The library sends no AOA
identification strings, deprecated audio request, or `ACCESSORY_START`, and it
does not wait for a new VID/PID. A physical port path remains useful for stable
discovery identity, but it is not used for a Mode-B re-enumeration loop.

The public `AOAHID_START_ACCESSORY_MODE` value and former Mode-B option members
remain ABI tombstones. C, Python, and C# examples leave those members in their
zero-initialized state; the Rust FFI structure must spell out the retained
fields to construct the exact C layout and sets each one to zero. Selecting the
legacy mode returns `AOAHID_ERR_UNSUPPORTED` before USB I/O. **[Project policy;
ABI compatibility only]**

## Bindings

- Python: call `aoahid.load(explicit_path)` and populate `ctypes.Structure`
  product/target fields yourself. Zero-valued Device tuning fields are handled
  by the native API. The binding test proves `struct_size` begins at zero.
- C#: populate the sequential structs, using unmanaged UTF-8 storage whose
  lifetime covers the native call. P/Invoke uses C calling convention.
- Rust: populate `aoahid_sys` `repr(C)` structures. The safe wrapper owns specs
  and provides a typed keyboard-node reference, but does not construct options.

All bindings expose native result codes. On error, inspect the C thread-local
detail on the same calling thread.

## Thread orchestration primary sources

These sources support only the language/host threading calls used by the
examples. They do not establish any AOA, HID, or Android behavior. Retrieved
2026-08-27. **[Official language or host-platform API]**

| Scope | Official source | Section or symbol used |
|---|---|---|
| POSIX hosts | https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_create.html and https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_join.html | `pthread_create`, `pthread_join` **[POSIX requirement]** |
| Windows hosts only | https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/beginthread-beginthreadex?view=msvc-170, https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread, and https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject | `_beginthreadex` for a worker that uses the CRT; `WaitForSingleObject` and `CloseHandle` for its returned handle **[Windows-only platform requirement]** |
| Python 3.9 minimum and CI's Python 3.13 | https://docs.python.org/3.9/library/threading.html and https://docs.python.org/3.13/library/threading.html | `threading.Thread.start`, `Thread.join`, non-daemon cleanup **[Python API contract]** |
| .NET | https://learn.microsoft.com/en-us/dotnet/api/system.threading.thread.start?view=net-8.0 and https://learn.microsoft.com/en-us/dotnet/api/system.threading.thread.join?view=net-8.0 | `Thread.Start`, `Thread.Join` **[.NET API contract]** |
| Rust | https://doc.rust-lang.org/std/thread/fn.scope.html | `std::thread::scope` and joined scoped workers **[Rust standard-library contract]** |

The published Python wheel and NuGet package contain declarations only. The
Python caller passes the exact shared-library path to `aoahid.load`; the .NET
native loader must resolve the name `aoahid`. In both cases, install the
version-matched shared native archive and its libusb runtime separately.
