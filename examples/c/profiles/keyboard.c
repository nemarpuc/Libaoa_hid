// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Keyboard profile: a six-cell HID 1.11 Array over one contiguous Usage range.
 * The library packs the array and routes modifiers; the caller still chooses
 * the form, the Usage interval, the cell width, and the array length. */
#include "profile_example.h"

static const char* const k_send_sequence[] = {
    "aoahid_kbd(node, 0x04, 1)   press Keyboard a/A; the library packs the Array",
    "aoahid_node_submit(node)    one accepted edge, one request 57",
    "aoahid_kbd(node, 0x04, 0)   release it; a held edge must reach one report",
    "aoahid_node_submit(node)    submitting an opposite edge too early is BUSY", NULL};

int main(void) {
    aoahid_keyboard_options options = {0};
    aoahid_spec* spec = NULL;
    aoahid_result result;

    options.struct_size = (uint32_t)sizeof options;
    options.rollover = AOAHID_KEYBOARD_ARRAY;
    options.array_length = 6U;
    /* Keyboard a/A through Keyboard Application, HUT 1.7 section 10. */
    options.usage_minimum = 0x04U;
    options.usage_maximum = 0x65U;
    options.usage_bit_width = 8U;

    result = aoahid_spec_create_keyboard(&options, &spec);
    return aoahid_example_run("keyboard", AOAHID_PROFILE_KEYBOARD, result, spec, k_send_sequence);
}
