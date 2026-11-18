///----------------------------------------------------------------------------------------------------
/// Name         :  entry.cpp
/// Description  :  Frametime Overlay addon for Nexus: FPS, frame time graph, 1% low and a
///                 CPU / GPU / Limit bottleneck indicator. 
///----------------------------------------------------------------------------------------------------

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include "nexus/Nexus.h"
#include "imgui/imgui.h"
#include "FrameStats.h"
#include "Sampler.h"

/* Prototypes */
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void RenderOverlay();
void RenderOptions();
void OnToggleKeybind(const char* aIdentifier, bool aIsRelease);

/* Globals */
static AddonDefinition_t AddonDef = {};
static HMODULE hSelf = nullptr;
static AddonAPI_t* APIDefs = nullptr;
static NexusLinkData_t* NexusLink = nullptr;
static FrameStats Stats;

static const char* ADDON_NAME = "Frametime Overlay";
static const char* KB_TOGGLE = "KB_FRAMETIME_OVERLAY_TOGGLE";

/* Settings */
struct Settings
{
	bool ShowOverlay = true;
	bool Locked = false;
	bool ShowGraph = true;
	bool ShowDetails = true;
	bool OnlyInGameplay = true;
	float BgAlpha = 0.45f;
	float GpuThreshold = 90.0f;
	float CpuThreshold = 85.0f;
	float PosX = 20.0f;
	float PosY = 200.0f;
};
static Settings g_Cfg;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH) hSelf = hModule;
	return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
	AddonDef.Signature = (uint32_t)-48219;
	AddonDef.APIVersion = NEXUS_API_VERSION;
	AddonDef.Name = ADDON_NAME;
	AddonDef.Version.Major = 2026;
	AddonDef.Version.Minor = 11;
	AddonDef.Version.Build = 18;
	AddonDef.Version.Revision = 0;
	AddonDef.Author = "Nekonekoo";
	AddonDef.Description = "Shows FPS, frame times, 1% low and whether you're CPU or GPU bound.";
	AddonDef.Load = AddonLoad;
	AddonDef.Unload = AddonUnload;
	AddonDef.Flags = AF_None;
	AddonDef.Provider = UP_GitHub;
	AddonDef.UpdateLink = "https://github.com/PatrickSantoZZ/GW2FrametimeOverlay";
	return &AddonDef;
}

///----------------------------------------------------------------------------------------------------
/// Settings
///----------------------------------------------------------------------------------------------------
static std::filesystem::path SettingsPath()
{
	const char* dir = APIDefs ? APIDefs->Paths_GetAddonDirectory("FrametimeOverlay") : nullptr;
	return dir ? std::filesystem::path(dir) / "settings.json" : std::filesystem::path();
}

static std::string JsonValue(const std::string& json, const char* key)
{
	const std::string needle = std::string("\"") + key + "\"";
	size_t pos = json.find(needle);
	if (pos == std::string::npos) return {};
	pos = json.find(':', pos + needle.size());
	if (pos == std::string::npos) return {};
	pos = json.find_first_not_of(" \t\r\n", pos + 1);
	if (pos == std::string::npos) return {};
	const size_t end = json.find_first_of(",}\r\n", pos);
	std::string val = json.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
	while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
	return val;
}

static void ReadBool(const std::string& json, const char* key, bool& out)
{
	const std::string v = JsonValue(json, key);
	if (!v.empty()) out = v == "true";
}

static void ReadFloat(const std::string& json, const char* key, float& out, float lo, float hi)
{
	const std::string v = JsonValue(json, key);
	if (v.empty()) return;
	try { out = std::stof(v); } catch (...) { return; }
	if (out < lo) out = lo;
	if (out > hi) out = hi;
}

static void LoadSettings()
{
	const auto path = SettingsPath();
	if (path.empty()) return;
	std::ifstream file(path);
	if (!file) return;
	const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	ReadBool(json, "showOverlay", g_Cfg.ShowOverlay);
	ReadBool(json, "locked", g_Cfg.Locked);
	ReadBool(json, "showGraph", g_Cfg.ShowGraph);
	ReadBool(json, "showDetails", g_Cfg.ShowDetails);
	ReadBool(json, "onlyInGameplay", g_Cfg.OnlyInGameplay);
	ReadFloat(json, "bgAlpha", g_Cfg.BgAlpha, 0.0f, 1.0f);
	ReadFloat(json, "gpuThreshold", g_Cfg.GpuThreshold, 50.0f, 100.0f);
	ReadFloat(json, "cpuThreshold", g_Cfg.CpuThreshold, 50.0f, 100.0f);
	ReadFloat(json, "posX", g_Cfg.PosX, -10000.0f, 10000.0f);
	ReadFloat(json, "posY", g_Cfg.PosY, -10000.0f, 10000.0f);
}

static void SaveSettings()
{
	const auto path = SettingsPath();
	if (path.empty()) return;
	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);

	auto b = [](bool v) { return v ? "true" : "false"; };
	std::ofstream file(path, std::ios::trunc);
	file << "{\n"
	     << "  \"showOverlay\": " << b(g_Cfg.ShowOverlay) << ",\n"
	     << "  \"locked\": " << b(g_Cfg.Locked) << ",\n"
	     << "  \"showGraph\": " << b(g_Cfg.ShowGraph) << ",\n"
	     << "  \"showDetails\": " << b(g_Cfg.ShowDetails) << ",\n"
	     << "  \"onlyInGameplay\": " << b(g_Cfg.OnlyInGameplay) << ",\n"
	     << "  \"bgAlpha\": " << g_Cfg.BgAlpha << ",\n"
	     << "  \"gpuThreshold\": " << g_Cfg.GpuThreshold << ",\n"
	     << "  \"cpuThreshold\": " << g_Cfg.CpuThreshold << ",\n"
	     << "  \"posX\": " << g_Cfg.PosX << ",\n"
	     << "  \"posY\": " << g_Cfg.PosY << "\n"
	     << "}\n";
}

///----------------------------------------------------------------------------------------------------
/// Bottleneck badge
///----------------------------------------------------------------------------------------------------
struct BadgeStyle { const char* Label; ImU32 Color; const char* Tooltip; };

static BadgeStyle StyleFor(EBottleneck b)
{
	switch (b)
	{
	case EBottleneck::CPU: return { "CPU", IM_COL32(70, 140, 230, 255), "CPU bound: a game thread is maxed out.\nHelps: lower character model limit/quality, reflections, shadows.\nResolution / render sampling won't help much." };
	case EBottleneck::GPU: return { "GPU", IM_COL32(220, 70, 70, 255), "GPU bound: the graphics card is maxed out.\nLower: resolution / render sampling, shadows, post-processing, reflections." };
	case EBottleneck::Limit: return { "LIM", IM_COL32(130, 130, 130, 255), "Limited: neither CPU nor GPU is maxed.\nUsually the FPS cap, V-Sync or the monitor refresh rate." };
	default: return { " ? ", IM_COL32(90, 90, 90, 255), "Not enough data yet, or GPU counters are unavailable on this system." };
	}
}

static void DrawBadge(EBottleneck b)
{
	const BadgeStyle s = StyleFor(b);
	const ImVec2 pad(6.0f, 3.0f);
	const ImVec2 text = ImGui::CalcTextSize("GPU"); 
	const ImVec2 size(text.x + pad.x * 2.0f, text.y + pad.y * 2.0f);
	const ImVec2 p = ImGui::GetCursorScreenPos();

	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), s.Color, 4.0f);
	const ImVec2 lbl = ImGui::CalcTextSize(s.Label);
	dl->AddText(ImVec2(p.x + (size.x - lbl.x) * 0.5f, p.y + pad.y), IM_COL32_WHITE, s.Label);

	ImGui::Dummy(size);
	if (!g_Cfg.Locked && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.Tooltip);
}

///----------------------------------------------------------------------------------------------------
/// Render
///----------------------------------------------------------------------------------------------------
void RenderOverlay()
{
	Stats.Tick(); 

	// Background sampling (GPU counters, thread times) - make sure it runs ONL when overlay is visible
	Sampler::SetPaused(!g_Cfg.ShowOverlay);
	if (!g_Cfg.ShowOverlay) return;
	if (g_Cfg.OnlyInGameplay && NexusLink && !NexusLink->IsGameplay) return;

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
	if (g_Cfg.Locked) flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowPos(ImVec2(g_Cfg.PosX, g_Cfg.PosY), ImGuiCond_Once);
	ImGui::SetNextWindowBgAlpha(g_Cfg.BgAlpha);

	if (ImGui::Begin("##FrametimeOverlay", nullptr, flags))
	{
		DrawBadge(Sampler::Bottleneck());
		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::Text("%4.0f FPS  %5.2f ms", Stats.Fps(), Stats.FrameTimeMs());
		ImGui::TextDisabled("1%% low %4.0f", Stats.OnePercentLow());
		ImGui::EndGroup();

		if (g_Cfg.ShowGraph)
		{
			ImGui::PlotLines("##ft", Stats.GraphData(), FrameStats::GRAPH_SIZE, Stats.GraphOffset(),
				nullptr, 0.0f, Stats.GraphMax(), ImVec2(200.0f, 40.0f));
		}

		if (g_Cfg.ShowDetails)
		{
			const float gpu = Sampler::GpuUtil();
			if (gpu >= 0.0f) ImGui::TextDisabled("GPU %3.0f%%  CPU-Thread %3.0f%%", gpu, Sampler::MaxThreadUtil());
			else ImGui::TextDisabled("GPU  n/a  CPU-Thread %3.0f%%", Sampler::MaxThreadUtil());
		}

		// Remember position after the user dragged it around >:c 
		const ImVec2 pos = ImGui::GetWindowPos();
		if (!g_Cfg.Locked && (pos.x != g_Cfg.PosX || pos.y != g_Cfg.PosY) && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			g_Cfg.PosX = pos.x;
			g_Cfg.PosY = pos.y;
			SaveSettings();
		}
	}
	ImGui::End();
}

void RenderOptions()
{
	bool changed = false;
	changed |= ImGui::Checkbox("Show overlay", &g_Cfg.ShowOverlay);
	ImGui::SameLine(); ImGui::TextDisabled("(toggle: ALT+SHIFT+F, rebindable in Nexus keybinds)");
	changed |= ImGui::Checkbox("Lock position (click-through)", &g_Cfg.Locked);
	changed |= ImGui::Checkbox("Only show in gameplay", &g_Cfg.OnlyInGameplay);
	changed |= ImGui::Checkbox("Show frame time graph", &g_Cfg.ShowGraph);
	changed |= ImGui::Checkbox("Show GPU / CPU-thread usage", &g_Cfg.ShowDetails);

	ImGui::SliderFloat("Background opacity", &g_Cfg.BgAlpha, 0.0f, 1.0f, "%.2f");
	changed |= ImGui::IsItemDeactivatedAfterEdit();

	ImGui::Separator();
	ImGui::Text("Bottleneck detection");
	bool th = false;
	ImGui::SliderFloat("GPU bound above", &g_Cfg.GpuThreshold, 50.0f, 100.0f, "%.0f%%");
	th |= ImGui::IsItemDeactivatedAfterEdit();
	ImGui::SliderFloat("CPU bound above (busiest thread)", &g_Cfg.CpuThreshold, 50.0f, 100.0f, "%.0f%%");
	th |= ImGui::IsItemDeactivatedAfterEdit();
	if (th) Sampler::SetThresholds(g_Cfg.GpuThreshold, g_Cfg.CpuThreshold);
	changed |= th;

	ImGui::Spacing();
	for (EBottleneck b : { EBottleneck::CPU, EBottleneck::GPU, EBottleneck::Limit })
	{
		DrawBadge(b);
		ImGui::SameLine();
		ImGui::TextWrapped("%s", StyleFor(b).Tooltip);
	}
	ImGui::TextDisabled("GW2 mostly runs on one main thread, so total CPU usage is misleading;\n"
	                    "this looks at the busiest single game thread instead.");

	if (changed) SaveSettings();
}

void OnToggleKeybind(const char* aIdentifier, bool aIsRelease)
{
	if (aIsRelease) return;
	if (strcmp(aIdentifier, KB_TOGGLE) == 0)
	{
		g_Cfg.ShowOverlay = !g_Cfg.ShowOverlay;
		SaveSettings();
	}
}

///----------------------------------------------------------------------------------------------------
/// Load / Unload
///----------------------------------------------------------------------------------------------------
void AddonLoad(AddonAPI_t* aApi)
{
	APIDefs = aApi;

	ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
	ImGui::SetAllocatorFunctions(
		(void* (*)(size_t, void*))APIDefs->ImguiMalloc,
		(void (*)(void*, void*))APIDefs->ImguiFree
	);

	NexusLink = (NexusLinkData_t*)APIDefs->DataLink_Get(DL_NEXUS_LINK);

	LoadSettings();
	Sampler::SetThresholds(g_Cfg.GpuThreshold, g_Cfg.CpuThreshold);
	Sampler::SetPaused(!g_Cfg.ShowOverlay);
	Sampler::Start();

	APIDefs->GUI_Register(RT_Render, RenderOverlay);
	APIDefs->GUI_Register(RT_OptionsRender, RenderOptions);
	APIDefs->InputBinds_RegisterWithString(KB_TOGGLE, OnToggleKeybind, "ALT+SHIFT+F");

	APIDefs->Log(LOGL_INFO, ADDON_NAME, "Loaded.");
}

void AddonUnload()
{
	if (APIDefs)
	{
		APIDefs->InputBinds_Deregister(KB_TOGGLE);
		APIDefs->GUI_Deregister(RenderOverlay);
		APIDefs->GUI_Deregister(RenderOptions);
	}

	Sampler::Stop();
	SaveSettings();
	NexusLink = nullptr;
}
