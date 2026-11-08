cmake_minimum_required(VERSION 3.21)
set(ISFLAB_BUILD_VERSION "0.0.1" CACHE STRING "ISFLab application version")
project(ISFLab VERSION ${ISFLAB_BUILD_VERSION} LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
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

add_library(ISFLabEngine STATIC src/engine/ShaderEngine.h src/engine/ShaderEngine.cpp)
target_include_directories(ISFLabEngine PUBLIC src/engine src/engine/input)
target_link_libraries(ISFLabEngine PUBLIC ISFLabCore Qt6::Gui Qt6::OpenGL)

qt_add_executable(ISFLab WIN32 MACOSX_BUNDLE
    src/app/main.cpp
    src/app/AppStyle.h src/app/AppStyle.cpp
    src/app/MainWindow.h src/app/MainWindow.cpp
    src/app/ShaderPreview.h src/app/ShaderPreview.cpp
    src/app/SliderField.h src/app/SliderField.cpp
    src/app/ColorPicker.h src/app/ColorPicker.cpp
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
target_link_libraries(ISFLab PRIVATE ISFLabEngine Qt6::Widgets Qt6::OpenGLWidgets)
target_compile_definitions(ISFLab PRIVATE ISFLAB_VERSION="${PROJECT_VERSION}")
if(MSVC)
    target_compile_options(ISFLab PRIVATE /utf-8)
    target_compile_options(ISFLabEngine PRIVATE /utf-8)
    target_compile_options(ISFLabCore PRIVATE /utf-8)
endif()
set_target_properties(ISFLab PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "studio.pixelstereo.isflab"
    MACOSX_BUNDLE_BUNDLE_NAME "ISFLab"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Pixel Stereo"
    MACOSX_BUNDLE_ICON_FILE "ISFLab.icns"
)

if(WIN32)
    configure_file(src/app/windows.rc.in windows.rc @ONLY)
    target_sources(ISFLab PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows.rc")
endif()

set(demo_shaders examples/ColorWave.fs examples/Invert.fs examples/Crossfade.fs)
if(APPLE)
    target_sources(ISFLab PRIVATE src/app/MacApplicationIcon.mm ${demo_shaders} third_party/syphon/License.txt assets/ISFLab.icns)
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
endif()

if(BUILD_TESTING)
    add_test(NAME archive COMMAND "${CMAKE_COMMAND}" -E tar tf "${CMAKE_CURRENT_BINARY_DIR}/test-library.zip")
    set_tests_properties(library PROPERTIES FIXTURES_SETUP exported_library)
    set_tests_properties(archive PROPERTIES FIXTURES_REQUIRED exported_library
        PASS_REGULAR_EXPRESSION "Transitions/Crossfade.fs")
endif()

option(ISFLAB_TEST_OPENGL "Run the preview test in a graphical session" OFF)
if(BUILD_TESTING AND ISFLAB_TEST_OPENGL)
    add_executable(ISFLabPreviewTest tests/PreviewTest.cpp src/app/ShaderPreview.cpp)
    target_include_directories(ISFLabPreviewTest PRIVATE src/app)
    target_compile_definitions(ISFLabPreviewTest PRIVATE ISFLAB_EXAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/examples")
    target_link_libraries(ISFLabPreviewTest PRIVATE ISFLabEngine Qt6::Widgets Qt6::OpenGLWidgets)
    add_test(NAME preview COMMAND ISFLabPreviewTest)
    set_tests_properties(preview PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77)
endif()

if(APPLE AND BUILD_TESTING AND ISFLAB_TEST_OPENGL)
    add_executable(ISFLabSyphonTest MACOSX_BUNDLE tests/SyphonTest.mm src/engine/input/SyphonInput.mm)
    set_target_properties(ISFLabSyphonTest PROPERTIES AUTOMOC OFF OBJCXX_STANDARD 17
        MACOSX_BUNDLE_GUI_IDENTIFIER "studio.pixelstereo.isflab.syphon-test")
    set_source_files_properties(tests/SyphonTest.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(ISFLabSyphonTest PRIVATE src/engine/input)
    target_link_libraries(ISFLabSyphonTest PRIVATE Qt6::Widgets Qt6::OpenGLWidgets isflab_syphon)
endif()
