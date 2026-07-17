#include <UVTD/Config.hpp>
#include <UVTD/TypeContainer.hpp>
#include <UVTD/TypeMetadata.hpp>

int main()
{
    RC::UVTD::MemberVariable member{};
    member.name = STR("Value");
    member.offset = 4;

    RC::UVTD::Class type{};
    type.class_name = STR("Sample");
    type.variables.emplace_back(member);

    RC::UVTD::TypeContainer container;
    return type.variables.front().offset == 4 && container.get_class_entries().empty() ? 0 : 1;
}
