#pragma once

#include <cstdint>
#include <format>
#include <map>
#include <vector>

#include <File/File.hpp>

namespace RC::UVTD
{
    struct DumpSettings
    {
        bool should_dump_vtable{};
        bool should_dump_member_vars{};
        bool should_dump_sol_bindings{};
    };

    enum class ValidForVTable
    {
        No = 0,
        Yes = 1
    };
    enum class ValidForMemberVars
    {
        No = 0,
        Yes = 1
    };

    struct SymbolNameInfo
    {
        ValidForVTable valid_for_vtable{};
        ValidForMemberVars valid_for_member_vars{};

        explicit SymbolNameInfo(ValidForVTable valid_for_vtable, ValidForMemberVars valid_for_member_vars)
            : valid_for_vtable(valid_for_vtable), valid_for_member_vars(valid_for_member_vars)
        {
        }
    };

    // Stores type information for a specific UE version
    struct VersionedType
    {
        File::StringType type;
        uint32_t size{};
        int32_t major_version{};
        int32_t minor_version{};

        // Bitfield info for this version
        bool is_bitfield{false};
        uint8_t bit_position{0};
        uint8_t bit_length{0};
    };

    struct MemberVariable
    {
        File::StringType type;
        File::StringType name;
        int32_t offset;
        uint32_t type_index{};
        uint32_t size{};

        // Bitfield information (only valid when is_bitfield is true)
        bool is_bitfield{false};
        uint8_t bit_position{0};  // Bit offset within the storage unit
        uint8_t bit_length{0};    // Number of bits in this bitfield

        // Track types that differ across versions
        // Key is "Major_Minor" (e.g., "4_27", "5_01")
        // Only populated when type changes are detected during join
        std::map<File::StringType, VersionedType> types_by_version{};

        // Check if this member has version-specific type changes
        bool has_type_changes() const { return !types_by_version.empty(); }
    };

    struct FunctionParam
    {
        File::StringType type;

        auto to_string() const -> File::StringType
        {
            return std::format(STR("{}"), type);
        }
    };

    struct MethodQualifiers
    {
        bool is_const = false;
        bool is_volatile = false;
        bool is_lvalue_ref = false; // &
        bool is_rvalue_ref = false; // &&

        auto to_string() const -> File::StringType
        {
            File::StringType result;

            // CV-qualifiers
            if (is_const && is_volatile)
            {
                result = STR("const volatile");
            }
            else if (is_const)
            {
                result = STR("const");
            }
            else if (is_volatile)
            {
                result = STR("volatile");
            }

            // Ref-qualifiers
            if (is_lvalue_ref)
            {
                if (!result.empty()) result += STR(" ");
                result += STR("&");
            }
            else if (is_rvalue_ref)
            {
                if (!result.empty()) result += STR(" ");
                result += STR("&&");
            }

            return result;
        }
    };

    struct MethodSignature
    {
        File::StringType return_type;
        File::StringType name;
        std::vector<FunctionParam> params;
        MethodQualifiers qualifiers;

        auto to_string() const -> File::StringType
        {
            File::StringType params_string{};

            for (size_t i = 0; i < params.size(); i++)
            {
                bool should_add_comma = i < params.size() - 1;
                params_string.append(std::format(STR("{}{}"), params[i].to_string(), should_add_comma ? STR(", ") : STR("")));
            }

            File::StringType qual_string = qualifiers.to_string();
            if (!qual_string.empty())
            {
                qual_string = STR(" ") + qual_string;
            }

            return std::format(STR("{} {}({}){}"), return_type, name, params_string, qual_string);
        }
    };

    struct MethodBody
    {
        File::StringType name;
        MethodSignature signature;
        uint32_t offset;
        bool is_overload;
    };

    struct Class
    {
        File::StringType class_name;
        File::StringType class_name_clean;
        std::map<uint32_t, MethodBody> functions;
        std::vector<MemberVariable> variables;
        uint32_t total_size{};  // Track total class size
        uint32_t last_virtual_offset{};
        uint8_t vtable_entry_size{};
        ValidForVTable valid_for_vtable{ValidForVTable::No};
        ValidForMemberVars valid_for_member_vars{ValidForMemberVars::No};
    };
} // namespace RC::UVTD
