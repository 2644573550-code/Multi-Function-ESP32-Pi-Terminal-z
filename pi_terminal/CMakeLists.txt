cmake_minimum_required(VERSION 3.21)
project(orange_pi_terminal VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(GNUInstallDirs)
option(TERMINAL_REPAIR_AUDIT "Enable explicit hardware screenshot regression harness" OFF)
add_library(pi_module_sdk INTERFACE)
add_library(OrangePi::ModuleSDK ALIAS pi_module_sdk)
set_target_properties(pi_module_sdk PROPERTIES EXPORT_NAME ModuleSDK)
target_include_directories(pi_module_sdk INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/modules/api> $<INSTALL_INTERFACE:include/orange-pi-terminal>)
target_compile_features(pi_module_sdk INTERFACE cxx_std_17)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Bluetooth Qml Quick QuickControls2 SerialPort OpenGL WebEngineQuick WebEngineWidgets WebEngineCore QuickWidgets WebChannel Concurrent Svg PrintSupport Multimedia TextToSpeech StateMachine)
find_package(Qt6 6.4 REQUIRED COMPONENTS ShaderTools)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/vlc
                 ${CMAKE_CURRENT_BINARY_DIR}/third_party/vlc)
set(VIPER_EMBEDDED ON CACHE BOOL "Build the Viper browser engine as embedded terminal components" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third_party/Viper-Browser
                 ${CMAKE_CURRENT_BINARY_DIR}/third_party/Viper-Browser)
if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()
add_library(terminal_protocol STATIC
    src/protocol/pibridge_protocol.cpp
    src/protocol/pibridge_protocol.h
    src/protocol/data_checksum.h)
target_include_directories(terminal_protocol PUBLIC src)
target_link_libraries(terminal_protocol PUBLIC Qt6::Core)
target_compile_options(terminal_protocol PRIVATE -Wall -Wextra -Wpedantic)

add_library(terminal_transports STATIC
    src/transport/TransportBase.h
    src/transport/PogoControlTransport.cpp src/transport/PogoControlTransport.h
    src/transport/BleTransport.cpp src/transport/BleTransport.h
    src/transport/HotspotLanTransport.cpp src/transport/HotspotLanTransport.h)
target_include_directories(terminal_transports PUBLIC src)
target_link_libraries(terminal_transports PUBLIC terminal_protocol Qt6::Core Qt6::Network Qt6::Bluetooth Qt6::SerialPort)
target_compile_options(terminal_transports PRIVATE -Wall -Wextra -Wpedantic)

add_library(terminal_core STATIC
    src/core/ModuleManager.cpp src/core/ModuleManager.h
    src/core/ConnectionManager.cpp src/core/ConnectionManager.h
    src/core/HostEventDetector.h
    src/core/BridgeConnectionMonitor.h
    src/core/ModuleCardModel.cpp src/core/ModuleCardModel.h
    src/core/PageProvider.cpp src/core/PageProvider.h
    src/core/SessionStateMachine.cpp src/core/SessionStateMachine.h
    src/core/PackageReceiver.cpp src/core/PackageReceiver.h)
target_include_directories(terminal_core PUBLIC src src/modules/api)
target_link_libraries(terminal_core PUBLIC terminal_transports Qt6::Core Qt6::Network Qt6::StateMachine)
target_compile_definitions(terminal_core PRIVATE PI_PACKAGE_TOOL="${CMAKE_INSTALL_PREFIX}/libexec/orange-pi-terminal/package_tool.py")
target_compile_options(terminal_core PRIVATE -Wall -Wextra -Wpedantic)
add_library(terminal_music SHARED
    src/ui/MusicPlayback.cpp src/ui/MusicPlayback.h
    src/ui/MusicService.cpp src/ui/MusicService.h)
target_include_directories(terminal_music PUBLIC src)
target_link_libraries(terminal_music PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(terminal_music PRIVATE TERMINAL_MUSIC_API_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/music_api")

add_library(terminal_system SHARED
    src/ui/SystemTools.cpp src/ui/SystemTools.h
    src/ui/SystemVolume.cpp src/ui/SystemVolume.h
    src/ui/RemovableStorage.cpp src/ui/RemovableStorage.h
    src/ui/DesktopShell.cpp src/ui/DesktopShell.h
    src/ui/ViperBrowserController.cpp src/ui/ViperBrowserController.h)
target_include_directories(terminal_system PUBLIC src)
target_include_directories(terminal_system PRIVATE
    ../third_party/Viper-Browser/src/core
    ../third_party/Viper-Browser/src
    ../third_party/Viper-Browser/src/core
    ../third_party/Viper-Browser/src/core/adblock
    ../third_party/Viper-Browser/src/core/app
    ../third_party/Viper-Browser/src/core/autofill
    ../third_party/Viper-Browser/src/core/bookmarks
    ../third_party/Viper-Browser/src/core/cache
    ../third_party/Viper-Browser/src/core/cookies
    ../third_party/Viper-Browser/src/core/credentials
    ../third_party/Viper-Browser/src/core/database
    ../third_party/Viper-Browser/src/core/database/sqlite
    ../third_party/Viper-Browser/src/core/downloads
    ../third_party/Viper-Browser/src/core/extensions
    ../third_party/Viper-Browser/src/core/highlighters
    ../third_party/Viper-Browser/src/core/history
    ../third_party/Viper-Browser/src/core/icons
    ../third_party/Viper-Browser/src/core/ipc
    ../third_party/Viper-Browser/src/core/network
    ../third_party/Viper-Browser/src/core/search
    ../third_party/Viper-Browser/src/core/session
    ../third_party/Viper-Browser/src/core/settings
    ../third_party/Viper-Browser/src/core/text_finder
    ../third_party/Viper-Browser/src/core/threading
    ../third_party/Viper-Browser/src/core/url_suggestion
    ../third_party/Viper-Browser/src/core/user_agents
    ../third_party/Viper-Browser/src/core/user_scripts
    ../third_party/Viper-Browser/src/core/utility
    ../third_party/Viper-Browser/src/core/web
    ../third_party/Viper-Browser/src/ui
    ../third_party/Viper-Browser/src/ui/adblock
    ../third_party/Viper-Browser/src/ui/autofill
    ../third_party/Viper-Browser/src/ui/bookmarks
    ../third_party/Viper-Browser/src/ui/cookies
    ../third_party/Viper-Browser/src/ui/downloads)
target_link_libraries(terminal_system PUBLIC terminal_music Qt6::Gui Qt6::Bluetooth Qt6::TextToSpeech Qt6::WebEngineQuick
    PRIVATE Qt6::Widgets Qt6::WebEngineWidgets viper-core viper-ui)

qt_add_library(terminal_ui SHARED
    src/ui/TerminalUi.cpp src/ui/TerminalUi.h
    src/ui/BlurItem.cpp src/ui/BlurItem.h src/ui/TextFormatter.h
    src/ui/PetSpriteItem.cpp src/ui/PetSpriteItem.h
    src/ui/PetLibrary.cpp src/ui/PetLibrary.h
    src/ui/GstVideoItem.cpp src/ui/GstVideoItem.h)
target_compile_definitions(terminal_ui PRIVATE ARKPETS_CONVERTER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/tools/arkpets")
target_include_directories(terminal_ui PUBLIC src)
target_link_libraries(terminal_ui PUBLIC Qt6::Quick Qt6::Qml PRIVATE terminal_vlc Qt6::QuickControls2 Qt6::OpenGL)

# ── 自研 GStreamer 播放内核 ──
# 开发头文件随仓库提供（third_party/gst/<arch>，由 tools/fetch_gst_headers.sh 生成），
# 不需要在新机器上安装 *-dev 包；运行时用系统已有的 GStreamer（Qt Multimedia 本来就依赖它）。
if(CMAKE_CROSSCOMPILING)
    set(GST_ROOT "${CMAKE_SYSROOT}/usr")
    set(GST_GLIB_ARCH "${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu/glib-2.0/include")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(GST_ARCH arm64)
    else()
        set(GST_ARCH amd64)
    endif()
    set(GST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/gst/${GST_ARCH}")
    set(GST_GLIB_ARCH "${GST_ROOT}/include/glib-arch")
    if(NOT EXISTS "${GST_ROOT}/include/glib-arch/glibconfig.h")
        message(FATAL_ERROR
            "缺少 ${GST_ROOT}。请执行 `${CMAKE_CURRENT_SOURCE_DIR}/../tools/fetch_gst_headers.sh` 生成（免 root）。")
    endif()
    target_link_directories(terminal_ui PRIVATE "${GST_ROOT}/lib")
endif()
target_include_directories(terminal_ui PUBLIC
    "${GST_ROOT}/include/gstreamer-1.0"
    "${GST_ROOT}/include/glib-2.0"
    "${GST_GLIB_ARCH}")
target_link_libraries(terminal_ui PRIVATE gstreamer-1.0 gstapp-1.0 gstvideo-1.0 gobject-2.0 glib-2.0)

# 最小视频探针（开发/性能测试用，不参与安装）
qt_add_executable(tv_probe tools/tv_probe.cpp)
target_link_libraries(tv_probe PRIVATE terminal_ui Qt6::Quick)


qt_add_executable(orange_pi_terminal
    src/main.cpp src/app/TerminalApplication.cpp src/app/TerminalApplication.h
    src/app/TerminalOptions.cpp src/app/TerminalOptions.h
    src/app/TerminalRuntime.cpp src/app/TerminalRuntime.h
    src/app/DiagnosticRunner.cpp src/app/DiagnosticRunner.h)
target_include_directories(orange_pi_terminal PRIVATE ../third_party/Viper-Browser/src/core/network)

set(TERMINAL_LIBRARY_DIR "${CMAKE_INSTALL_LIBDIR}/orange-pi-terminal")
foreach(module IN ITEMS terminal_music terminal_system terminal_ui)
    set_target_properties(${module} PROPERTIES
        VERSION ${PROJECT_VERSION} SOVERSION 1
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/lib/orange-pi-terminal"
        BUILD_RPATH_USE_ORIGIN TRUE INSTALL_RPATH "$ORIGIN")
endforeach()
file(RELATIVE_PATH TERMINAL_LIBRARY_FROM_BIN "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}/orange-pi-terminal")
set_target_properties(orange_pi_terminal PROPERTIES
    BUILD_RPATH_USE_ORIGIN TRUE INSTALL_RPATH "$ORIGIN/${TERMINAL_LIBRARY_FROM_BIN}")
set(UI_QML_FILES qml/Main.qml qml/Theme.qml qml/I18n.qml qml/GlassSurface.qml qml/CarText.qml qml/CarButton.qml qml/CarField.qml qml/MaterialIcon.qml qml/IconButton.qml qml/AppIcon.qml qml/PaintToolButton.qml qml/AppLauncher.qml qml/ApplicationDrawer.qml qml/StatusDock.qml qml/SettingsGroup.qml qml/FloatingKeyboard.qml qml/MusicPlayer.qml qml/VideoPlayer.qml)
list(APPEND UI_QML_FILES qml/MusicModeButton.qml)
list(APPEND UI_QML_FILES qml/BrowserJoystick.qml)
set_source_files_properties(qml/BrowserScroll.js PROPERTIES QT_RESOURCE_ALIAS BrowserScroll.js)
list(APPEND UI_QML_FILES qml/MusicText.qml)
list(APPEND UI_QML_FILES qml/PictureViewer.qml qml/FileThumbnail.qml)
list(APPEND UI_QML_FILES qml/QuickToggle.qml qml/EditorToolbar.qml)
list(APPEND UI_QML_FILES qml/QuickPanelTile.qml)
list(APPEND UI_QML_FILES qml/KeyboardKey.qml)
list(APPEND UI_QML_FILES qml/BlurCache.qml)
list(APPEND UI_QML_FILES qml/CachedBlur.qml)
list(APPEND UI_QML_FILES qml/StartupSplash.qml)
list(APPEND UI_QML_FILES qml/DesktopPet.qml)
list(APPEND UI_QML_FILES qml/PetManager.qml)
list(APPEND UI_QML_FILES qml/TvPlayer.qml)
foreach(qml_file IN LISTS UI_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/I18n.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(terminal_ui URI OrangePiTerminal NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/OrangePiTerminal"
    RESOURCE_PREFIX /qt/qml VERSION 1.0 QML_FILES ${UI_QML_FILES} qml/BrowserScroll.js)
file(GLOB MATERIAL_ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/qml/icons/material/dark/*.svg" "${CMAKE_CURRENT_SOURCE_DIR}/qml/icons/material/light/*.svg")
set_source_files_properties(assets/fonts/GoogleSans.ttf PROPERTIES QT_RESOURCE_ALIAS GoogleSans.ttf)
set_source_files_properties(assets/fonts/OFL.txt PROPERTIES QT_RESOURCE_ALIAS OFL.txt)
set_source_files_properties(assets/fonts/TRADEMARKS.md PROPERTIES QT_RESOURCE_ALIAS TRADEMARKS.md)
qt_add_resources(terminal_ui icons PREFIX /icons BASE qml/icons FILES qml/icons/extension.svg qml/icons/eraser.svg qml/icons/module-default.svg qml/icons/browser-new-tab.svg ${MATERIAL_ICONS})
qt_add_resources(terminal_ui google_sans PREFIX /fonts FILES assets/fonts/GoogleSans.ttf assets/fonts/OFL.txt assets/fonts/TRADEMARKS.md)
qt_add_resources(terminal_system pinyin_dictionary PREFIX /pinyin BASE assets/pinyin FILES assets/pinyin/pinyin_simp.dict.yaml assets/pinyin/phrases.tsv assets/pinyin/LICENSE assets/pinyin/AUTHORS assets/pinyin/PYPINYIN-LICENSE assets/pinyin/SOURCES.md)
qt_add_resources(terminal_system pinyin_frequency PREFIX /pinyin BASE assets/pinyin FILES assets/pinyin/jieba-dict.txt assets/pinyin/JIEBA-LICENSE)
set_source_files_properties(assets/orange_pi_terminal.svg PROPERTIES QT_RESOURCE_ALIAS orange_pi_terminal.svg)
qt_add_resources(terminal_ui app_icon PREFIX /icons FILES assets/orange_pi_terminal.svg)
qt_add_resources(terminal_ui tv_channels PREFIX /tv BASE assets/tv FILES assets/tv/china.json assets/tv/SOURCE.md)
qt_add_resources(terminal_ui arkpets_sprites PREFIX /arkpets BASE assets/arkpets
    FILES assets/arkpets/amiya/idle.png assets/arkpets/amiya/walk.png
          assets/arkpets/amiya/interact.png assets/arkpets/amiya/sit.png
          assets/arkpets/amiya/sleep.png assets/arkpets/amiya/animations.json)
qt_add_shaders(terminal_ui cached_blur_shader PREFIX /shaders BASE qml/shaders
    GLSL "100 es,120,150" FILES qml/shaders/cached-blur.frag)
target_link_libraries(orange_pi_terminal PRIVATE terminal_core terminal_system terminal_ui viper-core Qt6::Widgets Qt6::QuickControls2 Qt6::OpenGL Qt6::WebEngineQuick)
if(TERMINAL_REPAIR_AUDIT)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    target_link_libraries(orange_pi_terminal PRIVATE Qt6::Test)
    target_compile_definitions(orange_pi_terminal PRIVATE TERMINAL_REPAIR_AUDIT)
endif()
add_subdirectory(modules/example)
add_subdirectory(modules/test_devices)
add_subdirectory(tools/mock_pogo_control)
if(BUILD_TESTING)
    target_sources(orange_pi_terminal PRIVATE tests/GuiAudit.cpp)
    add_executable(arkpets_preview tests/arkpets_preview.cpp)
    target_link_libraries(arkpets_preview PRIVATE terminal_ui Qt6::QuickControls2)
    target_compile_definitions(arkpets_preview PRIVATE ARKPETS_PREVIEW_QML="${CMAKE_CURRENT_SOURCE_DIR}/tests/arkpets_preview.qml")
    add_executable(desktop_pet_test tests/desktop_pet_test.cpp)
    target_link_libraries(desktop_pet_test PRIVATE terminal_ui Qt6::Test Qt6::QuickControls2 Qt6::Widgets)
    target_compile_definitions(desktop_pet_test PRIVATE ARKPETS_PREVIEW_QML="${CMAKE_CURRENT_SOURCE_DIR}/tests/arkpets_preview.qml")
    add_test(NAME arkpets_local_interaction COMMAND desktop_pet_test)
    add_executable(pet_manager_test tests/pet_manager_test.cpp)
    target_link_libraries(pet_manager_test PRIVATE terminal_ui terminal_system Qt6::Test Qt6::QuickControls2 Qt6::Widgets)
    target_compile_definitions(pet_manager_test PRIVATE ARKPETS_PREVIEW_QML="${CMAKE_CURRENT_SOURCE_DIR}/tests/arkpets_preview.qml")
    add_test(NAME arkpets_manager_material_list_detail COMMAND pet_manager_test)
    set_tests_properties(arkpets_manager_material_list_detail PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
    set_tests_properties(arkpets_local_interaction PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    add_executable(text_formatter_test tests/text_formatter_test.cpp)
    target_include_directories(text_formatter_test PRIVATE src)
    target_link_libraries(text_formatter_test PRIVATE terminal_ui Qt6::Test)
    add_test(NAME text_editor_format_roundtrip COMMAND text_formatter_test)
    set_tests_properties(text_editor_format_roundtrip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    target_include_directories(orange_pi_terminal PRIVATE tests)
    target_link_libraries(orange_pi_terminal PRIVATE Qt6::Test)
    target_compile_definitions(orange_pi_terminal PRIVATE TERMINAL_GUI_AUDIT)
    add_executable(protocol_test tests/protocol_test.cpp)
    target_link_libraries(protocol_test PRIVATE terminal_protocol)
    add_test(NAME pibridge_protocol COMMAND protocol_test)
    add_executable(session_test tests/session_test.cpp)
    add_dependencies(session_test example_module)
    target_link_libraries(session_test PRIVATE terminal_core Qt6::Test)
    add_executable(runtime_test tests/runtime_test.cpp)
    add_executable(music_playback_test tests/music_playback_test.cpp)
    target_include_directories(music_playback_test PRIVATE src)
    target_link_libraries(music_playback_test PRIVATE terminal_music Qt6::Test)
    add_test(NAME music_playback_transactions COMMAND music_playback_test)
    add_executable(music_service_test tests/music_service_test.cpp)
    target_include_directories(music_service_test PRIVATE src)
    target_link_libraries(music_service_test PRIVATE terminal_music Qt6::Test)
    add_test(NAME music_native_api_requests COMMAND music_service_test)
    add_executable(music_seek_test tests/music_seek_test.cpp)
    target_include_directories(music_seek_test PRIVATE src)
    target_link_libraries(music_seek_test PRIVATE terminal_system terminal_music terminal_ui Qt6::Test Qt6::QuickControls2 Qt6::Multimedia)
    add_test(NAME music_http_seek_clock COMMAND music_seek_test)
    set_tests_properties(music_http_seek_clock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    target_link_libraries(runtime_test PRIVATE terminal_core terminal_system Qt6::Test)
    add_test(NAME runtime_models_state_files COMMAND runtime_test)
    add_executable(plugin_scale_test tests/plugin_scale_test.cpp)
    add_executable(startup_splash_test tests/startup_splash_test.cpp)
    target_link_libraries(startup_splash_test PRIVATE terminal_ui Qt6::Test Qt6::QuickControls2 Qt6::Widgets)
    add_test(NAME startup_splash_lifecycle COMMAND startup_splash_test)
    add_executable(video_player_ui_test tests/video_player_ui_test.cpp)
    target_link_libraries(video_player_ui_test PRIVATE terminal_ui Qt6::Test Qt6::QuickControls2)
    add_test(NAME media_video_player_interface COMMAND video_player_ui_test)
    set_tests_properties(media_video_player_interface PROPERTIES LABELS "interface"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    set_tests_properties(startup_splash_lifecycle PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    target_link_libraries(plugin_scale_test PRIVATE terminal_core Qt6::Test Qt6::Qml Qt6::Quick Qt6::Widgets)
    target_compile_definitions(plugin_scale_test PRIVATE PLUGIN_QML_PATH="${CMAKE_CURRENT_SOURCE_DIR}/modules/example/qml/ExamplePage.qml")
    add_test(NAME plugin_scale_geometry COMMAND plugin_scale_test)
    set_tests_properties(plugin_scale_geometry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME arkpets_worker_safety COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/character_worker_test.py)
    set_tests_properties(arkpets_worker_safety PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME arkpets_sprite_packing COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/pack_sprites_test.py)
    set_tests_properties(arkpets_sprite_packing PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    if(UNIX AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME shared_runtime_install
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_runtime_install.py
                    ${CMAKE_COMMAND} ${CMAKE_CURRENT_BINARY_DIR} ${TERMINAL_LIBRARY_DIR})
        set_tests_properties(shared_runtime_install PROPERTIES TIMEOUT 60)
    endif()
    find_program(XVFB_RUN xvfb-run)
    set(INTEGRATION_GUI)
    if(XVFB_RUN)
        set(INTEGRATION_GUI --gui)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        if(XVFB_RUN)
            add_test(NAME ui_responsive_touch COMMAND ${XVFB_RUN} -a -s "-screen 0 2200x1800x24" $<TARGET_FILE:orange_pi_terminal> --ui-audit ${CMAKE_CURRENT_BINARY_DIR}/ui-audit)
            add_test(NAME ui_highdpi_touch COMMAND ${CMAKE_COMMAND} -E env QT_SCALE_FACTOR=1.5 ${XVFB_RUN} -a -s "-screen 0 3200x2400x24" $<TARGET_FILE:orange_pi_terminal> --ui-audit ${CMAKE_CURRENT_BINARY_DIR}/ui-audit-hidpi)
            set_tests_properties(ui_responsive_touch ui_highdpi_touch PROPERTIES TIMEOUT 120)
        endif()
        add_test(NAME signed_session_integration COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_integration.py ${CMAKE_CURRENT_BINARY_DIR} ${INTEGRATION_GUI})
        set_tests_properties(signed_session_integration PROPERTIES TIMEOUT 150)
    endif()
endif()
install(TARGETS orange_pi_terminal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT TerminalRuntime)
install(TARGETS terminal_music terminal_system terminal_ui LIBRARY DESTINATION ${TERMINAL_LIBRARY_DIR} COMPONENT TerminalRuntime)
install(FILES music_api/server.cjs music_api/package.json music_api/package-lock.json DESTINATION libexec/orange-pi-terminal/music-api)
install(DIRECTORY music_api/node_modules/ DESTINATION libexec/orange-pi-terminal/music-api/node_modules OPTIONAL)
install(FILES tools/package_tool.py DESTINATION libexec/orange-pi-terminal)
install(FILES tools/arkpets/character_worker.py tools/arkpets/pack_sprites.py
    tools/arkpets/ExportSprites.java tools/arkpets/runtime-lock.json
    DESTINATION libexec/orange-pi-terminal/arkpets COMPONENT TerminalRuntime)
install(FILES assets/arkpets/NOTICE.md assets/arkpets/ARK-MODELS-NOTICE.md
    assets/arkpets/SPINE-RUNTIMES-LICENSE.txt assets/arkpets/SOURCES.json
    DESTINATION share/orange-pi-terminal/licenses/arkpets COMPONENT TerminalRuntime)
install(FILES config/product.example.json DESTINATION share/orange-pi-terminal RENAME product.example.json)
install(FILES config/product.ble.example.json config/product.ble-hotspot-package.example.json config/product.lan.example.json DESTINATION share/orange-pi-terminal/examples)
install(FILES config/environment.example DESTINATION share/orange-pi-terminal)
install(FILES qml/icons/LICENSE qml/icons/NOTICE DESTINATION share/orange-pi-terminal/licenses/material-icons)
install(FILES ../third_party/Viper-Browser/LICENSE ../third_party/Viper-Browser/README.md
    DESTINATION share/orange-pi-terminal/licenses/viper-browser COMPONENT TerminalRuntime)
install(FILES ../third_party/vlc/README.md ../third_party/vlc/VlcVideoItem.h
    ../third_party/vlc/VlcVideoItem.cpp ../third_party/vlc/CMakeLists.txt
    DESTINATION share/orange-pi-terminal/source/vlc COMPONENT TerminalRuntime)
install(DIRECTORY ../third_party/Viper-Browser/
    DESTINATION share/orange-pi-terminal/source/Viper-Browser
    COMPONENT TerminalRuntime PATTERN ".git" EXCLUDE)
install(DIRECTORY ../third_party/sqlite-amalgamation/
    DESTINATION share/orange-pi-terminal/third-party/sqlite-amalgamation
    COMPONENT TerminalRuntime FILES_MATCHING PATTERN "sqlite3.c" PATTERN "sqlite3.h")
install(FILES qml/icons/material/NOTICE qml/icons/material/SOURCES.json DESTINATION share/orange-pi-terminal/licenses/material-icons/round)
install(FILES assets/fonts/OFL.txt assets/fonts/TRADEMARKS.md DESTINATION share/orange-pi-terminal/licenses/google-sans)
install(FILES assets/pinyin/LICENSE assets/pinyin/AUTHORS assets/pinyin/PYPINYIN-LICENSE assets/pinyin/SOURCES.md DESTINATION share/orange-pi-terminal/licenses/pinyin)
install(TARGETS pi_module_sdk EXPORT OrangePiModuleSDKTargets)
install(EXPORT OrangePiModuleSDKTargets NAMESPACE OrangePi:: DESTINATION lib/cmake/OrangePiModuleSDK)
install(FILES cmake/OrangePiModuleSDKConfig.cmake DESTINATION lib/cmake/OrangePiModuleSDK)
install(FILES src/modules/api/pi_module_api.h src/modules/api/IModulePlugin.h DESTINATION include/orange-pi-terminal)
