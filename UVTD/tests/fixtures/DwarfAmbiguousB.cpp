#include <cstdint>

namespace Ambiguous
{
    struct Sample
    {
        int64_t Value;
    };

    struct VTableSample
    {
        virtual void Second();
        virtual void First();
    };
} // namespace Ambiguous

__attribute__((used, noinline)) auto touch_ambiguous_b(Ambiguous::Sample* value) -> unsigned long
{
    return sizeof(Ambiguous::Sample) + (value != nullptr);
}

__attribute__((used, noinline)) auto touch_vtable_ambiguous_b(Ambiguous::VTableSample* value) -> unsigned long
{
    return sizeof(Ambiguous::VTableSample) + (value != nullptr);
}
