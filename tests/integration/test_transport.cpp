// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * This suite checks the exact AOA control sequence and transfer lifecycle at
 * the transport boundary. The deterministic backend contains no HID policy.
 */

#include "api/internal.hpp"
#include "fake_libusb/libusb.h"
#include "test.hpp"
#include "transport/transport.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_probe {

std::atomic<bool> active{false};
std::atomic<std::size_t> count{0U};

void record() noexcept {
    if (active.load(std::memory_order_relaxed)) {
        count.fetch_add(1U, std::memory_order_relaxed);
    }
}

void* allocate(const std::size_t requested) {
    record();
    if (void* storage = std::malloc(requested == 0U ? 1U : requested); storage != nullptr) {
        return storage;
    }
    throw std::bad_alloc();
}

void* allocate_aligned(const std::size_t requested, const std::size_t alignment) {
    record();
#if defined(_MSC_VER)
    if (void* storage = _aligned_malloc(requested == 0U ? 1U : requested, alignment);
        storage != nullptr) {
        return storage;
    }
#else
    if (requested > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        throw std::bad_alloc();
    }
    const std::size_t adjusted =
        std::max(alignment, ((requested + alignment - 1U) / alignment) * alignment);
    if (void* storage = std::aligned_alloc(alignment, adjusted); storage != nullptr) {
        return storage;
    }
#endif
    throw std::bad_alloc();
}

void deallocate_aligned(void* storage) noexcept {
#if defined(_MSC_VER)
    _aligned_free(storage);
#else
    std::free(storage);
#endif
}

void begin() noexcept {
    count.store(0U, std::memory_order_relaxed);
    active.store(true, std::memory_order_release);
}

std::size_t finish() noexcept {
    active.store(false, std::memory_order_release);
    return count.load(std::memory_order_relaxed);
}

} // namespace allocation_probe

void* operator new(const std::size_t size) { return allocation_probe::allocate(size); }
void* operator new[](const std::size_t size) { return allocation_probe::allocate(size); }
void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* storage) noexcept { std::free(storage); }
void operator delete[](void* storage) noexcept { std::free(storage); }
void operator delete(void* storage, const std::size_t) noexcept { std::free(storage); }
void operator delete[](void* storage, const std::size_t) noexcept { std::free(storage); }
void operator delete(void* storage, const std::nothrow_t&) noexcept { std::free(storage); }
void operator delete[](void* storage, const std::nothrow_t&) noexcept { std::free(storage); }

#if defined(__cpp_aligned_new)
void* operator new(const std::size_t size, const std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](const std::size_t size, const std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new(const std::size_t size, const std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](const std::size_t size, const std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* storage, const std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, const std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete(void* storage, const std::size_t, const std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, const std::size_t, const std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete(void* storage, const std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, const std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
#endif

namespace {

using aoa::transport::Candidate;
using aoa::transport::Device;
using aoa::transport::DeviceConfig;
using aoa::transport::PreparedTransfer;
using aoa::transport::Runtime;

struct CompletionState {
    std::size_t calls{};
    aoahid_result result{AOAHID_OK};
    std::int32_t native_status{};
};

struct BlockingCompletionState {
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<aoahid_result> result{AOAHID_OK};
};

void completed(void* user, const aoahid_result result, const std::int32_t native_status) noexcept {
    auto* state = static_cast<CompletionState*>(user);
    if (state != nullptr) {
        ++state->calls;
        state->result = result;
        state->native_status = native_status;
    }
}

void blocking_completed(void* user, const aoahid_result result,
                        const std::int32_t native_status) noexcept {
    static_cast<void>(native_status);
    auto* state = static_cast<BlockingCompletionState*>(user);
    if (state == nullptr) {
        return;
    }
    state->result.store(result, std::memory_order_relaxed);
    state->entered.store(true, std::memory_order_release);
    state->entered.notify_all();
    state->release.wait(false, std::memory_order_acquire);
}

DeviceConfig current_mode_config() {
    DeviceConfig config{};
    config.event_mode = AOAHID_EVENT_CALLER_POLL;
    config.accept_future_versions = false;
    config.control_timeout_ms = 20U;
    config.send_timeout_ms = 20U;
    config.descriptor_fragment_bytes = 64U;
    config.pool_slots = 2U;
    config.maximum_report_bytes = 64U;
    config.first_report_attempts = 2U;
    config.first_report_backoff_us = 1U;
    config.claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    config.interface_number = -1;
    return config;
}

Candidate add_candidate(const std::uint8_t address, const std::array<std::uint8_t, 2U>& path,
                        const std::uint16_t vendor = 0x18D1U,
                        const std::uint16_t product = 0x2D00U) {
    const aoahid_fake_libusb_device_config fake{1U,        address,       path.data(), path.size(),
                                                vendor,    product,       2U,          "serial",
                                                "Android", LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);
    Candidate candidate{};
    candidate.bus = fake.bus;
    candidate.address = fake.address;
    candidate.port_path.assign(path.begin(), path.end());
    candidate.vendor_id = fake.vendor_id;
    candidate.product_id = fake.product_id;
    candidate.serial = fake.serial;
    candidate.product = fake.product;
    return candidate;
}

std::vector<aoahid_fake_libusb_control_record> controls_for(const std::uint8_t request) {
    std::vector<aoahid_fake_libusb_control_record> records;
    const std::size_t count = aoahid_fake_libusb_control_count();
    for (std::size_t index = 0U; index < count; ++index) {
        aoahid_fake_libusb_control_record record{};
        AOAHID_CHECK(aoahid_fake_libusb_get_control(index, &record) == LIBUSB_SUCCESS);
        if (record.request == request) {
            records.push_back(record);
        }
    }
    return records;
}

aoahid_fake_libusb_event_stats event_stats() {
    aoahid_fake_libusb_event_stats stats{};
    AOAHID_CHECK(aoahid_fake_libusb_get_event_stats(&stats) == LIBUSB_SUCCESS);
    return stats;
}

std::vector<std::vector<std::uint8_t>> control_payloads_for(const std::uint8_t request) {
    std::vector<std::vector<std::uint8_t>> payloads;
    const std::size_t count = aoahid_fake_libusb_control_count();
    for (std::size_t index = 0U; index < count; ++index) {
        aoahid_fake_libusb_control_record record{};
        AOAHID_CHECK(aoahid_fake_libusb_get_control(index, &record) == LIBUSB_SUCCESS);
        if (record.request != request)
            continue;
        std::vector<std::uint8_t> payload(record.length);
        AOAHID_CHECK(aoahid_fake_libusb_copy_control_data(index, payload.data(), payload.size()) ==
                     payload.size());
        payloads.push_back(std::move(payload));
    }
    return payloads;
}

std::uint64_t extract_report_value(const std::vector<std::uint8_t>& report,
                                   const aoa::hid::FieldLayout& field) {
    std::uint64_t value = 0U;
    for (std::uint8_t bit = 0U; bit < field.bit_width; ++bit) {
        const std::size_t position = field.bit_offset + bit;
        const std::uint64_t byte = report[position / 8U];
        value |= ((byte >> (position & 7U)) & std::uint64_t{1}) << bit;
    }
    return value;
}

const aoa::hid::FieldLayout* profile_field(const aoahid_spec* spec,
                                           const aoa::hid::FieldSemantic semantic,
                                           const std::uint16_t instance = 0U) {
    for (const auto& candidate : spec->layout.fields) {
        if (candidate.semantic == semantic && candidate.instance == instance)
            return &candidate;
    }
    return nullptr;
}

void poll_until(Runtime* runtime, CompletionState* state) {
    for (std::size_t attempt = 0U; attempt < 20U && state->calls == 0U; ++attempt) {
        AOAHID_CHECK(runtime->poll(1U) == AOAHID_OK);
    }
    AOAHID_CHECK(state->calls == 1U);
}

void test_runtime_initialization_options() {
    aoahid_fake_libusb_reset();
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_init_context_count() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_init_option_count() == 2U);

    aoahid_fake_libusb_init_option_record log_level{};
    aoahid_fake_libusb_init_option_record log_callback{};
    AOAHID_CHECK(aoahid_fake_libusb_get_init_option(0U, &log_level) == LIBUSB_SUCCESS);
    AOAHID_CHECK(aoahid_fake_libusb_get_init_option(1U, &log_callback) == LIBUSB_SUCCESS);
    AOAHID_CHECK(log_level.option == LIBUSB_OPTION_LOG_LEVEL);
    AOAHID_CHECK(log_level.integer_value == LIBUSB_LOG_LEVEL_NONE);
    AOAHID_CHECK(log_level.callback_present == 0);
    AOAHID_CHECK(log_callback.option == LIBUSB_OPTION_LOG_CB);
    AOAHID_CHECK(log_callback.callback_present == 1);

    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_registration_reservations_and_async_results() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(3U, {2U, 4U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    DeviceConfig config = current_mode_config();
    config.pool_slots = 3U;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);
    AOAHID_CHECK(protocol == 2U);

    std::array<std::uint8_t, 130U> descriptor{};
    for (std::size_t index = 0U; index < descriptor.size(); ++index) {
        descriptor[index] = static_cast<std::uint8_t>(index);
    }
    AOAHID_CHECK(device->register_hid(7U, descriptor.data(), descriptor.size()) == AOAHID_OK);
    const auto registrations = controls_for(54U);
    AOAHID_CHECK(registrations.size() == 1U);
    if (!registrations.empty()) {
        AOAHID_CHECK(registrations[0].request_type == 0x40U && registrations[0].value == 7U &&
                     registrations[0].index == descriptor.size() && registrations[0].length == 0U &&
                     registrations[0].asynchronous == 0);
    }
    const auto fragments = controls_for(56U);
    AOAHID_CHECK(fragments.size() == 3U);
    if (fragments.size() == 3U) {
        AOAHID_CHECK(fragments[0].value == 7U && fragments[0].index == 0U &&
                     fragments[0].length == 64U);
        AOAHID_CHECK(fragments[1].value == 7U && fragments[1].index == 64U &&
                     fragments[1].length == 64U);
        AOAHID_CHECK(fragments[2].value == 7U && fragments[2].index == 128U &&
                     fragments[2].length == 2U);
        AOAHID_CHECK(fragments[0].request_type == 0x40U && fragments[0].asynchronous == 0);
    }
    const auto descriptor_payloads = control_payloads_for(56U);
    AOAHID_CHECK(descriptor_payloads.size() == 3U);
    if (descriptor_payloads.size() == 3U) {
        AOAHID_CHECK(std::equal(descriptor_payloads[0].begin(), descriptor_payloads[0].end(),
                                descriptor.begin()));
        AOAHID_CHECK(std::equal(descriptor_payloads[1].begin(), descriptor_payloads[1].end(),
                                descriptor.begin() + 64));
        AOAHID_CHECK(std::equal(descriptor_payloads[2].begin(), descriptor_payloads[2].end(),
                                descriptor.begin() + 128));
    }

    aoa::transport::ReservationToken reservation7{};
    aoa::transport::ReservationToken reservation8{};
    aoa::transport::ReservationToken reservation9{};
    AOAHID_CHECK(device->set_reservation(7U, 1U, &reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(8U, 1U, &reservation8) == AOAHID_OK);
    CompletionState first{};
    CompletionState second{};
    CompletionState shared{};
    PreparedTransfer prepared_first{};
    PreparedTransfer prepared_second{};
    PreparedTransfer prepared_shared{};
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &shared, &completed, &prepared_shared) == AOAHID_OK);
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &first, &completed, &prepared_first) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(7U, reservation7, 3U, &first, &completed, &prepared_first) ==
                 AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(9U, {}, 3U, &second, &completed, &prepared_second) ==
                 AOAHID_ERR_BUSY);
    AOAHID_CHECK(device->acquire(8U, reservation8, 3U, &second, &completed, &prepared_second) ==
                 AOAHID_OK);
    device->abandon(prepared_shared);
    device->abandon(prepared_first);
    device->abandon(prepared_second);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation8) == AOAHID_OK);

    AOAHID_CHECK(device->set_reservation(7U, 0U, &reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(8U, 2U, &reservation8) == AOAHID_OK);
    AOAHID_CHECK(device->set_reservation(9U, 1U, &reservation9) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(device->clear_reservation(reservation7) == AOAHID_OK);
    AOAHID_CHECK(device->clear_reservation(reservation8) == AOAHID_OK);

    CompletionState retry{};
    PreparedTransfer retry_transfer{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &retry, &completed, &retry_transfer) == AOAHID_OK);
    retry_transfer.payload[0] = 1U;
    retry_transfer.payload[1] = 2U;
    retry_transfer.payload[2] = 3U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 3, 0U);
    AOAHID_CHECK(device->submit_first(retry_transfer, 3U, 11U) == AOAHID_OK);
    poll_until(runtime, &retry);
    AOAHID_CHECK(retry.result == AOAHID_OK);
    const auto retried_events = controls_for(57U);
    AOAHID_CHECK(retried_events.size() == 2U);
    for (const aoahid_fake_libusb_control_record event : retried_events) {
        AOAHID_CHECK(event.request_type == 0x40U && event.value == 7U && event.index == 0U &&
                     event.length == 3U && event.asynchronous == 1);
    }
    const auto retried_payloads = control_payloads_for(57U);
    AOAHID_CHECK(retried_payloads.size() == 2U);
    for (const auto& payload : retried_payloads) {
        AOAHID_CHECK(payload == std::vector<std::uint8_t>({1U, 2U, 3U}));
    }

    CompletionState short_transfer{};
    PreparedTransfer short_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &short_transfer, &completed, &short_prepared) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 2, 0U);
    AOAHID_CHECK(device->submit(short_prepared, 3U, 23U) == AOAHID_OK);
    poll_until(runtime, &short_transfer);
    AOAHID_CHECK(short_transfer.result == AOAHID_ERR_SHORT_TRANSFER);
    aoa::transport::ErrorInfo short_error{};
    AOAHID_CHECK(device->latched_error(&short_error) == AOAHID_ERR_SHORT_TRANSFER);
    AOAHID_CHECK(short_error.hid_id == 7U && short_error.report_id == 23U);
    AOAHID_CHECK(device->latched_error() == AOAHID_OK);

    CompletionState overflow{};
    PreparedTransfer overflow_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 3U, &overflow, &completed, &overflow_prepared) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_OVERFLOW, 0, 0U);
    AOAHID_CHECK(device->submit(overflow_prepared, 3U, 29U) == AOAHID_OK);
    poll_until(runtime, &overflow);
    AOAHID_CHECK(overflow.result == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow.native_status == LIBUSB_TRANSFER_OVERFLOW);
    aoa::transport::ErrorInfo overflow_error{};
    AOAHID_CHECK(device->latched_error(&overflow_error) == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow_error.hid_id == 7U && overflow_error.report_id == 29U);
    AOAHID_CHECK(overflow_error.length == 3U);
    AOAHID_CHECK(device->latched_error() == AOAHID_OK);

    CompletionState removed{};
    PreparedTransfer removed_prepared{};
    AOAHID_CHECK(device->acquire(7U, {}, 2U, &removed, &completed, &removed_prepared) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(device->submit(removed_prepared, 2U, 47U) == AOAHID_OK);
    poll_until(runtime, &removed);
    AOAHID_CHECK(removed.result == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(!device->present());
    PreparedTransfer rejected{};
    AOAHID_CHECK(device->acquire(7U, {}, 1U, &removed, &completed, &rejected) ==
                 AOAHID_ERR_NO_DEVICE);
    aoa::transport::ErrorInfo removed_error{};
    AOAHID_CHECK(device->latched_error(&removed_error) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(removed_error.hid_id == 7U && removed_error.report_id == 47U);
    AOAHID_CHECK(device->latched_error(&removed_error) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(removed_error.report_id == 47U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_uncertain_register_attempts_unregister_and_preserves_error() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(4U, {2U, 5U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    const DeviceConfig config = current_mode_config();
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 3U> descriptor{0x05U, 0x01U, 0xC0U};
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_TIMEOUT);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(19U, descriptor.data(), descriptor.size()) ==
                 AOAHID_ERR_TIMEOUT);

    const auto registrations = controls_for(54U);
    const auto cleanups = controls_for(55U);
    AOAHID_CHECK(registrations.size() == 1U);
    AOAHID_CHECK(cleanups.size() == 1U);
    if (!cleanups.empty()) {
        AOAHID_CHECK(cleanups[0].value == 19U && cleanups[0].index == 0U &&
                     cleanups[0].length == 0U);
    }
    const aoa::transport::ErrorInfo failure = aoa::transport::last_error();
    AOAHID_CHECK(failure.result == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(failure.native_status == LIBUSB_ERROR_TIMEOUT);
    AOAHID_CHECK(failure.aoa_request == 54 && failure.hid_id == 19U);
    AOAHID_CHECK(failure.length == descriptor.size());

    struct RegisterFailureCase {
        int native_status;
        aoahid_result expected;
        std::uint16_t hid_id;
    };
    static constexpr std::array<RegisterFailureCase, 2U> ambiguous_failures{
        RegisterFailureCase{LIBUSB_ERROR_IO, AOAHID_ERR_IO, 20U},
        RegisterFailureCase{1, AOAHID_ERR_SHORT_TRANSFER, 21U}};
    for (const RegisterFailureCase attempt : ambiguous_failures) {
        const std::size_t cleanup_count = controls_for(55U).size();
        aoahid_fake_libusb_queue_control_result(attempt.native_status);
        aoahid_fake_libusb_queue_control_result(LIBUSB_SUCCESS);
        AOAHID_CHECK(device->register_hid(attempt.hid_id, descriptor.data(), descriptor.size()) ==
                     attempt.expected);
        AOAHID_CHECK(controls_for(55U).size() == cleanup_count + 1U);
        const aoa::transport::ErrorInfo attempt_error = aoa::transport::last_error();
        AOAHID_CHECK(attempt_error.result == attempt.expected);
        AOAHID_CHECK(attempt_error.native_status == attempt.native_status);
        AOAHID_CHECK(attempt_error.aoa_request == 54 && attempt_error.hid_id == attempt.hid_id);
    }

    // A setup STALL is an explicit target rejection, unlike timeout/I/O/short
    // completion ambiguity, so there is no possibly-live request-54 entry to
    // clean up.
    const std::size_t cleanup_count = controls_for(55U).size();
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(22U, descriptor.data(), descriptor.size()) ==
                 AOAHID_ERR_STALL);
    AOAHID_CHECK(controls_for(55U).size() == cleanup_count);

    // On disconnect the audited Android gadget destroys every HID registration;
    // request 55 cannot be delivered and is unnecessary.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_NO_DEVICE);
    AOAHID_CHECK(device->register_hid(24U, descriptor.data(), descriptor.size()) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(controls_for(55U).size() == cleanup_count);
    AOAHID_CHECK(!device->present());

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_descriptor_failure_unregisters_and_preserves_error() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(12U, {2U, 6U});

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    const DeviceConfig config = current_mode_config();
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);

    std::array<std::uint8_t, 130U> descriptor{};
    aoahid_fake_libusb_queue_control_result(LIBUSB_SUCCESS); // request 54
    aoahid_fake_libusb_queue_control_result(64);             // request 56, offset 0
    aoahid_fake_libusb_queue_control_result(1);              // request 56, offset 64
    // The cleanup result must not replace the partial request-56 diagnostic.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(device->register_hid(23U, descriptor.data(), descriptor.size()) ==
                 AOAHID_ERR_SHORT_TRANSFER);

    const auto fragments = controls_for(56U);
    AOAHID_CHECK(fragments.size() == 2U);
    if (fragments.size() == 2U) {
        AOAHID_CHECK(fragments[0].value == 23U && fragments[0].index == 0U &&
                     fragments[0].length == 64U);
        AOAHID_CHECK(fragments[1].value == 23U && fragments[1].index == 64U &&
                     fragments[1].length == 64U);
    }
    const auto cleanups = controls_for(55U);
    AOAHID_CHECK(cleanups.size() == 1U);
    if (!cleanups.empty()) {
        AOAHID_CHECK(cleanups[0].value == 23U);
    }
    const aoa::transport::ErrorInfo failure = aoa::transport::last_error();
    AOAHID_CHECK(failure.result == AOAHID_ERR_SHORT_TRANSFER);
    AOAHID_CHECK(failure.native_status == 1);
    AOAHID_CHECK(failure.aoa_request == 56 && failure.hid_id == 23U);
    AOAHID_CHECK(failure.offset == 64U && failure.length == 64U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_protocol_short_response_preserves_not_aoa_diagnostic() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(13U, {2U, 7U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    aoahid_fake_libusb_queue_control_result(1);

    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    const DeviceConfig config = current_mode_config();
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) ==
                 AOAHID_ERR_NOT_AOA);
    AOAHID_CHECK(device == nullptr && protocol == 0U);
    const aoa::transport::ErrorInfo failure = aoa::transport::last_error();
    AOAHID_CHECK(failure.result == AOAHID_ERR_NOT_AOA);
    AOAHID_CHECK(failure.native_status == 1);
    AOAHID_CHECK(failure.aoa_request == 51 && failure.offset == 0U && failure.length == 2U);

    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_error_report_id_is_published_before_user_callback_returns() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(10U, {3U, 8U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_INTERNAL_THREAD, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);

    BlockingCompletionState completion{};
    PreparedTransfer prepared{};
    AOAHID_CHECK(device->acquire(37U, {}, 2U, &completion, &blocking_completed, &prepared) ==
                 AOAHID_OK);
    prepared.payload[0] = 91U;
    prepared.payload[1] = 1U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(device->submit(prepared, 2U, 91U) == AOAHID_OK);

    std::atomic<aoahid_result> poll_result{AOAHID_ERR_INTERNAL};
    std::thread event_thread(
        [&] { poll_result.store(runtime->poll(100U), std::memory_order_release); });
    completion.entered.wait(false, std::memory_order_acquire);

    aoa::transport::ErrorInfo error{};
    AOAHID_CHECK(device->latched_error(&error) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(error.hid_id == 37U && error.report_id == 91U);
    AOAHID_CHECK(error.length == 2U);

    completion.release.store(true, std::memory_order_release);
    completion.release.notify_all();
    event_thread.join();
    AOAHID_CHECK(poll_result.load(std::memory_order_acquire) == AOAHID_OK);
    AOAHID_CHECK(completion.result.load(std::memory_order_relaxed) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(device->drained());

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_current_mode_open_and_explicit_claim() {
    aoahid_fake_libusb_reset();
    const auto candidate = add_candidate(5U, {1U, 6U}, 0x1234U, 0x5678U);

    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.claim_policy = AOAHID_INTERFACE_CLAIM_EXPLICIT;
    config.interface_number = 7;

    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);
    AOAHID_CHECK(protocol == 2U);

    const auto probes = controls_for(51U);
    AOAHID_CHECK(probes.size() == 1U);
    if (!probes.empty()) {
        AOAHID_CHECK(probes[0].request_type == 0xC0U && probes[0].value == 0U &&
                     probes[0].index == 0U && probes[0].length == 2U &&
                     probes[0].asynchronous == 0);
    }
    AOAHID_CHECK(controls_for(52U).empty());
    AOAHID_CHECK(controls_for(53U).empty());
    AOAHID_CHECK(controls_for(58U).empty());

    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 1U);
    aoahid_fake_libusb_claim_record claim{};
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(0U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 7 && claim.release == 0);
    delete device;
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U);
    AOAHID_CHECK(aoahid_fake_libusb_get_claim(1U, &claim) == LIBUSB_SUCCESS);
    AOAHID_CHECK(claim.interface_number == 7 && claim.release == 1);
    delete runtime;
    aoahid_fake_libusb_reset();
}

aoahid_context_options public_context_options();
aoahid_device_options public_device_options();

void test_legacy_accessory_options_are_unsupported_before_io() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(6U, {1U, 7U});
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
    aoahid_device_options options = public_device_options();
    options.startup_mode = AOAHID_START_ACCESSORY_MODE;
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSUPPORTED);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "device.startup_mode");
    }

    using LegacyStringMember = const char* aoahid_aoa_strings::*;
    const std::array<LegacyStringMember, 6U> legacy_string_members{
        &aoahid_aoa_strings::manufacturer, &aoahid_aoa_strings::model,
        &aoahid_aoa_strings::description,  &aoahid_aoa_strings::version,
        &aoahid_aoa_strings::uri,          &aoahid_aoa_strings::serial};
    for (const LegacyStringMember member : legacy_string_members) {
        options = public_device_options();
        options.accessory_strings.*member = "retired";
        device = nullptr;
        AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                     AOAHID_ERR_UNSUPPORTED);
        AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);
        AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
        AOAHID_CHECK(aoahid_last_error()->field != nullptr);
        if (aoahid_last_error()->field != nullptr) {
            AOAHID_CHECK(std::string_view(aoahid_last_error()->field) ==
                         "device.legacy_accessory_fields");
        }
    }

    options = public_device_options();
    options.enable_deprecated_audio_mode = 1U;
    device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSUPPORTED);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) ==
                     "device.legacy_accessory_fields");
    }

    // This retained scalar is an ABI tombstone, not a Mode-B request: nonzero
    // values are deliberately ignored and the only control request is the
    // current-mode protocol probe.
    options = public_device_options();
    options.reenumeration_timeout_ms = 1234U;
    device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
    AOAHID_CHECK(device != nullptr);
    AOAHID_CHECK(controls_for(51U).size() == 1U);
    AOAHID_CHECK(controls_for(52U).empty() && controls_for(53U).empty() &&
                 controls_for(58U).empty());
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);

    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_required_device_policies_precede_usb_io() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(6U, {1U, 7U});
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

    using RequiredBytePolicy = std::uint32_t aoahid_device_options::*;
    const std::array<RequiredBytePolicy, 4U> required_byte_policies{
        &aoahid_device_options::aoa_descriptor_wire_policy_bytes,
        &aoahid_device_options::linux_descriptor_policy_bytes,
        &aoahid_device_options::target_ep0_data_policy_bytes,
        &aoahid_device_options::host_control_buffer_policy_bytes};
    for (const RequiredBytePolicy policy : required_byte_policies) {
        aoahid_device_options rejected = public_device_options();
        rejected.*policy = 0U;
        aoahid_device* rejected_device = nullptr;
        AOAHID_CHECK(aoahid_device_open(context, &selected, &rejected, &rejected_device) ==
                     AOAHID_ERR_UNSET_FIELD);
        AOAHID_CHECK(rejected_device == nullptr);
        AOAHID_CHECK(aoahid_fake_libusb_control_count() == 0U);
        AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 0U);
        AOAHID_CHECK(aoahid_last_error()->field != nullptr);
        if (aoahid_last_error()->field != nullptr) {
            AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "device_options");
        }
    }

    aoahid_device* device = nullptr;
    aoahid_device_options options = public_device_options();
    options.linux_hid_fields_per_report_policy = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid_usages_policy = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid_global_stack_depth_policy = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid_report_data_bits_policy = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    options = public_device_options();
    options.linux_hid_report_size_bits_policy = 0U;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) ==
                 AOAHID_ERR_UNSET_FIELD);
    AOAHID_CHECK(device == nullptr && aoahid_fake_libusb_control_count() == 0U);

    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_cancel_and_device_isolation() {
    aoahid_fake_libusb_reset();
    const auto first_candidate = add_candidate(2U, {3U, 1U});
    const auto second_candidate = add_candidate(4U, {3U, 2U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_CALLER_POLL, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    Device* first = nullptr;
    Device* second = nullptr;
    std::uint16_t protocol = 0U;
    AOAHID_CHECK(Device::open(runtime, first_candidate, config, &first, &protocol) == AOAHID_OK);
    AOAHID_CHECK(Device::open(runtime, second_candidate, config, &second, &protocol) == AOAHID_OK);

    CompletionState cancelled{};
    CompletionState unaffected{};
    PreparedTransfer first_transfer{};
    PreparedTransfer second_transfer{};
    AOAHID_CHECK(first->acquire(1U, {}, 1U, &cancelled, &completed, &first_transfer) == AOAHID_OK);
    AOAHID_CHECK(second->acquire(1U, {}, 1U, &unaffected, &completed, &second_transfer) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 50U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 0U);
    AOAHID_CHECK(first->submit(first_transfer, 1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(second->submit(second_transfer, 1U, 0U) == AOAHID_OK);
    AOAHID_CHECK(first->cancel_all() == AOAHID_OK);
    for (std::size_t poll = 0U; poll < 20U && (cancelled.calls == 0U || unaffected.calls == 0U);
         ++poll) {
        AOAHID_CHECK(runtime->poll(1U) == AOAHID_OK);
    }
    AOAHID_CHECK(cancelled.calls == 1U && cancelled.result == AOAHID_ERR_IO);
    AOAHID_CHECK(unaffected.calls == 1U && unaffected.result == AOAHID_OK);
    AOAHID_CHECK(first->drained());
    AOAHID_CHECK(second->drained());
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);

    delete first;
    delete second;
    delete runtime;
    aoahid_fake_libusb_reset();
}

void test_blocking_event_wait_is_woken_by_cancel() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(5U, {3U, 3U});
    Runtime* runtime = nullptr;
    AOAHID_CHECK(Runtime::create(AOAHID_EVENT_INTERNAL_THREAD, &runtime) == AOAHID_OK);
    DeviceConfig config = current_mode_config();
    config.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    Device* device = nullptr;
    std::uint16_t protocol = 0U;
    AOAHID_CHECK(Device::open(runtime, candidate, config, &device, &protocol) == AOAHID_OK);

    CompletionState completion{};
    PreparedTransfer prepared{};
    AOAHID_CHECK(device->acquire(1U, {}, 1U, &completion, &completed, &prepared) == AOAHID_OK);
    prepared.payload[0] = 1U;
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 1, 100'000U);
    AOAHID_CHECK(device->submit(prepared, 1U, 0U) == AOAHID_OK);

    // The generous timeout is only a deadlock escape, not a performance
    // threshold. The fake backend reports when poll() is actually blocked, so
    // cancellation can be issued at a deterministic synchronization point.
    std::atomic<aoahid_result> poll_result{AOAHID_ERR_INTERNAL};
    std::thread event_thread(
        [&] { poll_result.store(runtime->poll(5'000U), std::memory_order_release); });
    AOAHID_CHECK(aoahid_fake_libusb_wait_for_event_waiters(1U, 5'000U) == LIBUSB_SUCCESS);
    const aoahid_fake_libusb_event_stats waiting = event_stats();
    AOAHID_CHECK(waiting.active_waiters == 1U);
    AOAHID_CHECK(waiting.zero_timeout_calls == 0U);
    AOAHID_CHECK(waiting.requested_timeout_us == 5'000'000U);
    AOAHID_CHECK(waiting.callbacks_dispatched == 0U);

    AOAHID_CHECK(device->cancel_all() == AOAHID_OK);
    event_thread.join();
    const aoahid_fake_libusb_event_stats completed_stats = event_stats();
    AOAHID_CHECK(poll_result.load(std::memory_order_acquire) == AOAHID_OK);
    AOAHID_CHECK(completed_stats.notification_wakeups == waiting.notification_wakeups + 1U);
    AOAHID_CHECK(completed_stats.cancellation_wakeups == waiting.cancellation_wakeups + 1U);
    AOAHID_CHECK(completed_stats.callbacks_dispatched == 1U);
    AOAHID_CHECK(completion.calls == 1U && completion.result == AOAHID_ERR_IO);
    AOAHID_CHECK(device->drained());
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);

    delete device;
    delete runtime;
    aoahid_fake_libusb_reset();
}

aoahid_context_options public_context_options() {
    aoahid_context_options options{};
    options.struct_size = sizeof(options);
    options.event_mode = AOAHID_EVENT_CALLER_POLL;
    options.log_level = AOAHID_LOG_DISABLED;
    return options;
}

aoahid_device_options public_device_options() {
    aoahid_device_options options{};
    options.struct_size = sizeof(options);
    options.startup_mode = AOAHID_START_CURRENT_USB_MODE;
    options.accept_future_protocol_versions = 0U;
    options.control_timeout_ms = 20U;
    options.send_timeout_ms = 20U;
    options.descriptor_fragment_bytes = 64U;
    options.transfer_pool_slots = 1U;
    options.maximum_report_bytes = 64U;
    options.close_drain_timeout_ms = 1U;
    options.first_report_attempts = 2U;
    options.first_report_backoff_us = 1U;
    options.validate_reports = 0U;
    options.aoa_descriptor_wire_policy_bytes = 1024U;
    options.linux_descriptor_policy_bytes = 1024U;
    options.linux_hid_fields_per_report_policy = 256U;
    options.linux_hid_global_stack_depth_policy = 4U;
    options.linux_hid_usages_policy = 12288U;
    options.linux_hid_report_data_bits_policy = 65528U;
    options.linux_hid_report_size_bits_policy = 256U;
    options.target_ep0_data_policy_bytes = 4096U;
    options.host_control_buffer_policy_bytes = 4096U;
    options.interface_claim_policy = AOAHID_INTERFACE_CLAIM_NONE;
    options.interface_number = -1;
    return options;
}

void test_zero_tuning_fallbacks_and_zero_reservation() {
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
    aoahid_device_options options = public_device_options();
    options.control_timeout_ms = 0U;
    options.send_timeout_ms = 0U;
    options.descriptor_fragment_bytes = 0U;
    options.transfer_pool_slots = 0U;
    options.maximum_report_bytes = 0U;
    options.close_drain_timeout_ms = 0U;
    options.first_report_attempts = 0U;
    options.first_report_backoff_us = 0U;

    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
    AOAHID_CHECK(device != nullptr);
    if (device != nullptr) {
        AOAHID_CHECK(device->config.control_timeout_ms == 500U);
        AOAHID_CHECK(device->config.send_timeout_ms == 500U);
        AOAHID_CHECK(device->config.descriptor_fragment_bytes == 64U);
        AOAHID_CHECK(device->config.pool_slots == 8U);
        AOAHID_CHECK(device->config.maximum_report_bytes == 1024U);
        AOAHID_CHECK(device->close_drain_timeout_ms == 1000U);
        AOAHID_CHECK(device->config.first_report_attempts == 20U);
        AOAHID_CHECK(device->config.first_report_backoff_us == 1000U);
    }
    // Normalization is applied to a private copy, never to caller memory.
    AOAHID_CHECK(options.control_timeout_ms == 0U && options.maximum_report_bytes == 0U);
    const auto probes = controls_for(51U);
    AOAHID_CHECK(probes.size() == 1U && probes[0].timeout == 500U);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.rollover = AOAHID_KEYBOARD_ARRAY;
    keyboard.array_length = 6U;
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.usage_bit_width = 8U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    aoahid_node_options node_options{};
    node_options.struct_size = sizeof(node_options);
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, &node) == AOAHID_OK);
    AOAHID_CHECK(node != nullptr && node->reserved_slots == 0U);
    for (const std::uint8_t request : {std::uint8_t{54U}, std::uint8_t{56U}}) {
        const auto records = controls_for(request);
        AOAHID_CHECK(!records.empty());
        for (const auto& record : records)
            AOAHID_CHECK(record.timeout == 500U);
    }

    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    const auto reports = controls_for(57U);
    AOAHID_CHECK(!reports.empty() && reports.back().timeout == 500U);

    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_descriptor_requirements_are_caller_policies() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(13U, {5U, 3U});
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

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.rollover = AOAHID_KEYBOARD_ARRAY;
    keyboard.array_length = 6U;
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.usage_bit_width = 8U;
    aoahid_spec* keyboard_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &keyboard_spec) == AOAHID_OK);
    AOAHID_CHECK(keyboard_spec != nullptr);
    if (keyboard_spec != nullptr) {
        AOAHID_CHECK(keyboard_spec->requirements.maximum_fields_per_report == 2U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_report_size_bits == 8U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_usages == 102U);
        AOAHID_CHECK(keyboard_spec->requirements.maximum_report_data_bits == 64U);
    }

    std::vector<std::uint8_t> raw_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x15U, 0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U,
        0x01U,
        // First field declares 20001 Local Usages without expanding them.
        0x1AU, 0x00U, 0x00U, 0x2AU, 0x20U, 0x4EU, 0x81U, 0x02U};
    raw_descriptor.reserve(raw_descriptor.size() + (256U * 4U) + 1U);
    for (std::uint32_t field_index = 1U; field_index < 257U; ++field_index) {
        raw_descriptor.push_back(0x09U);
        raw_descriptor.push_back(0x01U);
        raw_descriptor.push_back(0x81U);
        raw_descriptor.push_back(0x02U);
    }
    raw_descriptor.push_back(0xC0U);
    const aoahid_raw_report raw_report{0U, 0U, 0U, 33U};
    const aoahid_raw_options raw_options{sizeof(aoahid_raw_options),
                                         0U,
                                         raw_descriptor.data(),
                                         raw_descriptor.size(),
                                         &raw_report,
                                         1U,
                                         1U,
                                         0U,
                                         0U};
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &raw_spec) == AOAHID_OK);
    AOAHID_CHECK(raw_spec != nullptr);
    if (raw_spec != nullptr) {
        AOAHID_CHECK(raw_spec->requirements.maximum_fields_per_report == 257U);
        AOAHID_CHECK(raw_spec->requirements.maximum_global_stack_depth == 0U);
        AOAHID_CHECK(raw_spec->requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(raw_spec->requirements.maximum_usages == 20001U);
        AOAHID_CHECK(raw_spec->requirements.maximum_report_data_bits == 257U);
    }

    const std::array<std::uint8_t, 29U> stack_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0xA4U, 0xA4U, 0xA4U, 0xA4U,
        0xA4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U, 0xB4U, 0x15U, 0x00U, 0x25U, 0x01U,
        0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report stack_report{0U, 0U, 0U, 1U};
    const aoahid_raw_options stack_options{sizeof(aoahid_raw_options),
                                           0U,
                                           stack_descriptor.data(),
                                           stack_descriptor.size(),
                                           &stack_report,
                                           1U,
                                           1U,
                                           0U,
                                           0U};
    aoahid_spec* stack_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&stack_options, &stack_spec) == AOAHID_OK);
    AOAHID_CHECK(stack_spec != nullptr);
    if (stack_spec != nullptr) {
        AOAHID_CHECK(stack_spec->requirements.maximum_global_stack_depth == 5U);
        AOAHID_CHECK(stack_spec->requirements.maximum_report_size_bits == 1U);
        AOAHID_CHECK(stack_spec->requirements.maximum_report_data_bits == 1U);
    }

    const std::array<std::uint8_t, 24U> report_size_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x77U, 0x01U, 0x01U, 0x00U, 0x00U, 0x15U,
        0x00U, 0x25U, 0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report report_size_report{0U, 0U, 0U, 1U};
    const aoahid_raw_options report_size_options{sizeof(aoahid_raw_options),
                                                 0U,
                                                 report_size_descriptor.data(),
                                                 report_size_descriptor.size(),
                                                 &report_size_report,
                                                 1U,
                                                 1U,
                                                 0U,
                                                 0U};
    aoahid_spec* report_size_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&report_size_options, &report_size_spec) == AOAHID_OK);
    AOAHID_CHECK(report_size_spec != nullptr);
    if (report_size_spec != nullptr) {
        AOAHID_CHECK(report_size_spec->requirements.maximum_report_size_bits == 257U);
        AOAHID_CHECK(report_size_spec->requirements.maximum_report_data_bits == 1U);
    }

    const std::array<std::uint8_t, 20U> large_report_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x15U, 0x00U, 0x25U, 0x01U,
        0x75U, 0x20U, 0x96U, 0x00U, 0x08U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report large_report{0U, 0U, 0U, 8192U};
    const aoahid_raw_options large_report_options{sizeof(aoahid_raw_options),
                                                  0U,
                                                  large_report_descriptor.data(),
                                                  large_report_descriptor.size(),
                                                  &large_report,
                                                  1U,
                                                  1U,
                                                  0U,
                                                  0U};
    aoahid_spec* large_report_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&large_report_options, &large_report_spec) == AOAHID_OK);
    AOAHID_CHECK(large_report_spec != nullptr);
    if (large_report_spec != nullptr) {
        AOAHID_CHECK(large_report_spec->requirements.maximum_report_size_bits == 32U);
        AOAHID_CHECK(large_report_spec->requirements.maximum_report_data_bits == 65536U);
    }

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    const auto reject_before_registration =
        [&](aoahid_spec* spec, const std::uint32_t fields_policy, const std::uint32_t stack_policy,
            const std::uint64_t usages_policy, const std::uint64_t report_bits_policy,
            const std::uint32_t report_size_policy, const char* expected_field) {
            aoahid_device_options options = public_device_options();
            options.aoa_descriptor_wire_policy_bytes = 4096U;
            options.linux_descriptor_policy_bytes = 4096U;
            options.linux_hid_fields_per_report_policy = fields_policy;
            options.linux_hid_global_stack_depth_policy = stack_policy;
            options.linux_hid_usages_policy = usages_policy;
            options.linux_hid_report_data_bits_policy = report_bits_policy;
            options.linux_hid_report_size_bits_policy = report_size_policy;
            options.maximum_report_bytes = 8192U;
            options.target_ep0_data_policy_bytes = 8192U;
            options.host_control_buffer_policy_bytes = 8192U;
            aoahid_device* device = nullptr;
            AOAHID_CHECK(aoahid_device_open(context, &selected, &options, &device) == AOAHID_OK);
            AOAHID_CHECK(device != nullptr);
            if (device == nullptr)
                return;
            AOAHID_CHECK(device->linux_hid_fields_per_report_policy == fields_policy);
            AOAHID_CHECK(device->linux_hid_global_stack_depth_policy == stack_policy);
            AOAHID_CHECK(device->linux_hid_usages_policy == usages_policy);
            AOAHID_CHECK(device->linux_hid_report_data_bits_policy == report_bits_policy);
            AOAHID_CHECK(device->linux_hid_report_size_bits_policy == report_size_policy);
            aoahid_node* node = nullptr;
            AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, &node) ==
                         AOAHID_ERR_OVERFLOW);
            AOAHID_CHECK(node == nullptr);
            AOAHID_CHECK(aoahid_last_error()->field != nullptr);
            if (aoahid_last_error()->field != nullptr) {
                AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == expected_field);
            }
            AOAHID_CHECK(controls_for(54U).empty());
            AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
        };

    if (keyboard_spec != nullptr) {
        reject_before_registration(keyboard_spec, 1U, 4U, 1000U, 65528U, 256U,
                                   "spec.requirements.maximum_fields_per_report");
        reject_before_registration(keyboard_spec, 10U, 4U, 101U, 65528U, 256U,
                                   "spec.requirements.maximum_usages");
    }
    if (raw_spec != nullptr) {
        reject_before_registration(raw_spec, 256U, 4U, 50000U, 65528U, 256U,
                                   "spec.requirements.maximum_fields_per_report");
        reject_before_registration(raw_spec, 1000U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_usages");
    }

    if (stack_spec != nullptr) {
        reject_before_registration(stack_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_global_stack_depth");
    }
    if (report_size_spec != nullptr) {
        reject_before_registration(report_size_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_report_size_bits");
    }
    if (large_report_spec != nullptr) {
        reject_before_registration(large_report_spec, 256U, 4U, 12288U, 65528U, 256U,
                                   "spec.requirements.maximum_report_data_bits");
    }

    aoahid_spec_release(large_report_spec);
    aoahid_spec_release(report_size_spec);
    aoahid_spec_release(stack_spec);
    aoahid_spec_release(raw_spec);
    aoahid_spec_release(keyboard_spec);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

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

    aoahid_touch_options touch{};
    touch.struct_size = sizeof(touch);
    touch.maximum_contacts = 2U;
    touch.contacts_per_report = 2U;
    touch.contact_identifier = {0, 15, 4U, {}};
    touch.x = {0, 1000, 16U, {}};
    touch.y = {0, 1000, 16U, {}};
    touch.contact_count = {0, 2, 2U, {}};
    touch.enable_scan_time = 1U;
    touch.scan_time = {0, 65535, 16U, {}};
    touch.scan_time_unit_100us = 1U;
    touch.enable_multi_packet_frames = 0U;
    touch.touchpad_button_count = 1U;

    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touch, &spec) == AOAHID_OK);
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

void test_public_controller_close_neutralizes_dpad() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(14U, {5U, 4U});

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

    const std::array<aoahid_gamepad_axis, 2U> axes{
        aoahid_gamepad_axis{AOAHID_AXIS_X, 0x01U, 0x30U, {-127, 127, 8U, {}}, 0, "ABS_X", "AXIS_X"},
        aoahid_gamepad_axis{
            AOAHID_AXIS_Y, 0x01U, 0x31U, {-127, 127, 8U, {}}, 0, "ABS_Y", "AXIS_Y"}};
    aoahid_gamepad_options controller{};
    controller.struct_size = sizeof(controller);
    controller.axes = axes.data();
    controller.axis_count = axes.size();
    controller.button_count = 5U;
    controller.button_usage_minimum = 1U;
    controller.application = AOAHID_CONTROLLER_GAMEPAD;

    controller.dpad_representation = AOAHID_DPAD_BUTTONS;
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&controller, &raw_spec) == AOAHID_OK);
    const std::array<const aoa::hid::FieldLayout*, 4U> raw_dpad{
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 5U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 6U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 7U),
        profile_field(raw_spec, aoa::hid::FieldSemantic::buttons, 8U)};
    AOAHID_CHECK(std::all_of(raw_dpad.begin(), raw_dpad.end(),
                             [](const auto* field) { return field != nullptr; }));
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* raw_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, raw_spec, &node_options, &raw_node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_dpad(raw_node, 1U, 0U, 0U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(raw_node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(raw_node) == AOAHID_OK);
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && std::all_of(raw_dpad.begin(), raw_dpad.end(),
                                             [](const auto* field) { return field != nullptr; })) {
        AOAHID_CHECK(extract_report_value(payloads[0], *raw_dpad[0]) == 1U);
        for (const auto* field : raw_dpad) {
            AOAHID_CHECK(extract_report_value(payloads[1], *field) == 0U);
        }
    }
    aoahid_spec_release(raw_spec);

    // The shared neutralization also clears the stored encoded value, but a
    // canonical Hat must continue to serialize its declared Null value.
    controller.dpad_representation = AOAHID_DPAD_HAT;
    controller.hat_logical_minimum = 0;
    controller.hat_logical_maximum = 7;
    controller.hat_bit_width = 4U;
    aoahid_spec* hat_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_gamepad(&controller, &hat_spec) == AOAHID_OK);
    const auto* hat = profile_field(hat_spec, aoa::hid::FieldSemantic::hat);
    AOAHID_CHECK(hat != nullptr);
    aoahid_node* hat_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, hat_spec, &node_options, &hat_node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_dpad(hat_node, 1U, 0U, 0U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(hat_node, 100U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(hat_node) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 4U);
    if (payloads.size() == 4U && hat != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads[2], *hat) == 0U);
        AOAHID_CHECK(extract_report_value(payloads[3], *hat) == 15U);
    }
    aoahid_spec_release(hat_spec);

    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void open_public_raw_session(aoahid_context** context, aoahid_device** device, aoahid_node** node,
                             const std::uint32_t close_drain_timeout_ms = 1U) {
    static constexpr std::array<std::uint8_t, 2U> path{5U, 1U};
    const aoahid_fake_libusb_device_config fake{
        2U,      7U, path.data(),       path.size(), 0x18D1U,
        0x2D00U, 2U, "graveyard-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);

    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,       fake.address,    path.data(), path.size(),
                                      fake.vendor_id, fake.product_id, fake.serial, fake.product};
    aoahid_device_options device_options = public_device_options();
    device_options.close_drain_timeout_ms = close_drain_timeout_ms;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_options, device) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 21U> descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
        0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report report{1U, 1U, 0U, 2U};
    const aoahid_raw_options raw_options{sizeof(aoahid_raw_options),
                                         0U,
                                         descriptor.data(),
                                         descriptor.size(),
                                         &report,
                                         1U,
                                         1U,
                                         0U,
                                         0U};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, node) == AOAHID_OK);
    // The node's retained reference must keep the spec alive through a late
    // callback even after the caller drops its own reference.
    aoahid_spec_release(spec);
}

void open_public_keyboard_session(aoahid_context** context, aoahid_device** device,
                                  aoahid_node** node, const std::uint32_t close_timeout_ms = 100U) {
    static constexpr std::array<std::uint8_t, 2U> path{6U, 1U};
    const aoahid_fake_libusb_device_config fake{
        3U,      9U, path.data(),      path.size(), 0x18D1U,
        0x2D00U, 2U, "keyboard-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);
    aoahid_context_options context_config = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_config, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,       fake.address,    path.data(), path.size(),
                                      fake.vendor_id, fake.product_id, fake.serial, fake.product};
    aoahid_device_options device_config = public_device_options();
    device_config.close_drain_timeout_ms = close_timeout_ms;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_config, device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.rollover = AOAHID_KEYBOARD_ARRAY;
    keyboard.array_length = 6U;
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.usage_bit_width = 8U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, node) == AOAHID_OK);
    aoahid_spec_release(spec);
}

void test_first_report_retry_is_consumed_by_accepted_terminal_failure() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);

    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_OK);
    for (std::size_t attempt = 0U;
         attempt < 10U && node->transfer_inflight.load(std::memory_order_acquire); ++attempt) {
        AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_OK);
    }
    AOAHID_CHECK(!node->transfer_inflight.load(std::memory_order_acquire));
    AOAHID_CHECK(controls_for(57U).size() == 2U);

    // Consume the first event's exhausted STALL after recording another
    // explicit state transition. That transition remains dirty for the next
    // submit, but it is no longer registration-race eligible.
    AOAHID_CHECK(aoahid_kbd(node, 0x05U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_ERR_STALL);
    const std::size_t before_later_event = controls_for(57U).size();
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_STALL, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_OK);
    for (std::size_t attempt = 0U;
         attempt < 10U && node->transfer_inflight.load(std::memory_order_acquire); ++attempt) {
        AOAHID_CHECK(aoahid_context_poll(context, 1U) == AOAHID_OK);
    }
    AOAHID_CHECK(!node->transfer_inflight.load(std::memory_order_acquire));
    AOAHID_CHECK(controls_for(57U).size() == before_later_event + 1U);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_ERR_STALL);

    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_error_contract_and_raw_padding() {
    auto* stale_discovery = reinterpret_cast<aoahid_discovery*>(static_cast<std::uintptr_t>(1U));
    AOAHID_CHECK(aoahid_discover(nullptr, 0U, &stale_discovery) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(stale_discovery == nullptr);
    auto* stale_device = reinterpret_cast<aoahid_device*>(static_cast<std::uintptr_t>(1U));
    AOAHID_CHECK(aoahid_device_open(nullptr, nullptr, nullptr, &stale_device) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(stale_device == nullptr);
    AOAHID_CHECK(aoahid_context_poll(nullptr, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr && aoahid_last_error()->reason != nullptr);
    static_cast<void>(aoahid_version());
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->field == nullptr);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == 0);
    AOAHID_CHECK(aoahid_result_name(999) != nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_result_name(AOAHID_OK) != nullptr);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);

    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);

    const std::array<std::uint8_t, 2U> bad_padding{1U, 0x80U};
    AOAHID_CHECK(aoahid_raw_submit(node, bad_padding.data(), bad_padding.size()) ==
                 AOAHID_ERR_PARAM);
    const aoahid_error_detail* padding_error = aoahid_last_error();
    AOAHID_CHECK(padding_error->field != nullptr && padding_error->reason != nullptr);
    AOAHID_CHECK(padding_error->aoa_request == 57);
    AOAHID_CHECK(padding_error->report_id == 1U);
    AOAHID_CHECK(padding_error->offset == 1U && padding_error->length == 1U);
    AOAHID_CHECK(controls_for(57U).empty());

    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_ERR_TIMEOUT);
    const aoahid_error_detail* completion_error = aoahid_last_error();
    AOAHID_CHECK(completion_error->code == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(completion_error->libusb_status == LIBUSB_TRANSFER_TIMED_OUT);
    AOAHID_CHECK(completion_error->aoa_request == 57);
    AOAHID_CHECK(completion_error->hid_id != 0U && completion_error->report_id == 1U);
    AOAHID_CHECK(completion_error->offset == 0U && completion_error->length == 2U);
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_TIMED_OUT);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->report_id == 1U);

    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_OVERFLOW, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_ERR_OVERFLOW);
    const aoahid_error_detail* overflow_error = aoahid_last_error();
    AOAHID_CHECK(overflow_error->code == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(overflow_error->libusb_status == LIBUSB_TRANSFER_OVERFLOW);
    AOAHID_CHECK(overflow_error->aoa_request == 57 && overflow_error->report_id == 1U);
    AOAHID_CHECK(overflow_error->length == report.size());
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_OVERFLOW);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_OVERFLOW);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_submit_rejection_retains_state_and_close_releases() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);

    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(controls_for(57U).empty());
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 1U);
    if (payloads.size() == 1U) {
        AOAHID_CHECK(std::find(payloads[0].begin(), payloads[0].end(), std::uint8_t{0x04U}) !=
                     payloads[0].end());
    }

    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U) {
        AOAHID_CHECK(std::all_of(payloads[1].begin(), payloads[1].end(),
                                 [](const std::uint8_t value) { return value == 0U; }));
    }
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_open_host_policy_failure_precedes_request54() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* first_node = nullptr;
    open_public_keyboard_session(&context, &device, &first_node);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.rollover = AOAHID_KEYBOARD_ARRAY;
    keyboard.array_length = 6U;
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    keyboard.usage_bit_width = 8U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const std::size_t registrations_before = controls_for(54U).size();
    const aoahid_node_options reserved_node{sizeof(aoahid_node_options), 0U, 1U, 1U};
    aoahid_node* rejected = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &reserved_node, &rejected) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(rejected == nullptr);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "node.reserved_slots");
    }
    AOAHID_CHECK(controls_for(54U).size() == registrations_before);

    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_node_close(first_node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_node_close_timeout_distinction_and_loss_freeze() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node, 100U);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_TIMED_OUT, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_TIMED_OUT);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);

    aoahid_fake_libusb_reset();
    open_public_keyboard_session(&context, &device, &node);
    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 0U) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_device_latched_error(device) == AOAHID_ERR_NO_DEVICE);
    // The transport latch is independent of the Node's terminal completion.
    // Device teardown must still publish that callback once while consuming
    // the graph.
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void submit_delayed_public_report(aoahid_node* node);

void test_node_close_retains_terminal_link_loss() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t hid_id = aoahid_node_hid_id(node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_NO_DEVICE, 0, 0U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);

    // Link loss makes present()==false, but it must not bypass the already
    // published terminal completion when the standalone Node remains owned by
    // the caller.
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_TRANSFER_NO_DEVICE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->hid_id == hid_id);
    AOAHID_CHECK(aoahid_last_error()->report_id == 1U);
    AOAHID_CHECK(aoahid_last_error()->length == report.size());
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_caller_poll_hot_path_is_lock_and_allocation_free() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);
    AOAHID_CHECK(context->active_state_mutex == nullptr);
    AOAHID_CHECK(node->active_completion_mutex == nullptr);

    // Prewarm the fake event dispatcher's reusable callback array, then stop
    // recording request 57 payloads so only library allocations are measured.
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    aoahid_fake_libusb_set_control_recording(0);
    const aoahid_fake_libusb_event_stats before = event_stats();
    allocation_probe::begin();
    const aoahid_result key_down = aoahid_kbd(node, 0x04U, 1U);
    const aoahid_result first_submit = aoahid_node_submit(node);
    const bool first_completed = !node->transfer_inflight.load(std::memory_order_acquire);
    const aoahid_result key_up = aoahid_kbd(node, 0x04U, 0U);
    const aoahid_result second_submit = aoahid_node_submit(node);
    const bool second_completed = !node->transfer_inflight.load(std::memory_order_acquire);
    const std::size_t hot_allocations = allocation_probe::finish();
    aoahid_fake_libusb_set_control_recording(1);

    AOAHID_CHECK(key_down == AOAHID_OK);
    AOAHID_CHECK(first_submit == AOAHID_OK);
    AOAHID_CHECK(first_completed);
    AOAHID_CHECK(key_up == AOAHID_OK);
    AOAHID_CHECK(second_submit == AOAHID_OK);
    AOAHID_CHECK(second_completed);
    const aoahid_fake_libusb_event_stats after = event_stats();
    // Each accepted report performs exactly one async submit followed by one
    // nonblocking event pass and one completion callback. Together with the
    // completed flags sampled before the next state change, these counts prove
    // there is no extra pre-submit event reap in the measured hot path.
    AOAHID_CHECK(after.successful_submits == before.successful_submits + 2U);
    AOAHID_CHECK(after.handle_calls == before.handle_calls + 2U);
    AOAHID_CHECK(after.zero_timeout_calls == before.zero_timeout_calls + 2U);
    AOAHID_CHECK(after.callbacks_dispatched == before.callbacks_dispatched + 2U);
    AOAHID_CHECK(after.waits_started == before.waits_started);
    AOAHID_CHECK(hot_allocations == 0U);

    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_accepted_submit_defers_opportunistic_poll_error() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_keyboard_session(&context, &device, &node);
    // These fixed null selectors are the structural fast-path contract: the
    // submit preflight/error latch and synchronous completion callback use the
    // caller's synchronization domain without acquiring std::mutex.
    AOAHID_CHECK(context->active_state_mutex == nullptr);
    AOAHID_CHECK(node->active_completion_mutex == nullptr);
    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 1U) == AOAHID_OK);
    aoahid_fake_libusb_queue_event_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_OK);

    // The report was accepted, so the submit remains successful. The exact
    // event-handler failure is delivered on the next Context-using call.
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_ERR_IO);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "context.event_pump");
    }
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_kbd(node, 0x04U, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_context_destroy_aggregates_unregister_error() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t hid_id = aoahid_node_hid_id(node);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);

    // This terminal error consumes Context: teardown completed and the exact
    // request-55 failure is returned instead of being discarded.
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 55);
    AOAHID_CHECK(aoahid_last_error()->hid_id == hid_id);
    AOAHID_CHECK(controls_for(55U).size() == 2U);
    aoahid_fake_libusb_reset();
}

void test_context_destroy_never_returns_terminal_pending() {
    aoahid_fake_libusb_reset();
    aoahid_context_options options = public_context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    aoa::detail::DeferredError invalid_terminal{};
    invalid_terminal.transport = aoa::transport::ErrorInfo{AOAHID_CLOSE_PENDING, 0, 0, 0U, 0U, 0U};
    invalid_terminal.field = "context.destroy";
    invalid_terminal.reason = "Injected invalid terminal pending state.";
    invalid_terminal.pending = true;
    {
        const aoa::detail::ModeMutexGuard guard(context->active_state_mutex);
        context->deferred_error = invalid_terminal;
    }
    // Context is consumed, but the consuming result can never be the one value
    // that authorizes the blocking wrapper to inspect it again.
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_ERR_INTERNAL);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_INTERNAL);
    AOAHID_CHECK(aoahid_last_error()->field != nullptr);
    if (aoahid_last_error()->field != nullptr) {
        AOAHID_CHECK(std::string_view(aoahid_last_error()->field) == "context.destroy");
    }
    aoahid_fake_libusb_reset();
}

void test_graveyard_publishes_unregister_and_cancel_errors() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t unregister_hid_id = aoahid_node_hid_id(node);
    submit_delayed_public_report(node);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_CLOSE_PENDING);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_STALL);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 55);
    AOAHID_CHECK(aoahid_last_error()->hid_id == unregister_hid_id);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);

    aoahid_fake_libusb_reset();
    open_public_raw_session(&context, &device, &node);
    const std::uint16_t cancel_hid_id = aoahid_node_hid_id(node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(report.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    aoahid_fake_libusb_queue_cancel_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->hid_id == cancel_hid_id);
    AOAHID_CHECK(aoahid_last_error()->length == report.size());
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 57);
    AOAHID_CHECK(aoahid_last_error()->hid_id == cancel_hid_id);
    AOAHID_CHECK(aoahid_last_error()->length == report.size());
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void submit_delayed_public_report(aoahid_node* node) {
    static constexpr std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, 2, 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
}

void open_public_touch_session(aoahid_context** context, aoahid_device** device, aoahid_node** node,
                               const bool enable_scan_time = false) {
    static constexpr std::array<std::uint8_t, 2U> path{5U, 2U};
    const aoahid_fake_libusb_device_config fake{
        2U,      8U, path.data(),         path.size(), 0x18D1U,
        0x2D00U, 2U, "touch-close-phone", "Android",   LIBUSB_SUCCESS};
    AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);

    aoahid_context_options context_options = public_context_options();
    AOAHID_CHECK(aoahid_context_create(&context_options, context) == AOAHID_OK);
    const aoahid_device_info selected{fake.bus,       fake.address,    path.data(), path.size(),
                                      fake.vendor_id, fake.product_id, fake.serial, fake.product};
    aoahid_device_options device_options = public_device_options();
    // A short drain timeout is fine on a lightly loaded CI runner, but the
    // fake backend's async completion still crosses a real OS thread wakeup,
    // and some Windows runners (particularly ARM64) have shown enough
    // scheduling jitter to miss a 50 ms budget. Draining still returns as
    // soon as the pending frame completes, so a larger ceiling only adds
    // margin; it does not slow down the success path.
    device_options.close_drain_timeout_ms = 2000U;
    AOAHID_CHECK(aoahid_device_open(*context, &selected, &device_options, device) == AOAHID_OK);

    aoahid_touch_options touch{};
    touch.struct_size = sizeof(touch);
    touch.maximum_contacts = 2U;
    touch.contacts_per_report = 1U;
    touch.contact_identifier = {0, 15, 4U, {}};
    touch.x = {0, 1000, 16U, {}};
    touch.y = {0, 1000, 16U, {}};
    touch.contact_count = {0, 2, 2U, {}};
    if (enable_scan_time) {
        touch.enable_scan_time = 1U;
        touch.scan_time = {0, 65535, 16U, {}};
        touch.scan_time_unit_100us = 1U;
    }
    touch.enable_multi_packet_frames = 1U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touch, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    AOAHID_CHECK(aoahid_node_open(*device, spec, &node_options, node) == AOAHID_OK);
    aoahid_spec_release(spec);

    const aoahid_touch_contact first{1U, 100, 200, 0, 0, 0, 0};
    const aoahid_touch_contact second{2U, 300, 400, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touch(*node, first.contact_id, 1U, first.x, first.y, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touch(*node, second.contact_id, 1U, second.x, second.y, nullptr) ==
                 AOAHID_OK);
}

void test_public_touch_scan_time_lifecycle() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_touch_session(&context, &device, &node, true);

    const auto* scan_time = profile_field(node->spec, aoa::hid::FieldSemantic::scan_time);
    AOAHID_CHECK(scan_time != nullptr);
    auto* touch = std::get_if<aoa::detail::TouchState>(&node->state);
    AOAHID_CHECK(touch != nullptr);
    if (scan_time == nullptr || touch == nullptr) {
        AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
        AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
        aoahid_fake_libusb_reset();
        return;
    }

    // Put the epoch one full 16-bit cycle plus 1000 ticks in the past. The
    // accepted value must therefore prove both nonzero progression and modulo
    // wrap without sleeping or assuming an OS/libusb latency.
    constexpr std::uint64_t kModulus = 65'536U;
    constexpr std::uint64_t kElapsedTicks = kModulus + 1'000U;
    constexpr auto kTick = std::chrono::microseconds{100};
    const auto epoch = std::chrono::steady_clock::now() - kTick * kElapsedTicks;
    touch->scan_epoch = epoch;
    touch->scan_epoch_active = true;

    const auto before_submit = std::chrono::steady_clock::now();
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    const auto after_submit = std::chrono::steady_clock::now();
    auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U) {
        const std::uint64_t first = extract_report_value(payloads[0], *scan_time);
        const std::uint64_t continuation = extract_report_value(payloads[1], *scan_time);
        AOAHID_CHECK(first == continuation);
        AOAHID_CHECK(first != 0U);

        const auto ticks_since_epoch = [epoch](const auto point) {
            return static_cast<std::uint64_t>(
                       std::chrono::duration_cast<std::chrono::microseconds>(point - epoch)
                           .count()) /
                   100U;
        };
        const std::uint64_t first_possible_tick = ticks_since_epoch(before_submit);
        const std::uint64_t last_possible_tick = ticks_since_epoch(after_submit);
        AOAHID_CHECK(first_possible_tick >= kModulus);
        const auto in_modulo_window = [](const std::uint64_t value, const std::uint64_t first_tick,
                                         const std::uint64_t last_tick) {
            if (last_tick - first_tick >= kModulus)
                return true;
            const std::uint64_t first_modulo = first_tick % kModulus;
            const std::uint64_t last_modulo = last_tick % kModulus;
            return first_modulo <= last_modulo ? value >= first_modulo && value <= last_modulo
                                               : value >= first_modulo || value <= last_modulo;
        };
        AOAHID_CHECK(in_modulo_window(first, first_possible_tick, last_possible_tick));
    }

    AOAHID_CHECK(aoahid_touch(node, 1U, 0U, 100, 200, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_touch(node, 2U, 0U, 300, 400, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    AOAHID_CHECK(!touch->scan_epoch_active);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 4U);

    const aoahid_touch_contact next{3U, 500, 600, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touch(node, next.contact_id, 1U, next.x, next.y, nullptr) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 100U) == AOAHID_OK);
    payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 5U);
    if (payloads.size() == 5U) {
        AOAHID_CHECK(extract_report_value(payloads.back(), *scan_time) == 0U);
    }

    AOAHID_CHECK(aoahid_node_close(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_touch_scan_time_rejected_first_attempt_stays_zero() {
    aoahid_fake_libusb_reset();
    const Candidate candidate = add_candidate(15U, {5U, 5U});

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
    aoahid_device_options device_options = public_device_options();
    device_options.transfer_pool_slots = 1U;
    device_options.close_drain_timeout_ms = 100U;
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    static constexpr std::array<std::uint8_t, 21U> raw_descriptor{
        0x05U, 0x01U, 0x09U, 0x06U, 0xA1U, 0x01U, 0x85U, 0x01U, 0x15U, 0x00U, 0x25U,
        0x01U, 0x75U, 0x01U, 0x95U, 0x01U, 0x09U, 0x01U, 0x81U, 0x02U, 0xC0U};
    const aoahid_raw_report raw_report{1U, 1U, 0U, 2U};
    aoahid_raw_options raw_options{};
    raw_options.struct_size = sizeof(raw_options);
    raw_options.descriptor = raw_descriptor.data();
    raw_options.descriptor_length = raw_descriptor.size();
    raw_options.reports = &raw_report;
    raw_options.report_count = 1U;
    raw_options.acknowledges_no_android_support = 1U;
    aoahid_spec* raw_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_raw(&raw_options, &raw_spec) == AOAHID_OK);

    aoahid_touch_options touch_options{};
    touch_options.struct_size = sizeof(touch_options);
    touch_options.maximum_contacts = 1U;
    touch_options.contacts_per_report = 1U;
    touch_options.contact_identifier = {0, 15, 4U, {}};
    touch_options.x = {0, 1000, 16U, {}};
    touch_options.y = {0, 1000, 16U, {}};
    touch_options.contact_count = {0, 1, 1U, {}};
    touch_options.enable_scan_time = 1U;
    touch_options.scan_time = {0, 65535, 16U, {}};
    touch_options.scan_time_unit_100us = 1U;
    aoahid_spec* touch_spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_touchscreen(&touch_options, &touch_spec) == AOAHID_OK);
    const auto* scan_time = profile_field(touch_spec, aoa::hid::FieldSemantic::scan_time);
    AOAHID_CHECK(scan_time != nullptr);

    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 1U, 0U};
    aoahid_node* raw_node = nullptr;
    aoahid_node* touch_node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, raw_spec, &node_options, &raw_node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_open(device, touch_spec, &node_options, &touch_node) == AOAHID_OK);
    const aoahid_touch_contact contact{1U, 100, 200, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touch(touch_node, contact.contact_id, 1U, contact.x, contact.y, nullptr) ==
                 AOAHID_OK);
    auto* touch = std::get_if<aoa::detail::TouchState>(&touch_node->state);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    static constexpr std::array<std::uint8_t, 2U> raw_payload{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(raw_payload.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(raw_node, raw_payload.data(), raw_payload.size()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
    AOAHID_CHECK(aoahid_node_submit(touch_node) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);

    // A libusb submission rejected synchronously has the same unaccepted-frame
    // semantics as an acquire failure and must roll the provisional epoch back.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_node_submit(touch_node) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(touch != nullptr && !touch->scan_epoch_active && touch->scan_time == 0U);

    AOAHID_CHECK(aoahid_node_submit_blocking(touch_node, 100U) == AOAHID_OK);
    const auto payloads = control_payloads_for(57U);
    AOAHID_CHECK(payloads.size() == 2U);
    if (payloads.size() == 2U && scan_time != nullptr) {
        AOAHID_CHECK(extract_report_value(payloads.back(), *scan_time) == 0U);
    }

    AOAHID_CHECK(aoahid_node_close(touch_node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(raw_node) == AOAHID_OK);
    aoahid_spec_release(touch_spec);
    aoahid_spec_release(raw_spec);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_close_drains_touch_frame_before_neutral() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_touch_session(&context, &device, &node);

    // The submit's one nonblocking poll completes packet one, leaving a
    // nonzero packet cursor while no transfer is active.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 0U);
    AOAHID_CHECK(aoahid_node_submit(node) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    const aoahid_touch_contact blocked_change{1U, 101, 201, 0, 0, 0, 0};
    AOAHID_CHECK(aoahid_touch(node, blocked_change.contact_id, 1U, blocked_change.x,
                              blocked_change.y, nullptr) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(controls_for(57U).size() == 1U);

    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_OK);
    // Initial frame: two packets. Neutral lift frame: two packets. Closing may
    // not splice the neutral state into the initial frame's second packet.
    AOAHID_CHECK(controls_for(57U).size() == 4U);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_graveyard_delayed_cancel() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    submit_delayed_public_report(node);

    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_fake_libusb_cancel_count() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 1U);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_CLOSE_PENDING);

    // poll executes CANCELLED, returns from the callback, and only then reaps
    // the complete device/node/spec/transport graph from the graveyard.
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_public_graveyard_blocking_destroy() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    submit_delayed_public_report(node);

    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_context_destroy_blocking(context, 50U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
    aoahid_fake_libusb_reset();
}

void test_blocking_destroy_timeout_retains_context() {
    aoahid_fake_libusb_reset();
    aoahid_context* context = nullptr;
    aoahid_device* device = nullptr;
    aoahid_node* node = nullptr;
    open_public_raw_session(&context, &device, &node);
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED,
                                              static_cast<int>(report.size()), 1'000U);
    AOAHID_CHECK(aoahid_raw_submit(node, report.data(), report.size()) == AOAHID_OK);
    aoahid_fake_libusb_queue_cancel_result(LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_device_close(device) == AOAHID_CLOSE_PENDING);

    AOAHID_CHECK(aoahid_context_destroy_blocking(context, 2U) == AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_last_error()->code == AOAHID_ERR_TIMEOUT);
    // TIMEOUT retains Context. Make the original accepted transfer terminal,
    // publish the stored cancel failure, and then finish destruction.
    aoahid_fake_libusb_make_pending_transfers_ready();
    AOAHID_CHECK(aoahid_context_poll(context, 0U) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(aoahid_last_error()->libusb_status == LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_context_destroy(context) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace

void test_transport() {
    test_runtime_initialization_options();
    test_registration_reservations_and_async_results();
    test_uncertain_register_attempts_unregister_and_preserves_error();
    test_descriptor_failure_unregisters_and_preserves_error();
    test_protocol_short_response_preserves_not_aoa_diagnostic();
    test_error_report_id_is_published_before_user_callback_returns();
    test_current_mode_open_and_explicit_claim();
    test_legacy_accessory_options_are_unsupported_before_io();
    test_required_device_policies_precede_usb_io();
    test_zero_tuning_fallbacks_and_zero_reservation();
    test_cancel_and_device_isolation();
    test_blocking_event_wait_is_woken_by_cancel();
    test_public_touchpad_button_only_close();
    test_public_controller_close_neutralizes_dpad();
    test_descriptor_requirements_are_caller_policies();
    test_public_touch_scan_time_lifecycle();
    test_public_touch_scan_time_rejected_first_attempt_stays_zero();
    test_close_drains_touch_frame_before_neutral();
    test_public_error_contract_and_raw_padding();
    test_first_report_retry_is_consumed_by_accepted_terminal_failure();
    test_submit_rejection_retains_state_and_close_releases();
    test_node_open_host_policy_failure_precedes_request54();
    test_node_close_timeout_distinction_and_loss_freeze();
    test_node_close_retains_terminal_link_loss();
    test_caller_poll_hot_path_is_lock_and_allocation_free();
    test_accepted_submit_defers_opportunistic_poll_error();
    test_context_destroy_aggregates_unregister_error();
    test_context_destroy_never_returns_terminal_pending();
    test_graveyard_publishes_unregister_and_cancel_errors();
    test_public_graveyard_delayed_cancel();
    test_public_graveyard_blocking_destroy();
    test_blocking_destroy_timeout_retains_context();
}
