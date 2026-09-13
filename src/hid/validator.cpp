// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * This file rejects descriptor/layout mismatches before Android sees them. The
 * raw parser follows HID 1.11 short-item and report constraints without trying
 * to predict Android support for unknown Usages.
 */

#include "hid/validator.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace aoa::hid {

namespace {
ValidationIssue issue(const aoahid_result result, const char* field, const char* reason,
                      const std::size_t offset = 0U) noexcept {
    return ValidationIssue{result, field, reason, offset};
}

std::uint32_t unsigned_value(const std::uint8_t* bytes, const std::size_t size) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0; index < size; ++index) {
        value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
    }
    return value;
}

std::int32_t signed_value(const std::uint8_t* bytes, const std::size_t size) noexcept {
    const std::uint32_t value = unsigned_value(bytes, size);
    if (size == 0U) {
        return 0;
    }
    const std::size_t bits = size * 8U;
    const std::uint32_t sign_bit = std::uint32_t{1U} << (bits - 1U);
    if ((value & sign_bit) == 0U) {
        return static_cast<std::int32_t>(value);
    }
    // Convert through a representable int64_t value rather than relying on an
    // implementation-defined unsigned-to-signed narrowing conversion.
    return static_cast<std::int32_t>(static_cast<std::int64_t>(value) - (std::int64_t{1} << bits));
}

struct Globals {
    std::int32_t logical_minimum{};
    std::int32_t logical_maximum{};
    std::int32_t physical_minimum{};
    std::int32_t physical_maximum{};
    std::uint32_t report_size{};
    std::uint32_t report_count{};
    std::uint32_t unit{};
    std::uint32_t usage_page{};
    std::uint8_t report_id{};
    bool usage_page_declared{};
    bool logical_minimum_declared{};
    bool logical_maximum_declared{};
    bool physical_minimum_declared{};
    bool physical_maximum_declared{};
    bool unit_exponent_declared{};
};

struct ParsedReport {
    bool seen{};
    std::size_t bits{};
    std::uint32_t fields{};
    std::size_t top_level{std::numeric_limits<std::size_t>::max()};
};

struct Locals {
    std::uint64_t usage_count{};
    std::uint32_t usage_minimum{};
    std::uint8_t usage_minimum_size{};
    bool usage_minimum_declared{};
    std::uint32_t delimiter_depth{};
    std::uint32_t delimiter_branch{};
};

bool add_usage_count(Locals* locals, const std::uint64_t count) noexcept {
    if (locals->usage_count > std::numeric_limits<std::uint64_t>::max() - count) {
        return false;
    }
    locals->usage_count += count;
    return true;
}

void observe_usages(DescriptorRequirements* requirements, const std::uint64_t count) noexcept {
    requirements->maximum_usages = std::max(requirements->maximum_usages, count);
}

struct FieldSpan {
    std::size_t initial_bit;
    std::uint32_t report_size;
    std::uint32_t report_count;
};

bool field_span_is_valid(const FieldSpan span) noexcept {
    // A field's starting alignment repeats modulo eight. Checking at most one
    // complete period proves the span for any Report Count without iterating
    // over a potentially 32-bit count.
    const std::uint32_t alignments = std::min<std::uint32_t>(span.report_count, 8U);
    const std::uint32_t size_modulo = span.report_size & 7U;
    std::uint32_t start_modulo = static_cast<std::uint32_t>(span.initial_bit & 7U);
    for (std::uint32_t field = 0U; field < alignments; ++field) {
        const std::uint32_t occupied_bytes = (start_modulo + span.report_size + 7U) / 8U;
        if (occupied_bytes > 4U) {
            return false;
        }
        start_modulo = (start_modulo + size_modulo) & 7U;
    }
    return true;
}

bool raw_logical_range_is_ordered(const std::int32_t logical_minimum,
                                  const std::int32_t logical_maximum) noexcept {
    if (logical_minimum < 0) {
        return logical_maximum >= logical_minimum;
    }
    return static_cast<std::uint32_t>(logical_maximum) >=
           static_cast<std::uint32_t>(logical_minimum);
}

bool raw_value_fits(const std::int32_t logical_minimum, const std::int32_t logical_maximum,
                    const std::uint8_t bit_width) noexcept {
    if (bit_width == 0U || bit_width > 32U ||
        !raw_logical_range_is_ordered(logical_minimum, logical_maximum)) {
        return false;
    }
    if (logical_minimum < 0) {
        if (bit_width == 32U) {
            return true;
        }
        const std::int64_t minimum = -(std::int64_t{1} << (bit_width - 1U));
        const std::int64_t maximum = (std::int64_t{1} << (bit_width - 1U)) - 1;
        return logical_minimum >= minimum && logical_maximum <= maximum;
    }
    if (bit_width == 32U) {
        return true;
    }
    const std::uint64_t maximum = (std::uint64_t{1} << bit_width) - 1U;
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(logical_maximum)) <= maximum;
}
} // namespace

bool value_fits(const std::int32_t logical_minimum, const std::int32_t logical_maximum,
                const std::uint8_t bit_width) noexcept {
    if (bit_width == 0U || bit_width > 32U || logical_maximum < logical_minimum) {
        return false;
    }
    if (logical_minimum < 0 || logical_maximum < 0) {
        if (bit_width == 32U) {
            return true;
        }
        const std::int64_t minimum = -(std::int64_t{1} << (bit_width - 1U));
        const std::int64_t maximum = (std::int64_t{1} << (bit_width - 1U)) - 1;
        return logical_minimum >= minimum && logical_maximum <= maximum;
    }
    if (bit_width == 32U) {
        return true;
    }
    const std::uint64_t maximum = (std::uint64_t{1} << bit_width) - 1U;
    return static_cast<std::uint64_t>(logical_maximum) <= maximum;
}

bool unit_is_valid(const std::uint32_t unit) noexcept {
    if (unit == 0U) {
        return true;
    }
    const std::uint32_t system = unit & 0x0FU;
    const bool defined_system = (system >= 1U && system <= 4U) || system == 0x0FU;
    return defined_system && (unit & 0xF0000000U) == 0U;
}

namespace {
ValidationIssue validate_generated_structure(const std::uint8_t* descriptor,
                                             const std::size_t descriptor_length,
                                             const ReportLayout& layout,
                                             const std::size_t layout_bits) noexcept {
    std::uint32_t report_size = 0U;
    std::uint32_t report_count = 0U;
    std::size_t input_bits = 0U;
    std::size_t feature_bits = 0U;
    std::size_t collection_depth = 0U;
    std::size_t top_level_count = 0U;
    std::size_t offset = 0U;
    std::uint8_t report_id = 0U;
    bool report_id_seen = false;
    bool data_main_seen = false;

    while (offset < descriptor_length) {
        const std::size_t item_offset = offset;
        const std::uint8_t prefix = descriptor[offset++];
        if ((prefix & 0xF0U) == 0xF0U) {
            return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                         "The generated descriptor unexpectedly contains a HID long item.",
                         item_offset);
        }
        const std::uint8_t size_code = prefix & 0x03U;
        const std::size_t size = size_code == 3U ? 4U : size_code;
        if (size > descriptor_length - offset) {
            return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                         "A generated short item extends beyond the descriptor.", item_offset);
        }
        const std::uint8_t type = (prefix >> 2U) & 0x03U;
        const std::uint8_t tag = (prefix >> 4U) & 0x0FU;
        const std::uint32_t value = unsigned_value(descriptor + offset, size);

        if (type == 1U) {
            if (tag == 7U) {
                report_size = value;
            } else if (tag == 8U) {
                if (value == 0U || value > 255U || report_id_seen || data_main_seen) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.report_id",
                                 "The generated descriptor changes, repeats, or misorders its "
                                 "Report ID.",
                                 item_offset);
                }
                report_id = static_cast<std::uint8_t>(value);
                report_id_seen = true;
            } else if (tag == 9U) {
                report_count = value;
            } else if (tag == 10U || tag == 11U) {
                return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                             "The generated descriptor unexpectedly contains Global Push or Pop.",
                             item_offset);
            }
        } else if (type == 0U) {
            if (tag == 10U) {
                if (value > 0xFFU || (value >= 0x07U && value <= 0x7FU)) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.collection",
                                 "The generated descriptor contains a reserved Collection type.",
                                 item_offset);
                }
                if (collection_depth == 0U) {
                    if (value != 1U || top_level_count != 0U) {
                        return issue(
                            AOAHID_ERR_INTERNAL, "spec.collection",
                            "The generated descriptor must have exactly one top-level Application "
                            "collection.",
                            item_offset);
                    }
                    ++top_level_count;
                }
                ++collection_depth;
            } else if (tag == 12U) {
                if (collection_depth == 0U) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.collection",
                                 "A generated End Collection has no matching Collection.",
                                 item_offset);
                }
                --collection_depth;
            } else if (tag == 8U || tag == 9U || tag == 11U) {
                data_main_seen = true;
                if (collection_depth == 0U || report_size == 0U || report_count == 0U) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                                 "A generated data Main item lacks collection or report state.",
                                 item_offset);
                }
                if (tag == 9U) {
                    return issue(AOAHID_ERR_UNSUPPORTED, "spec.descriptor",
                                 "Generated AOA HID profiles cannot require Output reports.",
                                 item_offset);
                }
                if (report_size > 32U) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                                 "A generated field exceeds the HID 32-bit field limit.",
                                 item_offset);
                }
                std::size_t& report_bits = tag == 8U ? input_bits : feature_bits;
                if (!field_span_is_valid(FieldSpan{report_bits, report_size, report_count})) {
                    return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                                 "A generated field spans more than four report bytes.",
                                 item_offset);
                }
                const std::uint64_t added = static_cast<std::uint64_t>(report_size) *
                                            static_cast<std::uint64_t>(report_count);
                if (added > std::numeric_limits<std::size_t>::max() - report_bits) {
                    return issue(AOAHID_ERR_OVERFLOW, "spec.descriptor",
                                 "A generated report bit count overflows size_t.", item_offset);
                }
                report_bits += static_cast<std::size_t>(added);
            } else {
                return issue(AOAHID_ERR_INTERNAL, "spec.descriptor",
                             "The generated descriptor contains a reserved Main item.",
                             item_offset);
            }
        }
        offset += size;
    }

    if (collection_depth != 0U || top_level_count != 1U) {
        return issue(AOAHID_ERR_INTERNAL, "spec.collection",
                     "The generated descriptor has an invalid top-level collection structure.");
    }
    if (report_id_seen != layout.has_report_id ||
        (report_id_seen && report_id != layout.report_id)) {
        return issue(AOAHID_ERR_INTERNAL, "spec.report_id",
                     "The descriptor Report ID does not match its generated layout.");
    }
    const std::size_t prefix_bits = report_id_seen ? 8U : 0U;
    if (input_bits > std::numeric_limits<std::size_t>::max() - prefix_bits ||
        input_bits + prefix_bits != layout_bits) {
        return issue(AOAHID_ERR_INTERNAL, "spec.layout",
                     "Descriptor Input bits do not match the generated field layout.");
    }
    return {};
}
} // namespace

ValidationIssue validate_generated(const std::uint8_t* descriptor,
                                   const std::size_t descriptor_length,
                                   const ReportLayout& layout) noexcept {
    if (descriptor == nullptr || descriptor_length == 0U) {
        return issue(AOAHID_ERR_UNSET_FIELD, "spec.descriptor",
                     "The report descriptor must contain at least one byte.");
    }
    if (descriptor_length > std::numeric_limits<std::uint16_t>::max()) {
        return issue(AOAHID_ERR_OVERFLOW, "spec.descriptor",
                     "The descriptor exceeds the 16-bit AOA wire-length field.");
    }
    if (layout.has_report_id && layout.report_id == 0U) {
        return issue(AOAHID_ERR_PARAM, "spec.report_id", "HID 1.11 reserves Report ID zero.");
    }
    std::size_t layout_bits = layout.has_report_id ? 8U : 0U;
    for (const FieldLayout& field : layout.fields) {
        const auto& physical = field.physical;
        const bool canonical_disabled = physical.enabled == 0U && physical.minimum == 0 &&
                                        physical.maximum == 0 && physical.unit_exponent == 0 &&
                                        physical.unit == 0U;
        const bool valid_enabled = physical.enabled == 1U && physical.maximum > physical.minimum &&
                                   physical.unit_exponent >= -8 && physical.unit_exponent <= 7 &&
                                   physical.unit != 0U && unit_is_valid(physical.unit);
        if (!canonical_disabled && !valid_enabled) {
            return issue(AOAHID_ERR_PARAM, "spec.physical",
                         "Physical properties are neither canonical-disabled nor a valid enabled "
                         "declaration.");
        }
        if (field.bit_width == 0U || field.bit_offset != layout_bits ||
            layout_bits > std::numeric_limits<std::size_t>::max() - field.bit_width) {
            return issue(AOAHID_ERR_INTERNAL, "spec.layout",
                         "Generated fields must be nonempty, contiguous, and overflow-free.");
        }
        if (!value_fits(field.logical_minimum, field.logical_maximum, field.bit_width) &&
            field.semantic != FieldSemantic::constant_padding) {
            return issue(AOAHID_ERR_PARAM, "spec.logical_range",
                         "A logical range does not fit its declared bit width.");
        }
        const std::size_t first_byte = field.bit_offset / 8U;
        const std::size_t last_byte = (field.bit_offset + field.bit_width - 1U) / 8U;
        if (last_byte - first_byte + 1U > 4U) {
            return issue(
                AOAHID_ERR_PARAM, "spec.field",
                "HID 1.11 section 8.4 forbids one field from spanning more than four bytes.");
        }
        layout_bits += field.bit_width;
    }
    if (layout_bits > std::numeric_limits<std::size_t>::max() - 7U ||
        (layout_bits + 7U) / 8U != layout.wire_bytes) {
        return issue(AOAHID_ERR_INTERNAL, "spec.layout",
                     "The generated wire length does not match the generated field layout.");
    }
    return validate_generated_structure(descriptor, descriptor_length, layout, layout_bits);
}

ValidationIssue validate_raw(const std::uint8_t* descriptor, const std::size_t descriptor_length,
                             const aoahid_raw_report* reports, const std::size_t report_count,
                             std::uint8_t* trailing_valid_masks,
                             DescriptorRequirements* requirements) {
    if (requirements != nullptr) {
        *requirements = DescriptorRequirements{};
    }
    if (descriptor == nullptr || descriptor_length == 0U) {
        return issue(AOAHID_ERR_UNSET_FIELD, "raw.descriptor",
                     "The raw descriptor must contain at least one byte.");
    }
    if (descriptor_length > std::numeric_limits<std::uint16_t>::max()) {
        return issue(AOAHID_ERR_OVERFLOW, "raw.descriptor",
                     "The descriptor exceeds the 16-bit AOA wire-length field.");
    }
    if (reports == nullptr || report_count == 0U) {
        return issue(AOAHID_ERR_UNSET_FIELD, "raw.reports",
                     "Raw descriptors require an explicit accepted-report table.");
    }
    if (report_count > 256U) {
        return issue(AOAHID_ERR_OVERFLOW, "raw.reports",
                     "The accepted-report table exceeds the 256 possible Report IDs.");
    }
    for (std::size_t index = 0U; index < report_count; ++index) {
        const aoahid_raw_report& report = reports[index];
        if (report.reserved16 != 0U || report.has_report_id > 1U ||
            (report.has_report_id == 1U && report.report_id == 0U) ||
            (report.has_report_id == 0U && report.report_id != 0U) || report.wire_length == 0U) {
            return issue(AOAHID_ERR_PARAM, "raw.reports",
                         "Every raw report entry needs canonical ID state, nonzero length, and "
                         "zero reserved fields.");
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (reports[previous].report_id == report.report_id &&
                reports[previous].has_report_id == report.has_report_id) {
                return issue(
                    AOAHID_ERR_PARAM, "raw.reports",
                    "The accepted-report table contains a duplicate Input report identity.");
            }
        }
    }

    Globals globals{};
    Locals locals{};
    DescriptorRequirements parsed_requirements{};
    std::vector<Globals> global_stack;
    std::array<ParsedReport, 256> parsed{};
    std::size_t offset = 0U;
    std::size_t collection_depth = 0U;
    std::size_t top_level_count = 0U;
    std::size_t current_top_level = std::numeric_limits<std::size_t>::max();
    bool has_report_ids = false;
    bool saw_main_before_report_id = false;

    while (offset < descriptor_length) {
        const std::size_t item_offset = offset;
        const std::uint8_t prefix = descriptor[offset++];
        if ((prefix & 0xF0U) == 0xF0U) {
            return issue(AOAHID_ERR_UNSUPPORTED, "raw.descriptor",
                         "The validated raw escape hatch does not accept HID long-item tags.",
                         item_offset);
        }
        const std::uint8_t size_code = prefix & 0x03U;
        const std::size_t size = size_code == 3U ? 4U : size_code;
        if (size > descriptor_length - offset) {
            return issue(AOAHID_ERR_PARAM, "raw.descriptor",
                         "A short item extends beyond the descriptor.", item_offset);
        }
        const std::uint8_t type = (prefix >> 2U) & 0x03U;
        const std::uint8_t tag = (prefix >> 4U) & 0x0FU;
        const std::uint8_t* data = descriptor + offset;

        if (type == 1U) {
            switch (tag) {
            case 0U: {
                const std::uint32_t usage_page = unsigned_value(data, size);
                if (usage_page > 0xFFFFU) {
                    return issue(AOAHID_ERR_PARAM, "raw.usage_page",
                                 "HID Usage Page values must fit in 16 bits.", item_offset);
                }
                globals.usage_page = usage_page;
                globals.usage_page_declared = true;
                break;
            }
            case 1U:
                globals.logical_minimum = signed_value(data, size);
                globals.logical_minimum_declared = true;
                break;
            case 2U:
                globals.logical_maximum =
                    globals.logical_minimum < 0
                        ? signed_value(data, size)
                        : static_cast<std::int32_t>(unsigned_value(data, size));
                globals.logical_maximum_declared = true;
                break;
            case 3U:
                globals.physical_minimum = signed_value(data, size);
                globals.physical_minimum_declared = true;
                break;
            case 4U:
                globals.physical_maximum =
                    globals.physical_minimum < 0
                        ? signed_value(data, size)
                        : static_cast<std::int32_t>(unsigned_value(data, size));
                globals.physical_maximum_declared = true;
                break;
            case 5U:
                // HID 1.11 section 6.2.2.7 defines a four-bit code. The
                // audited target also accepts legacy sign-extended encodings,
                // but parser tolerance is not the strict raw format contract.
                if (unsigned_value(data, size) > 0x0FU) {
                    return issue(AOAHID_ERR_PARAM, "raw.unit_exponent",
                                 "Unit Exponent must use the HID 1.11 four-bit encoding.",
                                 item_offset);
                }
                globals.unit_exponent_declared = true;
                break;
            case 6U: {
                const std::uint32_t unit = unsigned_value(data, size);
                if (!unit_is_valid(unit)) {
                    return issue(AOAHID_ERR_PARAM, "raw.unit",
                                 "The Unit item uses a reserved HID system code or a nonzero "
                                 "reserved high nibble.",
                                 item_offset);
                }
                globals.unit = unit;
                break;
            }
            case 7U:
                globals.report_size = unsigned_value(data, size);
                parsed_requirements.maximum_report_size_bits =
                    std::max(parsed_requirements.maximum_report_size_bits, globals.report_size);
                break;
            case 8U: {
                const std::uint32_t id = unsigned_value(data, size);
                if (id == 0U || id > 255U || saw_main_before_report_id) {
                    return issue(
                        AOAHID_ERR_PARAM, "raw.report_id",
                        "Report IDs must be 1 through 255 and precede every data Main item.",
                        item_offset);
                }
                globals.report_id = static_cast<std::uint8_t>(id);
                has_report_ids = true;
                break;
            }
            case 9U:
                globals.report_count = unsigned_value(data, size);
                observe_usages(&parsed_requirements, globals.report_count);
                break;
            case 10U:
                global_stack.push_back(globals);
                parsed_requirements.maximum_global_stack_depth =
                    std::max(parsed_requirements.maximum_global_stack_depth,
                             static_cast<std::uint32_t>(global_stack.size()));
                break;
            case 11U:
                if (global_stack.empty()) {
                    return issue(AOAHID_ERR_PARAM, "raw.global_stack",
                                 "A Global Pop has no matching Push.", item_offset);
                }
                globals = global_stack.back();
                global_stack.pop_back();
                break;
            default:
                // HID 1.11 section 6.2.2.7 reserves Global tags 12 through
                // 15. The exact audited target's hid_parser_global() rejects
                // every unknown Global tag, so accepting one here would defer
                // a deterministic parser failure until after AOA registration.
                return issue(AOAHID_ERR_PARAM, "raw.global",
                             "The descriptor contains a reserved HID Global item tag rejected by "
                             "the audited target parser.",
                             item_offset);
            }
        } else if (type == 2U) {
            // Mirrors the local-state and delimiter behavior of the exact
            // audited target:
            // https://android.googlesource.com/kernel/common/+/35556bed836f8dc07ac55f69c8d17dce3e7f0e25/drivers/hid/hid-core.c
            // symbols hid_parser_local, hid_parser_main, hid_add_field, and
            // hid_register_field. The numeric target limits are deliberately
            // reported as requirements rather than enforced here.
            const std::uint32_t value = unsigned_value(data, size);
            if (tag == 10U) {
                if (value > 1U) {
                    return issue(AOAHID_ERR_PARAM, "raw.delimiter",
                                 "HID Local Delimiter values must be exactly zero or one.",
                                 item_offset);
                }
                if (value == 1U) {
                    if (locals.delimiter_depth != 0U) {
                        return issue(AOAHID_ERR_PARAM, "raw.delimiter",
                                     "Nested HID Local delimiters are invalid.", item_offset);
                    }
                    ++locals.delimiter_depth;
                    ++locals.delimiter_branch;
                } else {
                    if (locals.delimiter_depth == 0U) {
                        return issue(AOAHID_ERR_PARAM, "raw.delimiter",
                                     "A HID Local close delimiter has no matching open delimiter.",
                                     item_offset);
                    }
                    --locals.delimiter_depth;
                }
            } else if (tag == 6U || tag >= 11U) {
                return issue(AOAHID_ERR_PARAM, "raw.local",
                             "The descriptor contains a reserved HID Local item tag.", item_offset);
            } else if (locals.delimiter_branch <= 1U) {
                if (tag == 0U) {
                    if (!add_usage_count(&locals, 1U)) {
                        return issue(AOAHID_ERR_OVERFLOW, "raw.usages",
                                     "The Local Usage count overflows uint64_t.", item_offset);
                    }
                    observe_usages(&parsed_requirements, locals.usage_count);
                } else if (tag == 1U) {
                    // The audited target stores the raw item value here. It
                    // concatenates a Usage Page only when hid_add_usage()
                    // expands the eventual range.
                    locals.usage_minimum = value;
                    locals.usage_minimum_size = static_cast<std::uint8_t>(size);
                    locals.usage_minimum_declared = true;
                } else if (tag == 2U) {
                    if (locals.usage_minimum_declared && locals.usage_minimum_size == 4U &&
                        size != 4U) {
                        return issue(
                            AOAHID_ERR_PARAM, "raw.usage_range",
                            "An extended Usage Minimum requires an extended Usage Maximum.",
                            item_offset);
                    }
                    const std::uint32_t maximum = value;
                    if (maximum == std::numeric_limits<std::uint32_t>::max()) {
                        return issue(AOAHID_ERR_UNSUPPORTED, "raw.usage_range",
                                     "Usage Maximum UINT32_MAX is excluded because the audited "
                                     "target's inclusive expansion can wrap for some Local state.",
                                     item_offset);
                    }
                    if (maximum < locals.usage_minimum) {
                        return issue(AOAHID_ERR_PARAM, "raw.usage_range",
                                     "Usage Maximum is below the current Usage Minimum.",
                                     item_offset);
                    }
                    const std::uint64_t range_count =
                        static_cast<std::uint64_t>(maximum) - locals.usage_minimum + 1U;
                    if (!add_usage_count(&locals, range_count)) {
                        return issue(AOAHID_ERR_OVERFLOW, "raw.usages",
                                     "The Local Usage count overflows uint64_t.", item_offset);
                    }
                    observe_usages(&parsed_requirements, locals.usage_count);
                }
            }
        } else if (type == 0U) {
            observe_usages(&parsed_requirements, locals.usage_count);
            if (locals.delimiter_depth != 0U) {
                return issue(AOAHID_ERR_PARAM, "raw.delimiter",
                             "A HID Local delimiter set must be closed before its Main item.",
                             item_offset);
            }
            if (tag == 10U) {
                const std::uint32_t collection_type = unsigned_value(data, size);
                if (collection_type > 0xFFU ||
                    (collection_type >= 0x07U && collection_type <= 0x7FU)) {
                    return issue(AOAHID_ERR_PARAM, "raw.collection",
                                 "The Collection item uses a reserved or oversized HID type.",
                                 item_offset);
                }
                if (locals.usage_count == 0U) {
                    return issue(AOAHID_ERR_PARAM, "raw.collection",
                                 "HID 1.11 section 6.2.2.6 requires a Usage for every Collection.",
                                 item_offset);
                }
                if (collection_type == 1U && locals.delimiter_branch != 0U) {
                    return issue(AOAHID_ERR_PARAM, "raw.delimiter",
                                 "HID 1.11 section 6.2.2.8 forbids delimiters for Application "
                                 "Collection Usages.",
                                 item_offset);
                }
                if (collection_depth == 0U) {
                    if (collection_type != 1U) {
                        return issue(
                            AOAHID_ERR_PARAM, "raw.collection",
                            "Every top-level collection must be an Application collection.",
                            item_offset);
                    }
                    current_top_level = top_level_count++;
                }
                ++collection_depth;
            } else if (tag == 12U) {
                if (collection_depth == 0U) {
                    return issue(AOAHID_ERR_PARAM, "raw.collection",
                                 "End Collection has no matching Collection.", item_offset);
                }
                --collection_depth;
                if (collection_depth == 0U) {
                    current_top_level = std::numeric_limits<std::size_t>::max();
                }
            } else if (tag == 8U || tag == 9U || tag == 11U) {
                if (!has_report_ids) {
                    saw_main_before_report_id = true;
                }
                if (tag != 8U) {
                    return issue(AOAHID_ERR_UNSUPPORTED, "raw.descriptor",
                                 "The validated raw profile is Input-only and rejects Output and "
                                 "Feature Main items.",
                                 item_offset);
                }
                if (tag == 8U) {
                    if (collection_depth == 0U || globals.report_size == 0U ||
                        globals.report_count == 0U) {
                        return issue(
                            AOAHID_ERR_PARAM, "raw.input",
                            "Each Input item needs a collection, Report Size, and Report Count.",
                            item_offset);
                    }
                    if (has_report_ids && globals.report_id == 0U) {
                        return issue(AOAHID_ERR_PARAM, "raw.report_id",
                                     "A descriptor using Report IDs cannot restore the implicit "
                                     "ID-zero report for Input data.",
                                     item_offset);
                    }
                    if (globals.report_size > 32U) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "HID 1.11 section 8.4 limits an individual field to 32 bits.",
                                     item_offset);
                    }
                    const std::uint32_t input_flags = unsigned_value(data, size);
                    const bool constant = (input_flags & 1U) != 0U;
                    const bool variable = (input_flags & 2U) != 0U;
                    if ((input_flags & ~0x17FU) != 0U) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "The Input item sets HID-reserved flag bits.", item_offset);
                    }
                    if (globals.unit != 0U &&
                        (!globals.logical_minimum_declared || !globals.logical_maximum_declared ||
                         !globals.physical_minimum_declared || !globals.physical_maximum_declared ||
                         !globals.unit_exponent_declared)) {
                        return issue(AOAHID_ERR_PARAM, "raw.unit",
                                     "HUT 1.7 section 3.3 requires Logical and Physical extents "
                                     "plus Unit Exponent when a Unit qualifies a Main item.",
                                     item_offset);
                    }
                    if (!raw_logical_range_is_ordered(globals.logical_minimum,
                                                      globals.logical_maximum)) {
                        return issue(AOAHID_ERR_PARAM, "raw.logical_range",
                                     "The Logical Maximum is below the Logical Minimum under HID "
                                     "signedness rules.",
                                     item_offset);
                    }
                    if (!variable && locals.delimiter_branch != 0U) {
                        return issue(
                            AOAHID_ERR_PARAM, "raw.delimiter",
                            "HID 1.11 section 6.2.2.8 forbids delimiters for Array item Usages.",
                            item_offset);
                    }
                    if (!constant &&
                        (!globals.usage_page_declared || !globals.logical_minimum_declared ||
                         !globals.logical_maximum_declared || locals.usage_count == 0U)) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "HID 1.11 section 6.2.2.2 requires Usage Page, Usage, Logical "
                                     "Minimum, and Logical Maximum to describe control data.",
                                     item_offset);
                    }
                    if (!constant &&
                        !raw_value_fits(globals.logical_minimum, globals.logical_maximum,
                                        static_cast<std::uint8_t>(globals.report_size))) {
                        return issue(AOAHID_ERR_PARAM, "raw.logical_range",
                                     "A data Input logical range does not fit its Report Size and "
                                     "signedness.",
                                     item_offset);
                    }
                    ParsedReport& report = parsed[globals.report_id];
                    if ((input_flags & 0x100U) != 0U && (report.bits & 7U) != 0U) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "Buffered Bytes Input data must begin on a byte boundary.",
                                     item_offset);
                    }
                    if (report.seen && report.top_level != current_top_level) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "One report cannot cross two top-level collections.",
                                     item_offset);
                    }
                    report.seen = true;
                    report.top_level = current_top_level;
                    if (locals.usage_count != 0U) {
                        if (report.fields == std::numeric_limits<std::uint32_t>::max()) {
                            return issue(AOAHID_ERR_OVERFLOW, "raw.fields",
                                         "The per-report Input field count overflows uint32_t.",
                                         item_offset);
                        }
                        ++report.fields;
                        parsed_requirements.maximum_fields_per_report =
                            std::max(parsed_requirements.maximum_fields_per_report, report.fields);
                    }
                    if (!field_span_is_valid(
                            FieldSpan{report.bits, globals.report_size, globals.report_count})) {
                        return issue(AOAHID_ERR_PARAM, "raw.input",
                                     "HID 1.11 section 8.4 forbids one field from spanning more "
                                     "than four bytes.",
                                     item_offset);
                    }
                    const std::size_t size_value = static_cast<std::size_t>(globals.report_size);
                    const std::size_t count_value = static_cast<std::size_t>(globals.report_count);
                    if (count_value > std::numeric_limits<std::size_t>::max() / size_value) {
                        return issue(AOAHID_ERR_OVERFLOW, "raw.input",
                                     "Report Size multiplied by Report Count overflows size_t.",
                                     item_offset);
                    }
                    const std::size_t added_bits = size_value * count_value;
                    if (report.bits > std::numeric_limits<std::size_t>::max() - added_bits) {
                        return issue(AOAHID_ERR_OVERFLOW, "raw.input",
                                     "The report bit length overflows size_t.", item_offset);
                    }
                    report.bits += added_bits;
                    parsed_requirements.maximum_report_data_bits =
                        std::max(parsed_requirements.maximum_report_data_bits,
                                 static_cast<std::uint64_t>(report.bits));
                }
            } else {
                return issue(AOAHID_ERR_PARAM, "raw.main",
                             "The descriptor contains a reserved HID Main item tag.", item_offset);
            }
            // Linux hid_parser_main resets the complete Local environment
            // after every Main item, including Collection and End Collection.
            locals = Locals{};
        } else {
            return issue(AOAHID_ERR_PARAM, "raw.item_type",
                         "The descriptor contains a reserved HID short-item type.", item_offset);
        }
        offset += size;
    }

    if (collection_depth != 0U || !global_stack.empty() || locals.delimiter_depth != 0U) {
        return issue(AOAHID_ERR_PARAM, "raw.descriptor",
                     "Collections, the Global state stack, or a Local delimiter are unbalanced.");
    }
    if (top_level_count == 0U) {
        return issue(AOAHID_ERR_PARAM, "raw.collection",
                     "The descriptor has no top-level Application collection.");
    }

    std::size_t parsed_count = 0U;
    for (std::size_t id = 0U; id < parsed.size(); ++id) {
        if (!parsed[id].seen) {
            continue;
        }
        ++parsed_count;
        if (parsed[id].bits > std::numeric_limits<std::size_t>::max() - 7U) {
            return issue(AOAHID_ERR_OVERFLOW, "raw.input",
                         "Rounding the report bit length to bytes overflows size_t.");
        }
        const std::size_t expected = (parsed[id].bits + 7U) / 8U + (has_report_ids ? 1U : 0U);
        if (expected > std::numeric_limits<std::uint32_t>::max()) {
            return issue(AOAHID_ERR_OVERFLOW, "raw.reports",
                         "The descriptor report length exceeds the raw report table's 32-bit "
                         "wire-length field.");
        }
        bool found = false;
        for (std::size_t index = 0U; index < report_count; ++index) {
            const std::uint8_t expected_id = static_cast<std::uint8_t>(id);
            if (reports[index].report_id == expected_id &&
                reports[index].has_report_id ==
                    static_cast<std::uint8_t>(has_report_ids ? 1U : 0U)) {
                found = true;
                if (reports[index].wire_length != expected) {
                    return issue(AOAHID_ERR_PARAM, "raw.reports",
                                 "An accepted raw report length does not match the descriptor.");
                }
                if (trailing_valid_masks != nullptr) {
                    const std::size_t trailing_bits = parsed[id].bits & 7U;
                    trailing_valid_masks[index] =
                        trailing_bits == 0U
                            ? std::uint8_t{0xFFU}
                            : static_cast<std::uint8_t>(
                                  (std::uint32_t{1U} << static_cast<std::uint32_t>(trailing_bits)) -
                                  std::uint32_t{1U});
                }
                break;
            }
        }
        if (!found) {
            return issue(AOAHID_ERR_PARAM, "raw.reports",
                         "The accepted-report table omits a descriptor Input report.");
        }
    }
    if (parsed_count != report_count) {
        return issue(AOAHID_ERR_PARAM, "raw.reports",
                     "The accepted-report table contains a report not defined by the descriptor.");
    }
    if (requirements != nullptr) {
        *requirements = parsed_requirements;
    }
    return {};
}

} // namespace aoa::hid
