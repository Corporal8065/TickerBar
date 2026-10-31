# Version-source guard — the stale-literal class (TickerBar v0.5.3,
# SessionBar v0.4.3; the file is identical in both repos, parametrised
# by the -D values in the add_test call. MeterBar never had the bug:
# it single-sourced its version at v0.19, the pattern now ported.)
#
# The bug this guards: the app's current-version string was a
# hard-coded literal in app/Source/Main.cpp (getApplicationVersion)
# and app/Source/MainComponent.cpp (kVersionNum, fed to the updater).
# The literals were never bumped after TickerBar v0.4.0 / SessionBar
# v0.3.0, so every later build reported the OLD version to the
# updater and perpetually offered the newest release — Fiona's
# "stuck on 0.4.0" report. Her manual install of the new Setup.exe
# had actually worked; the app was lying about its own version.
#
# This guard fails the test run if:
#   1. a bare semver string literal is ASSIGNED or RETURNED in
#      app/Source/Main.cpp / app/Source/MainComponent.cpp
#      (= "0.4.0", = "v0.4.0", return "0.4.0" — the exact
#      stale-literal shapes; historical // v0.4.0: comments do not
#      match and are fine);
#   2. any version carrier disagrees with the single source of
#      truth, the root project() VERSION: the app/CMakeLists.txt
#      gui-app VERSION and JUCE_APPLICATION_VERSION_STRING (either
#      may reference PROJECT_VERSION, which agrees by construction),
#      the installer .iss fallback define, or the generated version
#      header in the build tree.
#
# Invoked by ctest (test name: version_guard) with -DGUARD_APP,
# -DGUARD_ISS, -DGUARD_HEADER, -DAPP_SOURCE_DIR, -DAPP_BINARY_DIR.

foreach(var GUARD_APP GUARD_ISS GUARD_HEADER APP_SOURCE_DIR APP_BINARY_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "version_guard: -D${var} not set (this script is meant to run via the version_guard ctest entry)")
  endif()
endforeach()

# ---- the single source of truth: root project() VERSION ----
file(READ "${APP_SOURCE_DIR}/CMakeLists.txt" _root_cmake)
string(REGEX MATCH "project\\(${GUARD_APP} VERSION ([0-9]+\\.[0-9]+\\.[0-9]+)" _hit "${_root_cmake}")
if(NOT _hit)
  message(FATAL_ERROR "version_guard: could not parse project() VERSION from ${APP_SOURCE_DIR}/CMakeLists.txt")
endif()
set(project_version "${CMAKE_MATCH_1}")

# ---- 1. no bare semver literals assigned/returned in the app TUs ----
foreach(src "app/Source/Main.cpp" "app/Source/MainComponent.cpp")
  file(READ "${APP_SOURCE_DIR}/${src}" _text)
  string(REGEX MATCH "(=|return)[ \t]*\"v?[0-9]+\\.[0-9]+\\.[0-9]+\"" _bad "${_text}")
  if(_bad)
    message(FATAL_ERROR "version_guard: bare version literal '${CMAKE_MATCH_0}' in ${src} — version strings come from the generated ${GUARD_HEADER} (root project VERSION); literals go stale (the stuck-on-0.4.0 bug)")
  endif()
endforeach()

# ---- 2a. app/CMakeLists.txt carriers ----
file(READ "${APP_SOURCE_DIR}/app/CMakeLists.txt" _app_cmake)
string(REGEX MATCH "juce_add_gui_app\\(${GUARD_APP} [^)]*VERSION \"([^\"]+)\"" _hit "${_app_cmake}")
if(NOT _hit)
  message(FATAL_ERROR "version_guard: could not parse juce_add_gui_app VERSION from app/CMakeLists.txt")
endif()
set(_gui_version "${CMAKE_MATCH_1}")
string(FIND "${_gui_version}" "PROJECT_VERSION" _by_construction)
if(_by_construction EQUAL -1 AND NOT _gui_version STREQUAL project_version)
  message(FATAL_ERROR "version_guard: juce_add_gui_app VERSION is '${_gui_version}' but root project VERSION is '${project_version}' — carriers must agree")
endif()
string(REGEX MATCH "JUCE_APPLICATION_VERSION_STRING=\"([^\"]+)\"" _hit "${_app_cmake}")
if(NOT _hit)
  message(FATAL_ERROR "version_guard: could not parse JUCE_APPLICATION_VERSION_STRING from app/CMakeLists.txt")
endif()
set(_juce_version "${CMAKE_MATCH_1}")
string(FIND "${_juce_version}" "PROJECT_VERSION" _by_construction)
if(_by_construction EQUAL -1 AND NOT _juce_version STREQUAL project_version)
  message(FATAL_ERROR "version_guard: JUCE_APPLICATION_VERSION_STRING is '${_juce_version}' but root project VERSION is '${project_version}' — carriers must agree")
endif()

# ---- 2b. installer .iss fallback define ----
file(READ "${APP_SOURCE_DIR}/${GUARD_ISS}" _iss)
string(REGEX MATCH "#define MyAppVersion \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _hit "${_iss}")
if(NOT _hit)
  message(FATAL_ERROR "version_guard: could not parse the MyAppVersion fallback define from ${GUARD_ISS}")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL project_version)
  message(FATAL_ERROR "version_guard: ${GUARD_ISS} fallback MyAppVersion is '${CMAKE_MATCH_1}' but root project VERSION is '${project_version}' — carriers must agree")
endif()

# ---- 2c. the generated header in the build tree ----
set(_generated "${APP_BINARY_DIR}/generated/${GUARD_HEADER}")
if(NOT EXISTS "${_generated}")
  message(FATAL_ERROR "version_guard: generated header ${_generated} missing — configure the build first (it is produced by the root configure_file)")
endif()
file(READ "${_generated}" _gen)
string(REGEX MATCH "_VERSION_STRING \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _hit "${_gen}")
if(NOT _hit)
  message(FATAL_ERROR "version_guard: could not parse the version string from ${_generated}")
endif()
if(NOT CMAKE_MATCH_1 STREQUAL project_version)
  message(FATAL_ERROR "version_guard: generated ${GUARD_HEADER} says '${CMAKE_MATCH_1}' but root project VERSION is '${project_version}' — reconfigure the build")
endif()

message(STATUS "version_guard: all ${GUARD_APP} version carriers agree on ${project_version}; no bare version literals in Main.cpp / MainComponent.cpp")
