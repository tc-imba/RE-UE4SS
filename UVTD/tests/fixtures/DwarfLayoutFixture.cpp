#include <cstdint>

namespace Fixture
{
    struct Base
    {
        int32_t BaseValue;
    };

    struct Sample : Base
    {
        int32_t Value;
        uint32_t Flags : 3;
        uint32_t OtherFlags : 5;
        void* Pointer;
    };

    using SampleAlias = Sample;

    struct Incomplete;

    struct PrimaryBase
    {
        int32_t BaseMember;

        virtual ~PrimaryBase();
        virtual int Compute(int) = 0;
        virtual int Overload(int) const;
        virtual int Overload(float) volatile;
        virtual int LRef() &;
        virtual int RRef() &&;
    };

    PrimaryBase::~PrimaryBase() = default;

    int PrimaryBase::Overload(int value) const
    {
        return value;
    }

    int PrimaryBase::Overload(float value) volatile
    {
        return static_cast<int>(value);
    }

    int PrimaryBase::LRef() &
    {
        return BaseMember;
    }

    int PrimaryBase::RRef() &&
    {
        return BaseMember;
    }

    struct NonPolymorphicMixin
    {
        int32_t MixinMember;
    };

    struct Derived : PrimaryBase, NonPolymorphicMixin
    {
        ~Derived() override;
        int Compute(int) override;
        virtual void Added(char*);
    };

    Derived::~Derived() = default;

    int Derived::Compute(int value)
    {
        return value + BaseMember;
    }

    void Derived::Added(char*)
    {
    }

    struct LeftPolymorphic
    {
        virtual ~LeftPolymorphic();
        virtual int LeftOnly();
    };

    LeftPolymorphic::~LeftPolymorphic() = default;

    int LeftPolymorphic::LeftOnly()
    {
        return 1;
    }

    struct RightPolymorphic
    {
        virtual ~RightPolymorphic();
        virtual int RightOnly();
    };

    RightPolymorphic::~RightPolymorphic() = default;

    int RightPolymorphic::RightOnly()
    {
        return 2;
    }

    struct MultiplePolymorphic : LeftPolymorphic, RightPolymorphic
    {
    };

    struct VirtualInheritance : virtual LeftPolymorphic
    {
    };
} // namespace Fixture

static_assert(sizeof(Fixture::Sample) == 24);

__attribute__((used, noinline)) unsigned long touch(Fixture::SampleAlias* value)
{
    return sizeof(Fixture::SampleAlias) + (value != nullptr);
}

__attribute__((used, noinline)) auto touch_incomplete(Fixture::Incomplete* value) -> bool
{
    return value != nullptr;
}

__attribute__((used, noinline)) unsigned long touch_primary(Fixture::PrimaryBase* value)
{
    return sizeof(Fixture::PrimaryBase) + (value != nullptr);
}

__attribute__((used, noinline)) unsigned long touch_vtable_inheritance(Fixture::Derived* derived,
                                                                       Fixture::MultiplePolymorphic* multiple,
                                                                       Fixture::VirtualInheritance* virtual_inheritance)
{
    return sizeof(Fixture::Derived) + sizeof(Fixture::MultiplePolymorphic) + sizeof(Fixture::VirtualInheritance) +
           (derived != nullptr) + (multiple != nullptr) + (virtual_inheritance != nullptr);
}

namespace Ambiguous
{
    struct Sample;
    struct VTableSample;
}

auto touch_ambiguous_a(Ambiguous::Sample*) -> unsigned long;
auto touch_ambiguous_b(Ambiguous::Sample*) -> unsigned long;
auto touch_vtable_ambiguous_a(Ambiguous::VTableSample*) -> unsigned long;
auto touch_vtable_ambiguous_b(Ambiguous::VTableSample*) -> unsigned long;

int main()
{
    return static_cast<int>(touch(nullptr) + touch_primary(nullptr) + touch_vtable_inheritance(nullptr, nullptr, nullptr) +
                            touch_ambiguous_a(nullptr) + touch_ambiguous_b(nullptr) + touch_vtable_ambiguous_a(nullptr) +
                            touch_vtable_ambiguous_b(nullptr));
}
