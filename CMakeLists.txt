cmake_minimum_required(VERSION 3.20)
project(D3D11Example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
	src/Renderable.cpp
	src/Material.cpp
	src/SurfaceMaterial.cpp
	src/Texture.cpp
	src/Light.cpp
	src/FrameConstants.cpp
	src/DepthTarget.cpp
	src/ShadowMap.cpp
	src/RenderTarget.cpp
	src/Frustum.cpp
	src/PostProcess.cpp
	src/DemoScene.cpp
	src/Terrain.cpp
	src/Water.cpp
	src/Skybox.cpp
	src/libs/stb_image.cpp
)

# PUBLIC: everything that links Engine can write #include "Scene.h".
target_include_directories(Engine PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Link against the Windows SDK libraries for DirectX 11
# The d3d11 and dxgi libraries are part of the SDK and are available on Windows
# dxguid supplies the interface IDs (e.g. for D3DReflect in Shader.cpp); MSVC can hide the need, MinGW cannot.
target_link_libraries(Engine PUBLIC d3d11 dxgi d3dcompiler dxguid imm32 dwmapi)

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

if(BUILD_EDITOR)
    if(EXISTS ${CMAKE_SOURCE_DIR}/extern/imgui/imgui.cpp)
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
        )
        target_include_directories(D3D11Editor PRIVATE
            ${CMAKE_SOURCE_DIR}/editor
            ${CMAKE_SOURCE_DIR}/extern/imgui
            ${CMAKE_SOURCE_DIR}/extern/imgui/backends
        )
        target_link_libraries(D3D11Editor PRIVATE Engine)
        sync_runtime_files(D3D11Editor)
    else()
        message(WARNING "extern/imgui is empty - skipping the editor. Run: git submodule update --init")
    endif()
endif()
