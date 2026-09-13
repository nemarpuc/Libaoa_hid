// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Exercises typed C++ ownership wrappers over the stable C ABI without adding
 * option defaults or changing native policy. */
#include "aoahid.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

static_assert(sizeof(aoa::node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::keyboard_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::mouse_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::toggle_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::gamepad_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::touchscreen_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::pen_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::battery_node_ref) == sizeof(aoahid_node*));
static_assert(sizeof(aoa::raw_node_ref) == sizeof(aoahid_node*));
static_assert(std::is_trivially_copyable_v<aoa::keyboard_node_ref>);
static_assert(std::is_trivially_copyable_v<aoa::toggle_node_ref>);
static_assert(std::is_trivially_copyable_v<aoa::raw_node_ref>);
static_assert(std::is_default_constructible_v<aoa::keyboard_node_ref>);
static_assert(std::is_default_constructible_v<aoa::toggle_node_ref>);
static_assert(std::is_default_constructible_v<aoa::raw_node_ref>);
static_assert(!std::is_constructible_v<aoa::keyboard_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::mouse_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::toggle_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::gamepad_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::touchscreen_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::pen_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::battery_node_ref, aoahid_node*>);
static_assert(!std::is_constructible_v<aoa::raw_node_ref, aoahid_node*>);
static_assert(!std::is_convertible_v<aoa::keyboard_node_ref, aoa::toggle_node_ref>);
static_assert(!std::is_convertible_v<aoa::gamepad_node_ref, aoa::touchscreen_node_ref>);
static_assert(std::is_same_v<decltype(aoa::bind(nullptr, std::declval<aoa::keyboard_node_ref&>())),
                             aoahid_result>);
static_assert(std::is_same_v<decltype(aoa::bind(nullptr, std::declval<aoa::toggle_node_ref&>())),
                             aoahid_result>);
static_assert(
    std::is_same_v<decltype(aoa::bind(nullptr, std::declval<aoa::touchscreen_node_ref&>())),
                   aoahid_result>);

int main() {
    aoa::keyboard_node_ref keyboard{};
    aoa::toggle_node_ref toggle{};
    aoa::touchscreen_node_ref touchscreen{};
    if (keyboard.native_handle() != nullptr || aoa::bind(nullptr, keyboard) != AOAHID_ERR_PARAM ||
        keyboard.native_handle() != nullptr || aoa::bind(nullptr, toggle) != AOAHID_ERR_PARAM ||
        toggle.native_handle() != nullptr || aoa::bind(nullptr, touchscreen) != AOAHID_ERR_PARAM ||
        touchscreen.native_handle() != nullptr) {
        return 1;
    }
    return 0;
}
