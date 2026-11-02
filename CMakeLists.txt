cmake_minimum_required(VERSION 3.20)
project(D3D11Example LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


add_executable(D3D11Example
	src/main.cpp
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
	src/Texture.cpp
	src/Light.cpp
    src/libs/stb_image.cpp
	extern/imgui/imgui.cpp
	extern/imgui/imgui_demo.cpp
	extern/imgui/imgui_draw.cpp
	extern/imgui/imgui_tables.cpp
	extern/imgui/imgui_widgets.cpp
	extern/imgui/backends/imgui_impl_win32.cpp
	extern/imgui/backends/imgui_impl_dx11.cpp
)

target_include_directories(D3D11Example PRIVATE
	${CMAKE_SOURCE_DIR}/extern/imgui
	${CMAKE_SOURCE_DIR}/extern/imgui/backends
)

# Link against the Windows SDK libraries for DirectX 11
# The d3d11 and dxgi libraries are part of the SDK and are available on Windows
target_link_libraries(D3D11Example PRIVATE d3d11 dxgi d3dcompiler imm32 dwmapi)


add_custom_command(
    TARGET D3D11Example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders
        $<TARGET_FILE_DIR:D3D11Example>/shaders
)

add_custom_command(
    TARGET D3D11Example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:D3D11Example>/assets
)
