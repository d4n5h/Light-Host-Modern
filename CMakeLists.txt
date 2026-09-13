cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.3.1 LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Light Host Modern is now maintained as a Windows-only application.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(LIGHTHOST_REALTIME_AUDIT "Instrument this host executable's callback allocations in Release validation builds" OFF)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME release-performance-report-analysis COMMAND "${Python3_EXECUTABLE}" -X utf8 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_performance_analysis.py")
        set_tests_properties(release-performance-report-analysis PROPERTIES TIMEOUT 30)
    endif()
    add_executable(LightHostCallbackMeasurementTests Tests/CallbackMeasurementTests.cpp)
    target_include_directories(LightHostCallbackMeasurementTests PRIVATE Source)
    add_test(NAME callback-performance-measurement COMMAND LightHostCallbackMeasurementTests)
    set_tests_properties(callback-performance-measurement PROPERTIES TIMEOUT 15)
    add_executable(LightHostMeterTests Tests/MeterTests.cpp)
    target_include_directories(LightHostMeterTests PRIVATE Source)
    add_test(NAME audio-rms-peak-and-clipping COMMAND LightHostMeterTests)
    add_executable(LightHostRuntimeProfileTests Tests/RuntimeProfileTests.cpp)
    target_include_directories(LightHostRuntimeProfileTests PRIVATE Source)
    add_test(NAME runtime-profile-isolation COMMAND LightHostRuntimeProfileTests)
    set_tests_properties(runtime-profile-isolation PROPERTIES TIMEOUT 15)
    add_executable(LightHostOperationRegistryTests Tests/OperationRegistryTests.cpp)
    target_include_directories(LightHostOperationRegistryTests PRIVATE Source)
    add_test(NAME ipc-operation-retention COMMAND LightHostOperationRegistryTests)
    set_tests_properties(ipc-operation-retention PROPERTIES TIMEOUT 15)
    add_executable(LightHostIpcTests Tests/IpcTests.cpp)
    target_include_directories(LightHostIpcTests PRIVATE Source)
    add_test(NAME ipc-lifetime-and-transport COMMAND LightHostIpcTests)
    set_tests_properties(ipc-lifetime-and-transport PROPERTIES TIMEOUT 15)
    if(MSVC)
        add_executable(LightHostWinUiTransportTests Tests/WinUiTransportTests.cpp)
        find_path(LIGHTHOST_CPPWINRT_INCLUDE_DIR winrt/Windows.Foundation.h
            HINTS "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/cppwinrt"
            REQUIRED)
        target_include_directories(LightHostWinUiTransportTests PRIVATE WinUI/LightHost.WinUI "${LIGHTHOST_CPPWINRT_INCLUDE_DIR}")
        target_link_libraries(LightHostWinUiTransportTests PRIVATE windowsapp)
        set_property(TARGET LightHostWinUiTransportTests PROPERTY CXX_STANDARD 20)
        target_compile_options(LightHostWinUiTransportTests PRIVATE /utf-8)
        add_test(NAME winui-json-and-async-transport COMMAND LightHostWinUiTransportTests)
        set_tests_properties(winui-json-and-async-transport PROPERTIES TIMEOUT 20)
        add_executable(LightHostLateTransportTests Tests/LateTransportTests.cpp)
        target_include_directories(LightHostLateTransportTests PRIVATE WinUI/LightHost.WinUI "${LIGHTHOST_CPPWINRT_INCLUDE_DIR}")
        target_link_libraries(LightHostLateTransportTests PRIVATE windowsapp)
        set_property(TARGET LightHostLateTransportTests PROPERTY CXX_STANDARD 20)
        target_compile_options(LightHostLateTransportTests PRIVATE /utf-8)
        add_test(NAME winui-late-operation-reconciliation COMMAND LightHostLateTransportTests)
        set_tests_properties(winui-late-operation-reconciliation PROPERTIES TIMEOUT 20)
    endif()
endif()

include(FetchContent)

set(LIGHTHOST_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")
set(LIGHTHOST_VST3_SDK_DIR "" CACHE PATH "Optional local VST3 SDK checkout")
set(LIGHTHOST_VST2_SDK_DIR "" CACHE PATH "Optional local VST2 SDK root. Must contain pluginterfaces/vst2.x/aeffect.h")
set(LIGHTHOST_ENABLE_VST2 "AUTO" CACHE STRING "VST2 host support: AUTO, ON, or OFF")
set_property(CACHE LIGHTHOST_ENABLE_VST2 PROPERTY STRINGS AUTO ON OFF)
set(LIGHTHOST_VST2_PROVIDER "XAYMAR" CACHE STRING "VST2 header provider: LEGACY or XAYMAR")
set_property(CACHE LIGHTHOST_VST2_PROVIDER PROPERTY STRINGS LEGACY XAYMAR)
set(LIGHTHOST_ASIO_SDK_DIR "" CACHE PATH "Optional local ASIO SDK checkout")

if(LIGHTHOST_JUCE_DIR)
    # Local source checkouts remain untouched by the bounded MIDI accessor.
    set(juce_SOURCE_DIR "${CMAKE_BINARY_DIR}/juce-lighthost")
    file(COPY "${LIGHTHOST_JUCE_DIR}/" DESTINATION "${juce_SOURCE_DIR}" PATTERN ".git" EXCLUDE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Utilities/PatchJuce.cmake")
    add_subdirectory("${juce_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
    include("${CMAKE_CURRENT_SOURCE_DIR}/Utilities/PatchJuce.cmake")
endif()

if(LIGHTHOST_VST3_SDK_DIR)
    set(vst3sdk_SOURCE_DIR "${LIGHTHOST_VST3_SDK_DIR}")
else()
    FetchContent_Declare(vst3sdk
        GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
        GIT_TAG v3.8.0_build_66
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(vst3sdk)
endif()

juce_set_vst3_sdk_path("${vst3sdk_SOURCE_DIR}")

if(BUILD_TESTING)
    add_subdirectory(Tests/ScenarioPlugin scn EXCLUDE_FROM_ALL)
endif()

if(BUILD_TESTING)
    add_executable(LightHostPluginInstanceTests Tests/PluginInstanceTests.cpp)
    target_include_directories(LightHostPluginInstanceTests PRIVATE Source)
    target_compile_definitions(LightHostPluginInstanceTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_compile_options(LightHostPluginInstanceTests PRIVATE /utf-8)
    target_link_libraries(LightHostPluginInstanceTests PRIVATE juce::juce_audio_processors_headless juce::juce_cryptography)
    add_test(NAME plugin-instance-identity-and-legacy-adapter COMMAND LightHostPluginInstanceTests)
    set_tests_properties(plugin-instance-identity-and-legacy-adapter PROPERTIES TIMEOUT 20)
    add_executable(LightHostSessionTests Tests/SessionPersistenceTests.cpp Source/SessionStore.cpp)
    target_include_directories(LightHostSessionTests PRIVATE Source)
    target_compile_definitions(LightHostSessionTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_compile_options(LightHostSessionTests PRIVATE /utf-8)
    target_link_libraries(LightHostSessionTests PRIVATE juce::juce_audio_processors_headless juce::juce_cryptography)
    add_test(NAME session-atomic-persistence-and-recovery COMMAND LightHostSessionTests)
    set_tests_properties(session-atomic-persistence-and-recovery PROPERTIES TIMEOUT 30)
    add_executable(LightHostProtocolTests Tests/ProtocolTests.cpp)
    target_include_directories(LightHostProtocolTests PRIVATE Source)
    target_compile_definitions(LightHostProtocolTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostProtocolTests PRIVATE juce::juce_core)
    add_test(NAME ipc-versioned-protocol COMMAND LightHostProtocolTests)

    add_executable(LightHostStateSyncTests Tests/StateSyncTests.cpp)
    target_include_directories(LightHostStateSyncTests PRIVATE Source)
    target_compile_definitions(LightHostStateSyncTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostStateSyncTests PRIVATE juce::juce_core)
    add_test(NAME state-events-and-immutable-snapshots COMMAND LightHostStateSyncTests)
    set_tests_properties(ipc-versioned-protocol PROPERTIES TIMEOUT 15)
    add_executable(LightHostDevicePolicyTests Tests/DevicePolicyTests.cpp)
    target_include_directories(LightHostDevicePolicyTests PRIVATE Source)
    target_compile_definitions(LightHostDevicePolicyTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostDevicePolicyTests PRIVATE juce::juce_audio_devices)
    add_test(NAME device-creation-policy COMMAND LightHostDevicePolicyTests)
    set_tests_properties(device-creation-policy PROPERTIES TIMEOUT 15)
    add_executable(LightHostDeviceRecoveryTests Tests/DeviceRecoveryTests.cpp Source/DeviceController.cpp)
    target_include_directories(LightHostDeviceRecoveryTests PRIVATE Source)
    target_compile_definitions(LightHostDeviceRecoveryTests PRIVATE JUCE_USE_CURL=0)
    target_link_libraries(LightHostDeviceRecoveryTests PRIVATE juce::juce_audio_devices juce::juce_data_structures)
    add_test(NAME device-recovery-scenarios COMMAND LightHostDeviceRecoveryTests)
    set_tests_properties(device-recovery-scenarios PROPERTIES TIMEOUT 20)
endif()

string(TOUPPER "${LIGHTHOST_VST2_PROVIDER}" LIGHTHOST_VST2_PROVIDER_UPPER)
if(NOT LIGHTHOST_VST2_PROVIDER_UPPER MATCHES "^(LEGACY|XAYMAR)$")
    message(FATAL_ERROR "LIGHTHOST_VST2_PROVIDER must be LEGACY or XAYMAR.")
endif()

set(LIGHTHOST_VST2_INCLUDE_DIRS "")
set(LIGHTHOST_VST2_SEARCH_DIRS)
if(LIGHTHOST_VST2_PROVIDER_UPPER STREQUAL "XAYMAR")
    FetchContent_Declare(xaymar_vst2sdk
        URL https://github.com/Xaymar/vst2sdk/archive/refs/tags/v0.4.0.zip
        SOURCE_SUBDIR cmake-skip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(xaymar_vst2sdk)

    set(LIGHTHOST_VST2_INCLUDE_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/XaymarVST2JuceShim/include"
        "${xaymar_vst2sdk_SOURCE_DIR}/include")

    if(NOT EXISTS "${xaymar_vst2sdk_SOURCE_DIR}/include/vst.h")
        message(FATAL_ERROR "Xaymar vst2sdk was not fetched correctly. Expected include/vst.h.")
    endif()

    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/XaymarVST2JuceShim/include/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "Xaymar VST2 JUCE shim headers are missing.")
    endif()
else()
    if(LIGHTHOST_VST2_SDK_DIR)
        list(APPEND LIGHTHOST_VST2_SEARCH_DIRS "${LIGHTHOST_VST2_SDK_DIR}")
    endif()
    if(DEFINED ENV{LIGHTHOST_VST2_SDK_DIR})
        list(APPEND LIGHTHOST_VST2_SEARCH_DIRS "$ENV{LIGHTHOST_VST2_SDK_DIR}")
    endif()
    list(APPEND LIGHTHOST_VST2_SEARCH_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/VST2_SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/vst2"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VST2_SDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vst2"
        "${vst3sdk_SOURCE_DIR}")

    foreach(LIGHTHOST_VST2_CANDIDATE IN LISTS LIGHTHOST_VST2_SEARCH_DIRS)
        if(LIGHTHOST_VST2_INCLUDE_DIRS)
            break()
        endif()

        if(EXISTS "${LIGHTHOST_VST2_CANDIDATE}/pluginterfaces/vst2.x/aeffect.h"
                AND EXISTS "${LIGHTHOST_VST2_CANDIDATE}/pluginterfaces/vst2.x/aeffectx.h")
            set(LIGHTHOST_VST2_INCLUDE_DIRS "${LIGHTHOST_VST2_CANDIDATE}")
        elseif(EXISTS "${LIGHTHOST_VST2_CANDIDATE}/aeffect.h"
                AND EXISTS "${LIGHTHOST_VST2_CANDIDATE}/aeffectx.h"
                AND LIGHTHOST_VST2_CANDIDATE MATCHES "pluginterfaces[/\\]vst2\\.x$")
            get_filename_component(LIGHTHOST_VST2_PLUGINTERFACES_DIR "${LIGHTHOST_VST2_CANDIDATE}" DIRECTORY)
            get_filename_component(LIGHTHOST_VST2_INCLUDE_DIRS "${LIGHTHOST_VST2_PLUGINTERFACES_DIR}" DIRECTORY)
        endif()
    endforeach()
endif()

if(LIGHTHOST_VST2_INCLUDE_DIRS AND LIGHTHOST_VST2_PROVIDER_UPPER STREQUAL "LEGACY")
    list(GET LIGHTHOST_VST2_INCLUDE_DIRS 0 LIGHTHOST_VST2_LEGACY_INCLUDE_DIR)
    set(LIGHTHOST_VST2_SDK_DIR "${LIGHTHOST_VST2_LEGACY_INCLUDE_DIR}" CACHE PATH "Optional local VST2 SDK root. Must contain pluginterfaces/vst2.x/aeffect.h" FORCE)
endif()

string(TOUPPER "${LIGHTHOST_ENABLE_VST2}" LIGHTHOST_ENABLE_VST2_UPPER)
if(NOT LIGHTHOST_ENABLE_VST2_UPPER MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "LIGHTHOST_ENABLE_VST2 must be AUTO, ON, or OFF.")
endif()

set(LIGHTHOST_PLUGINHOST_VST2 0)
if(LIGHTHOST_ENABLE_VST2_UPPER STREQUAL "ON")
    if(NOT LIGHTHOST_VST2_INCLUDE_DIRS)
        message(FATAL_ERROR "VST2 support was requested, but aeffect.h/aeffectx.h were not found. Set LIGHTHOST_VST2_SDK_DIR to a folder containing pluginterfaces/vst2.x.")
    endif()
    set(LIGHTHOST_PLUGINHOST_VST2 1)
elseif(LIGHTHOST_ENABLE_VST2_UPPER STREQUAL "AUTO" AND LIGHTHOST_VST2_INCLUDE_DIRS)
    set(LIGHTHOST_PLUGINHOST_VST2 1)
endif()

if(LIGHTHOST_PLUGINHOST_VST2)
    message(STATUS "VST2 host support enabled using ${LIGHTHOST_VST2_PROVIDER_UPPER}: ${LIGHTHOST_VST2_INCLUDE_DIRS}")
else()
    message(STATUS "VST2 host support disabled. Set LIGHTHOST_ENABLE_VST2=ON and configure LIGHTHOST_VST2_PROVIDER/LIGHTHOST_VST2_SDK_DIR to enable it.")
endif()

if(LIGHTHOST_ASIO_SDK_DIR)
    set(asio_sdk_SOURCE_DIR "${LIGHTHOST_ASIO_SDK_DIR}")
else()
    FetchContent_Declare(asio_sdk
        GIT_REPOSITORY https://github.com/audiosdk/asio.git
        GIT_TAG 496a076
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR cmake-skip)
    FetchContent_MakeAvailable(asio_sdk)
endif()

set(LIGHTHOST_ASIO_INCLUDE_DIR "${asio_sdk_SOURCE_DIR}/common")
if(NOT EXISTS "${LIGHTHOST_ASIO_INCLUDE_DIR}/iasiodrv.h")
    message(FATAL_ERROR "ASIO SDK not found. Expected iasiodrv.h in ${LIGHTHOST_ASIO_INCLUDE_DIR}")
endif()

add_library(LightHostCore STATIC)

target_sources(LightHostCore
    PRIVATE
        Source/AudioEngine.cpp
        Source/SessionStore.cpp
        Source/DeviceController.cpp
        Source/LightHostController.cpp
        Source/PluginWindow.cpp
        Source/RealtimeHostProcessor.cpp
        Source/PluginScanController.cpp)

target_include_directories(LightHostCore
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/Source"
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

target_compile_definitions(LightHostCore
    PUBLIC
        JUCE_ASIO=1
        JUCE_ASIO_USE_EXTERNAL_SDK=1
        JUCE_CONTENT_SHARING=0
        JUCE_DIRECTSOUND=1
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_VST=${LIGHTHOST_PLUGINHOST_VST2}
        JUCE_PLUGINHOST_VST3=1
        JUCE_USE_CAMERA=0
        JUCE_USE_CDBURNER=0
        JUCE_USE_CDREADER=0
        JUCE_USE_CURL=0
        JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
        JUCE_WASAPI=1
        JUCE_WEB_BROWSER=0)

target_link_libraries(LightHostCore
    PUBLIC
        juce::juce_audio_processors
        juce::juce_cryptography
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_add_gui_app(LightHost
    BUNDLE_ID "com.lighthost.app"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Icon/logo.png"
    COMPANY_NAME "Light Host Modern"
    PRODUCT_NAME "Light Host Modern")

if(BUILD_TESTING)
    add_executable(LightHostRealtimeTests Tests/RealtimeTests.cpp Source/RealtimeAllocationAudit.cpp)
    target_link_libraries(LightHostRealtimeTests PRIVATE LightHostCore)
    target_include_directories(LightHostRealtimeTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_test(NAME realtime-channels-segments-and-bypass COMMAND LightHostRealtimeTests)
    set_tests_properties(realtime-channels-segments-and-bypass PROPERTIES TIMEOUT 30)
    # Opt-in real modules run in their own process under the Python deadline.
    # CTest remains self-contained and never downloads third-party binaries.
    add_executable(LightHostRealPluginTests EXCLUDE_FROM_ALL Tests/RealPluginTests.cpp Source/RealtimeAllocationAudit.cpp)
    target_link_libraries(LightHostRealPluginTests PRIVATE LightHostCore)
    target_include_directories(LightHostRealPluginTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
endif()

juce_add_console_app(LightHostScanner PRODUCT_NAME "LightHostScanner")
target_sources(LightHostScanner PRIVATE Source/ScannerMain.cpp)
target_link_libraries(LightHostScanner PRIVATE LightHostCore)
target_include_directories(LightHostScanner PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
set_target_properties(LightHostScanner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHost_artefacts/$<CONFIG>")
add_dependencies(LightHost LightHostScanner)

juce_add_console_app(LightHostUpdateHelper PRODUCT_NAME "LightHostUpdateHelper")
target_sources(LightHostUpdateHelper PRIVATE Source/UpdateHelperMain.cpp)
target_include_directories(LightHostUpdateHelper PRIVATE Source)
target_compile_definitions(LightHostUpdateHelper PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX)
target_compile_options(LightHostUpdateHelper PRIVATE /utf-8)
target_link_libraries(LightHostUpdateHelper PRIVATE juce::juce_core msi bcrypt winhttp shell32)
set_target_properties(LightHostUpdateHelper PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/LightHost_artefacts/$<CONFIG>")
add_dependencies(LightHost LightHostUpdateHelper)

# JUCE's resource command does not track Info.txt itself. Regenerate version
# resources when project metadata changes, including in incremental builds.
foreach(app_target IN ITEMS LightHost LightHostScanner LightHostUpdateHelper)
    get_target_property(app_generated_sources ${app_target} JUCE_GENERATED_SOURCES_DIRECTORY)
    get_target_property(app_info_file ${app_target} JUCE_INFO_FILE)
    add_custom_command(OUTPUT "${app_generated_sources}/${app_target}_resources.rc"
        APPEND DEPENDS "${app_info_file}")
endforeach()

if(BUILD_TESTING)
    add_executable(LightHostUpdateTests Tests/UpdateTests.cpp)
    target_include_directories(LightHostUpdateTests PRIVATE Source)
    target_compile_definitions(LightHostUpdateTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 NOMINMAX)
    target_compile_options(LightHostUpdateTests PRIVATE /utf-8)
    target_link_libraries(LightHostUpdateTests PRIVATE juce::juce_core msi bcrypt winhttp shell32)
    add_test(NAME update-streaming-validation-and-application COMMAND LightHostUpdateTests)
    set_tests_properties(update-streaming-validation-and-application PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    add_executable(LightHostScanProcessTests Tests/ScanProcessTests.cpp)
    target_include_directories(LightHostScanProcessTests PRIVATE Source)
    add_test(NAME scanner-process-lifetime COMMAND LightHostScanProcessTests)
    set_tests_properties(scanner-process-lifetime PROPERTIES TIMEOUT 15)
    add_executable(LightHostScanControllerTests Tests/ScanControllerTests.cpp)
    target_link_libraries(LightHostScanControllerTests PRIVATE LightHostCore Advapi32)
    target_include_directories(LightHostScanControllerTests PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}" ${LIGHTHOST_VST2_INCLUDE_DIRS})
    add_test(NAME scanner-controller-results COMMAND LightHostScanControllerTests)
    set_tests_properties(scanner-controller-results PROPERTIES TIMEOUT 30)
endif()

juce_generate_juce_header(LightHost)
set_target_properties(LightHost PROPERTIES OUTPUT_NAME "Light Host Modern")

target_sources(LightHost
    PRIVATE
        Source/DebugLog.cpp
        Source/HostIpcServer.cpp
        Source/HostStartup.cpp
        Source/IconMenu.cpp)

if(LIGHTHOST_REALTIME_AUDIT)
    target_sources(LightHost PRIVATE Source/RealtimeAllocationAudit.cpp)
    target_compile_definitions(LightHost PRIVATE LIGHTHOST_REALTIME_AUDIT=1)
endif()

target_include_directories(LightHost
    PRIVATE
        "${LIGHTHOST_ASIO_INCLUDE_DIR}"
        ${LIGHTHOST_VST2_INCLUDE_DIRS})

juce_add_binary_data(LightHostData
    SOURCES
        Icon/logo.png
        Icon/logo-black.png
        Icon/logo-white.png)

juce_add_binary_data(LightHostLocales
    NAMESPACE LightHostLocales
    HEADER_NAME LightHostLocales.h
    SOURCES WinUI/LightHost.WinUI/Locales/en-us.json WinUI/LightHost.WinUI/Locales/pt-br.json)

target_compile_definitions(LightHost
    PRIVATE
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:LightHost,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:LightHost,JUCE_VERSION>")

target_link_libraries(LightHost
    PRIVATE
        LightHostCore
        LightHostData
        LightHostLocales
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    set(LIGHTHOST_WINUI_TARGET_DIR
        "$<TARGET_FILE_DIR:LightHost>/WinUI/x64/$<CONFIG>/LightHost.WinUI")

    # Staging must also run when only WinUI changed and the host does not relink.
    # CMP0112 (NEW through cmake_minimum_required) makes TARGET_FILE_DIR a
    # directory query, so this prerequisite does not create a target cycle.
    add_custom_target(LightHostStageWinUI
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_SOURCE_DIR}/Utilities/WinUI Output.ps1" -Mode Stage -Platform x64 -Configuration $<CONFIG>
            -Destination "${LIGHTHOST_WINUI_TARGET_DIR}"
        COMMENT "Verifying and staging the current WinUI shell"
        VERBATIM)
    add_dependencies(LightHost LightHostStageWinUI)
endif()
