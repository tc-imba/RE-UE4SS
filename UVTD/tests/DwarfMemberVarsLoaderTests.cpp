#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <UVTD/Config.hpp>
#include <UVTD/DwarfMemberVarsLoader.hpp>

namespace
{
    using namespace RC::UVTD;

    auto request(RC::File::StringType configured_name, RC::File::StringType metadata_name) -> DwarfTypeRequest
    {
        return {
                .configured_name = std::move(configured_name),
                .metadata_names = {std::move(metadata_name)},
                .name_info = SymbolNameInfo{ValidForVTable::No, ValidForMemberVars::Yes},
        };
    }

    auto vtable_request(RC::File::StringType configured_name, RC::File::StringType metadata_name) -> DwarfTypeRequest
    {
        return {
                .configured_name = std::move(configured_name),
                .metadata_names = {std::move(metadata_name)},
                .name_info = SymbolNameInfo{ValidForVTable::Yes, ValidForMemberVars::No},
        };
    }

    auto fail(const char* message) -> int
    {
        std::fprintf(stderr, "%s\n", message);
        return 1;
    }

    auto expect_failure(const std::filesystem::path& fixture, DwarfTypeRequest type_request, const std::string& expected_text) -> bool
    {
        try
        {
            DwarfMemberVarsLoader{fixture, {std::move(type_request)}}.load();
        }
        catch (const std::exception& error)
        {
            return std::string{error.what()}.find(expected_text) != std::string::npos;
        }
        return false;
    }

    auto failure_text(const std::filesystem::path& fixture, std::vector<DwarfTypeRequest> requests, DwarfLoadOptions options) -> std::string
    {
        try
        {
            DwarfMemberVarsLoader{fixture, std::move(requests), options}.load();
        }
        catch (const std::exception& error)
        {
            return error.what();
        }
        return {};
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace RC::UVTD;

    if (argc != 3)
    {
        return fail("expected the C++ and special-case DWARF fixture paths");
    }

    const std::filesystem::path fixture{argv[1]};
    auto container = DwarfMemberVarsLoader{
            fixture,
            {
                    request(STR("SampleConfigured"), STR("Fixture::Sample")),
                    request(STR("SampleAliasConfigured"), STR("Fixture::SampleAlias")),
            }}.load();

    const auto& classes = container.get_class_entries();
    const auto sample_it = classes.find(STR("SampleConfigured"));
    const auto alias_it = classes.find(STR("SampleAliasConfigured"));
    if (sample_it == classes.end() || alias_it == classes.end())
    {
        return fail("direct and alias requests were not both resolved");
    }

    const auto& sample = sample_it->second;
    const auto& alias = alias_it->second;
    if (sample.class_name != STR("SampleConfigured") || alias.class_name != STR("SampleAliasConfigured"))
    {
        return fail("configured output names were not preserved");
    }
    if (sample.total_size != 24 || alias.total_size != sample.total_size)
    {
        return fail("class size was not extracted through the alias");
    }
    if (sample.variables.size() != 4 || alias.variables.size() != 4)
    {
        return fail("direct members were not extracted without flattening the base");
    }

    const std::vector<RC::File::StringType> expected_names{STR("Value"), STR("Flags"), STR("OtherFlags"), STR("Pointer")};
    const std::vector<int32_t> expected_offsets{4, 8, 8, 16};
    for (size_t index = 0; index < expected_names.size(); ++index)
    {
        const auto& member = sample.variables[index];
        if (member.name != expected_names[index] || member.offset != expected_offsets[index])
        {
            return fail("member declaration order or offset is incorrect");
        }
        if (member.type.empty() || member.size == 0 || member.type_index != 0)
        {
            return fail("member type metadata is incomplete");
        }
        if (alias.variables[index].name != member.name || alias.variables[index].offset != member.offset || alias.variables[index].size != member.size)
        {
            return fail("alias layout differs from the direct layout");
        }
    }

    if (!sample.variables[1].is_bitfield || sample.variables[1].bit_position != 0 || sample.variables[1].bit_length != 3 || sample.variables[1].size != 4)
    {
        return fail("first bitfield metadata is incorrect");
    }
    if (!sample.variables[2].is_bitfield || sample.variables[2].bit_position != 3 || sample.variables[2].bit_length != 5 || sample.variables[2].size != 4)
    {
        return fail("second bitfield metadata is incorrect");
    }

    if (!expect_failure(fixture, request(STR("Missing"), STR("Fixture::Missing")), "missing"))
    {
        return fail("missing type did not fail with a useful diagnostic");
    }
    if (!expect_failure(fixture, request(STR("Incomplete"), STR("Fixture::Incomplete")), "incomplete"))
    {
        return fail("incomplete type did not fail with a useful diagnostic");
    }
    if (!expect_failure(fixture, request(STR("Ambiguous"), STR("Ambiguous::Sample")), "ambiguous"))
    {
        return fail("conflicting definitions did not fail as ambiguous");
    }

    const auto vtable_container = DwarfMemberVarsLoader{
            fixture,
            {vtable_request(STR("PrimaryBaseConfigured"), STR("Fixture::PrimaryBase"))},
            DwarfLoadOptions{.members = false, .vtables = true}}
                                          .load();
    const auto primary_it = vtable_container.get_class_entries().find(STR("PrimaryBaseConfigured"));
    if (primary_it == vtable_container.get_class_entries().end())
    {
        return fail("vtable-only request was not loaded");
    }
    const auto& primary = primary_it->second;
    if (primary.vtable_entry_size != sizeof(void*) || primary.functions.size() < 7)
    {
        return fail("primary vtable slot metadata is incomplete");
    }

    bool has_compute = false;
    bool has_const_overload = false;
    bool has_volatile_overload = false;
    bool has_lref = false;
    bool has_rref = false;
    size_t reserved_destructor_slots = 0;
    for (const auto& [offset, function] : primary.functions)
    {
        if (offset % primary.vtable_entry_size != 0 || function.offset != offset)
        {
            return fail("DWARF vtable slot was not converted to a byte offset");
        }
        if (function.name.empty())
        {
            ++reserved_destructor_slots;
            continue;
        }
        if (function.signature.name == STR("Compute")) has_compute = function.signature.params.size() == 1 && function.signature.params[0].type == STR("int");
        if (function.name == STR("Overload_C__int")) has_const_overload = function.signature.qualifiers.is_const;
        if (function.name == STR("Overload_V__float")) has_volatile_overload = function.signature.qualifiers.is_volatile;
        if (function.signature.name == STR("LRef")) has_lref = function.signature.qualifiers.is_lvalue_ref;
        if (function.signature.name == STR("RRef")) has_rref = function.signature.qualifiers.is_rvalue_ref;
    }
    if (!has_compute || !has_const_overload || !has_volatile_overload || !has_lref || !has_rref || reserved_destructor_slots != 2)
    {
        return fail("virtual method names, signatures, qualifiers, or destructor reservations are incorrect");
    }

    auto combined_request = vtable_request(STR("PrimaryCombined"), STR("Fixture::PrimaryBase"));
    combined_request.name_info = SymbolNameInfo{ValidForVTable::Yes, ValidForMemberVars::Yes};
    const auto combined_container = DwarfMemberVarsLoader{
            fixture,
            {std::move(combined_request)},
            DwarfLoadOptions{.members = true, .vtables = true}}
                                            .load();
    const auto& combined_primary = combined_container.get_class_entries().at(STR("PrimaryCombined"));
    if (combined_primary.variables.size() != 1 || combined_primary.variables[0].name != STR("BaseMember") || combined_primary.functions.empty())
    {
        return fail("combined metadata included an artificial vptr or omitted requested vtable data");
    }

    const auto inherited_container = DwarfMemberVarsLoader{
            fixture,
            {
                    vtable_request(STR("PrimaryBaseConfigured"), STR("Fixture::PrimaryBase")),
                    vtable_request(STR("DerivedConfigured"), STR("Fixture::Derived")),
            },
            DwarfLoadOptions{.members = false, .vtables = true}}
                                             .load();
    const auto& inherited_classes = inherited_container.get_class_entries();
    const auto& inherited_base = inherited_classes.at(STR("PrimaryBaseConfigured"));
    const auto& derived = inherited_classes.at(STR("DerivedConfigured"));
    const auto base_compute = std::find_if(inherited_base.functions.begin(), inherited_base.functions.end(), [](const auto& entry) {
        return entry.second.signature.name == STR("Compute");
    });
    if (base_compute == inherited_base.functions.end())
    {
        return fail("base Compute slot is missing");
    }
    const auto derived_compute = derived.functions.find(base_compute->first);
    if (derived_compute == derived.functions.end() || derived_compute->second.signature.name != STR("Compute"))
    {
        return fail("derived override did not replace the inherited primary slot");
    }
    if (!derived.functions.contains(0x18) || !derived.functions.contains(0x20) || derived.last_virtual_offset <= inherited_base.last_virtual_offset)
    {
        return fail("primary slots were not inherited or the new virtual was not appended");
    }
    const auto added = std::find_if(derived.functions.begin(), derived.functions.end(), [](const auto& entry) {
        return entry.second.signature.name == STR("Added");
    });
    if (added == derived.functions.end() || added->first <= inherited_base.last_virtual_offset)
    {
        return fail("new derived virtual method has no primary slot");
    }

    const auto multiple_error = failure_text(
            fixture,
            {vtable_request(STR("MultipleConfigured"), STR("Fixture::MultiplePolymorphic"))},
            DwarfLoadOptions{.members = false, .vtables = true});
    if (multiple_error.find("multiple polymorphic direct bases") == std::string::npos || multiple_error.find("MultipleConfigured") == std::string::npos ||
        multiple_error.find("Fixture::MultiplePolymorphic") == std::string::npos || multiple_error.find("DIE 0x") == std::string::npos ||
        multiple_error.find(fixture.string()) == std::string::npos)
    {
        return fail("multiple polymorphic base rejection diagnostic is incomplete");
    }

    const auto virtual_error = failure_text(
            fixture,
            {vtable_request(STR("VirtualConfigured"), STR("Fixture::VirtualInheritance"))},
            DwarfLoadOptions{.members = false, .vtables = true});
    if (virtual_error.find("virtual inheritance") == std::string::npos || virtual_error.find("VirtualConfigured") == std::string::npos ||
        virtual_error.find("Fixture::VirtualInheritance") == std::string::npos || virtual_error.find("DIE 0x") == std::string::npos ||
        virtual_error.find(fixture.string()) == std::string::npos)
    {
        return fail("virtual inheritance rejection diagnostic is incomplete");
    }

    const auto mixed_error = failure_text(
            fixture,
            {
                    vtable_request(STR("DerivedConfigured"), STR("Fixture::Derived")),
                    vtable_request(STR("MultipleConfigured"), STR("Fixture::MultiplePolymorphic")),
            },
            DwarfLoadOptions{.members = false, .vtables = true});
    if (mixed_error.empty())
    {
        return fail("mixed valid and unsupported requests returned a partial container");
    }

    const auto ambiguous_vtable_error = failure_text(
            fixture,
            {vtable_request(STR("AmbiguousVTable"), STR("Ambiguous::VTableSample"))},
            DwarfLoadOptions{.members = false, .vtables = true});
    if (ambiguous_vtable_error.find("ambiguous") == std::string::npos)
    {
        return fail("conflicting vtable definitions did not fail as ambiguous");
    }

    const std::filesystem::path special_fixture{argv[2]};
    const auto specified = DwarfMemberVarsLoader{special_fixture, {request(STR("SpecifiedConfigured"), STR("SpecNamespace::Specified"))}}.load();
    const auto specified_it = specified.get_class_entries().find(STR("SpecifiedConfigured"));
    if (specified_it == specified.get_class_entries().end() || specified_it->second.total_size != 4 || specified_it->second.variables.size() != 1 ||
        specified_it->second.variables[0].name != STR("Value") || specified_it->second.variables[0].offset != 0)
    {
        return fail("DW_AT_specification class metadata was not resolved");
    }
    if (!expect_failure(special_fixture, request(STR("Dynamic"), STR("DynamicLocation")), "unsupported dynamic"))
    {
        return fail("dynamic member location did not fail explicitly");
    }

    auto& config = UVTDConfig::Get();
    config.object_items = {
            {STR("Sample"), ValidForVTable::No, ValidForMemberVars::Yes},
            {STR("VTableOnly"), ValidForVTable::Yes, ValidForMemberVars::No},
            {STR("TUObjectArray"), ValidForVTable::No, ValidForMemberVars::Yes},
    };
    const auto requests = DwarfMemberVarsLoader::requests_from_config();
    if (requests.size() != 2 || requests[0].configured_name != STR("Sample") || requests[1].configured_name != STR("TUObjectArray") ||
        requests[1].metadata_names.size() != 3)
    {
        return fail("configuration was not converted to DWARF member-layout requests");
    }
    const auto vtable_requests = DwarfMemberVarsLoader::requests_from_config(DwarfLoadOptions{.members = false, .vtables = true});
    if (vtable_requests.size() != 1 || vtable_requests[0].configured_name != STR("VTableOnly"))
    {
        return fail("configuration was not converted to DWARF vtable requests");
    }

    return 0;
}
