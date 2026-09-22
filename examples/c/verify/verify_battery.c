// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Sends a sequence of Battery Strength values, then the explicit unknown
 * (Null) state, so a person can check whether Android actually surfaces this
 * as battery metadata for the accessory.
 *
 * Unlike every other profile in this directory, Battery Strength is NOT an
 * ordinary input event: per docs/FACT_AUDIT.md A-21, the audited Linux kernel
 * assigns it EV_PWR and excludes it from the normal input-event path via
 * hidinput_setup_battery/hidinput_update_battery, associating it with a
 * `power_supply` device instead. That means:
 *   - `adb shell getevent` will NOT show this, even if everything works.
 *   - `hidinput_update_battery` silently ignores a raw value of exactly zero
 *     and any value outside the declared logical range, so "nothing changed"
 *     after sending 0 is expected kernel behavior, not proof of failure.
 *   - whether Android associates the resulting `power_supply` with this
 *     accessory's `InputDevice` at all, and whether any app-visible battery
 *     API reflects it, is OEM/kernel-config (CONFIG_HID_BATTERY_STRENGTH) and
 *     Android-version dependent and is NOT established by this repository.
 *
 * Check, in order of likelihood to show something:
 *   adb shell dumpsys battery
 *   adb shell sh -c 'for f in /sys/class/power_supply/\*; do echo "== $f =="; \
 *     cat "$f/uevent"; done'
 * Look for a power_supply entry that appears/changes only while this program
 * is running and disappears when the node closes.
 *
 * Before running: see docs/QUICKSTART.md for cable/udev setup. Ctrl+C to stop
 * early. */
#include "verify_common.h"

static int send_strength(aoahid_node* node, int32_t strength, const char* label) {
    printf("\n-- sending known Battery Strength: %d (%s) --\n", strength, label);
    printf("   check `adb shell dumpsys battery` and /sys/class/power_supply now\n");
    aoahid_result result = aoahid_battery_update(node, 1U, strength);
    if (result != AOAHID_OK)
        return verify_fail("battery update (known value)", result);
    result = aoahid_node_submit_blocking(node, 500U);
    if (result != AOAHID_OK)
        return verify_fail("submit battery update", result);
    verify_sleep_ms(2000);
    return 0;
}

int main(void) {
    aoahid_context* context = NULL;
    aoahid_device* device = NULL;
    if (verify_open_first_device(&context, &device) != 0)
        return 1;

    aoahid_battery_options options = {0};
    options.struct_size = (uint32_t)sizeof options;
    /* Same field shape as examples/c/profiles/battery.c: 0..100 in 7 bits
     * (0..127 representable), leaving 101..127 free for the Null encoding. */
    options.strength = (aoahid_integer_field){0, 100, 7U, {0, 0, 0, 0, 0}};
    options.enable_unknown_null_state = 1U;

    aoahid_spec* spec = NULL;
    aoahid_result result = aoahid_spec_create_battery(&options, &spec);
    if (result != AOAHID_OK) {
        aoahid_device_close(device);
        aoahid_context_destroy(context);
        return verify_fail("battery spec create", result);
    }

    const aoahid_node_options node_options = {(uint32_t)sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* node = NULL;
    result = aoahid_node_open(device, spec, &node_options, &node);
    aoahid_spec_release(spec);
    if (result != AOAHID_OK) {
        aoahid_device_close(device);
        aoahid_context_destroy(context);
        return verify_fail("battery node open", result);
    }

    printf("Starting in 3 seconds. This profile produces no getevent output by design;\n");
    printf("see the comment at the top of this file for what to check instead.\n");
    verify_sleep_ms(3000);

    int exit_code = 0;
    if (send_strength(node, 10, "low") != 0)
        exit_code = 1;
    if (exit_code == 0 && send_strength(node, 50, "mid") != 0)
        exit_code = 1;
    if (exit_code == 0 && send_strength(node, 90, "high") != 0)
        exit_code = 1;

    if (exit_code == 0) {
        printf("\n-- sending explicit unknown state (Null encoding) --\n");
        printf("   check whether the last known value now reads as unknown/absent\n");
        aoahid_result r = aoahid_battery_update(node, 0U, 0);
        if (r != AOAHID_OK) {
            exit_code = verify_fail("battery update (unknown)", r);
        } else {
            r = aoahid_node_submit_blocking(node, 500U);
            if (r != AOAHID_OK)
                exit_code = verify_fail("submit battery update (unknown)", r);
            else
                verify_sleep_ms(2000);
        }
    }

    if (exit_code == 0) {
        printf("\ndone. Record in TARGET_MATRIX.md what you actually observed:\n"
               "  - dumpsys battery / power_supply changed to match each value ->\n"
               "    evidence toward Hardware-verified\n"
               "  - kernel log shows hid-input registering a power_supply, but nothing in\n"
               "    dumpsys battery or any app battery API -> record as Conditional\n"
               "    (kernel accepted it, Android did not surface it)\n"
               "  - no power_supply appeared at all, and the kernel log shows no\n"
               "    hidinput_setup_battery activity -> check CONFIG_HID_BATTERY_STRENGTH on\n"
               "    that exact kernel before concluding this is a library defect\n");
    }

    aoahid_node_close(node);
    aoahid_device_close(device);
    aoahid_context_destroy(context);
    return exit_code;
}
