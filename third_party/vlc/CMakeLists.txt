cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(terminal_vlc LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_AUTOMOC ON)
    include(CTest)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Quick Qml)
endif()

# Use a target-platform SDK, never a downloaded host binary. CMake's root-path
# rules also apply to these searches when building for a Raspberry Pi sysroot.
set(VLC_ROOT "" CACHE PATH "Target VLC 3.x SDK prefix (include/vlc and lib)")
if(CMAKE_CROSSCOMPILING AND
   (NOT CMAKE_FIND_ROOT_PATH_MODE_LIBRARY STREQUAL "ONLY" OR
    NOT CMAKE_FIND_ROOT_PATH_MODE_INCLUDE STREQUAL "ONLY"))
    message(FATAL_ERROR "VLC cross builds require CMAKE_FIND_ROOT_PATH_MODE_LIBRARY/INCLUDE=ONLY to avoid linking host libraries")
endif()
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND AND NOT VLC_ROOT)
    pkg_check_modules(PC_LIBVLC QUIET libvlc>=3.0 libvlc<4.0)
endif()
find_path(LIBVLC_INCLUDE_DIR vlc/vlc.h
    HINTS "${VLC_ROOT}/include" "${VLC_ROOT}/sdk/include" ${PC_LIBVLC_INCLUDE_DIRS})
find_library(LIBVLC_LIBRARY NAMES vlc libvlc
    HINTS "${VLC_ROOT}/lib" "${VLC_ROOT}/sdk/lib" ${PC_LIBVLC_LIBRARY_DIRS})
if(NOT LIBVLC_INCLUDE_DIR OR NOT LIBVLC_LIBRARY)
    message(FATAL_ERROR "VLC 3.x SDK not found. Install libvlc-dev, or set VLC_ROOT to the target SDK. See third_party/vlc/README.md")
endif()
if(NOT TARGET VLC::VLC)
    add_library(VLC::VLC UNKNOWN IMPORTED)
    set_target_properties(VLC::VLC PROPERTIES
        IMPORTED_LOCATION "${LIBVLC_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBVLC_INCLUDE_DIR}")
endif()
add_library(terminal_vlc STATIC VlcVideoItem.cpp VlcVideoItem.h)
set_target_properties(terminal_vlc PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
target_compile_features(terminal_vlc PUBLIC cxx_std_17)
target_include_directories(terminal_vlc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(terminal_vlc PUBLIC Qt6::Quick Qt6::Qml PRIVATE VLC::VLC)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
