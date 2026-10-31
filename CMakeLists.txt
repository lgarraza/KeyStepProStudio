cmake_minimum_required(VERSION 3.22)
project(KeyStepProStudio VERSION 0.4.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

# Protocol and model code: header-only, C++17 + std, no JUCE (tested on its own).
add_library(ksp_core INTERFACE)
target_include_directories(ksp_core INTERFACE Source)

enable_testing()
add_executable(protocol_test tests/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE ksp_core)
add_test(NAME protocol_test COMMAND protocol_test)
add_executable(backup_test tests/backup_test.cpp)
target_link_libraries(backup_test PRIVATE ksp_core)
add_test(NAME backup_test COMMAND backup_test)
add_executable(restore_test tests/restore_test.cpp)
target_link_libraries(restore_test PRIVATE ksp_core)
add_test(NAME restore_test COMMAND restore_test)
add_executable(pattern_test tests/pattern_test.cpp)
target_link_libraries(pattern_test PRIVATE ksp_core)
add_test(NAME pattern_test COMMAND pattern_test)
add_executable(drum_test tests/drum_test.cpp)
target_link_libraries(drum_test PRIVATE ksp_core)
add_test(NAME drum_test COMMAND drum_test)
add_executable(scene_test tests/scene_test.cpp)
target_link_libraries(scene_test PRIVATE ksp_core)
add_test(NAME scene_test COMMAND scene_test)
add_executable(projectmap_test tests/projectmap_test.cpp)
target_link_libraries(projectmap_test PRIVATE ksp_core)
add_test(NAME projectmap_test COMMAND projectmap_test)
add_executable(studio_test tests/studio_test.cpp)
target_link_libraries(studio_test PRIVATE ksp_core)
add_test(NAME studio_test COMMAND studio_test)
add_executable(mix_test tests/mix_test.cpp)
target_link_libraries(mix_test PRIVATE ksp_core)
add_test(NAME mix_test COMMAND mix_test)
add_executable(gen_test tests/gen_test.cpp)
target_link_libraries(gen_test PRIVATE ksp_core)
add_test(NAME gen_test COMMAND gen_test)
add_executable(overdub_test tests/overdub_test.cpp)
target_link_libraries(overdub_test PRIVATE ksp_core)
add_test(NAME overdub_test COMMAND overdub_test)
add_executable(notemove_test tests/notemove_test.cpp)
target_link_libraries(notemove_test PRIVATE ksp_core)
add_test(NAME notemove_test COMMAND notemove_test)

# Compares a project file with a read-back (no device, no JUCE).
add_executable(KspCompare Tools/KspCompare/Main.cpp)
target_link_libraries(KspCompare PRIVATE ksp_core)

# JUCE: a local checkout with -DJUCE_DIR=..., otherwise downloaded once into the build folder.
# KSP_TESTS_ONLY=ON skips JUCE entirely (fast checks of the no-JUCE code).
option(KSP_TESTS_ONLY "Build only the tests without JUCE" OFF)
if(NOT KSP_TESTS_ONLY)
    set(JUCE_DIR "" CACHE PATH "Local JUCE checkout (empty = download)")
    if(JUCE_DIR)
        add_subdirectory(${JUCE_DIR} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    # Phase 0: read-only connection test with the KeyStep Pro.
    juce_add_console_app(KspProbe PRODUCT_NAME "KspProbe")
    target_sources(KspProbe PRIVATE Tools/KspProbe/Main.cpp)
    target_compile_definitions(KspProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KspProbe PRIVATE
        ksp_core
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Phase 1a: read-only backup of the global settings and the 16 projects to JSON.
    juce_add_console_app(KspBackup PRODUCT_NAME "KspBackup")
    target_sources(KspBackup PRIVATE Tools/KspBackup/Main.cpp)
    target_compile_definitions(KspBackup PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KspBackup PRIVATE
        ksp_core
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Phase 1b: restore one project from a backup file to a slot (writes, with a backup first and a read-back).
    juce_add_console_app(KspRestore PRODUCT_NAME "KspRestore")
    target_sources(KspRestore PRIVATE Tools/KspRestore/Main.cpp)
    target_compile_definitions(KspRestore PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KspRestore PRIVATE
        ksp_core
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Experiment: generate ambient patterns and write them to a test slot (with a backup first and a read-back).
    juce_add_console_app(KspGen PRODUCT_NAME "KspGen")
    target_sources(KspGen PRIVATE Tools/KspGen/Main.cpp)
    target_compile_definitions(KspGen PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KspGen PRIVATE
        ksp_core
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Phase 3: out-of-process VST3 scanner (one plugin file per run; a crash kills only this process).
    juce_add_console_app(KspVstHelper PRODUCT_NAME "KspVstHelper")
    target_sources(KspVstHelper PRIVATE Tools/KspVstHelper/Main.cpp)
    target_compile_definitions(KspVstHelper PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0)
    target_link_libraries(KspVstHelper PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Phase 3: checks that a scanned VST3 instrument loads, sounds and restores its state (no audio device, no window).
    juce_add_console_app(KspHostCheck PRODUCT_NAME "KspHostCheck")
    target_sources(KspHostCheck PRIVATE Tools/KspHostCheck/Main.cpp)
    target_compile_definitions(KspHostCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0)
    target_link_libraries(KspHostCheck PRIVATE
        ksp_core
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    # Phase 2: KeyStepPro Studio, the editor app.
    # Icon: App/Resources/*.png from Tools/make_icon.py (the four track colours); JUCE makes the .ico / .icns.
    juce_add_gui_app(KeyStepProStudio PRODUCT_NAME "KeyStepPro Studio" COMPANY_NAME "KeyStepPro Studio"
        BUNDLE_ID io.github.lgarraza.keystepprostudio
        ICON_BIG App/Resources/icon.png ICON_SMALL App/Resources/icon_small.png)
    target_sources(KeyStepProStudio PRIVATE App/Main.cpp)
    target_compile_definitions(KeyStepProStudio PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_APPLICATION_NAME_STRING="KeyStepPro Studio" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(KeyStepProStudio PRIVATE
        ksp_core
        juce::juce_gui_basics
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_dependencies(KeyStepProStudio KspVstHelper)
    add_custom_command(TARGET KeyStepProStudio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:KspVstHelper> $<TARGET_FILE_DIR:KeyStepProStudio>)
endif()
