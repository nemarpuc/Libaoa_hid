// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#pragma once

/* Zero-allocation C++20 typed node references. They add no wrapper-selected
 * values and do not own handles; the C API's documented zero-value transport
 * fallbacks and all lifetime/blocking rules remain those of aoahid.h.
 * Typed references are populated only by bind(), which checks the immutable
 * Node manifest before exposing profile-specific operations. */

#include "aoahid.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace aoa {

class node_ref {
  public:
    constexpr node_ref() noexcept = default;
    explicit constexpr node_ref(aoahid_node* value) noexcept : value_(value) {}
    [[nodiscard]] constexpr aoahid_node* native_handle() const noexcept { return value_; }
    [[nodiscard]] aoahid_result submit() const noexcept { return aoahid_node_submit(value_); }
    [[nodiscard]] aoahid_result submit_blocking(std::uint32_t deadline_ms) const noexcept {
        return aoahid_node_submit_blocking(value_, deadline_ms);
    }

  protected:
    aoahid_node* value_{};
};

class keyboard_node_ref final : public node_ref {
  public:
    constexpr keyboard_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result key(std::uint16_t usage, bool down) const noexcept {
        return aoahid_kbd(value_, usage, down ? 1U : 0U);
    }

  private:
    explicit constexpr keyboard_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, keyboard_node_ref&) noexcept;
};

class mouse_node_ref final : public node_ref {
  public:
    constexpr mouse_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result move(std::int32_t dx, std::int32_t dy) const noexcept {
        return aoahid_mouse_move(value_, dx, dy);
    }
    [[nodiscard]] aoahid_result scroll(std::int32_t wheel, std::int32_t pan) const noexcept {
        return aoahid_mouse_scroll(value_, wheel, pan);
    }
    [[nodiscard]] aoahid_result button(std::uint32_t index, bool pressed) const noexcept {
        return aoahid_mouse_button(value_, index, pressed ? 1U : 0U);
    }

  private:
    explicit constexpr mouse_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, mouse_node_ref&) noexcept;
};

/* Covers every allow-listed one-bit selector page: Consumer, System Control,
 * Camera keys, Telephony keys, or a caller-chosen HUT page. Which page a
 * bound Node speaks is decided entirely by the Spec it was opened with. */
class toggle_node_ref final : public node_ref {
  public:
    constexpr toggle_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result set(std::uint16_t usage, bool down) const noexcept {
        return aoahid_toggle(value_, usage, down ? 1U : 0U);
    }

  private:
    explicit constexpr toggle_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, toggle_node_ref&) noexcept;
};

class gamepad_node_ref final : public node_ref {
  public:
    constexpr gamepad_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result button(std::uint32_t index, bool pressed) const noexcept {
        return aoahid_gamepad_button(value_, index, pressed ? 1U : 0U);
    }
    [[nodiscard]] aoahid_result axis(std::size_t index, std::int32_t value) const noexcept {
        return aoahid_gamepad_set_axis(value_, index, value);
    }
    [[nodiscard]] aoahid_result dpad(bool up, bool down, bool right, bool left) const noexcept {
        return aoahid_dpad(value_, up ? 1U : 0U, down ? 1U : 0U, right ? 1U : 0U, left ? 1U : 0U);
    }

  private:
    explicit constexpr gamepad_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, gamepad_node_ref&) noexcept;
};

/* Always fixed-slot Multi-Touch. */
class touchscreen_node_ref final : public node_ref {
  public:
    constexpr touchscreen_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result touch(std::uint32_t contact_id, bool down, std::int32_t x,
                                      std::int32_t y,
                                      const aoahid_touch_extra* extra = nullptr) const noexcept {
        return aoahid_touch(value_, contact_id, down ? 1U : 0U, x, y, extra);
    }

  private:
    explicit constexpr touchscreen_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, touchscreen_node_ref&) noexcept;
};

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

class pen_node_ref final : public node_ref {
  public:
    constexpr pen_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result update(const aoahid_pen_sample& sample) const noexcept {
        return aoahid_pen_update(value_, &sample);
    }
    [[nodiscard]] aoahid_result depart() const noexcept { return aoahid_pen_depart(value_); }

  private:
    explicit constexpr pen_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, pen_node_ref&) noexcept;
};

class battery_node_ref final : public node_ref {
  public:
    constexpr battery_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result update(std::int32_t strength) const noexcept {
        return aoahid_battery_update(value_, 1U, strength);
    }
    [[nodiscard]] aoahid_result unknown() const noexcept {
        return aoahid_battery_update(value_, 0U, 0);
    }

  private:
    explicit constexpr battery_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, battery_node_ref&) noexcept;
};

class raw_node_ref final : public node_ref {
  public:
    constexpr raw_node_ref() noexcept = default;
    [[nodiscard]] aoahid_result report(std::span<const std::uint8_t> bytes) const noexcept {
        return aoahid_raw_submit(value_, bytes.data(), bytes.size());
    }

  private:
    explicit constexpr raw_node_ref(aoahid_node* value) noexcept : node_ref(value) {}
    friend aoahid_result bind(aoahid_node*, raw_node_ref&) noexcept;
};

namespace detail {
inline aoahid_result profile_kind(aoahid_node* value, aoahid_profile_kind& kind) noexcept {
    aoahid_capability_manifest manifest{};
    manifest.struct_size = static_cast<std::uint32_t>(sizeof(manifest));
    const aoahid_result result = aoahid_node_manifest(value, &manifest);
    if (result == AOAHID_OK) {
        kind = manifest.profile_kind;
    }
    return result;
}
} // namespace detail

/* These cold-path factories clear output before reading the immutable Node
 * manifest. A profile mismatch returns AOAHID_ERR_PARAM but deliberately does
 * not manufacture a C-ABI TLS diagnostic after the manifest call succeeded. */
[[nodiscard]] inline aoahid_result bind(aoahid_node* value, keyboard_node_ref& output) noexcept {
    output = keyboard_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_KEYBOARD)
        return AOAHID_ERR_PARAM;
    output = keyboard_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, mouse_node_ref& output) noexcept {
    output = mouse_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_MOUSE)
        return AOAHID_ERR_PARAM;
    output = mouse_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, toggle_node_ref& output) noexcept {
    output = toggle_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_TOGGLE)
        return AOAHID_ERR_PARAM;
    output = toggle_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, gamepad_node_ref& output) noexcept {
    output = gamepad_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_GAMEPAD)
        return AOAHID_ERR_PARAM;
    output = gamepad_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, touchscreen_node_ref& output) noexcept {
    output = touchscreen_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_TOUCHSCREEN)
        return AOAHID_ERR_PARAM;
    output = touchscreen_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, touchpad_node_ref& output) noexcept {
    output = touchpad_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_TOUCHPAD)
        return AOAHID_ERR_PARAM;
    output = touchpad_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, pen_node_ref& output) noexcept {
    output = pen_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_PEN)
        return AOAHID_ERR_PARAM;
    output = pen_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, battery_node_ref& output) noexcept {
    output = battery_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_BATTERY)
        return AOAHID_ERR_PARAM;
    output = battery_node_ref{value};
    return AOAHID_OK;
}

[[nodiscard]] inline aoahid_result bind(aoahid_node* value, raw_node_ref& output) noexcept {
    output = raw_node_ref{};
    aoahid_profile_kind kind{};
    const aoahid_result result = detail::profile_kind(value, kind);
    if (result != AOAHID_OK)
        return result;
    if (kind != AOAHID_PROFILE_RAW)
        return AOAHID_ERR_PARAM;
    output = raw_node_ref{value};
    return AOAHID_OK;
}

} // namespace aoa
