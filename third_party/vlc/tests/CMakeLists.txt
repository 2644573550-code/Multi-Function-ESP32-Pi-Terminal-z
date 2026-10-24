find_package(Qt6 REQUIRED COMPONENTS Test QuickTest QuickControls2)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(TERMINAL_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../pi_terminal")
set(TEST_CLIP "${CMAKE_CURRENT_BINARY_DIR}/test-pattern.avi")
add_test(NAME media_generate_fixture
    COMMAND "${Python3_EXECUTABLE}" "${TERMINAL_SOURCE_DIR}/tests/run_media_browser_tests.py"
            --generate-only "${TEST_CLIP}")
set_tests_properties(media_generate_fixture PROPERTIES FIXTURES_SETUP media_fixture LABELS "fixture" TIMEOUT 30)

add_executable(vlc_backend_test vlc_backend_test.cpp)
target_link_libraries(vlc_backend_test PRIVATE terminal_vlc VLC::VLC Qt6::Test)
foreach(kind IN ITEMS functional performance interface)
    add_test(NAME media_vlc_${kind} COMMAND vlc_backend_test ${kind})
    set_tests_properties(media_vlc_${kind} PROPERTIES
        FIXTURES_REQUIRED media_fixture LABELS "${kind}"
        ENVIRONMENT "TERMINAL_TEST_VIDEO=${TEST_CLIP};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 60)
endforeach()

add_executable(browser_joystick_test joystick_test.cpp)
target_link_libraries(browser_joystick_test PRIVATE Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(browser_joystick_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/qml")
add_test(NAME media_browser_joystick_interface COMMAND browser_joystick_test)
set_tests_properties(media_browser_joystick_interface PROPERTIES LABELS "interface;functional"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)

if(PROJECT_NAME STREQUAL "terminal_vlc")
    qt_add_executable(video_player_ui_test "${TERMINAL_SOURCE_DIR}/tests/video_player_ui_test.cpp")
    target_include_directories(video_player_ui_test PRIVATE "${TERMINAL_SOURCE_DIR}/src")
    target_compile_definitions(video_player_ui_test PRIVATE TERMINAL_ISOLATED_UI_TEST)
    target_link_libraries(video_player_ui_test PRIVATE terminal_vlc Qt6::QuickControls2 Qt6::Test)
    set(video_qml)
    foreach(file IN ITEMS VideoPlayer.qml Theme.qml I18n.qml IconButton.qml MaterialIcon.qml CarText.qml)
        set_source_files_properties("${TERMINAL_SOURCE_DIR}/qml/${file}" PROPERTIES QT_RESOURCE_ALIAS "${file}")
        list(APPEND video_qml "${TERMINAL_SOURCE_DIR}/qml/${file}")
    endforeach()
    set_source_files_properties("${TERMINAL_SOURCE_DIR}/qml/Theme.qml" "${TERMINAL_SOURCE_DIR}/qml/I18n.qml"
        PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(video_player_ui_test URI OrangePiTerminal VERSION 1.0
        RESOURCE_PREFIX /qt/qml OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/OrangePiTerminal"
        QML_FILES ${video_qml})
    add_test(NAME media_video_player_interface COMMAND video_player_ui_test)
    set_tests_properties(media_video_player_interface PROPERTIES LABELS "interface"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
endif()

option(TERMINAL_BROWSER_TESTS "Build real Qt WebEngine context-scroll integration tests" ON)
if(TERMINAL_BROWSER_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS WebEngineQuick)
    add_executable(browser_context_test browser_context_test.cpp)
    target_link_libraries(browser_context_test PRIVATE Qt6::WebEngineQuick Qt6::Quick Qt6::Test)
    target_compile_definitions(browser_context_test PRIVATE
        BROWSER_SCROLL_JS="${TERMINAL_SOURCE_DIR}/qml/BrowserScroll.js")
    add_test(NAME media_browser_context_functional COMMAND browser_context_test)
    set_tests_properties(media_browser_context_functional PROPERTIES LABELS "functional;interface"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 90)
endif()
