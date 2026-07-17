#include <cstdio>

#include <UVTD/VTableMethodNames.hpp>

namespace
{
    auto fail(const char* message) -> int
    {
        std::fprintf(stderr, "%s\n", message);
        return 1;
    }
}

int main()
{
    using namespace RC::UVTD;

    if (sanitize_type_for_identifier(STR("const volatile Foo::Bar*&")) != STR("Ptr_Ref_CV_Foo_Bar"))
    {
        return fail("cv/ref sanitization changed");
    }

    MethodSignature signature{
            .params = {{STR("int")}, {STR("const Foo*")}},
            .qualifiers = {.is_const = true, .is_lvalue_ref = true},
    };
    if (generate_mangled_suffix(signature) != STR("_CL__int__Ptr_C_Foo"))
    {
        return fail("overload suffix changed");
    }

    if (generate_mangled_suffix(MethodSignature{}) != STR(""))
    {
        return fail("empty signature gained a suffix");
    }
    return 0;
}
