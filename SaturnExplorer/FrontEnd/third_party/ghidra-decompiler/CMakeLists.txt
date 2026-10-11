# Ghidra's native decompiler as a static library, and its SLEIGH compiler as a host tool.
# Added from the SaturnExplorer root only when SE_ENABLE_DECOMPILER is on. A directory of its
# own so the optimisation override below stays scoped to these targets.

include(${CMAKE_CURRENT_SOURCE_DIR}/sources.cmake)   # written by vendor.py

# The engine is built optimised in every configuration. Unoptimised, a single decompile of a
# large function takes many times longer and MSVC Debug objects of the bigger translation
# units approach the section limit. MSVC rejects /O2 next to the /RTC1 a Debug build carries,
# so the runtime checks go too -- for these targets only, since this is a directory scope.
# The runtime library (/MDd in Debug) and with it _ITERATOR_DEBUG_LEVEL are left alone: they
# have to match every object the library is linked with.
if(MSVC)
    foreach(flags CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_DEBUG)
        string(REGEX REPLACE "/RTC[1csu]+" "" ${flags} "${${flags}}")
        string(REPLACE "/Od" "/O2" ${flags} "${${flags}}")
    endforeach()
else()
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2")
endif()

list(TRANSFORM SE_GHIDRA_DECOMP_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
list(TRANSFORM SE_GHIDRA_SLEIGHC_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)

add_library(SaturnExplorerGhidraDecomp STATIC ${SE_GHIDRA_DECOMP_SOURCES})
target_include_directories(SaturnExplorerGhidraDecomp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cpp)
# .sla files are zlib-compressed (slaformat/compression); se-sleighc writes them, the engine reads them.
target_link_libraries(SaturnExplorerGhidraDecomp PUBLIC SaturnExplorerZlib)
# Upstream requires C++11; the project's C++14 compiles it unchanged.
set_target_properties(SaturnExplorerGhidraDecomp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # The upstream sources select their Windows paths (filemanage.cc, the MSVC warning pragmas
    # in types.h) on _WINDOWS, which is what Ghidra's own Windows build defines. PUBLIC because
    # types.h is read by every file that includes an engine header.
    target_compile_definitions(SaturnExplorerGhidraDecomp PUBLIC _WINDOWS)
endif()
if(MSVC)
    # Ghidra's Windows build flags (/EHsc /W3), plus /bigobj for the largest units.
    target_compile_options(SaturnExplorerGhidraDecomp PRIVATE /EHsc /W3 /bigobj)
    target_compile_definitions(SaturnExplorerGhidraDecomp PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # The warnings upstream's own Makefile does not enable; nothing here is ours to fix.
    target_compile_options(SaturnExplorerGhidraDecomp PRIVATE
        -Wno-sign-compare -Wno-unused-variable -Wno-deprecated-declarations)
endif()

# The SLEIGH compiler. Turns sh-2.slaspec into the sh-2.sla that is checked in beside it;
# the freshness test runs it on every CI build to prove the checked-in file is current.
add_executable(se-sleighc ${SE_GHIDRA_SLEIGHC_SOURCES})
target_link_libraries(se-sleighc PRIVATE SaturnExplorerGhidraDecomp)
set_target_properties(se-sleighc PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(se-sleighc PRIVATE /EHsc /W3 /bigobj)
    target_compile_definitions(se-sleighc PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(se-sleighc PRIVATE -Wno-sign-compare -Wno-unused-function)
endif()

# Regenerate the checked-in spec artifacts after a Ghidra bump or a SLEIGH patch:
#   cmake --build build --target se-regen-sla
# rewrites processors/SuperH/sh-2.sla and FrontEnd/src/Decompiler/Sh2SpecData.h in the source
# tree. Deliberately not part of the normal build: the files are reviewed and committed.
set(SE_SPEC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/processors/SuperH)
set(SE_SPEC_HEADER ${PROJECT_SOURCE_DIR}/FrontEnd/src/Decompiler/Sh2SpecData.h)
add_custom_target(se-regen-sla
    COMMAND se-sleighc ${SE_SPEC_DIR}/sh-2.slaspec ${SE_SPEC_DIR}/sh-2.sla
    COMMAND ${CMAKE_COMMAND} -DSPEC_DIR=${SE_SPEC_DIR} -DOUT_FILE=${SE_SPEC_HEADER}
            -P ${PROJECT_SOURCE_DIR}/cmake/GenerateSpecData.cmake
    COMMENT "Regenerating sh-2.sla and Sh2SpecData.h in the source tree"
    VERBATIM)
