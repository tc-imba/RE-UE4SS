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
