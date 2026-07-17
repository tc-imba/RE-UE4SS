# RE-UVTD

I am not the original author of this tool.  I am rehosting it with permission.
 
### Prerequisites:
Visual Studio 2022
  - Include C++ ATL for v143



CMake


### Build Instructions:
1. Clone the repository
2. Find you local DIA_SDK folder, for VS 2022 it is in "Microsoft Visual Studio\2022\Community\DIA SDK"
3. Copy the contents of the folder to external_deps in the repository folder, "RE-UVTD\external_deps\DIA_SDK"
4. Open Command Prompt in the repository folder
5. Run the following command:

    cmake CMakeLists.txt
    
4. Open the generated solution file in Visual Studio
5. Build the solution

### Run Instructions:
1. Copy the file "msdia140.dll" from "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\IDE" or your VS installation path to the folder where the executable is located
2. Create a folder named "PDBs" in the same folder as the executable
3. Copy the PDB files of the UE engine versions you want to analyze to the "PDBs" folder.  PDBs from source engine builds are preferred
4. Run the executable
5. Follow the directions in the console window

### Adding additional types to dump:
Add the class or struct name to the following variables and function implementations in the format used for each

    static std::vector<ObjectItem> s_object_items
    
    static std::unordered_set<File::StringType> s_valid_udt_names
    
    auto static is_valid_type_to_dump(File::StringType type_name) -> bool

### Adding additional UE engine versions:
1. Copy the PDB files of the UE engine versions you want to analyze to the "PDBs" folder.  PDBs from source engine builds are preferred
2. Go to the function implementation for main() found at line 1672 in UVTD_DIA.cpp
3. Add the following code with the naming of your PDB file

      ```
      TRY([&] {
            {
                VTableDumper vtable_dumper{"PDBs/4_XX.pdb"};
                vtable_dumper.generate_code(vtable_or_member_vars);
            }
            CoUninitialize();
        });

### Todo
Make types to dump be gathered from a config file rather than through editing source

 
Other cleanup

## Cross-platform CMake workflow

The instructions above are the original Windows notes. The current monorepo
port also builds UVTD with CMake on Windows and Linux. It preserves the Windows
PDB path while adding an LLVM-based ELF/DWARF path on Linux.

UVTD can now generate Unreal Engine member layouts, wrappers, virtual
integration files, Sol bindings, and native vtable layouts on both hosts:

| Host | Metadata input | Member layouts | Wrappers | Sol bindings | Vtables |
| --- | --- | --- | --- | --- | --- |
| Windows | PDB/CodeView | Yes | Yes | Yes | Yes |
| Linux | ELF/DWARF | Yes | Yes | Yes | Yes |

Windows continues to extract CodeView metadata through `raw_pdb`. Linux reads
DWARF through LLVM. Both paths populate the same `TypeContainer` and call the
same member-layout, wrapper, virtual-integration, Sol-binding, and vtable output
generators. Production generation does not read
`platform_member_layouts.json` and does not require a separate Python layout
generator.

Linux vtable generation reads `DW_AT_virtuality` and
`DW_AT_vtable_elem_location` and follows the Itanium C++ ABI. Its offsets are
byte offsets from the primary vtable address point stored in the object's
primary vptr; they are not Windows/MSVC offsets reconstructed from Linux
metadata. UVTD's Linux support does not imply that the complete UE4SS runtime
supports Linux.

### Configuration and command line

Start with the files under `assets/Default_UVTD_Configs/Config`.
`object_items.json` controls the classes and structures whose metadata UVTD
loads. The configuration contains type names and generation policy, not
platform-specific offsets.

The noninteractive interface is:

```text
UnrealVTableDumper --input <PDB-or-ELF> --engine-version <version>
  [--config <directory>] [--debug-file <path>]
  [--member-vars] [--sol-bindings] [--vtable]
```

The engine version uses UVTD's filename form, such as `5_01` for UE 5.1.x. At
least one generation operation is required. `--debug-file` is intended for a
Linux split-debug ELF; omit it when the input ELF contains its own DWARF.

### Windows

#### Prerequisites

- Visual Studio 2022 with the Desktop development with C++ workload and the
  v143 toolset.
- C++ ATL for v143, as required by the existing Windows implementation.
- CMake 3.22 or newer.

Building UVTD itself does not require Unreal Editor. Producing a complete,
matching PDB normally requires a UE source build or another build that emits
full engine and game debug information. Installing the binary editor alone is
not a substitute for that PDB.

#### Build

From the repository root in a Visual Studio developer shell:

```powershell
cmake -S . -B build/uvtd-windows -G "Visual Studio 17 2022" -A x64 `
  -DUE4SS_PROJECTS=UVTD `
  -DUE4SS_PLATFORM_TYPES=Win64 `
  -DUE4SS_VERSION_CHECK=OFF

cmake --build build/uvtd-windows `
  --config Game__Shipping__Win64 `
  --target UnrealVTableDumper
```

The executable is normally written to:

```text
build/uvtd-windows/Game__Shipping__Win64/bin/UnrealVTableDumper.exe
```

#### Existing interactive workflow

The no-argument Windows behavior is unchanged. Prepare a working directory
containing the executable, a `Config` directory, and the PDBs named in
`Config/pdbs_to_dump.json`. For UE 5.1.1, a minimal PDB list is:

```json
["PDBs/5_01.pdb"]
```

Run the executable without arguments and select the desired operation from the
console menu.

#### Noninteractive PDB workflow

The PDB filename stem must match `--engine-version`. For example:

```powershell
./UnrealVTableDumper.exe `
  --input D:/ue-layout-reference/ue511/5_01.pdb `
  --engine-version 5_01 `
  --config D:/ue-layout-reference/ue511/Config `
  --member-vars `
  --sol-bindings `
  --vtable
```

This command continues to use `raw_pdb` and the existing Windows metadata
loaders. The default Windows filenames and generated contents are preserved.

### Linux

#### Prerequisites

- Clang with C++23 support.
- CMake 3.22 or newer.
- Ninja.
- LLVM development files containing `LLVMConfig.cmake` and the Object,
  BinaryFormat, DebugInfoDWARF, Demangle, and Support components.

On distributions that install multiple LLVM versions, pass `LLVM_DIR` if CMake
cannot find the intended package, for example
`-DLLVM_DIR=/usr/lib/llvm-19/lib/cmake/llvm`.

#### Build

From the repository root:

```bash
cmake -S . -B build/uvtd-linux -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Game__Shipping__Linux \
  -DUE4SS_PROJECTS=UVTD \
  -DUE4SS_PLATFORM_TYPES=Linux \
  -DUE4SS_VERSION_CHECK=OFF \
  -DUE4SS_NO_CUSTOM_FLAGS=ON

cmake --build build/uvtd-linux --target UnrealVTableDumper
```

The executable is:

```text
build/uvtd-linux/Game__Shipping__Linux/bin/UnrealVTableDumper
```

#### Generate from an ELF containing DWARF

```bash
./build/uvtd-linux/Game__Shipping__Linux/bin/UnrealVTableDumper \
  --input /home/user/ue511/LayoutProbe-Linux-Shipping \
  --engine-version 5_01 \
  --config assets/Default_UVTD_Configs/Config \
  --vtable \
  --member-vars \
  --sol-bindings
```

If debug information is stored in a separate ELF debug file, keep the original
binary as `--input` and select the debug artifact explicitly:

```bash
./build/uvtd-linux/Game__Shipping__Linux/bin/UnrealVTableDumper \
  --input /home/user/ue511/LayoutProbe-Linux-Shipping \
  --debug-file /home/user/ue511/LayoutProbe-Linux-Shipping.debug \
  --engine-version 5_01 \
  --config assets/Default_UVTD_Configs/Config \
  --vtable \
  --member-vars \
  --sol-bindings
```

#### Interactive Linux workflow

Run Linux UVTD without arguments to enter the interactive driver. It prompts
for the ELF path, an optional split-debug path, the engine version, and the
configuration directory, then offers member-layout, Sol-binding, vtable, or
combined generation.

Generation uses a temporary sibling directory and replaces
`UVTD_Generated_Output` only after extraction and generation succeed. A failed
run preserves the previously published output tree.

#### Linux vtable model and limitations

UVTD generates the effective primary Itanium table supported by UE4SS's current
one-`VTableLayoutMap`-per-class model. It carries primary-base slots into derived
classes, replaces overridden slots, and appends newly introduced virtuals.
Itanium offset-to-top and RTTI entries precede the address point and are not
counted.

A virtual destructor reserves its reported slot and the following deleting
destructor slot. Linux templates also emit deterministic
`__uvtd_reserved_slot_<N>` entries for ABI slots not represented by a callable
method DIE. These inspection placeholders are not inserted into the runtime
`VTableLayoutMap`.

UVTD rejects virtual inheritance and classes with more than one polymorphic
direct base because those layouts require secondary vtable address points that
the runtime map cannot identify safely. Non-polymorphic mixins are supported.

### Producing complete UE 5.1.1 metadata

Use the same UE 5.1.1 source revision, project, target rules, and configured type
anchors on Windows and Linux. A blank project is sufficient only when its build
forces every configured engine type into the debug information.

Use a non-editor, monolithic Shipping game target and enable debug information:

```csharp
Type = TargetType.Game;
LinkType = TargetLinkType.Monolithic;
BuildEnvironment = TargetBuildEnvironment.Unique;
bForceDebugInfo = true;
bDisableDebugInfo = false;
bDisableDebugInfoForGeneratedCode = false;
```

For Linux, also request standalone type descriptions:

```csharp
AdditionalCompilerArguments =
    (AdditionalCompilerArguments ?? "") + " -fstandalone-debug";
```

For Windows, request full non-incremental PDB generation:

```csharp
bUsePDBFiles = true;
bUseFastPDBLinking = false;
bUseIncrementalLinking = false;
```

Verify that the Windows link command contains `/DEBUG:FULL` and the Linux
compile commands contain `-fstandalone-debug`. Vtable generation also requires
full method DIEs and their `DW_AT_vtable_elem_location` attributes; a stripped
ELF or a limited debug build containing only data-member types is insufficient.
A useful reference configuration for a layout probe is
`-O0 -g -gdwarf-5 -fstandalone-debug`.

Do not assume a successful build contains all configured types. Add a non-unity
translation unit, such as `LayoutProbeTypeAnchors.cpp`, that takes pointers to
and evaluates `sizeof` for every configured class. This prevents unused engine
types from being omitted from PDB or DWARF metadata.

Use the PDB, ELF, and optional split-debug artifact produced by that exact
build. Do not mix symbols from another engine revision or build configuration.

### Generated output

UVTD writes beneath `UVTD_Generated_Output` in the current working directory.
Important paths include:

```text
UVTD_Generated_Output/assets/MemberVarLayoutTemplates/
UVTD_Generated_Output/assets/VTableLayoutTemplates/
UVTD_Generated_Output/assets/VTableLayoutTemplates/Platform/Linux/
UVTD_Generated_Output/deps/first/Unreal/generated_include/
UVTD_Generated_Output/deps/first/Unreal/generated_include/FunctionBodies/
UVTD_Generated_Output/deps/first/Unreal/generated_include/FunctionBodies/Platform/Linux/
UVTD_Generated_Output/UE4SS/generated_include/
UVTD_Generated_Output/GeneratedSolBindings/
```

Windows default setters remain directly under `FunctionBodies`. Linux member
and vtable setters are placed under `FunctionBodies/Platform/Linux`, while the
shared member templates, wrappers, virtual integration files, and Sol bindings
come from the same generators on both platforms. For UE 5.1, Linux vtable output
includes:

```text
UVTD_Generated_Output/assets/VTableLayoutTemplates/Platform/Linux/VTableLayout_5_01_Template.ini
UVTD_Generated_Output/deps/first/Unreal/generated_include/FunctionBodies/Platform/Linux/5_01_VTableOffsets_<Class>_FunctionBody.cpp
```

### Tests

Configure with `-DUE4SS_BUILD_TESTS=ON` to add the UVTD test targets, then run
them with CTest:

```bash
cmake -S . -B build/uvtd-linux-tests -G Ninja \
  -DCMAKE_C_COMPILER=clang \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Game__Shipping__Linux \
  -DUE4SS_PROJECTS=UVTD \
  -DUE4SS_PLATFORM_TYPES=Linux \
  -DUE4SS_VERSION_CHECK=OFF \
  -DUE4SS_NO_CUSTOM_FLAGS=ON \
  -DUE4SS_BUILD_TESTS=ON

cmake --build build/uvtd-linux-tests --target \
  UnrealVTableDumper \
  UVTDVTableOutputTests \
  UVTDVTableMethodNamesTests \
  UVTDTypeMetadataCompileTests \
  UVTDLinuxFileSmokeTests \
  UVTDMemberVarsOutputTests \
  UVTDConfigWithoutPlatformLayoutsTests \
  UVTDSolBindingsTypeContainerTests \
  UVTDLinuxInteractiveTests \
  UVTDDwarfMemberVarsLoaderTests \
  UVTDCommandLineTests
ctest --test-dir build/uvtd-linux-tests --output-on-failure -R UVTD
```
