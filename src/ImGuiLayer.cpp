#include "ImGuiLayer.h"
#include "Skybox.h"
#include "imgui.h"
#include <DirectXMath.h>

using namespace DirectX;

namespace {
void DrawFloat3(const char* label, XMFLOAT3& value, float speed, float minValue, float maxValue)
{
    float values[3] = { value.x, value.y, value.z };
    if (ImGui::DragFloat3(label, values, speed, minValue, maxValue)) {
        value = { values[0], values[1], values[2] };
    }
}
}

ImGuiLayer::ImGuiLayer(Scene* scene) : scene_(scene) {}

void ImGuiLayer::Draw()
{
    if (scene_) DrawCollectionWindow();
}

void ImGuiLayer::DrawCollectionWindow()
{
    if (!ImGui::Begin("Scene Collection", nullptr, ImGuiWindowFlags_MenuBar)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            if (ImGui::MenuItem("Select Camera")) selectionKind_ = SelectionKind::Camera;
            if (ImGui::MenuItem("Select Lights")) selectionKind_ = SelectionKind::Lights;
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::BeginChild("Hierarchy", ImVec2(230.0f, 0.0f), true);
    ImGui::Text("Collection");
    ImGui::Separator();
    if (ImGui::Selectable("Camera", selectionKind_ == SelectionKind::Camera)) {
        selectionKind_ = SelectionKind::Camera;
    }
    if (ImGui::Selectable("Lights", selectionKind_ == SelectionKind::Lights)) {
        selectionKind_ = SelectionKind::Lights;
    }
    ImGui::Separator();

    const auto& objects = scene_->GetObjects();
    if (selectedObject_ >= static_cast<int>(objects.size())) selectedObject_ = -1;
    for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
        SceneObject* object = objects[index].get();
        if (!object) continue;
        if (ImGui::Selectable(object->GetEditorName(),
                              selectionKind_ == SelectionKind::Object && selectedObject_ == index)) {
            selectionKind_ = SelectionKind::Object;
            selectedObject_ = index;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Inspector", ImVec2(0.0f, 0.0f), true);
    if (selectionKind_ == SelectionKind::Camera) {
        DrawCameraInspector();
    } else if (selectionKind_ == SelectionKind::Lights) {
        DrawLightsInspector();
    } else if (selectedObject_ >= 0 && selectedObject_ < static_cast<int>(objects.size())) {
        DrawObjectInspector(objects[selectedObject_].get());
    } else {
        ImGui::Text("Select an item from the collection.");
    }
    ImGui::EndChild();
    ImGui::End();
}

void ImGuiLayer::DrawObjectInspector(SceneObject* object)
{
    if (!object) return;
    ImGui::Text("Object: %s", object->GetEditorName());
    ImGui::Separator();
    DrawTransformEditor(object);

    if (auto* terrain = dynamic_cast<Terrain*>(object)) DrawTerrainInspector(terrain);
    if (auto* water = dynamic_cast<Water*>(object)) DrawWaterInspector(water);
    if (auto* skybox = dynamic_cast<Skybox*>(object)) DrawSkyboxInspector(skybox);
}

void ImGuiLayer::DrawTransformEditor(SceneObject* object)
{
    XMFLOAT3 position;
    XMFLOAT3 rotation;
    XMFLOAT3 scale;
    if (!object->GetEditorTransform(position, rotation, scale)) {
        ImGui::Text("Transform editing is not available for this object.");
        return;
    }

    bool changed = false;
    float positionValues[3] = { position.x, position.y, position.z };
    if (ImGui::DragFloat3("Position", positionValues, 0.1f)) {
        position = { positionValues[0], positionValues[1], positionValues[2] };
        changed = true;
    }

    XMFLOAT3 rotationDegrees = {
        XMConvertToDegrees(rotation.x), XMConvertToDegrees(rotation.y), XMConvertToDegrees(rotation.z)
    };
    float rotationValues[3] = { rotationDegrees.x, rotationDegrees.y, rotationDegrees.z };
    if (ImGui::DragFloat3("Rotation", rotationValues, 1.0f)) {
        rotation = { XMConvertToRadians(rotationValues[0]), XMConvertToRadians(rotationValues[1]),
                     XMConvertToRadians(rotationValues[2]) };
        changed = true;
    }

    float scaleValues[3] = { scale.x, scale.y, scale.z };
    if (ImGui::DragFloat3("Scale", scaleValues, 0.01f, 0.01f, 100.0f)) {
        scale = { scaleValues[0], scaleValues[1], scaleValues[2] };
        changed = true;
    }
    if (changed) object->SetEditorTransform(position, rotation, scale);
}

void ImGuiLayer::DrawCameraInspector()
{
    Camera& camera = scene_->GetCamera();
    ImGui::Text("Camera");
    XMFLOAT3 position = camera.GetPosition();
    XMFLOAT3 target = camera.GetTarget();
    XMFLOAT3 up = camera.GetUp();

    float positionValues[3] = { position.x, position.y, position.z };
    if (ImGui::DragFloat3("Position", positionValues, 0.1f))
        camera.SetPosition({ positionValues[0], positionValues[1], positionValues[2] });
    float targetValues[3] = { target.x, target.y, target.z };
    if (ImGui::DragFloat3("Target", targetValues, 0.1f))
        camera.SetTarget({ targetValues[0], targetValues[1], targetValues[2] });
    float upValues[3] = { up.x, up.y, up.z };
    if (ImGui::DragFloat3("Up", upValues, 0.01f, -1.0f, 1.0f))
        camera.SetUp({ upValues[0], upValues[1], upValues[2] });

    float moveSpeed = camera.GetMoveSpeed();
    if (ImGui::SliderFloat("Move speed", &moveSpeed, 0.1f, 100.0f)) camera.SetMoveSpeed(moveSpeed);
    float rotationSpeed = camera.GetRotationSpeed();
    if (ImGui::SliderFloat("Rotation sensitivity", &rotationSpeed, 0.0001f, 0.02f, "%.4f"))
        camera.SetRotationSpeed(rotationSpeed);
}

void ImGuiLayer::DrawLightsInspector()
{
    LightSystem& lights = scene_->GetLightSystem();
    XMFLOAT3& ambient = lights.Ambient();
    float ambientValues[3] = { ambient.x, ambient.y, ambient.z };
    if (ImGui::ColorEdit3("Ambient", ambientValues)) ambient = { ambientValues[0], ambientValues[1], ambientValues[2] };

    DirectionalLight& directional = lights.Directional();
    ImGui::SeparatorText("Directional light");
    DrawFloat3("Direction", directional.direction, 0.01f, -1.0f, 1.0f);
    float directionalColor[3] = { directional.color.x, directional.color.y, directional.color.z };
    if (ImGui::ColorEdit3("Directional color", directionalColor))
        directional.color = { directionalColor[0], directionalColor[1], directionalColor[2] };
    ImGui::SliderFloat("Directional intensity", &directional.intensity, 0.0f, 5.0f);

    ImGui::SeparatorText("Point lights");
    auto& pointLights = lights.PointLights();
    for (int index = 0; index < static_cast<int>(pointLights.size()); ++index) {
        PointLight& point = pointLights[index];
        ImGui::PushID(index);
        if (ImGui::TreeNode("Point light")) {
            DrawFloat3("Position", point.position, 0.1f, -1000.0f, 1000.0f);
            float color[3] = { point.color.x, point.color.y, point.color.z };
            if (ImGui::ColorEdit3("Color", color)) point.color = { color[0], color[1], color[2] };
            ImGui::SliderFloat("Intensity", &point.intensity, 0.0f, 10.0f);
            ImGui::SliderFloat("Range", &point.range, 0.1f, 100.0f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (pointLights.size() < LightSystem::kMaxPointLights && ImGui::Button("Add point light")) {
        PointLight point;
        point.position = { 0.0f, 10.0f, 0.0f };
        lights.AddPointLight(point);
    }
}

void ImGuiLayer::DrawTerrainInspector(Terrain* terrain)
{
    // Mesh-shaping values rebuild ~half a million vertices, so they are applied when you
    // RELEASE the control, not on every frame of a drag. While a control is being edited we keep
    // our own copy; otherwise we follow the terrain so changes made elsewhere show up.
    if (!ImGui::IsAnyItemActive()) pendingTerrainSettings_ = terrain->GetSettings();
    TerrainSettings& s = pendingTerrainSettings_;

    bool commit = false;
    auto commitOnRelease = [&commit] { if (ImGui::IsItemDeactivatedAfterEdit()) commit = true; };

    ImGui::SeparatorText("Shape");
    ImGui::SliderFloat("Width", &s.width, TerrainSettings::kWidthMin, TerrainSettings::kWidthMax, "%.0f");
    commitOnRelease();
    ImGui::SliderFloat("Height scale", &s.heightScale, TerrainSettings::kHeightMin, TerrainSettings::kHeightMax, "%.1f");
    commitOnRelease();
    ImGui::SliderInt("Mesh resolution", &s.maxCells, TerrainSettings::kCellsMin, TerrainSettings::kCellsMax);
    commitOnRelease();
    ImGui::SetItemTooltip("Grid cells across the terrain. Higher = smoother and slower to rebuild.");

    ImGui::SeparatorText("Noise");
    ImGui::SliderFloat("Strength", &s.noiseStrength, 0.0f, 1.0f, "%.3f");
    commitOnRelease();
    ImGui::SliderFloat("Scale", &s.noiseScale, TerrainSettings::kNoiseScaleMin, TerrainSettings::kNoiseScaleMax, "%.2f");
    commitOnRelease();
    ImGui::SliderInt("Octaves", &s.noiseOctaves, TerrainSettings::kOctavesMin, TerrainSettings::kOctavesMax);
    commitOnRelease();
    ImGui::SetItemTooltip("Layers of detail. Each octave doubles the frequency and halves the amplitude.");
    ImGui::InputInt("Seed", &s.noiseSeed);
    commitOnRelease();
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        s.noiseSeed = static_cast<int>(ImGui::GetTime() * 1000.0) & 0xffff;
        commit = true;
    }

    ImGui::SeparatorText("Material");
    // Tiling is just a shader constant, so it applies live while you drag.
    if (ImGui::SliderFloat("Texture tiling", &s.uvTiling, TerrainSettings::kTilingMin,
                           TerrainSettings::kTilingMax, "%.2f", ImGuiSliderFlags_Logarithmic)) {
        terrain->ApplySettings(s);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset terrain")) {
        s = TerrainSettings{};
        commit = true;
    }
    if (commit) lastTerrainGenerationSucceeded_ = terrain->ApplySettings(s);
    if (!lastTerrainGenerationSucceeded_) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Terrain rebuild failed");

    float minY = 0.0f, maxY = 0.0f;
    terrain->GetHeightRange(minY, maxY);
    ImGui::TextDisabled("Surface spans Y %.1f to %.1f", minY, maxY);
}

void ImGuiLayer::DrawWaterInspector(Water* water)
{
    ImGui::SeparatorText("Water material");
    if (!waterSettingsInitialized_) {
        pendingWaterSettings_ = water->GetSettings();
        waterSettingsInitialized_ = true;
    }
    ImGui::SliderFloat("Height", &pendingWaterSettings_.height, -100.0f, 100.0f);
    ImGui::SliderFloat("Size", &pendingWaterSettings_.size, 1.0f, 1000.0f);
    float color[3] = { pendingWaterSettings_.color.x, pendingWaterSettings_.color.y, pendingWaterSettings_.color.z };
    if (ImGui::ColorEdit3("Color", color)) pendingWaterSettings_.color = { color[0], color[1], color[2] };
    ImGui::SliderFloat("Opacity", &pendingWaterSettings_.alpha, 0.05f, 1.0f);
    ImGui::SliderFloat("Wave amplitude", &pendingWaterSettings_.waveAmplitude, 0.0f, 2.0f);
    ImGui::SliderFloat("Wave speed", &pendingWaterSettings_.waveSpeed, 0.0f, 5.0f);
    ImGui::SliderFloat("Wave frequency", &pendingWaterSettings_.waveFrequency, 0.05f, 2.0f);
    ImGui::SliderFloat("Sky reflection", &pendingWaterSettings_.reflectivity, 0.0f, 1.0f);
    if (ImGui::Button("Apply water settings")) water->SetSettings(pendingWaterSettings_);
}

void ImGuiLayer::DrawSkyboxInspector(Skybox* skybox)
{
    ImGui::SeparatorText("Skybox material");
    float intensity = skybox->GetIntensity();
    if (ImGui::SliderFloat("Intensity", &intensity, 0.0f, 3.0f)) skybox->SetIntensity(intensity);
    ImGui::Text("Cubemap: assets/Skybox");
}
