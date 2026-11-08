cmake_minimum_required(VERSION 3.22)
project(D3D11Example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------------------------
# Assimp reads model files (glTF/GLB, FBX, OBJ). It is downloaded and built from source the first
# time you configure (a few minutes, once). Only the three importers we use are compiled, and
# exporters, tools and tests are off, to keep that build small.
#   Offline/local source: pass -DFETCHCONTENT_SOURCE_DIR_ASSIMP=<path to assimp>
#   For the bundled submodule, use -DFETCHCONTENT_SOURCE_DIR_ASSIMP=${CMAKE_SOURCE_DIR}/extern/assimp
# ---------------------------------------------------------------------------------------------
include(FetchContent)
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_ASSIMP AND EXISTS "${CMAKE_SOURCE_DIR}/extern/assimp/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_ASSIMP "${CMAKE_SOURCE_DIR}/extern/assimp" CACHE PATH
        "Use the checked-out Assimp source instead of downloading it")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
	GIT_REPOSITORY https://github.com/assimp/assimp.git
	GIT_TAG v5.4.3
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(assimp)

# nlohmann/json: reads and writes the scene files. Header-only. Offline? -DFETCHCONTENT_SOURCE_DIR_JSON=<path>
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

option(BUILD_EDITOR "Build the editor executable (needs extern/imgui on its docking branch)" ON)

# ---------------------------------------------------------------------------------------------
# Three pieces:
#   Engine         - everything that draws and simulates. Knows NOTHING about ImGui.
#   D3D11Example   - the GAME: main.cpp + Engine. This is what you ship.
#   D3D11Editor    - the EDITOR: EditorLayer + Dear ImGui + Engine. Never shipped.
# Shipping a game = building D3D11Example only. There is no editor code to strip out because
# it was never linked into it.
# ---------------------------------------------------------------------------------------------
add_library(Engine STATIC
	src/Window.cpp
	src/Direct3D11Renderer.cpp
	src/Logger.cpp
	src/Shader.cpp
	src/Model.cpp
	src/Cube.cpp
	src/Scene.cpp
	src/Camera.cpp
	src/Mesh.cpp
	src/Vertex.cpp
	src/ModelImporter.cpp
	src/ModelObject.cpp
	src/RayPicking.cpp
	src/SceneSerializer.cpp
	src/Renderable.cpp
	src/Material.cpp
	src/SurfaceMaterial.cpp
	src/Texture.cpp
	src/TextureArray.cpp
	src/Light.cpp
	src/FrameConstants.cpp
	src/DepthTarget.cpp
	src/ShadowMap.cpp
	src/RenderTarget.cpp
	src/Frustum.cpp
	src/PostProcess.cpp
	src/DemoScene.cpp
	src/Terrain.cpp
	src/Heightfield.cpp
	src/Water.cpp
	src/Skybox.cpp
	src/EnvironmentLighting.cpp
	src/libs/stb_image.cpp
)

# PUBLIC: everything that links Engine can write #include "Scene.h".
target_include_directories(Engine PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(Engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

# Link against the Windows SDK libraries for DirectX 11
# The d3d11 and dxgi libraries are part of the SDK and are available on Windows
# dxguid supplies the interface IDs (e.g. for D3DReflect in Shader.cpp); MSVC can hide the need, MinGW cannot.
target_link_libraries(Engine PUBLIC d3d11 dxgi d3dcompiler dxguid imm32 dwmapi assimp::assimp nlohmann_json::nlohmann_json)

add_executable(D3D11Example src/main.cpp)
target_link_libraries(D3D11Example PRIVATE Engine)

# Shaders are loaded from disk at runtime. A POST_BUILD step only runs when the exe
# relinks, so editing just a .hlsl file used to leave a stale copy next to the exe.
# A custom target in ALL re-copies them on every build instead.
function(sync_runtime_files target)
    add_custom_target(${target}_SyncShaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders
            $<TARGET_FILE_DIR:${target}>/shaders
    )
    add_dependencies(${target} ${target}_SyncShaders)
    add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:${target}>/assets
    )
endfunction()

sync_runtime_files(D3D11Example)

# ---------------------------------------------------------------------------------------------
# Unit tests for the pure-logic pieces (no Direct3D, no window): build, then run `ctest` or the exe directly.
# ---------------------------------------------------------------------------------------------
option(BUILD_TESTS "Build the unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(HeightfieldTests tests/heightfield_test.cpp src/Heightfield.cpp)
    target_include_directories(HeightfieldTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME Heightfield COMMAND HeightfieldTests)
    add_executable(WaterTests tests/water_test.cpp)
    target_include_directories(WaterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME Water COMMAND WaterTests)
endif()

if(BUILD_EDITOR)
    if(EXISTS ${CMAKE_SOURCE_DIR}/extern/imgui/imgui.cpp)
        # ImGuizmo draws the move / rotate / scale handles. Only the editor uses it. SOURCE_SUBDIR names a folder
        # that does not exist, so FetchContent downloads the files but does not try to build ImGuizmo's own
        # project (we compile the one file we need below). Offline? -DFETCHCONTENT_SOURCE_DIR_IMGUIZMO=<path>
        FetchContent_Declare(imguizmo
            GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
            GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2
            SOURCE_SUBDIR does-not-exist
        )
        FetchContent_MakeAvailable(imguizmo)

        add_executable(D3D11Editor
            editor/main_editor.cpp
            editor/EditorLayer.cpp
            extern/imgui/imgui.cpp
            extern/imgui/imgui_demo.cpp
            extern/imgui/imgui_draw.cpp
            extern/imgui/imgui_tables.cpp
            extern/imgui/imgui_widgets.cpp
            extern/imgui/backends/imgui_impl_win32.cpp
            extern/imgui/backends/imgui_impl_dx11.cpp
            ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
        )
        target_include_directories(D3D11Editor PRIVATE
            ${CMAKE_SOURCE_DIR}/editor
            ${CMAKE_SOURCE_DIR}/extern/imgui
            ${CMAKE_SOURCE_DIR}/extern/imgui/backends
            ${imguizmo_SOURCE_DIR}/src
        )
        target_link_libraries(D3D11Editor PRIVATE Engine comdlg32)   # comdlg32: the Open File dialog
        sync_runtime_files(D3D11Editor)
    else()
        message(WARNING "extern/imgui is empty - skipping the editor. Run: git submodule update --init")
    endif()
endif()
