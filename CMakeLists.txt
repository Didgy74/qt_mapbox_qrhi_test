cmake_minimum_required(VERSION 3.25)
project(qt_map_hw LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    ShaderTools)
qt_standard_project_setup()

include(FetchContent)

# This library might be problematic because it may throw
# exceptions
FetchContent_Declare(
  CDT
  GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
  GIT_TAG master
)
FetchContent_MakeAvailable(CDT)

add_subdirectory(${cdt_SOURCE_DIR}/CDT ${cdt_BINARY_DIR})

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG master
)
FetchContent_MakeAvailable(glm)

find_package(protobuf CONFIG REQUIRED)

qt_add_executable(qt_map_hw WIN32 MACOSX_BUNDLE
    main.cpp
    qquickmap.cpp qquickmap.h
    Evaluator.h Evaluator.cpp
    LayerStyle.h LayerStyle.cpp
    tileloader.h tileloader.cpp
    MapboxGeometryDecoding.h MapboxGeometryDecoding.cpp
    vector_tile.pb.h vector_tile.pb.cc
)

target_link_libraries(qt_map_hw PUBLIC
    Qt6::Core
    Qt6::GuiPrivate
    Qt6::Qml
    Qt6::Quick
)

target_link_libraries(qt_map_hw PUBLIC
    CDT
    glm
    protobuf::libprotobuf protobuf::libprotoc protobuf::libprotobuf-lite
)

#target_include_directories(qt_map_hw PUBLIC external/protobuf)

qt_add_qml_module(qt_map_hw
    URI MyQmlApp
    VERSION 1.0
    QML_FILES
        Main.qml
    RESOURCE_PREFIX "/"
    NO_RESOURCE_TARGET_PATH
)
qt6_add_shaders(qt_map_hw "shaders"
    PREFIX
    "/"
    FILES
    "shaders/shader.vert"
    "shaders/shader.frag"
    "shaders/background.vert"
    "shaders/background.frag"
)

#qt_add_protobuf(qt_map_hw
#    PROTO_FILES
#    vector_tile.proto
#)

qt_add_resources(qt_map_hw "externalResources"
    PREFIX "/"
    FILES
    styleSheet-basic.json
    styleSheet-bright.json
)

install(TARGETS qt_map_hw
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_qml_app_script(
    TARGET qt_map_hw
    OUTPUT_SCRIPT deploy_script
    MACOS_BUNDLE_POST_BUILD
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
)
install(SCRIPT ${deploy_script})

# If on Windows:
# Setup a function to apply the windeployqt.exe tool to a given executable target.
# This copies the necessary .dll files into the executables folder.
if (WIN32 AND NOT DEFINED WINDEPLOYQT_EXECUTABLE)
    message(WARNING "Qt tool windeployqt not found. Executables might not be able to run outside Qt Creator.")
endif()
function(deploy_runtime_dependencies_if_win32 target)
    if (DEFINED WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
            ARGS --no-translations --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw $<TARGET_FILE:${target}>
            COMMENT "Running windeployqt on target..."
        )
    endif()
endfunction()
deploy_runtime_dependencies_if_win32(qt_map_hw)


# If on Android, the MapTiler key must be supplied through the
# MAPTILER_KEY variable.
if(ANDROID)
    if(NOT DEFINED MAPTILER_KEY OR "${MAPTILER_KEY}" STREQUAL "" OR "${MAPTILER_KEY}" STREQUAL "<UNSET>")
        message(FATAL_ERROR "No MapTiler key set.")
    endif()
    target_compile_definitions(qt_map_hw PUBLIC MAPTILER_KEY="${MAPTILER_KEY}")

    # Now link OpenSSL to enable networking.
    if (NOT DEFINED ANDROID_SDK_ROOT)
        message(FATAL_ERROR "Error. No ANDROID SDK_ROOT variable set.")
    endif()
    set(android_openssl_SOURCE_DIR "${ANDROID_SDK_ROOT}/android_openssl")
    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)

    add_android_openssl_libraries(qt_map_hw)
endif()
