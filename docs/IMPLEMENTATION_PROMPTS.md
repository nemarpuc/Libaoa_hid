# Implementation Prompt Set for `libaoahid`

**Target model:** ChatGPT (a long-context reasoning model). The same text works with any coding agent that can hold two reference documents plus one phase of work.

**How to use this file**

1. Start a new chat. Paste **Prompt 0** and attach `AOA_HID_GUIDE.md` and `DESIGN.md`. Do not attach the old `Libaoa_touch` sources - the guide states that the original documentation was wrong, and the old API and option model are deliberately superseded. The predecessor's techniques you need are already captured in `DESIGN.md` §7.3 and §8.
2. Then paste **Prompt 1**. When it finishes, review, then paste **Prompt 2**. One phase per message, in order.
3. If the context fills up, start a fresh chat, paste Prompt 0 again, attach both documents plus the code produced so far, and continue from the next phase.
4. Finish with the **Review Prompt** and the **Release Prompt**.

Do not merge phases. Each phase has an acceptance gate, and the gates are what keep a large generated codebase honest.

---

## Prompt 0 - Role and standing rules

> You are implementing `libaoahid`, a host-side C++20 library that drives Android devices as USB HID accessories over the Android Open Accessory 2.0 HID protocol, exposed through a stable C ABI.
>
> You have two reference documents.
>
> - `AOA_HID_GUIDE.md` is the **normative source of protocol and platform truth**. Every protocol fact, limit, and Android behavior comes from it. When you state a fact about AOA, HID, Linux, or Android, it must be traceable to a section of this guide. Never substitute your training data for it - an earlier version of this document was wrong in more than twenty places, which is why it exists.
> - `DESIGN.md` is the **normative source of architecture**. Layering, object model, threading model, option model, profile automation, error model, file layout, and CI are fixed there. If `DESIGN.md` and `AOA_HID_GUIDE.md` disagree, the guide wins and you must say so explicitly instead of quietly choosing.
>
> These rules apply to every phase and are not negotiable.
>
> **1. No defaults, no presets, ever.**
> Every option struct field must be explicitly set by the caller. There is no `_default()`, `_init()`, `_preset()`, `_standard()` or profile template function anywhere in the public API, and no internal fallback that fills in a missing value. Where zero is not a meaningful value, zero means "not set" and returns `AOAHID_ERR_UNSET_FIELD` naming the field. Where zero *is* meaningful, pair the field with an explicit `has_*` or `enable_*` flag so intent is encoded rather than inferred. Bit widths are never inferred from ranges and ranges are never inferred from bit widths; both are stated and the validator proves they agree.
>
> **2. Automate obligations, never choices.**
> The library automatically handles everything that exists only because Android, the Linux HID/Input stack, or the HID specification behaves a certain way - contact lifecycles, roll-over overflow, hat null values, report ID prefixes, descriptor fragment offsets, neutral state at close, the first-report registration race. It never picks a resolution, a range, a bit width, a usage, a timeout, or a mode. `DESIGN.md` §16 is the complete list of what you may decide for the caller. Adding to that list is a design change and requires you to stop and say so.
>
> **3. Latency and footprint are features.**
> No allocation, no locking, no logging, and no branching on configuration on the send path. `DESIGN.md` §7.3 lists thirteen specific techniques; implement all of them and put a comment on each explaining why it is there. Every hot-path function is `noexcept`. Exceptions and RTTI are disabled build-wide.
>
> **4. Comments explain why, not what.**
> Every file opens with its responsibility and its boundary. Every non-obvious decision carries a comment explaining the reason and citing the guide section it comes from. Anything that exists only because of Android or Linux kernel behavior says so explicitly, so that a future reader does not simplify it away. Never restate code in a comment. Never leave commented-out code or a bare TODO. All comments, identifiers, documentation, commit messages and error strings are in natural American English - complete sentences, present tense, no telegraphic fragments.
>
> **5. Any number of devices, sharing any number of HID definitions, is a primary use case.**
> There is no maximum number of contexts, devices per context, nodes per device, or specs. Never introduce one. Every such collection grows dynamically; a fixed-size array sized by a compile-time constant is a bug, and a hard-coded count in an example, a test or a document is a bug too. The only ceilings that may appear anywhere are the ones the protocol or platform imposes and the guide documents - the `uint16_t` AOA HID ID space, the 1-65535 byte descriptor wire length, the report length that must fit one control transfer, and the 16-pointer `MotionEvent` policy - and each must be annotated with its origin so no reader mistakes it for a library limitation.
>
> Every operation acts on exactly one device, or on one node of one device. Build no batch call, no broadcast handle and no fan-out object: `DESIGN.md` §3.5 explains why a caller's loop is exactly equivalent and strictly more controllable, and a fan-out helper that decides what to do when one device fails mid-loop has made a policy choice on the caller's behalf, which rule 1 forbids.
>
> What this demands of the implementation:
>
> - A device's session, pool, error latch, AOA HID ID allocation, node set and lifetime belong to that device alone. No state, timing or failure crosses between devices.
> - Nodes are registered and unregistered individually on a live device, at any time, without disturbing the device's other nodes.
> - A device can be opened, closed and reopened at any time while other devices keep running, and discovery can be re-run while sessions are open.
> - A dead device fails fast on every call so that a caller looping over devices is never slowed down by one that is gone.
> - A node's wire output is a pure function of its spec and the call sequence made against it, with no per-device state feeding back into serialization. This is what makes N devices given identical calls emit byte-identical reports, and it is the property that replaces a broadcast API. Test it explicitly.
>
> A profile definition (`aoahid_spec`, `DESIGN.md` §3.4) is built and validated once, is immutable, and is registered on any number of devices in any number of contexts simultaneously. A spec must never acquire a device pointer, a context pointer, or any mutable state, and per-device state must never migrate into a spec.
>
> **6. Input only.**
> The specified AOA gadget cannot carry Output reports or Feature data. Output, Feature, LEDs, rumble, haptics, and force feedback are absent from the API rather than present and failing. Do not generate descriptors for profiles whose purpose requires them.
>
> **7. Scope discipline.**
> Produce exactly what the current phase asks for. Do not scaffold future phases, do not write placeholder implementations, and do not invent extra features. If something in the phase is ambiguous or contradicts the two documents, stop and ask before writing code.
>
> **8. Output format.**
> Emit complete files with their full repository paths, never fragments or diffs against code you have not shown. At the end of each phase, list the files you created, the guide sections you relied on, and any question you had to resolve by judgment.
>
> Confirm you have both documents and can see `DESIGN.md` §16, then wait. Do not write code yet.

---

## Phase 1 - Skeleton, build system, public ABI surface

> Phase 1 of `libaoahid`. Create the repository skeleton exactly as laid out in `DESIGN.md` §12, and the public C ABI header, with no implementation behind it yet.
>
> Deliverables:
>
> - The full directory tree, with a real `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `.clang-tidy`, `.gitignore`, `LICENSE` (MIT), and a `README.md` stub that states the Input-only limitation in the first screen.
> - `include/aoahid.h`: the complete public C ABI. Every type, every enum, every option struct, every function signature, fully documented. No implementation.
> - The build must configure and produce a library target that links, with every declared function present as a stub returning `AOAHID_ERR_INTERNAL`.
>
> Requirements:
>
> - Every option struct starts with `uint32_t struct_size; uint32_t reserved;` per `DESIGN.md` §4.1.
> - The error enum is exactly `DESIGN.md` §10, and `aoahid_error_detail` is exactly §4.2.
> - The handle types `aoahid_context`, `aoahid_device`, `aoahid_node` are opaque. No libusb type, no C++ type, and no `libusb.h` include appears in any public header.
> - Every function's documentation states its ownership, whether it blocks, which synchronization domain it belongs to, and every error code it can return.
> - `event_mode` is a required field with the two values of `DESIGN.md` §5.2, and the enum documentation states the cost of each mode at the point of declaration.
> - Exported symbols are restricted by an ELF version script and `__declspec` on Windows.
> - Exceptions and RTTI disabled; warnings as errors.
>
> Acceptance: the tree matches §12; a caller can read `aoahid.h` alone and know exactly what they must supply for every call; nothing in the header hints at a default; the project configures and builds clean on Linux with GCC and Clang.

---

## Phase 2 - L1, the HID core

> Phase 2. Implement `src/hid/` per `DESIGN.md` §6. Pure code: no I/O, no threads, no allocation after construction, no libusb.
>
> Deliverables: `item_writer`, `descriptor_builder`, `report_layout`, `validator`, and a `usages` table covering the pages the profile catalog needs.
>
> Requirements:
>
> - The item writer emits HID 1.11 short items in the smallest encoding that survives the parser's sign extension, deciding per item from the **declared** Logical Minimum. A field is unsigned when both bounds are non-negative, so with a minimum of 0 the value 65535 fits in a two-byte item and a four-byte item is not required. The guide corrects a common error here - read that correction before writing this function and cite it in a comment.
> - The descriptor builder takes a declarative collection description and produces the descriptor bytes **and** the matching `ReportLayout` in one pass, so the two cannot disagree. `ReportLayout` records, per report ID, the total wire length, whether an ID prefix exists, and for every field its byte offset, bit offset, width, signedness, logical range and endianness.
> - The validator implements the complete table in `DESIGN.md` §6.3. Each check returns the error code shown and records the offending field name in `aoahid_error_detail`.
> - Descriptor size policies are four separately named fields, never conflated: AOA wire limit, Linux UAPI/transport policy, Android EP0 buffer, and libusb/OS control buffer minus the eight-byte setup packet. An error message that trips a policy limit must say it is a policy, not an AOA wire limit.
> - Emit explicit Constant padding for readability, but accept specification-compliant implicit zero padding when validating a caller-supplied descriptor.
>
> Also deliver unit tests: item encoding across the 1/2/4-byte boundaries and around sign extension, collection balance, global state stack behavior, and every validator failure path.
>
> Acceptance: no `libusb` reference; no allocation on any path a caller can reach after construction; every validator row of §6.3 has a test that fails without the check.

---

## Phase 3 - L0, the transport

> Phase 3. Implement `src/transport/` per `DESIGN.md` §7 and guide Part VI.
>
> Deliverables: discovery and probing, AOA requests 51-58, registration with fragmentation, the transfer pool, the event pump, completion handling, and the close sequence including the graveyard.
>
> Requirements:
>
> - Probing classifies `GET_PROTOCOL` results exactly as `DESIGN.md` §7.1. A STALL from a non-AOA device is an ordinary negative probe result, never a fatal I/O error.
> - Registration sends descriptor fragments strictly ascending with `wIndex` exactly equal to the bytes already accepted. A failed fragment aborts the whole registration; fragments are never retried out of order. Fragment size is a required caller field, and a comment states that 64 bytes is an implementation policy, not a protocol constant.
> - The kernel registers the HID asynchronously after the final fragment with no ready callback, so a node's **first** report may STALL. Implement the bounded retry with caller-supplied attempt count and backoff, applied to the first report only. Diagnostics must distinguish device removal from descriptor rejection, and say when they cannot.
> - One report per control transfer, never fragmented across requests.
> - Implement all thirteen latency techniques of §7.3, each with a comment explaining why. Pay particular attention to pre-filling the constant parts of the setup packet at pool init, which the predecessor did not do, and to reaping after submit rather than before.
> - Completion handling follows the table in §7.5. For a control transfer `actual_length` excludes the eight-byte setup packet, so compare it against the report length. No user callback runs inside the libusb callback.
> - Per-node slot reservations per §7.4.
> - Nodes register and unregister individually on a live device at any time, and a device opens, closes and reopens at any time without touching any other device. Discovery may be re-run while sessions are open. A device whose link is gone fails every call immediately rather than accumulating work.
> - The close sequence is the seven ordered steps of §9.3. If the drain budget expires, return `AOAHID_CLOSE_PENDING` and move the **entire** device object into the context graveyard. Never free an object, buffer, transfer, handle or context while a callback can still run - freeing the object and leaking only the pool is explicitly forbidden.
> - Both event modes of §5.2. In caller-poll mode take no lock at all. In internal-thread mode guard only the pool, the inflight counter and the error latch, with a spinlock, and say in a comment what it costs.
> - Interface 0 is claimed on Windows only, because that backend routes device-recipient control transfers through an interface handle, while on Linux EP0 is reachable without claiming and claiming would take the interface from a kernel driver. Release only what was explicitly claimed.
>
> Also deliver a mock transport for tests that records request numbers, `wValue`, `wIndex`, payloads, ordering and timing, plus tests for fragment ordering, the first-report retry, sticky `NO_DEVICE`, pool exhaustion returning `BUSY` without blocking, a teardown race that cancels transfers under load, and an isolation suite proving that a device which stalls, times out, is unplugged or is closed changes nothing observable about any other device on the same context.
>
> Acceptance: no HID semantics anywhere in this layer; ASan and TSan clean on the teardown suite; every guide Part VI rule has a corresponding test.

---

## Phase 4 - L2 part one, the contact engine (touchscreen, touchpad, pen)

> Phase 4. Implement the contact and stylus profiles per `DESIGN.md` §8.1, §8.2 and §8.7, and guide sections 17, 18 and 19.
>
> These carry the most Android-specific automation in the library, so implement the state machine first, test it against the obligations below, and only then wire it to descriptors.
>
> Mandatory automatic behaviors, each of which must be a named unit test and carry a comment saying why it exists:
>
> - The three-phase contact lifecycle `None -> Down -> Up -> None`. The `Up` frame is mandatory: Contact Count is the number of valid slots, so a contact that simply disappears is never observed to go up. It must appear once more with Tip Switch cleared, still counted, and only then be dropped.
> - Contact Count is never reported as 0. A zero-count frame makes the kernel reuse the previous count and misread the frame, so when nothing is left there is nothing to send.
> - Coordinates are retained across the lift, so the release frame reports the contact where it was let go.
> - Contact ID is stable for the life of a contact and is reported with the final coordinates on release.
> - Pressure, when declared, is floored at 1 while the tip is down, because zero pressure with the tip down reads as hovering and produces no touch events.
> - Contacts are compacted into leading blocks and the remainder of the fixed report is zeroed.
> - Scan Time, when enabled, is maintained by the library at the declared resolution. Record in a comment that behavior differs across Android releases.
> - Multi-packet frames, when enabled, split a frame larger than one report and carry the total count in the first packet.
> - All contacts are lifted before the node is unregistered.
> - For pen: Tip down implies In Range; hover is in-range with tip clear; departure is one report with both clear; eraser and tip are mutually exclusive; illegal combinations are rejected rather than transmitted; automatic departure on close.
>
> Everything else is caller-supplied and required: resolution, contact count in 1..16, protocol form, pressure presence and range, width and height, azimuth, orientation, contact-count-maximum declaration, report IDs, and every logical range. Note in the header that 16 is an Android `MotionEvent` application policy, not a HID or kernel limit. The pure single-touch form requires the caller to set `target_verified = 1`, because pure ST is not guaranteed to be classified as a direct device.
>
> Touchpad shares the contact engine but is a separate profile with its own collection strategy, because classification rather than geometry is what separates it from a touchscreen. Buttons that do not belong to the pointer collection go into a separate node.
>
> Also deliver golden descriptor files for a spread of option combinations.
>
> Acceptance: every obligation above has a test that fails when the behavior is removed; no default appears anywhere; descriptor and layout are produced together.

---

## Phase 5 - L2 part two, keyboard, mouse, consumer, gamepad

> Phase 5. Implement the remaining primary profiles per `DESIGN.md` §8.3 through §8.6 and guide sections 13, 14, 15 and 16.
>
> Keyboard: the library maintains a one-bit Variable field per declared key. There is no Array form: every declared nonmodifier Usage is its own bit, so any number of simultaneously pressed keys up to the declared range is reported at once, with no slot count and no `ErrorRollOver` (0x01) overflow encoding to inject or recover from. Modifier usages 0xE0-0xE7 are routed to the modifier byte and never into the bitmap. All keys and modifiers are released before the node is unregistered, because a stuck modifier on Android is invisible and miserable to debug. No LED or Output surface exists. Declared usage range, report IDs, and whether a consumer report shares the node are caller-supplied.
>
> Mouse: a relative movement larger than the declared logical range is split across consecutive reports rather than saturated. Accumulated deltas are consumed on send, so a repeated flush does not move the pointer again - call this out in a comment as the most common bug in hand-written relative pointer code. Buttons are level, not edge; releases are explicit; all buttons are released on close. Wheel and pan follow the same rules. Delta width and signedness, button count, wheel and pan presence and ranges, and resolution multiplier declaration are caller-supplied.
>
> Consumer and system control: press and release are explicit, plus a tap helper that emits both, because a usage selector must return to 0 to release. Only usages in the caller's declared allow-list are accepted, because only a subset survives the path from the Linux input layer through the target key layout to an Android keycode. The library ships no list of its own. Note in the header that the system or a media session may consume these before an application sees them.
>
> Gamepad: the hat null value is computed from the declared range, since "centered" must be encoded outside the logical range. Neutral state on close means buttons cleared, hat null, sticks at the midpoint of their declared range, triggers at their minimum. Axis values are validated against declared range and signedness with no silent wrapping. Trigger axes require the caller to state the usage, the expected Linux code and the expected Android axis explicitly, because that mapping is where portability is lost. Simulation Controls usages are allowed inside a Generic Desktop Game Pad or Joystick top-level collection.
>
> Also deliver golden descriptors and a named test per automatic behavior.
>
> Acceptance: as Phase 4.

---

## Phase 6 - Conditional profiles, raw escape hatch, capability manifests

> Phase 6. Implement the conditional profiles of `DESIGN.md` §8.8, the raw escape hatch of §6.4, and the capability manifest of §8.9.
>
> Conditional profiles are barcode and magnetic-stripe keyboard wedges, the supported camera control key subset, the supported telephony subset, and battery telemetry. Each reports `android_status = conditional` and carries a header comment stating the exact condition and the guide section it comes from. Do not overstate any of them.
>
> The raw escape hatch accepts caller-supplied descriptor bytes plus an explicit table of accepted report IDs and lengths, runs the full static validator, and refuses unless the caller has set `acknowledges_no_android_support = 1`, `requires_output = 0` and `requires_feature_response = 0`. Its manifest reports `android_status = unknown`.
>
> The capability manifest exposes the fields of §8.9 for every node. Add a generator that builds the README support table from the manifests, and a CI check that fails when the committed table differs from the generated one, so documentation cannot drift from behavior.
>
> Do **not** implement LED, Haptics, PID, Lighting, FIDO, HID Sensors or Braille profiles, and do not generate descriptors for them. Explain in `docs/LIMITS.md` why each is absent, citing the guide.
>
> Acceptance: the generated support table matches the manifests exactly; no profile claims portable status without on-device evidence recorded in the target matrix.

---

## Phase 7 - C ABI implementation, bindings, examples

> Phase 7. Implement `src/api/` behind the Phase 1 header, then the bindings and examples.
>
> The C ABI validates every handle before use, checks `struct_size` on every option struct, checks the node kind on every profile call so a serializer can never be called with the wrong node, populates thread-local `aoahid_error_detail` on every failure, and lets no exception cross the boundary.
>
> Bindings, each with its own tests and each mirroring the no-defaults rule so a binding never fills in a value the C API would have rejected: Python via ctypes with a `pyproject.toml`, C# via P/Invoke with a csproj, Rust as a `-sys` crate plus a safe wrapper. Provide the optional header-only C++ wrapper `aoahid.hpp`, which enforces node and profile pairing with distinct types.
>
> Examples in all four languages:
>
> - enumerate devices and print what each one reports;
> - register a keyboard, a mouse and a touchscreen on one device simultaneously, then unregister just the touchscreen while the other two keep working;
> - drive every Android device the host can see, with the count discovered at runtime and never written into the example, shown both as one context driven by one thread and as one context per thread;
> - issue the identical call sequence to every device so they all do the same thing at the same time, updating every node first and submitting in a second pass, with a comment explaining why that ordering matters;
> - handle one device going away mid-session - close it, keep the others running, reopen it, and re-register the same specs - while the others never pause;
> - a clean shutdown that handles `AOAHID_CLOSE_PENDING` correctly.
>
> Acceptance: every example compiles and runs against the mock transport in CI; no example contains a magic number that is not explained in a comment.

---

## Phase 8 - Documentation

> Phase 8. Write the documentation set of `DESIGN.md` §12, in English.
>
> - `README.md`: what this is, the Input-only limitation stated within the first screen, install, a minimal working example, the generated support table, and a link to the guide.
> - `docs/API.md`, `docs/PROFILES.md`, `docs/EXAMPLES.md` - worked examples as prose the caller copies and edits, since there are no preset functions to call.
> - `docs/LATENCY.md`: the thirteen techniques of §7.3, what each buys, and how to measure it.
> - `docs/LIMITS.md`: the four separately named size policies, and why each unimplemented profile is absent.
> - `docs/TARGET_MATRIX.md`: the fields the guide requires, prefilled with the untested state rather than with claims.
> - `docs/PORTING.md`: Linux udev rules, Windows driver requirements, macOS notes.
> - `CONTRIBUTING.md` including the comment policy of `DESIGN.md` §15, `SECURITY.md` noting that report payloads can contain keystrokes and are never logged, `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md` with the libusb LGPL notice and the static-linking relinking notice.
>
> Never write "Android supported" for anything not verified on hardware at the four levels the guide requires. Where behavior is release-dependent or vendor-dependent, say so and name the condition.
>
> Acceptance: a competent C programmer can go from zero to a working multi-profile session using only the README and `docs/EXAMPLES.md`.

---

## Phase 9 - CI, packaging, release automation

> Phase 9. Implement `DESIGN.md` §13.
>
> `ci.yml` on every push and pull request: the build matrix of §13.2; unit, integration and fuzz-smoke tests; ASan and UBSan on Linux and macOS; TSan against the internal-thread event mode; `clang-format --dry-run --Werror`; `clang-tidy`; the symbol and policy lint of §13.4; the hid-tools descriptor parse job on Linux; and the documentation drift check.
>
> The symbol and policy lint fails the build when an exported symbol contains `default`, `preset`, `standard` or `typical`; when a public struct lacks `struct_size`; when a public header includes `libusb.h`; when any profile factory can succeed with a zero-filled options struct; or when a fixed-size array bounds the number of contexts, devices, nodes or specs. Write it as a real script under `tools/symbol-lint/`, not as an inline shell fragment.
>
> `release.yml` on a pushed `v*` tag: build every target, run the full gate of §13.6, and publish a GitHub Release with per-platform shared and static archives, headers, the CMake package config, an SPDX SBOM, SHA-256 checksums, and that version's changelog section. Python wheel and NuGet publication run from the same workflow behind a repository-variable gate. `docs.yml` publishes Doxygen output to GitHub Pages.
>
> The release job must **refuse the tag** when any gate condition fails, rather than publishing with a warning.
>
> Acceptance: a tag on a clean tree produces a complete release with no manual step; a deliberately introduced default-valued option, an unformatted file, and a leaked `libusb.h` include each fail CI on their own.

---

## Review Prompt - run before the first release

> Audit the complete `libaoahid` tree against `AOA_HID_GUIDE.md` and `DESIGN.md`. Do not fix anything yet. Produce a findings list, each item naming the file, the line, the rule violated, and the guide or design section it comes from.
>
> Check specifically:
>
> 1. Every item on the guide's final implementer checklist.
> 2. The no-defaults rule: any field that can be omitted, any internal fallback, any function that fills in a value, any binding that supplies one the C API would have rejected.
> 3. The automation boundary: anything automated that is a caller's choice rather than a protocol obligation, measured against `DESIGN.md` §16.
> 4. The send path: any allocation, lock, log call, syscall or configuration branch.
> 5. Lifetime safety: any path that frees an object, buffer, transfer, handle or context while a callback can still run.
> 6. Any invented count limit: a fixed-size array holding contexts, devices, nodes or specs; a compile-time maximum; a hard-coded device count in an example, test or document; or a protocol-imposed ceiling stated without its origin.
> 7. Any coupling between devices: shared mutable state, a failure or timeout on one device affecting another, a batch or broadcast entry point, or a node that cannot be unregistered without disturbing its siblings.
> 8. AOA HID ID and HID Report ID confusion anywhere, in code, comments or documentation.
> 9. Any claim of Android support not backed by on-device evidence in the target matrix.
> 10. Any comment that restates code instead of explaining why, any non-English text, and any file missing its responsibility header.
> 11. Any place where the four size policies are conflated or a policy limit is described as a protocol limit.
> 12. Output, Feature, LED, rumble or haptics appearing anywhere as a runtime failure instead of an absence.
>
> Then rank the findings by severity and propose a fix order. Wait for approval before changing code.

---

## Release Prompt

> Prepare the first release. Verify every condition of `DESIGN.md` §13.6 and report the state of each with evidence, not assertion. For any condition that cannot be met yet - in particular on-device verification at the `getevent`, `dumpsys input` and application-API levels - say so plainly, mark the affected profiles as untested in the target matrix, and write release notes that make the untested state unmistakable. Do not soften it. A library that is honest about what it has not tested is more useful than one that is not.

---

## Appendix - questions the implementer should raise rather than guess

If any of these come up, stop and ask rather than choosing:

- A guide section and `DESIGN.md` disagree.
- A profile needs a value the caller cannot reasonably know, which usually means the option decomposition is wrong rather than that a default is needed.
- An automatic behavior would have to make a parameter choice to work.
- A latency technique conflicts with a correctness requirement.
- A platform backend needs behavior the design does not cover.
- Anything that would put Output or Feature semantics into the API.
