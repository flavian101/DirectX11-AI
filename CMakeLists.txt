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

# meshoptimizer: simplifies meshes into levels of detail and re-orders triangles for the GPU's vertex cache (src/MeshLod.cpp).
# Small, C++ only. Offline? -DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER=<path>
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
set(MESHOPT_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_Declare(meshoptimizer
	GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
	GIT_TAG v0.22
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(meshoptimizer)

option(BUILD_EDITOR "Build the editor executable (needs extern/imgui on its docking branch)" ON)

# ---------------------------------------------------------------------------------------------
# Three pieces:
#   Engine         - everything that draws and simulates. Knows NOTHING about ImGui.
#   D3D11Example   - the GAME: main.cpp + Engine. This is what you ship.
#   D3D11Editor    - the EDITOR: EditorLayer + Dear ImGui + Engine. Never shipped.
# Shipping a game = building D3D11Example only. There is no editor code to strip out because
# it was never linked into it.
# ---------------------------------------------------------------------------------------------

# ---------------------------------------------------------------------------------------------
# Windows headers (d3d11.h pulls in windows.h) define min and max as MACROS unless NOMINMAX is set. A macro
# named max() rewrites every std::max(a, b) into garbage: "error C2589: '(': illegal token on right side of '::'".
# This is set for EVERY target from here down, not just Engine: the unit tests under tests/ do not link Engine,
# yet several include engine headers that reach d3d11.h (e.g. WaterPresetTests via SceneJson.h), so a flag that
# only Engine carried never reached them. It sits after the FetchContent calls on purpose, so Assimp and
# meshoptimizer keep their own settings. WIN32_LEAN_AND_MEAN (keeps windows.h small) moved here with it, so
# one line owns both.
# ---------------------------------------------------------------------------------------------
add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

add_library(Engine STATIC
	src/Window.cpp
	src/Direct3D11Renderer.cpp
	src/GpuTimer.cpp
	src/Logger.cpp
	src/Shader.cpp
	src/Model.cpp
	src/Cube.cpp
	src/Scene.cpp
	src/Camera.cpp
	src/Mesh.cpp
	src/Vertex.cpp
	src/ModelImporter.cpp
	src/MeshLod.cpp
	src/ProceduralModels.cpp
	src/ScatterPlacement.cpp
	src/ScatterTileGround.cpp
	src/ScatterSettings.cpp
	src/ScatterObject.cpp
	src/ModelObject.cpp
	src/RayPicking.cpp
	src/SceneSerializer.cpp
	src/Project.cpp
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
	src/TerrainChunkIndices.cpp
	src/TerrainChunkIndexCache.cpp
	src/WorldGround.cpp
	src/WorldGrid.cpp
	src/WorldPicture.cpp
	src/WorldShape.cpp
	src/WorldEdits.cpp
	src/WorldRivers.cpp
	src/TerrainWorld.cpp
	src/Heightfield.cpp
	src/HeightfieldEdit.cpp
	src/RiverCarve.cpp
	src/TerrainSplat.cpp
	src/TerrainPaint.cpp
	src/Png16.cpp
	src/Water.cpp
	src/Skybox.cpp
	src/SkyFaces.cpp
	src/LightObject.cpp
	src/ProceduralSky.cpp
	src/EnvironmentLighting.cpp
	src/libs/stb_image.cpp
)

# PUBLIC: everything that links Engine can write #include "Scene.h".
target_include_directories(Engine PUBLIC ${CMAKE_SOURCE_DIR}/src)

# Link against the Windows SDK libraries for DirectX 11
# The d3d11 and dxgi libraries are part of the SDK and are available on Windows
# dxguid supplies the interface IDs (e.g. for D3DReflect in Shader.cpp); MSVC can hide the need, MinGW cannot.
target_link_libraries(Engine PUBLIC d3d11 dxgi d3dcompiler dxguid imm32 dwmapi assimp::assimp nlohmann_json::nlohmann_json meshoptimizer)

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
    add_executable(WaterSettingsTests tests/water_settings_test.cpp)
    target_include_directories(WaterSettingsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WaterSettings COMMAND WaterSettingsTests)
    add_executable(WaterPresetTests tests/water_preset_test.cpp)
    target_include_directories(WaterPresetTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(WaterPresetTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME WaterPreset COMMAND WaterPresetTests)
    add_executable(BuoyancyTests tests/buoyancy_test.cpp)
    target_include_directories(BuoyancyTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME Buoyancy COMMAND BuoyancyTests)
    add_executable(ProjectTests tests/project_test.cpp src/Project.cpp)
    target_include_directories(ProjectTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(ProjectTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME Project COMMAND ProjectTests)
    # Sculpting (lesson 27): brushes, the chunk-refresh rule, and the 16-bit PNG writer (read back with stb_image).
    add_executable(HeightfieldEditTests tests/heightfield_edit_test.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(HeightfieldEditTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME HeightfieldEdit COMMAND HeightfieldEditTests)
    add_executable(TerrainChunksTests tests/terrain_chunks_test.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(TerrainChunksTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME TerrainChunks COMMAND TerrainChunksTests)
    add_executable(TerrainSplatTests tests/terrain_splat_test.cpp src/TerrainSplat.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(TerrainSplatTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    find_package(Threads REQUIRED)
    target_link_libraries(TerrainSplatTests PRIVATE Threads::Threads)
    add_test(NAME TerrainSplat COMMAND TerrainSplatTests)
    # Painted layers (lesson 30): the brush, the overlay on the baked splat, undo patches, saving.
    add_executable(TerrainPaintTests tests/terrain_paint_test.cpp src/TerrainPaint.cpp src/TerrainSplat.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(TerrainPaintTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(TerrainPaintTests PRIVATE Threads::Threads)   # TerrainSplat.cpp bakes on several threads
    add_test(NAME TerrainPaint COMMAND TerrainPaintTests)
    # Scatter, culling and levels of detail (lesson 31): where trees stand, which level each one gets, the automatic
    # simplifier, and the built-in models.
    add_executable(ScatterPlacementTests tests/scatter_placement_test.cpp src/ScatterPlacement.cpp src/Heightfield.cpp)
    target_include_directories(ScatterPlacementTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ScatterPlacement COMMAND ScatterPlacementTests)
    # The scatter settings (lesson 36): variants, their limits, the translation into placement rules, old scene files.
    add_executable(ScatterSettingsTests tests/scatter_settings_test.cpp src/ScatterSettings.cpp src/ScatterPlacement.cpp src/Heightfield.cpp)
    target_include_directories(ScatterSettingsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(ScatterSettingsTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME ScatterSettings COMMAND ScatterSettingsTests)
    # The starter scatter must not move one tree while the scatter code is cleaned up (roadmap S11) or moved to biomes (S4).
    add_executable(ScatterGoldenTests tests/scatter_golden_test.cpp src/ScatterSettings.cpp src/ScatterPlacement.cpp src/Heightfield.cpp)
    target_include_directories(ScatterGoldenTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ScatterGolden COMMAND ScatterGoldenTests)
    add_executable(LodSelectTests tests/lod_select_test.cpp)
    target_include_directories(LodSelectTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME LodSelect COMMAND LodSelectTests)
    add_executable(MeshLodTests tests/mesh_lod_test.cpp src/MeshLod.cpp)
    target_include_directories(MeshLodTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(MeshLodTests PRIVATE meshoptimizer)
    add_test(NAME MeshLod COMMAND MeshLodTests)
    add_executable(ProceduralModelsTests tests/procedural_models_test.cpp src/ProceduralModels.cpp src/Vertex.cpp)
    target_include_directories(ProceduralModelsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME ProceduralModels COMMAND ProceduralModelsTests)
    add_executable(Png16Tests tests/png16_test.cpp src/Png16.cpp src/libs/stb_image.cpp)
    target_include_directories(Png16Tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME Png16 COMMAND Png16Tests)
    # Skybox face names (px/nx, right/left, 0-5...) and the Assets panel's thumbnail shrinker.
    add_executable(SkyFacesTests tests/sky_faces_test.cpp src/SkyFaces.cpp)
    target_include_directories(SkyFacesTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME SkyFaces COMMAND SkyFacesTests)
    # How a sun or a moon is aimed: azimuth/elevation <-> direction <-> the gizmo's rotation.
    add_executable(LightAnglesTests tests/light_angles_test.cpp)
    target_include_directories(LightAnglesTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME LightAngles COMMAND LightAnglesTests)
    # The sun's path, the light fades and the procedural sky's colours; and the underwater fog look. Pure maths: no GPU.
    add_executable(TimeOfDayTests tests/time_of_day_test.cpp)
    target_include_directories(TimeOfDayTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME TimeOfDay COMMAND TimeOfDayTests)
    add_executable(UnderwaterLookTests tests/underwater_look_test.cpp)
    target_include_directories(UnderwaterLookTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME UnderwaterLook COMMAND UnderwaterLookTests)
    # Rivers as part of the world's height function (lesson 51): agrees with the single-terrain carve, no seams, no edits far away.
    add_executable(WorldRiversTests tests/world_rivers_test.cpp src/WorldRivers.cpp src/WorldGrid.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(WorldRiversTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WorldRivers COMMAND WorldRiversTests)
    add_executable(RiverTests tests/river_test.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(RiverTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME River COMMAND RiverTests)
    # The reversed-Z depth convention (lesson 46): the end points, the inverse the water shader uses, and why we reversed.
    add_executable(DepthMathTests tests/depth_math_test.cpp)
    target_include_directories(DepthMathTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME DepthMath COMMAND DepthMathTests)
    # The world as a grid of tiles (lesson 47): tile maths, shared edges, the apron, seamless normals. Pure maths: no GPU.
    add_executable(WorldGridTests tests/world_grid_test.cpp src/WorldGrid.cpp src/TerrainSplat.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(WorldGridTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WorldGrid COMMAND WorldGridTests)
    add_executable(WorldPictureTests tests/world_picture_test.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/Heightfield.cpp src/Png16.cpp src/libs/stb_image.cpp)
    target_include_directories(WorldPictureTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WorldPicture COMMAND WorldPictureTests)
    # Heightmap customisation (levels, curve, smoothing, bicubic, detail noise) and auto levels.
    add_executable(WorldShapeTests tests/world_shape_test.cpp src/WorldShape.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(WorldShapeTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WorldShape COMMAND WorldShapeTests)
    # Sculpting and painting stored per tile, brush parity with the old terrain, and the migration of an old terrain.
    add_executable(WorldEditsTests tests/world_edits_test.cpp src/WorldEdits.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/HeightfieldEdit.cpp src/TerrainPaint.cpp src/TerrainSplat.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(WorldEditsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(WorldEditsTests PRIVATE Threads::Threads)   # TerrainSplat.cpp bakes on several threads
    add_test(NAME WorldEdits COMMAND WorldEditsTests)
    # Patching a loaded tile in place (roadmap T2): which samples a stroke reaches, and that patched == rebuilt.
    add_executable(TilePatchTests tests/tile_patch_test.cpp src/WorldEdits.cpp src/WorldRivers.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/TerrainPaint.cpp src/TerrainSplat.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(TilePatchTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(TilePatchTests PRIVATE Threads::Threads)
    add_test(NAME TilePatch COMMAND TilePatchTests)
    # The index buffers every terrain chunk of one size shares (roadmap T3).
    add_executable(TerrainChunkIndicesTests tests/terrain_chunk_indices_test.cpp src/TerrainChunkIndices.cpp)
    target_include_directories(TerrainChunkIndicesTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME TerrainChunkIndices COMMAND TerrainChunkIndicesTests)
    # The walls that close the edge of a bounded world (roadmap X1).
    add_executable(TerrainWallsTests tests/terrain_walls_test.cpp src/WorldGrid.cpp src/TerrainSplat.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp)
    target_include_directories(TerrainWallsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME TerrainWalls COMMAND TerrainWallsTests)
    # The water-terrain contract: the water measures the world's ground function, not its loaded tiles (roadmap W1).
    add_executable(WaterVisibilityTests tests/water_visibility_test.cpp src/WorldGround.cpp src/WorldRivers.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(WaterVisibilityTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(WaterVisibilityTests PRIVATE Threads::Threads)
    add_test(NAME WaterVisibility COMMAND WaterVisibilityTests)
    # The ground at one point: height, normal and a ray (roadmap C1).
    add_executable(WorldHeightTests tests/world_height_test.cpp src/WorldGround.cpp src/WorldRivers.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(WorldHeightTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(WorldHeightTests PRIVATE Threads::Threads)
    add_test(NAME WorldHeight COMMAND WorldHeightTests)
    # The file of one baked tile (roadmap C6).
    add_executable(WorldTileFileTests tests/world_tile_file_test.cpp src/WorldTileFile.cpp)
    target_include_directories(WorldTileFileTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME WorldTileFile COMMAND WorldTileFileTests)
    # The scatter's ground over the tiles of a world (roadmap S12).
    add_executable(ScatterTileGroundTests tests/scatter_tile_ground_test.cpp src/ScatterTileGround.cpp src/ScatterPlacement.cpp src/TerrainSplat.cpp src/WorldRivers.cpp src/WorldGrid.cpp src/WorldPicture.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(ScatterTileGroundTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(ScatterTileGroundTests PRIVATE Threads::Threads)
    add_test(NAME ScatterTileGround COMMAND ScatterTileGroundTests)
    # The two heights of a world (how tall, how high) and a migrated terrain keeping its Y (roadmaps H1, C3).
    add_executable(WorldBaseHeightTests tests/world_base_height_test.cpp src/WorldGround.cpp src/WorldRivers.cpp src/WorldPicture.cpp src/WorldGrid.cpp src/RiverCarve.cpp src/HeightfieldEdit.cpp src/Heightfield.cpp src/libs/stb_image.cpp)
    target_include_directories(WorldBaseHeightTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(WorldBaseHeightTests PRIVATE Threads::Threads)
    add_test(NAME WorldBaseHeight COMMAND WorldBaseHeightTests)
    # The new height system (roadmap H1 to H5): a picture becomes a grid of metres. Run from the repository root (reads assets/).
    add_executable(HeightRasterTests tests/height_raster_test.cpp src/HeightDeterrace.cpp src/HeightPicture.cpp src/HeightRaster.cpp src/Png16.cpp src/libs/stb_image.cpp)
    target_include_directories(HeightRasterTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME HeightRaster COMMAND HeightRasterTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_executable(HeightStatsTests tests/height_stats_test.cpp src/HeightStats.cpp src/HeightDeterrace.cpp src/HeightPicture.cpp src/HeightRaster.cpp src/libs/stb_image.cpp)
    target_include_directories(HeightStatsTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME HeightStats COMMAND HeightStatsTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_executable(HeightSampleTests tests/height_sample_test.cpp src/HeightSample.cpp src/HeightDeterrace.cpp src/HeightPicture.cpp src/HeightRaster.cpp src/libs/stb_image.cpp)
    target_include_directories(HeightSampleTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME HeightSample COMMAND HeightSampleTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_executable(HeightDeterraceTests tests/height_deterrace_test.cpp src/HeightDeterrace.cpp src/HeightPicture.cpp src/HeightRaster.cpp src/libs/stb_image.cpp)
    target_include_directories(HeightDeterraceTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_test(NAME HeightDeterrace COMMAND HeightDeterraceTests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    add_executable(ImageDownscaleTests tests/image_downscale_test.cpp)
    target_include_directories(ImageDownscaleTests PRIVATE ${CMAKE_SOURCE_DIR}/editor)
    add_test(NAME ImageDownscale COMMAND ImageDownscaleTests)
    # docs/ROADMAP.md is checked against the tests and the CMake file: see "Tracking" in the roadmap.
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME Roadmap COMMAND ${Python3_EXECUTABLE} tools/roadmap.py check WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    endif()
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
            editor/ScatterInspector.cpp
            editor/FileDialogs.cpp
            editor/EditorTheme.cpp
            editor/ConsolePanel.cpp
            editor/AssetBrowserPanel.cpp
            editor/EditorIcons.cpp
            editor/ThumbnailCache.cpp
            editor/ScenesPanel.cpp
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
        target_link_libraries(D3D11Editor PRIVATE Engine comdlg32 shell32)   # comdlg32: the Open File dialog; shell32: "Show in Explorer"
        sync_runtime_files(D3D11Editor)
    else()
        message(WARNING "extern/imgui is empty - skipping the editor. Run: git submodule update --init")
    endif()
endif()
