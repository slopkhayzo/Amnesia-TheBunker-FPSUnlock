// BunkerHighFPS - ASI plugin that raises the game update rate of Amnesia: The Bunker (GOG, x64)
// above the 120 the Framerate menu offers.
//
// The Bunker steps its game logic a fixed number of times per second, and that number is a
// setting: the Framerate menu writes 15/30/60/120 to both Screen.RefreshRate and
// Engine.GameplayUpdateRate, and the engine passes the value straight to its logic timer
// (microsecond clock, no upper limit). With LimitFPS on, a frame is rendered after each update,
// so the update rate is also the frame rate.
//
// This plugin makes the engine read a rate of its own choosing for both settings, whatever the
// settings file or the menu says, and keeps the value in the settings file untouched:
//
//   1. Load: the two cConfigFile::GetInt calls in the user-settings loader that read
//      Screen.RefreshRate and Engine.GameplayUpdateRate return the configured rate.
//   2. Save: the cConfigFile::SetInt call that writes Engine.GameplayUpdateRate back writes the
//      value that was read from the file, not the override.
//   3. Physics damping, which Newton applies per step, is converted to the new rate (see
//      "Newton damping" below).
//
// Every patch site is verified byte-for-byte before anything is written.

#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PLUGIN_NAME        "BunkerHighFPS"
#define PLUGIN_VERSION_STR "1.0.0"

// ---------------------------------------------------------------------------------------------
// AmnesiaTheBunker.exe (GOG build 57301760292462330) addresses, relative to the image base

namespace rva {
	constexpr uintptr_t kConfigGetInt       = 0x0030F4E0;  // int cConfigFile::GetInt(section, name, default)
	constexpr uintptr_t kConfigSetInt       = 0x00310DC0;  // void cConfigFile::SetInt(section, name, value)
	constexpr uintptr_t kLoadRefreshRate    = 0x001080DB;  // call GetInt ; cmp eax,r14d ; ... mov [rbx+7C],eax
	constexpr uintptr_t kLoadGameplayRate   = 0x0010855E;  // call GetInt ; lea rcx,[rbp-70] ; mov [rbx+84],eax
	constexpr uintptr_t kSaveGameplayRate   = 0x0010AB67;  // mov r9d,[rsi+84] ; ... ; call SetInt
	constexpr uintptr_t kGameBase           = 0x0095B4B8;  // game base object: [0] engine, [C0] user settings
}

// Offsets used for the read-only status report.
namespace layout {
	constexpr size_t kBaseEngine         = 0x00;  // cEngine*
	constexpr size_t kBaseSettings       = 0xC0;  // user settings
	constexpr size_t kSettingsRefresh    = 0x7C;  // int, Screen.RefreshRate
	constexpr size_t kSettingsGameplay   = 0x84;  // int, Engine.GameplayUpdateRate
	constexpr size_t kEngineLogicTimer   = 0x48;  // cLogicTimer*
	constexpr size_t kEngineLimitFPS     = 0x88;  // bool
	constexpr size_t kTimerUpdatesPerSec = 0x28;  // int
	constexpr size_t kTimerMaxUpdates    = 0x38;  // int, updates allowed in one pass of the main loop
}

// ---------------------------------------------------------------------------------------------
// Config (BunkerHighFPS.ini next to the .asi)

static struct Config {
	bool enabled       = true;
	int  updatesPerSec = 240;
	bool fixDamping    = true;
} g_cfg;

static char  g_dir[MAX_PATH];
static FILE* g_log;

static void Log(const char* fmt, ...)
{
	if (!g_log) return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	fprintf(g_log, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_log, fmt, ap);
	va_end(ap);
	fputc('\n', g_log);
	fflush(g_log);
}

static void InitPaths(HMODULE self)
{
	GetModuleFileNameA(self, g_dir, sizeof(g_dir));
	char* slash = strrchr(g_dir, '\\');
	if (slash) slash[1] = '\0';

	char logPath[MAX_PATH];
	snprintf(logPath, sizeof(logPath), "%s" PLUGIN_NAME ".log", g_dir);
	g_log = fopen(logPath, "w");
}

static void LoadConfig()
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s" PLUGIN_NAME ".ini", g_dir);
	g_cfg.enabled       = GetPrivateProfileIntA("General", "bEnabled", g_cfg.enabled, path) != 0;
	g_cfg.updatesPerSec = GetPrivateProfileIntA("Simulation", "iUpdatesPerSec", g_cfg.updatesPerSec, path);

	g_cfg.fixDamping    = GetPrivateProfileIntA("Fixes", "bFixPhysicsDamping", g_cfg.fixDamping, path) != 0;

	if (g_cfg.updatesPerSec < 30) g_cfg.updatesPerSec = 30;
	if (g_cfg.updatesPerSec > 1000) g_cfg.updatesPerSec = 1000;  // Newton steps no shorter than 1 ms

	Log("config: bEnabled=%d iUpdatesPerSec=%d bFixPhysicsDamping=%d", g_cfg.enabled, g_cfg.updatesPerSec,
	    g_cfg.fixDamping);
}

// ---------------------------------------------------------------------------------------------
// Hooks

static uintptr_t g_base;

typedef int  (*ConfigGetInt_t)(void* config, void* section, void* name, int defaultValue);
typedef void (*ConfigSetInt_t)(void* config, void* section, void* name, int value);
static ConfigGetInt_t OrigConfigGetInt;
static ConfigSetInt_t OrigConfigSetInt;

// What the settings file holds; -1 until it has been read.
static int g_fileRefreshRate  = -1;
static int g_fileGameplayRate = -1;

static int Hook_LoadRefreshRate(void* config, void* section, void* name, int defaultValue)
{
	const int fileValue = OrigConfigGetInt(config, section, name, defaultValue);
	if (fileValue != g_fileRefreshRate)
		Log("Screen.RefreshRate: settings file has %d, engine gets %d", fileValue, g_cfg.updatesPerSec);
	g_fileRefreshRate = fileValue;
	return g_cfg.updatesPerSec;
}

static int Hook_LoadGameplayRate(void* config, void* section, void* name, int defaultValue)
{
	const int fileValue = OrigConfigGetInt(config, section, name, defaultValue);
	if (fileValue != g_fileGameplayRate)
		Log("Engine.GameplayUpdateRate: settings file has %d, engine gets %d", fileValue, g_cfg.updatesPerSec);
	g_fileGameplayRate = fileValue;
	return g_cfg.updatesPerSec;
}

static void Hook_SaveGameplayRate(void* config, void* section, void* name, int value)
{
	OrigConfigSetInt(config, section, name, g_fileGameplayRate > 0 ? g_fileGameplayRate : value);
}

// ---------------------------------------------------------------------------------------------
// Patching

static bool BytesMatch(uintptr_t at, const uint8_t* expect, size_t n)
{
	return !IsBadReadPtr((const void*)at, n) && memcmp((const void*)at, expect, n) == 0;
}

static bool IsCallTo(uintptr_t at, uintptr_t target)
{
	if (IsBadReadPtr((const void*)at, 5) || *(const uint8_t*)at != 0xE8) return false;
	return at + 5 + (intptr_t) * (const int32_t*)(at + 1) == target;
}

static bool WriteBytes(uintptr_t at, const void* data, size_t n)
{
	DWORD old;
	if (!VirtualProtect((void*)at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy((void*)at, data, n);
	VirtualProtect((void*)at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), (void*)at, n);
	return true;
}

// A call instruction reaches +-2 GB, so each hook gets a small thunk (jmp [rip] ; address) in a
// page allocated close to the executable.
static uint8_t* g_thunks;
static size_t   g_thunkCount;

static bool AllocThunkPage()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	const uintptr_t step = si.dwAllocationGranularity;
	for (uintptr_t at = (g_base & ~(step - 1)) - step; at > g_base - 0x40000000 && at > step; at -= step) {
		g_thunks = (uint8_t*)VirtualAlloc((void*)at, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (g_thunks) return true;
	}
	return false;
}

static bool RedirectCall(uintptr_t callAt, const void* hook)
{
	uint8_t* thunk = g_thunks + 16 * g_thunkCount++;
	const uint8_t jmp[] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp qword ptr [rip+0]
	memcpy(thunk, jmp, sizeof(jmp));
	memcpy(thunk + sizeof(jmp), &hook, sizeof(hook));
	FlushInstructionCache(GetCurrentProcess(), thunk, 16);

	const int64_t rel = (int64_t)(uintptr_t)thunk - (int64_t)(callAt + 5);
	if (rel > INT32_MAX || rel < INT32_MIN) return false;
	const int32_t rel32 = (int32_t)rel;
	return WriteBytes(callAt + 1, &rel32, sizeof(rel32));
}

static bool VerifyGame()
{
	const uintptr_t getInt = g_base + rva::kConfigGetInt;
	const uintptr_t setInt = g_base + rva::kConfigSetInt;

	// call GetInt ; cmp eax,r14d ; lea rcx,[rbp-70] ; cmovl eax,r14d ; mov [rbx+7C],eax
	const uint8_t afterRefresh[] = { 0x41, 0x3B, 0xC6, 0x48, 0x8D, 0x4D, 0x90, 0x41, 0x0F, 0x4C, 0xC6,
	                                 0x89, 0x43, 0x7C };
	if (!IsCallTo(g_base + rva::kLoadRefreshRate, getInt) ||
	    !BytesMatch(g_base + rva::kLoadRefreshRate + 5, afterRefresh, sizeof(afterRefresh))) {
		Log("ERROR: Screen.RefreshRate load does not match (wrong AmnesiaTheBunker.exe version?)");
		return false;
	}
	// call GetInt ; lea rcx,[rbp-70] ; mov [rbx+84],eax
	const uint8_t afterGameplay[] = { 0x48, 0x8D, 0x4D, 0x90, 0x89, 0x83, 0x84, 0x00, 0x00, 0x00 };
	if (!IsCallTo(g_base + rva::kLoadGameplayRate, getInt) ||
	    !BytesMatch(g_base + rva::kLoadGameplayRate + 5, afterGameplay, sizeof(afterGameplay))) {
		Log("ERROR: Engine.GameplayUpdateRate load does not match");
		return false;
	}
	// mov r9d,[rsi+84] ; lea r8,[rsp+20] ; lea rdx,[rsp+60] ; mov rcx,rdi ; call SetInt
	const uint8_t beforeSave[] = { 0x44, 0x8B, 0x8E, 0x84, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x44, 0x24, 0x20,
	                               0x48, 0x8D, 0x54, 0x24, 0x60, 0x48, 0x8B, 0xCF };
	if (!IsCallTo(g_base + rva::kSaveGameplayRate, setInt) ||
	    !BytesMatch(g_base + rva::kSaveGameplayRate - sizeof(beforeSave), beforeSave, sizeof(beforeSave))) {
		Log("ERROR: Engine.GameplayUpdateRate save does not match");
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Newton damping
//
// Newton removes a fixed fraction of a body's velocity on every step (veloc -= veloc * coef,
// coef = 0.02 * damping), whatever the step length, and the engine passes the damping of an
// entity straight through. With more steps per second a body loses more speed per second: a
// falling object reaches a lower speed. This is already so at the menu's 120 compared with 60,
// and much more at higher rates. The damping the game asks for is converted so that the loss per
// 1/60 s is what it is at 60 updates per second, the rate the entities were tuned at.

constexpr double kNewtonDampScale = 0.02;
static double g_rateMul = 1.0;  // updates per second / 60

static float DampToNewton(float damping)
{
	const double d = damping < 0.0f ? 0.0 : damping > 1.0f ? 1.0 : (double)damping;
	return (float)((1.0 - std::pow(1.0 - kNewtonDampScale * d, 1.0 / g_rateMul)) / kNewtonDampScale);
}

static float DampFromNewton(float damping)
{
	return (float)((1.0 - std::pow(1.0 - kNewtonDampScale * (double)damping, g_rateMul)) / kNewtonDampScale);
}

static void  (*RealSetLinearDamping)(void* body, float damping);
static float (*RealGetLinearDamping)(void* body);
static void  (*RealSetAngularDamping)(void* body, const float* damping);
static void  (*RealGetAngularDamping)(void* body, float* damping);
static void* (*RealCreateBody)(void* world, void* collision, const float* matrix);

static void Hook_SetLinearDamping(void* body, float damping)
{
	RealSetLinearDamping(body, DampToNewton(damping));
}

static float Hook_GetLinearDamping(void* body)
{
	return DampFromNewton(RealGetLinearDamping(body));
}

static void Hook_SetAngularDamping(void* body, const float* damping)
{
	const float d[3] = { DampToNewton(damping[0]), DampToNewton(damping[1]), DampToNewton(damping[2]) };
	RealSetAngularDamping(body, d);
}

static void Hook_GetAngularDamping(void* body, float* damping)
{
	RealGetAngularDamping(body, damping);
	for (int i = 0; i < 3; i++) damping[i] = DampFromNewton(damping[i]);
}

// A new body starts with Newton's default damping, which needs the same conversion.
static void* Hook_CreateBody(void* world, void* collision, const float* matrix)
{
	void* body = RealCreateBody(world, collision, matrix);
	if (body) {
		RealSetLinearDamping(body, DampToNewton(RealGetLinearDamping(body)));
		float d[3];
		RealGetAngularDamping(body, d);
		Hook_SetAngularDamping(body, d);
	}
	return body;
}

// One import slot of the main executable, found by DLL + function name.
static void** FindImport(const char* dll, const char* func)
{
	uint8_t* base = (uint8_t*)g_base;
	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return nullptr;
	for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; imp++) {
		if (_stricmp((const char*)(base + imp->Name), dll) != 0) continue;
		auto names = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
		auto iat = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
		for (; names->u1.AddressOfData; names++, iat++) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			auto byName = (IMAGE_IMPORT_BY_NAME*)(base + names->u1.AddressOfData);
			if (strcmp((const char*)byName->Name, func) == 0)
				return (void**)&iat->u1.Function;
		}
	}
	return nullptr;
}

static bool InstallDampingFix()
{
	static const struct { const char* name; void* hook; void** real; } kImports[] = {
		{ "NewtonBodySetLinearDamping",  (void*)&Hook_SetLinearDamping,  (void**)&RealSetLinearDamping },
		{ "NewtonBodyGetLinearDamping",  (void*)&Hook_GetLinearDamping,  (void**)&RealGetLinearDamping },
		{ "NewtonBodySetAngularDamping", (void*)&Hook_SetAngularDamping, (void**)&RealSetAngularDamping },
		{ "NewtonBodyGetAngularDamping", (void*)&Hook_GetAngularDamping, (void**)&RealGetAngularDamping },
		{ "NewtonCreateBody",            (void*)&Hook_CreateBody,        (void**)&RealCreateBody },
	};
	void** slots[5];
	for (int i = 0; i < 5; i++) {
		slots[i] = FindImport("newton.dll", kImports[i].name);
		if (!slots[i]) {
			Log("ERROR: import %s not found; physics damping fix disabled", kImports[i].name);
			return false;
		}
	}
	for (int i = 0; i < 5; i++) *kImports[i].real = *slots[i];
	for (int i = 0; i < 5; i++) WriteBytes((uintptr_t)slots[i], &kImports[i].hook, sizeof(void*));
	Log("physics damping fix active (damping 1.0 -> %.4f, 0.1 -> %.4f)", DampToNewton(1.0f), DampToNewton(0.1f));
	return true;
}

// ---------------------------------------------------------------------------------------------
// Status report

template <class T>
static bool ReadAt(uintptr_t at, T* out)
{
	if (!at || IsBadReadPtr((const void*)at, sizeof(T))) return false;
	*out = *(const T*)at;
	return true;
}

// Reads what the engine ended up with. Read-only; returns false while the engine is not up.
static bool ReadEngineState(int* settingsRefresh, int* settingsGameplay, int* timerRate, int* timerMax,
                            uint8_t* limitFPS)
{
	uintptr_t gameBase, engine, settings, timer;
	return ReadAt(g_base + rva::kGameBase, &gameBase) &&
	       ReadAt(gameBase + layout::kBaseEngine, &engine) &&
	       ReadAt(gameBase + layout::kBaseSettings, &settings) &&
	       ReadAt(settings + layout::kSettingsRefresh, settingsRefresh) &&
	       ReadAt(settings + layout::kSettingsGameplay, settingsGameplay) &&
	       ReadAt(engine + layout::kEngineLogicTimer, &timer) &&
	       ReadAt(timer + layout::kTimerUpdatesPerSec, timerRate) &&
	       ReadAt(timer + layout::kTimerMaxUpdates, timerMax) &&
	       ReadAt(engine + layout::kEngineLimitFPS, limitFPS);
}

// Logs the engine's state once it is running, and again whenever it changes.
static DWORD WINAPI StatusThread(void*)
{
	int last[4] = { -1, -1, -1, -1 };
	uint8_t lastLimit = 0xFF;
	for (;;) {
		Sleep(2000);
		int now[4];
		uint8_t limit;
		if (!ReadEngineState(&now[0], &now[1], &now[2], &now[3], &limit)) continue;
		if (memcmp(now, last, sizeof(now)) == 0 && limit == lastLimit) continue;
		memcpy(last, now, sizeof(now));
		lastLimit = limit;
		Log("engine: logic timer %d updates per second (max %d per pass), settings RefreshRate=%d "
		    "GameplayUpdateRate=%d, LimitFPS=%d", now[2], now[3], now[0], now[1], limit);
	}
}

static void Install()
{
	LoadConfig();
	if (!g_cfg.enabled) {
		Log("disabled by bEnabled=0; no changes made");
		return;
	}

	g_base = (uintptr_t)GetModuleHandleA(nullptr);
	if (!VerifyGame()) {
		Log("Plugin disabled; no changes made.");
		return;
	}

	int state[4];
	uint8_t limit;
	if (ReadEngineState(&state[0], &state[1], &state[2], &state[3], &limit))
		Log("WARNING: loaded after the engine started (logic timer already at %d); the rate applies "
		    "the next time the game loads its settings", state[2]);

	if (!AllocThunkPage()) {
		Log("ERROR: could not allocate memory near the executable; plugin disabled");
		return;
	}
	OrigConfigGetInt = (ConfigGetInt_t)(g_base + rva::kConfigGetInt);
	OrigConfigSetInt = (ConfigSetInt_t)(g_base + rva::kConfigSetInt);
	if (!RedirectCall(g_base + rva::kLoadRefreshRate, (const void*)&Hook_LoadRefreshRate) ||
	    !RedirectCall(g_base + rva::kLoadGameplayRate, (const void*)&Hook_LoadGameplayRate) ||
	    !RedirectCall(g_base + rva::kSaveGameplayRate, (const void*)&Hook_SaveGameplayRate)) {
		Log("ERROR: could not redirect the settings calls");
		return;
	}
	Log("settings hooks installed (image base %p): update rate %d", (void*)g_base, g_cfg.updatesPerSec);

	if (g_cfg.fixDamping) {
		g_rateMul = g_cfg.updatesPerSec / 60.0;
		InstallDampingFix();
	}

	HANDLE thread = CreateThread(nullptr, 0, StatusThread, nullptr, 0, nullptr);
	if (thread) CloseHandle(thread);
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(self);
		InitPaths(self);
		Log(PLUGIN_NAME " " PLUGIN_VERSION_STR " (ASI plugin)");
		Install();
	}
	return TRUE;
}
