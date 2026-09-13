// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Deterministic fake implementation of the small libusb surface used by L0.
 * Every control transfer is recorded, and tests explicitly queue synchronous,
 * submit-time, and asynchronous outcomes.
 */

#include "libusb.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

struct libusb_context {
    std::uint64_t token{};
    bool user_interrupt_pending{};
};

struct libusb_device {
    std::size_t index{};
    std::uint8_t bus{};
    std::uint8_t address{};
    std::vector<std::uint8_t> ports;
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::uint16_t protocol_version{};
    std::string serial;
    std::string product;
    int open_status{LIBUSB_SUCCESS};
    int claim_status{LIBUSB_SUCCESS};
    bool present{true};
};

struct libusb_device_handle {
    libusb_device* device{};
};

namespace {

struct RecordedControl {
    aoahid_fake_libusb_control_record record{};
    std::vector<std::uint8_t> data;
};

struct AsyncOutcome {
    libusb_transfer_status status{LIBUSB_TRANSFER_COMPLETED};
    int actual_length{-1};
    std::uint32_t delay_polls{};
};

std::mutex global_mutex;
std::condition_variable event_cv;
std::vector<std::unique_ptr<libusb_device>> devices;
std::deque<int> control_results;
std::deque<int> submit_results;
std::deque<int> cancel_results;
std::deque<int> event_results;
std::deque<AsyncOutcome> async_outcomes;
std::vector<RecordedControl> controls;
std::vector<aoahid_fake_libusb_claim_record> claims;
std::vector<aoahid_fake_libusb_init_option_record> init_options;
std::vector<libusb_transfer*> allocated_transfers;
std::uint64_t poll_number{};
std::size_t cancel_count{};
std::size_t event_handle_count{};
aoahid_fake_libusb_event_stats event_stats{};
std::uint64_t event_notification_generation{};
enum class EventNotification : std::uint8_t { none, submit, cancel, ready, backend_result };
EventNotification last_event_notification{EventNotification::none};
std::size_t init_context_count{};
std::uint64_t context_token{};
bool control_recording_enabled{true};

libusb_device* find_device(const std::size_t index) noexcept {
    return index < devices.size() ? devices[index].get() : nullptr;
}

void record_control_locked(libusb_device* device, const std::uint8_t request_type,
                           const std::uint8_t request, const std::uint16_t value,
                           const std::uint16_t index, const std::uint16_t length,
                           const unsigned int timeout, const bool asynchronous,
                           const unsigned char* data) {
    // Allocation-probe tests disable recording only after registration. This
    // keeps fake-backend bookkeeping allocations out of the library hot-path
    // measurement without changing transfer behavior.
    if (!control_recording_enabled) {
        return;
    }
    RecordedControl recorded{};
    recorded.record.device_index = device->index;
    recorded.record.request_type = request_type;
    recorded.record.request = request;
    recorded.record.value = value;
    recorded.record.index = index;
    recorded.record.length = length;
    recorded.record.timeout = timeout;
    recorded.record.asynchronous = asynchronous ? 1 : 0;
    if (data != nullptr && length > 0U) {
        recorded.data.assign(data, data + length);
    }
    controls.push_back(std::move(recorded));
}

std::chrono::microseconds timeout_duration(const timeval* timeout) noexcept {
    if (timeout == nullptr || timeout->tv_sec < 0 || timeout->tv_usec < 0) {
        return std::chrono::microseconds::zero();
    }
    const auto seconds = static_cast<std::uint64_t>(timeout->tv_sec);
    const auto micros = static_cast<std::uint64_t>(timeout->tv_usec);
    const std::uint64_t total = seconds * 1'000'000U + micros;
    const auto bounded = std::min<std::uint64_t>(
        total, static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()));
    return std::chrono::microseconds(static_cast<std::int64_t>(bounded));
}

void notify_event_locked(const EventNotification notification) noexcept {
    last_event_notification = notification;
    ++event_notification_generation;
    event_cv.notify_all();
}

bool completion_ready_next_poll_locked() noexcept {
    for (const libusb_transfer* transfer : allocated_transfers) {
        if (transfer != nullptr && transfer->fake_submitted != 0 &&
            transfer->fake_ready_poll <= poll_number + 1U) {
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" {

int libusb_init_context(libusb_context** context, const libusb_init_option options[],
                        const int num_options) {
    if (context == nullptr || num_options < 0 || (num_options > 0 && options == nullptr)) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    auto* value = new (std::nothrow) libusb_context();
    if (value == nullptr) {
        return LIBUSB_ERROR_NO_MEM;
    }
    {
        const std::lock_guard<std::mutex> guard(global_mutex);
        value->token = ++context_token;
        ++init_context_count;
        for (int index = 0; index < num_options; ++index) {
            aoahid_fake_libusb_init_option_record record{};
            record.option = static_cast<int>(options[index].option);
            if (options[index].option == LIBUSB_OPTION_LOG_LEVEL) {
                record.integer_value = options[index].value.ival;
            } else if (options[index].option == LIBUSB_OPTION_LOG_CB) {
                record.callback_present = options[index].value.log_cbval == nullptr ? 0 : 1;
            }
            init_options.push_back(record);
        }
    }
    *context = value;
    return LIBUSB_SUCCESS;
}

int libusb_init(libusb_context** context) { return libusb_init_context(context, nullptr, 0); }

void libusb_exit(libusb_context* context) { delete context; }

const libusb_version* libusb_get_version(void) {
    static const libusb_version version{1U, 0U, 30U, 0U, "", "libusb 1.0.30 fake"};
    return &version;
}

ssize_t libusb_get_device_list(libusb_context* context, libusb_device*** list) {
    static_cast<void>(context);
    if (list == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    std::size_t count = 0U;
    for (const auto& device : devices) {
        if (device->present) {
            ++count;
        }
    }
    auto** result = static_cast<libusb_device**>(std::calloc(count + 1U, sizeof(libusb_device*)));
    if (result == nullptr) {
        return LIBUSB_ERROR_NO_MEM;
    }
    std::size_t output_index = 0U;
    for (const auto& device : devices) {
        if (device->present) {
            result[output_index++] = device.get();
        }
    }
    *list = result;
    return static_cast<ssize_t>(count);
}

void libusb_free_device_list(libusb_device** list, int unref_devices) {
    static_cast<void>(unref_devices);
    std::free(list);
}

int libusb_get_device_descriptor(libusb_device* device, libusb_device_descriptor* descriptor) {
    if (device == nullptr || descriptor == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (!device->present) {
        return LIBUSB_ERROR_NO_DEVICE;
    }
    std::memset(descriptor, 0, sizeof(*descriptor));
    descriptor->bLength = 18U;
    descriptor->bMaxPacketSize0 = 64U;
    descriptor->idVendor = device->vendor_id;
    descriptor->idProduct = device->product_id;
    descriptor->iSerialNumber = device->serial.empty() ? 0U : 1U;
    descriptor->iProduct = device->product.empty() ? 0U : 2U;
    descriptor->bNumConfigurations = 1U;
    return LIBUSB_SUCCESS;
}

uint8_t libusb_get_bus_number(libusb_device* device) {
    return device == nullptr ? 0U : device->bus;
}

uint8_t libusb_get_device_address(libusb_device* device) {
    return device == nullptr ? 0U : device->address;
}

int libusb_get_port_numbers(libusb_device* device, uint8_t* ports, int port_count) {
    if (device == nullptr || ports == nullptr || port_count < 0) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (device->ports.size() > static_cast<std::size_t>(port_count)) {
        return LIBUSB_ERROR_OVERFLOW;
    }
    std::copy(device->ports.begin(), device->ports.end(), ports);
    return static_cast<int>(device->ports.size());
}

int libusb_open(libusb_device* device, libusb_device_handle** handle) {
    if (device == nullptr || handle == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    *handle = nullptr;
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (!device->present) {
        return LIBUSB_ERROR_NO_DEVICE;
    }
    if (device->open_status != LIBUSB_SUCCESS) {
        return device->open_status;
    }
    auto* result = new (std::nothrow) libusb_device_handle();
    if (result == nullptr) {
        return LIBUSB_ERROR_NO_MEM;
    }
    result->device = device;
    *handle = result;
    return LIBUSB_SUCCESS;
}

void libusb_close(libusb_device_handle* handle) { delete handle; }

int libusb_get_string_descriptor_ascii(libusb_device_handle* handle, const uint8_t descriptor_index,
                                       unsigned char* data, const int length) {
    if (handle == nullptr || handle->device == nullptr || data == nullptr || length < 0) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (!handle->device->present) {
        return LIBUSB_ERROR_NO_DEVICE;
    }
    const std::string* value = nullptr;
    if (descriptor_index == 1U) {
        value = &handle->device->serial;
    } else if (descriptor_index == 2U) {
        value = &handle->device->product;
    } else {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    const std::size_t copied = std::min(value->size(), static_cast<std::size_t>(length));
    std::memcpy(data, value->data(), copied);
    return static_cast<int>(copied);
}

int libusb_control_transfer(libusb_device_handle* handle, const uint8_t request_type,
                            const uint8_t request, const uint16_t value, const uint16_t index,
                            unsigned char* data, const uint16_t length,
                            const unsigned int timeout) {
    if (handle == nullptr || handle->device == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    libusb_device* device = handle->device;
    if (!device->present) {
        return LIBUSB_ERROR_NO_DEVICE;
    }
    record_control_locked(device, request_type, request, value, index, length, timeout, false,
                          data);

    int result = static_cast<int>(length);
    if (!control_results.empty()) {
        result = control_results.front();
        control_results.pop_front();
    } else if (request == 51U && request_type == 0xC0U) {
        result = std::min<int>(2, static_cast<int>(length));
    }

    if (result >= 0 && request == 51U && request_type == 0xC0U && data != nullptr) {
        if (result > 0 && length > 0U) {
            data[0] = static_cast<unsigned char>(device->protocol_version & 0xFFU);
        }
        if (result > 1 && length > 1U) {
            data[1] = static_cast<unsigned char>((device->protocol_version >> 8U) & 0xFFU);
        }
    }
    return result;
}

int libusb_claim_interface(libusb_device_handle* handle, const int interface_number) {
    if (handle == nullptr || handle->device == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    const int result =
        handle->device->present ? handle->device->claim_status : LIBUSB_ERROR_NO_DEVICE;
    claims.push_back(
        aoahid_fake_libusb_claim_record{handle->device->index, interface_number, 0, result});
    return result;
}

int libusb_release_interface(libusb_device_handle* handle, const int interface_number) {
    if (handle == nullptr || handle->device == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    const int result = handle->device->present ? LIBUSB_SUCCESS : LIBUSB_ERROR_NO_DEVICE;
    claims.push_back(
        aoahid_fake_libusb_claim_record{handle->device->index, interface_number, 1, result});
    return result;
}

libusb_transfer* libusb_alloc_transfer(const int iso_packets) {
    static_cast<void>(iso_packets);
    auto* transfer = new (std::nothrow) libusb_transfer{};
    if (transfer == nullptr) {
        return nullptr;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    allocated_transfers.push_back(transfer);
    return transfer;
}

void libusb_free_transfer(libusb_transfer* transfer) {
    if (transfer == nullptr) {
        return;
    }
    {
        const std::lock_guard<std::mutex> guard(global_mutex);
        const auto found =
            std::find(allocated_transfers.begin(), allocated_transfers.end(), transfer);
        if (found != allocated_transfers.end()) {
            allocated_transfers.erase(found);
        }
    }
    delete transfer;
}

int libusb_submit_transfer(libusb_transfer* transfer) {
    if (transfer == nullptr || transfer->dev_handle == nullptr ||
        transfer->dev_handle->device == nullptr || transfer->buffer == nullptr ||
        transfer->length < static_cast<int>(LIBUSB_CONTROL_SETUP_SIZE)) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (!transfer->dev_handle->device->present) {
        return LIBUSB_ERROR_NO_DEVICE;
    }
    if (transfer->fake_submitted != 0) {
        return LIBUSB_ERROR_BUSY;
    }
    if (!submit_results.empty()) {
        const int result = submit_results.front();
        submit_results.pop_front();
        if (result != LIBUSB_SUCCESS) {
            return result;
        }
    }

    const auto* setup = reinterpret_cast<const libusb_control_setup*>(transfer->buffer);
    const std::uint16_t value = aoahid_fake_le16_to_cpu(setup->wValue);
    const std::uint16_t index = aoahid_fake_le16_to_cpu(setup->wIndex);
    const std::uint16_t length = aoahid_fake_le16_to_cpu(setup->wLength);
    record_control_locked(transfer->dev_handle->device, setup->bmRequestType, setup->bRequest,
                          value, index, length, transfer->timeout, true,
                          transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE);

    AsyncOutcome outcome{};
    if (!async_outcomes.empty()) {
        outcome = async_outcomes.front();
        async_outcomes.pop_front();
    }
    transfer->fake_submitted = 1;
    transfer->fake_cancel_requested = 0;
    transfer->fake_completion_status = outcome.status;
    transfer->fake_completion_length =
        outcome.actual_length < 0 ? static_cast<int>(length) : outcome.actual_length;
    transfer->fake_ready_poll = poll_number + static_cast<std::uint64_t>(outcome.delay_polls) + 1U;
    ++event_stats.successful_submits;
    notify_event_locked(EventNotification::submit);
    return LIBUSB_SUCCESS;
}

int libusb_cancel_transfer(libusb_transfer* transfer) {
    if (transfer == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (transfer->fake_submitted == 0 || transfer->fake_cancel_requested != 0) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    ++cancel_count;
    if (!cancel_results.empty()) {
        const int result = cancel_results.front();
        cancel_results.pop_front();
        if (result != LIBUSB_SUCCESS)
            return result;
    }
    transfer->fake_cancel_requested = 1;
    transfer->fake_completion_status = LIBUSB_TRANSFER_CANCELLED;
    transfer->fake_completion_length = 0;
    transfer->fake_ready_poll = poll_number + 1U;
    notify_event_locked(EventNotification::cancel);
    return LIBUSB_SUCCESS;
}

void libusb_interrupt_event_handler(libusb_context* context) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    ++event_stats.interrupt_calls;
    if (context != nullptr) {
        context->user_interrupt_pending = true;
    }
    event_cv.notify_all();
}

int libusb_handle_events_timeout_completed(libusb_context* context, const timeval* timeout,
                                           int* completed) {
    std::unique_lock<std::mutex> lock(global_mutex);
    ++event_handle_count;
    ++event_stats.handle_calls;
    event_cv.notify_all();
    const std::chrono::microseconds requested_timeout = timeout_duration(timeout);
    event_stats.requested_timeout_us += static_cast<std::uint64_t>(requested_timeout.count());
    if (requested_timeout == std::chrono::microseconds::zero()) {
        ++event_stats.zero_timeout_calls;
    }
    if (context != nullptr && context->user_interrupt_pending) {
        context->user_interrupt_pending = false;
        ++event_stats.interrupt_wakeups;
        return LIBUSB_SUCCESS;
    }
    if (!event_results.empty()) {
        const int result = event_results.front();
        event_results.pop_front();
        if (result != LIBUSB_SUCCESS)
            return result;
    }
    if (!completion_ready_next_poll_locked() &&
        requested_timeout != std::chrono::microseconds::zero()) {
        const std::uint64_t generation = event_notification_generation;
        ++event_stats.waits_started;
        ++event_stats.active_waiters;
        event_cv.notify_all();
        const bool notified =
            event_cv.wait_for(lock, requested_timeout, [context, generation]() noexcept {
                return (context != nullptr && context->user_interrupt_pending) ||
                       event_notification_generation != generation ||
                       completion_ready_next_poll_locked();
            });
        --event_stats.active_waiters;
        if (notified) {
            ++event_stats.notification_wakeups;
            if (context != nullptr && context->user_interrupt_pending) {
                context->user_interrupt_pending = false;
                ++event_stats.interrupt_wakeups;
                event_cv.notify_all();
                return LIBUSB_SUCCESS;
            }
            if (last_event_notification == EventNotification::cancel) {
                ++event_stats.cancellation_wakeups;
            }
        } else {
            ++event_stats.timeout_wakeups;
        }
        event_cv.notify_all();
    }

    // Reuse per-event-thread storage. A caller can prewarm this capacity after
    // opening a Node, so fake dispatch does not contaminate allocation probes.
    thread_local std::vector<libusb_transfer*> callbacks;
    callbacks.clear();
    callbacks.reserve(allocated_transfers.size());
    ++poll_number;
    for (libusb_transfer* transfer : allocated_transfers) {
        if (transfer == nullptr || transfer->fake_submitted == 0 ||
            transfer->fake_ready_poll > poll_number) {
            continue;
        }
        transfer->fake_submitted = 0;
        transfer->status = transfer->fake_completion_status;
        transfer->actual_length = transfer->fake_completion_length;
        callbacks.push_back(transfer);
    }
    event_stats.callbacks_dispatched += callbacks.size();
    lock.unlock();
    if (completed != nullptr) {
        *completed = callbacks.empty() ? 0 : 1;
    }
    for (libusb_transfer* transfer : callbacks) {
        if (transfer->callback != nullptr) {
            transfer->callback(transfer);
        }
    }
    return LIBUSB_SUCCESS;
}

void aoahid_fake_libusb_reset(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    devices.clear();
    control_results.clear();
    submit_results.clear();
    cancel_results.clear();
    event_results.clear();
    async_outcomes.clear();
    controls.clear();
    claims.clear();
    init_options.clear();
    allocated_transfers.clear();
    poll_number = 0U;
    cancel_count = 0U;
    event_handle_count = 0U;
    event_stats = {};
    event_notification_generation = 0U;
    last_event_notification = EventNotification::none;
    init_context_count = 0U;
    control_recording_enabled = true;
}

int aoahid_fake_libusb_add_device(const aoahid_fake_libusb_device_config* config) {
    if (config == nullptr || (config->port_path_length > 0U && config->port_path == nullptr)) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    auto device = std::make_unique<libusb_device>();
    device->bus = config->bus;
    device->address = config->address;
    if (config->port_path_length > 0U) {
        device->ports.assign(config->port_path, config->port_path + config->port_path_length);
    }
    device->vendor_id = config->vendor_id;
    device->product_id = config->product_id;
    device->protocol_version = config->protocol_version;
    device->serial = config->serial == nullptr ? "" : config->serial;
    device->product = config->product == nullptr ? "" : config->product;
    device->open_status = config->open_status;
    const std::lock_guard<std::mutex> guard(global_mutex);
    device->index = devices.size();
    const int result = static_cast<int>(device->index);
    devices.push_back(std::move(device));
    return result;
}

void aoahid_fake_libusb_set_present(const size_t device_index, const int present) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (libusb_device* device = find_device(device_index); device != nullptr) {
        device->present = present != 0;
    }
}

void aoahid_fake_libusb_set_protocol(const size_t device_index, const uint16_t protocol_version) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (libusb_device* device = find_device(device_index); device != nullptr) {
        device->protocol_version = protocol_version;
    }
}

void aoahid_fake_libusb_set_claim_result(const size_t device_index, const int result) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (libusb_device* device = find_device(device_index); device != nullptr) {
        device->claim_status = result;
    }
}

void aoahid_fake_libusb_queue_control_result(const int result) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    control_results.push_back(result);
}

void aoahid_fake_libusb_queue_submit_result(const int result) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    submit_results.push_back(result);
}

void aoahid_fake_libusb_queue_cancel_result(const int result) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    cancel_results.push_back(result);
}

void aoahid_fake_libusb_queue_event_result(const int result) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    event_results.push_back(result);
    notify_event_locked(EventNotification::backend_result);
}

void aoahid_fake_libusb_queue_async_completion(const libusb_transfer_status status,
                                               const int actual_length,
                                               const uint32_t delay_polls) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    async_outcomes.push_back(AsyncOutcome{status, actual_length, delay_polls});
}

void aoahid_fake_libusb_make_pending_transfers_ready(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    for (libusb_transfer* transfer : allocated_transfers) {
        if (transfer != nullptr && transfer->fake_submitted != 0) {
            transfer->fake_ready_poll = poll_number + 1U;
        }
    }
    notify_event_locked(EventNotification::ready);
}

void aoahid_fake_libusb_set_control_recording(const int enabled) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    control_recording_enabled = enabled != 0;
}

size_t aoahid_fake_libusb_control_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return controls.size();
}

int aoahid_fake_libusb_get_control(const size_t index, aoahid_fake_libusb_control_record* record) {
    if (record == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (index >= controls.size()) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    *record = controls[index].record;
    return LIBUSB_SUCCESS;
}

size_t aoahid_fake_libusb_copy_control_data(const size_t index, uint8_t* output,
                                            const size_t capacity) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (index >= controls.size()) {
        return 0U;
    }
    const auto& data = controls[index].data;
    if (output == nullptr) {
        return data.size();
    }
    const std::size_t copied = std::min(capacity, data.size());
    std::copy_n(data.begin(), copied, output);
    return copied;
}

size_t aoahid_fake_libusb_claim_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return claims.size();
}

int aoahid_fake_libusb_get_claim(const size_t index, aoahid_fake_libusb_claim_record* record) {
    if (record == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (index >= claims.size()) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    *record = claims[index];
    return LIBUSB_SUCCESS;
}

size_t aoahid_fake_libusb_pending_transfer_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return static_cast<size_t>(std::count_if(allocated_transfers.begin(), allocated_transfers.end(),
                                             [](const libusb_transfer* transfer) {
                                                 return transfer != nullptr &&
                                                        transfer->fake_submitted != 0;
                                             }));
}

size_t aoahid_fake_libusb_cancel_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return cancel_count;
}

size_t aoahid_fake_libusb_event_handle_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return event_handle_count;
}

int aoahid_fake_libusb_get_event_stats(aoahid_fake_libusb_event_stats* stats) {
    if (stats == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    *stats = event_stats;
    return LIBUSB_SUCCESS;
}

int aoahid_fake_libusb_wait_for_event_calls(const size_t minimum, const unsigned int timeout_ms) {
    std::unique_lock<std::mutex> lock(global_mutex);
    const bool reached =
        event_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [minimum]() noexcept { return event_stats.handle_calls >= minimum; });
    return reached ? LIBUSB_SUCCESS : LIBUSB_ERROR_TIMEOUT;
}

int aoahid_fake_libusb_wait_for_event_waiters(const size_t minimum, const unsigned int timeout_ms) {
    std::unique_lock<std::mutex> lock(global_mutex);
    const bool reached =
        event_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [minimum]() noexcept { return event_stats.active_waiters >= minimum; });
    return reached ? LIBUSB_SUCCESS : LIBUSB_ERROR_TIMEOUT;
}

size_t aoahid_fake_libusb_init_context_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return init_context_count;
}

size_t aoahid_fake_libusb_init_option_count(void) {
    const std::lock_guard<std::mutex> guard(global_mutex);
    return init_options.size();
}

int aoahid_fake_libusb_get_init_option(const size_t index,
                                       aoahid_fake_libusb_init_option_record* record) {
    if (record == nullptr) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    const std::lock_guard<std::mutex> guard(global_mutex);
    if (index >= init_options.size()) {
        return LIBUSB_ERROR_NOT_FOUND;
    }
    *record = init_options[index];
    return LIBUSB_SUCCESS;
}

} // extern "C"
