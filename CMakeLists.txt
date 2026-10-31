cmake_minimum_required(VERSION 3.22)
# v0.5.3: THIS project() VERSION IS THE SINGLE SOURCE OF TRUTH for the
# app version. It feeds the generated TickerBarVersion.h (UI string,
# application version, updater), the juce_add_gui_app version in
# app/CMakeLists.txt, and — via build-release.bat / CI passing ISCC
# /DMyAppVersion — the installer. Before v0.5.3 the version was ALSO
# hard-coded in Main.cpp / MainComponent.cpp, where the literals went
# stale at 0.4.0 and every later build reported 0.4.0 to the updater
# (Fiona's stuck-on-0.4.0 report); do not add another copy. Bump it
# here, only here. (MeterBar made this move at its v0.19.)
project(TickerBar VERSION 0.5.7 LANGUAGES C CXX)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app/TickerBarVersion.h.in
               ${CMAKE_BINARY_DIR}/generated/TickerBarVersion.h)
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
target_include_directories(test_core PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(test_core PRIVATE
  FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
  EXPECTED_PROJECT_VERSION="${PROJECT_VERSION}")
add_test(NAME core COMMAND test_core)
# v0.5.3: guard against the stale-literal class — fails if a bare
# version literal reappears in the app TUs or any version carrier
# drifts from the project() VERSION above (see the script).
add_test(NAME version_guard COMMAND ${CMAKE_COMMAND}
  -DGUARD_APP=TickerBar -DGUARD_ISS=TickerBar.iss -DGUARD_HEADER=TickerBarVersion.h
  -DAPP_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DAPP_BINARY_DIR=${CMAKE_BINARY_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/version_guard.cmake)

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
