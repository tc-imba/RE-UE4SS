#pragma once

#include <unordered_map>

#include <File/File.hpp>
#include <UVTD/PDBNameInfo.hpp>
#include <UVTD/TypeMetadata.hpp>

#include <PDB_DBIStream.h>
#include <PDB_RawFile.h>
#include <PDB_TPIStream.h>

namespace RC::UVTD
{

    class Symbols
    {
    public:
        struct EnumEntry
        {
            File::StringType name;
            File::StringType name_clean;
            std::map<File::StringType, MemberVariable> variables;
        };

        enum class ClassInheritanceModel
        {
            Single,
            Multiple,
            Virtual
        };

      public:
        std::filesystem::path pdb_file_path;
        File::Handle pdb_file_handle;
        std::span<uint8_t> pdb_file_map;

        PDB::RawFile pdb_file;
        PDB::DBIStream dbi_stream;
        bool is_425_plus;

    private:
        PDB::CodeView::DBI::CPUType m_machine_type{PDB::CodeView::DBI::CPUType::X64};
        PDBNameInfo m_pdb_name_info{};

    public:
        std::unordered_map<File::StringType, EnumEntry> enum_entries;
        std::unordered_map<File::StringType, Class> class_entries;
        static inline std::unordered_map<uint32_t, uint32_t> type_size_cache;


        Symbols() = delete;

        explicit Symbols(std::filesystem::path pdb_file_path);

        Symbols(const Symbols& other);

        Symbols& operator=(const Symbols& other);

      public:
        auto get_or_create_enum_entry(const File::StringType& symbol_name, const File::StringType& symbol_name_clean) -> EnumEntry&;

        auto generate_method_signature(const PDB::TPIStream& tpi_stream, const PDB::CodeView::TPI::Record* function_record, File::StringType method_name)
                -> MethodSignature;

      public:
        auto static get_type_name(const PDB::TPIStream& tpi_stream, uint32_t record_index, bool check_valid = false, bool is_64bit = true) -> File::StringType;

        // Bitfield info extraction
        struct BitfieldInfo
        {
            bool is_bitfield{false};
            uint8_t bit_position{0};
            uint8_t bit_length{0};
        };
        auto static get_bitfield_info(const PDB::TPIStream& tpi_stream, uint32_t record_index) -> BitfieldInfo;
        auto static read_numeric(const uint8_t*& data) -> uint64_t;
        auto static get_numeric_leaf_size(const uint8_t* data) -> uint32_t;
        auto static get_field_record_size(const PDB::CodeView::TPI::FieldList* field) -> uint32_t;
        auto static get_type_size_impl(const PDB::TPIStream& tpi_stream, uint32_t record_index, bool is_64bit = true) -> uint32_t;
        auto static get_type_size(const PDB::TPIStream& tpi_stream, uint32_t record_index, bool is_64bit = true) -> uint32_t;
        auto static get_method_name(const PDB::CodeView::TPI::FieldList* method_record) -> File::StringType;
        auto static get_class_inheritance_model(const PDB::TPIStream& tpi_stream, uint32_t class_type_index) -> ClassInheritanceModel;
        // Existing method for backward compatibility
        auto static get_leaf_name(const char* data, PDB::CodeView::TPI::TypeRecordKind kind) -> File::StringType;

        // New overload that takes class name for context-aware name mapping
        auto static get_leaf_name(const File::StringType& class_name, const char* data, PDB::CodeView::TPI::TypeRecordKind kind) -> File::StringType;

        auto static clean_name(File::StringType name) -> File::StringType;

        auto static is_virtual(PDB::CodeView::TPI::MemberAttributes attributes) -> bool;
        auto is_x64() const -> bool;
        auto is_x86() const -> bool;

        // PDB name info accessor
        auto get_pdb_name_info() const -> const PDBNameInfo& { return m_pdb_name_info; }

      private:
        auto setup_symbol_loader() -> void;
    };
} // namespace RC::UVTD