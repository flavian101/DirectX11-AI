#include "ImGuiLayer.h"
#include "imgui.h"
#include <DirectXMath.h>
#include <algorithm>

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
	if (!scene_) return;
	DrawMainMenuBar();
	if (showSceneWindow_) DrawSceneWindow();
	if (showLightsWindow_) DrawLightsWindow();
	if (showTerrainWindow_) DrawTerrainWindow();
	if (showCameraWindow_) DrawCameraWindow();
}

void ImGuiLayer::DrawMainMenuBar()
{
	if (!ImGui::BeginMainMenuBar()) return;

	if (ImGui::BeginMenu("Collections")) {
		ImGui::MenuItem("Scene", nullptr, &showSceneWindow_);
		ImGui::MenuItem("Lights", nullptr, &showLightsWindow_);
		ImGui::MenuItem("Terrain", nullptr, &showTerrainWindow_);
		ImGui::MenuItem("Camera", nullptr, &showCameraWindow_);
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void ImGuiLayer::DrawSceneWindow()
{
	if (!ImGui::Begin("Scene")) {
		ImGui::End();
		return;
	}

	const auto& objects = scene_->GetObjects();
	if (selectedObject_ >= static_cast<int>(objects.size())) selectedObject_ = -1;
	for (int index = 0; index < static_cast<int>(objects.size()); ++index) {
		SceneObject* object = objects[index];
		if (!object) continue;
		if (ImGui::Selectable(object->GetEditorName(), selectedObject_ == index)) {
			selectedObject_ = index;
		}
	}

	if (selectedObject_ >= 0 && selectedObject_ < static_cast<int>(objects.size())) {
		ImGui::Separator();
		DrawTransformEditor(objects[selectedObject_]);
	}
	ImGui::End();
}

void ImGuiLayer::DrawCameraWindow()
{
	if (!ImGui::Begin("Camera", &showCameraWindow_)) {
		ImGui::End();
		return;
	}

	Camera& camera = scene_->GetCamera();
	XMFLOAT3 position = camera.GetPosition();
	XMFLOAT3 target = camera.GetTarget();
	XMFLOAT3 up = camera.GetUp();

	float positionValues[3] = { position.x, position.y, position.z };
	if (ImGui::DragFloat3("Position", positionValues, 0.1f)) {
		camera.SetPosition({ positionValues[0], positionValues[1], positionValues[2] });
	}

	float targetValues[3] = { target.x, target.y, target.z };
	if (ImGui::DragFloat3("Target", targetValues, 0.1f)) {
		camera.SetTarget({ targetValues[0], targetValues[1], targetValues[2] });
	}

	float upValues[3] = { up.x, up.y, up.z };
	if (ImGui::DragFloat3("Up", upValues, 0.01f, -1.0f, 1.0f)) {
		camera.SetUp({ upValues[0], upValues[1], upValues[2] });
	}

	float moveSpeed = camera.GetMoveSpeed();
	if (ImGui::SliderFloat("Move speed", &moveSpeed, 0.1f, 100.0f)) {
		camera.SetMoveSpeed(moveSpeed);
	}

	float rotationSpeed = camera.GetRotationSpeed();
	if (ImGui::SliderFloat("Rotation sensitivity", &rotationSpeed, 0.0001f, 0.02f, "%.4f")) {
		camera.SetRotationSpeed(rotationSpeed);
	}
	ImGui::End();
}

void ImGuiLayer::DrawTransformEditor(SceneObject* object)
{
	if (!object) return;

	XMFLOAT3 position;
	XMFLOAT3 rotation;
	XMFLOAT3 scale;
	if (!object->GetEditorTransform(position, rotation, scale)) {
		ImGui::Text("Transform editing is not available for this object.");
		return;
	}

	ImGui::Text("Transform");
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
		rotation = {
			XMConvertToRadians(rotationValues[0]), XMConvertToRadians(rotationValues[1]),
			XMConvertToRadians(rotationValues[2])
		};
		changed = true;
	}

	float scaleValues[3] = { scale.x, scale.y, scale.z };
	if (ImGui::DragFloat3("Scale", scaleValues, 0.01f, 0.01f, 100.0f)) {
		scale = { scaleValues[0], scaleValues[1], scaleValues[2] };
		changed = true;
	}

	if (changed) object->SetEditorTransform(position, rotation, scale);
}

void ImGuiLayer::DrawLightsWindow()
{
	if (!ImGui::Begin("Lights")) {
		ImGui::End();
		return;
	}

	LightSystem& lights = scene_->GetLightSystem();
	XMFLOAT3& ambient = lights.Ambient();
	float ambientValues[3] = { ambient.x, ambient.y, ambient.z };
	if (ImGui::ColorEdit3("Ambient", ambientValues)) {
		ambient = { ambientValues[0], ambientValues[1], ambientValues[2] };
	}

	DirectionalLight& directional = lights.Directional();
	ImGui::SeparatorText("Directional light");
	DrawFloat3("Direction", directional.direction, 0.01f, -1.0f, 1.0f);
	float directionalColor[3] = {
		directional.color.x, directional.color.y, directional.color.z
	};
	if (ImGui::ColorEdit3("Directional color", directionalColor)) {
		directional.color = { directionalColor[0], directionalColor[1], directionalColor[2] };
	}
	ImGui::SliderFloat("Directional intensity", &directional.intensity, 0.0f, 5.0f);

	ImGui::SeparatorText("Point lights");
	auto& pointLights = lights.PointLights();
	for (int index = 0; index < static_cast<int>(pointLights.size()); ++index) {
		PointLight& point = pointLights[index];
		ImGui::PushID(index);
		if (ImGui::TreeNode("Point light")) {
			DrawFloat3("Position", point.position, 0.1f, -1000.0f, 1000.0f);
			float color[3] = { point.color.x, point.color.y, point.color.z };
			if (ImGui::ColorEdit3("Color", color)) {
				point.color = { color[0], color[1], color[2] };
			}
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
	ImGui::End();
}

void ImGuiLayer::DrawTerrainWindow()
{
	Terrain* terrain = scene_->GetTerrain();
	if (!terrain || !ImGui::Begin("Terrain Generation")) {
		if (terrain) ImGui::End();
		return;
	}

	if (!terrainSettingsInitialized_) {
		pendingTerrainSettings_ = terrain->GetSettings();
		terrainSettingsInitialized_ = true;
	}

	ImGui::SliderFloat("Height scale", &pendingTerrainSettings_.heightScale, 1.0f, 100.0f, "%.1f");
	ImGui::SliderFloat("Noise strength", &pendingTerrainSettings_.noiseStrength, 0.0f, 1.0f, "%.3f");
	ImGui::SliderFloat("Noise scale", &pendingTerrainSettings_.noiseScale, 0.5f, 16.0f, "%.2f");
	ImGui::InputInt("Noise seed", &pendingTerrainSettings_.noiseSeed);
	if (ImGui::Button("Generate terrain")) {
		lastTerrainGenerationSucceeded_ = scene_->RegenerateTerrain(pendingTerrainSettings_);
	}
	if (!lastTerrainGenerationSucceeded_) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Terrain regeneration failed");
	}
	ImGui::End();
}
