cmake_minimum_required(VERSION 3.21)

project(Viper-Browser VERSION 1.0.0)

set(VIPER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
option(VIPER_EMBEDDED "Build Viper core/UI libraries for the Orange Pi terminal" OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH "${VIPER_SOURCE_DIR}/cmake")

if(UNIX)
  include(GNUInstallDirs)
  set(VIPER_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/viper-browser/plugins")
else()
  set(VIPER_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/plugins")
endif()

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads)

# Get Qt
set(QT_VERSION_MIN "6.0.0")
find_package(Qt6 ${QT_VERSION_MIN} REQUIRED COMPONENTS Core Concurrent Gui Network PrintSupport Svg QuickWidgets Widgets WebChannel WebEngineWidgets)

if (ENABLE_TESTING)
    find_package(Qt6 ${QT_VERSION_MIN} REQUIRED COMPONENTS Test WebEngineCore)
endif()

# TODO: KF5Wallet incompatible with qt6. Need some version that is compatible with qt 6
option(DISABLE_KWALLET "Disable KWallet plugin" ON)
option(DISABLE_LIBSECRET "Force lib secret plugin not to be compiled." ON)

# Credential stores are optional desktop integrations, not terminal dependencies.
if(VIPER_EMBEDDED)
    set(DISABLE_KWALLET ON)
    set(DISABLE_LIBSECRET ON)
elseif(UNIX AND NOT APPLE)
    find_package(KF5Wallet)
    if (KF5Wallet_FOUND)
        message("-- Found KWallet")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Secret libsecret-1)
    if (Secret_FOUND)
        message("-- Found Secret")
    endif()
endif()

if(VIPER_EMBEDDED)
    add_library(viper-sqlite3 STATIC "${VIPER_SOURCE_DIR}/../sqlite-amalgamation/sqlite3.c")
    set_target_properties(viper-sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(viper-sqlite3 PUBLIC "${VIPER_SOURCE_DIR}/../sqlite-amalgamation")
    set(SQLite3_INCLUDE_DIR "${VIPER_SOURCE_DIR}/../sqlite-amalgamation")
    set(SQLite3_LIBRARY viper-sqlite3)
else()
    find_package(SQLite3)
endif()
include_directories(${SQLite3_INCLUDE_DIR})

configure_file("${VIPER_SOURCE_DIR}/src/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/config.h")

include_directories(
    ${VIPER_SOURCE_DIR}/src
    ${VIPER_SOURCE_DIR}/src/app
    ${VIPER_SOURCE_DIR}/src/core/adblock
    ${VIPER_SOURCE_DIR}/src/core/app
    ${VIPER_SOURCE_DIR}/src/core/autofill
    ${VIPER_SOURCE_DIR}/src/core/bookmarks
    ${VIPER_SOURCE_DIR}/src/core/cache
    ${VIPER_SOURCE_DIR}/src/core/cookies
    ${VIPER_SOURCE_DIR}/src/core/credentials
    ${VIPER_SOURCE_DIR}/src/core/database
    ${VIPER_SOURCE_DIR}/src/core/database/sqlite
    ${VIPER_SOURCE_DIR}/src/core/downloads
    ${VIPER_SOURCE_DIR}/src/core/extensions
    ${VIPER_SOURCE_DIR}/src/core/highlighters
    ${VIPER_SOURCE_DIR}/src/core/history
    ${VIPER_SOURCE_DIR}/src/core/icons
    ${VIPER_SOURCE_DIR}/src/core/ipc
    ${VIPER_SOURCE_DIR}/src/core/network
    ${VIPER_SOURCE_DIR}/src/core/search
    ${VIPER_SOURCE_DIR}/src/core/session
    ${VIPER_SOURCE_DIR}/src/core/settings
    ${VIPER_SOURCE_DIR}/src/core/text_finder
    ${VIPER_SOURCE_DIR}/src/core/threading
    ${VIPER_SOURCE_DIR}/src/core/url_suggestion
    ${VIPER_SOURCE_DIR}/src/core/user_agents
    ${VIPER_SOURCE_DIR}/src/core/user_scripts
    ${VIPER_SOURCE_DIR}/src/core/utility
    ${VIPER_SOURCE_DIR}/src/core/web
    ${VIPER_SOURCE_DIR}/src/ui/adblock
    ${VIPER_SOURCE_DIR}/src/ui/autofill
    ${VIPER_SOURCE_DIR}/src/ui/bookmarks
    ${VIPER_SOURCE_DIR}/src/ui/cookies
    ${VIPER_SOURCE_DIR}/src/ui/downloads
    ${VIPER_SOURCE_DIR}/src/ui/history
    ${VIPER_SOURCE_DIR}/src/ui/network
    ${VIPER_SOURCE_DIR}/src/ui/preferences
    ${VIPER_SOURCE_DIR}/src/ui/url_suggestion
    ${VIPER_SOURCE_DIR}/src/ui/user_agents
    ${VIPER_SOURCE_DIR}/src/ui/user_scripts
    ${VIPER_SOURCE_DIR}/src/ui/web
    ${VIPER_SOURCE_DIR}/src/ui/widgets
    ${VIPER_SOURCE_DIR}/src/ui/window
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

if(VIPER_EMBEDDED)
    add_subdirectory(src/core)
    add_subdirectory(src/ui)
else()
    add_subdirectory(src)
endif()

if (ENABLE_TESTING)
    enable_testing(true)
    add_subdirectory(tests)
endif()

if(UNIX AND NOT APPLE)
    install(FILES data/viper-browser.png DESTINATION share/icons/hicolor/64x64/apps)
    install(FILES data/viper-browser.desktop DESTINATION share/applications)
endif()
