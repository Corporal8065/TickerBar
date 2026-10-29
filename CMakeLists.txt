cmake_minimum_required(VERSION 3.22)
project(TickerBar VERSION 0.1.1 LANGUAGES C CXX)
# Static MSVC runtime (/MT) so a built TickerBar.exe runs on a friend's
# PC with NO Visual C++ Redistributable installed (MeterBar v0.15 rule).
# Must be set before any target is created (CMP0091). MSVC-only.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(TICKERBAR_BUILD_APP "Build the JUCE GUI app" OFF)

# ---- Pure core: market-data math/parsing, zero external deps ----
add_library(tickerbar_core INTERFACE)
target_include_directories(tickerbar_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
if(UNIX AND NOT APPLE)
  target_link_libraries(tickerbar_core INTERFACE m)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE tickerbar_core)
target_compile_definitions(test_core PRIVATE
  FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME core COMMAND test_core)

# ---- JUCE app ----
if(TICKERBAR_BUILD_APP)
  include(FetchContent)
  # URL (not git): the Windows build PC has no git installed; CMake can
  # download+extract a release archive itself (MeterBar kit fix v0.5).
  FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(JUCE)
  add_subdirectory(app)
endif()
