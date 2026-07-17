cmake_minimum_required(VERSION 3.22)

set(TARGET UnrealVTableDumper)
project(${TARGET})
message("Project: UnrealVTableDumper")

# Keep the existing Windows implementation intact and share only the generators
# that consume TypeContainer.
set(${TARGET}_Sources
    src/CommandLine.cpp
    src/Config.cpp
    src/Helpers.cpp
    src/MemberVarsOutputGenerator.cpp
    src/MemberVarsWrapperGenerator.cpp
    src/SolBindingsGenerator.cpp
    src/TemplateClassParser.cpp
    src/TypeContainer.cpp
    src/UnrealVirtualGenerator.cpp
    src/VTableMethodNames.cpp
    src/VTableOutputGenerator.cpp
)

if(WIN32)
    list(APPEND ${TARGET}_Sources
        src/MemberVarsDumper.cpp
        src/Symbols.cpp
        src/UVTD.cpp
        src/VTableDumper.cpp
        src/main.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND ${TARGET}_Sources
        src/DwarfMemberVarsLoader.cpp
        src/LinuxInteractive.cpp
        src/main_linux.cpp
    )
    find_package(LLVM CONFIG REQUIRED)
    if(TARGET LLVM AND LLVM_LINK_LLVM_DYLIB)
        set(UVTD_LLVM_LIBS LLVM)
    else()
        llvm_map_components_to_libnames(UVTD_LLVM_LIBS
            Object
            BinaryFormat
            DebugInfoDWARF
            Demangle
            Support
        )
    endif()
else()
    message(FATAL_ERROR "UVTD does not support ${CMAKE_SYSTEM_NAME}")
endif()

file(GLOB_RECURSE ${TARGET}_Headers "${CMAKE_CURRENT_SOURCE_DIR}/include/**.hpp")

add_executable(${TARGET} ${${TARGET}_Sources} ${${TARGET}_Headers})

# Organize sources in IDE
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}" FILES ${${TARGET}_Sources} ${${TARGET}_Headers})

if (NOT UE4SS_NO_CUSTOM_FLAGS)
        target_compile_options(${TARGET} PUBLIC "${PRIVATE_COMPILE_OPTIONS}")
        target_link_options(${TARGET} PUBLIC "${PRIVATE_LINK_OPTIONS}")
endif ()

# Enabling c++23 support
target_compile_features(${TARGET} PUBLIC cxx_std_23)

target_include_directories(${TARGET} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# First party static build -> START
target_compile_definitions(${TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_DYNAMIC_OUTPUT_BUILD_STATIC
        RC_SINGLE_PASS_SIG_SCANNER_BUILD_STATIC
        RC_SINGLE_PASS_SIG_SCANNER_STATIC
        RC_UNREAL_BUILD_STATIC
        RC_INPUT_BUILD_STATIC
        RC_LUA_MADE_SIMPLE_BUILD_STATIC
        RC_FUNCTION_TIMER_BUILD_STATIC
        RC_PARSER_BASE_BUILD_STATIC
        RC_INI_PARSER_BUILD_STATIC
        RC_JSON_BUILD_STATIC
)
# First party static build -> END

target_link_libraries(${TARGET} PRIVATE File DynamicOutput Helpers)

target_link_libraries(${TARGET} PRIVATE glaze::glaze)

if(WIN32)
    target_link_libraries(${TARGET} PRIVATE Input raw_pdb)
else()
    target_include_directories(${TARGET} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
    target_link_libraries(${TARGET} PRIVATE ${UVTD_LLVM_LIBS})
endif()

# Set IDE folder - make sure to use capital P for Programs
set_target_properties(${TARGET} PROPERTIES FOLDER "Programs")

if (UE4SS_BUILD_TESTS)
    set(TEST_TARGET UVTDVTableOutputTests)
    add_executable(${TEST_TARGET}
        tests/VTableOutputGeneratorTests.cpp
        src/Config.cpp
        src/Helpers.cpp
        src/TypeContainer.cpp
        src/VTableOutputGenerator.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_DYNAMIC_OUTPUT_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(${TEST_TARGET} PRIVATE
        File
        DynamicOutput
        Helpers
        fmt
        glaze::glaze
    )
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDVTableMethodNamesTests)
    add_executable(${TEST_TARGET}
        tests/VTableMethodNamesTests.cpp
        src/VTableMethodNames.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(${TEST_TARGET} PRIVATE File)
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDTypeMetadataCompileTests)
    add_executable(${TEST_TARGET}
        tests/TypeMetadataCompileTests.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/first/File/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/first/String/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/deps/first/Helpers/include>
    )
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDLinuxFileSmokeTests)
    add_executable(${TEST_TARGET}
        tests/LinuxFileSmokeTests.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_link_libraries(${TEST_TARGET} PRIVATE File)
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDMemberVarsOutputTests)
    add_executable(${TEST_TARGET}
        tests/MemberVarsOutputGeneratorTests.cpp
        src/Config.cpp
        src/Helpers.cpp
        src/MemberVarsOutputGenerator.cpp
        src/TypeContainer.cpp
        src/UnrealVirtualGenerator.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_DYNAMIC_OUTPUT_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(${TEST_TARGET} PRIVATE
        File
        DynamicOutput
        Helpers
        fmt
        glaze::glaze
    )
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDConfigWithoutPlatformLayoutsTests)
    add_executable(${TEST_TARGET}
        tests/ConfigWithoutPlatformLayoutsTests.cpp
        src/Config.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_DYNAMIC_OUTPUT_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(${TEST_TARGET} PRIVATE
        File
        DynamicOutput
        Helpers
        fmt
        glaze::glaze
    )
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    set(TEST_TARGET UVTDSolBindingsTypeContainerTests)
    add_executable(${TEST_TARGET}
        tests/SolBindingsTypeContainerTests.cpp
        src/Config.cpp
        src/SolBindingsGenerator.cpp
    )
    target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
    target_compile_definitions(${TEST_TARGET} PRIVATE
        RC_FILE_BUILD_STATIC
        RC_DYNAMIC_OUTPUT_BUILD_STATIC
        RC_USE_WCHAR
    )
    target_include_directories(${TEST_TARGET} PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_link_libraries(${TEST_TARGET} PRIVATE
        File
        DynamicOutput
        Helpers
        fmt
        glaze::glaze
    )
    add_test(NAME ${TEST_TARGET} COMMAND ${TEST_TARGET})
    set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(TEST_TARGET UVTDLinuxInteractiveTests)
        add_executable(${TEST_TARGET}
            tests/LinuxInteractiveTests.cpp
            src/CommandLine.cpp
            src/Config.cpp
            src/DwarfMemberVarsLoader.cpp
            src/Helpers.cpp
            src/LinuxInteractive.cpp
            src/MemberVarsOutputGenerator.cpp
            src/MemberVarsWrapperGenerator.cpp
            src/SolBindingsGenerator.cpp
            src/TypeContainer.cpp
            src/UnrealVirtualGenerator.cpp
            src/VTableMethodNames.cpp
            src/VTableOutputGenerator.cpp
        )
        target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
        target_compile_definitions(${TEST_TARGET} PRIVATE
            RC_FILE_BUILD_STATIC
            RC_DYNAMIC_OUTPUT_BUILD_STATIC
            RC_USE_WCHAR
        )
        target_include_directories(${TEST_TARGET} PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            ${LLVM_INCLUDE_DIRS}
        )
        target_link_libraries(${TEST_TARGET} PRIVATE
            File
            DynamicOutput
            Helpers
            fmt
            glaze::glaze
            ${UVTD_LLVM_LIBS}
        )
        add_test(
            NAME ${TEST_TARGET}
            COMMAND ${TEST_TARGET} $<TARGET_FILE:UVTDDwarfLayoutFixture>
        )
        set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")

        add_executable(UVTDDwarfLayoutFixture
            tests/fixtures/DwarfLayoutFixture.cpp
            tests/fixtures/DwarfAmbiguousA.cpp
            tests/fixtures/DwarfAmbiguousB.cpp
        )
        target_compile_features(UVTDDwarfLayoutFixture PRIVATE cxx_std_23)
        target_compile_options(UVTDDwarfLayoutFixture PRIVATE
            -O0
            -g
            -gdwarf-5
            -fstandalone-debug
        )
        add_dependencies(UVTDLinuxInteractiveTests UVTDDwarfLayoutFixture)

        set(DWARF_SPECIAL_CASES_FIXTURE
            ${CMAKE_CURRENT_BINARY_DIR}/DwarfSpecialCasesFixture.o)
        add_custom_command(
            OUTPUT ${DWARF_SPECIAL_CASES_FIXTURE}
            COMMAND ${CMAKE_CXX_COMPILER}
                -c
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/DwarfSpecialCasesFixture.s
                -o
                ${DWARF_SPECIAL_CASES_FIXTURE}
            DEPENDS tests/fixtures/DwarfSpecialCasesFixture.s
            VERBATIM
        )
        add_custom_target(
            UVTDDwarfSpecialCasesFixture
            DEPENDS ${DWARF_SPECIAL_CASES_FIXTURE}
        )

        set(TEST_TARGET UVTDDwarfMemberVarsLoaderTests)
        add_executable(${TEST_TARGET}
            tests/DwarfMemberVarsLoaderTests.cpp
            src/Config.cpp
            src/DwarfMemberVarsLoader.cpp
            src/Helpers.cpp
            src/TypeContainer.cpp
            src/VTableMethodNames.cpp
        )
        target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
        target_compile_definitions(${TEST_TARGET} PRIVATE
            RC_FILE_BUILD_STATIC
            RC_DYNAMIC_OUTPUT_BUILD_STATIC
            RC_USE_WCHAR
        )
        target_include_directories(${TEST_TARGET} PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            ${LLVM_INCLUDE_DIRS}
        )
        target_link_libraries(${TEST_TARGET} PRIVATE
            File
            DynamicOutput
            Helpers
            fmt
            glaze::glaze
            ${UVTD_LLVM_LIBS}
        )
        add_dependencies(
            ${TEST_TARGET}
            UVTDDwarfLayoutFixture
            UVTDDwarfSpecialCasesFixture
        )
        add_test(
            NAME ${TEST_TARGET}
            COMMAND ${TEST_TARGET}
                $<TARGET_FILE:UVTDDwarfLayoutFixture>
                ${DWARF_SPECIAL_CASES_FIXTURE}
        )
        set_target_properties(
            ${TEST_TARGET}
            UVTDDwarfLayoutFixture
            PROPERTIES FOLDER "Tests"
        )

        set(TEST_TARGET UVTDCommandLineTests)
        add_executable(${TEST_TARGET}
            tests/CommandLineTests.cpp
            src/CommandLine.cpp
            src/Config.cpp
            src/DwarfMemberVarsLoader.cpp
            src/Helpers.cpp
            src/MemberVarsOutputGenerator.cpp
            src/MemberVarsWrapperGenerator.cpp
            src/SolBindingsGenerator.cpp
            src/TypeContainer.cpp
            src/UnrealVirtualGenerator.cpp
            src/VTableMethodNames.cpp
            src/VTableOutputGenerator.cpp
        )
        target_compile_features(${TEST_TARGET} PRIVATE cxx_std_23)
        target_compile_definitions(${TEST_TARGET} PRIVATE
            RC_FILE_BUILD_STATIC
            RC_DYNAMIC_OUTPUT_BUILD_STATIC
            RC_USE_WCHAR
        )
        target_include_directories(${TEST_TARGET} PRIVATE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            ${LLVM_INCLUDE_DIRS}
        )
        target_link_libraries(${TEST_TARGET} PRIVATE
            File
            DynamicOutput
            Helpers
            fmt
            glaze::glaze
            ${UVTD_LLVM_LIBS}
        )
        add_dependencies(${TEST_TARGET} UVTDDwarfLayoutFixture)
        add_test(
            NAME ${TEST_TARGET}
            COMMAND ${TEST_TARGET} $<TARGET_FILE:UVTDDwarfLayoutFixture>
        )
        set_target_properties(${TEST_TARGET} PROPERTIES FOLDER "Tests")
    endif()

endif ()
