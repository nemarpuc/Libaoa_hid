// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Battery profile: one Battery Strength field with the null state enabled so
 * an unknown reading can be reported instead of a fabricated value. */
#include "profile_example.h"

static const char* const k_send_sequence[] = {
    "aoahid_battery_update(node, 1, 75)   a known Strength value",
    "aoahid_node_submit(node)             a known zero stays a known value",
    "aoahid_battery_update(node, 0, 0)    explicit unknown, if the Spec enables it",
    "aoahid_node_submit(node)             emits the out-of-range Null encoding", NULL};

int main(void) {
    aoahid_battery_options options = {0};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.strength = (aoahid_integer_field){0, 100, 7U, {0, 0, 0, 0, 0}};
    options.enable_unknown_null_state = 1U;

    result = aoahid_spec_create_battery(&options, &spec);
    return aoahid_example_run("battery", AOAHID_PROFILE_BATTERY, result, spec, k_send_sequence);
}
