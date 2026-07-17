#include <UVTD/DwarfMemberVarsLoader.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Helpers/String.hpp>
#include <UVTD/ConfigUtil.hpp>
#include <UVTD/Helpers.hpp>
#include <UVTD/VTableMethodNames.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>
#include <llvm/DebugInfo/DWARF/DWARFFormValue.h>
#include <llvm/DebugInfo/DWARF/DWARFTypePrinter.h>
#include <llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h>
#include <llvm/Object/Binary.h>
#include <llvm/Object/ObjectFile.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/DataExtractor.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>

namespace RC::UVTD
{
    namespace
    {
        struct ParsedDefinition
        {
            std::string canonical_name;
            uint64_t die_offset{};
            uint32_t total_size{};
            std::vector<MemberVariable> members;
            std::map<uint32_t, MethodBody> functions;
            uint32_t last_virtual_offset{};
            uint8_t vtable_entry_size{};
        };

        struct ParsedVirtualMethod
        {
            std::string source_name;
            std::string linkage_name;
            uint64_t die_offset{};
            uint32_t byte_offset{};
            MethodSignature signature;
            bool destructor{};
        };

        using Definitions = std::unordered_map<std::string, std::vector<ParsedDefinition>>;

        struct Index
        {
            Definitions definitions;
            std::unordered_map<std::string, std::vector<std::string>> aliases;
            std::unordered_map<std::string, size_t> incomplete_counts;
            std::unordered_map<std::string, std::vector<std::string>> unsupported;
        };

        auto narrow(const File::StringType& value) -> std::string
        {
            return to_string(value);
        }

        auto wide(llvm::StringRef value) -> File::StringType
        {
            return to_string_type(value.str().c_str());
        }

        auto naming_die(llvm::DWARFDie die) -> llvm::DWARFDie
        {
            std::unordered_set<uint64_t> seen;
            while (die && !die.find(llvm::dwarf::DW_AT_name))
            {
                if (!seen.emplace(die.getOffset()).second)
                {
                    return {};
                }
                auto referenced = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_specification);
                if (!referenced)
                {
                    referenced = die.getAttributeValueAsReferencedDie(llvm::dwarf::DW_AT_abstract_origin);
                }
                if (!referenced)
                {
                    return {};
                }
                die = referenced;
            }
            return die;
        }

        auto qualified_name(llvm::DWARFDie die) -> std::string
        {
            die = naming_die(die);
            if (!die)
            {
                return {};
            }
            std::string result;
            llvm::raw_string_ostream stream{result};
            llvm::DWARFTypePrinter<llvm::DWARFDie>{stream}.appendQualifiedName(die);
            stream.flush();
            return result;
        }

        auto type_name(llvm::DWARFDie die) -> File::StringType
        {
            std::string result;
            llvm::raw_string_ostream stream{result};
            llvm::DWARFTypePrinter<llvm::DWARFDie>{stream}.appendQualifiedName(die);
            stream.flush();

            const auto first = result.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }
            const auto last = result.find_last_not_of(" \t\r\n");
            return wide(llvm::StringRef{result}.slice(first, last + 1));
        }

        auto clean_name(File::StringType name) -> File::StringType
        {
            std::replace(name.begin(), name.end(), STR(':'), STR('_'));
            std::replace(name.begin(), name.end(), STR('~'), STR('$'));
            return name;
        }

        auto checked_u32(uint64_t value, const std::string& description) -> uint32_t
        {
            if (value > std::numeric_limits<uint32_t>::max())
            {
                throw std::runtime_error{description + " exceeds UVTD's 32-bit size range"};
            }
            return static_cast<uint32_t>(value);
        }

        auto checked_offset(uint64_t value, const std::string& description) -> int32_t
        {
            if (value > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
            {
                throw std::runtime_error{description + " exceeds UVTD's signed 32-bit offset range"};
            }
            return static_cast<int32_t>(value);
        }

        auto recursive_attribute(llvm::DWARFDie die, llvm::dwarf::Attribute attribute) -> std::optional<llvm::DWARFFormValue>
        {
            const llvm::dwarf::Attribute attributes[]{attribute};
            return die.findRecursively(attributes);
        }

        auto referenced_type(llvm::DWARFDie die) -> llvm::DWARFDie
        {
            auto attribute = recursive_attribute(die, llvm::dwarf::DW_AT_type);
            return attribute ? die.getAttributeValueAsReferencedDie(*attribute) : llvm::DWARFDie{};
        }

        auto constant_value(const std::optional<llvm::DWARFFormValue>& value) -> std::optional<uint64_t>
        {
            if (!value)
            {
                return std::nullopt;
            }
            if (auto unsigned_value = value->getAsUnsignedConstant())
            {
                return unsigned_value;
            }
            if (auto signed_value = value->getAsSignedConstant(); signed_value && *signed_value >= 0)
            {
                return static_cast<uint64_t>(*signed_value);
            }
            return std::nullopt;
        }

        auto expression_location(llvm::DWARFDie die, llvm::ArrayRef<uint8_t> block, const std::string& description) -> uint64_t
        {
            auto* unit = die.getDwarfUnit();
            llvm::StringRef bytes{reinterpret_cast<const char*>(block.data()), block.size()};
            llvm::DataExtractor extractor{bytes, unit->isLittleEndian(), unit->getAddressByteSize()};
            llvm::DWARFExpression expression{extractor, unit->getAddressByteSize(), unit->getFormat()};

            std::vector<llvm::DWARFExpression::Operation> operations;
            for (const auto& operation : expression)
            {
                if (operation.isError())
                {
                    throw std::runtime_error{description + " has a malformed DWARF expression"};
                }
                operations.push_back(operation);
            }

            if (operations.size() == 1 && operations[0].getCode() == llvm::dwarf::DW_OP_plus_uconst && operations[0].getNumOperands() == 1)
            {
                return operations[0].getRawOperand(0);
            }
            if (operations.size() == 2 && operations[0].getCode() == llvm::dwarf::DW_OP_constu && operations[0].getNumOperands() == 1 &&
                operations[1].getCode() == llvm::dwarf::DW_OP_plus)
            {
                return operations[0].getRawOperand(0);
            }

            throw std::runtime_error{description + " uses an unsupported dynamic DW_AT_data_member_location expression"};
        }

        auto member_location(llvm::DWARFDie die, bool allow_missing, const std::string& description) -> std::optional<uint64_t>
        {
            auto attribute = recursive_attribute(die, llvm::dwarf::DW_AT_data_member_location);
            if (!attribute)
            {
                if (allow_missing)
                {
                    return 0;
                }
                return std::nullopt;
            }
            if (auto value = constant_value(attribute))
            {
                return value;
            }
            if (auto block = attribute->getAsBlock())
            {
                return expression_location(die, *block, description);
            }
            throw std::runtime_error{description + " has a nonconstant DW_AT_data_member_location"};
        }

        auto parse_member(llvm::DWARFDie member, bool parent_is_union) -> std::optional<MemberVariable>
        {
            if (llvm::dwarf::toUnsigned(recursive_attribute(member, llvm::dwarf::DW_AT_external), 0) != 0 ||
                llvm::dwarf::toUnsigned(recursive_attribute(member, llvm::dwarf::DW_AT_artificial), 0) != 0)
            {
                return std::nullopt;
            }

            const char* raw_name = member.getShortName();
            if (raw_name == nullptr || *raw_name == '\0')
            {
                return std::nullopt;
            }
            const std::string member_name{raw_name};
            const auto description = std::format("member {} at DIE 0x{:x}", member_name, member.getOffset());

            auto type_die = referenced_type(member);
            if (!type_die)
            {
                throw std::runtime_error{description + " has no referenced type"};
            }
            auto type = type_name(type_die);
            if (type.empty())
            {
                throw std::runtime_error{description + " has no printable type"};
            }
            if (ConfigUtil::ShouldFilterType(type, TypeFilterCategory::CompleteExclusion))
            {
                return std::nullopt;
            }

            const auto type_size = type_die.getTypeSize(member.getDwarfUnit()->getAddressByteSize());
            if (!type_size || *type_size == 0)
            {
                throw std::runtime_error{description + " has no constant type size"};
            }
            const auto storage_size = checked_u32(*type_size, description + " type size");

            MemberVariable result{};
            result.type = std::move(type);
            result.name = wide(member_name);
            result.type_index = 0;
            result.size = storage_size;

            const auto bit_size = constant_value(recursive_attribute(member, llvm::dwarf::DW_AT_bit_size));
            if (!bit_size)
            {
                const auto location = member_location(member, parent_is_union, description);
                if (!location)
                {
                    throw std::runtime_error{description + " has no constant DW_AT_data_member_location"};
                }
                result.offset = checked_offset(*location, description + " location");
                return result;
            }

            if (*bit_size == 0 || *bit_size > std::numeric_limits<uint8_t>::max())
            {
                throw std::runtime_error{description + " has an invalid bit size"};
            }
            const uint64_t storage_bits = static_cast<uint64_t>(storage_size) * 8;
            uint64_t byte_offset{};
            uint64_t bit_position{};

            if (const auto data_bit_offset = constant_value(recursive_attribute(member, llvm::dwarf::DW_AT_data_bit_offset)))
            {
                byte_offset = (*data_bit_offset / storage_bits) * storage_size;
                bit_position = *data_bit_offset % storage_bits;
            }
            else
            {
                const auto location = member_location(member, parent_is_union, description);
                const auto legacy_bit_offset = constant_value(recursive_attribute(member, llvm::dwarf::DW_AT_bit_offset));
                if (!location || !legacy_bit_offset)
                {
                    throw std::runtime_error{description + " has no supported DWARF bit offset"};
                }
                byte_offset = *location;
                bit_position = member.getDwarfUnit()->isLittleEndian() ? storage_bits - *legacy_bit_offset - *bit_size : *legacy_bit_offset;
            }

            if (bit_position >= storage_bits || bit_position + *bit_size > storage_bits)
            {
                throw std::runtime_error{description + " has invalid bitfield metadata"};
            }
            result.offset = checked_offset(byte_offset, description + " bitfield location");
            result.is_bitfield = true;
            result.bit_position = static_cast<uint8_t>(bit_position);
            result.bit_length = static_cast<uint8_t>(*bit_size);
            return result;
        }

        auto vtable_slot_index(llvm::DWARFDie method, const std::string& description) -> uint64_t
        {
            auto attribute = recursive_attribute(method, llvm::dwarf::DW_AT_vtable_elem_location);
            if (!attribute)
            {
                throw std::runtime_error{description + " has no DW_AT_vtable_elem_location"};
            }
            if (auto value = constant_value(attribute))
            {
                return *value;
            }
            if (auto block = attribute->getAsBlock())
            {
                auto* unit = method.getDwarfUnit();
                llvm::StringRef bytes{reinterpret_cast<const char*>(block->data()), block->size()};
                llvm::DataExtractor extractor{bytes, unit->isLittleEndian(), unit->getAddressByteSize()};
                llvm::DWARFExpression expression{extractor, unit->getAddressByteSize(), unit->getFormat()};

                std::vector<llvm::DWARFExpression::Operation> operations;
                for (const auto& operation : expression)
                {
                    if (operation.isError())
                    {
                        throw std::runtime_error{description + " has a malformed DW_AT_vtable_elem_location expression"};
                    }
                    operations.push_back(operation);
                }
                if (operations.size() == 1 && operations[0].getNumOperands() == 1 &&
                    (operations[0].getCode() == llvm::dwarf::DW_OP_constu || operations[0].getCode() == llvm::dwarf::DW_OP_consts))
                {
                    return operations[0].getRawOperand(0);
                }
            }
            throw std::runtime_error{description + " uses an unsupported dynamic DW_AT_vtable_elem_location expression"};
        }

        auto apply_object_qualifiers(llvm::DWARFDie type, MethodQualifiers& qualifiers) -> void
        {
            std::unordered_set<uint64_t> seen;
            while (type && seen.emplace(type.getOffset()).second)
            {
                switch (type.getTag())
                {
                case llvm::dwarf::DW_TAG_pointer_type:
                case llvm::dwarf::DW_TAG_reference_type:
                case llvm::dwarf::DW_TAG_rvalue_reference_type:
                case llvm::dwarf::DW_TAG_typedef:
                    type = referenced_type(type);
                    break;
                case llvm::dwarf::DW_TAG_const_type:
                    qualifiers.is_const = true;
                    type = referenced_type(type);
                    break;
                case llvm::dwarf::DW_TAG_volatile_type:
                    qualifiers.is_volatile = true;
                    type = referenced_type(type);
                    break;
                default:
                    return;
                }
            }
        }

        auto parse_virtual_method(llvm::DWARFDie method, const std::string& canonical_name) -> std::optional<ParsedVirtualMethod>
        {
            const auto virtuality = constant_value(recursive_attribute(method, llvm::dwarf::DW_AT_virtuality));
            if (!virtuality || (*virtuality != llvm::dwarf::DW_VIRTUALITY_virtual && *virtuality != llvm::dwarf::DW_VIRTUALITY_pure_virtual))
            {
                return std::nullopt;
            }

            auto source_die = naming_die(method);
            const char* raw_name = source_die ? source_die.getShortName() : nullptr;
            const bool artificial = llvm::dwarf::toUnsigned(recursive_attribute(method, llvm::dwarf::DW_AT_artificial), 0) != 0;
            if (raw_name == nullptr || *raw_name == '\0')
            {
                if (artificial)
                {
                    return std::nullopt;
                }
                throw std::runtime_error{std::format("virtual method at DIE 0x{:x} in {} has no stable source name", method.getOffset(), canonical_name)};
            }

            const std::string source_name{raw_name};
            const auto linkage = llvm::dwarf::toStringRef(recursive_attribute(method, llvm::dwarf::DW_AT_linkage_name));
            const auto description = std::format("virtual method {} ({}) at DIE 0x{:x} in {}",
                                                 source_name,
                                                 linkage.empty() ? std::string{"no linkage name"} : linkage.str(),
                                                 method.getOffset(),
                                                 canonical_name);
            const auto slot_index = vtable_slot_index(method, description);
            const auto address_size = method.getDwarfUnit()->getAddressByteSize();
            if (address_size == 0 || slot_index > std::numeric_limits<uint32_t>::max() / address_size)
            {
                throw std::runtime_error{description + " has a vtable slot outside UVTD's 32-bit offset range"};
            }

            MethodSignature signature{};
            signature.name = wide(source_name);
            if (auto return_type = referenced_type(method))
            {
                signature.return_type = type_name(return_type);
                if (signature.return_type.empty())
                {
                    throw std::runtime_error{description + " has no printable return type"};
                }
            }
            else
            {
                signature.return_type = STR("void");
            }
            signature.qualifiers.is_lvalue_ref = llvm::dwarf::toUnsigned(recursive_attribute(method, llvm::dwarf::DW_AT_reference), 0) != 0;
            signature.qualifiers.is_rvalue_ref = llvm::dwarf::toUnsigned(recursive_attribute(method, llvm::dwarf::DW_AT_rvalue_reference), 0) != 0;

            for (auto parameter : source_die.children())
            {
                if (parameter.getTag() != llvm::dwarf::DW_TAG_formal_parameter)
                {
                    continue;
                }
                auto parameter_type = referenced_type(parameter);
                if (!parameter_type)
                {
                    throw std::runtime_error{description + " has a formal parameter without a referenced type"};
                }
                if (llvm::dwarf::toUnsigned(recursive_attribute(parameter, llvm::dwarf::DW_AT_artificial), 0) != 0)
                {
                    apply_object_qualifiers(parameter_type, signature.qualifiers);
                    continue;
                }
                auto printable_type = type_name(parameter_type);
                if (printable_type.empty())
                {
                    throw std::runtime_error{description + " has a formal parameter without a printable type"};
                }
                signature.params.emplace_back(FunctionParam{.type = std::move(printable_type)});
            }

            return ParsedVirtualMethod{
                    .source_name = source_name,
                    .linkage_name = linkage.str(),
                    .die_offset = method.getOffset(),
                    .byte_offset = static_cast<uint32_t>(slot_index * address_size),
                    .signature = std::move(signature),
                    .destructor = source_name.starts_with('~'),
            };
        }

        auto parse_direct_virtuals(llvm::DWARFDie die, ParsedDefinition& result) -> void
        {
            std::vector<ParsedVirtualMethod> methods;
            std::unordered_set<std::string> identities;
            for (auto child : die.children())
            {
                if (child.getTag() != llvm::dwarf::DW_TAG_subprogram)
                {
                    continue;
                }
                auto method = parse_virtual_method(child, result.canonical_name);
                if (!method)
                {
                    continue;
                }
                const auto identity = std::format("{}@{:x}", method->linkage_name, naming_die(child).getOffset());
                if (identities.emplace(identity).second)
                {
                    methods.emplace_back(std::move(*method));
                }
            }

            std::unordered_map<std::string, size_t> overload_counts;
            for (const auto& method : methods)
            {
                ++overload_counts[method.source_name];
            }

            result.vtable_entry_size = die.getDwarfUnit()->getAddressByteSize();
            for (auto& method : methods)
            {
                const auto reserve_slot = [&result, &method](uint32_t offset) {
                    MethodBody body{
                            .signature = method.signature,
                            .offset = offset,
                    };
                    const auto [_, inserted] = result.functions.emplace(offset, std::move(body));
                    if (!inserted)
                    {
                        throw std::runtime_error{std::format("conflicting virtual methods at byte offset 0x{:x} in {}", offset, result.canonical_name)};
                    }
                    result.last_virtual_offset = std::max(result.last_virtual_offset, offset);
                };

                if (method.destructor)
                {
                    reserve_slot(method.byte_offset);
                    const auto next_offset = checked_u32(static_cast<uint64_t>(method.byte_offset) + result.vtable_entry_size,
                                                         result.canonical_name + " deleting destructor slot");
                    reserve_slot(next_offset);
                    continue;
                }

                MethodBody body{
                        .name = clean_name(wide(method.source_name)),
                        .signature = std::move(method.signature),
                        .offset = method.byte_offset,
                        .is_overload = overload_counts[method.source_name] > 1,
                };
                if (body.is_overload)
                {
                    body.name += generate_mangled_suffix(body.signature);
                }
                const auto [_, inserted] = result.functions.emplace(method.byte_offset, std::move(body));
                if (!inserted)
                {
                    throw std::runtime_error{std::format("conflicting virtual methods at byte offset 0x{:x} in {}", method.byte_offset, result.canonical_name)};
                }
                result.last_virtual_offset = std::max(result.last_virtual_offset, method.byte_offset);
            }
        }

        auto join_strings(const std::vector<std::string>& values) -> std::string;

        auto compilation_unit_identity(llvm::DWARFDie die) -> std::string
        {
            auto unit_die = die.getDwarfUnit()->getUnitDIE(false);
            const auto name = llvm::dwarf::toStringRef(unit_die.find(llvm::dwarf::DW_AT_name));
            const auto directory = llvm::dwarf::toStringRef(unit_die.find(llvm::dwarf::DW_AT_comp_dir));
            if (directory.empty())
            {
                return name.str();
            }
            return std::format("{}/{}", directory.str(), name.str());
        }

        auto build_effective_virtuals(llvm::DWARFDie die,
                                      ParsedDefinition& result,
                                      std::unordered_set<uint64_t>& recursion_stack) -> void
        {
            if (!recursion_stack.emplace(die.getOffset()).second)
            {
                throw std::runtime_error{std::format("vtable inheritance cycle at DIE 0x{:x} in {} (CU '{}')",
                                                     die.getOffset(),
                                                     result.canonical_name,
                                                     compilation_unit_identity(die))};
            }

            struct PolymorphicBase
            {
                std::string canonical_name;
                uint64_t inheritance_die_offset{};
                ParsedDefinition definition;
            };
            std::vector<PolymorphicBase> polymorphic_bases;
            for (auto child : die.children())
            {
                if (child.getTag() != llvm::dwarf::DW_TAG_inheritance)
                {
                    continue;
                }
                const auto inheritance_virtuality = constant_value(recursive_attribute(child, llvm::dwarf::DW_AT_virtuality));
                if (inheritance_virtuality && *inheritance_virtuality != llvm::dwarf::DW_VIRTUALITY_none)
                {
                    throw std::runtime_error{std::format("virtual inheritance at DIE 0x{:x} in {} at DIE 0x{:x} (CU '{}')",
                                                         child.getOffset(),
                                                         result.canonical_name,
                                                         die.getOffset(),
                                                         compilation_unit_identity(die))};
                }

                auto base_die = referenced_type(child);
                if (!base_die)
                {
                    throw std::runtime_error{std::format("inheritance DIE 0x{:x} in {} at DIE 0x{:x} has no referenced base type (CU '{}')",
                                                         child.getOffset(),
                                                         result.canonical_name,
                                                         die.getOffset(),
                                                         compilation_unit_identity(die))};
                }
                const auto base_name = qualified_name(base_die);
                ParsedDefinition base_definition{
                        .canonical_name = base_name.empty() ? std::format("base@0x{:x}", base_die.getOffset()) : base_name,
                        .die_offset = base_die.getOffset(),
                };
                build_effective_virtuals(base_die, base_definition, recursion_stack);
                if (!base_definition.functions.empty())
                {
                    polymorphic_bases.emplace_back(PolymorphicBase{
                            .canonical_name = base_definition.canonical_name,
                            .inheritance_die_offset = child.getOffset(),
                            .definition = std::move(base_definition),
                    });
                }
            }

            if (polymorphic_bases.size() > 1)
            {
                std::vector<std::string> descriptions;
                for (const auto& base : polymorphic_bases)
                {
                    descriptions.emplace_back(std::format("{} at inheritance DIE 0x{:x}", base.canonical_name, base.inheritance_die_offset));
                }
                throw std::runtime_error{std::format("{} at DIE 0x{:x} has multiple polymorphic direct bases: {} (CU '{}')",
                                                     result.canonical_name,
                                                     die.getOffset(),
                                                     join_strings(descriptions),
                                                     compilation_unit_identity(die))};
            }

            if (!polymorphic_bases.empty())
            {
                auto& primary = polymorphic_bases.front().definition;
                result.functions = std::move(primary.functions);
                result.last_virtual_offset = primary.last_virtual_offset;
                result.vtable_entry_size = primary.vtable_entry_size;
            }

            ParsedDefinition direct{
                    .canonical_name = result.canonical_name,
                    .die_offset = result.die_offset,
            };
            parse_direct_virtuals(die, direct);
            if (result.vtable_entry_size != 0 && direct.vtable_entry_size != 0 && result.vtable_entry_size != direct.vtable_entry_size)
            {
                throw std::runtime_error{std::format("vtable entry width changed from {} to {} in {} at DIE 0x{:x} (CU '{}')",
                                                     result.vtable_entry_size,
                                                     direct.vtable_entry_size,
                                                     result.canonical_name,
                                                     die.getOffset(),
                                                     compilation_unit_identity(die))};
            }
            if (result.vtable_entry_size == 0)
            {
                result.vtable_entry_size = direct.vtable_entry_size;
            }

            for (auto& [offset, method] : direct.functions)
            {
                const auto inherited = result.functions.find(offset);
                if (inherited == result.functions.end())
                {
                    result.functions.emplace(offset, std::move(method));
                }
                else
                {
                    const bool destructor_override = inherited->second.name.empty() && method.name.empty();
                    const bool named_override = !inherited->second.name.empty() && !method.name.empty() &&
                                                inherited->second.signature.name == method.signature.name;
                    if (!destructor_override && !named_override)
                    {
                        throw std::runtime_error{std::format("conflicting primary vtable slot 0x{:x} in {} at DIE 0x{:x}: inherited '{}' versus direct '{}' (CU '{}')",
                                                             offset,
                                                             result.canonical_name,
                                                             die.getOffset(),
                                                             to_string(inherited->second.signature.name),
                                                             to_string(method.signature.name),
                                                             compilation_unit_identity(die))};
                    }
                    inherited->second = std::move(method);
                }
                result.last_virtual_offset = std::max(result.last_virtual_offset, offset);
            }
            recursion_stack.erase(die.getOffset());
        }

        auto parse_definition(llvm::DWARFDie die, const std::string& canonical_name, DwarfLoadOptions options) -> ParsedDefinition
        {
            const auto byte_size = constant_value(recursive_attribute(die, llvm::dwarf::DW_AT_byte_size));
            if (!byte_size)
            {
                throw std::runtime_error{canonical_name + " has no DW_AT_byte_size"};
            }

            ParsedDefinition result{
                    .canonical_name = canonical_name,
                    .die_offset = die.getOffset(),
                    .total_size = checked_u32(*byte_size, canonical_name + " size"),
            };
            if (options.members)
            {
                const bool is_union = die.getTag() == llvm::dwarf::DW_TAG_union_type;
                for (auto child : die.children())
                {
                    if (child.getTag() != llvm::dwarf::DW_TAG_member)
                    {
                        continue;
                    }
                    if (auto member = parse_member(child, is_union))
                    {
                        result.members.emplace_back(std::move(*member));
                    }
                }
            }
            if (options.vtables)
            {
                std::unordered_set<uint64_t> recursion_stack;
                build_effective_virtuals(die, result, recursion_stack);
            }
            return result;
        }

        auto is_definition_tag(llvm::dwarf::Tag tag) -> bool
        {
            return tag == llvm::dwarf::DW_TAG_class_type || tag == llvm::dwarf::DW_TAG_structure_type || tag == llvm::dwarf::DW_TAG_union_type;
        }

        auto add_unique_alias(Index& index, std::string name, std::string target) -> void
        {
            auto& targets = index.aliases[std::move(name)];
            if (std::find(targets.begin(), targets.end(), target) == targets.end())
            {
                targets.emplace_back(std::move(target));
            }
        }

        auto index_die_tree(llvm::DWARFDie die, Index& index, DwarfLoadOptions options) -> void
        {
            const auto tag = die.getTag();
            if (is_definition_tag(tag))
            {
                const auto name = qualified_name(die);
                if (!name.empty())
                {
                    const bool declaration = llvm::dwarf::toUnsigned(die.find(llvm::dwarf::DW_AT_declaration), 0) != 0;
                    if (declaration || !recursive_attribute(die, llvm::dwarf::DW_AT_byte_size))
                    {
                        ++index.incomplete_counts[name];
                    }
                    else
                    {
                        try
                        {
                            index.definitions[name].emplace_back(parse_definition(die, name, options));
                        }
                        catch (const std::exception& error)
                        {
                            index.unsupported[name].emplace_back(error.what());
                        }
                    }
                }
            }
            else if (tag == llvm::dwarf::DW_TAG_typedef)
            {
                const auto name = qualified_name(die);
                const auto target_die = referenced_type(die);
                const auto target = target_die ? qualified_name(target_die) : std::string{};
                if (!name.empty() && !target.empty())
                {
                    add_unique_alias(index, name, target);
                }
            }

            for (auto child : die.children())
            {
                index_die_tree(child, index, options);
            }
        }

        auto process_unit(llvm::DWARFUnit& unit, Index& index, DwarfLoadOptions options) -> void
        {
            auto unit_die = unit.getUnitDIE(false);
            if (unit_die)
            {
                index_die_tree(unit_die, index, options);
            }
            unit.clear();
        }

        auto resolve_alias(const std::string& name, const Index& index, std::unordered_set<std::string>& seen) -> std::vector<std::string>
        {
            if (!seen.emplace(name).second)
            {
                throw std::runtime_error{"DWARF typedef cycle involving " + name};
            }
            const auto alias_it = index.aliases.find(name);
            if (alias_it == index.aliases.end() || alias_it->second.empty())
            {
                seen.erase(name);
                return {name};
            }

            std::vector<std::string> result;
            for (const auto& target : alias_it->second)
            {
                for (auto resolved : resolve_alias(target, index, seen))
                {
                    if (std::find(result.begin(), result.end(), resolved) == result.end())
                    {
                        result.emplace_back(std::move(resolved));
                    }
                }
            }
            seen.erase(name);
            return result;
        }

        auto resolve_alias(const std::string& name, const Index& index) -> std::vector<std::string>
        {
            std::unordered_set<std::string> seen;
            return resolve_alias(name, index, seen);
        }

        auto request_has_definition(const DwarfTypeRequest& request, const Index& index) -> bool
        {
            for (const auto& metadata_name : request.metadata_names)
            {
                try
                {
                    for (const auto& resolved : resolve_alias(narrow(metadata_name), index))
                    {
                        if (const auto it = index.definitions.find(resolved); it != index.definitions.end() && !it->second.empty())
                        {
                            return true;
                        }
                    }
                }
                catch (const std::exception&)
                {
                }
            }
            return false;
        }

        auto all_requests_have_definitions(const std::vector<DwarfTypeRequest>& requests, const Index& index) -> bool
        {
            return std::all_of(requests.begin(), requests.end(), [&index](const auto& request) {
                return request_has_definition(request, index);
            });
        }

        auto same_member(const MemberVariable& left, const MemberVariable& right) -> bool
        {
            return left.type == right.type && left.name == right.name && left.offset == right.offset && left.size == right.size &&
                   left.is_bitfield == right.is_bitfield && left.bit_position == right.bit_position && left.bit_length == right.bit_length;
        }

        auto same_signature(const MethodSignature& left, const MethodSignature& right) -> bool
        {
            if (left.return_type != right.return_type || left.name != right.name || left.qualifiers.to_string() != right.qualifiers.to_string() ||
                left.params.size() != right.params.size())
            {
                return false;
            }
            return std::equal(left.params.begin(), left.params.end(), right.params.begin(), [](const auto& left_param, const auto& right_param) {
                return left_param.type == right_param.type;
            });
        }

        auto same_function(const std::pair<const uint32_t, MethodBody>& left, const std::pair<const uint32_t, MethodBody>& right) -> bool
        {
            return left.first == right.first && left.second.name == right.second.name && left.second.offset == right.second.offset &&
                   same_signature(left.second.signature, right.second.signature);
        }

        auto same_definition(const ParsedDefinition& left, const ParsedDefinition& right, DwarfLoadOptions options) -> bool
        {
            if (options.members &&
                (left.total_size != right.total_size || left.members.size() != right.members.size() ||
                 !std::equal(left.members.begin(), left.members.end(), right.members.begin(), same_member)))
            {
                return false;
            }
            return !options.vtables ||
                   (left.vtable_entry_size == right.vtable_entry_size && left.last_virtual_offset == right.last_virtual_offset &&
                    left.functions.size() == right.functions.size() &&
                    std::equal(left.functions.begin(), left.functions.end(), right.functions.begin(), same_function));
        }

        auto join_strings(const std::vector<std::string>& values) -> std::string
        {
            std::string result;
            for (size_t index = 0; index < values.size(); ++index)
            {
                if (index != 0)
                {
                    result += ", ";
                }
                result += values[index];
            }
            return result;
        }

        auto leaf_name(const std::string& name) -> std::string
        {
            const auto separator = name.rfind("::");
            return separator == std::string::npos ? name : name.substr(separator + 2);
        }

        auto nearby_names(const DwarfTypeRequest& request, const Index& index) -> std::vector<std::string>
        {
            std::vector<std::string> requested_leaves;
            for (const auto& name : request.metadata_names)
            {
                requested_leaves.emplace_back(leaf_name(narrow(name)));
            }

            std::vector<std::string> result;
            const auto add_if_nearby = [&requested_leaves, &result](const std::string& name) {
                if (std::find(requested_leaves.begin(), requested_leaves.end(), leaf_name(name)) != requested_leaves.end())
                {
                    result.emplace_back(name);
                }
            };
            for (const auto& [name, _] : index.definitions)
            {
                add_if_nearby(name);
            }
            for (const auto& [name, _] : index.incomplete_counts)
            {
                add_if_nearby(name);
            }
            for (const auto& [name, _] : index.aliases)
            {
                add_if_nearby(name);
            }
            std::sort(result.begin(), result.end());
            result.erase(std::unique(result.begin(), result.end()), result.end());
            if (result.size() > 8)
            {
                result.resize(8);
            }
            return result;
        }

        auto select_definition(const DwarfTypeRequest& request,
                               const Index& index,
                               const std::filesystem::path& input,
                               DwarfLoadOptions options) -> const ParsedDefinition&
        {
            std::vector<std::string> resolved_names;
            std::vector<std::string> alias_errors;
            for (const auto& metadata_name : request.metadata_names)
            {
                const auto name = narrow(metadata_name);
                try
                {
                    for (auto resolved : resolve_alias(name, index))
                    {
                        if (std::find(resolved_names.begin(), resolved_names.end(), resolved) == resolved_names.end())
                        {
                            resolved_names.emplace_back(std::move(resolved));
                        }
                    }
                }
                catch (const std::exception& error)
                {
                    alias_errors.emplace_back(error.what());
                }
            }

            std::vector<const ParsedDefinition*> candidates;
            for (const auto& name : resolved_names)
            {
                if (const auto it = index.definitions.find(name); it != index.definitions.end())
                {
                    for (const auto& definition : it->second)
                    {
                        candidates.emplace_back(&definition);
                    }
                }
            }

            const auto configured_name = narrow(request.configured_name);
            const auto input_text = input.string();
            if (candidates.empty())
            {
                std::vector<std::string> diagnostics = std::move(alias_errors);
                for (const auto& name : resolved_names)
                {
                    if (const auto it = index.unsupported.find(name); it != index.unsupported.end())
                    {
                        diagnostics.insert(diagnostics.end(), it->second.begin(), it->second.end());
                    }
                }
                if (!diagnostics.empty())
                {
                    throw std::runtime_error{std::format("unsupported DWARF layout for '{}' in '{}': {}", configured_name, input_text, join_strings(diagnostics))};
                }

                size_t incomplete_count{};
                for (const auto& name : resolved_names)
                {
                    if (const auto it = index.incomplete_counts.find(name); it != index.incomplete_counts.end())
                    {
                        incomplete_count += it->second;
                    }
                }
                if (incomplete_count != 0)
                {
                    throw std::runtime_error{std::format("incomplete DWARF type '{}' in '{}' ({} declarations)", configured_name, input_text, incomplete_count)};
                }
                const auto nearby = nearby_names(request, index);
                throw std::runtime_error{std::format("missing DWARF type '{}' in '{}' (metadata names: {}; nearby indexed names: {})",
                                                     configured_name,
                                                     input_text,
                                                     join_strings(resolved_names),
                                                     nearby.empty() ? std::string{"none"} : join_strings(nearby))};
            }

            std::vector<const ParsedDefinition*> distinct;
            for (const auto* candidate : candidates)
            {
                if (std::none_of(distinct.begin(), distinct.end(), [candidate, options](const auto* other) {
                        return same_definition(*candidate, *other, options);
                    }))
                {
                    distinct.emplace_back(candidate);
                }
            }
            if (distinct.size() != 1)
            {
                std::vector<std::string> descriptions;
                descriptions.reserve(candidates.size());
                for (const auto* candidate : candidates)
                {
                    descriptions.emplace_back(std::format("{} at DIE 0x{:x}", candidate->canonical_name, candidate->die_offset));
                }
                throw std::runtime_error{std::format("ambiguous DWARF type '{}' in '{}': {} distinct layouts from {}",
                                                     configured_name,
                                                     input_text,
                                                     distinct.size(),
                                                     join_strings(descriptions))};
            }

            return **std::min_element(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
                return left->die_offset < right->die_offset;
            });
        }
    } // namespace

    DwarfMemberVarsLoader::DwarfMemberVarsLoader(std::filesystem::path input,
                                                 std::vector<DwarfTypeRequest> requests,
                                                 DwarfLoadOptions options)
        : m_input(std::move(input)), m_requests(std::move(requests)), m_options(options)
    {
    }

    auto DwarfMemberVarsLoader::load() const -> TypeContainer
    {
        if (m_requests.empty())
        {
            throw std::runtime_error{"no DWARF types were requested"};
        }
        if (!m_options.members && !m_options.vtables)
        {
            throw std::runtime_error{"no DWARF metadata operations were requested"};
        }

        auto binary_or_error = llvm::object::createBinary(m_input.string());
        if (!binary_or_error)
        {
            throw std::runtime_error{std::format("failed to open DWARF input '{}': {}", m_input.string(), llvm::toString(binary_or_error.takeError()))};
        }
        auto binary = std::move(*binary_or_error);
        auto* object = llvm::dyn_cast<llvm::object::ObjectFile>(binary.getBinary());
        if (object == nullptr || !object->isELF())
        {
            throw std::runtime_error{std::format("DWARF input '{}' is not an ELF object", m_input.string())};
        }

        auto context = llvm::DWARFContext::create(*object);
        if (context->getNumCompileUnits() == 0)
        {
            throw std::runtime_error{std::format("ELF input '{}' contains no DWARF compilation units", m_input.string())};
        }

        std::vector<llvm::DWARFUnit*> preferred_units;
        std::vector<llvm::DWARFUnit*> remaining_units;
        for (const auto& unit_ptr : context->normal_units())
        {
            auto* unit = unit_ptr.get();
            auto unit_die = unit->getUnitDIE(true);
            const auto source_name = llvm::dwarf::toStringRef(unit_die.find(llvm::dwarf::DW_AT_name));
            if (source_name.ends_with("LayoutProbeTypeAnchors.cpp"))
            {
                preferred_units.emplace_back(unit);
            }
            else
            {
                remaining_units.emplace_back(unit);
            }
        }

        Index index;
        for (auto* unit : preferred_units)
        {
            process_unit(*unit, index, m_options);
        }
        if (!all_requests_have_definitions(m_requests, index))
        {
            for (auto* unit : remaining_units)
            {
                process_unit(*unit, index, m_options);
            }
        }

        TypeContainer container;
        for (const auto& request : m_requests)
        {
            const auto& definition = select_definition(request, index, m_input, m_options);
            auto class_name_clean = clean_name(request.configured_name);
            auto& class_entry = container.get_or_create_class_entry(request.configured_name, class_name_clean, request.name_info);
            class_entry.class_name = request.configured_name;
            class_entry.class_name_clean = std::move(class_name_clean);
            class_entry.total_size = definition.total_size;
            if (m_options.members)
            {
                class_entry.variables = definition.members;
            }
            if (m_options.vtables)
            {
                class_entry.functions = definition.functions;
                class_entry.last_virtual_offset = definition.last_virtual_offset;
                class_entry.vtable_entry_size = definition.vtable_entry_size;
            }
        }
        return container;
    }

    auto DwarfMemberVarsLoader::requests_from_config(DwarfLoadOptions options) -> std::vector<DwarfTypeRequest>
    {
        std::vector<DwarfTypeRequest> requests;
        std::unordered_set<File::StringType> seen;
        for (const auto& item : ConfigUtil::GetObjectItems())
        {
            const bool wants_members = options.members && item.valid_for_member_vars == ValidForMemberVars::Yes;
            const bool wants_vtable = options.vtables && item.valid_for_vtable == ValidForVTable::Yes;
            if ((!wants_members && !wants_vtable) || !seen.emplace(item.name).second)
            {
                continue;
            }

            std::vector<File::StringType> metadata_names{item.name};
            if (item.name == STR("TUObjectArray"))
            {
                metadata_names = {
                        STR("TUObjectArray"),
                        STR("FFixedUObjectArray"),
                        STR("FChunkedFixedUObjectArray"),
                };
            }
            requests.emplace_back(DwarfTypeRequest{
                    .configured_name = item.name,
                    .metadata_names = std::move(metadata_names),
                    .name_info = SymbolNameInfo{item.valid_for_vtable, item.valid_for_member_vars},
            });
        }
        return requests;
    }
} // namespace RC::UVTD
