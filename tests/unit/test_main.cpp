// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Runs the allocation-free HID/profile unit suite; integration executables
 * own transport and threading coverage. */
#include "test.hpp"

int g_failures = 0;

int main() {
    test_item_writer();
    test_profiles();
    test_transport();
    test_identity();
    return g_failures == 0 ? 0 : 1;
}
