#include <cstdint>

namespace Ambiguous
{
    struct Sample
    {
        int32_t Value;
    };

    struct VTableSample
    {
        virtual void First();
        virtual void Second();
    };
} // namespace Ambiguous

__attribute__((used, noinline)) auto touch_ambiguous_a(Ambiguous::Sample* value) -> unsigned long
{
    return sizeof(Ambiguous::Sample) + (value != nullptr);
}

__attribute__((used, noinline)) auto touch_vtable_ambiguous_a(Ambiguous::VTableSample* value) -> unsigned long
{
    return sizeof(Ambiguous::VTableSample) + (value != nullptr);
}
