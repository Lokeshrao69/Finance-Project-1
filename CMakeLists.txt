# ============================================================================
#  Nexus-LOB — root build configuration
#  Hybrid C++20 / CUDA / Pybind11 limit-order-book platform.
#
#  Intended dev environment: Linux / WSL-Ubuntu + NVIDIA GPU (see CLAUDE.md §8).
#  Quick start (from repo root):
#      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#      cmake --build build -j
#      ctest --test-dir build --output-on-failure
#
#  Useful toggles:
#      -DNEXUS_BUILD_PYBIND=OFF   # C++ engine + tests only (no Python needed)
#      -DNEXUS_BUILD_TESTS=OFF    # skip abi_check / lob_test
#      -DNEXUS_ENABLE_CUDA=OFF    # skip GPU risk-engine hooks (subsystem 3)
#      -DNEXUS_NATIVE_ARCH=OFF    # portable build (drop -march=native)
# ============================================================================
cmake_minimum_required(VERSION 3.20)

project(NexusLOB
    VERSION 0.1.0
    DESCRIPTION "Ultra-low-latency limit-order-book trading & microstructure platform"
    LANGUAGES CXX)                 # CUDA is enabled conditionally, further down.

# ---- global standards ------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)      # -std=c++20, not -std=gnu++20 (portable & strict)

# Position-independent code everywhere: the pybind MODULE links the engine, and a
# shared object must be built from PIC objects.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile_commands.json for clangd / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimized build (the engine's headline metrics are Release-only).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "No build type specified — defaulting to Release")
endif()

# ---- options ---------------------------------------------------------------
option(NEXUS_BUILD_TESTS  "Build the C++ unit tests (abi_check, lob_test)" ON)
option(NEXUS_BUILD_PYBIND "Build the nexus_engine Pybind11 module"         ON)
option(NEXUS_ENABLE_CUDA  "Build CUDA risk-engine hooks if a toolkit exists" ON)
option(NEXUS_NATIVE_ARCH  "Optimize for the build machine (-march=native)"  ON)

# ============================================================================
#  1. cpp_engine — the matching engine (Person A)
# ============================================================================
# Header-only TODAY: every symbol lives in cpp_engine/include/nexus/*.hpp.
# The roadmap adds translation units under cpp_engine/src/, so we build a STATIC
# library the moment any .cpp exists there and fall back to a header-only
# INTERFACE target until then. Either way the public name is `nexus::engine`, so
# dependents (tests, the pybind module, the future CUDA lib) never change.
file(GLOB_RECURSE NEXUS_ENGINE_SOURCES CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/cpp_engine/src/*.cpp")

if(NEXUS_ENGINE_SOURCES)
    add_library(cpp_engine STATIC ${NEXUS_ENGINE_SOURCES})
    set(NEXUS_ENGINE_SCOPE PUBLIC)
    message(STATUS "cpp_engine: STATIC library from ${CMAKE_CURRENT_LIST_DIR}/cpp_engine/src")
else()
    add_library(cpp_engine INTERFACE)
    set(NEXUS_ENGINE_SCOPE INTERFACE)
    message(STATUS "cpp_engine: header-only INTERFACE target (no cpp_engine/src/*.cpp yet)")
endif()
add_library(nexus::engine ALIAS cpp_engine)

target_include_directories(cpp_engine ${NEXUS_ENGINE_SCOPE}
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/cpp_engine/include>")
target_compile_features(cpp_engine ${NEXUS_ENGINE_SCOPE} cxx_std_20)

# -O3 (+ optional -march=native) on the optimized configs, GCC/Clang only.
if(NOT MSVC)
    target_compile_options(cpp_engine ${NEXUS_ENGINE_SCOPE}
        "$<$<CONFIG:Release>:-O3>"
        "$<$<CONFIG:RelWithDebInfo>:-O3>")
    if(NEXUS_NATIVE_ARCH)
        target_compile_options(cpp_engine ${NEXUS_ENGINE_SCOPE}
            "$<$<CONFIG:Release>:-march=native>"
            "$<$<CONFIG:RelWithDebInfo>:-march=native>")
    endif()
endif()

# ============================================================================
#  2. C++ tests — standalone, no framework (run via CTest)
# ============================================================================
if(NEXUS_BUILD_TESTS)
    enable_testing()

    # ABI lock: compile-time static_asserts + prints sizeof/offsets (== 448 B).
    add_executable(abi_check cpp_engine/tests/abi_check.cpp)
    target_link_libraries(abi_check PRIVATE nexus::engine)
    add_test(NAME abi_check COMMAND abi_check)

    # Matching-engine correctness: crosses, FIFO priority, IOC/FOK/market,
    # cancel/modify, and every reject path.
    if(EXISTS "${PROJECT_SOURCE_DIR}/cpp_engine/tests/lob_test.cpp")
        add_executable(lob_test cpp_engine/tests/lob_test.cpp)
        target_link_libraries(lob_test PRIVATE nexus::engine)
        if(NOT MSVC)
            target_compile_options(lob_test PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME lob_test COMMAND lob_test)
    endif()
endif()

# ============================================================================
#  3. nexus_engine — the Pybind11 bridge (bindings/, the C++<->Python seam)
# ============================================================================
if(NEXUS_BUILD_PYBIND)
    # Prefer a system / venv pybind11; otherwise fetch a pinned copy. Resolving
    # Python first lets pybind11 target the active interpreter (crucial inside a
    # WSL venv with multiple Pythons on PATH).
    find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

    find_package(pybind11 CONFIG QUIET)
    if(NOT pybind11_FOUND)
        message(STATUS "pybind11 not found on system — fetching v2.13.6 via FetchContent")
        include(FetchContent)
        FetchContent_Declare(pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG        v2.13.6
            GIT_SHALLOW    TRUE)
        FetchContent_MakeAvailable(pybind11)
    else()
        message(STATUS "pybind11: using system package ${pybind11_VERSION}")
    endif()

    # MODULE_NAME must match PYBIND11_MODULE(nexus_engine, m) in the source.
    pybind11_add_module(nexus_engine bindings/pybind_wrapper.cpp)
    target_link_libraries(nexus_engine PRIVATE nexus::engine)

    # Drop the compiled module into bindings/ so `import nexus_engine` resolves
    # for bindings/tests/test_abi_parity.py (pyproject adds bindings/ to
    # pytest's pythonpath). On Linux/WSL the .so is a LIBRARY artifact.
    set_target_properties(nexus_engine PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bindings"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bindings")
    message(STATUS "nexus_engine: Pybind11 module -> ${PROJECT_SOURCE_DIR}/bindings")

    # Harvest the compiled module into the wheel / install root as an importable
    # top-level module (scikit-build-core uses this for `pip install .`). Only
    # affects packaging — the direct `cmake --build` path above is unchanged.
    install(TARGETS nexus_engine
            LIBRARY DESTINATION .
            RUNTIME DESTINATION .)
endif()

# ============================================================================
#  4. CUDA risk engine — subsystem 3 (hooks only; stubbed until .cu lands)
# ============================================================================
# We only *enable the CUDA language and set flags* here — no .cu sources exist
# yet, so no target is created. When cuda_risk/*.cu arrives, uncomment the
# nexus_risk block below; the detection and flags are already in place.
if(NEXUS_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        find_package(CUDAToolkit)
    else()
        message(STATUS "CUDA: no nvcc on PATH — GPU risk engine (subsystem 3) skipped")
    endif()
endif()

if(CUDAToolkit_FOUND)
    message(STATUS "CUDA: toolkit ${CUDAToolkit_VERSION} found — enabling GPU hooks")

    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)

    # Target real hardware. `native` (CMake >= 3.24) matches the build GPU; older
    # CMake gets a sensible Volta..Ada default list.
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
            set(CMAKE_CUDA_ARCHITECTURES native)
        else()
            set(CMAKE_CUDA_ARCHITECTURES 70 75 80 86 89)
        endif()
    endif()

    # Device-code optimization flags. --use_fast_math is deliberately OFF: VaR/CVaR
    # want reproducible, IEEE-accurate reductions over 100k+ paths.
    set(NEXUS_CUDA_FLAGS $<$<COMPILE_LANGUAGE:CUDA>:-O3>)

    # --- Future GPU risk engine (uncomment when cuda_risk/*.cu exists) ----------
    # file(GLOB NEXUS_RISK_SOURCES CONFIGURE_DEPENDS
    #      "${PROJECT_SOURCE_DIR}/cuda_risk/*.cu")
    # add_library(nexus_risk STATIC ${NEXUS_RISK_SOURCES})
    # set_target_properties(nexus_risk PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
    # target_compile_options(nexus_risk PRIVATE ${NEXUS_CUDA_FLAGS})
    # target_link_libraries(nexus_risk PUBLIC CUDA::cudart nexus::engine)
    # ---------------------------------------------------------------------------
endif()

# ---- configuration summary -------------------------------------------------
message(STATUS "")
message(STATUS "Nexus-LOB configured:")
message(STATUS "  build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ tests     : ${NEXUS_BUILD_TESTS}")
message(STATUS "  pybind module : ${NEXUS_BUILD_PYBIND}")
message(STATUS "  CUDA hooks    : ${NEXUS_ENABLE_CUDA} (toolkit found: ${CUDAToolkit_FOUND})")
message(STATUS "  native arch   : ${NEXUS_NATIVE_ARCH}")
message(STATUS "")
