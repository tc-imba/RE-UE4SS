#include <UVTD/VTableMethodNames.hpp>

#include <algorithm>

namespace RC::UVTD
{
    auto sanitize_type_for_identifier(File::StringType type) -> File::StringType
    {
        File::StringType result = type;

        // Handle volatile prefix
        if (result.starts_with(STR("volatile ")))
        {
            result = result.substr(9);
            result = STR("V_") + result;
        }

        // Handle const volatile prefix
        if (result.starts_with(STR("const volatile ")) || result.starts_with(STR("volatile const ")))
        {
            result = result.substr(15);
            result = STR("CV_") + result;
        }

        // Handle const prefix
        if (result.starts_with(STR("const ")))
        {
            result = result.substr(6);
            result = STR("C_") + result;
        }

        // Handle references and pointers
        while (!result.empty() && (result.back() == '&' || result.back() == '*'))
        {
            if (result.back() == '&')
            {
                result.pop_back();
                result = STR("Ref_") + result;
            }
            else if (result.back() == '*')
            {
                result.pop_back();
                result = STR("Ptr_") + result;
            }
        }

        // Replace characters that are invalid in identifiers
        std::replace(result.begin(), result.end(), '<', '_');
        std::replace(result.begin(), result.end(), '>', '_');
        std::replace(result.begin(), result.end(), ',', '_');
        std::replace(result.begin(), result.end(), ' ', '_');
        std::replace(result.begin(), result.end(), ':', '_');
        std::replace(result.begin(), result.end(), '[', '_');
        std::replace(result.begin(), result.end(), ']', '_');
        std::replace(result.begin(), result.end(), '(', '_');
        std::replace(result.begin(), result.end(), ')', '_');

        // Remove any multiple underscores (not just doubles)
        File::StringType cleaned;
        bool last_was_underscore = false;
        for (auto c : result)
        {
            if (c == '_')
            {
                if (!last_was_underscore)
                {
                    cleaned += c;
                    last_was_underscore = true;
                }
            }
            else
            {
                cleaned += c;
                last_was_underscore = false;
            }
        }

        return cleaned;
    }

    auto generate_mangled_suffix(const MethodSignature& signature) -> File::StringType
    {
        File::StringType suffix = STR("");
        auto quals = signature.qualifiers;
        // Add qualifiers if any exist
        bool has_quals = quals.is_const || quals.is_volatile || quals.is_lvalue_ref || quals.is_rvalue_ref;

        if (has_quals)
        {
            suffix += STR("_");

            // cv-qualifiers come before ref-qualifiers
            if (quals.is_const)
            {
                suffix += STR("C"); // const
            }
            if (quals.is_volatile)
            {
                suffix += STR("V"); // volatile
            }

            // Ref-qualifiers
            if (quals.is_lvalue_ref)
            {
                suffix += STR("L"); // & (lvalue ref)
            }
            else if (quals.is_rvalue_ref)
            {
                suffix += STR("R"); // && (rvalue ref)
            }
        }

        auto params = signature.params;
        // Add parameters if any
        if (!params.empty())
        {
            suffix += STR("__");
            for (size_t i = 0; i < params.size(); ++i)
            {
                if (i > 0) suffix += STR("__");
                suffix += sanitize_type_for_identifier(params[i].type);
            }
        }

        return suffix;
    }
} // namespace RC::UVTD
