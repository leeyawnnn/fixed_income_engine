cmake_minimum_required(VERSION 3.20)
project(fixed_income_engine
    VERSION 0.1.0
    DESCRIPTION "Fixed income analytics and yield curve construction engine"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(FI_BUILD_TESTS "Build the test suite" ON)
option(FI_BUILD_APPS  "Build the demo applications" ON)
option(FI_WERROR      "Treat compiler warnings as errors" OFF)
option(FI_OFFLINE     "Require system-installed dependencies; never fetch over the network" OFF)

if(MSVC)
    add_compile_options(/W4)
    if(FI_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(FI_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Dependencies (FetchContent)
# ---------------------------------------------------------------------------
include(FetchContent)

# Dependencies resolve in two steps: use a system-installed copy when one is
# present, otherwise download a *pinned release tarball* verified by SHA-256.
#
# Pinned tarballs rather than `GIT_REPOSITORY`: a git clone at configure time
# needs the git protocol to a specific host, which fails behind restrictive
# corporate proxies and in locked-down CI networks, and a tag is mutable while
# a content hash is not. FI_OFFLINE=ON forbids the download path entirely and
# requires the dependency to already be installed.

# --- Eigen: header-only linear algebra (the NSS normal equations). ----------
# Eigen is hosted on GitLab and has no GitHub mirror (verified 2026-09-18:
# https://github.com/libeigen/eigen returns 404), so the GitLab release tarball
# is the only pinnable official source.
#
# NO_*_PACKAGE_REGISTRY is not optional here: building Eigen through
# FetchContent runs `export(PACKAGE Eigen3)`, which writes the *build tree*
# path into ~/.cmake/packages/Eigen3. Every later find_package(Eigen3) on that
# machine then resolves to some other project's build directory, which may
# since have been deleted. Only real installed locations are acceptable.
find_package(Eigen3 3.3 QUIET NO_MODULE
             NO_CMAKE_PACKAGE_REGISTRY NO_CMAKE_SYSTEM_PACKAGE_REGISTRY)
if(TARGET Eigen3::Eigen)
    message(STATUS "fi: using system Eigen3 ${Eigen3_VERSION}")
elseif(FI_OFFLINE)
    message(FATAL_ERROR
        "fi: FI_OFFLINE=ON but Eigen3 was not found. Install it first "
        "(apt: libeigen3-dev, brew: eigen, dnf: eigen3-devel) or re-run with "
        "-DFI_OFFLINE=OFF to fetch a pinned tarball.")
else()
    message(STATUS "fi: system Eigen3 not found; fetching pinned 3.4.0 tarball")
    set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    # Stop Eigen's own CMake writing this build tree into the user package
    # registry and leaking it into unrelated projects on this machine.
    set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
    FetchContent_Declare(
        Eigen3
        URL      https://gitlab.com/libeigen/eigen/-/archive/3.4.0/eigen-3.4.0.tar.gz
        URL_HASH SHA256=8586084f71f9bde545ee7fa6d00288b264a2b7ac3607b974e54d13e7162c1c72
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Eigen3)
endif()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(fi
    src/placeholder.cpp
    src/date.cpp
    src/day_count.cpp
    src/bond.cpp
    src/ytm_solver.cpp
    src/risk.cpp
    src/curve.cpp
    src/bootstrap.cpp
    src/nss.cpp
    src/swap.cpp
    src/scenario.cpp
)
target_include_directories(fi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(fi PUBLIC Eigen3::Eigen)
target_compile_features(fi PUBLIC cxx_std_20)
add_library(fi::fi ALIAS fi)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FI_BUILD_TESTS)
    find_package(Catch2 3 QUIET
                 NO_CMAKE_PACKAGE_REGISTRY NO_CMAKE_SYSTEM_PACKAGE_REGISTRY)
    if(TARGET Catch2::Catch2WithMain)
        message(STATUS "fi: using system Catch2 ${Catch2_VERSION}")
    elseif(FI_OFFLINE)
        message(FATAL_ERROR
            "fi: FI_OFFLINE=ON but Catch2 v3 was not found. Install it first "
            "(brew: catch2, vcpkg/conan: catch2) or configure with "
            "-DFI_BUILD_TESTS=OFF.")
    else()
        message(STATUS "fi: system Catch2 not found; fetching pinned v3.5.4 tarball")
        FetchContent_Declare(
            Catch2
            URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.4.tar.gz
            URL_HASH SHA256=b7754b711242c167d8f60b890695347f90a1ebc95949a045385114165d606dbb
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    endif()

    enable_testing()
    include(CTest)
    include(Catch)

    add_executable(fi_tests
        tests/test_smoke.cpp
        tests/test_date.cpp
        tests/test_day_count.cpp
        tests/test_bond.cpp
        tests/test_ytm.cpp
        tests/test_risk.cpp
        tests/test_curve.cpp
        tests/test_bootstrap.cpp
        tests/test_nss.cpp
        tests/test_swap.cpp
        tests/test_sensitivity.cpp
        tests/test_scenario.cpp
        tests/test_json.cpp
    )
    target_link_libraries(fi_tests PRIVATE fi::fi Catch2::Catch2WithMain)
    target_compile_definitions(fi_tests PRIVATE
        FI_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    catch_discover_tests(fi_tests)
endif()

# ---------------------------------------------------------------------------
# Apps
# ---------------------------------------------------------------------------
if(FI_BUILD_APPS)
    add_executable(curve_demo apps/curve_demo.cpp)
    target_link_libraries(curve_demo PRIVATE fi::fi)
endif()
