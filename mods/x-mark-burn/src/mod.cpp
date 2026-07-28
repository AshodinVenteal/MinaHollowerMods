#include "MinaModAPI.h"
#include "MinaModEnums.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <atomic>

#include "generated/xmark_runtime_assets.generated.h"
#include "generated/clashrend_regular_profile.generated.h"


namespace {

#include "detail/runtime.inl"
#include "detail/frame_bridge.inl"
#include "detail/enemy_registry.inl"
#include "detail/rendering.inl"
#include "detail/effects.inl"
#include "detail/targeting.inl"
#include "detail/burn.inl"
#include "detail/combat.inl"
#include "detail/commands.inl"
#include "detail/lifecycle.inl"

} // namespace


extern "C" __declspec(dllexport) void MinaMod_Init(MinaModAPI *mm) {
    g_mina = mm;
    if (!g_mina) {
        return;
    }
    xmark_environment_cache_initialize();
    clashrend_apply_runtime_damage_values();
    if (env_bool("MINA_XMARK_API_SURFACE_LOG", false)) {
        log_api_surface();
    }
    g_mina->InstallHook("FixedUpdate", 0, fixed_update);
    g_mina->InstallHook("WorldConstruct", 0, world_construct);
    g_mina->InstallHook("WorldDestroy", 0, world_destroy);
    g_mina->InstallHook("WorldUpdate", 0, world_update);
    g_mina->InstallHook("GameShutdown", 0, game_shutdown);
    g_mina->Log("XMarkBurn native room/effect probe installed.\n");
}
