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

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Dependencies (FetchContent)
# ---------------------------------------------------------------------------
include(FetchContent)

# Eigen — linear algebra (header-only; we only need the headers, so skip its
# tests/blas/lapack machinery to keep configure fast.)
FetchContent_Declare(
    Eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE
)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Eigen)

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
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.5.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
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
