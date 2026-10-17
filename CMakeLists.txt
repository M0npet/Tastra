cmake_minimum_required(VERSION 3.25)

project(
    Tastra
    VERSION 0.7.17
    DESCRIPTION "Privacy-first customizable virtual keyboard for Wayland"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

find_package(Qt6 REQUIRED COMPONENTS Core DBus Gui Qml Quick Test WaylandClient)
# Qt >= 6.9 ships private modules as separate CMake packages (Arch: Qt 6.11).
# Older Qt defines the private targets together with the public module.
if(NOT TARGET Qt6::GuiPrivate)
    find_package(Qt6GuiPrivate CONFIG REQUIRED)
endif()
if(NOT TARGET Qt6::WaylandClientPrivate)
    find_package(Qt6WaylandClientPrivate CONFIG REQUIRED)
endif()
find_package(PkgConfig REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
pkg_check_modules(HUNSPELL REQUIRED IMPORTED_TARGET hunspell)
find_package(Threads REQUIRED)

set(
    INPUT_METHOD_V1_PROTOCOL
    "${WAYLAND_PROTOCOLS_DATADIR}/unstable/input-method/input-method-unstable-v1.xml"
)

add_library(tastra-core STATIC
    src/core/keyboardcontroller.cpp
    src/core/keyboardmodel.cpp
    src/core/locallexicon.cpp
    src/core/morsecode.cpp
    src/core/capsmode.cpp
    src/core/typingengine.cpp
    src/core/clipboardhistory.cpp
    src/core/emojicatalog.cpp
    src/core/legacymigration.cpp
    src/core/glidegeometry.cpp
    src/core/voicecommands.cpp
    src/core/systemclipboard.h
)

target_include_directories(tastra-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

file(GLOB TASTRA_DATA_FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/frequency/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/bigrams/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/emoji/*.tsv"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/blocklist/*.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/sounds/*.wav"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/i18n/*.tsv")
qt_add_resources(tastra-core "tastra-data"
    PREFIX "/tastra"
    BASE data
    FILES ${TASTRA_DATA_FILES}
)

target_link_libraries(tastra-core
    PUBLIC
        Qt6::Core
    PRIVATE
        PkgConfig::HUNSPELL
        Threads::Threads
)

add_library(tastra-platform-kwin STATIC
    src/platform/kwin/kwininputmethodv1backend.cpp
    src/platform/kwin/kwininputmethodv1session.cpp
    src/platform/kwin/kwininputmethodv1connection.cpp
    src/platform/kwin/kwininputpanelintegration.cpp
    src/platform/kwin/kwininputpanelshellintegration.cpp
    src/platform/kwin/kwininputpanelsurface.cpp
)

# PRIVATE_CODE exists since Qt 6.8; older Qt only knows the (default) public code.
set(TASTRA_WAYLAND_CODE_VISIBILITY)
if(Qt6WaylandClient_VERSION VERSION_GREATER_EQUAL 6.8)
    set(TASTRA_WAYLAND_CODE_VISIBILITY PRIVATE_CODE)
endif()

qt_generate_wayland_protocol_client_sources(
    tastra-platform-kwin
    ${TASTRA_WAYLAND_CODE_VISIBILITY}
    NO_INCLUDE_CORE_ONLY
    FILES
        "${INPUT_METHOD_V1_PROTOCOL}"
)

target_include_directories(tastra-platform-kwin
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

target_link_libraries(tastra-platform-kwin
    PUBLIC
        tastra-core
        Qt6::Core
        Qt6::WaylandClient
    PRIVATE
        Qt6::GuiPrivate
        Qt6::WaylandClientPrivate
)

# KWin extras outside the input-method protocol, used when the compositor
# offers them: the clipboard through ext-data-control-v1 (wayland-protocols
# >= 1.39; KWin since Plasma 6.4) and the editing shortcuts through KDE's
# fake-input protocol (vendored in protocols/). Without them the keyboard
# falls back to QClipboard and offers Copy/Cut only for reported selections.
pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)
set(EXT_DATA_CONTROL_PROTOCOL "${WAYLAND_PROTOCOLS_DATADIR}/staging/ext-data-control/ext-data-control-v1.xml")
set(FAKE_INPUT_PROTOCOL "${CMAKE_CURRENT_SOURCE_DIR}/protocols/fake-input.xml")
set(WAYLAND_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${WAYLAND_GENERATED}")
function(tastra_wayland_protocol xml base)
    foreach(kind client-header server-header private-code)
        if(kind STREQUAL "client-header")
            set(out "${WAYLAND_GENERATED}/${base}-client-protocol.h")
        elseif(kind STREQUAL "server-header")
            set(out "${WAYLAND_GENERATED}/${base}-server-protocol.h")
        else()
            set(out "${WAYLAND_GENERATED}/${base}-protocol.c")
        endif()
        add_custom_command(OUTPUT "${out}"
            COMMAND "${WAYLAND_SCANNER}" ${kind} "${xml}" "${out}"
            DEPENDS "${xml}" VERBATIM)
    endforeach()
endfunction()
set(TASTRA_DATA_CONTROL OFF)
if(WAYLAND_SCANNER AND WAYLAND_CLIENT_FOUND)
    set(EXTRA_PROTOCOL_SOURCES)
    set(EXTRA_SOURCES src/platform/kwin/waylanddisplay.cpp src/platform/kwin/fakeinputkeychords.cpp)
    tastra_wayland_protocol("${FAKE_INPUT_PROTOCOL}" fake-input)
    list(APPEND EXTRA_PROTOCOL_SOURCES "${WAYLAND_GENERATED}/fake-input-protocol.c"
        "${WAYLAND_GENERATED}/fake-input-client-protocol.h" "${WAYLAND_GENERATED}/fake-input-server-protocol.h")
    if(EXISTS "${EXT_DATA_CONTROL_PROTOCOL}")
        set(TASTRA_DATA_CONTROL ON)
        tastra_wayland_protocol("${EXT_DATA_CONTROL_PROTOCOL}" ext-data-control-v1)
        list(APPEND EXTRA_PROTOCOL_SOURCES "${WAYLAND_GENERATED}/ext-data-control-v1-protocol.c"
            "${WAYLAND_GENERATED}/ext-data-control-v1-client-protocol.h" "${WAYLAND_GENERATED}/ext-data-control-v1-server-protocol.h")
        list(APPEND EXTRA_SOURCES src/platform/kwin/datacontrolclipboard.cpp)
    endif()
    add_library(tastra-wayland-protocols STATIC ${EXTRA_PROTOCOL_SOURCES})
    set_target_properties(tastra-wayland-protocols PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(tastra-wayland-protocols PUBLIC "${WAYLAND_GENERATED}")
    target_link_libraries(tastra-wayland-protocols PUBLIC PkgConfig::WAYLAND_CLIENT)
    add_library(tastra-wayland-extras STATIC ${EXTRA_SOURCES})
    target_include_directories(tastra-wayland-extras PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(tastra-wayland-extras PUBLIC TASTRA_WAYLAND_EXTRAS=1)
    if(TASTRA_DATA_CONTROL)
        target_compile_definitions(tastra-wayland-extras PUBLIC TASTRA_DATA_CONTROL=1)
    endif()
    target_link_libraries(tastra-wayland-extras
        PUBLIC tastra-core Qt6::Gui
        PRIVATE tastra-wayland-protocols Qt6::GuiPrivate)
endif()
message(STATUS "Clipboard through ext-data-control-v1: ${TASTRA_DATA_CONTROL}")
pkg_check_modules(WAYLAND_SERVER IMPORTED_TARGET wayland-server)
if(BUILD_TESTING AND TASTRA_DATA_CONTROL AND WAYLAND_SERVER_FOUND)
    # The test compositor also offers xdg_wm_base (Qt's Wayland platform
    # needs a shell to start).
    set(XDG_SHELL_PROTOCOL "${WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml")
    add_custom_command(
        OUTPUT "${WAYLAND_GENERATED}/xdg-shell-server-protocol.h"
        COMMAND "${WAYLAND_SCANNER}" server-header "${XDG_SHELL_PROTOCOL}" "${WAYLAND_GENERATED}/xdg-shell-server-protocol.h"
        DEPENDS "${XDG_SHELL_PROTOCOL}" VERBATIM)
    add_custom_command(
        OUTPUT "${WAYLAND_GENERATED}/xdg-shell-protocol.c"
        COMMAND "${WAYLAND_SCANNER}" private-code "${XDG_SHELL_PROTOCOL}" "${WAYLAND_GENERATED}/xdg-shell-protocol.c"
        DEPENDS "${XDG_SHELL_PROTOCOL}" VERBATIM)
    add_library(tastra-test-compositor STATIC
        "${WAYLAND_GENERATED}/xdg-shell-protocol.c"
        "${WAYLAND_GENERATED}/xdg-shell-server-protocol.h")
    target_include_directories(tastra-test-compositor PUBLIC "${WAYLAND_GENERATED}" ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(tastra-test-compositor PUBLIC PkgConfig::WAYLAND_SERVER tastra-wayland-protocols Threads::Threads)

    add_executable(tst_datacontrolclipboard tests/tst_datacontrolclipboard.cpp)
    target_link_libraries(tst_datacontrolclipboard PRIVATE tastra-wayland-extras tastra-test-compositor Qt6::Test)
    add_test(NAME datacontrolclipboard COMMAND tst_datacontrolclipboard)
    set_tests_properties(datacontrolclipboard PROPERTIES ENVIRONMENT "XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime")
    file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    # The same through Qt's Wayland platform plugin (needs qt6-wayland).
    add_executable(tst_datacontrolqtdisplay tests/tst_datacontrolqtdisplay.cpp)
    target_link_libraries(tst_datacontrolqtdisplay PRIVATE tastra-wayland-extras tastra-test-compositor Qt6::Test Qt6::Gui)
    add_test(NAME datacontrolqtdisplay COMMAND tst_datacontrolqtdisplay)
    set_tests_properties(datacontrolqtdisplay PROPERTIES ENVIRONMENT "XDG_RUNTIME_DIR=${CMAKE_CURRENT_BINARY_DIR}/xdg-runtime")
endif()


add_library(tastra-app STATIC
    src/app/keyboarduibridge.cpp
    src/app/keyboardlayoutmetrics.cpp
    src/app/tracelog.cpp
    src/app/panelvisibility.cpp
    src/app/keyboardhider.cpp
    src/app/voicecontroller.cpp
    src/app/uitranslator.cpp
    src/app/qtsystemclipboard.cpp
    src/app/terminationsaver.cpp
)

target_include_directories(tastra-app
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(tastra-app PRIVATE TASTRA_VERSION="${PROJECT_VERSION}")
target_link_libraries(tastra-app
    PUBLIC
        tastra-core
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
)

qt_add_executable(tastra
    src/app/main.cpp
)

target_link_libraries(tastra
    PRIVATE
        tastra-app
        tastra-platform-kwin
        $<TARGET_NAME_IF_EXISTS:tastra-wayland-extras>
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
)

set_source_files_properties(
    src/ui/Main.qml
    PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)

set_source_files_properties(
    src/ui/PanelButton.qml
    PROPERTIES QT_RESOURCE_ALIAS PanelButton.qml
)
set_source_files_properties(
    src/ui/ClipboardPanel.qml
    PROPERTIES QT_RESOURCE_ALIAS ClipboardPanel.qml
)
set_source_files_properties(
    src/ui/EmojiPanel.qml
    PROPERTIES QT_RESOURCE_ALIAS EmojiPanel.qml
)
set_source_files_properties(
    src/ui/TextEditingPanel.qml
    PROPERTIES QT_RESOURCE_ALIAS TextEditingPanel.qml
)
set_source_files_properties(
    src/ui/SettingsPanel.qml
    PROPERTIES QT_RESOURCE_ALIAS SettingsPanel.qml
)
set_source_files_properties(
    src/ui/icons/shift.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/shift.svg
)
set_source_files_properties(
    src/ui/icons/shift-lock.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/shift-lock.svg
)
set_source_files_properties(
    src/ui/icons/backspace.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/backspace.svg
)
set_source_files_properties(
    src/ui/icons/enter.svg
    PROPERTIES QT_RESOURCE_ALIAS icons/enter.svg
)

qt_add_resources(
    tastra
    "tastra-ui"
    PREFIX "/tastra"
    FILES
        src/ui/Main.qml
        src/ui/PanelButton.qml
        src/ui/ClipboardPanel.qml
        src/ui/EmojiPanel.qml
        src/ui/TextEditingPanel.qml
        src/ui/SettingsPanel.qml
        src/ui/icons/shift.svg
        src/ui/icons/shift-lock.svg
        src/ui/icons/backspace.svg
        src/ui/icons/enter.svg
)


if(BUILD_TESTING)
    add_executable(tastra-smoke
        tests/smoke.cpp
    )

    target_link_libraries(tastra-smoke
        PRIVATE
            Qt6::Core
            Qt6::Test
    )

    add_test(
        NAME tastra-smoke
        COMMAND tastra-smoke
    )

    add_executable(tst_keyboardcontroller
        tests/tst_keyboardcontroller.cpp
    )

    target_link_libraries(tst_keyboardcontroller
        PRIVATE
            tastra-core
            Qt6::Test
    )

    add_test(
        NAME keyboardcontroller
        COMMAND tst_keyboardcontroller
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_kwininputmethodv1backend
        tests/tst_kwininputmethodv1backend.cpp
    )

    target_link_libraries(tst_kwininputmethodv1backend
        PRIVATE
            tastra-platform-kwin
            Qt6::Test
    )

    add_test(
        NAME kwininputmethodv1backend
        COMMAND tst_kwininputmethodv1backend
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_kwininputmethodv1session
        tests/tst_kwininputmethodv1session.cpp
    )

    target_link_libraries(tst_kwininputmethodv1session
        PRIVATE
            tastra-platform-kwin
            Qt6::Test
    )

    add_test(
        NAME kwininputmethodv1session
        COMMAND tst_kwininputmethodv1session
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_kwininputmethodv1connection
        tests/tst_kwininputmethodv1connection.cpp
    )

    target_link_libraries(tst_kwininputmethodv1connection
        PRIVATE
            tastra-platform-kwin
            Qt6::Test
    )

    add_test(
        NAME kwininputmethodv1connection
        COMMAND tst_kwininputmethodv1connection
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_keyboarduibridge
        tests/tst_keyboarduibridge.cpp
    )

    target_compile_definitions(tst_keyboarduibridge PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_keyboarduibridge
        PRIVATE
            tastra-app
            Qt6::Test
    )

    add_test(
        NAME keyboarduibridge
        COMMAND tst_keyboarduibridge
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_keyboardmodel
        tests/tst_keyboardmodel.cpp
    )

    target_link_libraries(tst_keyboardmodel
        PRIVATE
            tastra-core
            Qt6::Test
    )

    add_test(
        NAME keyboardmodel
        COMMAND tst_keyboardmodel
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_specialkeys
        tests/tst_specialkeys.cpp
    )

    target_link_libraries(tst_specialkeys
        PRIVATE
            tastra-core
            Qt6::Test
    )

    add_test(
        NAME specialkeys
        COMMAND tst_specialkeys
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_keyboardlayoutmetrics
        tests/tst_keyboardlayoutmetrics.cpp
    )

    target_link_libraries(tst_keyboardlayoutmetrics
        PRIVATE
            tastra-app
            Qt6::Test
    )

    add_test(
        NAME keyboardlayoutmetrics
        COMMAND tst_keyboardlayoutmetrics
    )
endif()

if(BUILD_TESTING)
    add_executable(tst_smarttyping
        tests/tst_smarttyping.cpp
    )
    target_compile_definitions(tst_smarttyping PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_smarttyping PRIVATE tastra-core Qt6::Test)
    add_test(NAME smarttyping COMMAND tst_smarttyping)

    add_executable(tst_clientsim
        tests/tst_clientsim.cpp
        tests/textinputclient.h
    )
    target_compile_definitions(tst_clientsim PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_clientsim PRIVATE tastra-core Qt6::Test)
    add_test(NAME clientsim COMMAND tst_clientsim)

    add_executable(tst_clipboardemoji
        tests/tst_clipboardemoji.cpp
    )
    target_compile_definitions(tst_clipboardemoji PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_clipboardemoji PRIVATE tastra-core Qt6::Test)
    add_test(NAME clipboardemoji COMMAND tst_clipboardemoji)
endif()

if(BUILD_TESTING)
    add_executable(tst_qmlkeyboard
        tests/tst_qmlkeyboard.cpp
    )
    target_compile_definitions(tst_qmlkeyboard PRIVATE
        TASTRA_MAIN_QML="${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Main.qml"
        TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    qt_add_resources(tst_qmlkeyboard "tst-qmlkeyboard-icons"
        PREFIX "/tastra"
        FILES
            src/ui/icons/shift.svg
            src/ui/icons/shift-lock.svg
            src/ui/icons/backspace.svg
            src/ui/icons/enter.svg
    )
    target_link_libraries(tst_qmlkeyboard PRIVATE tastra-app Qt6::Quick Qt6::Qml Qt6::Test)
    add_test(NAME qmlkeyboard COMMAND tst_qmlkeyboard)
    set_tests_properties(qmlkeyboard PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

if(BUILD_TESTING)
    add_executable(tst_lexicon tests/tst_lexicon.cpp)
    target_compile_definitions(tst_lexicon PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_lexicon PRIVATE tastra-core Qt6::Test)
    add_test(NAME lexicon COMMAND tst_lexicon)
endif()

if(BUILD_TESTING)
    add_executable(tst_voice tests/tst_voice.cpp)
    target_link_libraries(tst_voice PRIVATE tastra-app Qt6::Test)
    add_test(NAME voice COMMAND tst_voice)
endif()

# Gboard "Sound on keypress": only when Qt Multimedia is installed.
find_package(Qt6 QUIET COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
    target_link_libraries(tastra-app PUBLIC Qt6::Multimedia)
    target_compile_definitions(tastra-app PUBLIC TASTRA_HAVE_KEY_SOUND=1)
endif()

# Offline voice input (opt-in at install time): built only when both Qt
# Multimedia and whisper.cpp are present; otherwise the keyboard has no mic key.
option(TASTRA_VOICE "Build offline voice input if whisper.cpp and Qt Multimedia are available" ON)
set(TASTRA_HAVE_VOICE OFF)
if(TASTRA_VOICE)
    find_package(Qt6 QUIET COMPONENTS Multimedia)
    find_package(whisper CONFIG QUIET)
    if(NOT TARGET whisper)
        pkg_check_modules(WHISPER QUIET IMPORTED_TARGET whisper)
        if(TARGET PkgConfig::WHISPER)
            add_library(whisper INTERFACE IMPORTED)
            target_link_libraries(whisper INTERFACE PkgConfig::WHISPER)
        endif()
    endif()
    if(TARGET Qt6::Multimedia AND TARGET whisper)
        set(TASTRA_HAVE_VOICE ON)
        add_library(tastra-voice STATIC
            src/voice/qtaudiorecorder.cpp
            src/voice/whisperrecognizer.cpp
        )
        target_include_directories(tastra-voice PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(tastra-voice PUBLIC tastra-app Qt6::Multimedia whisper)
        target_link_libraries(tastra PRIVATE tastra-voice)
        target_compile_definitions(tastra PRIVATE TASTRA_HAVE_VOICE=1)
        if(BUILD_TESTING)
            add_executable(tst_voiceadapters tests/tst_voiceadapters.cpp)
            target_link_libraries(tst_voiceadapters PRIVATE tastra-voice Qt6::Test)
            add_test(NAME voiceadapters COMMAND tst_voiceadapters)
        endif()
    endif()
endif()
message(STATUS "Tastra offline voice input: ${TASTRA_HAVE_VOICE}")

if(BUILD_TESTING)
    add_executable(tst_typingstress tests/tst_typingstress.cpp)
    target_compile_definitions(tst_typingstress PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_typingstress PRIVATE tastra-core Qt6::Test)
    add_test(NAME typingstress COMMAND tst_typingstress)
endif()

option(TASTRA_BENCHMARKS "Build developer benchmarks" OFF)
if(TASTRA_BENCHMARKS)
    add_executable(bench_keystroke tools/bench_keystroke.cpp)
    target_link_libraries(bench_keystroke PRIVATE tastra-app)
    add_executable(bench_memory tools/bench_memory.cpp)
    target_link_libraries(bench_memory PRIVATE tastra-core)
endif()

if(BUILD_TESTING)
    add_executable(tst_panelvisibility tests/tst_panelvisibility.cpp)
    target_link_libraries(tst_panelvisibility PRIVATE tastra-app Qt6::Test)
    add_test(NAME panelvisibility COMMAND tst_panelvisibility)
endif()

if(BUILD_TESTING)
    add_executable(tst_capsmode tests/tst_capsmode.cpp)
    target_link_libraries(tst_capsmode PRIVATE tastra-core Qt6::Test)
    add_test(NAME capsmode COMMAND tst_capsmode)
endif()

if(BUILD_TESTING)
    add_executable(tst_legacymigration tests/tst_legacymigration.cpp)
    target_link_libraries(tst_legacymigration PRIVATE tastra-core Qt6::Test)
    add_test(NAME legacymigration COMMAND tst_legacymigration)
endif()

if(BUILD_TESTING)
    add_executable(tst_glide tests/tst_glide.cpp)
    target_compile_definitions(tst_glide PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_glide PRIVATE tastra-core Qt6::Test)
    add_test(NAME glide COMMAND tst_glide)
endif()

if(BUILD_TESTING)
    add_executable(tst_terminationsaver tests/tst_terminationsaver.cpp)
    target_compile_definitions(tst_terminationsaver PRIVATE TASTRA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    target_link_libraries(tst_terminationsaver PRIVATE tastra-app Qt6::Test)
    add_test(NAME terminationsaver COMMAND tst_terminationsaver)
endif()

if(BUILD_TESTING)
    # The tests expect the English interface and C collation whatever the
    # system language is (Plasma exports LANGUAGE from its language setting;
    # the installer runs the suite in the user's session).
    get_property(TASTRA_ALL_TESTS DIRECTORY PROPERTY TESTS)
    set_property(TEST ${TASTRA_ALL_TESTS} APPEND PROPERTY ENVIRONMENT
        "LANGUAGE=en" "LC_ALL=C.UTF-8" "QT_QPA_OFFSCREEN_NO_GLX=1")
endif()
