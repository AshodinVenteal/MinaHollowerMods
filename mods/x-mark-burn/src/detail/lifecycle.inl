void maybe_auto_spawn(int game_state, unsigned int room_index, float room_time) {
    if (!g_native_auto_spawn || g_native_auto_spawn_done || !g_native_spawn_enabled) {
        return;
    }
    if (is_pseudo_room(room_index) || game_state <= 0 || room_time < 1.0f) {
        return;
    }
    g_native_auto_spawn_done = true;
    spawn_entity_probe(ENTITYTYPE_ANIM_EFFECT, "auto-room-enter");
}

void maybe_disable_save_writes_for_test() {
    if (g_test_save_writes_disabled ||
        !env_bool("MINA_XMARK_DISABLE_SAVE_WRITES_DURING_TEST", false) ||
        !g_mina ||
        !g_mina->SetSaveWriteEnabled) {
        return;
    }
    g_test_save_writes_disabled = true;
    __try {
        g_mina->SetSaveWriteEnabled(false);
        if (g_mina) {
            g_mina->Log("XMarkBurn disabled save writes for test harness.\n");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_mina) {
            g_mina->Log("XMarkBurn SetSaveWriteEnabled(false) raised an exception.\n");
        }
    }
}

void maybe_attachment_lab_auto_start_save(unsigned long long now_ms, int game_state) {
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) ||
        !env_bool("MINA_XMARK_ATTACHMENT_LAB_AUTO_START_SAVE", false) ||
        g_attachment_lab_auto_start_save_done ||
        !g_mina ||
        !g_mina->StartActiveSaveSlot) {
        return;
    }
    if (!g_attachment_lab_first_update_ms) {
        g_attachment_lab_first_update_ms = now_ms;
        return;
    }
    if (gameplay_state_can_spawn_test_enemy(game_state) && player_entity() && entity_manager()) {
        g_attachment_lab_auto_start_save_done = true;
        return;
    }
    const int required_state = static_cast<int>(env_uint("MINA_XMARK_ATTACHMENT_LAB_AUTO_START_REQUIRED_STATE", 0));
    if (required_state > 0 && game_state != required_state) {
        return;
    }
    const unsigned int delay_ms = env_uint("MINA_XMARK_ATTACHMENT_LAB_AUTO_START_DELAY_MS", 1000);
    if (now_ms - g_attachment_lab_first_update_ms < delay_ms) {
        return;
    }

    g_attachment_lab_auto_start_save_done = true;
    g_attachment_lab_auto_start_save_ms = now_ms;
    if (env_bool("MINA_XMARK_ATTACHMENT_LAB_SET_ACTIVE_SAVE_SLOT_ENABLED", false) &&
        g_mina->SetActiveSaveSlot) {
        const unsigned int slot = env_uint("MINA_XMARK_ATTACHMENT_LAB_ACTIVE_SAVE_SLOT", 0);
        if (g_mina) {
            char message[192]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn attachment lab setting active save slot %u before start.\n",
                slot);
            g_mina->Log(message);
        }
        __try {
            g_mina->SetActiveSaveSlot(slot);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_mina) {
                g_mina->Log("XMarkBurn attachment lab SetActiveSaveSlot raised an exception.\n");
            }
        }
    }
    if (g_mina) {
        g_mina->Log("XMarkBurn attachment lab calling MinaModAPI::StartActiveSaveSlot.\n");
    }
    __try {
        g_mina->StartActiveSaveSlot();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_mina) {
            g_mina->Log("XMarkBurn attachment lab StartActiveSaveSlot raised an exception.\n");
        }
    }
}

void maybe_attachment_lab_force_game_state(unsigned long long now_ms, int game_state) {
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) ||
        g_attachment_lab_force_game_state_done ||
        !g_mina ||
        !g_mina->TransitionToGameState) {
        return;
    }
    const int target_state = static_cast<int>(env_uint("MINA_XMARK_ATTACHMENT_LAB_FORCE_GAMESTATE", 0));
    if (target_state <= 0) {
        return;
    }
    if (!g_attachment_lab_first_update_ms) {
        g_attachment_lab_first_update_ms = now_ms;
        return;
    }
    if (gameplay_state_can_spawn_test_enemy(game_state) && player_entity() && entity_manager()) {
        g_attachment_lab_force_game_state_done = true;
        return;
    }
    const unsigned int delay_ms = env_uint("MINA_XMARK_ATTACHMENT_LAB_FORCE_GAMESTATE_DELAY_MS", 1800);
    if (now_ms - g_attachment_lab_first_update_ms < delay_ms) {
        return;
    }
    const unsigned int after_auto_start_ms =
        env_uint("MINA_XMARK_ATTACHMENT_LAB_FORCE_AFTER_AUTOSTART_MS", 3000);
    if (g_attachment_lab_auto_start_save_ms &&
        now_ms - g_attachment_lab_auto_start_save_ms < after_auto_start_ms) {
        return;
    }
    const int trigger_state = static_cast<int>(env_uint("MINA_XMARK_ATTACHMENT_LAB_FORCE_GAMESTATE_TRIGGER", 0));
    if (trigger_state > 0 && game_state != trigger_state) {
        return;
    }

    g_attachment_lab_force_game_state_done = true;
    if (env_bool("MINA_XMARK_ATTACHMENT_LAB_RESTORE_PLAYER_BEFORE_TRANSITION", true) &&
        g_mina->PlayerRestoreFromSave) {
        __try {
            g_mina->PlayerRestoreFromSave();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_mina) {
                g_mina->Log("XMarkBurn attachment lab PlayerRestoreFromSave raised an exception.\n");
            }
        }
    }
    if (g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn attachment lab forcing game state %d from state %d.\n",
            target_state,
            game_state);
        g_mina->Log(message);
    }
    __try {
        g_mina->TransitionToGameState(target_state);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (g_mina) {
            g_mina->Log("XMarkBurn attachment lab TransitionToGameState raised an exception.\n");
        }
    }
}

void maybe_spawn_test_enemy_harness(int game_state, unsigned int room_index, float room_time) {
    if (!g_test_enemy_harness_enabled) {
        return;
    }

    const bool manual_hotkeys_allowed = debug_hotkeys_allowed();
    if (!manual_hotkeys_allowed) {
        g_win_m_was_down = false;
        g_win_n_was_down = false;
    }
    const bool manual_spawn =
        manual_hotkeys_allowed && debug_key_pressed('N', &g_win_n_was_down);
    const bool ready = test_enemy_spawn_context_ready(game_state, room_index, room_time);
    if (manual_spawn && !ready && g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn test enemy spawn skipped: context not ready state=%d room=%u roomTime=%.3f.\n",
            game_state,
            room_index,
            static_cast<double>(room_time));
        g_mina->Log(message);
    }
    if (!ready) {
        return;
    }

    if (env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_ENABLED", true) &&
        manual_hotkeys_allowed &&
        debug_key_pressed('M', &g_win_m_was_down)) {
        mark_last_test_enemy_for_attachment_lab("key-M-last-test-enemy");
        return;
    }

    if (manual_spawn) {
        const unsigned long long now_ms = GetTickCount64();
        const unsigned int cooldown_ms = env_uint("MINA_XMARK_TEST_ENEMY_MANUAL_SPAWN_COOLDOWN_MS", 650);
        if (g_last_test_enemy_manual_spawn_ms &&
            now_ms - g_last_test_enemy_manual_spawn_ms < cooldown_ms) {
            if (g_mina && env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_SPAWN_DEBOUNCE_LOG", true)) {
                char message[192]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn test enemy manual spawn ignored inside debounce window cooldownMs=%u.\n",
                    cooldown_ms);
                g_mina->Log(message);
            }
            return;
        }
        g_last_test_enemy_manual_spawn_ms = now_ms;
        spawn_test_enemy_near_mina("key-N-test-enemy");
        return;
    }
    if (g_test_enemy_auto_spawn && !g_test_enemy_auto_spawn_done) {
        g_test_enemy_auto_spawn_done = true;
        spawn_test_enemy_near_mina("auto-test-enemy");
    }
}

bool xmark_post_art_queue_should_own_attachment_updates(unsigned long long now_ms) {
    const unsigned int fallback_ms = env_uint("MINA_XMARK_POST_ART_QUEUE_FALLBACK_MS", 250);
    const bool recent_frame_update = g_last_xmark_post_art_update_ms &&
        (fallback_ms == 0 || now_ms <= g_last_xmark_post_art_update_ms + fallback_ms);
    if (g_use_world_update_attachment && recent_frame_update) {
        return true;
    }
    return g_use_entity_post_art_queue &&
        g_xmark_entity_post_art_queue_registered &&
        recent_frame_update;
}

bool xmark_entity_hit_queue_should_own_basic_health_updates(unsigned long long now_ms) {
    if (!g_use_entity_hit_update_queue ||
        !g_hit_queue_owns_basic_health ||
        !g_xmark_entity_hit_queue_registered ||
        !g_last_xmark_entity_hit_update_ms) {
        return false;
    }
    const unsigned int fallback_ms =
        std::max(16u, env_uint("MINA_XMARK_HIT_QUEUE_FALLBACK_MS", 100));
    return now_ms <= g_last_xmark_entity_hit_update_ms + fallback_ms;
}

void xmark_entity_hit_queue_update(void *userData) {
    World *world = static_cast<World *>(userData);
    if (!g_mina || !xmark_world_matches_player_world(world)) {
        return;
    }
    const unsigned long long now_ms = GetTickCount64();
    g_last_world = world;
    g_last_xmark_entity_hit_update_ms = now_ms;
    ++g_xmark_entity_hit_queue_callbacks;

    if (!g_hit_queue_owns_basic_health ||
        !g_basic_attack_probe.active ||
        !g_basic_attack_probe.contact_active ||
        xmark_runtime_overlays_hidden_for_pause()) {
        return;
    }
    if (env_bool("MINA_XMARK_EVENT_ARMED_HEALTH_CHECKS", true) &&
        (!g_basic_attack_probe.health_check_armed ||
         now_ms < g_basic_attack_probe.next_health_check_ms)) {
        return;
    }
    const unsigned int probe_interval_ms = std::max(
        1u,
        env_uint("MINA_XMARK_EVENT_HEALTH_CHECK_INTERVAL_MS", 8));
    g_last_xmark_entity_hit_probe_update_ms = now_ms;

    ++g_xmark_entity_hit_queue_probe_updates;
    ModPerfScope health_scope(ModPerfStage::BasicHealth, mod_perf_enabled());
    maybe_apply_basic_attack_health_probe(now_ms);
    if (env_bool("MINA_XMARK_EVENT_ARMED_HEALTH_CHECKS", true)) {
        ++g_basic_attack_probe.health_check_count;
        g_basic_attack_probe.next_health_check_ms = now_ms + probe_interval_ms;
        const unsigned int max_checks = std::max(
            1u,
            env_uint("MINA_XMARK_EVENT_HEALTH_CHECK_MAX", 32));
        if (!g_basic_attack_probe.active ||
            now_ms >= g_basic_attack_probe.expires_ms ||
            g_basic_attack_probe.health_check_count >= max_checks) {
            g_basic_attack_probe.health_check_armed = false;
        }
    }
}

void remove_xmark_entity_hit_update_queue() {
    if (g_xmark_entity_hit_queue_handle &&
        g_mina &&
        xmark_entity_hit_update_queue_api_available()) {
        __try {
            g_mina->UpdateQueueRemove(g_xmark_entity_hit_queue_handle);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_xmark_entity_hit_queue_handle = nullptr;
    g_xmark_entity_hit_queue_world = nullptr;
    g_xmark_entity_hit_queue_registered = false;
    g_last_xmark_entity_hit_update_ms = 0;
    g_last_xmark_entity_hit_probe_update_ms = 0;
}

void ensure_xmark_entity_hit_update_queue(World *world, unsigned long long now_ms) {
    if (!g_use_entity_hit_update_queue) {
        remove_xmark_entity_hit_update_queue();
        return;
    }
    if (!world || !xmark_world_matches_player_world(world)) {
        return;
    }
    if (g_xmark_entity_hit_queue_registered &&
        g_xmark_entity_hit_queue_world == world) {
        return;
    }
    if (!xmark_entity_hit_update_queue_api_available()) {
        if (!g_xmark_entity_hit_queue_failed && g_mina) {
            g_mina->Log("XMarkBurn entity-hit update queue unavailable; keeping FixedUpdate HP fallback.\n");
        }
        g_xmark_entity_hit_queue_failed = true;
        return;
    }

    const unsigned int retry_ms =
        std::max(250u, env_uint("MINA_XMARK_HIT_QUEUE_REGISTER_RETRY_MS", 1000));
    if (g_last_xmark_entity_hit_register_attempt_ms &&
        now_ms < g_last_xmark_entity_hit_register_attempt_ms + retry_ms) {
        return;
    }
    g_last_xmark_entity_hit_register_attempt_ms = now_ms;
    remove_xmark_entity_hit_update_queue();

    ycUpdateQueue *queue = nullptr;
    __try {
        queue = g_mina->WorldGetEntityHitUpdateQueue(world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        queue = nullptr;
    }
    if (!queue) {
        if (!g_xmark_entity_hit_queue_failed && g_mina) {
            g_mina->Log("XMarkBurn entity-hit update queue is not ready; keeping FixedUpdate HP fallback.\n");
        }
        g_xmark_entity_hit_queue_failed = true;
        return;
    }

    void *handle = nullptr;
    __try {
        handle = g_mina->UpdateQueueAdd(queue, xmark_entity_hit_queue_update, world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        handle = nullptr;
    }
    if (!handle) {
        if (!g_xmark_entity_hit_queue_failed && g_mina) {
            g_mina->Log("XMarkBurn entity-hit update queue registration failed; keeping FixedUpdate HP fallback.\n");
        }
        g_xmark_entity_hit_queue_failed = true;
        return;
    }

    g_xmark_entity_hit_queue_handle = handle;
    g_xmark_entity_hit_queue_world = world;
    g_xmark_entity_hit_queue_registered = true;
    g_xmark_entity_hit_queue_failed = false;
    g_last_xmark_entity_hit_update_ms = 0;
    if (g_mina && env_bool("MINA_XMARK_HIT_QUEUE_REGISTER_LOG", true)) {
        g_mina->Log("XMarkBurn entity-hit update queue registered for the player world.\n");
    }
}

bool xmark_hud_queue_should_own_updates(unsigned long long now_ms) {
    if (!g_use_hud_update_queue ||
        !g_xmark_hud_update_queue_registered ||
        !g_last_xmark_hud_update_ms) {
        return false;
    }
    const unsigned int fallback_ms =
        std::max(32u, env_uint("MINA_XMARK_HUD_QUEUE_FALLBACK_MS", 250));
    return now_ms <= g_last_xmark_hud_update_ms + fallback_ms;
}

void update_xmark_hud_frame(unsigned long long status_now_ms) {
    maybe_publish_current_xmark_hud_state(status_now_ms);
    if (g_hud_layout_reporter_enabled) {
        maybe_report_xmark_hud_runtime_layout(
            status_now_ms,
            current_xmark_hud_state(status_now_ms));
    }
    xmark_hud_render_update_vertices(status_now_ms);
    xmark_hud_debug_draw(status_now_ms);
}

void xmark_hud_update_queue_update(void *userData) {
    World *world = static_cast<World *>(userData);
    if (!g_mina || !xmark_world_matches_player_world(world)) {
        return;
    }
    const unsigned long long real_now_ms = GetTickCount64();
    const unsigned long long status_now_ms = xmark_status_now_ms();
    g_last_xmark_hud_update_ms = real_now_ms;
    ModPerfScope hud_scope(ModPerfStage::Hud, mod_perf_enabled());
    update_xmark_hud_frame(status_now_ms);
}

void remove_xmark_hud_update_queue() {
    if (g_xmark_hud_update_queue_handle &&
        g_mina &&
        xmark_hud_update_queue_api_available()) {
        __try {
            g_mina->UpdateQueueRemove(g_xmark_hud_update_queue_handle);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_xmark_hud_update_queue_handle = nullptr;
    g_xmark_hud_update_queue_world = nullptr;
    g_xmark_hud_update_queue_registered = false;
    g_last_xmark_hud_update_ms = 0;
}

void ensure_xmark_hud_update_queue(World *world, unsigned long long now_ms) {
    if (!g_use_hud_update_queue) {
        remove_xmark_hud_update_queue();
        return;
    }
    if (!world || !xmark_world_matches_player_world(world)) {
        return;
    }
    if (g_xmark_hud_update_queue_registered &&
        g_xmark_hud_update_queue_world == world) {
        return;
    }
    if (!xmark_hud_update_queue_api_available()) {
        if (!g_xmark_hud_update_queue_failed && g_mina) {
            g_mina->Log("XMarkBurn HUD update queue unavailable; keeping FixedUpdate HUD fallback.\n");
        }
        g_xmark_hud_update_queue_failed = true;
        return;
    }
    const unsigned int retry_ms =
        std::max(250u, env_uint("MINA_XMARK_HUD_QUEUE_REGISTER_RETRY_MS", 1000));
    if (g_last_xmark_hud_register_attempt_ms &&
        now_ms < g_last_xmark_hud_register_attempt_ms + retry_ms) {
        return;
    }
    g_last_xmark_hud_register_attempt_ms = now_ms;
    remove_xmark_hud_update_queue();

    ycUpdateQueue *queue = nullptr;
    __try {
        queue = g_mina->WorldGetHUDUpdateQueue(world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        queue = nullptr;
    }
    if (!queue) {
        g_xmark_hud_update_queue_failed = true;
        return;
    }
    void *handle = nullptr;
    __try {
        handle = g_mina->UpdateQueueAdd(queue, xmark_hud_update_queue_update, world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        handle = nullptr;
    }
    if (!handle) {
        g_xmark_hud_update_queue_failed = true;
        return;
    }
    g_xmark_hud_update_queue_handle = handle;
    g_xmark_hud_update_queue_world = world;
    g_xmark_hud_update_queue_registered = true;
    g_xmark_hud_update_queue_failed = false;
    g_last_xmark_hud_update_ms = 0;
    if (g_mina && env_bool("MINA_XMARK_HUD_QUEUE_REGISTER_LOG", true)) {
        g_mina->Log("XMarkBurn HUD update queue registered for the player world.\n");
    }
}

void xmark_post_art_update(World *world, const char *source) {
    if (!g_mina || !world || !xmark_world_matches_player_world(world)) {
        return;
    }
    ModPerfScope perf_scope(ModPerfStage::PostArt, mod_perf_enabled());
    init_paths();
    g_last_world = world;
    const unsigned long long real_now_ms = GetTickCount64();
    const unsigned long long status_now_ms = xmark_status_now_ms();
    if (g_post_art_refresh_official_snapshot) {
        update_official_enemy_snapshot(world, real_now_ms);
    }
    {
        ModPerfScope attachment_scope(ModPerfStage::Attachments, mod_perf_enabled());
        update_xmark_attachments(status_now_ms);
    }
    observe_xmark_apply_sfx(status_now_ms);
    if (!g_use_world_update_attachment) {
        g_xmark_burn_debug_draw_phase = true;
        xmark_burn_debug_draw_effects(status_now_ms);
        g_xmark_burn_debug_draw_phase = false;
    }
    g_last_xmark_post_art_update_ms = real_now_ms;

    if (g_mina && env_bool("MINA_XMARK_POST_ART_QUEUE_LOG", false)) {
        const unsigned int log_ms = std::max(100u, env_uint("MINA_XMARK_POST_ART_QUEUE_LOG_MS", 1000));
        if (!g_last_xmark_post_art_queue_log_ms ||
            real_now_ms >= g_last_xmark_post_art_queue_log_ms + log_ms) {
            g_last_xmark_post_art_queue_log_ms = real_now_ms;
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn post-art attachment update source=%s activeQueue=%u world=0x%p\n",
                source && source[0] ? source : "-",
                g_xmark_entity_post_art_queue_registered ? 1u : 0u,
                reinterpret_cast<void *>(world));
            g_mina->Log(message);
        }
    }
}

void xmark_entity_post_art_queue_update(void *userData) {
    xmark_post_art_update(static_cast<World *>(userData), "EntityPostArtUpdateQueue");
}

void remove_xmark_entity_post_art_queue() {
    if (g_xmark_entity_post_art_queue_handle && g_mina && xmark_update_queue_api_available()) {
        __try {
            g_mina->UpdateQueueRemove(g_xmark_entity_post_art_queue_handle);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_xmark_entity_post_art_queue_handle = nullptr;
    g_xmark_update_queue_world = nullptr;
    g_xmark_entity_post_art_queue_registered = false;
    g_last_xmark_post_art_update_ms = 0;
}

void reset_xmark_room_local_state(unsigned long long now_ms) {
    stop_xmark_owned_burn_tick_sfx("room-transition");
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (burn.active) {
            release_xmark_burn_palette(burn, true);
        }
        burn = XMarkBurnEffect{};
    }
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        attachment = XMarkAttachment{};
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        mark = XMarkHudMark{};
    }
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        xmark_enemy_status_release(status);
    }
    std::memset(g_charged_consume_probes, 0, sizeof(g_charged_consume_probes));
    std::memset(g_pending_visual_marks, 0, sizeof(g_pending_visual_marks));
    std::memset(g_visual_enemy_hosts, 0, sizeof(g_visual_enemy_hosts));
    std::memset(g_basic_hit_candidates, 0, sizeof(g_basic_hit_candidates));
    g_clashrend_boom_pattern = ClashrendBoomPatternState{};
    g_clashrend_boom_geyser = ClashrendBoomGeyserState{};
    g_visual_enemy_host_count = 0;
    g_visual_enemy_state_tick = 0;
    g_visual_enemy_state_explicit_zero = false;
    g_last_visual_enemy_state_read_ms = 0;
    g_official_enemy_attack_discovery_attempts = 0;
    g_last_official_enemy_attack_discovery_ms = 0;
    g_official_enemy_attack_discovery_event = 0;
    g_official_enemy_registry_rescan_requested_ms = 0;
    g_last_official_enemy_scan_ms = 0;
    g_last_official_enemy_room_scan_ms = 0;
    g_last_official_enemy_registry_reconcile_ms = 0;
    g_last_official_enemy_lifecycle_batch_ms = 0;
    g_official_enemy_lifecycle_cursor = 0;
    official_enemy_clear_snapshot();
    clear_player_attack_anim_cache();
    publish_current_xmark_hud_state(now_ms);
}

void world_construct(void *pCtx) {
    if (!g_mina || !pCtx) {
        return;
    }
    struct WorldConstructCtx {
        World *world;
    };
    WorldConstructCtx *ctx = static_cast<WorldConstructCtx *>(pCtx);
    if (!ctx || !ctx->world || !xmark_world_matches_player_world(ctx->world)) {
        return;
    }
    init_paths();
    g_last_world = ctx->world;
    const unsigned long long now_ms = GetTickCount64();
    ensure_xmark_entity_hit_update_queue(ctx->world, now_ms);
    ensure_xmark_hud_update_queue(ctx->world, now_ms);

    if (!g_use_entity_post_art_queue) {
        return;
    }
    if (!xmark_update_queue_api_available()) {
        if (g_mina && !g_xmark_entity_post_art_queue_failed) {
            g_mina->Log("XMarkBurn entity post-art queue unavailable; using FixedUpdate attachment fallback.\n");
        }
        g_xmark_entity_post_art_queue_failed = true;
        return;
    }
    if (g_xmark_entity_post_art_queue_registered &&
        g_xmark_update_queue_world == ctx->world) {
        return;
    }
    remove_xmark_entity_post_art_queue();

    ycUpdateQueue *queue = nullptr;
    __try {
        queue = g_mina->WorldGetEntityPostArtUpdateQueue(ctx->world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        queue = nullptr;
    }
    if (!queue) {
        if (g_mina && !g_xmark_entity_post_art_queue_failed) {
            g_mina->Log("XMarkBurn entity post-art queue missing for world; using FixedUpdate attachment fallback.\n");
        }
        g_xmark_entity_post_art_queue_failed = true;
        return;
    }

    void *handle = nullptr;
    __try {
        handle = g_mina->UpdateQueueAdd(queue, xmark_entity_post_art_queue_update, ctx->world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        handle = nullptr;
    }
    if (!handle) {
        if (g_mina && !g_xmark_entity_post_art_queue_failed) {
            g_mina->Log("XMarkBurn entity post-art queue registration failed; using FixedUpdate attachment fallback.\n");
        }
        g_xmark_entity_post_art_queue_failed = true;
        return;
    }

    g_xmark_entity_post_art_queue_handle = handle;
    g_xmark_update_queue_world = ctx->world;
    g_xmark_entity_post_art_queue_registered = true;
    g_xmark_entity_post_art_queue_failed = false;
    if (g_mina && env_bool("MINA_XMARK_POST_ART_QUEUE_REGISTER_LOG", true)) {
        g_mina->Log("XMarkBurn entity post-art attachment queue registered.\n");
    }
}

void world_destroy(void *pCtx) {
    struct WorldDestroyCtx {
        World *world;
    };
    WorldDestroyCtx *ctx = static_cast<WorldDestroyCtx *>(pCtx);
    if (!ctx || ctx->world == g_xmark_update_queue_world) {
        remove_xmark_entity_post_art_queue();
    }
    if (!ctx || ctx->world == g_xmark_entity_hit_queue_world) {
        remove_xmark_entity_hit_update_queue();
    }
    if (!ctx || ctx->world == g_xmark_hud_update_queue_world) {
        remove_xmark_hud_update_queue();
    }
    if (ctx && ctx->world == g_last_world) {
        g_last_world = nullptr;
    }
    if (!ctx || ctx->world == g_xmark_game_clock_world) {
        g_xmark_game_clock_world = nullptr;
        g_xmark_game_clock_ready = false;
        g_xmark_game_clock_last_elapsed = 0.0f;
    }
    clear_player_attack_anim_cache();
    xmark_weak_ptr_destroy(g_weapon_shadow_anim_weak);
    g_weapon_shadow_anim_weak = nullptr;
    g_weapon_shadow_anim_component = 0;
    g_weapon_shadow_last_scan_ms = 0;
    std::memset(g_anim_property_probed_components, 0, sizeof(g_anim_property_probed_components));
    g_anim_property_probed_component_count = 0;
    stop_xmark_owned_burn_tick_sfx("world-destroy");
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        release_xmark_burn_palette(burn, true);
        burn.active = false;
    }
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        xmark_enemy_status_release(status);
    }
    official_enemy_clear_snapshot();
    g_clashrend_boom_pattern = ClashrendBoomPatternState{};
    g_clashrend_boom_geyser = ClashrendBoomGeyserState{};
}

void game_shutdown(void *) {
    stop_xmark_owned_burn_tick_sfx("game-shutdown");
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        release_xmark_burn_palette(burn, true);
        burn.active = false;
    }
    remove_xmark_entity_post_art_queue();
    remove_xmark_entity_hit_update_queue();
    remove_xmark_hud_update_queue();
    clear_player_attack_anim_cache();
    xmark_weak_ptr_destroy(g_weapon_shadow_anim_weak);
    g_weapon_shadow_anim_weak = nullptr;
    g_weapon_shadow_anim_component = 0;
    g_weapon_shadow_last_scan_ms = 0;
    g_xmark_game_clock_world = nullptr;
    g_xmark_game_clock_ready = false;
    g_xmark_game_clock_last_elapsed = 0.0f;
    std::memset(g_anim_property_probed_components, 0, sizeof(g_anim_property_probed_components));
    g_anim_property_probed_component_count = 0;
    for (XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        xmark_enemy_status_release(status);
    }
    official_enemy_clear_snapshot();
    g_clashrend_boom_pattern = ClashrendBoomPatternState{};
    g_clashrend_boom_geyser = ClashrendBoomGeyserState{};
}

void game_init(void *) {
    if (!g_mina) {
        return;
    }
    init_paths();
    xmark_hud_render_backend_ensure_initialized();
    g_mina->Log("XMarkBurn GameInit HUD atlas warmed.\n");
}

void prewarm_clashrend_during_gameplay_transition(
    int game_state,
    unsigned int room_index,
    float room_time,
    unsigned long long now_ms) {
    const bool gameplay = gameplay_state_can_spawn_test_enemy(game_state);
    const bool previous_gameplay =
        gameplay_state_can_spawn_test_enemy(g_clashrend_transition_previous_game_state);
    const bool entering_gameplay = gameplay && !previous_gameplay;
    g_clashrend_transition_previous_game_state = game_state;
    if (!env_bool("MINA_XMARK_TRANSITION_PREWARM_ENABLED", true) ||
        !gameplay || is_pseudo_room(room_index)) {
        return;
    }

    if (entering_gameplay ||
        (g_clashrend_transition_prewarmed_room != room_index &&
         g_clashrend_transition_prewarm_stage == 0u)) {
        g_clashrend_transition_prewarmed_room = room_index;
        g_clashrend_transition_prewarm_stage = 1u;
    }
    if (g_clashrend_transition_prewarmed_room != room_index ||
        g_clashrend_transition_prewarm_stage >= 5u) {
        return;
    }

    switch (g_clashrend_transition_prewarm_stage) {
    case 1u:
        if (g_xmark_hud_render_backend_ready ||
            xmark_hud_render_backend_ensure_initialized()) {
            g_clashrend_transition_prewarm_stage = 2u;
        }
        break;
    case 2u:
        if (!g_marker_debug_draw_warmup || g_xmark_marker_debug_draw_ready ||
            xmark_marker_debug_draw_ensure_initialized()) {
            g_marker_debug_draw_warmup = false;
            g_clashrend_transition_prewarm_stage = 3u;
        }
        break;
    case 3u:
        if (!g_burn_debug_draw_warmup || g_xmark_burn_debug_draw_ready ||
            xmark_burn_debug_draw_ensure_initialized()) {
            g_burn_debug_draw_warmup = false;
            g_clashrend_transition_prewarm_stage = 4u;
        }
        break;
    case 4u:
        if (g_clashrend_text_patch_complete || g_clashrend_text_patch_final_miss) {
            g_clashrend_transition_prewarm_stage = 5u;
            break;
        }
        g_clashrend_text_patch_next_ms = now_ms;
        maybe_patch_clashrend_runtime_text(now_ms);
        if (g_clashrend_text_patch_complete || g_clashrend_text_patch_final_miss) {
            g_clashrend_transition_prewarm_stage = 5u;
        }
        break;
    default:
        break;
    }
    if (g_mina && env_bool("MINA_XMARK_TRANSITION_PREWARM_LOG", false) &&
        g_clashrend_transition_prewarm_stage >= 5u) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn transition stream complete room=%u state=%d roomTime=%.3f marker=%u burn=%u hud=%u text=%u.\n",
            room_index,
            game_state,
            static_cast<double>(room_time),
            g_xmark_marker_debug_draw_ready ? 1u : 0u,
            g_xmark_burn_debug_draw_ready ? 1u : 0u,
            g_xmark_hud_render_backend_ready ? 1u : 0u,
            g_clashrend_text_patch_complete ? 1u : 0u);
        g_mina->Log(message);
    }
}

void world_update(void *pCtx) {
    if (!g_mina || !pCtx) {
        return;
    }
    struct WorldUpdateCtx {
        World *world;
        float elapsed;
    };
    WorldUpdateCtx *ctx = static_cast<WorldUpdateCtx *>(pCtx);
    if (!ctx || !ctx->world || !xmark_world_matches_player_world(ctx->world)) {
        return;
    }
    init_paths();
    g_last_world = ctx->world;
    const unsigned long long real_now_ms = GetTickCount64();
    update_xmark_game_clock(ctx->world, real_now_ms);
    const unsigned long long status_now_ms = xmark_status_now_ms();
    maybe_trace_player_hammer_anims(real_now_ms);
    update_boom_charge_anim_trace(real_now_ms);
    ensure_xmark_entity_hit_update_queue(ctx->world, real_now_ms);
    ensure_xmark_hud_update_queue(ctx->world, real_now_ms);
    if (g_use_world_update_attachment) {
        update_official_enemy_spawn_lifecycle_batch(real_now_ms);
        update_xmark_enemy_status_lifecycle(status_now_ms);
        ModPerfScope attachment_scope(ModPerfStage::Attachments, mod_perf_enabled());
        update_xmark_attachments(status_now_ms);
        const unsigned int burn_draw_min_ms =
            std::max(1u, env_uint("MINA_XMARK_BURN_DEBUG_DRAW_WORLD_MIN_MS", 8u));
        if (!g_xmark_burn_debug_draw_last_world_ms ||
            status_now_ms >= g_xmark_burn_debug_draw_last_world_ms + burn_draw_min_ms) {
            g_xmark_burn_debug_draw_phase = true;
            xmark_burn_debug_draw_effects(status_now_ms);
            g_xmark_burn_debug_draw_phase = false;
            g_xmark_burn_debug_draw_last_world_ms = status_now_ms;
        }
        observe_xmark_apply_sfx(status_now_ms);
        g_last_xmark_post_art_update_ms = real_now_ms;
    }
    if (!g_official_scan_in_world_update) {
        return;
    }
    update_official_enemy_snapshot(ctx->world, real_now_ms);
}

void warm_up_xmark_renderers(int game_state, unsigned int room_index, float room_time) {
    if (!gameplay_state_can_spawn_test_enemy(game_state) ||
        is_pseudo_room(room_index) ||
        room_time < env_float("MINA_XMARK_RENDERER_WARMUP_ROOM_TIME", 0.75f)) {
        return;
    }
    if (g_marker_debug_draw_warmup && !g_xmark_marker_debug_draw_ready) {
        if (!xmark_marker_debug_draw_enabled()) {
            g_marker_debug_draw_warmup = false;
        } else {
            xmark_marker_debug_draw_ensure_initialized();
        }
    }
    if (room_time >= env_float("MINA_XMARK_BURN_RENDERER_WARMUP_ROOM_TIME", 1.25f) &&
        g_burn_debug_draw_warmup && !g_xmark_burn_debug_draw_ready) {
        if (!xmark_burn_debug_draw_enabled()) {
            g_burn_debug_draw_warmup = false;
        } else {
            xmark_burn_debug_draw_ensure_initialized();
        }
    }
    if (!g_clashrend_boom_debug_draw_ready &&
        env_bool("MINA_XMARK_BOOM_PATTERN_ENABLED", true) &&
        env_bool("MINA_XMARK_BOOM_MODAPI_RENDER", false)) {
        clashrend_boom_debug_draw_ensure_initialized();
    }
}

void fixed_update(void *) {
    if (!g_mina) {
        return;
    }
    ModPerfScope perf_scope(ModPerfStage::Fixed, mod_perf_enabled());
    init_paths();

    const unsigned long long now_ms = GetTickCount64();
    const unsigned long long status_now_ms = xmark_status_now_ms();
    if (g_player_world_gate_enabled &&
        xmark_player_world_api_available()) {
        g_last_world = xmark_player_world_safe();
    }
    ensure_xmark_entity_hit_update_queue(g_last_world, now_ms);
    ensure_xmark_hud_update_queue(g_last_world, now_ms);
    const unsigned int room_index = g_mina->GetRoomIndex();
    const int game_state = g_mina->GetCurrentGameState();
    const float room_time = g_mina->GetRoomTime();
    prewarm_clashrend_during_gameplay_transition(
        game_state,
        room_index,
        room_time,
        now_ms);
    warm_up_xmark_renderers(game_state, room_index, room_time);
    if (!env_bool("MINA_XMARK_TRANSITION_PREWARM_ENABLED", true) &&
        gameplay_state_can_spawn_test_enemy(game_state) &&
        !is_pseudo_room(room_index) &&
        room_time >= env_float("MINA_XMARK_RUNTIME_TEXT_PATCH_MIN_ROOM_TIME", 1.0f)) {
        maybe_patch_clashrend_runtime_text(now_ms);
    }
    maybe_repair_clashrend_weapon_shadow(g_last_world, now_ms);
    const bool room_changed_at_frame_start = room_index != g_last_room;
    const bool gameplay_room_transition =
        room_changed_at_frame_start &&
        !is_pseudo_room(room_index) &&
        !is_pseudo_room(g_last_official_enemy_room) &&
        room_index != g_last_official_enemy_room;
    if (gameplay_room_transition) {
        reset_xmark_room_local_state(status_now_ms);
    }
    if (room_changed_at_frame_start) {
        g_basic_attack_probe = XMarkBasicAttackProbe{};
        g_attack_smear_contact_pending = false;
        g_last_muriel_mark_attack_press_ms = 0;
        g_last_muriel_mark_frame_tick = 0;
        g_last_muriel_mark_frame_draw = 0;
        std::memset(g_basic_hit_candidates, 0, sizeof(g_basic_hit_candidates));
        std::memset(g_recent_basic_frame_samples, 0, sizeof(g_recent_basic_frame_samples));
        g_recent_basic_frame_sample_cursor = 0;
    }
    maybe_refresh_official_enemy_snapshot_for_room(room_index, room_time, now_ms);
    reconcile_official_enemy_room_registry(now_ms);
    if (g_test_runtime_enabled) {
        maybe_disable_save_writes_for_test();
        maybe_attachment_lab_auto_start_save(now_ms, game_state);
        maybe_attachment_lab_force_game_state(now_ms, game_state);
    }
    float player_x = 0.0f;
    float player_y = 0.0f;
    g_mina->PlayerGetPos(&player_x, &player_y);
    update_last_direction_from_input();
    if (g_force_default_claymore_enabled) {
        maybe_force_default_claymore(now_ms, game_state, room_index);
    }
    if (g_command_file_enabled) {
        ModPerfScope command_scope(ModPerfStage::CommandPoll, mod_perf_enabled());
        maybe_process_command_file(now_ms, game_state, room_index, room_time);
    }
    update_xmark_pause_timer(now_ms, room_index, room_time, game_state);
    if (g_test_runtime_enabled) {
        maybe_resolve_test_enemy_runtime_target(now_ms);
        update_attachment_lab_target_motion(now_ms);
    }
    maybe_process_pending_xmark_visual_mark_retries(now_ms);
    if (!xmark_post_art_queue_should_own_attachment_updates(now_ms)) {
        ModPerfScope attachment_scope(ModPerfStage::Attachments, mod_perf_enabled());
        update_xmark_attachments(status_now_ms);
    }
    observe_xmark_apply_sfx(status_now_ms);
    {
        ModPerfScope burn_scope(ModPerfStage::Burn, mod_perf_enabled());
        update_xmark_charged_consume_health_probes(now_ms);
        maybe_auto_consume_xmark_for_burn(status_now_ms);
        update_xmark_burn_effects(status_now_ms);
        update_muriel_regular_dialogue_state(status_now_ms, room_index);
        update_xmark_native_final_hit_death_watches(now_ms);
    }
    if (!xmark_hud_queue_should_own_updates(now_ms)) {
        ModPerfScope hud_scope(ModPerfStage::Hud, mod_perf_enabled());
        update_xmark_hud_frame(status_now_ms);
    }

    if (g_debug_keys_enabled) {
        const bool debug_keys_allowed = debug_hotkeys_allowed();
        if (!debug_keys_allowed) {
            reset_debug_key_latches();
        } else if (debug_key_pressed('T', &g_win_t_was_down)) {
            const unsigned int consumed = consume_first_xmark_attachment_for_burn(now_ms, "key-T-burn-consume");
            if (g_mina && !consumed) {
                g_mina->Log("XMarkBurn key T burn consume missed: no active X mark attachment.\n");
            }
        }
        if (debug_keys_allowed && g_mina->IsKeyDown(YC_KEY_Y)) {
            spawn_entity_probe(ENTITYTYPE_ANIM_EFFECT_EMITTER, "key-Y");
        }
        if (debug_keys_allowed && g_mina->IsKeyDown(YC_KEY_U)) {
            spawn_entity_probe(ENTITYTYPE_MARKER, "key-U");
        }
        if (debug_keys_allowed && debug_key_pressed('G', &g_win_g_was_down)) {
            spawn_direct_xmark_enemy_test("key-G-enemy-anchor");
        }
        if (debug_keys_allowed && debug_key_pressed('H', &g_win_h_was_down)) {
            spawn_direct_f0029_probe("key-H");
        }
    }
    if (g_test_runtime_enabled) {
        maybe_spawn_test_enemy_harness(game_state, room_index, room_time);
    }
    const bool attachment_lab_suppresses_combat =
        g_attachment_lab_enabled &&
        !env_bool("MINA_XMARK_ATTACHMENT_LAB_ALLOW_COMBAT_PROBES", false);
    if (!attachment_lab_suppresses_combat) {
        ModPerfScope combat_scope(ModPerfStage::Combat, mod_perf_enabled());
        maybe_spawn_f0029_from_attack(now_ms, game_state, room_index, room_time);
        maybe_handle_d3d12_basic_frame_state(now_ms, game_state, room_index, room_time);
        maybe_start_clashrend_boom_from_native_impact();
        update_clashrend_boom_pattern(status_now_ms);
        update_clashrend_boom_geyser(status_now_ms);
        {
            ModPerfScope health_scope(ModPerfStage::BasicHealth, mod_perf_enabled());
            if (!xmark_entity_hit_queue_should_own_basic_health_updates(now_ms)) {
                maybe_apply_basic_attack_health_probe(now_ms);
            }
        }
        {
            ModPerfScope history_scope(ModPerfStage::HealthHistory, mod_perf_enabled());
            refresh_runtime_health_history(now_ms, false);
        }
    }
    xmark_attack_overlay_render_update_vertices(now_ms);
    if (g_native_auto_spawn) {
        maybe_auto_spawn(game_state, room_index, room_time);
    }
    mod_perf_maybe_report(now_ms);

    const bool room_changed = room_index != g_last_room;
    if (!room_changed && now_ms - g_last_write_ms < g_write_interval_ms) {
        return;
    }

    if (g_room_state_write_enabled) {
        ModPerfScope state_write_scope(ModPerfStage::StateWrite, mod_perf_enabled());
        write_state_file(game_state, room_index, room_time, player_x, player_y, now_ms);
    }
    g_last_write_ms = now_ms;

    if (room_changed) {
        g_last_room = room_index;
        if (is_pseudo_room(room_index) && !g_trace_pseudo_rooms) {
            return;
        }
        append_trace_row(game_state, room_index, room_time, player_x, player_y, now_ms);
        if (env_bool("MINA_XMARK_ROOM_CHANGE_LOG", false)) {
            char message[512]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn room changed: room=%u hex=0x%08X state=%d roomTime=%.3f player=(%.3f, %.3f)\n",
                room_index,
                room_index,
                game_state,
                static_cast<double>(room_time),
                static_cast<double>(player_x),
                static_cast<double>(player_y));
            g_mina->Log(message);
        }
    }
}

