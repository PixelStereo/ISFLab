cmake_minimum_required(VERSION 3.21)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" isflab_version LIMIT_COUNT 1)
if(DEFINED ISFLAB_TAG_VERSION)
    set(isflab_version "${ISFLAB_TAG_VERSION}")
endif()
project(ISFLab VERSION "${isflab_version}" LANGUAGES CXX)

# Published tag builds carry the plain release number ("0.2.1"); every other build also carries the commits since
# that release ("0.2.1+3") and a trailing "+" when it has uncommitted changes ("0.2.1+3+").
set(ISFLAB_DISPLAY_VERSION "${PROJECT_VERSION}")
if(NOT DEFINED ISFLAB_TAG_VERSION AND NOT ISFLAB_RELEASE_BUILD)
    include(third_party/pxs-qt/cmake/PixelStereoBuildId.cmake)
    pxs_nearest_release("${CMAKE_CURRENT_SOURCE_DIR}" isflab_release)
    if(NOT isflab_release STREQUAL "")
        set(ISFLAB_DISPLAY_VERSION "${isflab_release}") # the tag the commits are counted from, even before VERSION catches up
    endif()
    pxs_build_suffix("${CMAKE_CURRENT_SOURCE_DIR}" isflab_suffix)
    string(APPEND ISFLAB_DISPLAY_VERSION "${isflab_suffix}")
    execute_process(COMMAND git symbolic-ref -q HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE isflab_branch_ref OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    foreach(isflab_ref HEAD ${isflab_branch_ref} packed-refs)
        execute_process(COMMAND git rev-parse --path-format=absolute --git-path ${isflab_ref}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE isflab_ref_path
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(EXISTS "${isflab_ref_path}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${isflab_ref_path}")
        endif()
    endforeach()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Multimedia)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

add_library(ISFLabCore STATIC
    src/core/ISFScanner.h src/core/ISFScanner.cpp
    src/core/Library.h src/core/Library.cpp
    src/core/ZipExport.h src/core/ZipExport.cpp
)
target_include_directories(ISFLabCore PUBLIC src/core)
target_link_libraries(ISFLabCore PUBLIC Qt6::Core)

add_library(ISFLabEngine STATIC
    src/engine/ShaderEngine.h src/engine/ShaderEngine.cpp
    src/engine/DimensionExpression.h src/engine/DimensionExpression.cpp
    src/engine/MaterialGenerators.h src/engine/MaterialGenerators.cpp
    src/engine/MaterialSource.h src/engine/MaterialSource.cpp
    src/engine/TestPatterns.h src/engine/TestPatterns.cpp
    src/engine/ShaderChecker.h src/engine/ShaderChecker.cpp
)
target_include_directories(ISFLabEngine PUBLIC src/engine src/engine/input)
target_link_libraries(ISFLabEngine PUBLIC ISFLabCore Qt6::Gui Qt6::OpenGL)

qt_add_resources(ISFLabEngine madmapper PREFIX "/madmapper" BASE "third_party/madmapper"
    FILES third_party/madmapper/MadCommon.glsl third_party/madmapper/MadNoise.glsl
          third_party/madmapper/MadSDF.glsl third_party/madmapper/noiseLUT.png)

qt_add_executable(ISFLab WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/AppStyle.h src/app/AppStyle.cpp
    src/app/SettingsKeys.h
    src/app/ShaderTypes.h src/app/ShaderTypes.cpp
    src/app/MainWindow.h src/app/MainWindow.cpp
    src/app/MediaBin.h src/app/MediaBin.cpp
    src/app/LibraryActions.h src/app/LibraryActions.cpp
    src/app/PreviewPanel.h src/app/PreviewPanel.cpp
    src/app/ParameterPanel.h src/app/ParameterPanel.cpp
    src/app/PointPad.h src/app/PointPad.cpp
    src/app/ShaderDocument.h src/app/ShaderDocument.cpp
    src/app/SourceEditor.h src/app/SourceEditor.cpp
    src/app/FindBar.h src/app/FindBar.cpp
    src/app/ShaderCheckWorker.h src/app/ShaderCheckWorker.cpp
    src/app/HeaderInspector.h src/app/HeaderInspector.cpp
    src/app/DiagnosticsPanel.h src/app/DiagnosticsPanel.cpp
    src/app/UiHelpers.h
    src/app/ShaderPreview.h src/app/ShaderPreview.cpp
)
qt_add_resources(ISFLab branding PREFIX "/" FILES assets/ISFLab-rounded.png)
set(live_source src/engine/input/NoLiveInput.cpp)
set(live_libraries)
if(APPLE)
    enable_language(C OBJC OBJCXX)
    set_property(TARGET ISFLabEngine PROPERTY OBJCXX_STANDARD 17)
    set(live_source src/engine/input/SyphonInput.mm)
    set_source_files_properties(src/engine/input/SyphonInput.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set(syphon_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/syphon")
    file(GLOB syphon_headers "${syphon_dir}/*.h")
    file(COPY ${syphon_headers} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/syphon-include/Syphon")
    file(GLOB syphon_sources "${syphon_dir}/*.m" "${syphon_dir}/*.c")
    list(FILTER syphon_sources EXCLUDE REGEX "Metal")
    add_library(isflab_syphon STATIC ${syphon_sources})
    set_target_properties(isflab_syphon PROPERTIES AUTOMOC OFF)
    target_include_directories(isflab_syphon PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/syphon-include" PRIVATE "${syphon_dir}")
    target_compile_definitions(isflab_syphon PRIVATE SYPHON_CORE_SHARE GL_SILENCE_DEPRECATION)
    target_compile_options(isflab_syphon PRIVATE "SHELL:-include \"${syphon_dir}/Syphon_Prefix.pch\""
        $<$<COMPILE_LANGUAGE:OBJC>:-fobjc-arc>)
    target_link_libraries(isflab_syphon PUBLIC "-framework Cocoa" "-framework OpenGL" "-framework IOSurface" "-framework Foundation")
    set(live_libraries isflab_syphon)
elseif(WIN32)
    set(live_source src/engine/input/SpoutInput.cpp)
    set(spout_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/spout/SPOUTSDK/SpoutGL")
    add_library(isflab_spout STATIC
        "${spout_dir}/Spout.cpp" "${spout_dir}/SpoutCopy.cpp" "${spout_dir}/SpoutDirectX.cpp"
        "${spout_dir}/SpoutFrameCount.cpp" "${spout_dir}/SpoutGL.cpp" "${spout_dir}/SpoutGLextensions.cpp"
        "${spout_dir}/SpoutReceiver.cpp" "${spout_dir}/SpoutSender.cpp" "${spout_dir}/SpoutSenderNames.cpp"
        "${spout_dir}/SpoutSharedMemory.cpp" "${spout_dir}/SpoutUtils.cpp")
    set_target_properties(isflab_spout PROPERTIES AUTOMOC OFF)
    target_include_directories(isflab_spout PUBLIC "${spout_dir}")
    target_compile_definitions(isflab_spout PUBLIC SPOUT_BUILD_STATIC)
    target_link_libraries(isflab_spout PUBLIC opengl32 d3d9 d3d11 dxgi version winmm shlwapi)
    set(live_libraries isflab_spout)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET gstreamer-app-1.0 gstreamer-video-1.0)
        if(GSTREAMER_FOUND)
            set(live_source src/engine/input/ShmInput.cpp)
            set(live_libraries PkgConfig::GSTREAMER)
        endif()
    endif()
endif()
target_sources(ISFLabEngine PRIVATE src/engine/input/LiveInput.h ${live_source})
target_link_libraries(ISFLabEngine PRIVATE ${live_libraries})

# Video outputs: the rendered picture shared with other software. Each one is optional at build time (an option, and
# its platform) and does nothing until it is enabled in the application.
#  - Syphon (macOS), Spout (Windows): the GPU texture is shared, never read back
#  - v4l2loopback (Linux): a virtual webcam; needs the kernel's V4L2 header, and the module at run time
#  - OMT (all platforms): network; libomt and libvmx are loaded at run time, no build-time dependency
option(ISFLAB_OUTPUT_SYPHON "Syphon video output (macOS)" ON)
option(ISFLAB_OUTPUT_SPOUT "Spout video output (Windows)" ON)
option(ISFLAB_OUTPUT_V4L2 "Virtual webcam video output through v4l2loopback (Linux)" ON)
option(ISFLAB_OUTPUT_OMT "OMT (Open Media Transport) network video output, libomt loaded at run time" ON)
target_sources(ISFLabEngine PRIVATE
    src/engine/output/VideoOutput.h src/engine/output/PixelConvert.h src/engine/output/PixelConvert.cpp
    src/engine/output/CpuSendThread.h src/engine/output/CpuSendThread.cpp
    src/engine/output/OutputManager.h src/engine/output/OutputManager.cpp
    src/engine/output/OutputNone.cpp)
target_include_directories(ISFLabEngine PUBLIC src/engine/output)
if(APPLE AND ISFLAB_OUTPUT_SYPHON)
    target_sources(ISFLabEngine PRIVATE src/engine/output/OutputSyphon.mm)
    set_source_files_properties(src/engine/output/OutputSyphon.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_compile_definitions(ISFLabEngine PRIVATE ISFLAB_OUTPUT_SYPHON)
endif()
if(WIN32 AND ISFLAB_OUTPUT_SPOUT)
    target_sources(ISFLabEngine PRIVATE src/engine/output/OutputSpout.cpp)
    target_compile_definitions(ISFLabEngine PRIVATE ISFLAB_OUTPUT_SPOUT)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND ISFLAB_OUTPUT_V4L2)
    include(CheckIncludeFileCXX)
    check_include_file_cxx(linux/videodev2.h isflab_has_v4l2)
    if(isflab_has_v4l2)
        target_sources(ISFLabEngine PRIVATE src/engine/output/OutputV4l2.cpp)
        target_compile_definitions(ISFLabEngine PRIVATE ISFLAB_OUTPUT_V4L2)
    else()
        message(WARNING "linux/videodev2.h not found: built without the virtual webcam output")
    endif()
endif()
if(ISFLAB_OUTPUT_OMT)
    target_sources(ISFLabEngine PRIVATE src/engine/output/OutputOmt.cpp)
    target_compile_definitions(ISFLabEngine PRIVATE ISFLAB_OUTPUT_OMT)
endif()
# Shared Pixel Stereo code (SliderField, ColorPicker), pinned as a submodule
set(PXS_BUILD_TESTS OFF)
add_subdirectory(third_party/pxs-qt pxs EXCLUDE_FROM_ALL)
target_link_libraries(ISFLab PRIVATE ISFLabEngine Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia pxs::theme pxs::widgets)
target_compile_definitions(ISFLab PRIVATE ISFLAB_VERSION="${ISFLAB_DISPLAY_VERSION}")
if(MSVC)
    target_compile_options(ISFLab PRIVATE /utf-8)
    target_compile_options(ISFLabEngine PRIVATE /utf-8)
    target_compile_options(ISFLabCore PRIVATE /utf-8)
endif()
# The application carries its version in its name ("ISFLab 0.1.5.app"), as NyxMap does.
# Linux keeps the plain executable name that the .desktop entry launches.
# Qt's Windows deploy script cannot take a space in the executable name: on Windows the build keeps
# ISFLab.exe and the CI renames it to "ISFLab 0.1.7.exe" once the install has deployed Qt.
set(ISFLAB_APP_NAME "ISFLab ${ISFLAB_DISPLAY_VERSION}")
if(APPLE)
    set_target_properties(ISFLab PROPERTIES OUTPUT_NAME "${ISFLAB_APP_NAME}")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/application.json"
    CONTENT "{\"name\":\"${ISFLAB_APP_NAME}\",\"version\":\"${ISFLAB_DISPLAY_VERSION}\",\"executable\":\"$<TARGET_FILE_NAME:ISFLab>\"}\n")
set_target_properties(ISFLab PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "studio.pixelstereo.isflab"
    MACOSX_BUNDLE_BUNDLE_NAME "${ISFLAB_APP_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Pixel Stereo"
    MACOSX_BUNDLE_ICON_FILE "ISFLab.icns"
)

if(WIN32)
    configure_file(src/app/windows.rc.in windows.rc @ONLY)
    target_sources(ISFLab PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
endif()

set(madmapper_resources
    third_party/madmapper/MadCommon.glsl
    third_party/madmapper/MadNoise.glsl
    third_party/madmapper/MadSDF.glsl
    third_party/madmapper/noiseLUT.png
    third_party/madmapper/LICENSE
    third_party/madmapper/LICENSE-webgl-noise
    third_party/madmapper/README.md
)
set(demo_shaders examples/ColorWave.fs examples/Invert.fs examples/Crossfade.fs)
if(APPLE)
    file(READ "${CMAKE_ROOT}/Modules/MacOSXBundleInfo.plist.in" macos_bundle_plist)
    string(REPLACE "</dict>" "    <key>NSMicrophoneUsageDescription</key>\n    <string>ISFLab uses audio input for audio-reactive shader previews.</string>\n</dict>"
        macos_bundle_plist "${macos_bundle_plist}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ISFLabInfo.plist.in"
        CONTENT "${macos_bundle_plist}" @ONLY)
    set_target_properties(ISFLab PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/ISFLabInfo.plist.in")
    if(TARGET Qt6::QDarwinMicrophonePermissionPlugin)
        qt_import_plugins(ISFLab INCLUDE Qt6::QDarwinMicrophonePermissionPlugin)
    endif()
    target_sources(ISFLab PRIVATE src/app/MacApplicationIcon.mm ${demo_shaders} third_party/syphon/License.txt assets/ISFLab.icns ${madmapper_resources})
    set_source_files_properties(${madmapper_resources} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/madmapper")
    target_link_libraries(ISFLab PRIVATE "-framework AppKit")
    set_source_files_properties(assets/ISFLab.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(third_party/syphon/License.txt PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses")
    set_source_files_properties(${demo_shaders} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/examples")
else()
    add_custom_command(TARGET ISFLab POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:ISFLab>/examples"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${demo_shaders} "$<TARGET_FILE_DIR:ISFLab>/examples"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

include(GNUInstallDirs)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install(FILES assets/studio.pixelstereo.isflab.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES assets/ISFLab-256.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps"
        RENAME studio.pixelstereo.isflab.png)
endif()
install(TARGETS ISFLab BUNDLE DESTINATION . RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32)
    install(FILES third_party/spout/LICENSE DESTINATION licenses RENAME Spout.txt)
endif()
if(NOT APPLE)
    install(FILES ${demo_shaders} DESTINATION examples)
    install(FILES ${madmapper_resources} DESTINATION "${CMAKE_INSTALL_DATADIR}/isflab/madmapper")
endif()
if(APPLE OR WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND Qt6_VERSION VERSION_GREATER_EQUAL 6.8))
    set(ISFLAB_MACOS_DEPLOY_LIBPATH "" CACHE PATH "Extra framework location for macdeployqt")
    set(deploy_options)
    if(APPLE AND ISFLAB_MACOS_DEPLOY_LIBPATH)
        list(APPEND deploy_options DEPLOY_TOOL_OPTIONS "-libpath=${ISFLAB_MACOS_DEPLOY_LIBPATH}")
    endif()
    qt_generate_deploy_app_script(TARGET ISFLab OUTPUT_SCRIPT deploy_script NO_UNSUPPORTED_PLATFORM_ERROR ${deploy_options})
    install(SCRIPT "${deploy_script}")
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(ISFLabLibraryTest tests/LibraryTest.cpp)
    target_compile_definitions(ISFLabLibraryTest PRIVATE ISFLAB_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples")
    target_link_libraries(ISFLabLibraryTest PRIVATE ISFLabCore)
    add_test(NAME library COMMAND ISFLabLibraryTest)
    add_executable(ISFLabScannerTest tests/ScannerTest.cpp)
    target_link_libraries(ISFLabScannerTest PRIVATE ISFLabCore)
    add_test(NAME scanner COMMAND ISFLabScannerTest)
    add_executable(ISFLabEngineTest tests/EngineTest.cpp)
    target_link_libraries(ISFLabEngineTest PRIVATE ISFLabEngine)
    add_test(NAME engine COMMAND ISFLabEngineTest)
    add_executable(ISFLabOutputTest tests/OutputTest.cpp)
    target_link_libraries(ISFLabOutputTest PRIVATE ISFLabEngine)
    add_test(NAME output COMMAND ISFLabOutputTest)
    if(ISFLAB_OUTPUT_OMT)
        # With the real libomt: skipped (77) when the library or its discovery service is not installed
        add_executable(ISFLabOmtTest tests/OmtTest.cpp)
        target_link_libraries(ISFLabOmtTest PRIVATE ISFLabEngine)
        add_test(NAME omt COMMAND ISFLabOmtTest)
        set_tests_properties(omt PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
    endif()
endif()

if(BUILD_TESTING)
    add_test(NAME archive COMMAND "${CMAKE_COMMAND}" -E tar tf "${CMAKE_CURRENT_BINARY_DIR}/test-library.zip")
    set_tests_properties(library PROPERTIES FIXTURES_SETUP exported_library)
    set_tests_properties(archive PROPERTIES FIXTURES_REQUIRED exported_library
        PASS_REGULAR_EXPRESSION "Transitions/Crossfade.fs")
endif()

if(BUILD_TESTING)
    # The Media Bin's filters, offscreen (no display or GPU needed)
    qt_add_executable(ISFLabMediaBinTest tests/MediaBinTest.cpp src/app/MediaBin.h src/app/MediaBin.cpp
        src/app/ShaderTypes.h src/app/ShaderTypes.cpp src/app/AppStyle.h src/app/AppStyle.cpp)
    target_include_directories(ISFLabMediaBinTest PRIVATE src/app)
    target_link_libraries(ISFLabMediaBinTest PRIVATE ISFLabCore Qt6::Widgets pxs::theme)
    add_test(NAME mediabin COMMAND ISFLabMediaBinTest)
    set_tests_properties(mediabin PROPERTIES TIMEOUT 60)
endif()

option(ISFLAB_TEST_OPENGL "Run the preview test in a graphical session" OFF)
if(BUILD_TESTING AND ISFLAB_TEST_OPENGL)
    add_executable(ISFLabPreviewTest tests/PreviewTest.cpp src/app/ShaderPreview.cpp)
    target_include_directories(ISFLabPreviewTest PRIVATE src/app)
    target_compile_definitions(ISFLabPreviewTest PRIVATE ISFLAB_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples")
    target_link_libraries(ISFLabPreviewTest PRIVATE ISFLabEngine Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia)
    add_test(NAME preview COMMAND ISFLabPreviewTest)
    set_tests_properties(preview PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
endif()

if(APPLE AND BUILD_TESTING AND ISFLAB_TEST_OPENGL)
    add_executable(ISFLabSyphonTest MACOSX_BUNDLE tests/SyphonTest.mm)
    set_target_properties(ISFLabSyphonTest PROPERTIES AUTOMOC OFF OBJCXX_STANDARD 17
        MACOSX_BUNDLE_GUI_IDENTIFIER "studio.pixelstereo.isflab.syphon-test")
    set_source_files_properties(tests/SyphonTest.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(ISFLabSyphonTest PRIVATE Qt6::Widgets Qt6::OpenGLWidgets ISFLabEngine)
    add_test(NAME syphon COMMAND "$<TARGET_FILE:ISFLabSyphonTest>" --automated)
    set_tests_properties(syphon PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
endif()
