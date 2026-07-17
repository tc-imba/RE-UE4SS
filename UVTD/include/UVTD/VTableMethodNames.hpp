#pragma once

#include <File/File.hpp>
#include <UVTD/TypeMetadata.hpp>

namespace RC::UVTD
{
    auto sanitize_type_for_identifier(File::StringType type) -> File::StringType;
    auto generate_mangled_suffix(const MethodSignature& signature) -> File::StringType;
} // namespace RC::UVTD
