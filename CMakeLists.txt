cmake_minimum_required(VERSION 4.2)
project(VideoEditor VERSION 1.0.1 LANGUAGES CXX)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.lock.json" dependency_lock)
string(JSON pinned_cmake GET "${dependency_lock}" buildTools cmake version)
string(JSON pinned_compiler GET "${dependency_lock}" buildTools msvc compilerVersion)
string(JSON pinned_toolset GET "${dependency_lock}" buildTools msvc toolsetDirectoryVersion)
string(JSON pinned_sdk GET "${dependency_lock}" buildTools windowsSdkVersion)
string(JSON pinned_qt GET "${dependency_lock}" packages 0 version)
string(JSON qt_prefix GET "${dependency_lock}" packages 0 prefix)
string(JSON pinned_ffmpeg GET "${dependency_lock}" packages 1 version)
string(JSON ffmpeg_prefix GET "${dependency_lock}" packages 1 prefix)
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The pinned application build requires Windows x64 and MSVC.")
endif()
if(NOT CMAKE_VERSION STREQUAL pinned_cmake OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL pinned_compiler)
    message(FATAL_ERROR "Tool version drift. Use scripts/Build.ps1 and dependencies.lock.json.")
endif()
# CMake reports only 14.51 for this toolset variable; verify the full directory pin.
string(FIND "${CMAKE_CXX_COMPILER}" "/MSVC/${pinned_toolset}/" pinned_toolset_position)
if(pinned_toolset_position LESS 0 OR
   NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL pinned_sdk)
    message(FATAL_ERROR "MSVC toolset or Windows SDK drift: toolset=${CMAKE_VS_PLATFORM_TOOLSET_VERSION}, SDK=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}. Use scripts/Build.ps1.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${qt_prefix}")
find_package(Qt6 ${pinned_qt} EXACT REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Test
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${qt_prefix}/lib/cmake/Qt6" NO_DEFAULT_PATH)
find_path(FFMPEG_INCLUDE_DIR libavutil/avutil.h
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${ffmpeg_prefix}/include" NO_DEFAULT_PATH REQUIRED)
find_library(AVUTIL_LIBRARY avutil
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${ffmpeg_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
foreach(component avformat avcodec swscale swresample avfilter)
    find_library(${component}_LIBRARY ${component}
        PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${ffmpeg_prefix}/lib" NO_DEFAULT_PATH REQUIRED)
endforeach()

configure_file(src/BuildInfo.h.in generated/BuildInfo.h @ONLY)
configure_file(src/Version.rc.in generated/Version.rc @ONLY)
add_library(editor_project STATIC src/project/Project.cpp src/project/Effects.cpp src/project/Time.cpp src/project/ProjectStore.cpp src/project/Recovery.cpp
    src/project/Project.h src/project/ProjectStore.h)
set_target_properties(editor_project PROPERTIES AUTOMOC OFF)
target_include_directories(editor_project PUBLIC src)
target_link_libraries(editor_project PUBLIC Qt6::Core)
add_library(editor_timeline STATIC src/timeline/Timeline.cpp src/timeline/Timeline.h)
set_target_properties(editor_timeline PROPERTIES AUTOMOC OFF)
target_link_libraries(editor_timeline PUBLIC editor_project)
add_library(editor_timeline_ui STATIC src/timeline/TimelineWidget.cpp src/timeline/TimelineWidget.h
    src/timeline/PropertyDialogs.cpp src/timeline/PropertyDialogs.h
    src/timeline/EffectDialog.cpp
    src/ui/InspectorWidget.cpp src/ui/InspectorWidget.h
    src/timeline/TimelineCanvas.cpp src/timeline/TimelineCanvas.h)
target_link_libraries(editor_timeline_ui PUBLIC editor_timeline Qt6::Widgets)
add_library(editor_media STATIC src/media/RuntimeInfo.cpp src/media/RuntimeInfo.h src/media/Inspection.cpp src/media/Inspection.h src/media/AidCache.cpp src/media/AidCache.h)
target_include_directories(editor_media SYSTEM PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_include_directories(editor_media PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(editor_media PUBLIC editor_project Qt6::Gui PRIVATE "${AVUTIL_LIBRARY}" "${avformat_LIBRARY}" "${avcodec_LIBRARY}" "${swscale_LIBRARY}")
add_library(editor_playback STATIC src/playback/RenderDescription.cpp src/playback/RenderDescription.h
    src/playback/VisualEffects.cpp src/playback/VisualEffects.h
    src/playback/AudioMixer.cpp src/playback/AudioMixer.h
    src/playback/DecodeWorker.cpp src/playback/DecodeWorker.h src/playback/AudioOutput.cpp src/playback/AudioOutput.h
    src/playback/PlaybackController.cpp src/playback/PlaybackController.h src/playback/MonitorWidget.cpp src/playback/MonitorWidget.h)
target_include_directories(editor_playback SYSTEM PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(editor_playback PUBLIC editor_project Qt6::Widgets Qt6::OpenGLWidgets
    PRIVATE "${AVUTIL_LIBRARY}" "${avformat_LIBRARY}" "${avcodec_LIBRARY}" "${swscale_LIBRARY}" "${swresample_LIBRARY}" "${avfilter_LIBRARY}" ole32 uuid)
target_compile_definitions(editor_playback PRIVATE NOMINMAX)

add_library(editor_export STATIC src/export/ExportWorker.cpp src/export/ExportWorker.h
    src/export/ExportDialog.cpp src/export/ExportDialog.h src/export/Capabilities.cpp src/export/Capabilities.h
    src/export/EncoderConfig.cpp src/export/EncoderConfig.h src/export/EncoderControls.cpp)
qt_add_resources(editor_export export_presets PREFIX "/export" BASE "src/export" FILES src/export/presets.json)
target_include_directories(editor_export SYSTEM PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(editor_export PUBLIC editor_playback PRIVATE "${AVUTIL_LIBRARY}" "${avformat_LIBRARY}" "${avcodec_LIBRARY}" "${swscale_LIBRARY}" "${swresample_LIBRARY}" dxgi)
target_compile_definitions(editor_export PRIVATE NOMINMAX)

add_executable(ExportCapabilityProbe src/export/CapabilityProbeMain.cpp)
set_target_properties(ExportCapabilityProbe PROPERTIES AUTOMOC OFF)
target_link_libraries(ExportCapabilityProbe PRIVATE editor_export)
target_compile_options(ExportCapabilityProbe PRIVATE /W4 /permissive- /utf-8)

qt_add_executable(VideoEditor WIN32
    src/main.cpp src/Diagnostics.cpp src/Diagnostics.h src/MainWindow.cpp src/MainWindow.h src/media/WaveformWidget.h
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Version.rc")
target_include_directories(VideoEditor PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(VideoEditor PRIVATE Qt6::Widgets editor_media editor_project editor_timeline_ui editor_playback editor_export)
add_dependencies(VideoEditor ExportCapabilityProbe)
foreach(target VideoEditor editor_media editor_project editor_timeline editor_timeline_ui editor_playback editor_export)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
endforeach()

# Development deployment. New-WindowsPackage.ps1 creates the clean local release.
add_custom_command(TARGET VideoEditor POST_BUILD
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/${qt_prefix}/bin/windeployqt.exe"
        "$<IF:$<CONFIG:Debug>,--debug,--release>" --no-translations --no-compiler-runtime
        --no-opengl-sw --no-system-d3d-compiler --skip-plugin-types generic,networkinformation,tls
        "$<TARGET_FILE:VideoEditor>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/${ffmpeg_prefix}/bin/avutil-61.dll" "$<TARGET_FILE_DIR:VideoEditor>"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:VideoEditor>/platforms"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/${qt_prefix}/plugins/platforms/qoffscreen$<$<CONFIG:Debug>:d>.dll"
        "$<TARGET_FILE_DIR:VideoEditor>/platforms"
    VERBATIM)
foreach(runtime avformat-63 avcodec-63 swscale-10 swresample-7 avfilter-12)
    add_custom_command(TARGET VideoEditor POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/${ffmpeg_prefix}/bin/${runtime}.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        VERBATIM)
endforeach()

enable_testing()
add_executable(ProjectTests tests/ProjectTests.cpp)
set_target_properties(ProjectTests PROPERTIES AUTOMOC OFF)
target_link_libraries(ProjectTests PRIVATE editor_project)
target_compile_options(ProjectTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(ProjectTests VideoEditor)
add_test(NAME project-persistence COMMAND ProjectTests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/projects"
    "${CMAKE_CURRENT_BINARY_DIR}/project-test-data")
set_tests_properties(project-persistence PROPERTIES TIMEOUT 30)
add_test(NAME application-shell
    COMMAND VideoEditor --smoke-test --data-dir "${CMAKE_CURRENT_BINARY_DIR}/smoke-data"
        --project-fixtures "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/projects")
set_tests_properties(application-shell PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)

add_executable(MediaTests tests/MediaTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(MediaTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(MediaTests PRIVATE Qt6::Widgets editor_media editor_project editor_timeline_ui editor_playback editor_export)
target_compile_options(MediaTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(MediaTests VideoEditor)
add_test(NAME media-import-inspection COMMAND MediaTests "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/media-test-data" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/generated/session-3")
set_tests_properties(media-import-inspection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

add_executable(TimelineTests tests/TimelineTests.cpp)
set_target_properties(TimelineTests PROPERTIES AUTOMOC OFF)
target_link_libraries(TimelineTests PRIVATE editor_timeline)
target_compile_options(TimelineTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(TimelineTests VideoEditor)
add_test(NAME timeline-editing COMMAND TimelineTests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/projects/editing-v2.veproject"
    "${CMAKE_CURRENT_BINARY_DIR}/timeline-test-data")
set_tests_properties(timeline-editing PROPERTIES TIMEOUT 60)

add_executable(PlaybackTests tests/PlaybackTests.cpp)
set_target_properties(PlaybackTests PROPERTIES AUTOMOC OFF)
target_link_libraries(PlaybackTests PRIVATE editor_playback editor_media)
target_compile_options(PlaybackTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(PlaybackTests VideoEditor)
add_test(NAME playback-decode-render COMMAND PlaybackTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/playback-test-data")
set_tests_properties(playback-decode-render PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)
add_executable(PlaybackBenchmark tests/PlaybackBenchmark.cpp)
set_target_properties(PlaybackBenchmark PROPERTIES AUTOMOC OFF)
target_link_libraries(PlaybackBenchmark PRIVATE editor_playback editor_media psapi winmm)
target_compile_definitions(PlaybackBenchmark PRIVATE NOMINMAX)
target_compile_options(PlaybackBenchmark PRIVATE /W4 /permissive- /utf-8)
add_dependencies(PlaybackBenchmark VideoEditor)

add_executable(TimelineUiTests tests/TimelineUiTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(TimelineUiTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(TimelineUiTests PRIVATE Qt6::Test editor_timeline_ui editor_playback editor_media editor_export)
target_compile_options(TimelineUiTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(TimelineUiTests VideoEditor)
add_custom_command(TARGET TimelineUiTests POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:TimelineUiTests>" VERBATIM)
add_test(NAME timeline-interface COMMAND TimelineUiTests "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/timeline-ui-test-data")
set_tests_properties(timeline-interface PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)

add_executable(AudioTitleTests tests/AudioTitleTests.cpp)
set_target_properties(AudioTitleTests PROPERTIES AUTOMOC OFF)
target_link_libraries(AudioTitleTests PRIVATE editor_timeline_ui editor_playback editor_media)
target_compile_options(AudioTitleTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(AudioTitleTests VideoEditor)
add_test(NAME audio-title-basics COMMAND AudioTitleTests "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/audio-title-test-data")
set_tests_properties(audio-title-basics PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

add_executable(ExportTests tests/ExportTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(ExportTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ExportTests PRIVATE editor_export editor_media editor_timeline_ui)
target_compile_options(ExportTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(ExportTests VideoEditor)
add_test(NAME sequence-export COMMAND ExportTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/export-test-data")
set_tests_properties(sequence-export PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)

add_executable(RecoveryTests tests/RecoveryTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(RecoveryTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(RecoveryTests PRIVATE Qt6::Test editor_export editor_media editor_timeline_ui)
target_compile_options(RecoveryTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(RecoveryTests VideoEditor TimelineUiTests)
add_test(NAME project-recovery-relink COMMAND RecoveryTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/recovery-test-data")
set_tests_properties(project-recovery-relink PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

add_executable(PerformanceTests tests/PerformanceTests.cpp)
set_target_properties(PerformanceTests PROPERTIES AUTOMOC OFF)
target_link_libraries(PerformanceTests PRIVATE editor_export editor_media editor_timeline psapi)
target_compile_definitions(PerformanceTests PRIVATE NOMINMAX)
target_compile_options(PerformanceTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(PerformanceTests VideoEditor)
add_test(NAME performance-4k-cache COMMAND PerformanceTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/performance-test-data")
set_tests_properties(performance-4k-cache PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)

add_executable(EffectsTests tests/EffectsTests.cpp)
set_target_properties(EffectsTests PROPERTIES AUTOMOC OFF)
target_link_libraries(EffectsTests PRIVATE editor_export editor_media editor_timeline_ui)
target_compile_options(EffectsTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(EffectsTests VideoEditor)
add_test(NAME effects-keyframes-fades COMMAND EffectsTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/effects-test-data")
set_tests_properties(effects-keyframes-fades PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

add_executable(AdvancedEffectsTests tests/AdvancedEffectsTests.cpp)
set_target_properties(AdvancedEffectsTests PROPERTIES AUTOMOC OFF)
target_link_libraries(AdvancedEffectsTests PRIVATE editor_export editor_media editor_timeline_ui)
target_compile_options(AdvancedEffectsTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(AdvancedEffectsTests VideoEditor)
add_test(NAME color-speed-masks COMMAND AdvancedEffectsTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/advanced-effects-test-data")
set_tests_properties(color-speed-masks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)

add_executable(SequenceTests tests/SequenceTests.cpp)
set_target_properties(SequenceTests PROPERTIES AUTOMOC OFF)
target_link_libraries(SequenceTests PRIVATE editor_export editor_media editor_timeline_ui psapi)
target_compile_definitions(SequenceTests PRIVATE NOMINMAX)
target_compile_options(SequenceTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(SequenceTests VideoEditor)
add_test(NAME nested-multicamera COMMAND SequenceTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sequences-test-data")
set_tests_properties(nested-multicamera PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)

add_executable(CapabilityTests tests/CapabilityTests.cpp)
set_target_properties(CapabilityTests PROPERTIES AUTOMOC OFF)
target_link_libraries(CapabilityTests PRIVATE editor_export editor_media editor_timeline)
target_compile_options(CapabilityTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(CapabilityTests VideoEditor)
add_test(NAME export-device-compatibility COMMAND CapabilityTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/capability-test-data")
set_tests_properties(export-device-compatibility PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300)

add_executable(EncoderCatalogTests tests/EncoderCatalogTests.cpp)
set_target_properties(EncoderCatalogTests PROPERTIES AUTOMOC OFF)
target_link_libraries(EncoderCatalogTests PRIVATE editor_export editor_media editor_timeline)
target_compile_options(EncoderCatalogTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(EncoderCatalogTests VideoEditor)
add_test(NAME export-encoder-catalog COMMAND EncoderCatalogTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/encoder-catalog-test-data")
set_tests_properties(export-encoder-catalog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 1200)

add_executable(CapabilityCacheTests tests/CapabilityCacheTests.cpp)
set_target_properties(CapabilityCacheTests PROPERTIES AUTOMOC OFF)
target_link_libraries(CapabilityCacheTests PRIVATE editor_export)
target_compile_options(CapabilityCacheTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(CapabilityCacheTests VideoEditor)
add_test(NAME export-capability-cache COMMAND CapabilityCacheTests "${CMAKE_CURRENT_BINARY_DIR}/capability-cache-test-data")
set_tests_properties(export-capability-cache PROPERTIES TIMEOUT 60)

add_executable(WorkspaceTests tests/WorkspaceTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(WorkspaceTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(WorkspaceTests PRIVATE Qt6::Test editor_export editor_media editor_timeline_ui)
target_compile_options(WorkspaceTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(WorkspaceTests VideoEditor)
add_test(NAME workspace-organization COMMAND WorkspaceTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/workspace-test-data")
set_tests_properties(workspace-organization PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)

add_executable(ExportTabsTests tests/ExportTabsTests.cpp)
set_target_properties(ExportTabsTests PROPERTIES AUTOMOC OFF)
target_include_directories(ExportTabsTests PRIVATE "${FFMPEG_INCLUDE_DIR}")
target_link_libraries(ExportTabsTests PRIVATE editor_export editor_media editor_timeline ${avcodec_LIBRARY} ${AVUTIL_LIBRARY})
target_compile_options(ExportTabsTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(ExportTabsTests VideoEditor)
add_test(NAME export-simple-advanced COMMAND ExportTabsTests "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/export-tabs-test-data")
set_tests_properties(export-simple-advanced PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)

add_executable(PackageWorkflowTests tests/PackageWorkflowTests.cpp src/MainWindow.cpp src/MainWindow.h
    src/Diagnostics.cpp src/Diagnostics.h src/media/WaveformWidget.h)
target_include_directories(PackageWorkflowTests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(PackageWorkflowTests PRIVATE editor_export editor_media editor_timeline_ui)
target_compile_definitions(PackageWorkflowTests PRIVATE NOMINMAX)
target_compile_options(PackageWorkflowTests PRIVATE /W4 /permissive- /utf-8)
add_dependencies(PackageWorkflowTests VideoEditor)
add_test(NAME packaged-first-workflow COMMAND PackageWorkflowTests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/generated/session-5/flash-beep.mp4"
    "${CMAKE_CURRENT_BINARY_DIR}/package-workflow-test-data")
set_tests_properties(packaged-first-workflow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 240)
