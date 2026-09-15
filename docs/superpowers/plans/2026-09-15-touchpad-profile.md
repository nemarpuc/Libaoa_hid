# Touchpad Profile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `AOAHID_PROFILE_TOUCHPAD` as a ninth, independent public profile (`aoahid_touchpad_options` / `aoahid_spec_create_touchpad()` / `aoahid_touchpad_button()`), sharing contact-field internals with Touchscreen via a new `TouchFields` struct, without touching the existing `aoahid_touch_options` ABI.

**Architecture:** A new public struct/enum/two functions are additive to the C ABI and a new `touchpad_node_ref` is additive to the C++ wrapper. Internally, `TouchConfig` moves from holding a raw `aoahid_touch_options` copy to holding a profile-agnostic `TouchFields` struct that both `aoahid_spec_create_touchscreen` and the new `aoahid_spec_create_touchpad` populate before calling one shared descriptor/validation function. The button state machine removed in commit `fa55417` (0.3.0) is restored in `TouchState`, gated by `TouchFields::button_count` (0 for Touchscreen, caller-chosen for Touchpad) rather than by a profile-kind branch.

**Tech Stack:** C++20 core library (CMake), C ABI header, C++ header-only wrapper, Python ctypes binding, C# P/Invoke binding, Rust FFI (`aoahid-sys`) + safe wrapper (`aoahid`) binding, C examples.

**Spec:** `docs/superpowers/specs/2026-09-15-touchpad-profile-design.md` — this plan implements that spec; read both.

## Global Constraints

- `button_count == 0` is valid (buttonless/clickpad); do not require `button_count >= 1`.
- `aoahid_touch_options` / `aoahid_spec_create_touchscreen` / `touchscreen_node_ref` get **zero** behavioral or ABI changes. Only their internal storage type changes (`TouchConfig::options` → `TouchConfig::fields`).
- New version: `0.4.0` (MINOR bump — purely additive, no breaking change).
- Touchpad's `aoahid_capability_manifest.android_status` is always `AOAHID_ANDROID_CONDITIONAL`, regardless of `button_count`.
- Touchpad's Application Collection Usage is Digitizers `0x0D` / Touch Pad `0x05` (`aoa::hid::usage::touch_pad`, to be restored in `usages.hpp`).
- **No AI/assistant attribution anywhere**: commit messages, CHANGELOG, release notes, code comments. Every commit uses the repository's existing local git identity (`nemarpuc <nemarpuc@gmail.com>` — already configured via `git config --local`, do not touch global config).
- `set_error()` in `src/api/error_detail.cpp` stores the raw `const char*` field-name pointer without copying it (see `g_error.field` assignment) — every string passed to `set_error`/`validate_field`/`validate_report_id` etc. **must have static storage duration** (a string literal, or a `thread_local` buffer that itself has static storage duration). Never pass a stack-local `std::string::c_str()`.

---

### Task 1: Public C ABI and C++ wrapper additions

**Files:**
- Modify: `include/aoahid.h`
- Modify: `include/aoahid.hpp`
- Modify: `src/hid/usages.hpp`
- Test: full project build (no functional test yet — nothing calls the new symbols)

**Interfaces:**
- Produces: `AOAHID_PROFILE_TOUCHPAD = 9`; `aoahid_touchpad_options` struct; `aoahid_spec_create_touchpad(const aoahid_touchpad_options*, aoahid_spec**)`; `aoahid_touchpad_button(aoahid_node*, uint32_t, uint32_t)`; C++ `aoa::touchpad_node_ref` with `touch()`/`button()`.

- [ ] **Step 1: Add `AOAHID_PROFILE_TOUCHPAD` to the enum in `include/aoahid.h`**

Find the `aoahid_profile_kind` enum (currently ends `AOAHID_PROFILE_RAW = 8`) and append, leaving every existing value untouched:

```c
typedef int32_t aoahid_profile_kind;
enum {
    AOAHID_PROFILE_KEYBOARD = 1,
    AOAHID_PROFILE_MOUSE = 2,
    AOAHID_PROFILE_TOGGLE = 3,
    AOAHID_PROFILE_GAMEPAD = 4,
    AOAHID_PROFILE_TOUCHSCREEN = 5,
    AOAHID_PROFILE_PEN = 6,
    AOAHID_PROFILE_BATTERY = 7,
    AOAHID_PROFILE_RAW = 8,
    /* Always the Digitizers / Touch Pad Application Collection Usage. */
    AOAHID_PROFILE_TOUCHPAD = 9
};
```

- [ ] **Step 2: Add `aoahid_touchpad_options` immediately after `aoahid_touch_options` in `include/aoahid.h`**

```c
/* Same fixed-slot Multi-Touch report shape as aoahid_touch_options, under the
 * Digitizers / Touch Pad Application Collection Usage instead of Touch Screen,
 * plus button_count physical click buttons. button_count may be zero for a
 * buttonless clickpad; only then does aoahid_touchpad_button stay inert. */
typedef struct aoahid_touchpad_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint32_t maximum_contacts;
    uint32_t contacts_per_report;
    aoahid_integer_field contact_identifier;
    aoahid_integer_field x;
    aoahid_integer_field y;
    aoahid_integer_field contact_count;
    uint32_t enable_pressure;
    aoahid_integer_field pressure;
    uint32_t enable_width;
    aoahid_integer_field width;
    uint32_t enable_height;
    aoahid_integer_field height;
    uint32_t enable_azimuth;
    aoahid_integer_field azimuth;
    uint32_t enable_scan_time;
    aoahid_integer_field scan_time;
    uint32_t scan_time_unit_100us;
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
    uint32_t button_count;
} aoahid_touchpad_options;
```

- [ ] **Step 3: Add the two function declarations in `include/aoahid.h`, right after `aoahid_spec_create_touchscreen` (for the factory) and right after `aoahid_touch` (for the runtime function)**

```c
/* aoahid_spec_create_touchpad
 * Ownership: Borrows and copies options. On success, out_spec receives one
 * caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * caller-owned options must not be concurrently mutated.
 * Always the Digitizers / Touch Pad Application Collection. button_count may
 * be zero (buttonless clickpad).
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, range,
 * count, or flag; AOAHID_ERR_OVERFLOW for button, descriptor, or layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_touchpad(const aoahid_touchpad_options* options, aoahid_spec** out_spec);
```

```c
/* aoahid_touchpad_button
 * Ownership: Borrows exactly a Touchpad Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected during an in-flight/multi-packet frame.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile, one-based button,
 * or pressed flag; AOAHID_ERR_BUSY for an in-flight frame;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_touchpad_button(aoahid_node* node, uint32_t button,
                                                            uint32_t pressed);
```

Place `aoahid_touchpad_button` textually after `aoahid_touch`'s declaration (matching the existing per-profile grouping order in the file: create-spec functions are grouped together, then runtime functions are grouped together in the same profile order).

- [ ] **Step 4: Add `touchpad_node_ref` to `include/aoahid.hpp`, right after `touchscreen_node_ref`**

```cpp
/* Always fixed-slot Multi-Touch under the Touch Pad Application Collection.
 * button() is only meaningful when the Spec declared button_count above zero. */
class touchpad_node_ref final : public node_ref {
  public:
    constexpr touchpad_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result touch(std::uint32_t contact_id, bool down, std::int32_t x,
                                      std::int32_t y,
                                      const aoahid_touch_extra* extra = nullptr) const noexcept {
        return aoahid_touch(value_, contact_id, down ? 1U : 0U, x, y, extra);
    }
    [[nodiscard]] aoahid_result button(std::uint32_t index, bool pressed) const noexcept {
        return aoahid_touchpad_button(value_, index, pressed ? 1U : 0U);
    }

  private:
    explicit constexpr touchpad_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, touchpad_node_ref&) noexcept;
};
```

- [ ] **Step 5: Add the `bind()` overload for `touchpad_node_ref` in `include/aoahid.hpp`**

Read the existing `bind(aoahid_node* value, touchscreen_node_ref& output)` function (around line 214) — it checks `node_profile_kind(value)` against `AOAHID_PROFILE_TOUCHSCREEN` and constructs the wrapper on match. Add an identical overload for `touchpad_node_ref` checking `AOAHID_PROFILE_TOUCHPAD`:

```cpp
[[nodiscard]] inline aoahid_result bind(aoahid_node* value, touchpad_node_ref& output) noexcept {
    output = touchpad_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result probe = node_profile_kind(value, kind);
    if (probe != AOAHID_OK)
        return probe;
    if (kind != AOAHID_PROFILE_TOUCHPAD)
        return AOAHID_ERR_PARAM;
    output = touchpad_node_ref{value};
    return AOAHID_OK;
}
```

(Match whatever the real helper the existing `touchscreen_node_ref` overload uses to read the profile kind — read it first and mirror it exactly; do not invent a different mechanism.)

- [ ] **Step 6: Restore `touch_pad = 0x05U` in `src/hid/usages.hpp`**

In the Digitizers-page usage block, immediately after `constexpr std::uint16_t touch_screen = 0x04U;`, add:

```cpp
constexpr std::uint16_t touch_pad = 0x05U;
```

- [ ] **Step 7: Build and confirm the existing test suite still passes (regression check)**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAOAHID_BUILD_TESTS=ON -DAOAHID_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: build succeeds (new symbols are declared but unimplemented and uncalled, so nothing links against them yet); every existing test still passes.

- [ ] **Step 8: Commit**

```bash
git add include/aoahid.h include/aoahid.hpp src/hid/usages.hpp
git commit -m "Add Touchpad public ABI surface (declarations only)

Adds AOAHID_PROFILE_TOUCHPAD, aoahid_touchpad_options,
aoahid_spec_create_touchpad, aoahid_touchpad_button, and the C++
touchpad_node_ref wrapper. No implementation yet; internals land next."
```

---

### Task 2: Core implementation — internal representation, descriptor generation, state machine, lifecycle wiring

**Files:**
- Modify: `src/api/internal.hpp`
- Modify: `src/profiles/spec.cpp`
- Modify: `src/profiles/state.cpp`
- Modify: `src/api/c_api.cpp`

**Interfaces:**
- Consumes: `AOAHID_PROFILE_TOUCHPAD`, `aoahid_touchpad_options`, `aoahid_spec_create_touchpad`, `aoahid_touchpad_button` declared in Task 1.
- Produces: `aoa::detail::TouchFields`, `aoa::detail::TouchConfig::fields` (replaces `::options`), `aoa::detail::TouchState::buttons`/`button_transitions` (restored), a working `aoahid_spec_create_touchpad` and `aoahid_touchpad_button`, and a `touchscreen_node_ref`/Touchscreen path that behaves identically to before this task.

- [ ] **Step 1: Replace `TouchConfig` with `TouchFields` + `TouchConfig` in `src/api/internal.hpp`**

Find:
```cpp
struct TouchConfig {
    aoahid_touch_options options{};
};
```
Replace with:
```cpp
/* Profile-agnostic contact-field representation shared by Touchscreen and
 * Touchpad. button_count is always here; Touchscreen always passes zero, so
 * the button state machine below needs no profile_kind branch. */
struct TouchFields {
    aoahid_report_id_option report_id{};
    std::uint32_t maximum_contacts{};
    std::uint32_t contacts_per_report{};
    aoahid_integer_field contact_identifier{};
    aoahid_integer_field x{};
    aoahid_integer_field y{};
    aoahid_integer_field contact_count{};
    std::uint32_t enable_pressure{};
    aoahid_integer_field pressure{};
    std::uint32_t enable_width{};
    aoahid_integer_field width{};
    std::uint32_t enable_height{};
    aoahid_integer_field height{};
    std::uint32_t enable_azimuth{};
    aoahid_integer_field azimuth{};
    std::uint32_t enable_scan_time{};
    aoahid_integer_field scan_time{};
    std::uint32_t scan_time_unit_100us{};
    std::uint32_t enable_contact_count_maximum_feature_declaration{};
    std::uint32_t enable_multi_packet_frames{};
    std::uint32_t button_count{};
};

struct TouchConfig {
    TouchFields fields{};
};
```

- [ ] **Step 2: Restore button state in `TouchState` in `src/api/internal.hpp`**

Find:
```cpp
struct TouchState {
    std::array<ContactState, 16> contacts{};
    std::uint32_t scan_time{};
    std::size_t packet_cursor{};
    std::chrono::steady_clock::time_point scan_epoch{};
    bool scan_epoch_active{};
};
```
Replace with:
```cpp
struct TouchState {
    std::array<ContactState, 16> contacts{};
    std::vector<std::uint8_t> buttons;
    std::vector<std::uint8_t> button_transitions;
    std::uint32_t scan_time{};
    std::size_t packet_cursor{};
    std::chrono::steady_clock::time_point scan_epoch{};
    bool scan_epoch_active{};
};
```

- [ ] **Step 3: Rewrite `create_touch_spec` in `src/profiles/spec.cpp` into a shared fields-based function plus two thin ABI wrappers**

Read the current `create_touch_spec` function in full first (it runs from the `static aoahid_result create_touch_spec(const aoahid_touch_options* options, aoahid_spec** out_spec)` line through its closing `}`, followed by `aoahid_spec_create_touchscreen_impl`). You will transform it as follows — the validation logic and descriptor-building lambda body are unchanged in substance, only the input type and two additions change:

1. Add a small thread-local field-name helper near the top of the anonymous namespace in `spec.cpp` (it must return a pointer with static/thread storage duration — `set_error` stores the raw pointer without copying, so a stack `std::string` would dangle):

```cpp
thread_local char g_touch_field_scratch[64];

const char* touch_field(const char* prefix, const char* suffix) noexcept {
    std::size_t written = 0U;
    for (const char* p = prefix; *p != '\0' && written + 1U < sizeof(g_touch_field_scratch); ++p) {
        g_touch_field_scratch[written++] = *p;
    }
    if (written + 1U < sizeof(g_touch_field_scratch)) {
        g_touch_field_scratch[written++] = '.';
    }
    for (const char* p = suffix; *p != '\0' && written + 1U < sizeof(g_touch_field_scratch); ++p) {
        g_touch_field_scratch[written++] = *p;
    }
    g_touch_field_scratch[written] = '\0';
    return g_touch_field_scratch;
}
```

2. Change the function signature from `create_touch_spec(const aoahid_touch_options* options, aoahid_spec** out_spec)` to:

```cpp
static aoahid_result create_touch_spec_from_fields(const aoa::detail::TouchFields& fields,
                                                    const char* prefix,
                                                    const aoahid_profile_kind profile_kind,
                                                    const std::uint16_t application_usage,
                                                    const aoahid_android_status android_status,
                                                    aoahid_spec** out_spec) {
```

Drop the `options == nullptr` / `aoa::detail::valid_struct(...)` / `options->reserved != 0U` checks entirely from this function — those are ABI-struct-specific and move into the two thin wrappers (Step 4/5 below), which run before converting to `TouchFields`.

3. Throughout the remaining body, replace every `options->` with `fields.` (e.g. `options->x` → `fields.x`, `options->maximum_contacts` → `fields.maximum_contacts`, etc.) and replace every string-literal field name `"touch.<suffix>"` with `touch_field(prefix, "<suffix>")` (e.g. `"touch.x"` → `touch_field(prefix, "x")`, `"touch.report_id"` → `touch_field(prefix, "report_id")`). The two top-level `"touchscreen_options"` literals (the `valid_struct` failure message and the "invalid required field..." message at the very top of the original function) do **not** need a replacement here at all — per point 2 above, the `valid_struct`/`reserved` checks that used them move entirely into the two thin wrappers (Step 4/5), each already passing its own literal (`"touchscreen_options"` or `"touchpad_options"`) at the point of the check. So `create_touch_spec_from_fields` itself never emits either literal and needs no `struct_name` parameter.

4. After the existing `button_count`-independent validation (i.e., right after the `enable_scan_time` block, before `aoa::detail::TouchConfig config{};`), add the button-count overflow check (restoring the pre-0.3.0 check, generalized to both profiles):

```cpp
    if (fields.button_count > 65535U) {
        set_error(AOAHID_ERR_OVERFLOW, touch_field(prefix, "button_count"),
                  "The Button Usage range cannot exceed the 16-bit Usage value space.");
        return AOAHID_ERR_OVERFLOW;
    }
```

5. Change `aoa::detail::TouchConfig config{}; config.options = *options;` to `aoa::detail::TouchConfig config{}; config.fields = fields;`.

6. Change `GeneratedSpecIdentity{AOAHID_PROFILE_TOUCHSCREEN, AOAHID_ANDROID_PORTABLE_CANDIDATE}` to `GeneratedSpecIdentity{profile_kind, android_status}` (both now function parameters).

7. In the descriptor-building lambda, change `builder.begin_application(aoa::hid::usage::page_digitizers, aoa::hid::usage::touch_screen)` to `builder.begin_application(aoa::hid::usage::page_digitizers, application_usage)` (now a captured parameter — the lambda already captures by value `[=]`, so `application_usage` is captured automatically).

8. Immediately before the final `pad_report(builder, layout);` / `return builder.end_collection();` at the end of the lambda (after the Contact Count field, matching where the pre-0.3.0 code placed the button field — see the commit `fa55417` reverse-diff for exact placement), add:

```cpp
            if (fields.button_count > 0U) {
                if (!builder.variable_range(aoa::hid::usage::page_button, 1U,
                                            static_cast<std::uint16_t>(fields.button_count), 1U,
                                            FieldSemantic::buttons)) {
                    return false;
                }
            }
```

9. Rename the two remaining `options->x.physical`-style references inside the lambda that build width/height Physical-units matching (`physical_units_match(options->width.physical, options->x.physical)` etc. — these were already replaced by step 3's blanket `options->` → `fields.` substitution; just confirm none were missed).

- [ ] **Step 4: Add the Touchscreen thin wrapper in `src/profiles/spec.cpp`**

Replace the existing:
```cpp
static aoahid_result aoahid_spec_create_touchscreen_impl(const aoahid_touch_options* options,
                                                         aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    return create_touch_spec(options, out_spec);
}
```
with:
```cpp
static aoahid_result touch_fields_from_touchscreen(const aoahid_touch_options& options,
                                                    aoa::detail::TouchFields* out) noexcept {
    out->report_id = options.report_id;
    out->maximum_contacts = options.maximum_contacts;
    out->contacts_per_report = options.contacts_per_report;
    out->contact_identifier = options.contact_identifier;
    out->x = options.x;
    out->y = options.y;
    out->contact_count = options.contact_count;
    out->enable_pressure = options.enable_pressure;
    out->pressure = options.pressure;
    out->enable_width = options.enable_width;
    out->width = options.width;
    out->enable_height = options.enable_height;
    out->height = options.height;
    out->enable_azimuth = options.enable_azimuth;
    out->azimuth = options.azimuth;
    out->enable_scan_time = options.enable_scan_time;
    out->scan_time = options.scan_time;
    out->scan_time_unit_100us = options.scan_time_unit_100us;
    out->enable_contact_count_maximum_feature_declaration =
        options.enable_contact_count_maximum_feature_declaration;
    out->enable_multi_packet_frames = options.enable_multi_packet_frames;
    out->button_count = 0U;
    return AOAHID_OK;
}

static aoahid_result aoahid_spec_create_touchscreen_impl(const aoahid_touch_options* options,
                                                         aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "touchscreen_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U) {
        set_error(AOAHID_ERR_PARAM, "touchscreen_options",
                  "The touch options contain an invalid required field, range, count, or flag.");
        return AOAHID_ERR_PARAM;
    }
    aoa::detail::TouchFields fields{};
    touch_fields_from_touchscreen(*options, &fields);
    return create_touch_spec_from_fields(fields, "touch", AOAHID_PROFILE_TOUCHSCREEN,
                                         aoa::hid::usage::touch_screen,
                                         AOAHID_ANDROID_PORTABLE_CANDIDATE, out_spec);
}
```

- [ ] **Step 5: Add the Touchpad wrapper and impl in `src/profiles/spec.cpp`, right after the Touchscreen wrapper**

```cpp
static void touch_fields_from_touchpad(const aoahid_touchpad_options& options,
                                       aoa::detail::TouchFields* out) noexcept {
    out->report_id = options.report_id;
    out->maximum_contacts = options.maximum_contacts;
    out->contacts_per_report = options.contacts_per_report;
    out->contact_identifier = options.contact_identifier;
    out->x = options.x;
    out->y = options.y;
    out->contact_count = options.contact_count;
    out->enable_pressure = options.enable_pressure;
    out->pressure = options.pressure;
    out->enable_width = options.enable_width;
    out->width = options.width;
    out->enable_height = options.enable_height;
    out->height = options.height;
    out->enable_azimuth = options.enable_azimuth;
    out->azimuth = options.azimuth;
    out->enable_scan_time = options.enable_scan_time;
    out->scan_time = options.scan_time;
    out->scan_time_unit_100us = options.scan_time_unit_100us;
    out->enable_contact_count_maximum_feature_declaration =
        options.enable_contact_count_maximum_feature_declaration;
    out->enable_multi_packet_frames = options.enable_multi_packet_frames;
    out->button_count = options.button_count;
}

static aoahid_result aoahid_spec_create_touchpad_impl(const aoahid_touchpad_options* options,
                                                      aoahid_spec** out_spec) {
    aoa::detail::clear_error();
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options == nullptr ? 0U : options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "touchpad_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U) {
        set_error(AOAHID_ERR_PARAM, "touchpad_options",
                  "The touchpad options contain an invalid required field, range, count, or flag.");
        return AOAHID_ERR_PARAM;
    }
    aoa::detail::TouchFields fields{};
    touch_fields_from_touchpad(*options, &fields);
    return create_touch_spec_from_fields(fields, "touchpad", AOAHID_PROFILE_TOUCHPAD,
                                         aoa::hid::usage::touch_pad, AOAHID_ANDROID_CONDITIONAL,
                                         out_spec);
}
```

- [ ] **Step 6: Export both ABI entry points at the bottom of `src/profiles/spec.cpp` (mirror how `aoahid_spec_create_touchscreen` is exported)**

Find:
```cpp
aoahid_result AOAHID_CALL aoahid_spec_create_touchscreen(const aoahid_touch_options* options,
                                                         aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_touchscreen",
                            [&] { return aoahid_spec_create_touchscreen_impl(options, out_spec); });
}
```
Add immediately after:
```cpp
aoahid_result AOAHID_CALL aoahid_spec_create_touchpad(const aoahid_touchpad_options* options,
                                                      aoahid_spec** out_spec) {
    return abi_spec_factory(out_spec, "spec.create_touchpad",
                            [&] { return aoahid_spec_create_touchpad_impl(options, out_spec); });
}
```

- [ ] **Step 7: Fix every remaining `TouchConfig::options` reference in `src/profiles/state.cpp`**

Three call sites read `config_value.options` / `spec->options` / `touch_config->options`:

1. `validate_contact(const aoa::detail::TouchConfig& config_value, ...)`: change `const auto& options = config_value.options;` to `const auto& options = config_value.fields;` (keep the local variable named `options` — every subsequent line in that function already reads `options.x`, `options.contact_identifier`, etc. and needs no further change).

2. `aoahid_touch_impl`: change `const auto* spec = config<aoa::detail::TouchConfig>(node);` usages of `spec->options.x` / `spec->options.y` / `spec->options.maximum_contacts` to `spec->fields.x` / `spec->fields.y` / `spec->fields.maximum_contacts`. Also change the `right_kind` check from:
```cpp
    const bool right_kind =
        node != nullptr && node->spec != nullptr && node->spec->kind == AOAHID_PROFILE_TOUCHSCREEN;
```
to:
```cpp
    const bool right_kind =
        node != nullptr && node->spec != nullptr &&
        (node->spec->kind == AOAHID_PROFILE_TOUCHSCREEN || node->spec->kind == AOAHID_PROFILE_TOUCHPAD);
```
(`aoahid_touch()` is the shared runtime function for both `touchscreen_node_ref::touch()` and `touchpad_node_ref::touch()` — see Task 1 Step 4 — so it must accept either profile kind.)

3. `serialize_node`: change `touch_config->options.contacts_per_report` to `touch_config->fields.contacts_per_report`.

- [ ] **Step 8: Restore the button branch in `serialize_node`'s `FieldSemantic::buttons` case in `src/profiles/state.cpp`**

Find the `case FieldSemantic::buttons:` block (currently: mouse branch, gamepad-buttons branch, gamepad-raw-dpad-buttons branch, then `else if (pen != nullptr) { ... }`). Insert a touch branch between the gamepad-raw-dpad branch and the pen branch:

```cpp
            } else if (touch != nullptr && field.instance < touch->buttons.size()) {
                value = touch->buttons[field.instance];
            } else if (pen != nullptr) {
```

- [ ] **Step 9: Add `aoahid_touchpad_button_impl` in `src/profiles/state.cpp`, right after `aoahid_touch_impl`**

```cpp
static aoahid_result aoahid_touchpad_button_impl(aoahid_node* node, const std::uint32_t button,
                                                 const std::uint32_t pressed) {
    aoa::detail::clear_error();
    const aoahid_result check = usable(node, AOAHID_PROFILE_TOUCHPAD, "touchpad.button");
    if (check != AOAHID_OK)
        return check;
    auto* touch = state<aoa::detail::TouchState>(node);
    if (touch->packet_cursor != 0U) {
        set_error(AOAHID_ERR_BUSY, "touchpad.button",
                  "The current multi-packet frame must finish before state changes.");
        return AOAHID_ERR_BUSY;
    }
    if (button == 0U || button > touch->buttons.size() || !aoa::detail::valid_boolean(pressed)) {
        set_error(AOAHID_ERR_PARAM, "touchpad.button",
                  "Button is one-based and pressed must be zero or one.");
        return AOAHID_ERR_PARAM;
    }
    if (!transition_storage_matches(touch->buttons, touch->button_transitions, "touchpad.button")) {
        return AOAHID_ERR_INTERNAL;
    }
    const std::size_t index = button - 1U;
    const auto value = static_cast<std::uint8_t>(pressed);
    const bool changed = touch->buttons[index] != value;
    if (changed && touch->button_transitions[index] != 0U) {
        return pending_transition("touchpad.button");
    }
    if (changed) {
        touch->buttons[index] = value;
        touch->button_transitions[index] = 1U;
    }
    node->dirty = node->dirty || changed;
    return AOAHID_OK;
}
```

- [ ] **Step 10: Export `aoahid_touchpad_button` at the bottom of `src/profiles/state.cpp`, right after `aoahid_touch`'s export**

```cpp
aoahid_result AOAHID_CALL aoahid_touchpad_button(aoahid_node* node, const std::uint32_t button,
                                                 const std::uint32_t pressed) {
    return abi_state_result("touchpad.button",
                            [&] { return aoahid_touchpad_button_impl(node, button, pressed); });
}
```

- [ ] **Step 11: Restore button transitions clearing in `consume_lifecycle_transitions` in `src/profiles/state.cpp`**

Find the `touch` branch in `consume_lifecycle_transitions` (currently only clears `contact.lifecycle_transition_pending`). Add, right after the `for (auto& contact : touch->contacts)` loop:

```cpp
        std::fill(touch->button_transitions.begin(), touch->button_transitions.end(),
                  std::uint8_t{0});
```

- [ ] **Step 12: Wire button state sizing, neutralization, and non-neutral detection in `src/api/c_api.cpp`**

1. `initialize_node_state`: replace
```cpp
    case AOAHID_PROFILE_TOUCHSCREEN: {
        node->state = aoa::detail::TouchState{};
        break;
    }
```
with:
```cpp
    case AOAHID_PROFILE_TOUCHSCREEN:
    case AOAHID_PROFILE_TOUCHPAD: {
        const auto* spec = std::get_if<aoa::detail::TouchConfig>(&node->spec->config);
        aoa::detail::TouchState value{};
        value.buttons.resize(spec->fields.button_count);
        value.button_transitions.resize(spec->fields.button_count);
        node->state = std::move(value);
        break;
    }
```

2. `has_non_neutral_state`: replace
```cpp
    if (const auto* touch = std::get_if<aoa::detail::TouchState>(&node->state)) {
        return std::any_of(touch->contacts.begin(), touch->contacts.end(), [](const auto& contact) {
            return contact.phase == aoa::detail::ContactPhase::down;
        });
    }
```
with:
```cpp
    if (const auto* touch = std::get_if<aoa::detail::TouchState>(&node->state)) {
        const bool active_contact =
            std::any_of(touch->contacts.begin(), touch->contacts.end(), [](const auto& contact) {
                return contact.phase == aoa::detail::ContactPhase::down;
            });
        return active_contact || std::any_of(touch->buttons.begin(), touch->buttons.end(),
                                             [](const std::uint8_t value) { return value != 0U; });
    }
```

3. `neutralize_touch`: replace
```cpp
AOAHID_NOINLINE bool neutralize_touch(aoa::detail::TouchState* touch) noexcept {
    bool had_non_neutral_state = false;
    for (auto& contact : touch->contacts) {
        had_non_neutral_state =
            had_non_neutral_state || contact.phase != aoa::detail::ContactPhase::none;
        if (contact.phase == aoa::detail::ContactPhase::down) {
            contact.phase = aoa::detail::ContactPhase::up;
        }
    }
    touch->packet_cursor = 0U;
    return had_non_neutral_state;
}
```
with:
```cpp
AOAHID_NOINLINE bool neutralize_touch(aoa::detail::TouchState* touch) noexcept {
    bool had_non_neutral_state = false;
    for (auto& contact : touch->contacts) {
        had_non_neutral_state =
            had_non_neutral_state || contact.phase != aoa::detail::ContactPhase::none;
        if (contact.phase == aoa::detail::ContactPhase::down) {
            contact.phase = aoa::detail::ContactPhase::up;
        }
    }
    had_non_neutral_state =
        had_non_neutral_state || std::any_of(touch->buttons.begin(), touch->buttons.end(),
                                             [](const std::uint8_t value) { return value != 0U; });
    std::fill(touch->buttons.begin(), touch->buttons.end(), std::uint8_t{0});
    touch->packet_cursor = 0U;
    return had_non_neutral_state;
}
```

4. Search `src/profiles/state.cpp` for the `transfer_complete` still-active check (around the code that decides whether a completed touch report leaves the node idle) and restore the `|| std::any_of(touch->buttons...)` clause the same way it appeared before `fa55417` (mirror the `has_non_neutral_state` change above — same predicate, same location relative to the existing `std::any_of(touch->contacts...)` check).

- [ ] **Step 13: Build and run the full existing test suite (regression gate for this task)**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: builds cleanly (no `-Wall -Wextra -Wpedantic -Werror` warnings — this project builds with `-Werror`, so every new parameter, including `prefix`, `profile_kind`, and `application_usage`, must actually be used in the function body); every pre-existing test still passes (Touchscreen behavior is bit-for-bit unchanged).

- [ ] **Step 14: Commit**

```bash
git add src/api/internal.hpp src/profiles/spec.cpp src/profiles/state.cpp src/api/c_api.cpp
git commit -m "Implement Touchpad profile sharing TouchFields with Touchscreen

aoahid_spec_create_touchpad and aoahid_touchpad_button are now fully
implemented, sharing descriptor generation, validation, and the
contact/button state machine with Touchscreen through the new
internal TouchFields representation. Touchscreen's own ABI and
behavior are unchanged."
```

---

### Task 3: Touchpad-specific unit and integration tests

**Files:**
- Modify: `tests/unit/test_profiles.cpp`
- Modify: `tests/integration/test_transport.cpp`

**Interfaces:**
- Consumes: `aoahid_spec_create_touchpad`, `aoahid_touchpad_options`, `aoahid_touchpad_button`, `AOAHID_PROFILE_TOUCHPAD` from Task 2.

- [ ] **Step 1: Add a `touchpad_options()` helper in `tests/unit/test_profiles.cpp`, right after the existing `touch_options()` helper**

```cpp
aoahid_touchpad_options touchpad_options() {
    aoahid_touchpad_options options{};
    options.struct_size = static_cast<std::uint32_t>(sizeof(options));
    options.report_id = {0U, 0U, {0U, 0U, 0U}};
    options.maximum_contacts = 3U;
    options.contacts_per_report = 2U;
    options.contact_identifier = {0, 15, 4U, {}};
    options.x = {0, 1000, 16U, {}};
    options.y = {0, 1000, 16U, {}};
    options.contact_count = {0, 3, 2U, {}};
    options.enable_pressure = 1U;
    options.pressure = {0, 255, 8U, {}};
    options.enable_width = 0U;
    options.width = {0, 0, 0U, {}};
    options.enable_height = 0U;
    options.height = {0, 0, 0U, {}};
    options.enable_scan_time = 1U;
    options.scan_time = {0, 65535, 16U, {}};
    options.scan_time_unit_100us = 1U;
    options.enable_contact_count_maximum_feature_declaration = 0U;
    options.enable_multi_packet_frames = 1U;
    options.button_count = 0U;
    return options;
}
```

- [ ] **Step 2: Add `test_touchpad_button_only_and_mid_frame_guard` in `tests/unit/test_profiles.cpp`, right after `test_touch_lift_frame_and_idle_suppression`**

```cpp
void test_touchpad_button_only_and_mid_frame_guard() {
    aoahid_touchpad_options options = touchpad_options();
    options.button_count = 1U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&options, &spec) == AOAHID_OK);
    if (spec == nullptr)
        return;

    aoahid_node node{};
    node.spec = spec;
    aoa::detail::TouchState state_value{};
    state_value.buttons.resize(1U);
    state_value.button_transitions.resize(1U);
    node.state = std::move(state_value);

    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 1U) == AOAHID_OK);
    std::array<std::uint8_t, 128U> report{};
    std::size_t length = 0U;
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    const auto* count = field(spec, aoa::hid::FieldSemantic::contact_count);
    const auto* button = field(spec, aoa::hid::FieldSemantic::buttons);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 0U);
    AOAHID_CHECK(button != nullptr && extract(report.data(), *button) == 1U);
    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_ERR_BUSY);
    aoa::detail::transfer_complete(&node, AOAHID_OK, 0);

    auto* touch = std::get_if<aoa::detail::TouchState>(&node.state);
    AOAHID_CHECK(touch != nullptr);
    if (touch != nullptr) {
        touch->packet_cursor = 1U;
        AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_ERR_BUSY);
        AOAHID_CHECK(touch->buttons[0] == 1U);
        touch->packet_cursor = 0U;
    }

    AOAHID_CHECK(aoahid_touchpad_button(&node, 1U, 0U) == AOAHID_OK);
    report.fill(0U);
    AOAHID_CHECK(aoa::detail::serialize_node(&node, report.data(), report.size(), &length) ==
                 AOAHID_OK);
    AOAHID_CHECK(count != nullptr && extract(report.data(), *count) == 0U);
    AOAHID_CHECK(button != nullptr && extract(report.data(), *button) == 0U);
    aoahid_spec_release(spec);
}
```

Note: `field(spec, ...)` here is the local unit-test helper already used throughout this file (distinct from `profile_field` used in `test_transport.cpp`) — use whatever the surrounding tests already use for the same purpose; read one neighboring test (e.g. `test_touch_lift_frame_and_idle_suppression`) to confirm the exact helper name before writing this.

- [ ] **Step 3: Register the new test and add two assertions in `test_profiles()`, `tests/unit/test_profiles.cpp`**

Add the call right after `test_touch_lift_frame_and_idle_suppression();`:
```cpp
    test_touchpad_button_only_and_mid_frame_guard();
```

Then, near the existing `invalid_touch.contact_identifier = {1, 15, 4U, {}};` / `AOAHID_ERR_PARAM` assertion block, add an analogous Touchpad overflow check and a button_count==0 acceptance check:
```cpp
    aoahid_touchpad_options invalid_touchpad = touchpad_options();
    invalid_touchpad.button_count = 65536U;
    aoahid_spec* touchpad_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&invalid_touchpad, &touchpad_spec) ==
                 AOAHID_ERR_OVERFLOW);

    aoahid_touchpad_options buttonless = touchpad_options();
    buttonless.button_count = 0U;
    aoahid_spec* buttonless_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&buttonless, &buttonless_spec) == AOAHID_OK);
    if (buttonless_spec != nullptr) {
        aoahid_capability_manifest manifest{};
        manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
        AOAHID_CHECK(aoahid_spec_manifest(buttonless_spec, &manifest) == AOAHID_OK);
        AOAHID_CHECK(manifest.profile_kind == AOAHID_PROFILE_TOUCHPAD);
        AOAHID_CHECK(manifest.android_status == AOAHID_ANDROID_CONDITIONAL);
    }
    aoahid_spec_release(buttonless_spec);
```

(Insert this block inside the same function that already holds `invalid_touch`/`spec`, using a distinct local `spec` variable name to avoid shadowing — read the surrounding function first to pick non-colliding names.)

- [ ] **Step 4: Add `test_public_touchpad_button_only_close` in `tests/integration/test_transport.cpp`, right after `test_descriptor_requirements_are_caller_policies`**

```cpp
void test_public_touchpad_button_only_close() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(12U, {5U, 2U});

    aoahid_context* context = nullptr;
    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);

    const aoahid_device_info selected{candidate.bus,
                                      candidate.address,
                                      candidate.port_path.data(),
                                      candidate.port_path.size(),
                                      candidate.vendor_id,
                                      candidate.product_id,
                                      candidate.serial.c_str(),
                                      candidate.product.c_str()};
    aoahid_device* device = nullptr;
    aoahid_device_options device_options = public_device_options();
    device_options.close_drain_timeout_ms = 100U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_touchpad_options touchpad{};
    touchpad.struct_size = sizeof(touchpad);
    touchpad.maximum_contacts = 2U;
    touchpad.contacts_per_report = 2U;
    touchpad.contact_identifier = {0, 15, 4U, {}};
    touchpad.x = {0, 1000, 16U, {}};
    touchpad.y = {0, 1000, 16U, {}};
    touchpad.contact_count = {0, 2, 2U, {}};
    touchpad.enable_scan_time = 1U;
    touchpad.scan_time = {0, 65535, 16U, {}};
    touchpad.scan_time_unit_100us = 1U;
    touchpad.enable_multi_packet_frames = 0U;
    touchpad.button_count = 1U;

    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchpad(&touchpad, &spec) == AOAHID_OK);
    const aoa::hid::FieldLayout* count =
        profile_field(spec, aoa::hid::FieldSemantic::contact_count);
    const aoa::hid::FieldLayout* button = profile_field(spec, aoa::hid::FieldSemantic::buttons);
    AOAHID_CHECK(count != nullptr && button != nullptr);

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, &node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touchpad_button(node, 1U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);

    const auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && count != nullptr && button != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads[0], *count) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[0], *button) == 1U);
        AOAHID_CHECK(extract_report_value(payloads[1], *count) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[1], *button) == 0U);
    }

    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}
```

- [ ] **Step 5: Register it in `test_transport()`, `tests/integration/test_transport.cpp`**

Add right after `test_cancel_and_device_isolation();` / `test_blocking_event_wait_is_woken_by_cancel();` (wherever `test_public_controller_close_neutralizes_dpad();` currently sits, add the new call immediately before it, matching the original pre-0.3.0 ordering):

```cpp
    test_public_touchpad_button_only_close();
```

- [ ] **Step 6: Build and run tests**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including the four new/modified touchpad assertions.

- [ ] **Step 7: Commit**

```bash
git add tests/unit/test_profiles.cpp tests/integration/test_transport.cpp
git commit -m "Add unit and integration test coverage for the Touchpad profile"
```

---

### Task 4: ABI layout oracle, golden descriptor, support table, frozen schema, version bump

**Files:**
- Modify: `tests/abi/layout_oracle.c`
- Modify: `tests/hidtools/export_descriptors.cpp`
- Create: `tests/golden/touchpad.hex`
- Modify: `tools/generate-support-table/generate.py`
- Modify: `README.md` (regenerated table, mechanical — see Step 5)
- Rename+modify: `tests/abi/check_v0_3_0_golden.py` → `tests/abi/check_v0_4_0_golden.py`
- Modify: `include/aoahid.h` (`AOAHID_VERSION_MINOR`), `CMakeLists.txt`, `vcpkg.json`, `tools/docs/Doxyfile`, `bindings/python/pyproject.toml`, `bindings/csharp/AoaHid/AoaHid.csproj`, `bindings/rust/aoahid-sys/Cargo.toml`, `bindings/rust/aoahid/Cargo.toml`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add the `aoahid_touchpad_options` layout check in `tests/abi/layout_oracle.c`**

Right after the existing `AOAHID_BEGIN(aoahid_touch_options);` ... block (ends around the `enable_multi_packet_frames` field), add an analogous block for the new struct — same field list, plus `button_count`:

```c
    AOAHID_BEGIN(aoahid_touchpad_options);
    AOAHID_FIELD(aoahid_touchpad_options, struct_size);
    AOAHID_FIELD(aoahid_touchpad_options, reserved);
    AOAHID_FIELD(aoahid_touchpad_options, report_id);
    AOAHID_FIELD(aoahid_touchpad_options, maximum_contacts);
    AOAHID_FIELD(aoahid_touchpad_options, contacts_per_report);
    AOAHID_FIELD(aoahid_touchpad_options, contact_identifier);
    AOAHID_FIELD(aoahid_touchpad_options, x);
    AOAHID_FIELD(aoahid_touchpad_options, y);
    AOAHID_FIELD(aoahid_touchpad_options, contact_count);
    AOAHID_FIELD(aoahid_touchpad_options, enable_pressure);
    AOAHID_FIELD(aoahid_touchpad_options, pressure);
    AOAHID_FIELD(aoahid_touchpad_options, enable_width);
    AOAHID_FIELD(aoahid_touchpad_options, width);
    AOAHID_FIELD(aoahid_touchpad_options, enable_height);
    AOAHID_FIELD(aoahid_touchpad_options, height);
    AOAHID_FIELD(aoahid_touchpad_options, enable_azimuth);
    AOAHID_FIELD(aoahid_touchpad_options, azimuth);
    AOAHID_FIELD(aoahid_touchpad_options, enable_scan_time);
    AOAHID_FIELD(aoahid_touchpad_options, scan_time);
    AOAHID_FIELD(aoahid_touchpad_options, scan_time_unit_100us);
    AOAHID_FIELD(aoahid_touchpad_options, enable_contact_count_maximum_feature_declaration);
    AOAHID_FIELD(aoahid_touchpad_options, enable_multi_packet_frames);
    AOAHID_FIELD(aoahid_touchpad_options, button_count);
```

Also add `AOAHID_CONSTANT(AOAHID_PROFILE_TOUCHPAD);` right after the existing `AOAHID_CONSTANT(AOAHID_PROFILE_TOUCHSCREEN);` line.

- [ ] **Step 2: Add a Touchpad export in `tests/hidtools/export_descriptors.cpp`**

Right after the existing:
```cpp
    aoahid_touch_options touch = fixed_touch_options();
    ok = emit_descriptor(output_directory, "touchscreen-fixed-mt",
                         aoahid_spec_create_touchscreen(&touch, &spec), spec) &&
         ok;
    spec = nullptr;
```
add:
```cpp
    aoahid_touchpad_options touchpad{};
    touchpad.struct_size = static_cast<std::uint32_t>(sizeof(touchpad));
    touchpad.report_id = touch.report_id;
    touchpad.maximum_contacts = touch.maximum_contacts;
    touchpad.contacts_per_report = touch.contacts_per_report;
    touchpad.contact_identifier = touch.contact_identifier;
    touchpad.x = touch.x;
    touchpad.y = touch.y;
    touchpad.contact_count = touch.contact_count;
    touchpad.enable_pressure = touch.enable_pressure;
    touchpad.pressure = touch.pressure;
    touchpad.enable_width = touch.enable_width;
    touchpad.width = touch.width;
    touchpad.enable_height = touch.enable_height;
    touchpad.height = touch.height;
    touchpad.enable_azimuth = touch.enable_azimuth;
    touchpad.azimuth = touch.azimuth;
    touchpad.enable_scan_time = touch.enable_scan_time;
    touchpad.scan_time = touch.scan_time;
    touchpad.scan_time_unit_100us = touch.scan_time_unit_100us;
    touchpad.enable_contact_count_maximum_feature_declaration =
        touch.enable_contact_count_maximum_feature_declaration;
    touchpad.enable_multi_packet_frames = touch.enable_multi_packet_frames;
    touchpad.button_count = 2U;
    ok = emit_descriptor(output_directory, "touchpad",
                         aoahid_spec_create_touchpad(&touchpad, &spec), spec) &&
         ok;
    spec = nullptr;
```

(This mirrors the pre-0.3.0 `touchpad-fixed-mt` export, which reused the fixed touch options with `touchpad_button_count = 2U`; naming this export `"touchpad"` rather than `"touchpad-fixed-mt"` since it is now its own struct/factory, not a variant of the touchscreen options.)

- [ ] **Step 3: Build the export tool and generate `tests/golden/touchpad.hex`**

```bash
cmake --build build --target aoahid_export_descriptors -j
mkdir -p /tmp/aoahid-golden-scratch
./build/aoahid_export_descriptors /tmp/aoahid-golden-scratch -
```

(Passing `-` as the second argument skips golden-file comparison, so this run only writes `.bin` files and does not fail on the not-yet-created `touchpad.hex`.) Then hex-encode the produced `touchpad.bin` into the golden file using the exact same lowercase-nibble, trailing-newline format `emit_descriptor` compares against:

```bash
xxd -p -c 0 /tmp/aoahid-golden-scratch/touchpad.bin | tr -d '\n' > tests/golden/touchpad.hex
printf '\n' >> tests/golden/touchpad.hex
```

Verify: `xxd -p -c 0` emits lowercase hex with no separators, matching the `digits[]` lowercase-hex encoding in `emit_descriptor`. If your `xxd` build inserts spaces, use `od -An -tx1 -v /tmp/aoahid-golden-scratch/touchpad.bin | tr -d ' \n' > tests/golden/touchpad.hex && printf '\n' >> tests/golden/touchpad.hex` instead — inspect the resulting file with `cat -A tests/golden/touchpad.hex` and confirm it is one line of lowercase hex ending in `$` (single newline, no trailing space) before proceeding.

- [ ] **Step 4: Add the Touchpad row to `tools/generate-support-table/generate.py`**

Right after the existing `Presentation("touchscreen-fixed-mt", "Touchscreen, fixed MT", "portable candidate", "Audited Linux commit and dated Android documentation; target matrix still required"),` line, add:

```python
    Presentation("touchpad", "Touchpad", "conditional", "Distinct Linux input property (INPUT_PROP_POINTER); Android converts contacts to mouse-source motion, gesture value-add is OEM/release dependent"),
```

- [ ] **Step 5: Run the full ABI/golden/support-table test group and regenerate the README table**

```bash
cmake --build build -j
ctest --test-dir build -R "aoahid.golden-descriptors|aoahid.support-table|aoahid.abi" --output-on-failure
```

If `aoahid.golden-descriptors` now passes (confirming `tests/golden/touchpad.hex` matches), regenerate the README's marked table section from the fresh catalog:

```bash
python3 tools/generate-support-table/generate.py --readme README.md --catalog build/support-manifests.tsv > /tmp/readme-generated.md
mv /tmp/readme-generated.md README.md
ctest --test-dir build -R "aoahid.support-table" --output-on-failure
```

Expected: `aoahid.support-table` now passes with `--check` (run automatically by ctest).

- [ ] **Step 6: Rename and update the frozen ABI golden-schema check**

```bash
git mv tests/abi/check_v0_3_0_golden.py tests/abi/check_v0_4_0_golden.py
```

Open `tests/abi/check_v0_4_0_golden.py` and, mirroring exactly how the file already represents every other struct/enum (e.g. its `TouchOptions` class around line 212 and the `"AOAHID_PROFILE_TOUCHSCREEN": 5` entry around line 417):
- Add a `TouchpadOptions(c.Structure)` class with the same `_fields_` list as `tests/abi/layout_oracle.c`'s new block from Step 1 (translated to the file's existing ctypes idiom — copy the pattern used by the neighboring `TouchOptions` class exactly, adding `("button_count", c.c_uint32)` at the end).
- Add `"aoahid_touchpad_options": TouchpadOptions,` to the struct-name-to-class map (next to `"aoahid_touch_options": TouchOptions,`).
- Add `"AOAHID_PROFILE_TOUCHPAD": 9,` to the profile-kind constant map.
- Update the file's embedded expected-version check (search for `0, 3, 0` or `"0.3.0"` in the file) to `0, 4, 0` / `"0.4.0"`.
- Search `CMakeLists.txt` and any CI config (`.github/workflows/*.yml` if present) for the literal string `check_v0_3_0_golden.py` and update every reference to `check_v0_4_0_golden.py`.

- [ ] **Step 7: Bump the version to 0.4.0 everywhere**

1. `include/aoahid.h`: `#define AOAHID_VERSION_MINOR 3` → `#define AOAHID_VERSION_MINOR 4`.
2. `CMakeLists.txt`: `project(libaoahid VERSION 0.3.0` → `project(libaoahid VERSION 0.4.0`.
3. `vcpkg.json`: `"version-semver": "0.3.0",` → `"version-semver": "0.4.0",`.
4. `tools/docs/Doxyfile`: `PROJECT_NUMBER         = 0.3.0` → `PROJECT_NUMBER         = 0.4.0`.
5. `bindings/python/pyproject.toml`: `version = "0.3.0"` → `version = "0.4.0"`.
6. `bindings/csharp/AoaHid/AoaHid.csproj`: `<Version>0.3.0</Version>` → `<Version>0.4.0</Version>`.
7. `bindings/rust/aoahid-sys/Cargo.toml`: `version = "0.3.0"` → `version = "0.4.0"`.
8. `bindings/rust/aoahid/Cargo.toml`: both `version = "0.3.0"` (the crate's own version) and `aoahid-sys = { path = "../aoahid-sys", version = "0.3.0" }` → `"0.4.0"`.

- [ ] **Step 8: Add a `[0.4.0]` entry to `CHANGELOG.md`**

Insert immediately after the `## [Unreleased]` line and before `## [0.3.0] - 2026-09-15`:

```markdown
## [0.4.0] - 2026-09-15

### Added

- **Touchpad profile.** `AOAHID_PROFILE_TOUCHPAD`, `aoahid_touchpad_options`,
  `aoahid_spec_create_touchpad`, and `aoahid_touchpad_button` add back a
  Touchpad Application Collection (Digitizers Touch Pad `0x0d/0x05`) as its
  own independent profile — not, as before its 0.3.0 removal, a
  `touchpad_button_count` field folded into the Touchscreen options.
  `aoahid_touch_options` / `aoahid_spec_create_touchscreen` are unchanged.

  `aoahid_touchpad_options` mirrors `aoahid_touch_options` field-for-field
  (same fixed-slot Multi-Touch contact shape) plus a `button_count` field for
  the Touchpad's physical click buttons; `button_count` may be zero for a
  buttonless clickpad. `aoahid_touchpad_button(node, button, pressed)` is a
  one-based, edge-guarded button state machine restored from the pre-0.3.0
  implementation, now gated to Touchpad Nodes only.

  Internally, Touchscreen and Touchpad now share one `TouchFields`
  representation, one descriptor/validation function, and one contact+button
  state machine; only the Application Collection Usage and the runtime
  `aoahid_capability_manifest.android_status` (Touchscreen stays a portable
  candidate; Touchpad is always conditional — Android converts Touchpad
  contacts to ordinary mouse-source motion, and gesture value-add is
  release/OEM dependent) differ between the two.

  A new C++ `touchpad_node_ref` wrapper offers `touch()` and `button()`;
  `touchscreen_node_ref` is unchanged (`touch()` only).

  This is a purely additive ABI change: every 0.3.0 struct, enum value, and
  function signature is unchanged.
```

- [ ] **Step 9: Full rebuild and full test run**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAOAHID_BUILD_TESTS=ON -DAOAHID_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: every test passes, including `aoahid.golden-descriptors`, `aoahid.support-table`, and the renamed ABI golden-schema test.

- [ ] **Step 10: Commit**

```bash
git add tests/abi/layout_oracle.c tests/hidtools/export_descriptors.cpp tests/golden/touchpad.hex \
        tools/generate-support-table/generate.py README.md \
        tests/abi/check_v0_4_0_golden.py CMakeLists.txt vcpkg.json tools/docs/Doxyfile \
        bindings/python/pyproject.toml bindings/csharp/AoaHid/AoaHid.csproj \
        bindings/rust/aoahid-sys/Cargo.toml bindings/rust/aoahid/Cargo.toml \
        include/aoahid.h CHANGELOG.md
git status
git commit -m "Freeze Touchpad in the ABI oracle and golden descriptors; bump to 0.4.0"
```

(Run `git status` first to confirm no other unrelated files were swept in, and that `tests/abi/check_v0_3_0_golden.py` shows as renamed rather than deleted+added.)

---

### Task 5: Python binding

**Files:**
- Modify: `bindings/python/aoahid/native.py`

**Interfaces:**
- Consumes: `AOAHID_PROFILE_TOUCHPAD = 9`, `aoahid_touchpad_options` (Task 1/2 field list), `aoahid_spec_create_touchpad`, `aoahid_touchpad_button`.

- [ ] **Step 1: Add `TouchpadOptions` right after the existing `TouchOptions` class**

Mirror `TouchOptions`'s `_fields_` list exactly (see the class already in the file, `struct_size` through `enable_multi_packet_frames`), adding one more tuple at the end:

```python
class TouchpadOptions(c.Structure):
    _fields_ = [
        ("struct_size", c.c_uint32),
        ("reserved", c.c_uint32),
        ("report_id", ReportId),
        ("maximum_contacts", c.c_uint32),
        ("contacts_per_report", c.c_uint32),
        ("contact_identifier", IntegerField),
        ("x", IntegerField),
        ("y", IntegerField),
        ("contact_count", IntegerField),
        ("enable_pressure", c.c_uint32),
        ("pressure", IntegerField),
        ("enable_width", c.c_uint32),
        ("width", IntegerField),
        ("enable_height", c.c_uint32),
        ("height", IntegerField),
        ("enable_azimuth", c.c_uint32),
        ("azimuth", IntegerField),
        ("enable_scan_time", c.c_uint32),
        ("scan_time", IntegerField),
        ("scan_time_unit_100us", c.c_uint32),
        ("enable_contact_count_maximum_feature_declaration", c.c_uint32),
        ("enable_multi_packet_frames", c.c_uint32),
        ("button_count", c.c_uint32),
    ]
```

- [ ] **Step 2: Register the struct in every name/type map that already lists `TouchOptions`**

Find each of these and add a parallel `aoahid_touchpad_options` / `TouchpadOptions` entry right next to the existing `aoahid_touch_options` one:
- The struct-name-to-class dict (`"aoahid_touch_options": TouchOptions,` → add `"aoahid_touchpad_options": TouchpadOptions,`).
- The per-function options-type map used for `aoahid_spec_create_*` declarations (`"aoahid_spec_create_touchscreen": TouchOptions,` → add `"aoahid_spec_create_touchpad": TouchpadOptions,`).
- The `__all__` export list (`"TouchOptions",` → add `"TouchpadOptions",`).

- [ ] **Step 3: Declare the two new native functions**

Find where `aoahid_spec_create_touchscreen` and `aoahid_touch` are declared via the file's `declare(...)` helper (see the existing `declare("aoahid_touch", [NodeP, c.c_uint32, c.c_uint32, c.c_int32, c.c_int32, c.POINTER(TouchExtra)], result)` line) and add, using the exact same helper and result-type conventions used for `aoahid_spec_create_touchscreen`/`aoahid_touch`:

```python
declare("aoahid_spec_create_touchpad", [c.POINTER(TouchpadOptions), c.POINTER(c.c_void_p)], result)
declare("aoahid_touchpad_button", [NodeP, c.c_uint32, c.c_uint32], result)
```

(Match the exact `out_spec`/pointer-type spelling the file already uses for `aoahid_spec_create_touchscreen` — copy that line's parameter types verbatim rather than retyping from scratch, since the surrounding file's `SpecP`/`c.c_void_p` convention must be read first.)

- [ ] **Step 4: Run the binding conformance check**

```bash
python3 tools/check_bindings.py
```

Expected: passes (it diffs the header against every binding via regex, so this both validates Steps 1-3 and catches any missed entry).

- [ ] **Step 5: Commit**

```bash
git add bindings/python/aoahid/native.py
git commit -m "Add Touchpad profile to the Python binding"
```

---

### Task 6: C# binding

**Files:**
- Modify: `bindings/csharp/AoaHid/Native.cs`

- [ ] **Step 1: Add `AoahidProfileKind.Touchpad = 9` next to the existing enum entries**

Find `Touchscreen = 5, Pen = 6, Battery = 7, Raw = 8,` and add `Touchpad = 9,` immediately after (matching whatever enum name/style wraps that line — read it first, it is likely `public enum ProfileKind`).

- [ ] **Step 2: Add a `TouchpadOptions` struct right after `TouchOptions`**

Mirror `TouchOptions`'s field list exactly, appending `button_count`:

```csharp
[StructLayout(LayoutKind.Sequential)]
public struct TouchpadOptions
{
    public uint StructSize;
    public uint Reserved;
    public ReportId ReportId;
    public uint MaximumContacts;
    public uint ContactsPerReport;
    public IntegerField ContactIdentifier;
    public IntegerField X;
    public IntegerField Y;
    public IntegerField ContactCount;
    public uint EnablePressure;
    public IntegerField Pressure;
    public uint EnableWidth;
    public IntegerField Width;
    public uint EnableHeight;
    public IntegerField Height;
    public uint EnableAzimuth;
    public IntegerField Azimuth;
    public uint EnableScanTime;
    public IntegerField ScanTime;
    public uint ScanTimeUnit100us;
    public uint EnableContactCountMaximumFeatureDeclaration;
    public uint EnableMultiPacketFrames;
    public uint ButtonCount;
}
```

- [ ] **Step 3: Register it in the reflection-based struct map**

Find `["aoahid_touch_options"] = typeof(TouchOptions),` and add right after:
```csharp
["aoahid_touchpad_options"] = typeof(TouchpadOptions),
```

- [ ] **Step 4: Add the two P/Invoke declarations**

Right after the existing:
```csharp
[DllImport(Library, EntryPoint = "aoahid_spec_create_touchscreen", ExactSpelling = true, CallingConvention = Call)]
public static extern Result SpecCreateTouchscreen(in TouchOptions options, out nint spec);
```
add:
```csharp
[DllImport(Library, EntryPoint = "aoahid_spec_create_touchpad", ExactSpelling = true, CallingConvention = Call)]
public static extern Result SpecCreateTouchpad(in TouchpadOptions options, out nint spec);
```

Right after the existing:
```csharp
[DllImport(Library, EntryPoint = "aoahid_touch", ExactSpelling = true, CallingConvention = Call)]
public static extern Result Touch(nint node, uint contactId, uint down, int x, int y, in TouchExtra extra);
```
add:
```csharp
[DllImport(Library, EntryPoint = "aoahid_touchpad_button", ExactSpelling = true, CallingConvention = Call)]
public static extern Result TouchpadButton(nint node, uint button, uint pressed);
```

- [ ] **Step 5: Run the binding conformance check**

```bash
python3 tools/check_bindings.py
```

- [ ] **Step 6: Build the C# binding project (if a local .NET SDK is available)**

```bash
dotnet build bindings/csharp/AoaHid/AoaHid.csproj
```

If no `dotnet` SDK is installed in this environment, skip this build step but do not skip Step 5.

- [ ] **Step 7: Commit**

```bash
git add bindings/csharp/AoaHid/Native.cs
git commit -m "Add Touchpad profile to the C# binding"
```

---

### Task 7: Rust binding (`aoahid-sys` FFI + `aoahid` safe wrapper)

**Files:**
- Modify: `bindings/rust/aoahid-sys/src/lib.rs`
- Modify: `bindings/rust/aoahid-sys/tests/layout.rs`
- Modify: `bindings/rust/aoahid/src/lib.rs`

- [ ] **Step 1: Add `aoahid_touchpad_options` to `bindings/rust/aoahid-sys/src/lib.rs`, right after `aoahid_touch_options`**

```rust
c_struct!(aoahid_touchpad_options {
    struct_size: u32,
    reserved: u32,
    report_id: aoahid_report_id_option,
    maximum_contacts: u32,
    contacts_per_report: u32,
    contact_identifier: aoahid_integer_field,
    x: aoahid_integer_field,
    y: aoahid_integer_field,
    contact_count: aoahid_integer_field,
    enable_pressure: u32,
    pressure: aoahid_integer_field,
    enable_width: u32,
    width: aoahid_integer_field,
    enable_height: u32,
    height: aoahid_integer_field,
    enable_azimuth: u32,
    azimuth: aoahid_integer_field,
    enable_scan_time: u32,
    scan_time: aoahid_integer_field,
    scan_time_unit_100us: u32,
    enable_contact_count_maximum_feature_declaration: u32,
    enable_multi_packet_frames: u32,
    button_count: u32,
});
```

- [ ] **Step 2: Add the two `extern "C"` function declarations**

Right after the existing `aoahid_spec_create_touchscreen` declaration (around line 378):
```rust
    pub fn aoahid_spec_create_touchpad(
        options: *const aoahid_touchpad_options,
        out_spec: *mut *mut aoahid_spec,
    ) -> aoahid_result;
```
Right after the existing `aoahid_touch` declaration (around line 442):
```rust
    pub fn aoahid_touchpad_button(node: *mut aoahid_node, button: u32, pressed: u32)
        -> aoahid_result;
```

(Match the exact parameter types/ordering the neighboring `aoahid_spec_create_touchscreen`/`aoahid_touch` declarations already use — read them first rather than guessing `aoahid_spec`/`aoahid_node` spelling.)

- [ ] **Step 3: Add `AOAHID_PROFILE_TOUCHPAD` if `aoahid_profile_kind` constants are declared in this file**

Search `bindings/rust/aoahid-sys/src/lib.rs` for `AOAHID_PROFILE_TOUCHSCREEN` (as a `pub const`); if found, add `pub const AOAHID_PROFILE_TOUCHPAD: aoahid_profile_kind = 9;` right after it in the same style.

- [ ] **Step 4: Update `bindings/rust/aoahid-sys/tests/layout.rs`**

Find how this file verifies `aoahid_touch_options`'s layout (likely `assert_eq!(mem::size_of::<aoahid_touch_options>(), ...)` and/or field-offset assertions). Add an analogous block for `aoahid_touchpad_options`, following the exact same pattern used for `aoahid_touch_options` in this file — read that block first and mirror its structure with the new field list from Step 1.

- [ ] **Step 5: Add the safe wrapper spec type in `bindings/rust/aoahid/src/lib.rs`**

Right after the existing:
```rust
safe_spec_type!(
    TouchscreenSpec,
    sys::aoahid_touch_options,
    sys::aoahid_spec_create_touchscreen
);
```
add:
```rust
safe_spec_type!(
    TouchpadSpec,
    sys::aoahid_touchpad_options,
    sys::aoahid_spec_create_touchpad
);
```

- [ ] **Step 6: Add `TouchpadNodeRef` right after `node_type!(TouchscreenNodeRef);`**

```rust
node_type!(TouchpadNodeRef);
```

- [ ] **Step 7: Implement `touch()` and `button()` on `TouchpadNodeRef`, right after the existing `impl TouchscreenNodeRef` block**

```rust
/// Always fixed-slot Multi-Touch under the Touch Pad Application Collection.
impl TouchpadNodeRef {
    pub fn touch(
        self,
        contact_id: u32,
        down: bool,
        x: i32,
        y: i32,
        extra: Option<&sys::aoahid_touch_extra>,
    ) -> Result<(), Error> {
        result(unsafe {
            sys::aoahid_touch(
                self.as_ptr(),
                contact_id,
                u32::from(down),
                x,
                y,
                extra.map_or(core::ptr::null(), |value| value as *const _),
            )
        })
    }

    pub fn button(self, button: u32, pressed: bool) -> Result<(), Error> {
        result(unsafe { sys::aoahid_touchpad_button(self.as_ptr(), button, u32::from(pressed)) })
    }
}
```

- [ ] **Step 8: Build and test the Rust bindings (if a local toolchain is available)**

```bash
cargo build --manifest-path bindings/rust/aoahid-sys/Cargo.toml
cargo test --manifest-path bindings/rust/aoahid-sys/Cargo.toml
cargo build --manifest-path bindings/rust/aoahid/Cargo.toml
```

If no local `cargo`/Rust toolchain is available in this environment, skip this step but still run Step 9.

- [ ] **Step 9: Run the binding conformance check**

```bash
python3 tools/check_bindings.py
```

- [ ] **Step 10: Commit**

```bash
git add bindings/rust/aoahid-sys/src/lib.rs bindings/rust/aoahid-sys/tests/layout.rs \
        bindings/rust/aoahid/src/lib.rs
git commit -m "Add Touchpad profile to the Rust binding"
```

---

### Task 8: C example

**Files:**
- Create: `examples/c/profiles/touchpad.c`
- Modify: `CMakeLists.txt` (register the new example executable, mirroring the existing per-profile example registrations)

- [ ] **Step 1: Read `examples/c/profiles/touchscreen.c` and `examples/c/profiles/profile_example.h` in full, and find how `CMakeLists.txt` registers `examples/c/profiles/touchscreen.c` as an executable/test**

- [ ] **Step 2: Create `examples/c/profiles/touchpad.c`, mirroring `touchscreen.c` exactly, swapping the factory/profile constant and adding a button press/release to the send sequence**

```c
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Touchpad profile: same fixed multi-touch contact shape as Touchscreen, plus
 * one physical click button. button_count may be zero for a buttonless
 * clickpad; this example declares one button to exercise aoahid_touchpad_button. */
#include "profile_example.h"

static const char* const k_send_sequence[] = {
    "aoahid_touch(node, 0, 1, 500, 500, NULL)     contact id 0, tip down",
    "aoahid_node_submit(node)                     Contact Count and Scan Time are derived",
    "aoahid_touchpad_button(node, 1, 1)            physical click button pressed",
    "aoahid_node_submit(node)                      button report",
    "aoahid_touchpad_button(node, 1, 0)            button released",
    "aoahid_touch(node, 0, 0, 500, 500, NULL)      tip up keeps the same stable id",
    "aoahid_node_submit(node)                      closes the frame", NULL};

int main(void) {
    aoahid_touchpad_options options = {0};
    const aoahid_integer_field coordinate = {0, 32767, 16U, {0, 0, 0, 0, 0}};
    const aoahid_integer_field byte_range = {0, 255, 8U, {0, 0, 0, 0, 0}};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.maximum_contacts = 3U;
    options.contacts_per_report = 3U;
    options.contact_identifier = (aoahid_integer_field){0, 15, 4U, {0, 0, 0, 0, 0}};
    options.x = coordinate;
    options.y = coordinate;
    options.contact_count = (aoahid_integer_field){0, 3, 2U, {0, 0, 0, 0, 0}};
    options.enable_pressure = 1U;
    options.pressure = byte_range;
    options.enable_width = 1U;
    options.width = byte_range;
    options.enable_height = 1U;
    options.height = byte_range;
    options.enable_azimuth = 1U;
    options.azimuth = (aoahid_integer_field){0, 36000, 16U, {1U, 0, 360, 0, 0x14U}};
    options.enable_scan_time = 1U;
    options.scan_time = (aoahid_integer_field){0, 65535, 16U, {0, 0, 0, 0, 0}};
    options.scan_time_unit_100us = 1U;
    options.button_count = 1U;

    result = aoahid_spec_create_touchpad(&options, &spec);
    return aoahid_example_run("touchpad", AOAHID_PROFILE_TOUCHPAD, result, spec,
                              k_send_sequence);
}
```

Note: `aoahid_example_run`'s send-sequence strings are documentation-only labels for a human reading the printed example (confirm this by reading `profile_example.h`/`profile_example.c` before assuming it — if it actually *parses and executes* each string, adjust the button lines' call syntax to match its real button-call grammar instead of inventing one, since other examples with multiple distinct calls, e.g. `mouse.c` or `gamepad.c` with `button`/axis calls, show the real pattern to copy).

- [ ] **Step 3: Register the new example in `CMakeLists.txt`**

Find the block that builds `examples/c/profiles/touchscreen.c` as its own executable (search `profiles/touchscreen.c`) and add an equivalent block for `profiles/touchpad.c`, copying that block's target name pattern (likely `aoahid_example_touchscreen` → `aoahid_example_touchpad`), compiler flags, and `add_test` registration verbatim aside from the name/source substitution.

- [ ] **Step 4: Build and run the new example**

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/aoahid_example_touchpad 2>&1 | head -40
```

Expected: the example builds and runs to completion the same way `aoahid_example_touchscreen` does (both run without a real device — confirm by reading how the touchscreen example test is wired: it likely runs against the fake libusb backend or exits early with a documented "no device" message; match that same behavior).

- [ ] **Step 5: Commit**

```bash
git add examples/c/profiles/touchpad.c CMakeLists.txt
git commit -m "Add a Touchpad profile example"
```

---

### Task 9: Documentation

**Files:**
- Modify: `docs/PROFILES.md`, `docs/API.md`, `docs/DESIGN.md`, `docs/FACT_AUDIT.md`, `docs/LIMITS.md`, `docs/EXAMPLES.md`, `docs/SOURCE_CONFLICTS.md`, `docs/TARGET_MATRIX.md`

(`README.md`'s profile table was already regenerated mechanically in Task 4 Step 5; `docs/AOA_HID_GUIDE.md` and `docs/IMPLEMENTATION_PROMPTS.md` are explicitly out of scope per the spec — they document general platform facts / historical prompts, not this library's current surface.)

- [ ] **Step 1: `docs/PROFILES.md`**

1. In the "Profile consolidation" removal table (around line 21), the current row reads:
   `| `aoahid_spec_create_touchpad` / `AOAHID_PROFILE_TOUCHPAD` | Removed in 0.3.0. ... |`
   Replace it with a row documenting the reintroduction instead of a removal:
   `| `aoahid_spec_create_touchpad` / `AOAHID_PROFILE_TOUCHPAD` | Reintroduced in 0.4.0 as its own independent profile with its own `aoahid_touchpad_options` struct — not, as in 0.1.0–0.2.0, a `touchpad_button_count` field folded into `aoahid_touch_options`. See the "Touchpad" section below. |`
2. Read the automation-audit table row for `Touchscreen: Touchpad application (same factory, touchpad_button_count > 0)` if it still exists anywhere in the file (it should have been removed in `fa55417` — if you find no such row, skip this). Add a new standalone row for Touchpad in the same automation-audit table (the one containing the `Touchscreen, fixed MT` row), styled identically:
   `| Touchpad (`aoahid_spec_create_touchpad`) | Uses the same fixed-MT contact/count/Up automation as Touchscreen; derives a button-only frame with Contact Count zero via `aoahid_touchpad_button`. | Button changes are busy between contact-frame packets; contact and button edges cannot be coalesced away. | Contact domains, button count (may be zero), physical size, gesture, palm, and pointer-acceleration policy. | Button processing is a **[Specified Linux implementation observation]** at the cited revision; frame coordination is **[Guide policy]**. Conditional and **[Unverified on hardware]**. |`
3. Add a new `## Touchpad` section right after the existing `## Touchscreen` section (which ends around line 324 with the Contact Count table). Model it directly on the Touchscreen section's structure (Descriptor forms / Contact lifecycle / Contact Count and packetization) but: name the factory `aoahid_spec_create_touchpad`, the Application Usage `Digitizers Touch Pad 0x0d/0x05`, note the contact lifecycle is identical (same `aoahid_touch` state machine, shared with Touchscreen), and add a "Button lifecycle" subsection describing `aoahid_touchpad_button(node, button, pressed)` as a one-based, edge-guarded button state machine (mirror the button-lifecycle wording from the Mouse or Gamepad button sections elsewhere in this file — read one of those first) that stays inert when `button_count == 0`. State plainly that classification is always `AOAHID_ANDROID_CONDITIONAL` and cite the same Android-mouse-source-motion rationale already in this repo's history (see the `fa55417` commit message reproduced in Task 4/CHANGELOG) as the reason it is conditional rather than a portable candidate.

- [ ] **Step 2: `docs/API.md`**

Find the Touchscreen entry in this file's profile/function reference and add an equivalent Touchpad entry immediately after it (same format), listing `aoahid_touchpad_options`, `aoahid_spec_create_touchpad`, and `aoahid_touchpad_button`.

- [ ] **Step 3: `docs/DESIGN.md`**

Find wherever this file explains the Touchscreen state machine / TouchConfig design and add a short paragraph noting that Touchscreen and Touchpad now share one internal `TouchFields` representation and one state machine, differing only in Application Collection Usage, `button_count`, and `android_status`.

- [ ] **Step 4: `docs/FACT_AUDIT.md`**

Find the audit entries this file already has for Touchscreen (Linux/HUT citations for contact fields, Contact Count, Azimuth range, etc.) and add a Touchpad entry citing the same evidence for contacts, plus a citation for the Touch Pad Application Collection Usage (`0x0d/0x05`, HUT 1.7 §16) and for the `INPUT_PROP_POINTER` vs `INPUT_PROP_DIRECT` distinction (already present in the `fa55417` commit message — treat that as the fact to audit-cite here, in this file's existing evidence-label style).

- [ ] **Step 5: `docs/LIMITS.md`**

If this file lists per-profile limits (e.g. `maximum_contacts <= 16`, button count ceilings) for Touchscreen/Mouse/Gamepad, add a Touchpad row/paragraph: same contact limits as Touchscreen, `button_count` ceiling of 65535 (one 16-bit Usage range), `button_count == 0` explicitly allowed.

- [ ] **Step 6: `docs/EXAMPLES.md`**

Add `examples/c/profiles/touchpad.c` to whatever index/table this file keeps of the per-profile C examples (mirror the existing `touchscreen.c` entry).

- [ ] **Step 7: `docs/SOURCE_CONFLICTS.md`**

If this file has an entry discussing the Touchpad Application Collection's Android classification conflict/uncertainty (it likely does, given the detailed rationale in `fa55417`'s commit message — search for "Touchpad" and "INPUT_PROP_POINTER"), update it to describe Touchpad as a currently-implemented independent profile again rather than a removed one. If no such entry exists, skip this file.

- [ ] **Step 8: `docs/TARGET_MATRIX.md`**

If this file has a per-profile hardware-target verification row/column, add a Touchpad row in the same "not yet verified" state as every other profile in this table (this repo has explicitly not completed hardware verification for any profile — see the README's own disclaimer).

- [ ] **Step 9: Commit**

```bash
git add docs/PROFILES.md docs/API.md docs/DESIGN.md docs/FACT_AUDIT.md docs/LIMITS.md \
        docs/EXAMPLES.md docs/SOURCE_CONFLICTS.md docs/TARGET_MATRIX.md
git commit -m "Document the Touchpad profile"
```

---

### Task 10: Final full verification

**Files:** none (verification only)

- [ ] **Step 1: Clean full build with every optional component enabled**

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DAOAHID_BUILD_TESTS=ON -DAOAHID_BUILD_EXAMPLES=ON
cmake --build build -j
```

Expected: zero warnings (the project builds with `-Werror`/`/WX`), zero errors.

- [ ] **Step 2: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 100% pass, including `aoahid.golden-descriptors`, `aoahid.support-table`, `aoahid.abi`-prefixed tests, `aoahid.internal-thread`, `aoahid.transport`/integration tests, and unit tests.

- [ ] **Step 3: Run the binding conformance check standalone**

```bash
python3 tools/check_bindings.py
```

- [ ] **Step 4: Grep for any remaining stale reference**

```bash
grep -rn "touchpad_button_count\|AOAHID_CONTROLLER_" --include="*.c" --include="*.cpp" --include="*.h" --include="*.hpp" --include="*.py" --include="*.cs" --include="*.rs" .
```

Expected: no output (confirms no leftover reference to the pre-0.3.0 folded-in design, and confirms Task 1-9 did not accidentally resurrect the already-removed Joystick `application` field).

- [ ] **Step 5: `git status` and `git log --oneline -15` review**

```bash
git status
git log --oneline -15
```

Confirm the working tree is clean and every commit message is in English with no AI/assistant attribution of any kind (per the Global Constraints).

---

### Task 11: Push to GitHub and create the release

**Files:** none (git/GitHub operations only)

- [ ] **Step 1: Confirm remote and identity**

```bash
git remote -v
git config --local user.name
git config --local user.email
git log -1 --format='%an <%ae>'
gh auth status
```

Expected: `origin` points at `https://github.com/nemarpuc/Libaoa_hid.git`; local identity is `nemarpuc <nemarpuc@gmail.com>`; `gh` is authenticated as `nemarpuc`.

- [ ] **Step 2: Push `main`**

```bash
git push origin main
```

- [ ] **Step 3: Tag and create the GitHub release**

```bash
git tag -a v0.4.0 -m "v0.4.0"
git push origin v0.4.0
gh release create v0.4.0 \
  --title "v0.4.0" \
  --notes "$(sed -n '/^## \[0.4.0\]/,/^## \[0.3.0\]/p' CHANGELOG.md | sed '$d')"
```

(The `sed` extracts exactly the `[0.4.0]` section written in Task 4 Step 8, so the release notes and CHANGELOG stay identical; review the extracted text before running `gh release create` and trim the trailing blank line if the `sed '$d'` left one.)

- [ ] **Step 4: Verify**

```bash
gh release view v0.4.0
```

Report the release URL back to the user.
