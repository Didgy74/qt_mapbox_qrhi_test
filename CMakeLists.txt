cmake_minimum_required(VERSION 3.25)
project(qt_map_hw LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(FetchContent)

# Specify the repository and the tag/commit/branch you want to fetch
FetchContent_Declare(
  CDT
  GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
  GIT_TAG master  # You can specify a specific commit, tag, or branch here
)

# Fetch the content
FetchContent_MakeAvailable(CDT)

# Add the CDT library as a subdirectory
add_subdirectory(${cdt_SOURCE_DIR}/CDT ${cdt_BINARY_DIR})

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    ShaderTools
    Protobuf
    ProtobufTools)

qt_standard_project_setup()

qt_add_executable(qt_map_hw WIN32 MACOSX_BUNDLE
    main.cpp
    qquickmap.cpp qquickmap.h
    Evaluator.h Evaluator.cpp
    LayerStyle.h LayerStyle.cpp
    MapboxGeometryDecoding.h MapboxGeometryDecoding.cpp
)

target_link_libraries(qt_map_hw PUBLIC
    Qt6::Core
    Qt6::GuiPrivate
    Qt6::Qml
    Qt6::Quick
    Qt6::Protobuf
    CDT
)

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

qt_add_protobuf(qt_map_hw
    PROTO_FILES
    vector_tile.proto
)

qt_add_resources(qt_map_hw "externalResources"
    PREFIX "/"
    FILES
    z0x0y0.mvt
    styleSheet.json
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
