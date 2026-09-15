# libaoahid - Architecture and Design Specification

**Status:** design baseline for implementation
**Normative source:** `docs/AOA_HID_GUIDE.md` (*Android Open Accessory 2.0 HID: Complete Implementation Guide*, verification date 2026-08-27)
**Predecessor:** `libaoa_touch` v1 - touch-only, superseded. Its transport techniques are carried forward; its API, its option model and its documentation are not.

---

## 0. What this document is

This is the complete design the implementation must follow. It fixes the object model, the threading model, the option model, the profile catalog, the automatic-behavior layer, the error model, the file layout, and the build and release pipeline.

It does not restate the protocol. Every protocol fact comes from `docs/AOA_HID_GUIDE.md` and is referenced by section number, for example *(guide §5.1)*. Where this document and the guide disagree, **the guide wins** and this document is a bug.

### 0.1 Goals

1. Cover every HID device profile that the guide classifies as expressible and carryable over AOA Input reports.
2. Keep the steady-state send path bounded and expose a caller-poll mode that
   avoids a libaoahid event thread. Measure allocation, synchronization,
   wakeups, latency, and jitter at their stated boundaries instead of inferring
   whole-stack costs from the library design. `LATENCY.md` records the
   deterministic checks and the real-backend measurements still required.
3. Automate every behavior that exists only because Android and the Linux HID/Input stack behave the way they do, so the caller never has to know them.
4. Expose every product- and target-defining value the HID descriptor can carry
   as a caller-set parameter, with no profile presets or inferred values. Keep
   the small set of documented host-transport tuning fallbacks separate.
5. Support multiple Android devices simultaneously, and multiple logical HIDs per Android device simultaneously.
6. Explain itself: every non-obvious decision carries a comment saying why it is that way.

### 0.2 Non-goals

- Output reports, Feature reports, keyboard LEDs, rumble, haptics, force feedback. The specified AOA gadget is Input-only *(guide §0.3, §7.1)*. These are absent from the API entirely, not present-and-failing.
- Switching the selected Android device into Accessory Mode. The runtime sends
  requests 51 and 54-57 only; it never sends requests 52, 53, or 58 and never
  waits for re-enumeration. The old public mode value and option members remain
  ABI tombstones and do not re-enable that path.
- Profiles whose whole purpose requires the above: LED (page 0x08), Haptics (0x0E), PID (0x0F), Lighting (0x59), FIDO (0xF1D0), HID Sensors (0x20), Braille (0x41) *(guide §11, §21)*. The library does not generate descriptors for them.
- Android-side software. This is a host-side accessory library.
- Guessing a product or target contract. An unset Usage, range, width, Report
  ID, descriptor/EP0 policy, or exact-target parser policy is an error. Only
  the explicitly listed host-transport tuning zeros in §4 select fallbacks.

---

## 1. Naming and repository identity

| Item | Value |
|---|---|
| Repository / library | `libaoahid` |
| C ABI prefix | `aoahid_` |
| Macro / enum prefix | `AOAHID_` |
| C++ namespace | Public wrapper symbols are in `aoa`; internal implementation namespaces are not public API. |
| Public header | `include/aoahid.h` (C), `include/aoahid.hpp` (optional C++ wrapper, header-only) |
| License | MIT for libaoahid. Packaged libusb 1.0.30 is LGPL-2.1-or-later and remains a dynamic dependency of both libaoahid variants; see `THIRD_PARTY_NOTICES.md`. |
| Minimum standards | C99 for consumers, C++20 for the implementation, CMake 3.20 |

No public symbol may contain `default`, `preset`, `standard`, `typical`, or `auto_config`. CI enforces this with a lint over the exported symbol list (§13.4). The word *auto* is permitted only for the behavior layer of §8, which automates protocol obligations, never parameter choices.

---

## 2. Layering

```
+--------------------------------------------------------------+
| L4  bindings/   Python (ctypes), C# (P/Invoke), Rust (FFI)    |
+--------------------------------------------------------------+
| L3  api/        stable C ABI, handle validation, error detail |
+--------------------------------------------------------------+
| L2  profiles/   descriptor + layout + state machine per       |
|                 profile; all Android-specific automation      |
+--------------------------------------------------------------+
| L1  hid/        HID item writer, descriptor builder,          |
|                 report layout, static validator. Pure.        |
+--------------------------------------------------------------+
| L0  transport/  libusb, AOA requests 51-57, transfer pool,    |
|                 event pump, lifecycle                         |
+--------------------------------------------------------------+
```

Rules, per *guide §22*:

- `libusb.h` is included by L0 only. It never appears in a public header, and no libusb type crosses the C ABI.
- L0 never interprets HID content. It sees an AOA HID ID, a byte span, and a length.
- L1 and L2 perform no I/O, spawn no threads, and allocate nothing after construction.
- L2 never mentions libusb.
- A registered node owns its descriptor bytes, its AOA HID ID, its accepted report-length set, and its capability manifest. A serializer for profile A must be impossible to call with a node of profile B; the C ABI enforces this with a `kind` tag checked on every call, and the C++ wrapper enforces it with distinct types.

---

## 3. Object model

```
aoahid_spec             an immutable, validated profile definition.
                        Built once, owned by the caller, registerable on
                        any number of devices and contexts (§3.4).

aoahid_context          one libusb context, one synchronization domain
   |
   +-- aoahid_device    one Android device, one USB handle, one AOA session.
   |      |             Opened, operated and closed entirely on its own.
   |      +-- aoahid_node   one AOA HID ID = one logical HID = one live
   |      +-- aoahid_node   instance of one aoahid_spec. Registered and
   |      +-- aoahid_node   unregistered independently, at any time.
   |
   +-- aoahid_device    completely independent of the one above: its own
   |      +-- aoahid_node   pool, latch, IDs, state machines and lifetime
   |
   +-- (graveyard)      devices closed but not yet drained (§9.3)
```

There is no object above a device that owns several devices' behavior. Everything the library does is done to one device, or to one node of one device. Simultaneity across devices is produced by the caller issuing the same calls to each of them, and §3.5 explains why that is exactly equivalent to a broadcast without needing one.

### 3.1 Context

- Owns exactly one `libusb_context`. libusb officially supports sharing a context across devices and threads *(guide §30)*, so one context serving many phones is the normal configuration and one event pump advances all of them.
- Is the **synchronization domain**. See §5.
- Owns the log sink, the event mode, and the graveyard.

### 3.2 Device

- One `libusb_device_handle`, one AOA session, one transfer pool, one sticky-error latch.
- The runtime implements only **Mode A**: requests 54-57 are sent on the
  selected device's current EP0 without `ACCESSORY_START` or re-enumeration.
  AOA 2.0 states that HID needs no new interface, while exact kernel behavior
  before `START` remains target-specific; `SOURCE_CONFLICTS.md` T-07 records the
  separate requirement, implementation observation, and project policy.
- The pool is per device rather than per node: one pool means one free list, one cache-resident set of buffers, and fair reuse across profiles. Per-node reservations (§7.4) prevent a chatty touchscreen from starving a keyboard.

### 3.3 Node

- One AOA HID ID, one descriptor, one report layout, one profile state machine.
- AOA HID IDs are `uint16_t` on the wire and are allocated by the library, monotonically, **never reused within one device session**, because unregistering is asynchronous: the kernel moves the object to `dead_hid_list` and a worker destroys it later, with no notification, so re-registering the same ID can briefly expose two nodes *(guide §5.1)*. The caller may read the assigned ID for log correlation but cannot choose it.
- An AOA HID ID is not a HID Report ID. The two are separate identifier spaces *(guide §3.1)* and the API keeps the names distinct everywhere.
- A node holds all *mutable* state: the profile state machine, the pending report, the dirty flag, the reserved slots. It holds no descriptor bytes of its own; it points at a spec.

### 3.4 Spec - one HID definition, many devices

Building a descriptor means running the item writer, computing the layout and running the full static validator. None of that depends on which phone the descriptor is going to, so doing it once per phone is wasted work and an opportunity for two phones to end up with subtly different descriptors.

`aoahid_spec` is therefore a separate, caller-owned object:

- Built once from an options struct, validated once, then **immutable**.
- Contains the descriptor bytes, the `ReportLayout` for every report ID, and the capability manifest.
- Contains **no** mutable state, no device pointer, no context pointer.
- Reference-counted, and safe to read from any thread precisely because nothing in it changes. It is the one object in the library that is not confined to a synchronization domain.
- Registerable on any number of devices, in any number of contexts, simultaneously.

`aoahid_node_open(device, spec, node_options)` binds a spec to a device. Any number of phones receiving the same keyboard definition run the builder and validator once, transfer identical descriptor bytes, and are guaranteed byte-identical because they literally share the bytes.

### 3.5 Per-device control, and why that is all you need

Every operation in the library acts on exactly one device, or on one node of one device. There is no batch call, no broadcast handle and no fan-out object, because none of them would be able to do anything the caller cannot do with a loop, and each of them would take away control the caller might want.

What is per-device and independently available at any time, while other devices keep running untouched:

| Operation | Granularity |
|---|---|
| Enumerate and probe | per candidate device |
| Open a Mode-A session on the current EP0 | per device, with its own options |
| Register a node from a spec | per device, at any time after open |
| Every profile operation - keys, buttons, axes, contacts, pen state | per node |
| Submit | per node |
| Read the latched error and the link state | per device |
| Unregister a single node, leaving the rest running | per node |
| Register a replacement node on a live device | per device |
| Close the session | per device |
| Reopen after an unplug | per device |

A device that is unplugged, errors out, or is deliberately closed affects nothing else. Its pool, its error latch, its HID IDs, its node state machines and its lifetime are entirely its own, and no other device's timing changes because of it.

**Identical input produces identical behavior.** A node's wire output is a pure function of its spec and the sequence of calls made against it. Two devices holding the same spec, given the same call sequence, emit byte-identical reports - identical because they share the spec's descriptor bytes (§3.4) and because no per-device state feeds back into serialization. So a caller who wants N phones to do the same thing at the same time writes the loop:

```c
for (size_t i = 0; i < device_count; ++i)
    aoahid_kbd(keyboard_node[i], AOAHID_USAGE_KEY_A, 1U);
for (size_t i = 0; i < device_count; ++i)
    aoahid_node_submit(keyboard_node[i]);
```

Submitting in a second pass, after every state machine has been updated, keeps one device's submit out of the next device's critical path and is the same ordering a built-in broadcast would have had to use. It is written out here rather than hidden inside the library so that the caller can also choose not to do it - skip a device, send a different key to one of them, or handle one device's `BUSY` without stalling the others.

A caller who wants a fan-out helper is welcome to write a ten-line one. The library does not, because a helper that decides what to do when device three fails mid-fan-out has made a policy choice on the caller's behalf, and §4 forbids that.

---

## 4. Option model: explicit product policy and bounded transport tuning

This is the single most invasive rule in the design and it shapes every struct.

### 4.1 Mechanics

Every options struct is laid out as:

```c
typedef struct {
    uint32_t struct_size;      /* sizeof(this struct) as the caller saw it */
    uint32_t reserved;         /* must be 0 */
    ...                        /* fields, none optional-by-omission */
} aoahid_xxx_options;
```

- `struct_size` gives forward ABI compatibility: a newer library reading an older caller's struct sees a smaller size, knows which fields exist, and rejects the call if a field it now requires is absent. A mismatched or implausible `struct_size` is `AOAHID_ERR_PARAM`.
- The caller zero-initializes the struct and fills every product/target field.
  The implementation validates a local copy and never mutates caller storage.
- Where zero is not meaningful for a product/target value (for example a field
  width or required count), zero means "not set" and produces
  `AOAHID_ERR_UNSET_FIELD`.
- In `aoahid_device_options`, zero selects these bounded **[project policy]**
  host-transport fallbacks: control/send timeout 500 ms, descriptor fragment 64
  bytes, transfer pool 8 slots, maximum report 1024 bytes, close-drain timeout
  1000 ms, first-report 20 total attempts, and first-report backoff 1000
  microseconds. A timeout is a backend failure deadline, not a successful-I/O
  delay and not a libusb recommendation. Explicit nonzero values pass through.
- `aoahid_node_options` with `has_reserved_slots == 0` and
  `reserved_slots == 0` selects no reservation. A positive reservation remains
  explicit and must satisfy the Device-pool rules.
- Other cases where zero **is** meaningful (`logical_minimum`, a button index,
  or a disabled optional product field) retain their explicit `has_*` or
  `enable_*` contract. A flag must be exactly 0 or 1.
- There is no `aoahid_*_options_init()`, no `_default()`, no `_preset()`, no profile template. Documentation carries fully worked examples instead, in `docs/EXAMPLES.md`, as prose the caller copies and edits rather than as code the caller calls.

### 4.2 Diagnosing an unset field

Rejecting a struct with a bare error code would make this model hostile. Every validation failure records the offending field:

```c
typedef struct {
    int32_t     code;            /* AOAHID_ERR_* */
    const char *field;           /* "touchscreen.contact.pressure.logical_maximum", or NULL */
    const char *reason;          /* static string, never NULL */
    int32_t     libusb_status;   /* raw libusb code, or 0 */
    int32_t     aoa_request;     /* 51..57, or 0 */
    uint16_t    hid_id;
    uint16_t    report_id;
    uint32_t    offset;
    uint32_t    length;
} aoahid_error_detail;

AOAHID_API const aoahid_error_detail *aoahid_last_error(void); /* thread-local */
```

The detail is thread-local and valid until the next failing call on the same thread. Strings are static; nothing is freed.

### 4.3 What the caller must specify

Non-exhaustive, to show the scale of the rule:

- Context: event mode, log level, log sink.
- Device: the explicit current-USB startup token, whether send-time validation
  runs, interface policy, four byte-layer policies, and five exact-target HID
  parser policies. Host-transport tuning may be explicit or use only the zero
  fallbacks listed in §4.1. Former Mode-B fields remain only to preserve the
  public ABI layout and are not runtime configuration.
- Every profile: every usage, every logical minimum and maximum, every report size in bits, every report count, every physical range and unit if declared at all, every report ID, and every enable flag for every optional field.

Bit widths are never inferred from a range, and ranges are never inferred from a bit width. Both are stated and the validator proves they agree (§6.3).

---

## 5. Threading and the event model

### 5.1 Domain rule

**A context is a synchronization domain.** In caller-poll mode, one caller must
serialize everything under that context: devices, nodes, pools, latches, and
state machines. The implementation then selects null libaoahid Context/Node
mutex pointers. This statement is deliberately limited to libaoahid; the real
libusb backend and operating system may still allocate and synchronize.

This is not a limit on how many phones can be driven: one caller can drive any
number of devices in one context. A caller that needs independent scheduling
creates separate contexts. The one object exempt from the Context domain rule
is `aoahid_spec` (§3.4), because it is immutable.

Two ways to use multiple threads:

1. **One context per thread.** Threads do not share a libaoahid synchronization
   domain, and each context has its own event pump. Whether this has lower
   latency on a target depends on its scheduler and USB backend and must be
   measured.
2. **One context, one driving thread, many devices.** One pump advances every
   device. This avoids one libaoahid event thread per device, but its target CPU
   cost and jitter must still be measured *(guide §30)*.

Mixing - calling into one context from two threads without external synchronization - is undefined and is documented as such.

### 5.2 Event mode is a required choice

`aoahid_context_options.event_mode` must be one of:

- `AOAHID_EVENT_CALLER_POLL` - the library starts no event thread. Completions
  are reaped inside `aoahid_context_poll()` and, opportunistically, after an
  accepted submit. In the prewarmed deterministic hot-path test, the selected
  libaoahid Context/Node mutex pointers are null and the instrumented C++
  allocation count is zero. Real libusb and operating-system work remains and
  target latency is not inferred from that test.
- `AOAHID_EVENT_INTERNAL_THREAD` - the context starts one event thread. libusb
  permits submitting from one thread while another handles events; libaoahid
  therefore selects its Context, Node-completion, and transport mutexes. The
  thread blocks in the event handler while idle and is explicitly interrupted
  for teardown. No fixed per-submit timing cost is claimed without a target
  benchmark.

The mode is fixed at context creation so the hot path has no mode branch.

### 5.3 Pump fairness

With several devices on one context, `libusb_handle_events_timeout_completed()` advances all of them, so a device that is not being flushed still makes progress as long as *some* device is driven. If no device is driven, nothing progresses; the caller must still call `aoahid_context_poll()` *(guide §30)*. The header says so where `poll` is declared.

---

## 6. L1 - HID core

### 6.1 Item writer

A sequential writer over a caller-supplied fixed buffer with sticky failure, carried over from the predecessor. It emits HID short items *(HID 1.11 §6.2.2.2)* in the smallest encoding that survives the parser's sign extension: a field is unsigned when both Logical Minimum and Logical Maximum are non-negative, so with a minimum of 0 the value 65535 fits in `0x26 FF FF` and a four-byte item is not required *(guide §1, §10.3)*. The predecessor's rule of "smallest size that stays positive when read as signed" is retained but is now applied per item using the *declared* minimum, not globally.

Long items are not emitted. Nothing in the profile catalog needs them.

### 6.2 Descriptor builder

Profiles do not write raw bytes. They describe their collections declaratively and the builder emits items and, in the same pass, produces the `ReportLayout` that the serializer uses. Descriptor and layout are generated together so they cannot disagree - the predecessor's most valuable structural decision, and it is kept and generalized.

`ReportLayout` records, per report ID: total wire length, whether an ID prefix is present, and for every field its byte offset, bit offset, bit width, signedness, logical range, and endianness. Serialization is then straight stores with no HID knowledge at runtime.

### 6.3 Static validator

Runs on every generated descriptor, including caller-supplied raw ones, before registration *(guide §25)*:

| Check | Failure |
|---|---|
| Descriptor length outside 1-65535 | `OVERFLOW` |
| Descriptor length above the caller's stated portability policy | `OVERFLOW`, with a message saying this is a policy, not an AOA wire limit |
| Report ID 0 declared, or first ID declared after a Main item, or ID and non-ID layouts mixed | `PARAM` |
| Logical minimum/maximum disagrees with serializer signedness | `PARAM` |
| Report Size × Report Count disagrees with the emitted report bits | `INTERNAL` |
| A field spans more than four bytes, or a 32-bit field does not start on a byte boundary | `PARAM` |
| Wire length ≠ ceil(data bits / 8), or non-zero trailing pad bits | `PARAM` |
| A top-level collection is not an Application collection, or one report crosses two TLCs | `PARAM` |
| Collection nesting unbalanced, or global state stack corrupt | `INTERNAL` |
| Profile requires Output or a Feature response | `UNSUPPORTED` |
| Field count in one report type + ID exceeds the caller's stated kernel-field policy | `OVERFLOW` |

Explicit Constant padding is emitted for readability, and the validator also accepts specification-compliant implicit zero padding in raw descriptors *(guide §25)*.

Descriptor byte-level limits are kept in four separate, separately-named policy fields so they are never conflated *(guide §6, §44)*: AOA wire limit (1-65535), Linux UAPI/transport policy (commonly 4096), Android EP0 data buffer, and libusb/OS control buffer minus the 8-byte setup packet.

### 6.4 Raw escape hatch

`aoahid_spec_create_raw()` takes caller-supplied descriptor bytes plus an
explicit table of accepted report IDs and lengths, runs the same validator, and
refuses unless the raw acknowledgement fields exclude Output and Feature
transport. The immutable result is registered through the ordinary
`aoahid_node_open()` path. Its capability manifest reports
`android_status = unknown` (`SOURCE_CONFLICTS.md` D-02).

---

## 7. L0 - transport

### 7.1 Discovery

`GET_PROTOCOL` (request 51, IN, two little-endian bytes) classifies every candidate *(guide §2)*:

| Result | Classification |
|---|---|
| STALL / `LIBUSB_ERROR_PIPE` | `NOT_AOA` - an ordinary negative probe, never a fatal I/O error |
| Exactly two bytes, value 0 | `NOT_AOA` |
| Short transfer | `NOT_AOA`, candidate excluded |
| Value 1 | `ERR_VERSION` - AOA 1.0, no HID |
| Value 2 | usable |
| Value > 2 | usable only when the caller set `accept_future_protocol_versions = 1`, and logged |

Enumeration reports bus, port path, address, physical VID/PID, serial where readable, and product string, in stable bus/port order so a device identifier survives a reboot of the phone.

### 7.2 Registration

Per *guide §5.1*:

- `REGISTER_HID` (54) carries the HID ID in `wValue` and the total descriptor length in `wIndex`.
- `SET_HID_REPORT_DESC` (56) fragments must arrive strictly ascending with `wIndex` exactly equal to the bytes already accepted. Duplicates, gaps, rewinds and reordering are rejected by the gadget, so the sender never retries a fragment out of order; a failed fragment aborts the whole registration.
- A nonzero fragment size is an explicit caller policy; zero selects the
  documented 64-byte project-policy fallback. It is not an AOA, USB, or libusb
  constant.
- After the final fragment the kernel registers the HID **asynchronously** and there is no ready callback. The first `SEND_HID_EVENT` may therefore STALL. Only the first event of a node is eligible for a bounded retry with backoff, using either explicit nonzero tuning or the documented zero fallback. Any later STALL is an error *(guide §5.1, §26)*.
- A successful descriptor transfer does not prove `hid_parse_report()` succeeded. Diagnostics must distinguish "device removed" from "descriptor rejected" and say when they cannot.

### 7.3 Send path

One report per control transfer, never fragmented, because request 57 has no offset or sequence field *(guide §6)*.

```
[ setup: bmRequestType=0x40, bRequest=57, wValue=HID ID, wIndex=0, wLength=N ]
[ report: N bytes                                                            ]
```

The implemented send-path boundaries are:

1. **Preallocated transfer pool with a LIFO free stack.** Device open creates the
   pool, per-slot payload buffers, libusb transfer objects, and supporting
   containers. Acquire and release are O(1); this is not a one-allocation claim.
2. **Direct generated-profile serialization.** Generated state serializers write
   into the acquired payload buffer. Raw reports require one copy because an
   asynchronous transfer cannot borrow caller-owned storage.
3. **Partially prefilled setup packets.** Request type, request 57, and index are
   constant. Because a Device pool is shared by Nodes, HID ID and report length
   are written at acquisition.
4. **Dirty-state suppression.** A state update that changes nothing leaves the
   node clean and produces no USB transfer.
5. **Post-submit event handling.** Caller-poll submission uses a zero-timeout
   event-handler call only after an accepted asynchronous submit. Internal-thread
   mode uses its blocking event loop.
6. **Latched completion errors.** A failure reported asynchronously surfaces at
   a later submit or poll. Device removal is sticky; state consumption follows
   the accepted/completed result rules tested by the integration suite.
7. **Precomputed layouts and endian-explicit stores.** Send-time serialization
   does not parse HID descriptors and never copies a C/C++ structure image onto
   the wire *(guide §24.2)*.
8. **Immutable reusable specs.** The item writer and static validator run during
   spec construction; registering one spec on multiple devices transfers the
   same descriptor bytes.
9. **Explicit interface policy.** Device-recipient EP0 routing is a backend
   matter. The library claims no interface for `NONE`; for `EXPLICIT` it claims
   exactly the caller-supplied interface and releases only that interface. It
   never assumes interface 0 *(guide §27.2, §31; `SOURCE_CONFLICTS.md` T-01)*.
10. **Bounded logging claim.** libaoahid itself emits only through the configured
    sink and never logs HID payload bytes by default. libusb may still write
    diagnostics under its own build or environment controls; see
    `SOURCE_CONFLICTS.md` T-15.

`LATENCY.md` records the deterministic allocation, synchronization, submit,
wakeup, cancellation, and TSan checks and their explicit limitations.

On pool exhaustion, submit returns `AOAHID_ERR_BUSY` without blocking, after one opportunistic reap. Blocking is available only through an explicit blocking submit variant with a caller-supplied deadline. Stale movement may be coalesced by the state machine; key, button and contact-lifecycle transitions are never dropped *(guide §29.1)*.

### 7.4 Reservations

Zero-initialized Node options (`has_reserved_slots=0`, `reserved_slots=0`) make
no reservation. An explicit reservation sets `has_reserved_slots=1`; because a
Node permits one in-flight report, `reserved_slots` is then zero or one.
Reserved slots are available only to that Node. The sum of positive
reservations remains below the Device pool size so at least one slot is shared;
invalid combinations return `AOAHID_ERR_PARAM`.

### 7.5 Completion handling

Per *guide §29.2*. `actual_length` for a control transfer is the data-stage length and excludes the 8-byte setup packet, so it is compared against `N`:

| Status | Action |
|---|---|
| `COMPLETED`, `actual_length == N` | success |
| `COMPLETED`, `actual_length != N` | `SHORT_TRANSFER` |
| `CANCELLED` | normal terminal state during close |
| `NO_DEVICE` | sticky fatal, nothing is ever submitted again |
| `STALL` | bounded retry only for a node's first report; otherwise `STALL` |
| `TIMED_OUT` | `TIMEOUT` |
| `ERROR` | `IO` |
| `OVERFLOW` | `OVERFLOW` |

No user callback runs inside the libusb callback. Results are latched or queued.

---

## 8. L2 - profiles and the automatic behavior layer

This is what makes the library "easy" without making it opinionated.
Product-defining parameters are never chosen for the caller. Each rule below
is classified by its evidence label: USB/HUT requirements and specified
Android/Linux behavior remain distinct from the library's explicit project
policy.

### 8.1 Cross-profile automation

- **Report ID prefixing.** If a node's descriptor uses Report IDs, every report carries the one-byte prefix; if it does not, no zero byte is prepended *(guide §3.1)*. The caller never writes it.
- **Neutral state on close.** Before a node is unregistered, the library emits the profile's neutral report, because a HID device that disappears mid-input leaves Android believing the last state is still current: keys stay down, buttons stay held, fingers stay on the screen.
- **Freeze on loss.** Once `NO_DEVICE` is sticky, state machines stop accepting input and every call returns `NO_DEVICE` rather than accumulating state that will never be sent.
- **Send-time validation** *(guide §26)*, on when the caller sets `validate_reports = 1`: node/profile identity, prefix and value correctness, exact report length for the selected ID, every field inside its logical range with matching signedness, lifecycle correctness, and no fragmentation.

### 8.2 Touchscreen

The predecessor's finger state machine is the reference implementation of this section and is generalized, not replaced.

- **Three-phase contact lifecycle: `None -> Down -> Up -> None`.** The `Up` frame is mandatory and is the phase a naive implementation forgets. A contact cannot simply vanish from the report: Contact Count is the number of *valid* slots, so a contact that disappears is never observed to go up. It must appear once more with Tip Switch cleared, still counted, and only then be dropped *(guide §17.4)*.
- **Contact Count is derived per packet.** The first packet of a fixed-MT frame
  carries the total emitted-record count and continuation packets carry zero,
  matching the audited Linux frame accumulator. After the final Up, a
  no-contact touchscreen frame is not sent (`FACT_AUDIT.md` A-13).
- **Coordinates are retained across the lift.** The release frame reports the contact where it was let go, not at the origin.
- **Contact ID is stable for the life of a contact** and is reported with the final X/Y and `Tip Switch = 0` on release *(guide §1, §17.4)*.
- **Pressure is clamped to at least 1 while the tip is down**, when pressure is
  declared and the descriptor proves that 0 and 1 are representable. Android's
  documented convention is nonzero for contact and zero otherwise; the library
  does not claim that every zero-pressure target emits no events
  (`FACT_AUDIT.md` A-16).
- **Slot compaction.** Only valid contacts occupy leading blocks, contiguously; the remainder of the fixed report is zeroed. A fixed-length report with compacted contacts is the layout the generic multitouch path handles most predictably.
- **Scan Time**, when the caller enables it, is maintained by the library at the declared resolution and unit, because a monotonically advancing counter is the point of the field and a caller-supplied one is a bug farm. Behavior differs across Android releases *(guide §17.5.1)*, which the header records.
- **Multi-packet frames**, when the caller enables them, split a frame with more contacts than one report can carry, carrying the total count in the first packet.
- **Explicit lift on close**, so no finger is left down when the node disappears.

Configurable and *never* chosen by the library: resolution, contact count 1-16 *(the 16 figure is an Android `MotionEvent` application policy, not a HID or kernel limit - guide §6)*, protocol form (one-contact MT, fixed MT, or target-specific pure single touch), pressure presence and range, width and height presence and range, azimuth, orientation, contact-count-maximum feature declaration, Report IDs, and every logical range.

`PureSingleTouchTargetSpecific` requires the caller to set `target_verified = 1`, because pure ST is not guaranteed to be classified DIRECT *(guide §24)*.

### 8.3 Keyboard and keypad

- **Slot management.** The caller presses and releases usages; the library maintains a one-bit Variable field per declared key.
- **Full NKRO, no overflow.** Every declared nonmodifier Usage is its own bit, so any number of simultaneously pressed keys up to the declared range is reported at once. There is no Array, no slot count, and no `ErrorRollOver` (0x01) overflow encoding to inject or recover from.
- **Modifier separation.** Usages 0xE0-0xE7 are routed to the modifier byte automatically and never into the bitmap.
- **Release-all on close**, because a stuck modifier on Android is invisible and miserable to debug.
- **No LED, no Output.** Not exposed, so it cannot be attempted *(guide §13.2)*.

Configurable: declared Usage interval and Report ID. Consumer Control is a
separate Spec/Node.

The repository provides no text-to-Usage translator. Text interpretation
depends on the target key layout, KCM, locale, and IME and is not inferred
*(guide §13.2)*.

### 8.4 Mouse, pointing stick, trackball

- **Delta splitting.** A relative movement larger than the declared logical range is split across consecutive reports automatically. Saturating instead would silently lose distance, and a caller who chose 8-bit deltas should not have to implement this.
- **Deltas are consumed on send.** After a report is submitted the accumulated delta resets to zero, so a repeated flush does not move the pointer again. This is the most common bug in hand-written relative-pointer code.
- **Buttons are level, not edge.** Every report carries current button state, releases are explicit, and all buttons are released on close *(guide §14.3)*.
- Wheel and pan follow the same splitting and consumption rules.

Configurable: delta ranges and widths, button count, Wheel and Pan presence and
ranges, and Report ID. The generated public surface does not declare a
Resolution Multiplier Feature.

### 8.5 Consumer, System, Camera, and Telephony controls

- Each caller-allowed Usage is emitted as its own sparse one-bit Variable field;
  the library never invents an intervening Usage range.
- The caller explicitly selects one of the six supported HUT semantics:
  Selector bitmap, OOC toggle, OOC maintained, MC, OSC, or RTC. Descriptor
  flags follow that semantic; unsupported LC/DV/NA forms are not converted into
  key-like taps.
- `press` asserts the selected field, `release` emits zero, and `tap` observes
  both reports. Zero re-arms OSC, stops RTC, deasserts MC/maintained OOC, leaves
  toggle OOC without an extra toggle, and clears a Selector bit. Edge guards
  prevent an unaccepted transition from being overwritten.
- Camera Auto-focus/Shutter are audited OSC fields. Consumer, System, and
  Telephony require caller allow-lists, semantics, and expected target evidence.
  OS or `MediaSession` interception remains target behavior.

### 8.6 Gamepad, D-pad

- **Canonical Hat state.** The Android-candidate Hat is fixed to logical 0..7
  in four bits with Physical 0..315 Degrees. Boolean directions map to eight
  clockwise values and no direction to Null 15; opposite pairs are rejected.
  Raw D-pad fields remain independent OOC bits.
- **Neutral on close.** Buttons clear, a canonical Hat uses Null 15, raw D-pad
  bits clear to zero, and every axis uses its caller-declared in-range neutral;
  no midpoint is inferred.
- **Axis validation** against declared range and signedness, with no silent wrapping.
- Trigger axes require the caller to state the Usage, the expected Linux code and the expected Android axis explicitly *(guide §24)*, because this mapping is where portability is actually lost.

Configurable: the full axis list with per-axis usage, bit width, signedness and
range; button count and usage base; hat presence; whether the D-pad is a hat or
buttons; Report IDs. Only a Game Pad with the canonical Hat and a contiguous
Button range beginning at 1 with at least five fields is a portable candidate
under the audited Android 17 contract. Raw/no-D-pad Gamepads remain
conditional. Simulation Controls usages are permitted inside a Generic
Desktop Game Pad top-level collection *(guide §11, page 0x02)*; that HUT
placement alone does not establish Android portability.

### 8.7 Pen and stylus

- **In Range and Tip are constrained together.** Tip down implies in range; hover is in-range with tip clear; departure is a single report with both clear. Illegal combinations are rejected rather than transmitted.
- **Tool-end ordering.** `eraser` selects the Invert tool end and may coexist
  with Tip contact. When enabled, the descriptor orders Invert before Tip and
  In Range so the audited Linux handler sees the selector before choosing PEN
  versus RUBBER.
- **Automatic departure on close.**
- Direct (on-screen) and indirect (tablet) modes change the collection and property strategy, not just the coordinate space, and are separate profile modes *(guide §19, §24)*.

Configurable: coordinate ranges, pressure presence and range, tilt, twist, barrel buttons, eraser, hover reporting, Report IDs.

### 8.8 Conditional profiles

Implemented, with `android_status = conditional` in the manifest and a header comment explaining the condition: barcode/MSR keyboard wedge *(guide §11, pages 0x8C/0x8E)*, camera control for the small supported key subset *(page 0x90)*, telephony for the small supported subset *(page 0x0B)*, and battery telemetry *(guide §20.2)*.

### 8.9 Capability manifest

Every node exposes *(guide §24.1)*:

```
input_supported             always 1
output_supported            always 0
feature_transport_supported always 0
android_status              portable | conditional | custom_system_only | unsupported | unknown
report_ids[]                declared IDs and their exact wire lengths
descriptor_bytes            length
```

Being able to generate a descriptor is not `portable`. The README's support table is generated from these manifests so documentation cannot drift from behavior.

---

## 9. Lifecycle

### 9.1 Open

1. Enumerate, probe `GET_PROTOCOL`, select by serial or bus/port path.
2. Proceed on the selected device's current EP0. The runtime does not send
   requests 52, 53, or 58 and does not reopen a re-enumerated device.
3. Construct and validate each immutable Spec before opening the Node that uses
   it. Node creation is a sequential public operation, so an earlier Node may
   already be registered if construction or registration of a later Node
   fails; the caller closes the earlier Node or the Device explicitly.
4. Allocate the pool when the Device opens. Register Nodes in caller-selected
   order, transferring each descriptor completely before that Node's first
   report.

### 9.2 Run

Submit, poll, read latched errors. Nothing else.

### 9.3 Close

Ordered per *guide §31*:

1. Set the Device closing flag and stop accepting new submissions.
2. For each Node, finish its already-submitted report or immutable touch frame
   and consume the terminal completion.
3. If the device is still present, serialize and submit that Node's neutral
   state, wait for its terminal callback, then send `UNREGISTER_HID`. A
   successful control transfer means only that request 55 was accepted, not
   that the target input node has been synchronously destroyed.
4. Clear that Node's pool reservation and release its storage only after its
   callbacks have finished.
5. After the orderly Node loop, call `libusb_cancel_transfer()` for any
   residual asynchronous transfers left by an error or expired drain budget,
   and handle events until every transfer reaches a terminal callback.
6. Release only interfaces that were explicitly claimed; close the handle;
   exit the context only with its last owner.

If the drain budget expires with transfers still in flight, `aoahid_device_close()` returns `AOAHID_CLOSE_PENDING` and the **entire** device object - buffers, transfers, callback state, handle, and its reference to the context - moves to the context graveyard. Nothing is freed. Freeing the object while leaking only the pool, as the predecessor did, is forbidden: a late callback writes into freed memory *(guide §31)*.

`aoahid_context_poll()` continues to drain the graveyard. `aoahid_context_destroy()` returns `AOAHID_CLOSE_PENDING` while the graveyard is non-empty; `aoahid_context_destroy_blocking(ctx, timeout_ms)` waits. A context that cannot be drained is a wedged backend and is reported as such rather than papered over.

---

## 10. Error model

```c
AOAHID_OK, ERR_PARAM, ERR_UNSET_FIELD, ERR_UNSUPPORTED, ERR_NOT_AOA, ERR_VERSION,
ERR_ACCESS, ERR_BUSY, ERR_NO_DEVICE, ERR_STALL, ERR_TIMEOUT, ERR_SHORT_TRANSFER,
ERR_DESCRIPTOR_REJECTED, ERR_IO, ERR_OVERFLOW, CLOSE_PENDING, ERR_INTERNAL
```

Matches *guide §32* with `ERR_UNSET_FIELD` added for §4. `NOT_AOA` is a negative probe result, not a failure. `VERSION` means a valid AOA response below the HID requirement.

### 10.1 Logging

A caller-supplied sink with a level, per context. Diagnostics carry bus and port, physical VID/PID, AOA protocol version, startup mode, AOA HID ID, Report ID, request number, offset and length, and the raw libusb status *(guide §32)*.

**Report payloads are never logged.** They can contain keystrokes, passwords,
and medical data. No public compile-time or runtime switch enables payload
logging (`SOURCE_CONFLICTS.md` T-14). libusb's own diagnostics remain subject
to its separately documented controls (T-15).

---

## 11. Multi-device and multi-node

- Many devices per context, or one context per thread: both are first-class (§5.1).
- Many nodes per device, each with its own HID ID and profile *(guide §24.3)*. A typical set is keyboard, mouse, consumer, gamepad, touchscreen and pen simultaneously; the IDs carry no Android meaning and exist to route inside the accessory implementation.
- Within one Context and physical-identity domain, HID IDs advance
  monotonically across reopen and failed registration. Reopening does not
  promise the same numeric IDs as a prior connection (`SOURCE_CONFLICTS.md`
  T-05).
- Per-device state is fully independent: no global mutable state exists anywhere in the library, so nothing is shared between devices except the libusb context they were asked to share and the immutable specs they were asked to register.

### 11.1 Running N phones with the same HID set

End to end, for N phones each receiving an identical keyboard, mouse and touchscreen set, where N is whatever the host's USB bus and file-descriptor budget allow:

1. Build three specs - keyboard, mouse, touchscreen - once (§3.4). Descriptor generation and validation run three times in total, not three times per phone.
2. Open the devices. Each `aoahid_device_open()` is a separate Mode-A call with
   its own timeouts and pool size. Put them all in one context, or split them
   across one context per thread if they are to be driven in parallel.
3. Register nodes. Each device gets its own node per spec, with its own AOA HID IDs, which need not match between devices and mean nothing outside their own device.
4. Drive them. Update every node, then submit every node (§3.5). Identical call sequences against a shared spec produce byte-identical reports, so N phones do the same thing at the same time without the library having a concept of "at the same time".
5. Handle each device on its own terms. One phone returning `BUSY` does not stop the others. One phone unplugged goes sticky `NO_DEVICE` and can be closed and reopened while the rest keep running, and rejoins by registering fresh nodes against the same unchanged specs.

Every step above is also available at a finer grain: add an eleventh phone mid-session, unregister just the touchscreen node on phone four, close phone seven while phones one through six are mid-drag. Nothing in the library couples them.

### 11.2 Hot plug and reconnection

- A device that disappears sets its own sticky `NO_DEVICE`. Its node state machines freeze rather than accumulating input that will never be sent, and every call against it returns `NO_DEVICE` immediately, so a caller looping over devices is not slowed down by a dead one.
- Closing and reopening is a per-device operation with no global effect. Within
  one Context and physical-identity domain, AOA HID IDs remain monotonic across
  reopen and failed registration because the audited gadget exposes no
  synchronous destruction barrier (`SOURCE_CONFLICTS.md` T-05).
- Specs survive everything. A reconnecting phone registers the same immutable spec it had before; nothing is rebuilt, revalidated or re-derived.
- Discovery may be re-run at any time while other devices are open, because probing a candidate touches nothing that belongs to an already-open session.

### 11.3 Count ceilings

Context, Device, and Spec collections grow dynamically subject to host memory,
USB, handle, and scheduling resources. Nodes additionally consume the finite
nonzero 16-bit AOA HID-ID space within their physical-identity domain; pool
slots and caller-selected parser/transport policies can reject a configuration
earlier. Descriptor and report limits are listed independently in
`LIMITS.md`; no single cross-layer "only ceilings" list is claimed.

---

## 12. Repository layout

```
libaoahid/
  CMakeLists.txt
  CMakePresets.json
  LICENSE
  README.md                     generated support table, quick start
  CHANGELOG.md                  Keep a Changelog format
  CONTRIBUTING.md
  SECURITY.md
  THIRD_PARTY_NOTICES.md
  .clang-format
  .clang-tidy
  .gitignore
  include/
    aoahid.h                    stable C ABI, the only required header
    aoahid.hpp                  optional header-only C++ wrapper
  src/
    transport/                  usb_device, aoa_requests, transfer_pool, event_pump, discovery
    hid/                        item_writer, descriptor_builder, report_layout, validator, usages
    profiles/                   keyboard, mouse, consumer, gamepad, touchscreen, pen,
                                barcode, camera, telephony, battery, raw
    api/                        c_api, handle_table, error_detail, logging
  bindings/
    python/                     ctypes wrapper, tests, pyproject.toml
    csharp/                     P/Invoke wrapper, csproj
    rust/                       -sys crate plus safe wrapper
  examples/
    c/, python/, csharp/, rust/
  tests/
    unit/                       descriptor golden files, layout, state machines, validator
    fuzz/                       descriptor builder, report serializer, raw validator
    integration/                mock transport, lifecycle, teardown races
    hidtools/                   Linux uhid/hid-tools parse checks
  tools/
    device-check/               on-device verification scripts: getevent, dumpsys input, test app
    symbol-lint/                exported-symbol policy check
  docs/
    AOA_HID_GUIDE.md            the current, evidence-qualified guide
    inputs/                     byte-preserved supplied source documents
    DESIGN.md                   this document
    API.md, PROFILES.md, EXAMPLES.md, LATENCY.md, LIMITS.md, TARGET_MATRIX.md, PORTING.md
  udev/
    51-aoahid.rules
  cmake/
    toolchains/, aoahid-config.cmake.in
  .github/
    workflows/                  ci.yml, release.yml, docs.yml
    ISSUE_TEMPLATE/
```

---

## 13. Build, CI, release

### 13.1 Build

CMake ≥ 3.20, targets `aoahid::aoahid`. Options: `AOAHID_BUILD_SHARED`,
`AOAHID_BUILD_STATIC`, `AOAHID_BUILD_TESTS`, `AOAHID_BUILD_EXAMPLES`,
`AOAHID_BUILD_FUZZ`, `AOAHID_SANITIZE`, `AOAHID_TSAN`, and
`AOAHID_USE_FAKE_LIBUSB`. RTTI is disabled. Exceptions remain enabled for
cold-path allocation and foreign-failure handling, and every exported C
function catches and translates them at the ABI boundary (`SOURCE_CONFLICTS.md`
C-03). Warnings are errors. Exported symbols are restricted by a version script on ELF and
`__declspec` on Windows; nothing but the documented ABI is visible.

Installs a CMake package config and a pkg-config file.

### 13.2 Targets

Tagged native release packages target Ubuntu 22.04 glibc on Linux x86-64 and
AArch64, and Windows x64 and ARM64. CI additionally compiles and tests the
portable source on the macOS architectures listed in the workflow. A CI build
is not a claim that an unlisted platform receives a release binary.

### 13.3 CI on main/tag pushes and every pull request

Build matrix; unit, integration and fuzz-smoke tests; ASan+UBSan on Linux and
macOS; a Linux TSan gate over shared and static builds, examples, caller-poll,
and internal-thread paths; `clang-format --dry-run --Werror`; `clang-tidy`; the
symbol lint of §13.4; the hid-tools descriptor parse job on Linux; and a
documentation job that regenerates the README support table from the capability
manifests and fails if it differs from what is committed. The exact-source CI
quality job also builds Doxygen with warnings treated as errors; the separate
documentation workflow publishes the resulting Pages site from `main`.

### 13.4 Symbol and policy lint

Fails the build when an exported symbol contains `default`, `preset`, `standard` or `typical`; when a public struct lacks `struct_size`; when a public header includes `libusb.h`; or when a profile factory can succeed with a zero-filled options struct. The device tuning fallbacks do not create a profile factory or infer descriptor/product policy.

### 13.5 Release

A pushed `v*` tag builds the four listed native targets, runs the release gates,
and publishes a GitHub Release containing per-platform shared and static
archives, headers, the CMake package config, an SPDX SBOM, SHA-256 checksums,
and the changelog section for that version. The `publish` job explicitly needs
the binding-package job, so a Python wheel and NuGet package are validated
before the GitHub Release is published. Optional publication to PyPI and NuGet
is gated by a repository variable. Doxygen output is published to GitHub Pages
by a separate workflow.

### 13.6 Release gate

Per *guide §40*, the mechanical workflow gates publication on descriptor and
semantic tests; setup-packet and short-transfer tests for current-mode requests
51 and 54-57; rejection of the retained Mode-B token before USB I/O;
post-registration race, STALL retry, unregister, and re-registration tests;
asynchronous callback draining under the configured ASan/UBSan and TSan jobs;
ABI, binding, package, and release-asset validators; and a successful CI run
for the exact tagged commit. Release review additionally requires the public
API/documentation to keep Output and Feature transport unsupported, the target
matrix to be current, and source URLs and retrieval dates to be recorded; those
human evidence judgments are not claimed as a complete automated semantic
check.

The phrase "Android supported" does not appear in release notes for anything not verified at the `getevent`, `dumpsys input` and application-API layers on real hardware *(guide §37)*.

---

## 14. Testing

- **Golden descriptors.** Every profile, over a spread of option combinations, byte-compared against committed expected output, so an unintended descriptor change is always visible in review.
- **Layout property tests.** For every generated layout, every field's declared bit offset and width match where the serializer actually writes.
- **State machine tests.** The obligations of §8 are each a named test: the
  mandatory `Up` frame, first-packet active Contact Count with continuation
  Count `0`, coordinate retention across a lift, contact ID stability,
  roll-over overflow and recovery, delta splitting and consumption, hat null
  value, In Range and Tip constraints, and neutral state on close.
- **Mock transport** replacing libusb, verifying exact request numbers, `wValue`, `wIndex`, fragment offsets, ordering, and the first-report retry.
- **Fuzzing** of the descriptor builder, the report serializer and the raw validator.
- **Sanitizers**, including a teardown race suite that cancels transfers under load.
- **hid-tools** parse of every generated descriptor on Linux CI.
- **On-device**, scripted but manual: four-level verification per *guide §37* - kernel device, Android classification, event stream, application API - recorded in `docs/TARGET_MATRIX.md`.

---

## 15. Code and comment policy

The predecessor's comment style is the standard: prose in natural English that explains **why**, not what.

- Every file opens with a comment stating its responsibility and its boundary.
- Every non-obvious decision carries a comment explaining the reason, and cites the guide section where the reason comes from.
- Anything that exists only because of Android or Linux kernel behavior says
  so explicitly, so a future reader does not "simplify" it away. The `Up`
  phase, first/continuation Contact Count rules, the pressure floor of 1, the
  platform-conditional interface policy, the first-report retry, and the
  graveyard all fall in this class.
- Comments never restate the code. `i++ // increment i` is a review rejection.
- No commented-out code, no TODO without an issue number.
- American English, present tense, complete sentences.
- Public headers document every function's ownership, blocking behavior, thread-safety domain, and every error code it can return.

---

## 16. Summary of decisions taken on the caller's behalf

The library mechanically handles the following decisions. Their distinct
specification, implementation-observation, and project-policy classifications
are recorded in the audit documents:

1. AOA HID IDs are allocated monotonically within a Context and
   physical-identity domain and are not reused after asynchronous
   unregistration or reopen.
2. Report ID prefixes are inserted or omitted according to the descriptor.
3. Contact, key, button, hat, axis and pen state machines maintain the transitions Android requires, including neutral state at close.
4. Descriptor fragments are sent strictly ascending with exact offsets.
5. The first report after registration may be retried within explicit nonzero
   tuning or the documented bounded zero fallback.
6. Reports are never fragmented across AOA requests.
7. Interface claiming is OS-independent and follows the explicit policy:
   `NONE` claims nothing; `EXPLICIT` claims only the caller-selected index.
8. Nothing is freed before its libusb callback has run.

Everything else that defines the product or target - every Usage, range, width,
Report ID, profile count, event mode, descriptor/EP0 policy, and exact-target
parser policy - is the caller's and is required. The eight bounded Device
tuning fallbacks and the no-reservation Node fallback are the only omissions
accepted by this option model.
