// The scatter inspector: species, their looks (variants) and each look's material overrides. It lived inside EditorLayer.cpp
// (4800 lines) until roadmap S11 moved it here; it is still EditorLayer::DrawScatterInspector, so nothing else changes.
#include "EditorLayer.h"
#include "FileDialogs.h"
#include "ProceduralModels.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

namespace {
// A material override with nothing replaced is no override at all: it is not saved.
bool IsPlainOverride(const ScatterMaterialOverride& o)
{
    return o.albedo.empty() && o.normal.empty() && o.orm.empty() && o.emissive.empty() && o.roughness < 0.0f &&
           o.baseColor[0] == 1.0f && o.baseColor[1] == 1.0f && o.baseColor[2] == 1.0f && o.baseColor[3] == 1.0f;
}

// One image slot of a material override: the file's name, a "..." to choose one and an "x" to go back to the model's own.
bool DrawImageSlot(HWND owner, const char* label, std::string& path)
{
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine(88.0f);
    const std::string shown = path.empty() ? std::string("(model's own)") : std::filesystem::path(path).filename().string();
    ImGui::TextDisabled("%s", shown.c_str());
    ImGui::SetItemTooltip("%s", path.empty() ? "The map that comes with the model is used." : path.c_str());
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 34.0f);
    if (ImGui::SmallButton("...")) {
        const std::string picked = PickImageFile(owner);
        if (!picked.empty()) { path = picked; changed = true; }
    }
    if (!path.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) { path.clear(); changed = true; }
    }
    ImGui::PopID();
    return changed;
}

// The looks (variants) of one species: add, remove, weights, where each one grows, and per-material texture replacements.
// Returns true if anything changed. `scatter` is only read, for what the loaded models are (their material names, counts).
bool DrawVariantEditor(HWND owner, const ScatterObject* scatter, size_t speciesIndex, ScatterSpeciesSettings& sp)
{
    bool changed = false;
    ImGui::TextDisabled("%zu placed in all, up to %d levels of detail", scatter->GetInstanceCount(speciesIndex), scatter->GetSpeciesLodLevels(speciesIndex));
    float totalWeight = 0.0f;
    for (const ScatterVariantSettings& v : sp.variants) if (v.enabled) totalWeight += v.weight;
    int removeIndex = -1;
    for (size_t vi = 0; vi < sp.variants.size(); ++vi) {
        ScatterVariantSettings& v = sp.variants[vi];
        ImGui::PushID(static_cast<int>(vi));
        const float share = v.enabled && totalWeight > 0.0f ? 100.0f * v.weight / totalWeight : 0.0f;
        const bool open = ImGui::TreeNodeEx("##variant", ImGuiTreeNodeFlags_AllowOverlap, "%s  (%.0f%%)", v.name.c_str(), share);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.0f);
        if (sp.variants.size() > 1 && ImGui::SmallButton("Remove##variant")) removeIndex = static_cast<int>(vi);   // a species keeps at least one look
        if (open) {
            char nameBuffer[64];
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", v.name.c_str());
            if (ImGui::InputText("Name##variant", nameBuffer, sizeof(nameBuffer))) { v.name = nameBuffer; changed = true; }
            changed |= ImGui::Checkbox("Enabled##variant", &v.enabled);

            ImGui::TextDisabled("%s", v.model.c_str());
            auto builtin = [&](const char* label, const char* id) {
                if (ImGui::SmallButton(label)) { v.model = id; changed = true; }
                ImGui::SameLine();
            };
            builtin("Pine##v", kBuiltinPine); builtin("Rock##v", kBuiltinRock); builtin("Flat##v", kBuiltinRockFlat);
            builtin("Tall##v", kBuiltinRockTall); builtin("Mossy##v", kBuiltinRockMossy);
            if (ImGui::SmallButton("Model file...##variant")) {
                const std::string path = PickModelFile(owner);
                if (!path.empty()) { v.model = path; v.materials.clear(); changed = true; }   // other model, other material slots
            }
            if (!scatter->IsVariantReady(speciesIndex, vi)) {
                if (v.enabled) {
                    const std::string& error = scatter->GetVariantError(speciesIndex, vi);
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", error.empty() ? "Loading..." : error.c_str());
                }
            } else {
                ImGui::TextDisabled("%zu placed", scatter->GetVariantInstanceCount(speciesIndex, vi));
            }

            changed |= ImGui::SliderFloat("Weight", &v.weight, 0.0f, 10.0f, "%.2f");
            ImGui::SetItemTooltip("How often this look is chosen, relative to the others that the ground allows.\n0 = never.");
            ImGui::TextDisabled("Likes the terrain layer (x weight):");
            for (int k = 0; k < kSplatChannels; ++k) changed |= ImGui::SliderFloat(kTerrainLayerNames[k], &v.layerBias[k], 0.0f, 4.0f, "%.2f");
            ImGui::SetItemTooltip("A mossy rock: high on grass, near 0 on rock and snow.\nThe species' own layer table decides IF something grows; this decides WHICH look it gets.");
            changed |= ImGui::DragFloatRange2("Slope", &v.minSlope, &v.maxSlope, 0.5f, 0.0f, 90.0f, "%.0f deg", "%.0f deg");
            ImGui::SetItemTooltip("This look only grows on ground in this band. A cliff fragment: 50 to 90.");
            changed |= ImGui::SliderFloat("Size x", &v.scaleMul, 0.1f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Multiplies the species' scale range. A large granite rock: 2.");

            const size_t materialCount = scatter->GetVariantMaterialCount(speciesIndex, vi);
            if (materialCount > 0 && ImGui::TreeNode("Textures##variant")) {
                ImGui::TextDisabled("Replace the maps of a material. Empty = the model's own.\nORM = occlusion (R), roughness (G), metallic (B) in one image.");
                for (size_t slot = 0; slot < materialCount; ++slot) {
                    ImGui::PushID(static_cast<int>(slot));
                    ScatterMaterialOverride* existing = nullptr;
                    for (ScatterMaterialOverride& o : v.materials) if (o.slot == static_cast<int>(slot)) existing = &o;
                    ScatterMaterialOverride edit;
                    edit.slot = static_cast<int>(slot);
                    if (existing) edit = *existing;
                    const std::string title = scatter->GetVariantMaterialName(speciesIndex, vi, slot);
                    bool slotChanged = false;
                    if (ImGui::TreeNodeEx("##material", ImGuiTreeNodeFlags_DefaultOpen, "%s", title.c_str())) {
                        slotChanged |= DrawImageSlot(owner, "Diffuse", edit.albedo);
                        slotChanged |= DrawImageSlot(owner, "Normal", edit.normal);
                        slotChanged |= DrawImageSlot(owner, "ORM", edit.orm);
                        slotChanged |= DrawImageSlot(owner, "Emissive", edit.emissive);
                        if (!edit.normal.empty()) slotChanged |= ImGui::Checkbox("Flip green (OpenGL normal map)", &edit.flipGreen);
                        slotChanged |= ImGui::ColorEdit3("Tint", edit.baseColor.data(), ImGuiColorEditFlags_Float);
                        ImGui::SetItemTooltip("Multiplies the albedo (linear). Grey it down, or push it towards green for moss.");
                        bool overrideRoughness = edit.roughness >= 0.0f;
                        if (ImGui::Checkbox("Set roughness", &overrideRoughness)) { edit.roughness = overrideRoughness ? 0.7f : -1.0f; slotChanged = true; }
                        if (overrideRoughness) {
                            slotChanged |= ImGui::SliderFloat("Roughness", &edit.roughness, 0.0f, 1.0f, "%.2f");
                            ImGui::SetItemTooltip("0 = mirror, 1 = chalk. With an ORM map this scales the map's roughness instead.");
                        }
                        ImGui::TreePop();
                    }
                    if (slotChanged) {
                        changed = true;
                        if (existing) {
                            if (IsPlainOverride(edit)) v.materials.erase(v.materials.begin() + (existing - v.materials.data()));
                            else *existing = edit;
                        } else if (!IsPlainOverride(edit)) {
                            v.materials.push_back(edit);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) {
        sp.variants.erase(sp.variants.begin() + removeIndex);
        changed = true;
    }

    if (sp.variants.size() < static_cast<size_t>(kMaxScatterVariants)) {
        if (ImGui::Button("Add look...##variants")) ImGui::OpenPopup("AddVariant");
        if (ImGui::BeginPopup("AddVariant")) {
            auto add = [&](const char* name, const std::string& model) {
                ScatterVariantSettings fresh;
                fresh.name = name; fresh.model = model;
                sp.variants.push_back(fresh);
                changed = true;
            };
            if (ImGui::MenuItem("Pine (built-in)")) add("Pine", kBuiltinPine);
            if (ImGui::MenuItem("Boulder (built-in)")) add("Boulder", kBuiltinRock);
            if (ImGui::MenuItem("Flat rock (built-in)")) add("Slab", kBuiltinRockFlat);
            if (ImGui::MenuItem("Tall rock (built-in)")) add("Block", kBuiltinRockTall);
            if (ImGui::MenuItem("Mossy rock (built-in)")) add("Mossy boulder", kBuiltinRockMossy);
            if (ImGui::MenuItem("From a model file...")) {
                const std::string path = PickModelFile(owner);
                if (!path.empty()) add(std::filesystem::path(path).stem().string().c_str(), path);
            }
            if (!sp.variants.empty() && ImGui::MenuItem("Copy of the last look")) {
                ScatterVariantSettings copy = sp.variants.back();
                copy.name += " copy";
                sp.variants.push_back(copy);   // same model and textures: change the textures to make a different stone
                changed = true;
            }
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled("A species has at most %d looks. Add another species for more.", kMaxScatterVariants);
    }
    return changed;
}
}  // namespace

void EditorLayer::DrawScatterInspector(ScatterObject* scatter)
{
    // Like water, every value here applies live: read the settings, edit, write back if anything moved. What actually has to
    // be done (reload a model, place the cells again, nothing) is worked out by the scatter.
    ScatterSettings s = scatter->GetSettings();
    bool changed = false;
    ImGui::SeparatorText("Scatter");
    if (!scatter->HasGround()) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "There is no terrain to scatter on.");
    changed |= ImGui::Checkbox("Enabled##scatter", &s.enabled);
    if (scatter->GetPendingCells() > 0) ImGui::TextDisabled("Placing: %d cells left (nearest first)", scatter->GetPendingCells());

    if (ImGui::CollapsingHeader("Placement")) {
        changed |= ImGui::InputInt("Seed", &s.seed);
        ImGui::SetItemTooltip("A different seed is a different forest.\nThe same seed always gives the same forest, on any machine.");
        changed |= ImGui::SliderFloat("Cell size (m)", &s.cellSize, ScatterSettings::kCellMin, ScatterSettings::kCellMax, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("The terrain is cut into square cells. A cell is culled, simplified and regenerated as one block.\nSmall: finer culling and quicker edits, but more draw calls.\nLarge: fewer draw calls, coarser culling.");
        changed |= ImGui::SliderFloat("Placement budget (ms/frame)", &s.generationBudgetMs, 0.5f, 30.0f, "%.1f");
        ImGui::SetItemTooltip("How long each frame may spend placing cells after an edit. Higher: the forest appears sooner, the editor stutters more.");
        changed |= ImGui::SliderInt("Max instances per species", &s.maxInstancesPerSpecies, 1000, 5000000, "%d", ImGuiSliderFlags_Logarithmic);
    }
    if (ImGui::CollapsingHeader("Culling and detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        changed |= ImGui::Checkbox("Frustum culling", &s.frustumCulling);
        ImGui::SetItemTooltip("Cells outside the view are skipped. Turn it off to see how much that saves.");
        changed |= ImGui::Checkbox("Level of detail", &s.lodEnabled);
        changed |= ImGui::SliderFloat("Detail bias", &s.lodBias, ModelRenderSettings::kBiasMin, ModelRenderSettings::kBiasMax, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetItemTooltip("Above 1: stay detailed for longer. Below 1: simplify sooner and hide small things earlier.");
        changed |= ImGui::Checkbox("Cast shadows", &s.shadows);
        ImGui::SetItemTooltip("The master switch. Each species also has its own, and a distance beyond which it casts none.");
        changed |= ImGui::SliderFloat("Reflection detail", &s.reflectionDetail, 0.1f, 1.0f, "%.2f");
        ImGui::SetItemTooltip("The water's mirror draws the scene a second time. At 0.5 it sees scatter at half the distance.");
    }
    if (ImGui::CollapsingHeader("Statistics")) {
        const ScatterViewStats& main = scatter->GetViewStats(ViewKind::Main);
        const ScatterViewStats& sun = scatter->GetViewStats(ViewKind::Shadow);
        ImGui::Text("Camera: %d / %d cells, %d / %d instances", main.cellsDrawn, main.cellsTotal, main.instancesDrawn, main.instancesTotal);
        ImGui::Text("%d draw calls, %.2f M triangles", main.drawCalls, main.trianglesDrawn / 1.0e6f);
        ImGui::TextDisabled("Instances per level: %d / %d / %d / %d", main.instancesPerLod[0], main.instancesPerLod[1], main.instancesPerLod[2], main.instancesPerLod[3]);
        ImGui::Text("Sun: %d instances, %d draw calls, %.2f M triangles", sun.instancesDrawn, sun.drawCalls, sun.trianglesDrawn / 1.0e6f);
    }

    ImGui::SeparatorText("Species");
    int removeIndex = -1;
    for (size_t i = 0; i < s.species.size(); ++i) {
        ScatterSpeciesSettings& sp = s.species[i];
        ImGui::PushID(static_cast<int>(i));
        const bool open = ImGui::TreeNodeEx("##species", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap, "%s", sp.name.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 40.0f);
        if (ImGui::SmallButton("Remove##species")) removeIndex = static_cast<int>(i);
        if (open) {
            char nameBuffer[64];
            std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", sp.name.c_str());
            if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) { sp.name = nameBuffer; changed = true; }
            changed |= ImGui::Checkbox("Enabled##species", &sp.enabled);

            // The looks: one model (as before), or several variants (lesson 36).
            changed |= DrawVariantEditor(hwnd_, scatter, i, sp);

            ImGui::SeparatorText("Where");
            changed |= ImGui::SliderFloat("Spacing (m)", &sp.spacing, 0.5f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("The ground is cut into squares this wide, each with at most one plant. The most you can get is 1 / spacing^2 per m^2.");
            changed |= ImGui::SliderFloat("Min distance (m)", &sp.minDistance, 0.0f, sp.spacing, "%.1f");
            ImGui::SetItemTooltip("Two plants are never closer than this. Set it to roughly the width of a full-grown one.");
            changed |= ImGui::SliderFloat("Density", &sp.density, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("The chance that a square on PERFECT ground gets a plant.\nAbout %.1f per 1000 m^2 at most.", 1000.0f * sp.density / (sp.spacing * sp.spacing));
            ImGui::TextDisabled("Likes the terrain layer:");
            for (int k = 0; k < kSplatChannels; ++k) changed |= ImGui::SliderFloat(kTerrainLayerNames[k], &sp.layerDensity[k], 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("1 = grows freely where this layer is, 0 = never. The layers are the baked rules plus what you painted:\npaint grass and trees come, paint rock and they leave.");
            changed |= ImGui::SliderFloat("Min slope", &sp.minSlope, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::SliderFloat("Max slope", &sp.maxSlope, 0.0f, 90.0f, "%.0f deg");
            changed |= ImGui::DragFloatRange2("Height (m)", &sp.minHeight, &sp.maxHeight, 1.0f, -1000.0f, 5000.0f, "%.0f", "%.0f");
            changed |= ImGui::SliderFloat("Clumping", &sp.clusterStrength, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0 = evenly spread. 1 = groves with clearings between them.");
            if (sp.clusterStrength > 0.0f) changed |= ImGui::SliderFloat("Clump size (m)", &sp.clusterScale, 5.0f, 300.0f, "%.0f", ImGuiSliderFlags_Logarithmic);

            ImGui::SeparatorText("Each one");
            changed |= ImGui::DragFloatRange2("Scale", &sp.scaleMin, &sp.scaleMax, 0.01f, 0.05f, 10.0f, "%.2f", "%.2f");
            changed |= ImGui::SliderFloat("Follow ground", &sp.alignToNormal, 0.0f, 1.0f, "%.2f");
            ImGui::SetItemTooltip("0 = stands straight up (trees). 1 = leans with the slope (rocks).");
            changed |= ImGui::SliderFloat("Random lean", &sp.tilt, 0.0f, 30.0f, "%.1f deg");
            changed |= ImGui::SliderFloat("Sink", &sp.sink, 0.0f, 0.8f, "%.2f");
            ImGui::SetItemTooltip("The fraction of its height buried in the ground. Rocks look planted, not placed, with a little sink.");
            changed |= ImGui::InputInt("Seed##species", &sp.seed);

            ImGui::SeparatorText("Drawing");
            changed |= ImGui::SliderFloat("Level 1 below", &sp.lodScreen[0], 0.003f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetItemTooltip("Fractions of the screen's height that one plant covers. Level 1 starts below the first,\nlevel 2 below the second, level 3 below the third. 0.1 is about 100 pixels on a 1080p screen.");
            changed |= ImGui::SliderFloat("Level 2 below", &sp.lodScreen[1], 0.002f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Level 3 below", &sp.lodScreen[2], 0.001f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            changed |= ImGui::SliderFloat("Hide below", &sp.cullScreen, 0.0f, 0.05f, "%.4f");
            ImGui::SetItemTooltip("Plants covering less than this fraction of the screen are not drawn. 0.004 is about 4 pixels.");
            changed |= ImGui::Checkbox("Casts shadows", &sp.castShadows);
            if (sp.castShadows) changed |= ImGui::SliderFloat("Shadow distance (m)", &sp.shadowDistance, 10.0f, 500.0f, "%.0f");
            ImGui::SetItemTooltip("Beyond this distance from the camera the species casts no shadow. The sun's shadow map covers the whole terrain,\nso far shadows would be a few blurry texels anyway.");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIndex >= 0) { s.species.erase(s.species.begin() + removeIndex); changed = true; }

    if (ImGui::Button("Add species...")) ImGui::OpenPopup("AddSpecies");
    if (ImGui::BeginPopup("AddSpecies")) {
        auto add = [&](const char* name, const std::string& model, float spacing, float maxSlope) {
            ScatterSpeciesSettings fresh;
            fresh.name = name; fresh.variants[0].name = name; fresh.variants[0].model = model;
            fresh.spacing = spacing; fresh.minDistance = spacing * 0.6f; fresh.maxSlope = maxSlope;
            fresh.seed = static_cast<int>(s.species.size()) + 1;
            s.species.push_back(fresh);
            changed = true;
        };
        if (ImGui::MenuItem("Pine (built-in)")) add("Pine", kBuiltinPine, 7.0f, 32.0f);
        if (ImGui::MenuItem("Rock (built-in)")) add("Rock", kBuiltinRock, 9.0f, 60.0f);
        if (ImGui::MenuItem("From a model file...")) {
            const std::string path = PickModelFile(hwnd_);
            if (!path.empty()) add(std::filesystem::path(path).stem().string().c_str(), path, 8.0f, 35.0f);
        }
        ImGui::EndPopup();
    }
    if (changed) scatter->SetSettings(s);
}
