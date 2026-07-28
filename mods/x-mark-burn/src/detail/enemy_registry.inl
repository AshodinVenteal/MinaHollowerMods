Vec3 official_spawn_position() {
    Vec3 position{0.0f, 0.0f, 0.0f};
    const uintptr_t game = game_singleton();
    const uintptr_t player = player_entity();
    if (!game || !player) {
        if (g_mina) {
            g_mina->PlayerGetPos(&position.x, &position.y);
        }
        return position;
    }

    float world_scale = 1.0f;
    float room_offset_x = 0.0f;
    float room_offset_y = 0.0f;
    float room_offset_z = 0.0f;
    float player_x = 0.0f;
    float player_y = 0.0f;
    float player_z = 0.0f;
    safe_read_float(world_scale_address(), &world_scale);
    safe_read_float(game + 0x144, &room_offset_x);
    safe_read_float(game + 0x148, &room_offset_y);
    safe_read_float(game + 0x14C, &room_offset_z);
    safe_read_float(player + 0xDC, &player_x);
    safe_read_float(player + 0xE0, &player_y);
    safe_read_float(player + 0xE4, &player_z);

    position.x = player_x + (room_offset_x * world_scale);
    position.y = player_y + (room_offset_y * world_scale);
    position.z = player_z + (room_offset_z * world_scale);
    return position;
}

bool is_pseudo_room(unsigned int room_index) {
    return room_index == 0xFFFFFFFFu;
}

const char *entity_type_name(uint32_t entity_type) {
    switch (entity_type) {
    case ENTITYTYPE_ANIM_EFFECT:
        return "ENTITYTYPE_ANIM_EFFECT";
    case ENTITYTYPE_ANIM_EFFECT_EMITTER:
        return "ENTITYTYPE_ANIM_EFFECT_EMITTER";
    case ENTITYTYPE_MARKER:
        return "ENTITYTYPE_MARKER";
    case ENTITYTYPE_TEST_TRAINING_DUMMY:
        return "ENTITYTYPE_TEST_TRAINING_DUMMY";
    case ENTITYTYPE_TRAINING_DUMMY:
        return "ENTITYTYPE_TRAINING_DUMMY";
    case ENTITYTYPE_SHOCKTROOPER_ENEMY:
        return "ENTITYTYPE_SHOCKTROOPER_ENEMY";
    case ENTITYTYPE_SHOCKTROOPER_ENEMY_L3:
        return "ENTITYTYPE_SHOCKTROOPER_ENEMY_L3";
    case ENTITYTYPE_SHOCKTROOPER_ENEMY_L4:
        return "ENTITYTYPE_SHOCKTROOPER_ENEMY_L4";
    case ENTITYTYPE_GREMLIN_ENEMY:
        return "ENTITYTYPE_GREMLIN_ENEMY";
    case ENTITYTYPE_GREMLIN_SPEAR:
        return "ENTITYTYPE_GREMLIN_SPEAR";
    case ENTITYTYPE_BADRAT_ENEMY:
        return "ENTITYTYPE_BADRAT_ENEMY";
    case ENTITYTYPE_GOOPER_ENEMY:
        return "ENTITYTYPE_GOOPER_ENEMY";
    case ENTITYTYPE_SENTRY:
        return "ENTITYTYPE_SENTRY";
    default:
        return "ENTITYTYPE_UNKNOWN";
    }
}

void log_api_pointer(const char *name, const void *ptr) {
    if (!g_mina) {
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const uintptr_t address = reinterpret_cast<uintptr_t>(ptr);
    const unsigned long long rva = (address >= base) ? static_cast<unsigned long long>(address - base) : 0ull;
    char bytes[3 * 32 + 1]{};

    __try {
        const unsigned char *p = reinterpret_cast<const unsigned char *>(address);
        for (int i = 0; i < 32; ++i) {
            std::snprintf(bytes + (i * 3), sizeof(bytes) - (i * 3), "%02X ", static_cast<unsigned int>(p[i]));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(bytes, sizeof(bytes), "<unreadable>");
    }

    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn API %s ptr=0x%p rva=0x%llX bytes=%s\n",
        name,
        ptr,
        rva,
        bytes);
    g_mina->Log(message);
}

unsigned int exe_image_size() {
    const uintptr_t base = exe_base();
    if (!base) {
        return 0;
    }
    __try {
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return 0;
        }
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return 0;
        }
        return nt->OptionalHeader.SizeOfImage;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool pointer_in_main_module(uintptr_t ptr) {
    const uintptr_t base = exe_base();
    const unsigned int size = exe_image_size();
    return base && size && ptr >= base && ptr < base + size;
}

bool pointer_is_executable(uintptr_t ptr) {
    if (!ptr) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void *>(ptr), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protect = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return protect == PAGE_EXECUTE ||
           protect == PAGE_EXECUTE_READ ||
           protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}

uintptr_t api_function_field(size_t field_offset) {
    if (!g_mina) {
        return 0;
    }
    uintptr_t ptr = 0;
    if (!safe_read_ptr(reinterpret_cast<uintptr_t>(g_mina) + field_offset, &ptr)) {
        return 0;
    }
    return ptr;
}

bool api_function_field_ready(size_t field_offset) {
    return pointer_is_executable(api_function_field(field_offset));
}

bool xmark_game_clock_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetElapsedTime));
}

void update_xmark_game_clock(World *world, unsigned long long real_now_ms) {
    if (!world ||
        !env_bool("MINA_XMARK_GAME_TIME_TIMERS_ENABLED", true) ||
        !xmark_game_clock_api_available()) {
        g_xmark_game_clock_ready = false;
        g_xmark_game_clock_world = world;
        g_xmark_game_clock_last_real_ms = real_now_ms;
        g_xmark_game_clock_last_elapsed = 0.0f;
        return;
    }

    if (!g_xmark_game_clock_ms) {
        g_xmark_game_clock_ms = real_now_ms;
    }
    if (g_xmark_game_clock_world != world) {
        g_xmark_game_clock_world = world;
        g_xmark_game_clock_fraction_ms = 0.0;
    }

    float elapsed = 0.0f;
    bool read = false;
    __try {
        elapsed = g_mina->WorldGetElapsedTime(world);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        elapsed = 0.0f;
        read = false;
    }
    if (!read || !std::isfinite(elapsed) || elapsed < 0.0f) {
        g_xmark_game_clock_ready = false;
        g_xmark_game_clock_last_real_ms = real_now_ms;
        g_xmark_game_clock_last_elapsed = 0.0f;
        return;
    }

    const float max_elapsed = std::max(
        0.016f,
        env_float("MINA_XMARK_GAME_TIME_MAX_FRAME_SECONDS", 0.25f));
    elapsed = std::min(elapsed, max_elapsed);
    g_xmark_game_clock_fraction_ms += static_cast<double>(elapsed) * 1000.0;
    const unsigned long long whole_ms = static_cast<unsigned long long>(g_xmark_game_clock_fraction_ms);
    if (whole_ms > 0) {
        g_xmark_game_clock_ms += whole_ms;
        g_xmark_game_clock_fraction_ms -= static_cast<double>(whole_ms);
    }
    g_xmark_game_clock_last_elapsed = elapsed;
    g_xmark_game_clock_last_real_ms = real_now_ms;
    ++g_xmark_game_clock_updates;
    g_xmark_game_clock_ready = true;
    if (!g_xmark_game_clock_activation_logged && g_mina) {
        g_xmark_game_clock_activation_logged = true;
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn gameplay clock active world=0x%p elapsed=%.6f statusMs=%llu.\n",
            reinterpret_cast<void *>(world),
            static_cast<double>(elapsed),
            g_xmark_game_clock_ms);
        g_mina->Log(message);
    }
}

unsigned long long xmark_status_now_ms() {
    if (env_bool("MINA_XMARK_GAME_TIME_TIMERS_ENABLED", true) &&
        g_xmark_game_clock_ready &&
        g_xmark_game_clock_ms) {
        return g_xmark_game_clock_ms;
    }
    return GetTickCount64();
}

bool xmark_render_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, CreateTexture)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateTexture)) &&
           api_function_field_ready(offsetof(MinaModAPI, CreateIndexBuffer)) &&
           api_function_field_ready(offsetof(MinaModAPI, CreateVertexBuffer)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateGpuBuffer)) &&
           api_function_field_ready(offsetof(MinaModAPI, GetRenderPass)) &&
           api_function_field_ready(offsetof(MinaModAPI, CreateRenderObject)) &&
           api_function_field_ready(offsetof(MinaModAPI, RenderDrawCallSetIndexBuffer)) &&
           api_function_field_ready(offsetof(MinaModAPI, RenderDrawCallSetVertexBuffer)) &&
           api_function_field_ready(offsetof(MinaModAPI, RenderDrawCallSetTexture)) &&
           api_function_field_ready(offsetof(MinaModAPI, RenderCmdDrawIndexed));
}

bool xmark_marker_debug_draw_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, CreateTexture)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateTexture)) &&
           api_function_field_ready(offsetof(MinaModAPI, GetDebugDraw)) &&
           api_function_field_ready(offsetof(MinaModAPI, DebugDrawTexturedQuad));
}

bool xmark_component_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetGameRootEntity)) &&
           api_function_field_ready(offsetof(MinaModAPI, EntityGetChildren)) &&
           api_function_field_ready(offsetof(MinaModAPI, EntityGetMainComponent)) &&
           api_function_field_ready(offsetof(MinaModAPI, ComponentGetTypeName)) &&
           api_function_field_ready(offsetof(MinaModAPI, ComponentGetParent)) &&
           api_function_field_ready(offsetof(MinaModAPI, ComponentGetType)) &&
           api_function_field_ready(offsetof(MinaModAPI, ComponentIsa)) &&
           api_function_field_ready(offsetof(MinaModAPI, EntityGetWorldTransform)) &&
           api_function_field_ready(offsetof(MinaModAPI, PlayerGetComponent)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatCoreGetHealth)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatCoreGetHealthMax));
}

bool xmark_combat_core_health_write_api_available() {
    return xmark_component_api_available() &&
           api_function_field_ready(offsetof(MinaModAPI, CombatCoreSetHealth));
}

bool xmark_sound_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, SoundPlay));
}

bool xmark_sound_stop_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, SoundStop));
}

bool xmark_sound_is_playing_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, SoundIsPlaying));
}

bool xmark_play_sound_name(const char *sound_name, const char *reason) {
    if (!sound_name || !sound_name[0] || !xmark_sound_api_available()) {
        return false;
    }
    bool played = false;
    __try {
        g_mina->SoundPlay(sound_name);
        played = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        played = false;
    }
    if (g_mina && env_bool("MINA_XMARK_BURN_SFX_LOG", false)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn SFX reason=%s name=%s played=%u\n",
            reason ? reason : "<none>",
            sound_name,
            played ? 1u : 0u);
        g_mina->Log(message);
    }
    return played;
}

bool xmark_stop_sound_name(const char *sound_name, const char *reason) {
    if (!sound_name || !sound_name[0] || !xmark_sound_stop_api_available()) {
        return false;
    }
    bool stopped = false;
    __try {
        g_mina->SoundStop(sound_name);
        stopped = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        stopped = false;
    }
    if (g_mina && env_bool("MINA_XMARK_BURN_SFX_LOG", false)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn SFX stop reason=%s name=%s stopped=%u\n",
            reason ? reason : "<none>",
            sound_name,
            stopped ? 1u : 0u);
        g_mina->Log(message);
    }
    return stopped;
}

bool xmark_sound_is_playing_name(const char *sound_name) {
    if (!sound_name || !sound_name[0] || !xmark_sound_is_playing_api_available()) {
        return false;
    }
    bool playing = false;
    __try {
        playing = g_mina->SoundIsPlaying(sound_name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        playing = false;
    }
    return playing;
}

void xmark_burn_tick_sfx_name(char *sound_name, size_t sound_name_size) {
    if (!sound_name || sound_name_size == 0) {
        return;
    }
    sound_name[0] = 0;
    if (!xmark_read_environment_value(
            "MINA_XMARK_BURN_SFX_TICK_NAME",
            sound_name,
            sound_name_size)) {
        std::snprintf(sound_name, sound_name_size, "%s", "enemy_death_flames2");
    }
}

void stop_xmark_owned_burn_tick_sfx(const char *reason) {
    if (!g_xmark_burn_tick_sfx_owned) {
        return;
    }
    char sound_name[96]{};
    xmark_burn_tick_sfx_name(sound_name, sizeof(sound_name));
    xmark_stop_sound_name(sound_name, reason);
    g_xmark_burn_tick_sfx_owned = false;
}

bool xmark_play_timed_sfx(
    const char *name_env,
    const char *fallback_name,
    const char *cooldown_env,
    unsigned int fallback_cooldown_ms,
    unsigned long long now_ms,
    unsigned long long *last_played_ms,
    const char *reason) {
    if (!env_bool("MINA_XMARK_SFX_ENABLED", true) ||
        xmark_runtime_overlays_hidden_for_pause() ||
        !last_played_ms) {
        return false;
    }
    const unsigned int cooldown_ms = env_uint(cooldown_env, fallback_cooldown_ms);
    if (*last_played_ms && now_ms >= *last_played_ms &&
        now_ms - *last_played_ms < cooldown_ms) {
        return false;
    }

    char sound_name[96]{};
    if (!xmark_read_environment_value(name_env, sound_name, sizeof(sound_name))) {
        std::snprintf(sound_name, sizeof(sound_name), "%s", fallback_name ? fallback_name : "");
    }
    if (!sound_name[0]) {
        return false;
    }

    if (xmark_play_sound_name(sound_name, reason)) {
        *last_played_ms = now_ms;
        return true;
    }
    return false;
}

void play_xmark_burn_ignite_sfx(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_SFX_ENABLED", true)) {
        return;
    }
    xmark_play_timed_sfx(
        "MINA_XMARK_BURN_SFX_IGNITE_NAME",
        "fireburst",
        "MINA_XMARK_BURN_SFX_IGNITE_COOLDOWN_MS",
        100u,
        now_ms,
        &g_xmark_burn_last_ignite_sfx_ms,
        "consume");
}

void play_xmark_apply_sfx(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_APPLY_SFX_ENABLED", true)) {
        return;
    }
    xmark_play_timed_sfx(
        "MINA_XMARK_APPLY_SFX_NAME",
        "enemy_damage",
        "MINA_XMARK_APPLY_SFX_COOLDOWN_MS",
        100u,
        now_ms,
        &g_xmark_last_apply_sfx_ms,
        "mark-apply");
}

void observe_xmark_apply_sfx(unsigned long long now_ms) {
    for (size_t index = 0; index < 16; ++index) {
        XMarkAttachment &attachment = g_xmark_attachments[index];
        XMarkApplySfxObservation &observed = g_xmark_apply_sfx_observations[index];
        if (!attachment.active || !attachment.started_ms) {
            observed = XMarkApplySfxObservation{};
            continue;
        }
        observed.target = attachment.target;
        observed.official_combat_core = attachment.official_combat_core;
        observed.visual_key = attachment.visual_key;
        observed.started_ms = attachment.started_ms;
        if (!attachment.apply_sfx_pending) {
            continue;
        }
        attachment.apply_sfx_pending = false;
        play_xmark_apply_sfx(now_ms);
    }
}

void play_xmark_burn_tick_sfx(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_SFX_ENABLED", true)) {
        return;
    }
    if (xmark_play_timed_sfx(
        "MINA_XMARK_BURN_SFX_TICK_NAME",
        "enemy_death_flames2",
        "MINA_XMARK_BURN_SFX_TICK_COOLDOWN_MS",
        90u,
        now_ms,
        &g_xmark_burn_last_tick_sfx_ms,
        "damage-tick")) {
        g_xmark_burn_tick_sfx_owned = true;
    }
}

bool xmark_official_damage_gate_enabled() {
    return env_bool("MINA_XMARK_OFFICIAL_DAMAGE_GATE_ENABLED", true) &&
           xmark_component_api_available();
}

bool xmark_update_queue_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetEntityPostArtUpdateQueue)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueAdd)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueRemove));
}

bool xmark_entity_hit_update_queue_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetEntityHitUpdateQueue)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueAdd)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueRemove));
}

bool xmark_hud_update_queue_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetHUDUpdateQueue)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueAdd)) &&
           api_function_field_ready(offsetof(MinaModAPI, UpdateQueueRemove));
}

bool xmark_player_world_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, PlayerGetWorld));
}

bool xmark_spawn_point_registry_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, GameComponentGetSpawnPoint)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetSpawnedEntity)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointIsEntitySpawned)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointIsEntityKilled)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetEntityType)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetNameHash)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetNameLevelHash)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetLayerNameHash)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetSpawnType)) &&
           api_function_field_ready(offsetof(MinaModAPI, SpawnPointGetTileLayerIndex));
}

bool xmark_spawn_point_room_registry_enabled() {
    return env_bool("MINA_XMARK_SPAWN_POINT_ROOM_REGISTRY", true) &&
           xmark_spawn_point_registry_api_available();
}

unsigned int xmark_spawn_point_identity_count() {
    unsigned int count = 0;
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        if (host.spawn_identity_valid && host.spawn_point) {
            ++count;
        }
    }
    return count;
}

bool xmark_spawn_point_room_registry_authoritative() {
    if (!xmark_spawn_point_room_registry_enabled() || g_official_enemy_host_count == 0) {
        return false;
    }
    return xmark_spawn_point_identity_count() == g_official_enemy_host_count;
}

bool xmark_combat_full_scan_fallback_allowed() {
    return !xmark_spawn_point_room_registry_authoritative() ||
           env_bool("MINA_XMARK_ROOM_REGISTRY_ALLOW_COMBAT_FULL_SCAN_FALLBACK", false);
}

World *xmark_player_world_safe() {
    if (!g_mina || !xmark_player_world_api_available()) {
        return nullptr;
    }
    World *world = nullptr;
    __try {
        world = g_mina->PlayerGetWorld();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        world = nullptr;
    }
    return world;
}

bool xmark_world_matches_player_world(World *world) {
    if (!world) {
        return false;
    }
    if (!env_bool("MINA_XMARK_PLAYER_WORLD_GATE", true) ||
        !xmark_player_world_api_available()) {
        return true;
    }
    return xmark_player_world_safe() == world;
}

bool xmark_weak_ptr_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, CreateWeakPtr)) &&
           api_function_field_ready(offsetof(MinaModAPI, DestroyWeakPtr)) &&
           api_function_field_ready(offsetof(MinaModAPI, WeakPtrGet));
}

bool xmark_combat_shape_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, CombatCoreGetDefenseShape)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatShapeGetShapeCount)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatShapeIsAABB)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatShapeGetAABB)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatShapeIsSphere)) &&
           api_function_field_ready(offsetof(MinaModAPI, CombatShapeGetSphere));
}

MM_WeakPtr *xmark_weak_ptr_create(void *ptr) {
    if (!ptr || !xmark_weak_ptr_api_available()) {
        return nullptr;
    }
    MM_WeakPtr *weak = nullptr;
    __try {
        weak = g_mina->CreateWeakPtr(ptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        weak = nullptr;
    }
    return weak;
}

void xmark_weak_ptr_destroy(MM_WeakPtr *weak) {
    if (!weak || !xmark_weak_ptr_api_available()) {
        return;
    }
    __try {
        g_mina->DestroyWeakPtr(weak);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void *xmark_weak_ptr_get(MM_WeakPtr *weak) {
    if (!weak || !xmark_weak_ptr_api_available()) {
        return nullptr;
    }
    void *ptr = nullptr;
    __try {
        ptr = g_mina->WeakPtrGet(weak);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ptr = nullptr;
    }
    return ptr;
}

bool xmark_anim_bounds_api_available() {
    return xmark_game_anim_init_rtti() &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetWorldTransform)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetCurrentFrameBound));
}

bool xmark_game_anim_visual_bounds(
    ycComponent *anim,
    Vec3 *center_out,
    float *half_w_out,
    float *half_h_out) {
    if (!anim || !xmark_anim_bounds_api_available()) {
        return false;
    }
    MM_Transform transform{};
    MM_AABB bound{};
    bool ok = false;
    __try {
        g_mina->GameAnimGetWorldTransform(anim, &transform);
        g_mina->GameAnimGetCurrentFrameBound(anim, &bound);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        return false;
    }

    const float sx = std::isfinite(transform.s.x) ? transform.s.x : 1.0f;
    const float sy = std::isfinite(transform.s.y) ? transform.s.y : 1.0f;
    const float local_center_x = bound.center.x * sx;
    const float local_center_y = bound.center.y * sy;
    const float local_half_x = std::fabs(bound.extents.x * sx);
    const float local_half_y = std::fabs(bound.extents.y * sy);

    const float r00 = 1.0f - 2.0f * (transform.r.y * transform.r.y + transform.r.z * transform.r.z);
    const float r01 = 2.0f * (transform.r.x * transform.r.y - transform.r.z * transform.r.w);
    const float r10 = 2.0f * (transform.r.x * transform.r.y + transform.r.z * transform.r.w);
    const float r11 = 1.0f - 2.0f * (transform.r.x * transform.r.x + transform.r.z * transform.r.z);

    const float center_x = transform.t.x + r00 * local_center_x + r01 * local_center_y;
    const float center_y = transform.t.y + r10 * local_center_x + r11 * local_center_y;
    const float half_w = std::fabs(r00) * local_half_x + std::fabs(r01) * local_half_y;
    const float half_h = std::fabs(r10) * local_half_x + std::fabs(r11) * local_half_y;
    if (!std::isfinite(center_x) || !std::isfinite(center_y) ||
        !std::isfinite(half_w) || !std::isfinite(half_h) ||
        half_w <= 0.001f || half_h <= 0.001f) {
        return false;
    }
    if (center_out) {
        *center_out = Vec3{center_x, center_y, transform.t.z + bound.center.z * transform.s.z};
    }
    if (half_w_out) {
        *half_w_out = half_w;
    }
    if (half_h_out) {
        *half_h_out = half_h;
    }
    return true;
}

bool xmark_anim_property_probe_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPropertyExists)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPropertyPoint)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPropertyAnchor)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPropertyRect)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPropertyCircle));
}

bool xmark_anim_property_probe_seen(uintptr_t component) {
    for (unsigned int i = 0; i < g_anim_property_probed_component_count; ++i) {
        if (g_anim_property_probed_components[i] == component) {
            return true;
        }
    }
    if (g_anim_property_probed_component_count < _countof(g_anim_property_probed_components)) {
        g_anim_property_probed_components[g_anim_property_probed_component_count++] = component;
    }
    return false;
}

void xmark_probe_anim_properties(ycComponent *anim) {
    if (!anim || !g_mina ||
        !env_bool("MINA_XMARK_ANIM_PROPERTY_PROBE_ENABLED", false) ||
        !xmark_anim_property_probe_api_available() ||
        xmark_anim_property_probe_seen(reinterpret_cast<uintptr_t>(anim))) {
        return;
    }

    char seq[64]{};
    char seq_full[96]{};
    uint32_t frame_idx = 0;
    uint32_t num_frames = 0;
    uint32_t loops = 0;
    float frame_time = 0.0f;
    float play_rate = 1.0f;
    bool visible = true;
    MM_Transform world{};
    MM_AABB frame_bound{};
    __try {
        copy_mm_string_to_cstr(seq, sizeof(seq), g_mina->GameAnimGetSeqNameNoDir(anim));
        if (g_mina->GameAnimGetSeqName) {
            copy_mm_string_to_cstr(seq_full, sizeof(seq_full), g_mina->GameAnimGetSeqName(anim));
        }
        frame_idx = g_mina->GameAnimGetSeqFrameIdx ? g_mina->GameAnimGetSeqFrameIdx(anim) : 0u;
        num_frames = api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumSeqFrames))
            ? g_mina->GameAnimGetNumSeqFrames(anim) : 0u;
        loops = api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumLoopsPlayed))
            ? g_mina->GameAnimGetNumLoopsPlayed(anim) : 0u;
        frame_time = api_function_field_ready(offsetof(MinaModAPI, GameAnimGetCurrentFrameTime))
            ? g_mina->GameAnimGetCurrentFrameTime(anim) : 0.0f;
        play_rate = api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPlayRate))
            ? g_mina->GameAnimGetPlayRate(anim) : 1.0f;
        visible = api_function_field_ready(offsetof(MinaModAPI, GameAnimIsVisible))
            ? g_mina->GameAnimIsVisible(anim) : true;
        g_mina->GameAnimGetWorldTransform(anim, &world);
        g_mina->GameAnimGetCurrentFrameBound(anim, &frame_bound);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn anim inspect component=0x%p seq=%s full=%s frame=%u/%u loops=%u frameTime=%.4f playRate=%.3f visible=%u world=(%.3f,%.3f,%.3f) boundCenter=(%.3f,%.3f) boundHalf=(%.3f,%.3f)\n",
        reinterpret_cast<void *>(anim),
        seq[0] ? seq : "-",
        seq_full[0] ? seq_full : "-",
        frame_idx,
        num_frames,
        loops,
        static_cast<double>(frame_time),
        static_cast<double>(play_rate),
        visible ? 1u : 0u,
        static_cast<double>(world.t.x),
        static_cast<double>(world.t.y),
        static_cast<double>(world.t.z),
        static_cast<double>(frame_bound.center.x),
        static_cast<double>(frame_bound.center.y),
        static_cast<double>(frame_bound.extents.x),
        static_cast<double>(frame_bound.extents.y));
    g_mina->Log(message);

    static const char *kCandidateProperties[] = {
        "center", "origin", "root", "body", "head", "feet", "hand",
        "weapon", "weaponTip", "tip", "contact", "attack", "hit",
        "hitbox", "hurt", "hurtbox", "damage", "smear", "anchor"
    };
    for (const char *name : kCandidateProperties) {
        bool exists = false;
        __try {
            exists = g_mina->GameAnimGetPropertyExists(anim, name);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            exists = false;
        }
        if (!exists) {
            continue;
        }

        MM_Vec2 point{};
        MM_Vec2 anchor{};
        float angle = 0.0f;
        MM_AABB rect{};
        MM_Circle circle{};
        __try { g_mina->GameAnimGetPropertyPoint(anim, name, &point); }
        __except (EXCEPTION_EXECUTE_HANDLER) { point = {}; }
        __try { g_mina->GameAnimGetPropertyAnchor(anim, name, &anchor, &angle); }
        __except (EXCEPTION_EXECUTE_HANDLER) { anchor = {}; angle = 0.0f; }
        __try { g_mina->GameAnimGetPropertyRect(anim, name, &rect); }
        __except (EXCEPTION_EXECUTE_HANDLER) { rect = {}; }
        __try { g_mina->GameAnimGetPropertyCircle(anim, name, &circle); }
        __except (EXCEPTION_EXECUTE_HANDLER) { circle = {}; }
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn anim property component=0x%p name=%s point=(%.3f,%.3f) anchor=(%.3f,%.3f,%.3f) rectCenter=(%.3f,%.3f) rectHalf=(%.3f,%.3f) circle=(%.3f,%.3f,r=%.3f)\n",
            reinterpret_cast<void *>(anim),
            name,
            static_cast<double>(point.x), static_cast<double>(point.y),
            static_cast<double>(anchor.x), static_cast<double>(anchor.y), static_cast<double>(angle),
            static_cast<double>(rect.center.x), static_cast<double>(rect.center.y),
            static_cast<double>(rect.extents.x), static_cast<double>(rect.extents.y),
            static_cast<double>(circle.center.x), static_cast<double>(circle.center.y),
            static_cast<double>(circle.radius));
        g_mina->Log(message);
    }
}

void xmark_find_best_game_anim(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes,
    ycComponent **best_anim,
    float *best_area,
    unsigned int *best_depth) {
    if (!g_mina || !entity || !nodes || !best_anim || !best_area || !best_depth ||
        depth > max_depth || *nodes >= max_nodes) {
        return;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    const size_t cap = std::min<size_t>(child_count, 128);
    if (!cap) {
        return;
    }
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * cap));
    if (!children) {
        return;
    }
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, cap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }
    const size_t limit = std::min(read_count, cap);
    for (size_t i = 0; i < limit && *nodes < max_nodes; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        bool is_entity = false;
        bool is_game_anim = false;
        __try {
            const MM_Rtti type = g_mina->ComponentGetType(component);
            is_entity = rtti_equal(type, g_official_entity_rtti) ||
                g_mina->ComponentIsa(component, g_official_entity_rtti);
            is_game_anim = rtti_equal(type, g_game_anim_rtti) ||
                g_mina->ComponentIsa(component, g_game_anim_rtti);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            is_entity = false;
            is_game_anim = false;
        }
        if (is_entity) {
            xmark_find_best_game_anim(
                reinterpret_cast<ycEntity *>(component), depth + 1u, max_depth,
                nodes, max_nodes, best_anim, best_area, best_depth);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }
        float half_w = 0.0f;
        float half_h = 0.0f;
        if (xmark_game_anim_visual_bounds(component, nullptr, &half_w, &half_h)) {
            const float area = half_w * half_h;
            if (!*best_anim || depth < *best_depth ||
                (depth == *best_depth && area > *best_area)) {
                *best_area = area;
                *best_depth = depth;
                *best_anim = component;
            }
        }
    }
    g_mina->Free(children);
}

bool official_enemy_refresh_anim_bounds(XMarkOfficialEnemyHost &host) {
    if (!xmark_anim_bounds_api_available()) {
        return false;
    }
    ycComponent *anim = host.game_anim_weak
        ? static_cast<ycComponent *>(xmark_weak_ptr_get(host.game_anim_weak))
        : reinterpret_cast<ycComponent *>(host.game_anim);
    if (!anim) {
        uintptr_t entity_ptr = host.entity_weak
            ? reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(host.entity_weak))
            : host.entity;
        if (!entity_ptr) {
            return false;
        }
        unsigned int nodes = 0;
        float best_area = 0.0f;
        unsigned int best_depth = 0xFFFFFFFFu;
        xmark_find_best_game_anim(
            reinterpret_cast<ycEntity *>(entity_ptr), 0, 4, &nodes, 128,
            &anim, &best_area, &best_depth);
        if (!anim) {
            return false;
        }
        xmark_weak_ptr_destroy(host.game_anim_weak);
        host.game_anim = reinterpret_cast<uintptr_t>(anim);
        host.game_anim_weak = xmark_weak_ptr_create(anim);
        xmark_probe_anim_properties(anim);
    }

    Vec3 center{};
    float half_w = 0.0f;
    float half_h = 0.0f;
    if (!xmark_game_anim_visual_bounds(anim, &center, &half_w, &half_h)) {
        return false;
    }
    host.game_anim = reinterpret_cast<uintptr_t>(anim);
    uintptr_t entity_ptr = host.entity_weak
        ? reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(host.entity_weak))
        : host.entity;
    if (entity_ptr) {
        __try {
            const MM_Transform entity_transform = g_mina->EntityGetWorldTransform(
                reinterpret_cast<ycEntity *>(entity_ptr));
            host.entity_position = Vec3{
                entity_transform.t.x,
                entity_transform.t.y,
                entity_transform.t.z,
            };
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    const Vec3 offset{
        center.x - host.entity_position.x,
        center.y - host.entity_position.y,
        center.z - host.entity_position.z,
    };
    const float max_offset_x = std::max(
        0.0f,
        env_float("MINA_XMARK_OFFICIAL_BODY_CENTER_MAX_OFFSET_X", 4.0f));
    const float max_offset_y = std::max(
        0.0f,
        env_float("MINA_XMARK_OFFICIAL_BODY_CENTER_MAX_OFFSET_Y", 6.0f));
    const bool live_center_valid =
        std::isfinite(offset.x) && std::isfinite(offset.y) &&
        std::fabs(offset.x) <= max_offset_x &&
        std::fabs(offset.y) <= max_offset_y;
    if (live_center_valid) {
        host.body_center_offset = offset;
        host.body_center_offset_valid = true;
        host.position = center;
    } else if (host.body_center_offset_valid) {
        host.position = Vec3{
            host.entity_position.x + host.body_center_offset.x,
            host.entity_position.y + host.body_center_offset.y,
            host.entity_position.z + host.body_center_offset.z,
        };
    } else {
        host.position = host.entity_position;
    }
    host.visual_half_w = half_w;
    host.visual_half_h = half_h;
    host.visual_bounds_valid = true;
    return true;
}

void xmark_enemy_status_release(XMarkEnemyStatusRecord &status) {
    xmark_weak_ptr_destroy(status.entity_weak);
    xmark_weak_ptr_destroy(status.combat_core_weak);
    if (status.marked_original_palette &&
        g_mina &&
        api_function_field_ready(offsetof(MinaModAPI, ReleasePalette))) {
        __try {
            g_mina->ReleasePalette(status.marked_original_palette);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    status = XMarkEnemyStatusRecord{};
}

XMarkEnemyStatusRecord *xmark_enemy_status_by_id(unsigned int status_id) {
    if (!status_id || status_id > _countof(g_xmark_enemy_status)) {
        return nullptr;
    }
    XMarkEnemyStatusRecord &status = g_xmark_enemy_status[status_id - 1];
    return status.active ? &status : nullptr;
}

XMarkEnemyStatusRecord *xmark_enemy_status_find(uintptr_t entity, uintptr_t combat_core) {
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        if (status.active &&
            ((combat_core && status.combat_core == combat_core) ||
             (entity && status.entity == entity))) {
            return &status;
        }
    }
    return nullptr;
}

XMarkEnemyStatusRecord *xmark_enemy_status_bind(
    uintptr_t entity,
    uintptr_t combat_core,
    XMarkEnemyStatusPhase phase,
    unsigned long long now_ms,
    unsigned long long expires_ms) {
    if (!entity && !combat_core) {
        return nullptr;
    }
    XMarkEnemyStatusRecord *slot = nullptr;
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        if (status.active &&
            ((combat_core && status.combat_core == combat_core) ||
             (entity && status.entity == entity))) {
            slot = &status;
            break;
        }
        if (!status.active && !slot) {
            slot = &status;
        }
    }
    if (!slot) {
        slot = &g_xmark_enemy_status[0];
        xmark_enemy_status_release(*slot);
    }
    if (!slot->active || slot->entity != entity || slot->combat_core != combat_core) {
        xmark_enemy_status_release(*slot);
        slot->active = true;
        slot->entity = entity;
        slot->combat_core = combat_core;
        slot->entity_weak = xmark_weak_ptr_create(reinterpret_cast<void *>(entity));
        slot->combat_core_weak = xmark_weak_ptr_create(reinterpret_cast<void *>(combat_core));
        XMarkOfficialEnemyHost host{};
        if (combat_core && official_enemy_host_by_combat_core(combat_core, &host)) {
            slot->spawn_point = host.spawn_point;
            slot->health = host.health;
            slot->health_max = host.health_max;
        }
    }
    slot->phase = phase;
    slot->state_started_ms = now_ms;
    slot->state_expires_ms = expires_ms;
    slot->last_lifecycle_ms = now_ms;
    return slot;
}

unsigned int xmark_enemy_status_id(const XMarkEnemyStatusRecord *status) {
    return status
        ? static_cast<unsigned int>(status - g_xmark_enemy_status) + 1u
        : 0u;
}

void log_api_field_pointer(const char *name, size_t field_offset) {
    if (!g_mina) {
        return;
    }
    uintptr_t ptr = 0;
    const bool readable = safe_read_ptr(reinterpret_cast<uintptr_t>(g_mina) + field_offset, &ptr);
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn API field %s readable=%u ptr=0x%p inMainModule=%u executable=%u\n",
        name,
        readable ? 1u : 0u,
        reinterpret_cast<void *>(ptr),
        pointer_in_main_module(ptr) ? 1u : 0u,
        pointer_is_executable(ptr) ? 1u : 0u);
    g_mina->Log(message);
    if (readable && pointer_in_main_module(ptr)) {
        log_api_pointer(name, reinterpret_cast<const void *>(ptr));
    }
}

void log_api_surface() {
    if (!g_mina) {
        return;
    }
    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn native probe init: apiVersion=%llu gameRevision=%u enemyPlacements=%u controls=[G nearestEnemyXMark, T consumeXMarkBurn, Y animEffectEmitter, U marker, H renderF0029, J integratedF0029]\n",
        static_cast<unsigned long long>(g_mina->APIVersion),
        g_mina->GetGameRevision ? g_mina->GetGameRevision() : 0u,
        kXMarkEnemyPlacementCount);
    g_mina->Log(message);

    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn API capability: render=%u componentEntity=%u healthWrite=%u debugDraw=%u spawnEntity2=%u gameAnim=%u updateQueue=%u hitQueue=%u playerWorld=%u spawnRegistry=%u weakPtr=%u\n",
        xmark_render_api_available() ? 1u : 0u,
        xmark_component_api_available() ? 1u : 0u,
        xmark_combat_core_health_write_api_available() ? 1u : 0u,
        xmark_marker_debug_draw_api_available() ? 1u : 0u,
        api_function_field_ready(offsetof(MinaModAPI, SpawnEntity2)) ? 1u : 0u,
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqNameNoDir)) ? 1u : 0u,
        xmark_update_queue_api_available() ? 1u : 0u,
        xmark_entity_hit_update_queue_api_available() ? 1u : 0u,
        xmark_player_world_api_available() ? 1u : 0u,
        xmark_spawn_point_registry_api_available() ? 1u : 0u,
        xmark_weak_ptr_api_available() ? 1u : 0u);
    g_mina->Log(message);

    log_api_pointer("SpawnEntity", reinterpret_cast<const void *>(g_mina->SpawnEntity));
    log_api_pointer("PlayerGetPos", reinterpret_cast<const void *>(g_mina->PlayerGetPos));
    log_api_pointer("GetRoomIndex", reinterpret_cast<const void *>(g_mina->GetRoomIndex));
    log_api_pointer("GetRoomTime", reinterpret_cast<const void *>(g_mina->GetRoomTime));
    log_api_field_pointer("CreateTexture", offsetof(MinaModAPI, CreateTexture));
    log_api_field_pointer("CreateVertexBuffer", offsetof(MinaModAPI, CreateVertexBuffer));
    log_api_field_pointer("GetRenderPass", offsetof(MinaModAPI, GetRenderPass));
    log_api_field_pointer("CreateRenderObject", offsetof(MinaModAPI, CreateRenderObject));
    log_api_field_pointer("RenderCmdDrawIndexed", offsetof(MinaModAPI, RenderCmdDrawIndexed));
    log_api_field_pointer("GetDebugDraw", offsetof(MinaModAPI, GetDebugDraw));
    log_api_field_pointer("DebugDrawTexturedQuad", offsetof(MinaModAPI, DebugDrawTexturedQuad));
    log_api_field_pointer("WorldGetGameRootEntity", offsetof(MinaModAPI, WorldGetGameRootEntity));
    log_api_field_pointer("EntityGetChildren", offsetof(MinaModAPI, EntityGetChildren));
    log_api_field_pointer("EntityGetMainComponent", offsetof(MinaModAPI, EntityGetMainComponent));
    log_api_field_pointer("EntityGetWorldTransform", offsetof(MinaModAPI, EntityGetWorldTransform));
    log_api_field_pointer("CombatCoreGetHealth", offsetof(MinaModAPI, CombatCoreGetHealth));
    log_api_field_pointer("CombatCoreSetHealth", offsetof(MinaModAPI, CombatCoreSetHealth));
    log_api_field_pointer("GameAnimGetSeqNameNoDir", offsetof(MinaModAPI, GameAnimGetSeqNameNoDir));
    log_api_field_pointer("GameAnimGetCurrentFrameTime", offsetof(MinaModAPI, GameAnimGetCurrentFrameTime));
    log_api_field_pointer("GameAnimGetNumLoopsPlayed", offsetof(MinaModAPI, GameAnimGetNumLoopsPlayed));
    log_api_field_pointer("GameAnimIsVisible", offsetof(MinaModAPI, GameAnimIsVisible));
    log_api_field_pointer("GameAnimGetNumSeqFrames", offsetof(MinaModAPI, GameAnimGetNumSeqFrames));
    log_api_field_pointer("GameAnimGetPlayRate", offsetof(MinaModAPI, GameAnimGetPlayRate));
    log_api_field_pointer("GameAnimGetPropertyExists", offsetof(MinaModAPI, GameAnimGetPropertyExists));
    log_api_field_pointer("GameAnimGetPropertyPoint", offsetof(MinaModAPI, GameAnimGetPropertyPoint));
    log_api_field_pointer("GameAnimGetPropertyAnchor", offsetof(MinaModAPI, GameAnimGetPropertyAnchor));
    log_api_field_pointer("GameAnimGetPropertyRect", offsetof(MinaModAPI, GameAnimGetPropertyRect));
    log_api_field_pointer("GameAnimGetPropertyCircle", offsetof(MinaModAPI, GameAnimGetPropertyCircle));
    log_api_field_pointer("GameAnimGetWorldTransform", offsetof(MinaModAPI, GameAnimGetWorldTransform));
    log_api_field_pointer("GameAnimGetCurrentFrameBound", offsetof(MinaModAPI, GameAnimGetCurrentFrameBound));
    log_api_field_pointer("GameAnimGetPalette", offsetof(MinaModAPI, GameAnimGetPalette));
    log_api_field_pointer("GameAnimSetPalette", offsetof(MinaModAPI, GameAnimSetPalette));
    log_api_field_pointer("ClonePalette", offsetof(MinaModAPI, ClonePalette));
    log_api_field_pointer("PaletteWriteIndex", offsetof(MinaModAPI, PaletteWriteIndex));
    log_api_field_pointer("WorldGetElapsedTime", offsetof(MinaModAPI, WorldGetElapsedTime));
    log_api_field_pointer("PlayerGetWorld", offsetof(MinaModAPI, PlayerGetWorld));
    log_api_field_pointer("WorldGetEntityHitUpdateQueue", offsetof(MinaModAPI, WorldGetEntityHitUpdateQueue));
    log_api_field_pointer("WorldGetEntityPostArtUpdateQueue", offsetof(MinaModAPI, WorldGetEntityPostArtUpdateQueue));
    log_api_field_pointer("WorldGetHUDUpdateQueue", offsetof(MinaModAPI, WorldGetHUDUpdateQueue));
    log_api_field_pointer("GameComponentGetSpawnPoint", offsetof(MinaModAPI, GameComponentGetSpawnPoint));
    log_api_field_pointer("SpawnPointGetSpawnedEntity", offsetof(MinaModAPI, SpawnPointGetSpawnedEntity));
    log_api_field_pointer("SpawnPointGetNameLevelHash", offsetof(MinaModAPI, SpawnPointGetNameLevelHash));
    log_api_field_pointer("UpdateQueueAdd", offsetof(MinaModAPI, UpdateQueueAdd));
    log_api_field_pointer("CreateWeakPtr", offsetof(MinaModAPI, CreateWeakPtr));
}

void official_enemy_clear_snapshot() {
    for (XMarkOfficialEnemyHost &host : g_official_enemy_hosts) {
        if (host.entity_weak) {
            xmark_weak_ptr_destroy(host.entity_weak);
        }
        if (host.combat_core_weak) {
            xmark_weak_ptr_destroy(host.combat_core_weak);
        }
        if (host.game_anim_weak) {
            xmark_weak_ptr_destroy(host.game_anim_weak);
        }
        if (host.spawned_component_weak) {
            xmark_weak_ptr_destroy(host.spawned_component_weak);
        }
    }
    g_official_enemy_host_count = 0;
    g_official_enemy_scan_nodes = 0;
    g_official_enemy_snapshot_valid = false;
    g_last_official_enemy_registry_reconcile_ms = 0;
    for (XMarkOfficialEnemyHost &host : g_official_enemy_hosts) {
        host = {};
    }
}

bool official_enemy_resolve_weak_refs(const XMarkOfficialEnemyHost &host, uintptr_t *entity_out, uintptr_t *combat_core_out) {
    uintptr_t entity = host.entity;
    uintptr_t combat_core = host.combat_core;
    if (host.entity_weak) {
        entity = reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(host.entity_weak));
    }
    if (host.combat_core_weak) {
        combat_core = reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(host.combat_core_weak));
    }
    if (entity_out) {
        *entity_out = entity;
    }
    if (combat_core_out) {
        *combat_core_out = combat_core;
    }
    return entity && combat_core;
}

XMarkOfficialEnemyHost official_enemy_host_with_resolved_refs(const XMarkOfficialEnemyHost &host) {
    XMarkOfficialEnemyHost resolved = host;
    official_enemy_resolve_weak_refs(host, &resolved.entity, &resolved.combat_core);
    resolved.entity_weak = nullptr;
    resolved.combat_core_weak = nullptr;
    resolved.spawned_component_weak = nullptr;
    return resolved;
}

void official_enemy_capture_spawn_identity(XMarkOfficialEnemyHost &host, ycEntity *entity) {
    if (!entity || !xmark_spawn_point_room_registry_enabled()) {
        return;
    }
    SpawnPoint *spawn_point = nullptr;
    GameComponent *spawned_component = nullptr;
    __try {
        if (host.combat_core) {
            spawn_point = g_mina->GameComponentGetSpawnPoint(
                reinterpret_cast<GameComponent *>(host.combat_core));
        }
        ycComponent *main_component = g_mina->EntityGetMainComponent(entity);
        if (!spawn_point &&
            main_component &&
            rtti_valid(g_official_game_component_rtti) &&
            g_mina->ComponentIsa(main_component, g_official_game_component_rtti)) {
            spawn_point = g_mina->GameComponentGetSpawnPoint(
                reinterpret_cast<GameComponent *>(main_component));
        }
        if (!spawn_point) {
            const size_t child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
            const size_t child_limit = std::min<size_t>(
                child_count,
                env_uint("MINA_XMARK_ROOM_REGISTRY_ENTITY_CHILD_CAP", 128));
            ycComponent **children = child_limit
                ? static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * child_limit))
                : nullptr;
            if (children) {
                const size_t read_count = g_mina->EntityGetChildren(entity, children, child_limit);
                const size_t read_limit = std::min(read_count, child_limit);
                for (size_t i = 0; i < read_limit && !spawn_point; ++i) {
                    ycComponent *component = children[i];
                    if (!component ||
                        !rtti_valid(g_official_game_component_rtti) ||
                        !g_mina->ComponentIsa(component, g_official_game_component_rtti)) {
                        continue;
                    }
                    spawn_point = g_mina->GameComponentGetSpawnPoint(
                        reinterpret_cast<GameComponent *>(component));
                }
                g_mina->Free(children);
            }
        }
        if (spawn_point) {
            spawned_component = g_mina->SpawnPointGetSpawnedEntity(spawn_point);
            host.spawn_name_hash = g_mina->SpawnPointGetNameHash(spawn_point);
            host.spawn_name_level_hash = g_mina->SpawnPointGetNameLevelHash(spawn_point);
            host.spawn_layer_hash = g_mina->SpawnPointGetLayerNameHash(spawn_point);
            host.spawn_entity_type = g_mina->SpawnPointGetEntityType(spawn_point);
            host.spawn_type = g_mina->SpawnPointGetSpawnType(spawn_point);
            host.spawn_tile_layer = g_mina->SpawnPointGetTileLayerIndex(spawn_point);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spawn_point = nullptr;
        spawned_component = nullptr;
    }
    if (!spawn_point) {
        return;
    }
    host.spawn_point = reinterpret_cast<uintptr_t>(spawn_point);
    host.spawned_component = reinterpret_cast<uintptr_t>(spawned_component);
    host.spawned_component_weak = xmark_weak_ptr_create(spawned_component);
    host.registry_generation = g_official_enemy_registry_generation;
    host.spawn_identity_valid = true;
}

ycComponent *official_enemy_find_health_core_for_entity(
    ycEntity *entity,
    float preferred_health_max) {
    if (!entity || !g_mina || !official_enemy_init_rtti()) {
        return nullptr;
    }
    const size_t child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    if (child_count == 0) {
        return nullptr;
    }
    const size_t limit = std::min<size_t>(
        child_count,
        env_uint("MINA_XMARK_ROOM_REGISTRY_ENTITY_CHILD_CAP", 128));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * limit));
    if (!children) {
        return nullptr;
    }
    ycComponent *best = nullptr;
    float best_delta = FLT_MAX;
    const size_t read_count = g_mina->EntityGetChildren(entity, children, limit);
    const size_t read_limit = std::min(read_count, limit);
    for (size_t i = 0; i < read_limit; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        const MM_Rtti component_type = g_mina->ComponentGetType(component);
        const bool is_combat_core =
            rtti_equal(component_type, g_official_combat_core_rtti) ||
            (rtti_valid(g_official_combat_core_rtti) && g_mina->ComponentIsa &&
                g_mina->ComponentIsa(component, g_official_combat_core_rtti));
        if (!is_combat_core) {
            continue;
        }
        const float health_max = g_mina->CombatCoreGetHealthMax(component);
        if (!(health_max > 0.0f) || !std::isfinite(health_max)) {
            continue;
        }
        const float delta = preferred_health_max > 0.0f
            ? std::fabs(health_max - preferred_health_max)
            : 0.0f;
        if (!best || delta < best_delta) {
            best = component;
            best_delta = delta;
        }
    }
    g_mina->Free(children);
    return best;
}

bool official_enemy_rebind_from_spawn_point(
    XMarkOfficialEnemyHost &host,
    unsigned long long now_ms) {
    if (!host.spawn_identity_valid || !host.spawn_point ||
        !xmark_spawn_point_room_registry_enabled()) {
        return false;
    }
    SpawnPoint *spawn_point = reinterpret_cast<SpawnPoint *>(host.spawn_point);
    GameComponent *spawned_component = nullptr;
    ycEntity *entity = nullptr;
    ycComponent *combat_core = nullptr;
    float health = 0.0f;
    float health_max = 0.0f;
    MM_Transform transform{};
    bool spawned = false;
    __try {
        if (g_mina->SpawnPointIsEntityKilled(spawn_point) ||
            !g_mina->SpawnPointIsEntitySpawned(spawn_point)) {
            return false;
        }
        spawned_component = g_mina->SpawnPointGetSpawnedEntity(spawn_point);
        if (!spawned_component && host.spawned_component_weak) {
            spawned_component = static_cast<GameComponent *>(
                xmark_weak_ptr_get(host.spawned_component_weak));
        }
        if (!spawned_component) {
            return false;
        }
        entity = g_mina->ComponentGetParent(reinterpret_cast<ycComponent *>(spawned_component));
        if (!entity) {
            return false;
        }
        combat_core = official_enemy_find_health_core_for_entity(entity, host.health_max);
        if (!combat_core) {
            return false;
        }
        health = g_mina->CombatCoreGetHealth(combat_core);
        health_max = g_mina->CombatCoreGetHealthMax(combat_core);
        transform = g_mina->EntityGetWorldTransform(entity);
        spawned = health > 0.0f && health_max > 0.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        spawned = false;
    }
    if (!spawned) {
        return false;
    }

    xmark_weak_ptr_destroy(host.entity_weak);
    xmark_weak_ptr_destroy(host.combat_core_weak);
    xmark_weak_ptr_destroy(host.game_anim_weak);
    xmark_weak_ptr_destroy(host.spawned_component_weak);
    host.entity = reinterpret_cast<uintptr_t>(entity);
    host.combat_core = reinterpret_cast<uintptr_t>(combat_core);
    host.spawned_component = reinterpret_cast<uintptr_t>(spawned_component);
    host.entity_weak = xmark_weak_ptr_create(entity);
    host.combat_core_weak = xmark_weak_ptr_create(combat_core);
    host.game_anim = 0;
    host.game_anim_weak = nullptr;
    host.visual_bounds_valid = false;
    host.spawned_component_weak = xmark_weak_ptr_create(spawned_component);
    host.entity_position = Vec3{transform.t.x, transform.t.y, transform.t.z};
    host.position = host.entity_position;
    official_enemy_refresh_anim_bounds(host);
    host.health = health;
    host.health_max = health_max;
    host.last_seen_ms = now_ms;
    host.active = true;
    ++g_official_enemy_registry_targeted_refresh_count;
    return true;
}

void request_official_enemy_registry_rescan(unsigned long long now_ms) {
    const unsigned long long requested_ms =
        now_ms + env_uint("MINA_XMARK_ROOM_REGISTRY_INVALIDATION_RESCAN_DELAY_MS", 250);
    if (!g_official_enemy_registry_rescan_requested_ms ||
        requested_ms < g_official_enemy_registry_rescan_requested_ms) {
        g_official_enemy_registry_rescan_requested_ms = requested_ms;
    }
}

void reconcile_official_enemy_room_registry(unsigned long long now_ms, bool force = false) {
    if (!xmark_spawn_point_room_registry_enabled() ||
        !g_official_enemy_snapshot_valid ||
        g_official_enemy_host_count == 0) {
        return;
    }
    const unsigned int interval_ms =
        std::max(16u, env_uint("MINA_XMARK_ROOM_REGISTRY_RECONCILE_MS", 250));
    if (!force && g_last_official_enemy_registry_reconcile_ms &&
        now_ms < g_last_official_enemy_registry_reconcile_ms + interval_ms) {
        return;
    }
    g_last_official_enemy_registry_reconcile_ms = now_ms;
    ++g_official_enemy_registry_reconcile_count;

    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        uintptr_t resolved_entity = 0;
        uintptr_t resolved_core = 0;
        if (official_enemy_resolve_weak_refs(host, &resolved_entity, &resolved_core)) {
            host.entity = resolved_entity;
            host.combat_core = resolved_core;
            __try {
                const MM_Transform transform = g_mina->EntityGetWorldTransform(
                    reinterpret_cast<ycEntity *>(resolved_entity));
                host.entity_position = Vec3{transform.t.x, transform.t.y, transform.t.z};
                host.position = host.entity_position;
                if (!host.body_center_offset_valid) {
                    official_enemy_refresh_anim_bounds(host);
                }
                const float health = g_mina->CombatCoreGetHealth(
                    reinterpret_cast<ycComponent *>(resolved_core));
                const float health_max = g_mina->CombatCoreGetHealthMax(
                    reinterpret_cast<ycComponent *>(resolved_core));
                if (std::isfinite(health)) {
                    host.health = health;
                }
                if (std::isfinite(health_max) && health_max > 0.0f) {
                    host.health_max = health_max;
                }
                host.last_seen_ms = now_ms;
                if (!std::isfinite(health) || health <= 0.0f) {
                    host.active = false;
                    ++g_official_enemy_registry_invalid_count;
                    if (!host.spawn_identity_valid || !host.spawn_point) {
                        request_official_enemy_registry_rescan(now_ms);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            continue;
        }
        if (host.active) {
            host.active = false;
            ++g_official_enemy_registry_invalid_count;
            if (!host.spawn_identity_valid || !host.spawn_point) {
                request_official_enemy_registry_rescan(now_ms);
            }
        }
        official_enemy_rebind_from_spawn_point(host, now_ms);
    }
}

void update_official_enemy_spawn_lifecycle_batch(unsigned long long now_ms) {
    if (!xmark_spawn_point_room_registry_enabled() ||
        !g_official_enemy_snapshot_valid ||
        g_official_enemy_host_count == 0) {
        return;
    }
    const unsigned int interval_ms =
        std::max(16u, env_uint("MINA_XMARK_ROOM_REGISTRY_LIFECYCLE_INTERVAL_MS", 100));
    if (g_last_official_enemy_lifecycle_batch_ms &&
        now_ms < g_last_official_enemy_lifecycle_batch_ms + interval_ms) {
        return;
    }
    g_last_official_enemy_lifecycle_batch_ms = now_ms;
    const unsigned int batch = std::min<unsigned int>(
        g_official_enemy_host_count,
        std::max(1u, env_uint("MINA_XMARK_ROOM_REGISTRY_LIFECYCLE_BATCH", 12)));
    for (unsigned int step = 0; step < batch; ++step) {
        const unsigned int index = g_official_enemy_lifecycle_cursor++ % g_official_enemy_host_count;
        XMarkOfficialEnemyHost &host = g_official_enemy_hosts[index];
        uintptr_t entity = 0;
        uintptr_t core = 0;
        if (!official_enemy_resolve_weak_refs(host, &entity, &core)) {
            host.active = false;
            official_enemy_rebind_from_spawn_point(host, now_ms);
            continue;
        }
        host.entity = entity;
        host.combat_core = core;
        __try {
            const float health = g_mina->CombatCoreGetHealth(
                reinterpret_cast<ycComponent *>(core));
            const MM_Transform transform = g_mina->EntityGetWorldTransform(
                reinterpret_cast<ycEntity *>(entity));
            host.entity_position = Vec3{transform.t.x, transform.t.y, transform.t.z};
            host.position = host.entity_position;
            if (!host.body_center_offset_valid) {
                official_enemy_refresh_anim_bounds(host);
            }
            host.health = health;
            host.last_seen_ms = now_ms;
            host.active = std::isfinite(health) && health > 0.0f;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            host.active = false;
        }
    }
}

bool official_enemy_init_rtti() {
    if (g_official_rtti_initialized) {
        return rtti_valid(g_official_entity_rtti) && rtti_valid(g_official_combat_core_rtti);
    }
    g_official_rtti_initialized = true;
    if (!g_mina || !g_mina->Hash64) {
        return false;
    }
    __try {
        g_official_entity_rtti = rtti_from_hash(g_mina->Hash64("ycEntity", 8));
        g_official_combat_core_rtti = rtti_from_hash(g_mina->Hash64("CombatCore", 10));
        g_official_game_component_rtti = rtti_from_hash(g_mina->Hash64("GameComponent", 13));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_official_entity_rtti = {};
        g_official_combat_core_rtti = {};
        g_official_game_component_rtti = {};
        return false;
    }
    return rtti_valid(g_official_entity_rtti) && rtti_valid(g_official_combat_core_rtti);
}

void copy_string_ref(char *dest, size_t dest_size, MM_StringRef ref) {
    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = 0;
    if (!ref.str || ref.len == 0) {
        return;
    }
    const size_t count = std::min(dest_size - 1, ref.len);
    std::memcpy(dest, ref.str, count);
    dest[count] = 0;
}

bool official_enemy_seen(uintptr_t combat_core) {
    if (!combat_core) {
        return true;
    }
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        if (g_official_enemy_hosts[i].combat_core == combat_core) {
            return true;
        }
    }
    return false;
}

bool xmark_ascii_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) {
        return false;
    }
    const size_t needle_len = std::strlen(needle);
    for (const char *cursor = haystack; *cursor; ++cursor) {
        if (_strnicmp(cursor, needle, needle_len) == 0) {
            return true;
        }
    }
    return false;
}

bool xmark_rejected_non_enemy_component_type(const char *type_name) {
    if (!type_name || !type_name[0] ||
        !env_bool("MINA_XMARK_REJECT_NON_ENEMY_COMBAT_CORES", true)) {
        return false;
    }
    if (env_token_list_contains("MINA_XMARK_REJECT_COMPONENT_TYPES", type_name)) {
        return true;
    }

    static const char *kDefaultRejectedTypes[] = {
        "Breakable",
        "HittableAnimTile",
        "Pickup",
    };
    for (const char *rejected_type : kDefaultRejectedTypes) {
        if (_stricmp(type_name, rejected_type) == 0 ||
            xmark_ascii_contains_ci(type_name, rejected_type)) {
            return true;
        }
    }
    return false;
}

bool xmark_known_enemy_component_type(const char *type_name) {
    if (!type_name || !type_name[0]) {
        return false;
    }
    if (env_token_list_contains("MINA_XMARK_KNOWN_ENEMY_COMPONENT_TYPES", type_name)) {
        return true;
    }

    static const char *kDefaultKnownEnemyTypes[] = {
        "Enemy",
        "Gremlin",
        "Trooper",
        "Sentry",
        "BadRat",
        "Gooper",
        "Boss",
        "TrainingDummy",
        "Dummy",
        "NPCBehavior_Muriel",
        "MurielNPC",
        "MurielRabbit",
    };
    for (const char *known_type : kDefaultKnownEnemyTypes) {
        if (_stricmp(type_name, known_type) == 0 ||
            xmark_ascii_contains_ci(type_name, known_type)) {
            return true;
        }
    }
    for (const char *known_type : kXMarkKnownEnemyComponentTokens) {
        if (_stricmp(type_name, known_type) == 0 ||
            xmark_ascii_contains_ci(type_name, known_type)) {
            return true;
        }
    }
    return false;
}

bool xmark_muriel_component_type(const char *type_name) {
    return type_name && type_name[0] &&
        xmark_ascii_contains_ci(type_name, "Muriel");
}

bool xmark_official_host_is_muriel(const XMarkOfficialEnemyHost &host) {
    return host.muriel_no_damage ||
        (host.spawn_identity_valid && host.spawn_entity_type == ENTITYTYPE_MURIEL_NPC) ||
        xmark_muriel_component_type(host.component_type);
}

bool xmark_scripted_boss_component_type(const char *type_name) {
    if (!type_name || !type_name[0]) {
        return false;
    }
    if (env_token_list_contains("MINA_XMARK_BURN_SCRIPTED_BOSS_COMPONENT_TYPES", type_name)) {
        return true;
    }

    static const char *kDefaultScriptedBossTypes[] = {
        "HulkTrooperEnemy",
        "GigaThorneEnemy",
    };
    for (const char *boss_type : kDefaultScriptedBossTypes) {
        if (_stricmp(type_name, boss_type) == 0) {
            return true;
        }
    }
    return xmark_ascii_contains_ci(type_name, "Boss") ||
        xmark_ascii_contains_ci(type_name, "GigaThorne");
}

bool xmark_burn_target_is_scripted_boss(
    const XMarkBurnEffect &burn,
    const XMarkOfficialEnemyHost *resolved_host,
    char *matched_type,
    size_t matched_type_size) {
    if (matched_type && matched_type_size > 0) {
        matched_type[0] = 0;
    }
    if (!env_bool("MINA_XMARK_BURN_SCRIPTED_BOSS_LETHAL_GUARD", true)) {
        return false;
    }

    if (resolved_host && xmark_scripted_boss_component_type(resolved_host->component_type)) {
        if (matched_type && matched_type_size > 0) {
            std::snprintf(matched_type, matched_type_size, "%s", resolved_host->component_type);
        }
        return true;
    }

    const uintptr_t entity = resolved_host && resolved_host->entity
        ? resolved_host->entity
        : burn.target;
    if (entity && probable_heap_object(entity) &&
        g_mina && g_mina->EntityGetChildren && g_mina->ComponentGetTypeName &&
        g_mina->Alloc && g_mina->Free) {
        size_t child_count = 0;
        __try {
            child_count = g_mina->EntityGetChildren(
                reinterpret_cast<ycEntity *>(entity), nullptr, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            child_count = 0;
        }
        const size_t max_children = std::min<size_t>(
            child_count,
            env_uint("MINA_XMARK_BURN_SCRIPTED_BOSS_COMPONENT_CHILD_CAP", 96));
        if (max_children > 0) {
            ycComponent **children = static_cast<ycComponent **>(
                g_mina->Alloc(sizeof(ycComponent *) * max_children));
            if (children) {
                size_t read_count = 0;
                __try {
                    read_count = g_mina->EntityGetChildren(
                        reinterpret_cast<ycEntity *>(entity), children, max_children);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    read_count = 0;
                }
                const size_t limit = std::min(read_count, max_children);
                for (size_t i = 0; i < limit; ++i) {
                    char type_name[96]{};
                    if (children[i]) {
                        __try {
                            copy_string_ref(
                                type_name,
                                sizeof(type_name),
                                g_mina->ComponentGetTypeName(children[i]));
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            type_name[0] = 0;
                        }
                    }
                    if (!xmark_scripted_boss_component_type(type_name)) {
                        continue;
                    }
                    if (matched_type && matched_type_size > 0) {
                        std::snprintf(matched_type, matched_type_size, "%s", type_name);
                    }
                    g_mina->Free(children);
                    return true;
                }
                g_mina->Free(children);
            }
        }
    }

    const char *visual_tokens[] = {
        burn.visual_entry,
        burn.visual_stem,
        burn.visual_catalog,
    };
    for (const char *token : visual_tokens) {
        if (xmark_ascii_contains_ci(token, "boss") ||
            xmark_ascii_contains_ci(token, "gigaThorne") ||
            xmark_ascii_contains_ci(token, "hulkTrooper")) {
            if (matched_type && matched_type_size > 0) {
                std::snprintf(matched_type, matched_type_size, "visual:%s", token);
            }
            return true;
        }
    }
    return false;
}

bool xmark_entity_has_known_enemy_component(
    ycEntity *entity,
    char *matched_type,
    size_t matched_type_size);

bool xmark_visual_host_is_known_enemy(const XMarkVisualEnemyHost &host) {
    if (!host.active) {
        return false;
    }
    if (host.entry[0] && _strnicmp(host.entry, "enemies/", 8) == 0) {
        return true;
    }
    if (visual_enemy_host_matches_preferred_training(host)) {
        return true;
    }
    for (const XMarkEnemyPlacement &placement : kXMarkEnemyPlacements) {
        if ((host.catalog[0] && placement.catalog_name && _stricmp(host.catalog, placement.catalog_name) == 0) ||
            (host.entry[0] && placement.local_entry && _stricmp(host.entry, placement.local_entry) == 0) ||
            (host.stem[0] && placement.local_entry && xmark_ascii_contains_ci(placement.local_entry, host.stem))) {
            return true;
        }
    }
    return false;
}

bool xmark_official_host_matches_visual_enemy_whitelist(
    const XMarkOfficialEnemyHost &host,
    unsigned long long now_ms,
    XMarkVisualEnemyHost *host_out = nullptr) {
    if (host_out) {
        *host_out = XMarkVisualEnemyHost{};
    }
    if (!env_bool("MINA_XMARK_OFFICIAL_REQUIRE_VISUAL_ENEMY_WHITELIST", true)) {
        return true;
    }
    if (!host.active || !host.entity || !host.combat_core) {
        return false;
    }

    const Vec3 visual_space_position = render_position_to_visual_host_space(host.position);
    const float max_distance = std::max(
        0.1f,
            env_float(
                "MINA_XMARK_OFFICIAL_ENEMY_VISUAL_WHITELIST_RADIUS",
                env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_MAX_DISTANCE", 8.0f)));
    const float max_distance_sq = max_distance * max_distance;
    bool found = false;
    XMarkVisualEnemyHost best{};
    float best_score = FLT_MAX;
    if (read_enemy_visual_state_file(now_ms, false)) {
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            const XMarkVisualEnemyHost &visual_host = g_visual_enemy_hosts[i];
            if (!xmark_visual_host_is_known_enemy(visual_host)) {
                continue;
            }
            const float dx = visual_host.position.x - visual_space_position.x;
            const float dy = visual_host.position.y - visual_space_position.y;
            const float distance_sq = (dx * dx) + (dy * dy);
            if (distance_sq > max_distance_sq || distance_sq >= best_score) {
                continue;
            }
            found = true;
            best = visual_host;
            best.distance_sq = distance_sq;
            best_score = distance_sq;
        }
    }
    if (found && host_out) {
        *host_out = best;
    }
    if (found) {
        return true;
    }

    if (env_bool("MINA_XMARK_OFFICIAL_ALLOW_KNOWN_ENEMY_COMPONENT_FALLBACK", true) &&
        host.entity &&
        probable_heap_object(host.entity)) {
        char matched_type[96]{};
        if (xmark_entity_has_known_enemy_component(
                reinterpret_cast<ycEntity *>(host.entity),
                matched_type,
                sizeof(matched_type))) {
            if (g_mina && env_bool("MINA_XMARK_OFFICIAL_ENEMY_WHITELIST_LOG", false)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn official enemy whitelist component fallback entity=0x%p core=0x%p component=%s pos=(%.3f, %.3f, %.3f)\n",
                    reinterpret_cast<void *>(host.entity),
                    reinterpret_cast<void *>(host.combat_core),
                    matched_type[0] ? matched_type : "-",
                    static_cast<double>(host.position.x),
                    static_cast<double>(host.position.y),
                    static_cast<double>(host.position.z));
                g_mina->Log(message);
            }
            return true;
        }
    }
    return false;
}

bool xmark_entity_has_known_enemy_component(
    ycEntity *entity,
    char *matched_type,
    size_t matched_type_size) {
    if (matched_type && matched_type_size > 0) {
        matched_type[0] = 0;
    }
    if (!entity ||
        !g_mina ||
        !g_mina->EntityGetChildren ||
        !g_mina->ComponentGetTypeName ||
        !g_mina->Alloc ||
        !g_mina->Free) {
        return false;
    }

    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (child_count == 0) {
        return false;
    }

    const size_t max_children = std::min<size_t>(
        child_count,
        env_uint("MINA_XMARK_KNOWN_ENEMY_COMPONENT_CHILD_CAP", 96));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * max_children));
    if (!children) {
        return false;
    }

    bool found = false;
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, max_children);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }

    const size_t limit = std::min(read_count, max_children);
    for (size_t i = 0; i < limit; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        char type_name[96]{};
        __try {
            copy_string_ref(type_name, sizeof(type_name), g_mina->ComponentGetTypeName(component));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            type_name[0] = 0;
        }
        if (xmark_rejected_non_enemy_component_type(type_name)) {
            continue;
        }
        if (xmark_known_enemy_component_type(type_name)) {
            if (!found && matched_type && matched_type_size > 0) {
                std::snprintf(matched_type, matched_type_size, "%s", type_name);
            }
            found = true;
            if (xmark_muriel_component_type(type_name)) {
                if (matched_type && matched_type_size > 0) {
                    std::snprintf(matched_type, matched_type_size, "%s", type_name);
                }
                break;
            }
        }
    }
    g_mina->Free(children);
    return found;
}

bool xmark_entity_has_rejected_non_enemy_component(
    ycEntity *entity,
    char *matched_type,
    size_t matched_type_size) {
    if (matched_type && matched_type_size > 0) {
        matched_type[0] = 0;
    }
    if (!entity ||
        !env_bool("MINA_XMARK_REJECT_NON_ENEMY_COMBAT_CORES", true) ||
        !g_mina ||
        !g_mina->EntityGetChildren ||
        !g_mina->ComponentGetTypeName ||
        !g_mina->Alloc ||
        !g_mina->Free) {
        return false;
    }

    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (child_count == 0) {
        return false;
    }

    const size_t max_children = std::min<size_t>(
        child_count,
        env_uint("MINA_XMARK_REJECT_COMPONENT_CHILD_CAP", 96));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * max_children));
    if (!children) {
        return false;
    }

    bool rejected = false;
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, max_children);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }

    const size_t limit = std::min(read_count, max_children);
    for (size_t i = 0; i < limit; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        char type_name[96]{};
        __try {
            copy_string_ref(type_name, sizeof(type_name), g_mina->ComponentGetTypeName(component));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            type_name[0] = 0;
        }
        if (xmark_rejected_non_enemy_component_type(type_name)) {
            if (matched_type && matched_type_size > 0) {
                std::snprintf(matched_type, matched_type_size, "%s", type_name);
            }
            rejected = true;
            break;
        }
    }
    g_mina->Free(children);
    return rejected;
}

bool xmark_official_host_rejected_for_xmark(
    const XMarkOfficialEnemyHost &host,
    char *matched_type,
    size_t matched_type_size) {
    if (matched_type && matched_type_size > 0) {
        matched_type[0] = 0;
    }
    if (xmark_rejected_non_enemy_component_type(host.component_type)) {
        if (matched_type && matched_type_size > 0) {
            std::snprintf(matched_type, matched_type_size, "%s", host.component_type);
        }
        return true;
    }
    if (!host.entity || !probable_heap_object(host.entity)) {
        return false;
    }
    return xmark_entity_has_rejected_non_enemy_component(
        reinterpret_cast<ycEntity *>(host.entity),
        matched_type,
        matched_type_size);
}

bool xmark_official_host_allowed_for_xmark(
    const XMarkOfficialEnemyHost &host,
    unsigned long long now_ms,
    char *reject_reason,
    size_t reject_reason_size) {
    if (reject_reason && reject_reason_size > 0) {
        reject_reason[0] = 0;
    }
    const bool require_known_enemy =
        env_bool("MINA_XMARK_OFFICIAL_REQUIRE_KNOWN_ENEMY_COMPONENT", false);
    if (require_known_enemy) {
        char enemy_type[96]{};
        if (!host.entity ||
            !probable_heap_object(host.entity) ||
            !xmark_entity_has_known_enemy_component(
                reinterpret_cast<ycEntity *>(host.entity),
                enemy_type,
                sizeof(enemy_type))) {
            if (reject_reason && reject_reason_size > 0) {
                std::snprintf(reject_reason, reject_reason_size, "not-known-enemy-component");
            }
            return false;
        }
    } else {
        char rejected_type[96]{};
        if (xmark_official_host_rejected_for_xmark(host, rejected_type, sizeof(rejected_type))) {
            if (reject_reason && reject_reason_size > 0) {
                std::snprintf(
                    reject_reason,
                    reject_reason_size,
                    "component:%s",
                    rejected_type[0] ? rejected_type : "-");
            }
            return false;
        }
    }
    if (!xmark_official_host_matches_visual_enemy_whitelist(host, now_ms)) {
        if (reject_reason && reject_reason_size > 0) {
            std::snprintf(reject_reason, reject_reason_size, "visual-whitelist");
        }
        return false;
    }
    return true;
}

bool xmark_burn_effect_active_for_entity_or_core(uintptr_t entity, uintptr_t combat_core) {
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active) {
            continue;
        }
        if ((combat_core && burn.official_combat_core == combat_core) ||
            (entity && burn.target == entity)) {
            return true;
        }
    }
    return false;
}

bool xmark_normal_death_probe_recently_logged(
    uintptr_t entity,
    uintptr_t combat_core,
    unsigned long long now_ms) {
    const unsigned int dedupe_ms = env_uint("MINA_XMARK_NORMAL_DEATH_PROBE_DEDUPE_MS", 6000);
    for (const XMarkNormalDeathProbeLog &log : g_normal_death_probe_logs) {
        if (!log.logged_ms) {
            continue;
        }
        const bool matched =
            (combat_core && log.combat_core == combat_core) ||
            (entity && log.entity == entity);
        if (!matched) {
            continue;
        }
        if (!dedupe_ms || now_ms < log.logged_ms || now_ms - log.logged_ms <= dedupe_ms) {
            return true;
        }
    }
    return false;
}

void remember_xmark_normal_death_probe(
    uintptr_t entity,
    uintptr_t combat_core,
    unsigned long long now_ms) {
    XMarkNormalDeathProbeLog *slot = nullptr;
    for (XMarkNormalDeathProbeLog &log : g_normal_death_probe_logs) {
        if (!log.logged_ms) {
            slot = &log;
            break;
        }
        if ((combat_core && log.combat_core == combat_core) ||
            (entity && log.entity == entity)) {
            slot = &log;
            break;
        }
    }
    if (!slot) {
        slot = &g_normal_death_probe_logs[0];
        for (XMarkNormalDeathProbeLog &log : g_normal_death_probe_logs) {
            if (log.logged_ms < slot->logged_ms) {
                slot = &log;
            }
        }
    }
    slot->entity = entity;
    slot->combat_core = combat_core;
    slot->logged_ms = now_ms;
}

void trace_xmark_normal_death_probe(
    const XMarkOfficialEnemyHost &host,
    float observed_health,
    unsigned long long now_ms,
    const char *phase) {
    char rejected_type[96]{};
    if (!env_bool("MINA_XMARK_NORMAL_DEATH_PROBE_ENABLED", false) ||
        !host.entity ||
        !host.combat_core ||
        xmark_official_host_rejected_for_xmark(host, rejected_type, sizeof(rejected_type)) ||
        xmark_burn_effect_active_for_entity_or_core(host.entity, host.combat_core) ||
        xmark_normal_death_probe_recently_logged(host.entity, host.combat_core, now_ms)) {
        if (rejected_type[0] && g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn normal death probe rejected non-enemy target=0x%p officialCore=0x%p component=%s phase=%s\n",
                reinterpret_cast<void *>(host.entity),
                reinterpret_cast<void *>(host.combat_core),
                rejected_type,
                phase ? phase : "<none>");
            g_mina->Log(message);
        }
        return;
    }

    remember_xmark_normal_death_probe(host.entity, host.combat_core, now_ms);
    if (g_mina) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn normal death candidate phase=%s target=0x%p officialCore=0x%p observedHp=%.3f lastAliveHp=%.3f/%.3f pos=(%.3f, %.3f, %.3f) type=%s\n",
            phase ? phase : "<none>",
            reinterpret_cast<void *>(host.entity),
            reinterpret_cast<void *>(host.combat_core),
            static_cast<double>(observed_health),
            static_cast<double>(host.health),
            static_cast<double>(host.health_max),
            static_cast<double>(host.position.x),
            static_cast<double>(host.position.y),
            static_cast<double>(host.position.z),
            host.component_type[0] ? host.component_type : "-");
        g_mina->Log(message);
    }

    XMarkBurnEffect probe{};
    probe.active = true;
    probe.target = host.entity;
    probe.official_combat_core = host.combat_core;
    probe.last_position = host.position;
    probe.has_last_position = true;
    trace_xmark_burn_death_target(probe, now_ms, phase);
}

void trace_missing_official_enemy_deaths(
    const XMarkOfficialEnemyHost *previous_hosts,
    unsigned int previous_host_count,
    unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_NORMAL_DEATH_PROBE_ENABLED", false) ||
        !previous_hosts ||
        previous_host_count == 0) {
        return;
    }

    const float max_prev_health = std::max(
        0.0f,
        env_float("MINA_XMARK_NORMAL_DEATH_PROBE_MAX_PREV_HEALTH", 2.0f));
    const unsigned int stale_ms = env_uint("MINA_XMARK_NORMAL_DEATH_PROBE_STALE_MS", 600);
    const unsigned int max_per_scan = std::max(1u, env_uint("MINA_XMARK_NORMAL_DEATH_PROBE_MAX_PER_SCAN", 2));
    unsigned int logged = 0;
    for (unsigned int i = 0; i < previous_host_count && logged < max_per_scan; ++i) {
        const XMarkOfficialEnemyHost &host = previous_hosts[i];
        if (!host.active ||
            !host.entity ||
            !host.combat_core ||
            !(host.health > 0.0f) ||
            host.health > max_prev_health ||
            official_enemy_seen(host.combat_core) ||
            xmark_burn_effect_active_for_entity_or_core(host.entity, host.combat_core)) {
            continue;
        }
        if (stale_ms && host.last_seen_ms && now_ms >= host.last_seen_ms + stale_ms) {
            continue;
        }
        trace_xmark_normal_death_probe(host, host.health, now_ms, "normal-death-missing");
        ++logged;
    }
}

void queue_xmark_native_final_hit_death_watch(
    const XMarkOfficialEnemyHost &host,
    unsigned long long now_ms) {
    char rejected_type[96]{};
    if (!env_bool("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_ENABLED", false) ||
        !host.entity ||
        !host.combat_core ||
        xmark_official_host_rejected_for_xmark(host, rejected_type, sizeof(rejected_type))) {
        if (rejected_type[0] && g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn native final-hit death watch rejected non-enemy target=0x%p officialCore=0x%p component=%s\n",
                reinterpret_cast<void *>(host.entity),
                reinterpret_cast<void *>(host.combat_core),
                rejected_type);
            g_mina->Log(message);
        }
        return;
    }

    XMarkNativeFinalHitDeathWatch *slot = nullptr;
    for (XMarkNativeFinalHitDeathWatch &watch : g_native_final_hit_death_watches) {
        if (watch.active &&
            ((host.combat_core && watch.host.combat_core == host.combat_core) ||
             (host.entity && watch.host.entity == host.entity))) {
            slot = &watch;
            break;
        }
        if (!watch.active && !slot) {
            slot = &watch;
        }
    }
    if (!slot) {
        slot = &g_native_final_hit_death_watches[0];
    }

    *slot = XMarkNativeFinalHitDeathWatch{};
    slot->active = true;
    slot->host = host;
    slot->step = 0;
    slot->max_steps = std::max(1u, env_uint("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_STEPS", 6));
    slot->next_probe_ms = now_ms + env_uint("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_DELAY_MS", 24);

    if (g_mina && env_bool("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn native final-hit death watch queued target=0x%p officialCore=0x%p hp=%.3f/%.3f pos=(%.3f, %.3f, %.3f) steps=%u\n",
            reinterpret_cast<void *>(host.entity),
            reinterpret_cast<void *>(host.combat_core),
            static_cast<double>(host.health),
            static_cast<double>(host.health_max),
            static_cast<double>(host.position.x),
            static_cast<double>(host.position.y),
            static_cast<double>(host.position.z),
            slot->max_steps);
        g_mina->Log(message);
    }
}

void update_xmark_native_final_hit_death_watches(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_ENABLED", false)) {
        return;
    }
    const unsigned int step_ms = std::max(
        1u,
        env_uint("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_STEP_MS", 48));
    for (XMarkNativeFinalHitDeathWatch &watch : g_native_final_hit_death_watches) {
        if (!watch.active || now_ms < watch.next_probe_ms) {
            continue;
        }
        if (!watch.host.entity || !probable_heap_object(watch.host.entity)) {
            watch.active = false;
            continue;
        }
        const unsigned int step = watch.step++;
        char phase[64]{};
        std::snprintf(phase, sizeof(phase), "native-final-hit-%u", step);
        XMarkBurnEffect probe{};
        probe.active = true;
        probe.target = watch.host.entity;
        probe.official_combat_core = watch.host.combat_core;
        probe.last_position = watch.host.position;
        probe.has_last_position = true;
        trace_xmark_burn_death_target(probe, now_ms, phase);
        if (watch.step >= watch.max_steps) {
            watch.active = false;
        } else {
            watch.next_probe_ms = now_ms + step_ms;
        }
    }
}

bool official_combat_core_defense_bounds(
    ycComponent *combat_core,
    const MM_Transform &transform,
    Vec3 *center_out,
    float *half_w_out,
    float *half_h_out) {
    if (center_out) {
        *center_out = Vec3{transform.t.x, transform.t.y, transform.t.z};
    }
    if (half_w_out) {
        *half_w_out = 0.0f;
    }
    if (half_h_out) {
        *half_h_out = 0.0f;
    }
    if (!combat_core || !g_mina || !xmark_combat_shape_api_available()) {
        return false;
    }

    CombatShape *shape = nullptr;
    __try {
        shape = g_mina->CombatCoreGetDefenseShape(combat_core);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        shape = nullptr;
    }
    if (!shape) {
        return false;
    }

    uint32_t shape_count = 0;
    __try {
        shape_count = g_mina->CombatShapeGetShapeCount(shape);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        shape_count = 0;
    }
    if (shape_count == 0) {
        return false;
    }

    float min_x = FLT_MAX;
    float max_x = -FLT_MAX;
    float min_y = FLT_MAX;
    float max_y = -FLT_MAX;
    bool found = false;
    for (uint32_t i = 0; i < shape_count; ++i) {
        MM_AABB aabb{};
        MM_Sphere sphere{};
        bool shape_found = false;
        __try {
            if (g_mina->CombatShapeIsAABB(shape, i) &&
                g_mina->CombatShapeGetAABB(shape, i, &aabb)) {
                min_x = std::min(min_x, aabb.center.x - std::fabs(aabb.extents.x));
                max_x = std::max(max_x, aabb.center.x + std::fabs(aabb.extents.x));
                min_y = std::min(min_y, aabb.center.y - std::fabs(aabb.extents.y));
                max_y = std::max(max_y, aabb.center.y + std::fabs(aabb.extents.y));
                shape_found = true;
            } else if (g_mina->CombatShapeIsSphere(shape, i) &&
                       g_mina->CombatShapeGetSphere(shape, i, &sphere)) {
                const float radius = std::fabs(sphere.radius);
                min_x = std::min(min_x, sphere.center.x - radius);
                max_x = std::max(max_x, sphere.center.x + radius);
                min_y = std::min(min_y, sphere.center.y - radius);
                max_y = std::max(max_y, sphere.center.y + radius);
                shape_found = true;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            shape_found = false;
        }
        found = found || shape_found;
    }
    if (!found || min_x > max_x || min_y > max_y) {
        return false;
    }

    const float sx = std::fabs(transform.s.x) > 0.001f ? transform.s.x : 1.0f;
    const float sy = std::fabs(transform.s.y) > 0.001f ? transform.s.y : 1.0f;
    const float local_center_x = (min_x + max_x) * 0.5f;
    const float local_center_y = (min_y + max_y) * 0.5f;
    const float half_w = std::fabs(max_x - min_x) * 0.5f * std::fabs(sx);
    const float half_h = std::fabs(max_y - min_y) * 0.5f * std::fabs(sy);

    if (center_out) {
        center_out->x = transform.t.x + local_center_x * sx;
        center_out->y = transform.t.y + local_center_y * sy;
        center_out->z = transform.t.z;
    }
    if (half_w_out) {
        *half_w_out = half_w;
    }
    if (half_h_out) {
        *half_h_out = half_h;
    }
    return std::isfinite(half_w) && std::isfinite(half_h);
}

void official_enemy_add_host(
    ycComponent *combat_core,
    ycEntity *entity,
    const MM_Transform &transform,
    float health,
    float health_max,
    MM_StringRef type_name,
    unsigned long long now_ms) {
    if (!combat_core || !entity || g_official_enemy_host_count >= _countof(g_official_enemy_hosts)) {
        return;
    }
    const uintptr_t combat_core_ptr = reinterpret_cast<uintptr_t>(combat_core);
    if (official_enemy_seen(combat_core_ptr)) {
        return;
    }
    XMarkOfficialEnemyHost candidate{};
    candidate.active = true;
    candidate.entity = reinterpret_cast<uintptr_t>(entity);
    candidate.combat_core = combat_core_ptr;
    candidate.entity_position = Vec3{transform.t.x, transform.t.y, transform.t.z};
    candidate.position = candidate.entity_position;
    candidate.health = health;
    candidate.health_max = health_max;
    candidate.last_seen_ms = now_ms;
    copy_string_ref(candidate.component_type, sizeof(candidate.component_type), type_name);
    if (env_bool("MINA_XMARK_OFFICIAL_DEFENSE_BOUNDS_ENABLED", false)) {
        if (official_combat_core_defense_bounds(
                combat_core,
                transform,
                &candidate.position,
                &candidate.bounds_half_w,
                &candidate.bounds_half_h)) {
            candidate.bounds_valid = true;
        }
    }

    bool muriel_component = xmark_muriel_component_type(candidate.component_type);
    const bool require_known_enemy =
        env_bool("MINA_XMARK_OFFICIAL_REQUIRE_KNOWN_ENEMY_COMPONENT", false);
    char enemy_type[96]{};
    bool has_known_enemy_component = false;
    const bool inspect_muriel_components =
        env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) &&
        (g_last_room == 14u || g_last_room == 0xFFFFFFFFu);
    if (require_known_enemy || inspect_muriel_components) {
        has_known_enemy_component = xmark_entity_has_known_enemy_component(
            entity,
            enemy_type,
            sizeof(enemy_type));
        muriel_component = muriel_component ||
            (has_known_enemy_component && xmark_muriel_component_type(enemy_type));
    }
    if (require_known_enemy) {
        if (!has_known_enemy_component) {
            if (g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn official enemy scan skipped unrecognized CombatCore parent=0x%p core=0x%p pos=(%.3f, %.3f, %.3f)\n",
                    reinterpret_cast<void *>(entity),
                    reinterpret_cast<void *>(combat_core),
                    static_cast<double>(candidate.position.x),
                    static_cast<double>(candidate.position.y),
                    static_cast<double>(candidate.position.z));
                g_mina->Log(message);
            }
            return;
        }
    } else {
        char rejected_type[96]{};
        if (xmark_official_host_rejected_for_xmark(candidate, rejected_type, sizeof(rejected_type))) {
            if (g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn official enemy scan skipped rejected CombatCore parent=0x%p core=0x%p component=%s pos=(%.3f, %.3f, %.3f)\n",
                    reinterpret_cast<void *>(entity),
                    reinterpret_cast<void *>(combat_core),
                    rejected_type[0] ? rejected_type : "-",
                    static_cast<double>(candidate.position.x),
                    static_cast<double>(candidate.position.y),
                    static_cast<double>(candidate.position.z));
                g_mina->Log(message);
            }
            return;
        }
    }

    candidate.entity_weak = xmark_weak_ptr_create(entity);
    candidate.combat_core_weak = xmark_weak_ptr_create(combat_core);
    official_enemy_refresh_anim_bounds(candidate);
    official_enemy_capture_spawn_identity(candidate, entity);
    candidate.muriel_no_damage =
        env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) &&
        (muriel_component ||
         (candidate.spawn_identity_valid &&
          candidate.spawn_entity_type == ENTITYTYPE_MURIEL_NPC));
    if (candidate.muriel_no_damage && g_mina &&
        env_bool("MINA_XMARK_MURIEL_HOST_LOG", false) &&
        (candidate.entity != g_last_muriel_host_log_entity ||
         now_ms >= g_last_muriel_host_log_ms +
            std::max(250u, env_uint("MINA_XMARK_MURIEL_HOST_LOG_INTERVAL_MS", 2000)))) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Muriel host registered entity=0x%p core=0x%p spawnType=%u pos=(%.3f, %.3f).\n",
            reinterpret_cast<void *>(candidate.entity),
            reinterpret_cast<void *>(candidate.combat_core),
            candidate.spawn_entity_type,
            static_cast<double>(candidate.position.x),
            static_cast<double>(candidate.position.y));
        g_mina->Log(message);
        g_last_muriel_host_log_entity = candidate.entity;
        g_last_muriel_host_log_ms = now_ms;
    }
    XMarkOfficialEnemyHost &host = g_official_enemy_hosts[g_official_enemy_host_count++];
    host = candidate;
}

void official_enemy_traverse_entity(
    ycEntity *entity,
    ycEntity *player_entity_api,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int max_nodes,
    unsigned long long now_ms) {
    if (!g_mina ||
        !entity ||
        depth > max_depth ||
        g_official_enemy_scan_nodes >= max_nodes ||
        g_official_enemy_host_count >= _countof(g_official_enemy_hosts)) {
        return;
    }
    if (player_entity_api &&
        entity == player_entity_api &&
        env_bool("MINA_XMARK_OFFICIAL_EXCLUDE_PLAYER_SUBTREE", true)) {
        return;
    }
    ++g_official_enemy_scan_nodes;

    size_t child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    if (child_count == 0) {
        return;
    }
    const size_t max_children = std::min<size_t>(child_count, env_uint("MINA_XMARK_OFFICIAL_ENTITY_CHILD_CAP", 256));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * max_children));
    if (!children) {
        return;
    }
    const size_t read_count = g_mina->EntityGetChildren(entity, children, max_children);
    const size_t limit = std::min(read_count, max_children);
    for (size_t i = 0; i < limit; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        const MM_Rtti component_type = g_mina->ComponentGetType(component);
        const bool is_entity =
            rtti_equal(component_type, g_official_entity_rtti) ||
            (rtti_valid(g_official_entity_rtti) && g_mina->ComponentIsa &&
                g_mina->ComponentIsa(component, g_official_entity_rtti));
        if (is_entity) {
            official_enemy_traverse_entity(
                reinterpret_cast<ycEntity *>(component),
                player_entity_api,
                depth + 1u,
                max_depth,
                max_nodes,
                now_ms);
            continue;
        }
        const bool is_combat_core =
            rtti_equal(component_type, g_official_combat_core_rtti) ||
            (rtti_valid(g_official_combat_core_rtti) && g_mina->ComponentIsa &&
                g_mina->ComponentIsa(component, g_official_combat_core_rtti));
        if (!is_combat_core) {
            continue;
        }
        ycEntity *parent = g_mina->ComponentGetParent(component);
        if (!parent || parent == player_entity_api) {
            continue;
        }
        char rejected_type[96]{};
        if (xmark_entity_has_rejected_non_enemy_component(parent, rejected_type, sizeof(rejected_type))) {
            if (g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
                char message[320]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn official enemy scan skipped non-enemy CombatCore parent=0x%p core=0x%p component=%s\n",
                    reinterpret_cast<void *>(parent),
                    reinterpret_cast<void *>(component),
                    rejected_type[0] ? rejected_type : "-");
                g_mina->Log(message);
            }
            continue;
        }
        const float health_max = g_mina->CombatCoreGetHealthMax(component);
        const float health = g_mina->CombatCoreGetHealth(component);
        if (!(health_max > 0.0f)) {
            continue;
        }
        const MM_Transform transform = g_mina->EntityGetWorldTransform(parent);
        const MM_StringRef type_name = g_mina->ComponentGetTypeName(component);
        if (!(health > 0.0f)) {
            if (std::isfinite(health) && health <= 0.0f) {
                XMarkOfficialEnemyHost dead_host{};
                dead_host.active = true;
                dead_host.entity = reinterpret_cast<uintptr_t>(parent);
                dead_host.combat_core = reinterpret_cast<uintptr_t>(component);
                dead_host.position = Vec3{transform.t.x, transform.t.y, transform.t.z};
                dead_host.health = health;
                dead_host.health_max = health_max;
                dead_host.last_seen_ms = now_ms;
                copy_string_ref(dead_host.component_type, sizeof(dead_host.component_type), type_name);
                char dead_rejected_type[96]{};
                if (!xmark_official_host_rejected_for_xmark(dead_host, dead_rejected_type, sizeof(dead_rejected_type))) {
                    trace_xmark_normal_death_probe(dead_host, health, now_ms, "normal-death-zero");
                } else if (g_mina && env_bool("MINA_XMARK_REJECT_NON_ENEMY_LOG", false)) {
                    char message[320]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn normal death zero skipped rejected target=0x%p officialCore=0x%p component=%s\n",
                        reinterpret_cast<void *>(dead_host.entity),
                        reinterpret_cast<void *>(dead_host.combat_core),
                        dead_rejected_type[0] ? dead_rejected_type : "-");
                    g_mina->Log(message);
                }
            }
            continue;
        }
        official_enemy_add_host(component, parent, transform, health, health_max, type_name, now_ms);
    }
    g_mina->Free(children);
}

void update_official_enemy_snapshot(World *world, unsigned long long now_ms) {
    if (!world ||
        !env_bool("MINA_XMARK_OFFICIAL_ENEMY_SNAPSHOT_ENABLED", true) ||
        !xmark_component_api_available() ||
        !official_enemy_init_rtti()) {
        official_enemy_clear_snapshot();
        return;
    }
    const unsigned int interval_ms = std::max(16u, env_uint("MINA_XMARK_OFFICIAL_ENEMY_SCAN_MS", 100));
    if (g_last_official_enemy_scan_ms &&
        now_ms - g_last_official_enemy_scan_ms < interval_ms) {
        return;
    }
    g_last_official_enemy_scan_ms = now_ms;
    const bool perf_enabled = mod_perf_enabled();
    const unsigned long long perf_started = perf_enabled ? mod_perf_ticks() : 0;

    XMarkOfficialEnemyHost previous_hosts[128]{};
    const unsigned int previous_host_count =
        std::min<unsigned int>(g_official_enemy_host_count, static_cast<unsigned int>(_countof(previous_hosts)));
    for (unsigned int i = 0; i < previous_host_count; ++i) {
        previous_hosts[i] = g_official_enemy_hosts[i];
        previous_hosts[i].entity_weak = nullptr;
        previous_hosts[i].combat_core_weak = nullptr;
        previous_hosts[i].spawned_component_weak = nullptr;
    }

    if (xmark_spawn_point_room_registry_enabled()) {
        ++g_official_enemy_registry_generation;
        if (g_official_enemy_registry_generation == 0) {
            g_official_enemy_registry_generation = 1;
        }
    }
    official_enemy_clear_snapshot();
    const unsigned int max_depth = env_uint("MINA_XMARK_OFFICIAL_ENTITY_MAX_DEPTH", 32);
    const unsigned int max_nodes = env_uint("MINA_XMARK_OFFICIAL_ENTITY_MAX_NODES", 2048);
    bool ok = false;
    __try {
        ycEntity *player_entity_api = nullptr;
        if (g_mina->PlayerGetComponent && g_mina->ComponentGetParent) {
            ycComponent *player_component = g_mina->PlayerGetComponent();
            if (player_component) {
                player_entity_api = g_mina->ComponentGetParent(player_component);
            }
        }
        auto traverse_root = [&](ycEntity *root) {
            if (root && g_official_enemy_scan_nodes < max_nodes) {
                official_enemy_traverse_entity(root, player_entity_api, 0, max_depth, max_nodes, now_ms);
                ok = true;
            }
        };
        traverse_root(g_mina->WorldGetGameRootEntity(world));
        if (env_bool("MINA_XMARK_OFFICIAL_SCAN_SYSTEM_ROOT", true) && g_mina->WorldGetSystemRootEntity) {
            traverse_root(g_mina->WorldGetSystemRootEntity(world));
        }
        if (env_bool("MINA_XMARK_OFFICIAL_SCAN_MENU_ROOT", false) && g_mina->WorldGetMenuRootEntity) {
            traverse_root(g_mina->WorldGetMenuRootEntity(world));
        }
        if (env_bool("MINA_XMARK_OFFICIAL_SCAN_HUD_ROOT", false) && g_mina->WorldGetHUDRootEntity) {
            traverse_root(g_mina->WorldGetHUDRootEntity(world));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    g_official_enemy_snapshot_valid = ok;
    if (!ok) {
        ++g_official_enemy_scan_faults;
        g_official_enemy_api_fault = true;
        official_enemy_clear_snapshot();
    } else {
        g_official_enemy_api_fault = false;
        trace_missing_official_enemy_deaths(previous_hosts, previous_host_count, now_ms);
    }

    if (g_mina && env_bool("MINA_XMARK_OFFICIAL_ENEMY_SNAPSHOT_LOG", false)) {
        const unsigned int log_ms = std::max(100u, env_uint("MINA_XMARK_OFFICIAL_ENEMY_SNAPSHOT_LOG_MS", 1000));
        if (!g_last_official_enemy_log_ms || now_ms - g_last_official_enemy_log_ms >= log_ms) {
            g_last_official_enemy_log_ms = now_ms;
            const XMarkOfficialEnemyHost *first =
                g_official_enemy_host_count > 0 ? &g_official_enemy_hosts[0] : nullptr;
            unsigned int spawn_identity_count = 0;
            for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
                if (g_official_enemy_hosts[i].spawn_identity_valid) {
                    ++spawn_identity_count;
                }
            }
            char message[512]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn official enemy snapshot ok=%u count=%u nodes=%u spawnIds=%u generation=%u firstEntity=0x%p firstHp=%.2f/%.2f firstPos=(%.3f, %.3f, %.3f)\n",
                ok ? 1u : 0u,
                g_official_enemy_host_count,
                g_official_enemy_scan_nodes,
                spawn_identity_count,
                g_official_enemy_registry_generation,
                first ? reinterpret_cast<void *>(first->entity) : nullptr,
                first ? static_cast<double>(first->health) : 0.0,
                first ? static_cast<double>(first->health_max) : 0.0,
                first ? static_cast<double>(first->position.x) : 0.0,
                first ? static_cast<double>(first->position.y) : 0.0,
                first ? static_cast<double>(first->position.z) : 0.0);
            g_mina->Log(message);
        }
    }
    if (perf_enabled) {
        mod_perf_record(ModPerfStage::Snapshot, perf_started);
    }
}

void maybe_refresh_official_enemy_snapshot_for_room(
    unsigned int room_index,
    float room_time,
    unsigned long long now_ms) {
    if (!g_last_world ||
        !env_bool("MINA_XMARK_OFFICIAL_ENEMY_SNAPSHOT_ENABLED", true) ||
        !env_bool("MINA_XMARK_OFFICIAL_SCAN_ON_ROOM_CHANGE", true)) {
        return;
    }

    static unsigned int warmup_room = 0xFFFFFFFFu;
    static unsigned int warmup_scan_stage = 0;
    if (room_index != warmup_room) {
        warmup_room = room_index;
        warmup_scan_stage = 0;
    }

    const unsigned int stale_ms = env_uint("MINA_XMARK_OFFICIAL_ROOM_SNAPSHOT_REFRESH_MS", 30000);
    const bool room_changed = room_index != g_last_official_enemy_room;
    const float initial_delay_seconds =
        static_cast<float>(env_uint("MINA_XMARK_OFFICIAL_ROOM_INITIAL_SCAN_DELAY_MS", 250)) / 1000.0f;
    if (room_changed && room_time >= 0.0f && room_time < initial_delay_seconds) {
        return;
    }
    const bool stale =
        !xmark_spawn_point_room_registry_authoritative() &&
        stale_ms > 0 &&
        g_last_official_enemy_room_scan_ms > 0 &&
        now_ms >= g_last_official_enemy_room_scan_ms &&
        now_ms - g_last_official_enemy_room_scan_ms >= stale_ms;
    const unsigned int warmup_scan_count =
        env_uint("MINA_XMARK_OFFICIAL_ROOM_WARMUP_RESCAN_COUNT", 3);
    const unsigned int minimum_warmup_scans = std::min(
        warmup_scan_count,
        env_uint("MINA_XMARK_OFFICIAL_ROOM_MINIMUM_WARMUP_RESCANS", 1));
    const unsigned int warmup_scan_base_ms =
        env_uint("MINA_XMARK_OFFICIAL_ROOM_WARMUP_RESCAN_MS", 750);
    const unsigned int warmup_scan_step_ms =
        std::max(100u, env_uint("MINA_XMARK_OFFICIAL_ROOM_WARMUP_RESCAN_STEP_MS", 750));
    const float warmup_scan_seconds = static_cast<float>(
        warmup_scan_base_ms + warmup_scan_stage * warmup_scan_step_ms) / 1000.0f;
    const bool warmup_scan_due =
        warmup_scan_stage < warmup_scan_count &&
        (warmup_scan_stage < minimum_warmup_scans ||
         !xmark_spawn_point_room_registry_authoritative()) &&
        room_time >= warmup_scan_seconds;
    const bool invalidation_scan_due =
        g_official_enemy_registry_rescan_requested_ms &&
        now_ms >= g_official_enemy_registry_rescan_requested_ms;
    if (!room_changed && !stale && !warmup_scan_due &&
        !invalidation_scan_due && g_official_enemy_snapshot_valid) {
        return;
    }

    g_last_official_enemy_scan_ms = 0;
    update_official_enemy_snapshot(g_last_world, now_ms);
    g_official_enemy_registry_rescan_requested_ms = 0;
    g_last_official_enemy_room = room_index;
    g_last_official_enemy_room_scan_ms = now_ms;
    if (warmup_scan_due) {
        ++warmup_scan_stage;
    }
}

bool official_enemy_host_by_combat_core(uintptr_t combat_core, XMarkOfficialEnemyHost *host_out) {
    if (host_out) {
        *host_out = XMarkOfficialEnemyHost{};
    }
    if (!combat_core) {
        return false;
    }
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        uintptr_t resolved_entity = 0;
        uintptr_t resolved_core = 0;
        official_enemy_resolve_weak_refs(host, &resolved_entity, &resolved_core);
        if (!host.active ||
            (host.combat_core != combat_core && resolved_core != combat_core)) {
            continue;
        }
        official_enemy_refresh_anim_bounds(host);
        if (host_out) {
            XMarkOfficialEnemyHost resolved = host;
            resolved.entity = resolved_entity ? resolved_entity : host.entity;
            resolved.combat_core = resolved_core ? resolved_core : host.combat_core;
            resolved.entity_weak = nullptr;
            resolved.combat_core_weak = nullptr;
            resolved.game_anim_weak = nullptr;
            resolved.spawned_component_weak = nullptr;
            *host_out = resolved;
        }
        return true;
    }
    return false;
}

bool official_enemy_host_by_entity(uintptr_t entity, XMarkOfficialEnemyHost *host_out) {
    if (host_out) {
        *host_out = XMarkOfficialEnemyHost{};
    }
    if (!entity) {
        return false;
    }
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        uintptr_t resolved_entity = 0;
        uintptr_t resolved_core = 0;
        official_enemy_resolve_weak_refs(host, &resolved_entity, &resolved_core);
        if (!host.active ||
            (host.entity != entity && resolved_entity != entity)) {
            continue;
        }
        official_enemy_refresh_anim_bounds(host);
        if (host_out) {
            XMarkOfficialEnemyHost resolved = host;
            resolved.entity = resolved_entity ? resolved_entity : host.entity;
            resolved.combat_core = resolved_core ? resolved_core : host.combat_core;
            resolved.entity_weak = nullptr;
            resolved.combat_core_weak = nullptr;
            resolved.game_anim_weak = nullptr;
            resolved.spawned_component_weak = nullptr;
            *host_out = resolved;
        }
        return true;
    }
    return false;
}

bool official_enemy_host_for_attachment(const XMarkAttachment &attachment, XMarkOfficialEnemyHost *host_out) {
    if (attachment.official_combat_core &&
        official_enemy_host_by_combat_core(attachment.official_combat_core, host_out)) {
        return true;
    }
    return official_enemy_host_by_entity(attachment.target, host_out);
}

bool official_enemy_host_for_burn(const XMarkBurnEffect &burn, XMarkOfficialEnemyHost *host_out) {
    if (burn.official_combat_core &&
        official_enemy_host_by_combat_core(burn.official_combat_core, host_out)) {
        return true;
    }
    return official_enemy_host_by_entity(burn.target, host_out);
}

void update_xmark_enemy_status_lifecycle(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_UNIFIED_ENEMY_STATUS_ENABLED", true)) {
        return;
    }
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        if (!status.active) {
            continue;
        }
        uintptr_t entity = status.entity_weak
            ? reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(status.entity_weak))
            : status.entity;
        uintptr_t combat_core = status.combat_core_weak
            ? reinterpret_cast<uintptr_t>(xmark_weak_ptr_get(status.combat_core_weak))
            : status.combat_core;
        bool lifecycle_alive = entity && combat_core;
        if (status.training_target) {
            if (now_ms >= status.state_expires_ms) {
                if (status.attachment_index && status.attachment_index <= _countof(g_xmark_attachments)) {
                    XMarkAttachment &attachment = g_xmark_attachments[status.attachment_index - 1];
                    if (attachment.active && attachment.status_id == xmark_enemy_status_id(&status)) {
                        attachment.active = false;
                    }
                }
                if (status.burn_index && status.burn_index <= _countof(g_xmark_burn_effects)) {
                    XMarkBurnEffect &burn = g_xmark_burn_effects[status.burn_index - 1];
                    if (burn.active && burn.status_id == xmark_enemy_status_id(&status)) {
                        end_xmark_burn_effect(burn, now_ms);
                    }
                }
                if (status.active) {
                    xmark_enemy_status_release(status);
                }
                continue;
            }
            XMarkOfficialEnemyHost training_host{};
            const bool resolved_training_host =
                (official_enemy_host_by_combat_core(status.combat_core, &training_host) ||
                 official_enemy_host_by_entity(status.entity, &training_host)) &&
                training_host.active &&
                xmark_official_host_is_muriel(training_host);
            if (resolved_training_host) {
                entity = training_host.entity;
                combat_core = training_host.combat_core;
                lifecycle_alive = entity && combat_core;
            } else {
                lifecycle_alive = now_ms < status.state_expires_ms;
                entity = status.entity;
                combat_core = status.combat_core;
            }
        }
        if (lifecycle_alive && !status.training_target && status.spawn_point &&
            xmark_spawn_point_room_registry_enabled()) {
            __try {
                SpawnPoint *spawn = reinterpret_cast<SpawnPoint *>(status.spawn_point);
                lifecycle_alive = !g_mina->SpawnPointIsEntityKilled(spawn) &&
                    g_mina->SpawnPointIsEntitySpawned(spawn);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                lifecycle_alive = false;
            }
        }

        float health = status.health;
        if (lifecycle_alive && !status.training_target && combat_core_health_read(combat_core, &health)) {
            status.health = health;
            if (health <= 0.0f) {
                status.phase = XMarkEnemyStatusPhase::Dying;
            }
        }
        status.entity = entity;
        status.combat_core = combat_core;
        status.last_lifecycle_ms = now_ms;

        if (lifecycle_alive) {
            continue;
        }
        if (status.attachment_index && status.attachment_index <= _countof(g_xmark_attachments)) {
            XMarkAttachment &attachment = g_xmark_attachments[status.attachment_index - 1];
            if (attachment.active && attachment.status_id == xmark_enemy_status_id(&status)) {
                attachment.active = false;
            }
        }
        if (status.burn_index && status.burn_index <= _countof(g_xmark_burn_effects)) {
            XMarkBurnEffect &burn = g_xmark_burn_effects[status.burn_index - 1];
            if (burn.active && burn.status_id == xmark_enemy_status_id(&status)) {
                end_xmark_burn_effect(burn, now_ms);
            }
        }
        if (status.active && status.entity) {
            shorten_xmark_hud_mark(status.entity, now_ms);
        }
        if (status.active) {
            xmark_enemy_status_release(status);
        }
    }
}

bool official_enemy_host_near_position(
    const Vec3 &position,
    float radius_x,
    float radius_y,
    XMarkOfficialEnemyHost *host_out) {
    if (host_out) {
        *host_out = XMarkOfficialEnemyHost{};
    }
    if (!g_official_enemy_snapshot_valid || g_official_enemy_host_count == 0) {
        return false;
    }
    radius_x = std::max(0.1f, radius_x);
    radius_y = std::max(0.1f, radius_y);
    const XMarkOfficialEnemyHost *best = nullptr;
    float best_score = FLT_MAX;
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        const XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        if (!host.active || !host.entity || !host.combat_core || !(host.health > 0.0f) || !(host.health_max > 0.0f)) {
            continue;
        }
        const float dx = host.position.x - position.x;
        const float dy = host.position.y - position.y;
        if (std::fabs(dx) > radius_x || std::fabs(dy) > radius_y) {
            continue;
        }
        const float score = (dx * dx) + (dy * dy);
        if (score >= best_score) {
            continue;
        }
        best_score = score;
        best = &host;
    }
    if (!best) {
        return false;
    }
    if (host_out) {
        *host_out = official_enemy_host_with_resolved_refs(*best);
    }
    return true;
}

bool promote_xmark_attachment_to_official_enemy(XMarkAttachment &attachment, unsigned long long now_ms) {
    XMarkOfficialEnemyHost host{};
    if (official_enemy_host_for_attachment(attachment, &host) && host.health > 0.0f && host.health_max > 0.0f) {
        attachment.target = host.entity;
        attachment.official_combat_core = host.combat_core;
        attachment.official_follow = true;
        attachment.suppress_hud = xmark_official_host_is_muriel(host);
        attachment.target_health_like = !attachment.suppress_hud;
        if (attachment.suppress_hud) {
            clear_suppressed_xmark_hud_aliases(attachment.target);
        }
        attachment.render_half_w = official_enemy_render_half_from_health(host, xmark_default_render_half_w());
        attachment.render_half_h = official_enemy_render_half_from_health(host, xmark_default_render_half_h());
        return true;
    }

    Vec3 target_position = attachment.has_last_position
        ? xmark_attachment_target_position_from_mark(attachment.last_position)
        : Vec3{0.0f, 0.0f, 0.0f};
    if (attachment.visual_follow && attachment.visual_key) {
        XMarkVisualEnemyHost visual_host{};
        if (visual_enemy_host_by_key(attachment.visual_key, &visual_host, now_ms)) {
            target_position = visual_host_render_position(visual_host);
        }
    }

    const float radius_x = env_float("MINA_XMARK_BURN_PROMOTE_OFFICIAL_RADIUS_X", 3.0f);
    const float radius_y = env_float("MINA_XMARK_BURN_PROMOTE_OFFICIAL_RADIUS_Y", 3.0f);
    if (!official_enemy_host_near_position(target_position, radius_x, radius_y, &host)) {
        return false;
    }

    attachment.target = host.entity;
    attachment.official_combat_core = host.combat_core;
    attachment.official_follow = true;
    attachment.suppress_hud = xmark_official_host_is_muriel(host);
    attachment.target_health_like = !attachment.suppress_hud;
    if (attachment.suppress_hud) {
        clear_suppressed_xmark_hud_aliases(attachment.target);
    }
    attachment.render_half_w = official_enemy_render_half_from_health(host, xmark_default_render_half_w());
    attachment.render_half_h = official_enemy_render_half_from_health(host, xmark_default_render_half_h());
    attachment.last_position = xmark_attachment_mark_position(host.position);
    attachment.has_last_position = true;
    if (g_mina && env_bool("MINA_XMARK_BURN_PROMOTE_OFFICIAL_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn consume promoted visual mark to official enemy target=0x%p core=0x%p hp=%.3f/%.3f pos=(%.3f, %.3f, %.3f)\n",
            reinterpret_cast<void *>(host.entity),
            reinterpret_cast<void *>(host.combat_core),
            static_cast<double>(host.health),
            static_cast<double>(host.health_max),
            static_cast<double>(host.position.x),
            static_cast<double>(host.position.y),
            static_cast<double>(host.position.z));
        g_mina->Log(message);
    }
    return true;
}

float official_enemy_render_half_from_health(const XMarkOfficialEnemyHost &host, float fallback) {
    fallback = std::max(0.001f, fallback);
    if (env_bool("MINA_XMARK_OFFICIAL_RENDER_SCALE_FROM_ANIM_BOUNDS", true) &&
        host.visual_bounds_valid) {
        const float scale = std::max(
            0.05f,
            env_float("MINA_XMARK_OFFICIAL_RENDER_ANIM_BOUNDS_SCALE", 0.50f));
        const float visual_half = std::max(host.visual_half_w, host.visual_half_h) * scale;
        const float max_half = std::max(
            fallback,
            env_float("MINA_XMARK_OFFICIAL_RENDER_ANIM_BOUNDS_MAX_HALF", 1.25f));
        return std::max(fallback, std::min(max_half, std::max(0.001f, visual_half)));
    }
    if (!env_bool("MINA_XMARK_OFFICIAL_RENDER_SCALE_FROM_DEFENSE_BOUNDS", true) ||
        !host.bounds_valid) {
        return fallback;
    }
    const float scale = std::max(0.05f, env_float("MINA_XMARK_OFFICIAL_RENDER_BOUNDS_SCALE", 0.50f));
    const float half = std::max(host.bounds_half_w, host.bounds_half_h) * scale;
    const float max_half = std::max(fallback, env_float("MINA_XMARK_OFFICIAL_RENDER_BOUNDS_MAX_HALF", 1.25f));
    return std::max(fallback, std::min(max_half, std::max(0.001f, half)));
}

float official_enemy_burn_render_half_from_health(const XMarkOfficialEnemyHost &host, float fallback) {
    fallback = std::max(0.001f, fallback);
    if (!env_bool("MINA_XMARK_BURN_SCALE_TO_HEALTH_MAX", true) ||
        !(host.health_max > 0.0f)) {
        return fallback;
    }
    const float threshold = env_float("MINA_XMARK_BURN_SCALE_HEALTH_THRESHOLD", 24.0f);
    const float health_over_threshold = host.health_max - threshold;
    if (!(health_over_threshold > 0.0f)) {
        return fallback;
    }
    const float extra = std::min(
        env_float("MINA_XMARK_BURN_SCALE_MAX_EXTRA_HALF", 1.35f),
        health_over_threshold * env_float("MINA_XMARK_BURN_SCALE_HALF_PER_HP", 0.018f));
    return std::max(fallback, fallback + std::max(0.0f, extra));
}

bool combat_core_health_read(uintptr_t combat_core, float *health_out) {
    if (health_out) {
        *health_out = 0.0f;
    }
    if (!g_mina || !g_mina->CombatCoreGetHealth || !combat_core || !probable_heap_object(combat_core)) {
        return false;
    }
    float health = 0.0f;
    bool ok = false;
    __try {
        health = g_mina->CombatCoreGetHealth(reinterpret_cast<ycComponent *>(combat_core));
        ok = std::isfinite(health);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    if (health_out) {
        *health_out = health;
    }
    return true;
}

bool combat_core_is_alive(uintptr_t combat_core) {
    float health = 0.0f;
    return combat_core_health_read(combat_core, &health) && health > 0.0f;
}

void xmark_attachment_observe_health(
    XMarkAttachment &attachment,
    float health,
    unsigned long long now_ms) {
    if (!std::isfinite(health)) {
        return;
    }
    const float min_drop = std::max(
        0.0f,
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_DROP_MIN", 0.01f));
    if (attachment.has_observed_health &&
        health + min_drop <= attachment.observed_health) {
        attachment.health_before_recent_drop = attachment.observed_health;
        attachment.health_after_recent_drop = health;
        attachment.recent_health_drop_ms = now_ms;
    }
    attachment.observed_health = health;
    attachment.observed_health_ms = now_ms;
    attachment.has_observed_health = true;
}

bool official_entity_world_position_read(uintptr_t entity, Vec3 *position_out) {
    if (position_out) {
        *position_out = Vec3{0.0f, 0.0f, 0.0f};
    }
    if (!position_out ||
        !g_mina ||
        !g_mina->EntityGetWorldTransform ||
        !entity ||
        !probable_heap_object(entity)) {
        return false;
    }

    bool ok = false;
    MM_Transform transform{};
    __try {
        transform = g_mina->EntityGetWorldTransform(reinterpret_cast<ycEntity *>(entity));
        ok = std::isfinite(transform.t.x) &&
            std::isfinite(transform.t.y) &&
            std::isfinite(transform.t.z);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    *position_out = Vec3{transform.t.x, transform.t.y, transform.t.z};
    return true;
}

void append_probe_token(char *dest, size_t dest_size, const char *token) {
    if (!dest || dest_size == 0 || !token || !token[0]) {
        return;
    }
    const size_t used = std::strlen(dest);
    if (used + 4 >= dest_size) {
        return;
    }
    std::snprintf(
        dest + used,
        dest_size - used,
        "%s%s",
        used ? "," : "",
        token);
}

bool xmark_burn_death_probe_api_available() {
    return g_mina &&
        g_mina->EntityGetChildren &&
        g_mina->ComponentGetTypeName &&
        g_mina->ComponentGetType &&
        g_mina->ComponentIsa &&
        g_mina->GameAnimGetSeqNameNoDir &&
        xmark_game_anim_init_rtti() &&
        official_enemy_init_rtti();
}

struct XMarkBurnDeathProbeSummary {
    unsigned int nodes;
    unsigned int components;
    unsigned int anims;
    unsigned int play_attempts;
    unsigned int play_successes;
    char play_mode[32];
    char component_tokens[768];
    char anim_tokens[768];
};

void xmark_burn_death_probe_entity(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int max_nodes,
    XMarkBurnDeathProbeSummary *summary,
    bool try_play,
    const char *play_sequence) {
    if (!entity || !summary || depth > max_depth || summary->nodes >= max_nodes) {
        return;
    }
    ++summary->nodes;

    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    if (child_count == 0) {
        return;
    }

    const size_t max_children = std::min<size_t>(
        child_count,
        env_uint("MINA_XMARK_BURN_DEATH_PROBE_CHILD_CAP", 96));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * max_children));
    if (!children) {
        return;
    }

    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, max_children);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }

    const size_t limit = std::min(read_count, max_children);
    for (size_t i = 0; i < limit && summary->nodes < max_nodes; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }

        ++summary->components;
        MM_Rtti component_type{};
        __try {
            component_type = g_mina->ComponentGetType(component);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            component_type = {};
        }

        char type_name[96]{};
        __try {
            copy_string_ref(type_name, sizeof(type_name), g_mina->ComponentGetTypeName(component));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            type_name[0] = 0;
        }
        if (type_name[0]) {
            char component_token[144]{};
            std::snprintf(
                component_token,
                sizeof(component_token),
                "d%u:%s@0x%p",
                depth,
                type_name,
                reinterpret_cast<void *>(component));
            append_probe_token(summary->component_tokens, sizeof(summary->component_tokens), component_token);
        }

        const bool is_entity =
            rtti_equal(component_type, g_official_entity_rtti) ||
            (rtti_valid(g_official_entity_rtti) && g_mina->ComponentIsa(component, g_official_entity_rtti));
        if (is_entity) {
            xmark_burn_death_probe_entity(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                max_nodes,
                summary,
                try_play,
                play_sequence);
            continue;
        }

        const bool is_game_anim =
            rtti_equal(component_type, g_game_anim_rtti) ||
            (rtti_valid(g_game_anim_rtti) && g_mina->ComponentIsa(component, g_game_anim_rtti));
        if (!is_game_anim) {
            continue;
        }

        ++summary->anims;
        char seq[96]{};
        char seq_full[96]{};
        uint32_t frame_idx = 0;
        bool paused = false;
        bool done = false;
        bool new_frame = false;
        __try {
            copy_mm_string_to_cstr(seq, sizeof(seq), g_mina->GameAnimGetSeqNameNoDir(component));
            if (g_mina->GameAnimGetSeqName) {
                copy_mm_string_to_cstr(seq_full, sizeof(seq_full), g_mina->GameAnimGetSeqName(component));
            }
            if (g_mina->GameAnimGetSeqFrameIdx) {
                frame_idx = g_mina->GameAnimGetSeqFrameIdx(component);
            }
            if (g_mina->GameAnimIsPaused) {
                paused = g_mina->GameAnimIsPaused(component);
            }
            if (g_mina->GameAnimIsDone) {
                done = g_mina->GameAnimIsDone(component);
            }
            if (g_mina->GameAnimIsNewFrame) {
                new_frame = g_mina->GameAnimIsNewFrame(component);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            seq[0] = 0;
            seq_full[0] = 0;
            frame_idx = 0;
            paused = false;
            done = false;
            new_frame = false;
        }
        char anim_token[192]{};
        std::snprintf(
            anim_token,
            sizeof(anim_token),
            "d%u:%s/%s#%u p%u d%u n%u@0x%p",
            depth,
            seq[0] ? seq : "-",
            seq_full[0] ? seq_full : "-",
            frame_idx,
            paused ? 1u : 0u,
            done ? 1u : 0u,
            new_frame ? 1u : 0u,
            reinterpret_cast<void *>(component));
        append_probe_token(summary->anim_tokens, sizeof(summary->anim_tokens), anim_token);

        if (try_play &&
            play_sequence &&
            play_sequence[0] &&
            (g_mina->GameAnimPlayDir || g_mina->GameAnimPlay) &&
            !summary->play_successes) {
            ++summary->play_attempts;
            bool played = false;
            const int32_t loops = static_cast<int32_t>(env_int(
                "MINA_XMARK_BURN_FORCE_DEATH_ANIM_LOOPS",
                env_int("MINA_XMARK_BURN_DEATH_ANIM_PLAY_LOOPS", 1)));
            const float speed = env_float(
                "MINA_XMARK_BURN_FORCE_DEATH_ANIM_SPEED",
                env_float("MINA_XMARK_BURN_DEATH_ANIM_PLAY_SPEED", 1.0f));
            const bool force_restart = env_bool("MINA_XMARK_BURN_DEATH_ANIM_FORCE_RESTART", true);
            const bool prefer_dir = env_bool("MINA_XMARK_BURN_DEATH_ANIM_USE_PLAY_DIR", true);
            __try {
                if (prefer_dir && g_mina->GameAnimPlayDir) {
                    g_mina->GameAnimPlayDir(
                        component,
                        play_sequence,
                        loops,
                        speed,
                        force_restart);
                    std::snprintf(summary->play_mode, sizeof(summary->play_mode), "dir");
                } else if (g_mina->GameAnimPlay) {
                    g_mina->GameAnimPlay(
                        component,
                        play_sequence,
                        loops,
                        speed,
                        force_restart);
                    std::snprintf(summary->play_mode, sizeof(summary->play_mode), "plain");
                } else if (g_mina->GameAnimPlayDir) {
                    g_mina->GameAnimPlayDir(
                        component,
                        play_sequence,
                        loops,
                        speed,
                        force_restart);
                    std::snprintf(summary->play_mode, sizeof(summary->play_mode), "dir-fallback");
                }
                played = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                played = false;
            }
            if (played) {
                ++summary->play_successes;
            }
        }
    }
    g_mina->Free(children);
}

void force_xmark_burn_death_animation(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_FORCE_DEATH_ANIM_ON_KILL", false) ||
        burn.death_anim_play_attempted ||
        !burn.target ||
        !probable_heap_object(burn.target) ||
        !xmark_burn_death_probe_api_available() ||
        (!g_mina->GameAnimPlayDir && !g_mina->GameAnimPlay)) {
        return;
    }

    char play_sequence[96]{};
    if (!xmark_read_environment_value(
            "MINA_XMARK_BURN_FORCE_DEATH_ANIM_SEQUENCE",
            play_sequence,
            sizeof(play_sequence)) ||
        !play_sequence[0]) {
        std::snprintf(play_sequence, sizeof(play_sequence), "death");
    }

    burn.death_anim_play_attempted = true;
    XMarkBurnDeathProbeSummary summary{};
    xmark_burn_death_probe_entity(
        reinterpret_cast<ycEntity *>(burn.target),
        0,
        env_uint("MINA_XMARK_BURN_DEATH_PROBE_MAX_DEPTH", 8),
        env_uint("MINA_XMARK_BURN_DEATH_PROBE_MAX_NODES", 192),
        &summary,
        true,
        play_sequence);
    if (summary.play_successes) {
        burn.death_anim_verify_step = 0;
        burn.death_anim_verify_next_ms =
            now_ms + env_uint("MINA_XMARK_BURN_FORCE_DEATH_ANIM_VERIFY_DELAY_MS", 48);
    }

    if (g_mina && env_bool("MINA_XMARK_BURN_FORCE_DEATH_ANIM_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn forced death anim sequence='%s' mode=%s target=0x%p officialCore=0x%p attempts=%u successes=%u anims=%u nowMs=%llu\n",
            play_sequence,
            summary.play_mode[0] ? summary.play_mode : "-",
            reinterpret_cast<void *>(burn.target),
            reinterpret_cast<void *>(burn.official_combat_core),
            summary.play_attempts,
            summary.play_successes,
            summary.anims,
            now_ms);
        g_mina->Log(message);
    }
}

void trace_xmark_burn_death_target(XMarkBurnEffect &burn, unsigned long long now_ms, const char *phase) {
    if (!env_bool("MINA_XMARK_BURN_DEATH_PROBE_ENABLED", true) ||
        !burn.target ||
        !probable_heap_object(burn.target) ||
        !xmark_burn_death_probe_api_available()) {
        return;
    }

    const bool is_pre = phase && std::strcmp(phase, "pre-lethal") == 0;
    const bool is_post = phase && std::strcmp(phase, "post-lethal") == 0;
    if (is_pre || is_post) {
        bool *logged = is_pre ? &burn.death_probe_pre_logged : &burn.death_probe_post_logged;
        if (*logged && !env_bool("MINA_XMARK_BURN_DEATH_PROBE_REPEAT", false)) {
            return;
        }
        *logged = true;
    }

    char play_sequence[96]{};
    if (!xmark_read_environment_value(
            "MINA_XMARK_BURN_DEATH_ANIM_PLAY_SEQUENCE",
            play_sequence,
            sizeof(play_sequence))) {
        play_sequence[0] = 0;
    }
    const bool try_play =
        env_bool("MINA_XMARK_BURN_DEATH_ANIM_TRY_PLAY", false) &&
        play_sequence[0] &&
        !burn.death_anim_play_attempted;
    if (try_play) {
        burn.death_anim_play_attempted = true;
    }

    XMarkBurnDeathProbeSummary summary{};
    xmark_burn_death_probe_entity(
        reinterpret_cast<ycEntity *>(burn.target),
        0,
        env_uint("MINA_XMARK_BURN_DEATH_PROBE_MAX_DEPTH", 8),
        env_uint("MINA_XMARK_BURN_DEATH_PROBE_MAX_NODES", 192),
        &summary,
        try_play,
        play_sequence);

    float health = 0.0f;
    if (burn.official_combat_core) {
        combat_core_health_read(burn.official_combat_core, &health);
    }
    Vec3 entity_position{};
    official_entity_world_position_read(burn.target, &entity_position);

    if (g_mina) {
        char message[1800]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn death probe phase=%s nowMs=%llu target=0x%p officialCore=0x%p hp=%.3f pos=(%.3f, %.3f, %.3f) nodes=%u components=%u anims=%u play='%s' attempts=%u successes=%u mode=%s components=[%s] anims=[%s]\n",
            phase ? phase : "<none>",
            now_ms,
            reinterpret_cast<void *>(burn.target),
            reinterpret_cast<void *>(burn.official_combat_core),
            static_cast<double>(health),
            static_cast<double>(entity_position.x),
            static_cast<double>(entity_position.y),
            static_cast<double>(entity_position.z),
            summary.nodes,
            summary.components,
            summary.anims,
            play_sequence[0] ? play_sequence : "-",
            summary.play_attempts,
            summary.play_successes,
            summary.play_mode[0] ? summary.play_mode : "-",
            summary.component_tokens[0] ? summary.component_tokens : "-",
            summary.anim_tokens[0] ? summary.anim_tokens : "-");
        g_mina->Log(message);
    }
}

bool runtime_target_from_official_enemy_host(const XMarkOfficialEnemyHost &host, XMarkRuntimeTarget *target_out) {
    if (!target_out || !host.active || !host.entity || !host.combat_core || !(host.health_max > 0.0f)) {
        return false;
    }
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina && g_mina->PlayerGetPos) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    XMarkRuntimeTarget target{};
    target.entity = host.entity;
    target.official_combat_core = host.combat_core;
    target.anchor = host.position;
    target.position = host.position;
    target.health_value = host.health;
    target.health_max = host.health_max;
    target.health_like = true;
    target.official_follow = true;
    target.render_half_w = official_enemy_render_half_from_health(host, xmark_default_render_half_w());
    target.render_half_h = official_enemy_render_half_from_health(host, xmark_default_render_half_h());
    target.contact_half_w = host.bounds_valid ? host.bounds_half_w : 0.0f;
    target.contact_half_h = host.bounds_valid ? host.bounds_half_h : 0.0f;
    const float dx = host.position.x - player_api.x;
    const float dy = host.position.y - player_api.y;
    target.distance_sq = (dx * dx) + (dy * dy);
    runtime_target_direction_delta_for_direction(target, player_api, g_last_direction, &target.forward_delta, &target.lateral_delta);
    target.facing_match = true;
    copy_visual_token(target.visual_entry, sizeof(target.visual_entry), host.component_type);
    copy_visual_token(target.visual_stem, sizeof(target.visual_stem), "official-combat-core");
    copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), "ModAPI");
    if (xmark_official_host_is_muriel(host)) {
        target.visual_follow = false;
        target.visual_key = 0;
        target.health_like = false;
        target.suppress_hud = true;
        target.render_half_w = std::max(
            0.001f,
            env_float("MINA_XMARK_MURIEL_MARK_HALF_W", xmark_default_render_half_w()));
        target.render_half_h = std::max(
            0.001f,
            env_float("MINA_XMARK_MURIEL_MARK_HALF_H", xmark_default_render_half_h()));
        copy_visual_token(target.visual_entry, sizeof(target.visual_entry), "MurielNPC");
        copy_visual_token(target.visual_stem, sizeof(target.visual_stem), "muriel");
    } else if (env_bool("MINA_XMARK_OFFICIAL_LINK_VISUAL_HOST", true) &&
        apply_nearest_visual_host_identity_to_runtime_target(
            host.position,
            &target,
            GetTickCount64(),
            "official-runtime-target")) {
        const float linked_dx = target.position.x - player_api.x;
        const float linked_dy = target.position.y - player_api.y;
        target.distance_sq = (linked_dx * linked_dx) + (linked_dy * linked_dy);
        runtime_target_direction_delta_for_direction(
            target,
            player_api,
            g_last_direction,
            &target.forward_delta,
            &target.lateral_delta);
    }
    *target_out = target;
    return true;
}

void init_paths() {
    if (g_paths_initialized) {
        return;
    }
    g_paths_initialized = true;
    copy_env_or_default("MINA_XMARK_ROOM_STATE_FILE", "xmark_room_state.txt", g_state_path, sizeof(g_state_path));
    copy_env_or_default("MINA_XMARK_HUD_STATE_FILE", "xmark_hud_state.txt", g_hud_state_path, sizeof(g_hud_state_path));
    copy_env_or_default(
        "MINA_XMARK_HUD_LAYOUT_STATE_FILE",
        "xmark_hud_layout_state.txt",
        g_hud_layout_state_path,
        sizeof(g_hud_layout_state_path));
    copy_env_or_default("MINA_XMARK_ROOM_TRACE_FILE", "xmark_room_trace.tsv", g_trace_path, sizeof(g_trace_path));
    copy_env_or_default("MINA_XMARK_BASIC_FRAME_STATE_FILE", "xmark_basic_frame_state.txt", g_basic_frame_state_path, sizeof(g_basic_frame_state_path));
    copy_env_or_default("MINA_XMARK_BASIC_HIT_STATE_FILE", "xmark_basic_hit_state.txt", g_basic_hit_state_path, sizeof(g_basic_hit_state_path));
    copy_env_or_default("MINA_XMARK_ENEMY_VISUAL_STATE_FILE", "xmark_enemy_visual_state.tsv", g_enemy_visual_state_path, sizeof(g_enemy_visual_state_path));
    copy_env_or_default("MINA_XMARK_COMMAND_FILE", "xmark_command.txt", g_command_path, sizeof(g_command_path));
    g_write_interval_ms = env_uint("MINA_XMARK_ROOM_STATE_WRITE_MS", 1000);
    if (g_write_interval_ms < 250) {
        g_write_interval_ms = 250;
    }
    g_hud_write_interval_ms = env_uint("MINA_XMARK_HUD_STATE_WRITE_MS", 80);
    if (g_hud_write_interval_ms < 40) {
        g_hud_write_interval_ms = 40;
    }
    g_trace_pseudo_rooms = env_bool("MINA_XMARK_ROOM_TRACE_PSEUDO", false);
    g_room_state_write_enabled = env_bool("MINA_XMARK_ROOM_STATE_WRITE_ENABLED", true);
    g_command_file_enabled = env_bool("MINA_XMARK_COMMAND_FILE_ENABLED", true);
    g_debug_keys_enabled = env_bool("MINA_XMARK_DEBUG_KEYS_ENABLED", false);
    g_attachment_lab_enabled = env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false);
    g_force_default_claymore_enabled = env_bool("MINA_XMARK_FORCE_CLAYMORE_DEFAULT", false);
    g_player_world_gate_enabled = env_bool("MINA_XMARK_PLAYER_WORLD_GATE", true);
    g_use_world_update_attachment = env_bool("MINA_XMARK_USE_WORLD_UPDATE_ATTACHMENT", true);
    g_use_entity_post_art_queue = env_bool("MINA_XMARK_USE_ENTITY_POST_ART_QUEUE", true);
    g_use_entity_hit_update_queue = env_bool("MINA_XMARK_USE_ENTITY_HIT_UPDATE_QUEUE", true);
    g_hit_queue_owns_basic_health = env_bool("MINA_XMARK_HIT_QUEUE_OWNS_BASIC_HEALTH", true);
    g_use_hud_update_queue = env_bool("MINA_XMARK_USE_HUD_UPDATE_QUEUE", true);
    g_hud_layout_reporter_enabled = env_bool("MINA_XMARK_HUD_LAYOUT_REPORTER_ENABLED", false);
    g_official_scan_in_world_update = env_bool("MINA_XMARK_OFFICIAL_SCAN_IN_WORLD_UPDATE", false);
    g_post_art_refresh_official_snapshot = env_bool("MINA_XMARK_POST_ART_REFRESH_OFFICIAL_SNAPSHOT", false);
    g_marker_debug_draw_warmup = env_bool("MINA_XMARK_MARK_DEBUG_DRAW_WARMUP", true);
    g_burn_debug_draw_warmup = env_bool("MINA_XMARK_BURN_DEBUG_DRAW_WARMUP", true);
    g_native_spawn_enabled = env_bool("MINA_XMARK_NATIVE_SPAWN_ENABLED", true);
    g_native_auto_spawn = env_bool("MINA_XMARK_NATIVE_AUTO_SPAWN", false);
    g_native_spawn_limit = env_uint("MINA_XMARK_NATIVE_SPAWN_LIMIT", 4096);
    g_native_direct_enabled = env_bool("MINA_XMARK_NATIVE_DIRECT_ENABLED", true);
    g_integrated_f0029_enabled = env_bool("MINA_XMARK_INTEGRATED_F0029_ENABLED", false);
    g_native_crater_enabled = env_bool("MINA_XMARK_NATIVE_CRATER_ENABLED", false);
    g_xmark_render_backend_enabled = env_bool("MINA_XMARK_RENDER_BACKEND_ENABLED", true);
    g_xmark_render_backend_required = env_bool("MINA_XMARK_RENDER_BACKEND_REQUIRED", false);
    g_test_enemy_harness_enabled = env_bool("MINA_XMARK_TEST_ENEMY_HARNESS_ENABLED", false);
    g_test_enemy_auto_spawn = env_bool("MINA_XMARK_TEST_ENEMY_AUTO_SPAWN", false);
    g_test_enemy_type = env_uint("MINA_XMARK_TEST_ENEMY_TYPE", ENTITYTYPE_TEST_TRAINING_DUMMY);
    g_test_runtime_enabled =
        g_test_enemy_harness_enabled ||
        g_attachment_lab_enabled ||
        env_bool("MINA_XMARK_DISABLE_SAVE_WRITES_DURING_TEST", false);
    g_integrated_f0029_cooldown_ms = env_uint("MINA_XMARK_INTEGRATED_F0029_COOLDOWN_MS", 160);
    if (g_integrated_f0029_cooldown_ms < 50) {
        g_integrated_f0029_cooldown_ms = 50;
    }

    if (g_mina) {
        char message[MAX_PATH * 8]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn room/native paths: state='%s' hudState='%s' trace='%s' visualState='%s' intervalMs=%u hudIntervalMs=%u tracePseudo=%u spawnEnabled=%u autoSpawn=%u limit=%u directEnabled=%u integratedF0029=%u nativeCrater=%u renderBackend=%u renderRequired=%u f0029CooldownMs=%u testEnemyHarness=%u testEnemyAuto=%u testEnemyType=%u\n",
            g_state_path[0] ? g_state_path : "<unset>",
            g_hud_state_path[0] ? g_hud_state_path : "<unset>",
            g_trace_path[0] ? g_trace_path : "<unset>",
            g_enemy_visual_state_path[0] ? g_enemy_visual_state_path : "<unset>",
            g_write_interval_ms,
            g_hud_write_interval_ms,
            g_trace_pseudo_rooms ? 1u : 0u,
            g_native_spawn_enabled ? 1u : 0u,
            g_native_auto_spawn ? 1u : 0u,
            g_native_spawn_limit,
            g_native_direct_enabled ? 1u : 0u,
            g_integrated_f0029_enabled ? 1u : 0u,
            g_native_crater_enabled ? 1u : 0u,
            g_xmark_render_backend_enabled ? 1u : 0u,
            g_xmark_render_backend_required ? 1u : 0u,
            g_integrated_f0029_cooldown_ms,
            g_test_enemy_harness_enabled ? 1u : 0u,
            g_test_enemy_auto_spawn ? 1u : 0u,
            g_test_enemy_type);
        g_mina->Log(message);
    }
}

bool clashrend_write_memory(void *target, const void *source, size_t size) {
    if (!target || !source || !size) {
        return false;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(target, size, PAGE_READWRITE, &old_protection)) {
        return false;
    }
    bool wrote = false;
    __try {
        std::memcpy(target, source, size);
        wrote = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wrote = false;
    }
    DWORD ignored = 0;
    VirtualProtect(target, size, old_protection, &ignored);
    return wrote;
}

struct ClashrendDamageContactPatch {
    const char *name;
    float stock_value;
    float target_value;
};

bool clashrend_find_main_module_section(
    const char *section_name,
    uintptr_t *begin_out,
    uintptr_t *end_out) {
    if (!section_name || !begin_out || !end_out) {
        return false;
    }
    const uintptr_t base = exe_base();
    if (!base) {
        return false;
    }
    __try {
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) {
            return false;
        }
        const IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (unsigned int index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
            char name[IMAGE_SIZEOF_SHORT_NAME + 1]{};
            std::memcpy(name, section[index].Name, IMAGE_SIZEOF_SHORT_NAME);
            if (std::strcmp(name, section_name) != 0) {
                continue;
            }
            const size_t size = std::max<size_t>(
                section[index].Misc.VirtualSize,
                section[index].SizeOfRawData);
            *begin_out = base + section[index].VirtualAddress;
            *end_out = *begin_out + size;
            return size != 0u;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

uintptr_t clashrend_find_module_ascii_string(const char *text) {
    if (!text || !text[0]) {
        return 0;
    }
    const uintptr_t base = exe_base();
    const unsigned int image_size = exe_image_size();
    if (!base || !image_size) {
        return 0;
    }
    const size_t length = std::strlen(text) + 1u;
    const auto *begin = reinterpret_cast<const unsigned char *>(base);
    const auto *end = begin + image_size;
    const auto *needle = reinterpret_cast<const unsigned char *>(text);
    const unsigned char *match = nullptr;
    __try {
        match = std::search(begin, end, needle, needle + length);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        match = end;
    }
    return match == end ? 0u : reinterpret_cast<uintptr_t>(match);
}

uintptr_t clashrend_find_damage_contact_record(const char *contact_name) {
    const uintptr_t string_address = clashrend_find_module_ascii_string(contact_name);
    if (!string_address) {
        return 0;
    }
    uintptr_t data_begin = 0;
    uintptr_t data_end = 0;
    if (!clashrend_find_main_module_section(".data", &data_begin, &data_end)) {
        return 0;
    }
    uintptr_t found = 0;
    for (uintptr_t cursor = data_begin;
         cursor + 0x18u <= data_end;
         cursor += sizeof(uintptr_t)) {
        uintptr_t name_pointer = 0;
        uintptr_t sentinel = 0;
        __try {
            name_pointer = *reinterpret_cast<const uintptr_t *>(cursor);
            sentinel = *reinterpret_cast<const uintptr_t *>(cursor + 8u);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (name_pointer != string_address || sentinel != UINTPTR_MAX) {
            continue;
        }
        if (found) {
            return 0;
        }
        found = cursor;
    }
    return found;
}

void clashrend_apply_runtime_damage_values() {
    if (!env_bool("MINA_XMARK_RUNTIME_DAMAGE_PATCH_ENABLED", true)) {
        return;
    }
    const ClashrendDamageContactPatch contacts[] = {
        {"PlayerHammerSmack", 3.0f,
            env_float("MINA_XMARK_DAMAGE_BASIC", 2.5f)},
        {"PlayerHammerCharge", 11.0f,
            env_float("MINA_XMARK_DAMAGE_CHARGED", 5.0f)},
        {"PlayerHammerChargeFull", 10.0f,
            env_float("MINA_XMARK_DAMAGE_FULL_CHARGED", 5.0f)},
        {"PlayerHammerShellBlast", 10.0f,
            env_float("MINA_XMARK_DAMAGE_CROSS_BLAST_CENTER", 7.5f)},
        {"PlayerHammerShellShockwave", 8.0f,
            env_float("MINA_XMARK_DAMAGE_CROSS_BLAST_OUTER", 6.0f)},
    };
    unsigned int verified = 0;
    unsigned int patched = 0;
    for (const ClashrendDamageContactPatch &contact : contacts) {
        const uintptr_t record = clashrend_find_damage_contact_record(contact.name);
        float current = 0.0f;
        if (!record || !safe_read_float(record + 0x10u, &current)) {
            continue;
        }
        const bool known_value =
            std::fabs(current - contact.stock_value) <= 0.0001f ||
            std::fabs(current - contact.target_value) <= 0.0001f;
        if (!known_value) {
            continue;
        }
        ++verified;
        if (std::fabs(current - contact.target_value) <= 0.0001f) {
            continue;
        }
        if (clashrend_write_memory(
                reinterpret_cast<void *>(record + 0x10u),
                &contact.target_value,
                sizeof(contact.target_value))) {
            ++patched;
        }
    }
    if (g_mina) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn damage contacts verified=%u/%u patched=%u.\n",
            verified,
            static_cast<unsigned int>(_countof(contacts)),
            patched);
        g_mina->Log(message);
    }
}

bool clashrend_patch_runtime_text_key(
    uintptr_t region_base,
    size_t region_size,
    const char *key,
    const char *replacement) {
    if (!region_base || !region_size || !key || !replacement) {
        return false;
    }
    const size_t key_length = std::strlen(key);
    const size_t replacement_length = std::strlen(replacement);
    const char *begin = reinterpret_cast<const char *>(region_base);
    const char *end = begin + region_size;
    const char *cursor = begin;
    while (cursor + key_length + 1u <= end) {
        const char *match = std::search(cursor, end, key, key + key_length);
        if (match == end || match + key_length >= end) {
            break;
        }
        if (match[key_length] != 0) {
            cursor = match + 1;
            continue;
        }
        const uintptr_t match_address = reinterpret_cast<uintptr_t>(match);
        const uintptr_t search_floor = std::max(
            region_base,
            match_address > 512u ? match_address - 512u : region_base);
        uintptr_t entry = match_address >= 16u ? match_address - 16u : 0;
        for (; entry >= search_floor && entry + 16u <= region_base + region_size; entry -= 4u) {
            bool key_entry_matches = false;
            __try {
                key_entry_matches =
                    *reinterpret_cast<const uint32_t *>(entry) == 0u &&
                    *reinterpret_cast<const uint32_t *>(entry + 4u) == key_length &&
                    *reinterpret_cast<const uint64_t *>(entry + 8u) == match_address;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                key_entry_matches = false;
            }
            if (key_entry_matches) {
                const uintptr_t donor_entry = entry + 7u * 16u;
                if (donor_entry + 16u > region_base + region_size ||
                    entry + 12u * 16u > region_base + region_size) {
                    break;
                }
                uint32_t donor_capacity = 0;
                uint64_t donor_target = 0;
                __try {
                    donor_capacity = *reinterpret_cast<const uint32_t *>(donor_entry + 4u) + 1u;
                    donor_target = *reinterpret_cast<const uint64_t *>(donor_entry + 8u);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    donor_capacity = 0;
                    donor_target = 0;
                }
                if (donor_target && donor_capacity >= replacement_length + 1u &&
                    clashrend_write_memory(
                        reinterpret_cast<void *>(static_cast<uintptr_t>(donor_target)),
                        replacement,
                        replacement_length + 1u)) {
                    const uint32_t new_length = static_cast<uint32_t>(replacement_length);
                    bool table_patched = true;
                    for (unsigned int slot = 1; slot < 12; ++slot) {
                        const uintptr_t language_entry = entry + slot * 16u;
                        table_patched =
                            clashrend_write_memory(
                                reinterpret_cast<void *>(language_entry + 4u),
                                &new_length,
                                sizeof(new_length)) &&
                            clashrend_write_memory(
                                reinterpret_cast<void *>(language_entry + 8u),
                                &donor_target,
                                sizeof(donor_target)) &&
                            table_patched;
                    }
                    return table_patched;
                }
                break;
            }
            if (entry < search_floor + 4u) {
                break;
            }
        }
        cursor = match + key_length + 1u;
    }
    return false;
}

void maybe_patch_clashrend_runtime_text(unsigned long long now_ms) {
    if (g_clashrend_text_patch_complete || g_clashrend_text_patch_final_miss ||
        !env_bool("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_ENABLED", true)) {
        return;
    }
    if (!g_clashrend_text_patch_next_ms) {
        g_clashrend_text_patch_next_ms = now_ms +
            env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_DELAY_MS", 1500);
        return;
    }
    if (now_ms < g_clashrend_text_patch_next_ms) {
        return;
    }
    constexpr char kNameKey[] = "hammer_name";
    constexpr char kDescriptionKey[] = "hammer_desc";
    constexpr char kUpgradeNameKey[] = "hammer_level_2_name";
    constexpr char kUpgradeDescriptionKey[] = "hammer_level_2_desc";
    constexpr char kUpgradeManualTitleKey[] = "emanual_weapons_upgrades_hammer_title";
    constexpr char kName[] = "Clashrend Claymore";
    constexpr char kDescription[] =
        "A greatsword with explosive force. Hold <b action=attack></b> to charge up. "
        "While charging, move to roll.";
    constexpr char kUpgradeName[] = "Upgrade - Cross Blast";
    constexpr char kUpgradeDescription[] =
        "Hold <b action=attack></b> to charge up a three-step explosion wave.";
    constexpr char kUpgradeManualTitle[] = "Clashrend Claymore - Cross Blast";

    if (!g_clashrend_text_patch_scan_active) {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        g_clashrend_text_patch_scan_cursor =
            reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        g_clashrend_text_patch_scan_max =
            reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        g_clashrend_text_patch_region_base = 0;
        g_clashrend_text_patch_region_cursor = 0;
        g_clashrend_text_patch_region_end = 0;
        g_clashrend_text_patch_scan_active = true;
        ++g_clashrend_text_patch_attempts;
    }

    const size_t max_region_size = std::max<size_t>(
        1024u * 1024u,
        env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_MAX_REGION_MB", 32) * 1024ull * 1024ull);
    const size_t chunk_size = std::max<size_t>(
        64u * 1024u,
        env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_CHUNK_KB", 256) * 1024ull);
    const unsigned int chunk_budget = std::max(
        1u,
        env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_CHUNKS_PER_TICK", 1));
    const unsigned int query_budget = std::max(
        8u,
        env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_QUERIES_PER_TICK", 96));
    unsigned int chunks_scanned = 0;
    unsigned int queries = 0;

    while ((g_clashrend_text_patch_region_cursor < g_clashrend_text_patch_region_end ||
            g_clashrend_text_patch_scan_cursor < g_clashrend_text_patch_scan_max) &&
        chunks_scanned < chunk_budget && queries < query_budget &&
        (!g_clashrend_text_patch_name || !g_clashrend_text_patch_description ||
         !g_clashrend_text_patch_upgrade_name ||
         !g_clashrend_text_patch_upgrade_description ||
         !g_clashrend_text_patch_upgrade_manual_title)) {
        if (!g_clashrend_text_patch_region_cursor ||
            g_clashrend_text_patch_region_cursor >= g_clashrend_text_patch_region_end) {
            MEMORY_BASIC_INFORMATION mbi{};
            ++queries;
            if (!VirtualQuery(
                    reinterpret_cast<const void *>(g_clashrend_text_patch_scan_cursor),
                    &mbi,
                    sizeof(mbi))) {
                g_clashrend_text_patch_scan_cursor = g_clashrend_text_patch_scan_max;
                break;
            }
            const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            const size_t size = mbi.RegionSize;
            const uintptr_t next = base + size;
            if (next <= g_clashrend_text_patch_scan_cursor) {
                g_clashrend_text_patch_scan_cursor = g_clashrend_text_patch_scan_max;
                break;
            }
            g_clashrend_text_patch_scan_cursor = next;
            const DWORD protection =
                mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
            const bool writable_private =
                mbi.State == MEM_COMMIT &&
                mbi.Type == MEM_PRIVATE &&
                (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY) &&
                size >= 0x10000u && size <= max_region_size;
            if (!writable_private) {
                continue;
            }
            g_clashrend_text_patch_region_base = base;
            g_clashrend_text_patch_region_cursor = base;
            g_clashrend_text_patch_region_end = next;
        }

        const uintptr_t chunk_begin = g_clashrend_text_patch_region_cursor;
        const uintptr_t chunk_end = std::min(
            g_clashrend_text_patch_region_end,
            chunk_begin + static_cast<uintptr_t>(chunk_size));
        const uintptr_t scan_begin = chunk_begin > g_clashrend_text_patch_region_base + 768u
            ? chunk_begin - 768u
            : g_clashrend_text_patch_region_base;
        const uintptr_t scan_end = std::min(
            g_clashrend_text_patch_region_end,
            chunk_end + static_cast<uintptr_t>(768u));
        const size_t scan_size = static_cast<size_t>(scan_end - scan_begin);
        __try {
            if (!g_clashrend_text_patch_name) {
                g_clashrend_text_patch_name =
                    clashrend_patch_runtime_text_key(scan_begin, scan_size, kNameKey, kName);
            }
            if (!g_clashrend_text_patch_description) {
                g_clashrend_text_patch_description = clashrend_patch_runtime_text_key(
                    scan_begin, scan_size, kDescriptionKey, kDescription);
            }
            if (!g_clashrend_text_patch_upgrade_name) {
                g_clashrend_text_patch_upgrade_name = clashrend_patch_runtime_text_key(
                    scan_begin, scan_size, kUpgradeNameKey, kUpgradeName);
            }
            if (!g_clashrend_text_patch_upgrade_description) {
                g_clashrend_text_patch_upgrade_description = clashrend_patch_runtime_text_key(
                    scan_begin, scan_size, kUpgradeDescriptionKey, kUpgradeDescription);
            }
            if (!g_clashrend_text_patch_upgrade_manual_title) {
                g_clashrend_text_patch_upgrade_manual_title = clashrend_patch_runtime_text_key(
                    scan_begin, scan_size, kUpgradeManualTitleKey, kUpgradeManualTitle);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        g_clashrend_text_patch_region_cursor = chunk_end;
        ++chunks_scanned;
    }

    g_clashrend_text_patch_complete =
        g_clashrend_text_patch_name && g_clashrend_text_patch_description &&
        g_clashrend_text_patch_upgrade_name &&
        g_clashrend_text_patch_upgrade_description &&
        g_clashrend_text_patch_upgrade_manual_title;
    const bool scan_finished =
        g_clashrend_text_patch_scan_cursor >= g_clashrend_text_patch_scan_max &&
        g_clashrend_text_patch_region_cursor >= g_clashrend_text_patch_region_end;
    if (!g_clashrend_text_patch_complete && !scan_finished) {
        return;
    }

    g_clashrend_text_patch_scan_active = false;
    g_clashrend_text_patch_region_base = 0;
    g_clashrend_text_patch_region_cursor = 0;
    g_clashrend_text_patch_region_end = 0;
    const unsigned int max_attempts = std::max(
        1u,
        env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_MAX_ATTEMPTS", 2));
    if (!g_clashrend_text_patch_complete && g_clashrend_text_patch_attempts >= max_attempts) {
        g_clashrend_text_patch_final_miss = true;
    } else if (!g_clashrend_text_patch_complete) {
        g_clashrend_text_patch_next_ms = now_ms +
            env_uint("MINA_XMARK_RUNTIME_EQUIPMENT_TEXT_PATCH_RETRY_MS", 2000);
    }
    if (g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn equipment text patch name=%u description=%u upgradeName=%u upgradeDescription=%u manualTitle=%u attempt=%u final=%u.\n",
            g_clashrend_text_patch_name ? 1u : 0u,
            g_clashrend_text_patch_description ? 1u : 0u,
            g_clashrend_text_patch_upgrade_name ? 1u : 0u,
            g_clashrend_text_patch_upgrade_description ? 1u : 0u,
            g_clashrend_text_patch_upgrade_manual_title ? 1u : 0u,
            g_clashrend_text_patch_attempts,
            g_clashrend_text_patch_final_miss ? 1u : 0u);
        g_mina->Log(message);
    }
}

bool clashrend_weapon_shadow_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, WorldGetMenuRootEntity)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimInit)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimPlay)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqNameNoDir)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqFrameIdx)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetSeqFrameIdx));
}

bool repair_clashrend_weapon_shadow_entity(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes) {
    if (!entity || !nodes || depth > max_depth || *nodes >= max_nodes) {
        return false;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    if (!child_count) {
        return false;
    }
    const size_t child_cap = std::min<size_t>(child_count, 128u);
    ycComponent **children = static_cast<ycComponent **>(
        g_mina->Alloc(sizeof(ycComponent *) * child_cap));
    if (!children) {
        return false;
    }
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, child_cap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }
    bool repaired = false;
    const size_t limit = std::min(read_count, child_cap);
    for (size_t i = 0; i < limit && !repaired && *nodes < max_nodes; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        bool is_entity = false;
        bool is_game_anim = false;
        __try {
            is_entity = g_mina->ComponentIsa(component, g_official_entity_rtti);
            is_game_anim = g_mina->ComponentIsa(component, g_game_anim_rtti);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            is_entity = false;
            is_game_anim = false;
        }
        if (is_entity) {
            repaired = repair_clashrend_weapon_shadow_entity(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                nodes,
                max_nodes);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }
        char sequence[64]{};
        uint32_t frame = 0;
        bool paused = false;
        bool visible = true;
        __try {
            copy_mm_string_to_cstr(
                sequence,
                sizeof(sequence),
                g_mina->GameAnimGetSeqNameNoDir(component));
            frame = g_mina->GameAnimGetSeqFrameIdx(component);
            paused = g_mina->GameAnimIsPaused(component);
            if (api_function_field_ready(offsetof(MinaModAPI, GameAnimIsVisible))) {
                visible = g_mina->GameAnimIsVisible(component);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sequence[0] = 0;
        }
        if (_stricmp(sequence, "hammerShadow") != 0) {
            continue;
        }
        MM_Transform local_transform{};
        const bool preserve_transform =
            api_function_field_ready(offsetof(MinaModAPI, GameAnimGetLocalTransform)) &&
            api_function_field_ready(offsetof(MinaModAPI, GameAnimSetLocalTransform));
        __try {
            if (preserve_transform) {
                g_mina->GameAnimGetLocalTransform(component, &local_transform);
            }
            g_mina->GameAnimInit(component, "UI/weaponChest/weaponSelect.anb.yc", nullptr);
            g_mina->GameAnimPlay(component, "hammerShadow", -1, 1.0f, true);
            g_mina->GameAnimSetSeqFrameIdx(component, frame);
            g_mina->GameAnimSetPaused(component, paused);
            if (api_function_field_ready(offsetof(MinaModAPI, GameAnimSetVisible))) {
                g_mina->GameAnimSetVisible(component, visible);
            }
            if (preserve_transform) {
                g_mina->GameAnimSetLocalTransform(component, &local_transform);
            }
            repaired = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            repaired = false;
        }
        if (repaired) {
            xmark_weak_ptr_destroy(g_weapon_shadow_anim_weak);
            g_weapon_shadow_anim_weak = xmark_weak_ptr_create(component);
            g_weapon_shadow_anim_component = reinterpret_cast<uintptr_t>(component);
        }
    }
    g_mina->Free(children);
    return repaired;
}

void maybe_repair_clashrend_weapon_shadow(World *world, unsigned long long now_ms) {
    if (!world || !env_bool("MINA_XMARK_WEAPON_SHADOW_MENU_REPAIR_ENABLED", true) ||
        !clashrend_weapon_shadow_api_available() ||
        !official_enemy_init_rtti() || !xmark_game_anim_init_rtti()) {
        return;
    }
    if (g_weapon_shadow_anim_weak && xmark_weak_ptr_get(g_weapon_shadow_anim_weak)) {
        return;
    }
    if (g_weapon_shadow_anim_weak) {
        xmark_weak_ptr_destroy(g_weapon_shadow_anim_weak);
        g_weapon_shadow_anim_weak = nullptr;
        g_weapon_shadow_anim_component = 0;
    }
    const unsigned int scan_ms = std::max(
        250u,
        env_uint("MINA_XMARK_WEAPON_SHADOW_MENU_REPAIR_SCAN_MS", 500));
    if (g_weapon_shadow_last_scan_ms && now_ms < g_weapon_shadow_last_scan_ms + scan_ms) {
        return;
    }
    g_weapon_shadow_last_scan_ms = now_ms;
    ycEntity *menu_root = nullptr;
    __try {
        menu_root = g_mina->WorldGetMenuRootEntity(world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        menu_root = nullptr;
    }
    if (!menu_root) {
        return;
    }
    unsigned int nodes = 0;
    if (repair_clashrend_weapon_shadow_entity(
            menu_root,
            0,
            8,
            &nodes,
            env_uint("MINA_XMARK_WEAPON_SHADOW_MENU_REPAIR_MAX_NODES", 256)) &&
        g_mina) {
        g_mina->Log("XMarkBurn refreshed the Equipment Box hammerShadow from the Clashrend asset.\n");
    }
}

