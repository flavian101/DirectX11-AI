cmake_minimum_required(VERSION 3.20)
project(D3D11Example LANGUAGES CXX)

add_executable(D3D11Example WIN32 src/main.cpp)

# Link against the Windows SDK libraries for DirectX 11
# The d3d11 and dxgi libraries are part of the SDK and are available on Windows
target_link_libraries(D3D11Example PRIVATE d3d11 dxgi)
