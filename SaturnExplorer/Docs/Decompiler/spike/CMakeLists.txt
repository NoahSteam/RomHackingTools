# Feasibility spike (see ../SPIKE_RESULTS.md). Not part of the SaturnExplorer build.
cmake_minimum_required(VERSION 3.15)
project(Sh2DecompSpike LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(GHIDRA_DECOMP_DIR "" CACHE PATH "Ghidra/Features/Decompiler/src/decompile/cpp")
find_package(ZLIB REQUIRED)   # .sla files are zlib-compressed (slaformat/compression)

# Exactly the Makefile's CORE + DECCORE + SLEIGH groups, plus the SleighArchitecture
# glue (sleigh_arch, inject_sleigh) and the library entry point. No BFD, no XML load
# image, no console/test code, no ghidra_* (the Java-process bridge).
set(CORE xml marshal space float address pcoderaw translate opcodes metatype globalcontext)
set(DECCORE capability architecture options graph cover block cast typeop database cpool
    comment stringmanage modelrules fspec action loadimage grammar varnode op type
    variable varmap jumptable emulate emulateutil flow userop expression multiprecision
    funcdata funcdata_block funcdata_op funcdata_varnode unionresolve pcodeinject
    heritage prefersplit rangeutil ruleaction subflow blockaction merge double
    transform constseq bitfield coreaction condexe override dynamic crc32 prettyprint
    printlanguage printc printjava memstate opbehavior paramid signature)
set(SLEIGH sleigh pcodeparse pcodecompile sleighbase slghsymbol
    slghpatexpress slghpattern semantics context slaformat compression filemanage)
set(GLUE sleigh_arch inject_sleigh libdecomp)
set(SRCS)
foreach(n ${CORE} ${DECCORE} ${SLEIGH} ${GLUE})
  list(APPEND SRCS ${GHIDRA_DECOMP_DIR}/${n}.cc)
endforeach()
add_library(ghidra_decomp STATIC ${SRCS})
target_include_directories(ghidra_decomp PUBLIC ${GHIDRA_DECOMP_DIR})
target_link_libraries(ghidra_decomp PUBLIC ZLIB::ZLIB)
target_compile_options(ghidra_decomp PRIVATE -Wno-sign-compare -Wno-unused-variable -Wno-deprecated-declarations)

# Optional: the SLEIGH compiler, to turn .slaspec into .sla at build time.
add_executable(sleigh_compiler ${GHIDRA_DECOMP_DIR}/slgh_compile.cc ${GHIDRA_DECOMP_DIR}/slghparse.cc ${GHIDRA_DECOMP_DIR}/slghscan.cc)
target_link_libraries(sleigh_compiler PRIVATE ghidra_decomp)
target_compile_options(sleigh_compiler PRIVATE -Wno-sign-compare -Wno-unused-function)

add_executable(sh2_decomp_spike sh2_decomp_spike.cpp)
target_link_libraries(sh2_decomp_spike PRIVATE ghidra_decomp)

# Fixture extraction through SaturnExplorer's own savestate driver (built separately).
set(SE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(SE_BUILD "" CACHE PATH "SaturnExplorer build dir with libSaturnExplorerSavestateDriver.a")
if(SE_BUILD)
  add_executable(yss_dump yss_dump.cpp)
  target_include_directories(yss_dump PRIVATE ${SE_ROOT}/Drivers/Savestate/src ${SE_ROOT}/include ${SE_ROOT}/Drivers/Common/src)
  target_link_libraries(yss_dump PRIVATE
      ${SE_BUILD}/lib/libSaturnExplorerSavestateDriver.a
      ${SE_BUILD}/lib/libSaturnExplorerCore.a
      ${SE_BUILD}/lib/libSaturnExplorerDriverCommon.a ZLIB::ZLIB)
endif()
