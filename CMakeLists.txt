cmake_minimum_required(VERSION 3.20)

set(VWAVE_VERSION_DEFAULT "0.0.7")
set(VWAVE_VERSION "${VWAVE_VERSION_DEFAULT}")
set(VWAVE_GIT_TAG "untagged")
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE VWAVE_DESCRIBED_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE VWAVE_GIT_RESULT
)
if(VWAVE_GIT_RESULT EQUAL 0 AND VWAVE_DESCRIBED_TAG MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(VWAVE_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(VWAVE_GIT_TAG "${VWAVE_DESCRIBED_TAG}")
endif()
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE VWAVE_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE VWAVE_COMMIT_RESULT
)
if(NOT VWAVE_COMMIT_RESULT EQUAL 0)
    set(VWAVE_GIT_COMMIT "unknown")
endif()

project(VWaveEditor
    VERSION "${VWAVE_VERSION}"
    DESCRIPTION "Non-destructive wave editor and embeddable audio editing core"
    LANGUAGES C CXX
)
message(STATUS "VWaveEditor version ${PROJECT_VERSION} (tag ${VWAVE_GIT_TAG}, commit ${VWAVE_GIT_COMMIT})")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

option(VWAVE_BUILD_GUI "Build the standalone SDL2 editor" ON)
option(VWAVE_BUILD_CLI "Build the vwave command-line tool" ON)
option(VWAVE_BUILD_TESTS "Build unit and integration tests" ON)
option(VWAVE_BUILD_LUA "Embed Lua 5.4 scripting" ON)
option(VWAVE_BUILD_MCP "Embed the MCP stdio server" ON)
option(VWAVE_BUILD_EXAMPLES "Build embedding examples" ON)
option(VWAVE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
if(VWAVE_BUILD_GUI)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(SDL2_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
endif()

function(vwave_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
    if(VWAVE_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/vwave/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/vwave/Version.h
    @ONLY
)

if(VWAVE_BUILD_LUA)
    file(GLOB LUA54_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua54/*.c)
    list(FILTER LUA54_SOURCES EXCLUDE REGEX "(^|/)(lua|luac)\\.c$")
    add_library(vwave_lua STATIC ${LUA54_SOURCES})
    target_include_directories(vwave_lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua54)
    target_compile_options(vwave_lua PRIVATE -w)
    set_target_properties(vwave_lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(VWAVE_LUA_TARGET vwave_lua)
endif()

file(GLOB_RECURSE VWAVE_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_library(vwave_core STATIC ${VWAVE_CORE_SOURCES})
add_library(vwave::core ALIAS vwave_core)
vwave_set_warnings(vwave_core)
target_include_directories(vwave_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(vwave_core PUBLIC PkgConfig::SNDFILE Threads::Threads)
target_compile_definitions(vwave_core PUBLIC VWAVE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR})
if(VWAVE_BUILD_LUA)
    target_link_libraries(vwave_core PUBLIC ${VWAVE_LUA_TARGET})
    target_compile_definitions(vwave_core PUBLIC VWAVE_HAS_LUA=1)
endif()
if(VWAVE_BUILD_MCP)
    target_compile_definitions(vwave_core PUBLIC VWAVE_HAS_MCP=1)
endif()
set_target_properties(vwave_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(VWAVE_BUILD_EXAMPLES)
    add_executable(vwave_embed_example examples/yawn_embed.cpp)
    vwave_set_warnings(vwave_embed_example)
    target_link_libraries(vwave_embed_example PRIVATE vwave::core)
    add_executable(vwave_c_example examples/c_embed.c)
    target_link_libraries(vwave_c_example PRIVATE vwave::core)
    set_target_properties(vwave_c_example PROPERTIES C_STANDARD 11)
endif()

if(VWAVE_BUILD_CLI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/vwave_cli.cpp)
    add_executable(vwave_cli ${CMAKE_CURRENT_SOURCE_DIR}/tools/vwave_cli.cpp)
    vwave_set_warnings(vwave_cli)
    target_link_libraries(vwave_cli PRIVATE vwave::core)
    set_target_properties(vwave_cli PROPERTIES OUTPUT_NAME vwave)
endif()

if(VWAVE_BUILD_GUI AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/main.cpp)
    add_executable(vwave_editor
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/App.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/AudioPlayer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/AudioRecorder.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/chrome/ChromeView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/AnalysisController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/ClipController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/PlaybackController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/RecordingController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/SelectionController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/SpectralController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/StereoController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/controllers/TrackController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/chrome/ContextMenu.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/services/AnalysisService.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/services/AnalysisService.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/services/FileService.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/ui/DialogController.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/ui/UiFormat.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/ui/UiRenderer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/ui/UiTheme.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/ImageView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/MasteringView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/ScopeView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/SidebarView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/SpectrumView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/WaveformSidebar.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/WaveformView.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave/views/WavetableView.cpp
    )
    vwave_set_warnings(vwave_editor)
    target_include_directories(vwave_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps/vwave)
    target_link_libraries(vwave_editor PRIVATE
        vwave::core PkgConfig::SDL2 PkgConfig::SDL2_TTF
    )
endif()

if(VWAVE_BUILD_TESTS)
    enable_testing()
    file(GLOB VWAVE_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
    if(NOT VWAVE_BUILD_LUA)
        list(FILTER VWAVE_TEST_SOURCES EXCLUDE REGEX "test_lua\\.cpp$")
    endif()
    add_executable(vwave_tests ${VWAVE_TEST_SOURCES})
    vwave_set_warnings(vwave_tests)
    target_include_directories(vwave_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(vwave_tests PRIVATE vwave::core)
    add_test(NAME vwave_tests COMMAND vwave_tests)
    if(TARGET vwave_cli)
        add_test(NAME vwave_cli_help COMMAND vwave_cli --help)
    endif()
    if(TARGET vwave_editor)
        add_test(NAME vwave_editor_smoke COMMAND vwave_editor)
        set_tests_properties(vwave_editor_smoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;VWAVE_SMOKE_TEST=1")
    endif()
endif()

install(TARGETS vwave_core ARCHIVE DESTINATION lib)
if(TARGET vwave_cli)
    install(TARGETS vwave_cli RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/vwave DESTINATION include)
