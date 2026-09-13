// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Declares the AOA/libusb transport boundary and its ownership model; HID
 * profile semantics remain in the layers above it. */
#pragma once

/*
 * This header is the boundary of the libusb transport. It exposes only plain
 * host-side values to the API and profile layers; no libusb type crosses it.
 */

#include "aoahid.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace aoa::transport {

class Device;

struct Candidate {
    std::uint8_t bus{};
    std::uint8_t address{};
    std::vector<std::uint8_t> port_path;
    std::uint16_t vendor_id{};
    std::uint16_t product_id{};
    std::string serial;
    std::string product;
};

struct DeviceConfig {
    aoahid_event_mode event_mode{};
    bool accept_future_versions{};
    std::uint32_t control_timeout_ms{};
    std::uint32_t send_timeout_ms{};
    std::uint32_t descriptor_fragment_bytes{};
    std::uint32_t pool_slots{};
    std::uint32_t maximum_report_bytes{};
    std::uint32_t first_report_attempts{};
    std::uint32_t first_report_backoff_us{};
    aoahid_interface_claim_policy claim_policy{};
    std::int32_t interface_number{};
};

using Completion = void (*)(void* user, aoahid_result result, std::int32_t native_status) noexcept;

struct PreparedTransfer {
    void* slot{};
    std::uint8_t* payload{};
    std::size_t capacity{};
};

struct ReservationToken {
    void* value{};
};

struct ErrorInfo {
    aoahid_result result{AOAHID_OK};
    std::int32_t native_status{};
    std::int32_t aoa_request{};
    std::uint16_t hid_id{};
    std::uint32_t offset{};
    std::uint32_t length{};
    std::uint16_t report_id{};
};

ErrorInfo last_error() noexcept;
bool is_cancelled_completion_status(std::int32_t native_status) noexcept;
void reset_error() noexcept;
void record_error(aoahid_result result, std::int32_t native_status = 0,
                  std::int32_t aoa_request = 0, std::uint16_t hid_id = 0, std::uint32_t offset = 0,
                  std::uint32_t length = 0) noexcept;

class Runtime final {
  public:
    static aoahid_result create(aoahid_event_mode event_mode, Runtime** out) noexcept;
    ~Runtime() noexcept;

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    aoahid_result poll(std::uint32_t timeout_ms) noexcept;
    void interrupt_event_handler() noexcept;
    aoahid_result discover(std::uint32_t timeout_ms, std::vector<Candidate>* out);
    void* native_context() const noexcept;

    /* First-report STALL retries are the only source of deferred transport
     * work, and they arise solely from the registration race. Counting the
     * armed retries lets every poll skip the device registry and the per-slot
     * scan in the overwhelmingly common case of none.
     *
     * Device::Impl calls these, so they are public rather than relying on the
     * access a nested class inherits from its enclosing friend. This header is
     * internal and is never installed. */
    void note_retry_armed() noexcept;
    void note_retry_disarmed() noexcept;
    bool any_retry_armed() const noexcept;

  private:
    Runtime() noexcept = default;

    friend class Device;
    void register_device(Device* device);
    void unregister_device(Device* device) noexcept;
    std::uint64_t next_retry_delay_us(std::uint64_t maximum_us) noexcept;
    void service_retries() noexcept;

    void* context_{};
    aoahid_event_mode event_mode_{};
    std::atomic<std::uint32_t> retries_armed_{0U};
    std::mutex devices_mutex_;
    std::vector<Device*> devices_;
};

class Device final {
  public:
    static aoahid_result open(Runtime* runtime, const Candidate& candidate,
                              const DeviceConfig& config, Device** out,
                              std::uint16_t* protocol_version);
    ~Device() noexcept;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    aoahid_result register_hid(std::uint16_t hid_id, const std::uint8_t* descriptor,
                               std::size_t descriptor_length) noexcept;
    aoahid_result unregister_hid(std::uint16_t hid_id) noexcept;
    aoahid_result set_reservation(std::uint16_t hid_id, std::size_t count, ReservationToken* out);
    aoahid_result clear_reservation(ReservationToken token) noexcept;
    aoahid_result acquire(std::uint16_t hid_id, ReservationToken reservation,
                          std::size_t report_length, void* user, Completion completion,
                          PreparedTransfer* out) noexcept;
    aoahid_result submit(PreparedTransfer prepared, std::size_t report_length,
                         std::uint16_t report_id) noexcept;
    aoahid_result submit_first(PreparedTransfer prepared, std::size_t report_length,
                               std::uint16_t report_id) noexcept;
    void abandon(PreparedTransfer prepared) noexcept;
    aoahid_result cancel_all() noexcept;
    bool drained() const noexcept;
    bool present() const noexcept;
    aoahid_result latched_error(ErrorInfo* detail = nullptr) noexcept;

  private:
    Device() noexcept = default;

    friend class Runtime;
    std::uint64_t next_retry_delay_us(std::uint64_t maximum_us) noexcept;
    void service_retries() noexcept;

    struct Impl;
    Impl* impl_{};
};

aoahid_result map_libusb_error(std::int32_t status, bool probe) noexcept;

} // namespace aoa::transport
