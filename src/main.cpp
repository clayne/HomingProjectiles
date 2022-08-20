extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
#ifndef DEBUG
	auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
	auto path = logger::log_directory();
	if (!path) {
		return false;
	}

	*path /= Version::PROJECT;
	*path += ".log"sv;
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

	auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

#ifndef DEBUG
	log->set_level(spdlog::level::trace);
#else
	log->set_level(spdlog::level::info);
	log->flush_on(spdlog::level::info);
#endif

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v"s);

	logger::info(FMT_STRING("{} v{}"), Version::PROJECT, Version::NAME);

	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Version::PROJECT.data();
	a_info->version = Version::MAJOR;

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

#include "FenixProjectilesAPI.h"
#include "json/json.h"

std::vector<uint32_t> homie_spells;

int get_mod_index(const std::string_view& name)
{
	auto esp = RE::TESDataHandler::GetSingleton()->LookupModByName(name);
	if (!esp)
		return -1;

	return !esp->IsLight() ? esp->compileIndex << 24 : (0xFE000 | esp->smallFileCompileIndex) << 12;
}

void read_json()
{
	Json::Value json_root;
	std::ifstream ifs;
	ifs.open("Data/SKSE/Plugins/HomingProjectiles/HomieSpells.json");
	ifs >> json_root;
	ifs.close();

	for (auto& mod_name : json_root.getMemberNames()) {
		auto hex = get_mod_index(mod_name);
		if (hex == -1)
			continue;

		const Json::Value mod_data = json_root[mod_name];
		for (int i = 0; i < (int)mod_data.size(); i++) {
			homie_spells.push_back(hex | std::stoul(mod_data[i].asString(), nullptr, 16));
		}
	}
}

bool is_homie(RE::FormID formid) { return std::find(homie_spells.begin(), homie_spells.end(), formid) != homie_spells.end(); }

static set_type_t _set_AutoAimType;
void set_AutoAimType(RE::Projectile* proj) { return (*_set_AutoAimType)(proj); }
void init()
{
	_set_AutoAimType =
		(set_type_t)GetProcAddress(GetModuleHandleA("FenixProjectilesAPI.dll"), "FenixProjectilesAPI_set_AutoAimType");

	read_json();
}

class CoolFireballHook
{
public:
	static void Hook()
	{
		_MissileProjectile__ctor =
			SKSE::GetTrampoline().write_call<5>(REL::ID(42928).address() + 0x219, Ctor);  // SkyrimSE.exe+74B389
	}

private:
	static RE::MissileProjectile* Ctor(RE::MissileProjectile* proj, void* LaunchData)
	{
		proj = _MissileProjectile__ctor(proj, LaunchData);
		if (auto spell = proj->spell; spell && is_homie(spell->formID)) {
			set_AutoAimType(proj);
		}
		return proj;
	}

	static inline REL::Relocation<decltype(Ctor)> _MissileProjectile__ctor;
};

static void SKSEMessageHandler(SKSE::MessagingInterface::Message* message)
{
	switch (message->type) {
	case SKSE::MessagingInterface::kDataLoaded:
		init();
		CoolFireballHook::Hook();

		break;
	}
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
	auto g_messaging = reinterpret_cast<SKSE::MessagingInterface*>(a_skse->QueryInterface(SKSE::LoadInterface::kMessaging));
	if (!g_messaging) {
		logger::critical("Failed to load messaging interface! This error is fatal, plugin will not load.");
		return false;
	}

	logger::info("loaded");

	SKSE::Init(a_skse);
	SKSE::AllocTrampoline(1 << 10);

	g_messaging->RegisterListener("SKSE", SKSEMessageHandler);

	return true;
}
