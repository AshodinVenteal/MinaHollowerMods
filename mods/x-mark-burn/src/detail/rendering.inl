bool xmark_hud_enemy_endcap_sequence(const char *sequence, bool *is_left_out, bool *is_right_out) {
    if (is_left_out) {
        *is_left_out = false;
    }
    if (is_right_out) {
        *is_right_out = false;
    }
    if (!sequence || !sequence[0]) {
        return false;
    }
    const bool is_left =
        _stricmp(sequence, "healthBar_Enemy_Left") == 0 ||
        _stricmp(sequence, "healthBar_Enemy_Left_LowHealth") == 0;
    const bool is_right =
        _stricmp(sequence, "healthBar_Enemy_Right") == 0 ||
        _stricmp(sequence, "healthBar_Enemy_Right_LowHealth") == 0;
    if (is_left_out) {
        *is_left_out = is_left;
    }
    if (is_right_out) {
        *is_right_out = is_right;
    }
    return is_left || is_right;
}

void xmark_hud_scan_runtime_layout_entity(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int max_nodes,
    XMarkHudRuntimeLayout *layout) {
    if (!entity || !layout || depth > max_depth || layout->nodes >= max_nodes) {
        return;
    }
    ++layout->nodes;

    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    if (!child_count) {
        return;
    }

    const size_t child_cap = std::min<size_t>(
        child_count,
        env_uint("MINA_XMARK_HUD_LAYOUT_REPORTER_CHILD_CAP", 1024));
    ycComponent **children = static_cast<ycComponent **>(
        g_mina->Alloc(sizeof(ycComponent *) * child_cap));
    if (!children) {
        return;
    }
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, child_cap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }

    const size_t limit = std::min(read_count, child_cap);
    for (size_t i = 0; i < limit && layout->nodes < max_nodes; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        ++layout->components;

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
            xmark_hud_scan_runtime_layout_entity(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                max_nodes,
                layout);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }

        char sequence[64]{};
        __try {
            copy_string_ref(sequence, sizeof(sequence), g_mina->GameAnimGetSeqNameNoDir(component));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sequence[0] = 0;
        }
        bool is_left = false;
        bool is_right = false;
        if (!xmark_hud_enemy_endcap_sequence(sequence, &is_left, &is_right)) {
            continue;
        }

        ycEntity *owner = entity;
        if (g_mina->ComponentGetParent) {
            __try {
                ycEntity *parent = g_mina->ComponentGetParent(component);
                if (parent) {
                    owner = parent;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                owner = entity;
            }
        }
        MM_Transform transform{};
        bool transform_ok = false;
        __try {
            transform = g_mina->EntityGetWorldTransform(owner);
            transform_ok = std::isfinite(transform.t.x) && std::isfinite(transform.t.y) &&
                std::isfinite(transform.s.x) && std::isfinite(transform.s.y);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            transform_ok = false;
        }
        if (!transform_ok) {
            continue;
        }

        if (is_left && (!layout->found_left || transform.t.x < layout->left_x)) {
            layout->found_left = true;
            layout->left_x = transform.t.x;
            layout->left_y = transform.t.y;
            layout->left_scale_x = transform.s.x;
            layout->left_scale_y = transform.s.y;
            std::snprintf(layout->left_sequence, sizeof(layout->left_sequence), "%s", sequence);
        }
        if (is_right && (!layout->found_right || transform.t.x > layout->right_x)) {
            layout->found_right = true;
            layout->right_x = transform.t.x;
            layout->right_y = transform.t.y;
            layout->right_scale_x = transform.s.x;
            layout->right_scale_y = transform.s.y;
            std::snprintf(layout->right_sequence, sizeof(layout->right_sequence), "%s", sequence);
        }
    }
    g_mina->Free(children);
}

void write_xmark_hud_runtime_layout_state(const XMarkHudRuntimeLayout &layout) {
    if (!g_hud_layout_state_path[0]) {
        return;
    }
    char tmp_path[MAX_PATH * 4]{};
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_hud_layout_state_path);
    FILE *file = nullptr;
    if (fopen_s(&file, tmp_path, "wb") != 0 || !file) {
        return;
    }
    std::fprintf(file, "version=1\n");
    std::fprintf(file, "tick=%llu\n", layout.sampled_ms);
    std::fprintf(file, "valid=%u\n", layout.valid ? 1u : 0u);
    std::fprintf(file, "nodes=%u\n", layout.nodes);
    std::fprintf(file, "components=%u\n", layout.components);
    std::fprintf(file, "leftFound=%u\n", layout.found_left ? 1u : 0u);
    std::fprintf(file, "leftSequence=%s\n", layout.left_sequence[0] ? layout.left_sequence : "-");
    std::fprintf(file, "leftX=%.6f\n", static_cast<double>(layout.left_x));
    std::fprintf(file, "leftY=%.6f\n", static_cast<double>(layout.left_y));
    std::fprintf(file, "leftScaleX=%.6f\n", static_cast<double>(layout.left_scale_x));
    std::fprintf(file, "leftScaleY=%.6f\n", static_cast<double>(layout.left_scale_y));
    std::fprintf(file, "rightFound=%u\n", layout.found_right ? 1u : 0u);
    std::fprintf(file, "rightSequence=%s\n", layout.right_sequence[0] ? layout.right_sequence : "-");
    std::fprintf(file, "rightX=%.6f\n", static_cast<double>(layout.right_x));
    std::fprintf(file, "rightY=%.6f\n", static_cast<double>(layout.right_y));
    std::fprintf(file, "rightScaleX=%.6f\n", static_cast<double>(layout.right_scale_x));
    std::fprintf(file, "rightScaleY=%.6f\n", static_cast<double>(layout.right_scale_y));
    std::fclose(file);
    MoveFileExA(tmp_path, g_hud_layout_state_path, MOVEFILE_REPLACE_EXISTING);
}

void maybe_report_xmark_hud_runtime_layout(unsigned long long now_ms, const XMarkHudState &hud_state) {
    if (!env_bool("MINA_XMARK_HUD_LAYOUT_REPORTER_ENABLED", false) ||
        (!hud_state.active_count && !env_bool("MINA_XMARK_HUD_LAYOUT_REPORTER_SCAN_ALWAYS", false))) {
        return;
    }
    const unsigned int interval_ms = std::max(
        100u,
        env_uint("MINA_XMARK_HUD_LAYOUT_REPORTER_INTERVAL_MS", 500));
    if (g_last_hud_layout_scan_ms && now_ms < g_last_hud_layout_scan_ms + interval_ms) {
        return;
    }
    g_last_hud_layout_scan_ms = now_ms;
    if (!g_last_world || !g_mina || !g_mina->WorldGetHUDRootEntity ||
        !g_mina->EntityGetChildren || !g_mina->ComponentIsa ||
        !g_mina->GameAnimGetSeqNameNoDir || !g_mina->EntityGetWorldTransform ||
        !g_mina->Alloc || !g_mina->Free || !official_enemy_init_rtti() ||
        !xmark_game_anim_init_rtti()) {
        return;
    }

    ycEntity *hud_root = nullptr;
    __try {
        hud_root = g_mina->WorldGetHUDRootEntity(g_last_world);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hud_root = nullptr;
    }
    if (!hud_root) {
        return;
    }

    XMarkHudRuntimeLayout layout{};
    layout.sampled_ms = now_ms;
    xmark_hud_scan_runtime_layout_entity(
        hud_root,
        0,
        env_uint("MINA_XMARK_HUD_LAYOUT_REPORTER_MAX_DEPTH", 10),
        env_uint("MINA_XMARK_HUD_LAYOUT_REPORTER_MAX_NODES", 512),
        &layout);
    layout.valid = layout.found_left && layout.found_right;
    g_xmark_hud_runtime_layout = layout;
    write_xmark_hud_runtime_layout_state(layout);

    if (g_mina && env_bool("MINA_XMARK_HUD_LAYOUT_REPORTER_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn HUD layout sample valid=%u nodes=%u components=%u left=%s(%.3f,%.3f s=%.3f,%.3f) right=%s(%.3f,%.3f s=%.3f,%.3f)\n",
            layout.valid ? 1u : 0u,
            layout.nodes,
            layout.components,
            layout.left_sequence[0] ? layout.left_sequence : "-",
            static_cast<double>(layout.left_x),
            static_cast<double>(layout.left_y),
            static_cast<double>(layout.left_scale_x),
            static_cast<double>(layout.left_scale_y),
            layout.right_sequence[0] ? layout.right_sequence : "-",
            static_cast<double>(layout.right_x),
            static_cast<double>(layout.right_y),
            static_cast<double>(layout.right_scale_x),
            static_cast<double>(layout.right_scale_y));
        g_mina->Log(message);
    }
}

void publish_xmark_hud_state(const XMarkHudState &hud_state) {
    if (!g_mina || !env_bool("MINA_XMARK_HUD_SHARED_VALUES_ENABLED", true)) {
        return;
    }
    g_mina->SetSharedValue("xmark.hud.activeCount", static_cast<uintptr_t>(hud_state.active_count));
    g_mina->SetSharedValue("xmark.hud.mode", static_cast<uintptr_t>(hud_state.mode));
    g_mina->SetSharedValue("xmark.hud.visible", static_cast<uintptr_t>(hud_state.visible ? 1u : 0u));
    g_mina->SetSharedValue("xmark.hud.blinking", static_cast<uintptr_t>(hud_state.blinking ? 1u : 0u));
    g_mina->SetSharedValue("xmark.hud.damagePopupVisible", static_cast<uintptr_t>(hud_state.damage_popup_visible ? 1u : 0u));
    g_mina->SetSharedValue("xmark.hud.firstTarget", hud_state.first_target);
    g_mina->SetSharedValue("xmark.hud.firstExpiresMs", static_cast<uintptr_t>(hud_state.first_expires_ms));
    g_mina->SetSharedValue("xmark.hud.firstBlinkStartMs", static_cast<uintptr_t>(hud_state.first_blink_start_ms));
    g_mina->SetSharedValue("xmark.hud.damagePopupUntilMs", static_cast<uintptr_t>(hud_state.damage_popup_until_ms));
}

void write_hud_state_file(unsigned long long now_ms, const XMarkHudState &hud_state) {
    if (!env_bool("MINA_XMARK_HUD_STATE_FILE_ENABLED", true) || !g_hud_state_path[0]) {
        return;
    }

    char tmp_path[MAX_PATH * 4]{};
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_hud_state_path);

    FILE *file = nullptr;
    if (fopen_s(&file, tmp_path, "wb") != 0 || !file) {
        return;
    }

    const unsigned long long remaining_ms =
        hud_state.first_expires_ms > now_ms ? hud_state.first_expires_ms - now_ms : 0ull;
    const XMarkBurnEffect *first_burn = nullptr;
    unsigned int burn_count = 0;
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active || now_ms >= burn.expires_ms) {
            continue;
        }
        if (!first_burn) {
            first_burn = &burn;
        }
        if (burn.has_last_position &&
            (burn.visual_key || burn.target || burn.official_combat_core)) {
            ++burn_count;
        }
    }
    std::fprintf(file, "version=1\n");
    std::fprintf(file, "tick=%llu\n", now_ms);
    std::fprintf(file, "activeCount=%u\n", hud_state.active_count);
    std::fprintf(file, "mode=%u\n", hud_state.mode);
    std::fprintf(file, "visible=%u\n", hud_state.visible ? 1u : 0u);
    std::fprintf(file, "blinking=%u\n", hud_state.blinking ? 1u : 0u);
    std::fprintf(file, "damagePopupVisible=%u\n", hud_state.damage_popup_visible ? 1u : 0u);
    std::fprintf(file, "firstTarget=0x%p\n", reinterpret_cast<void *>(hud_state.first_target));
    std::fprintf(file, "firstHealth=%.6f\n", static_cast<double>(hud_state.first_health));
    std::fprintf(file, "firstHealthMax=%.6f\n", static_cast<double>(hud_state.first_health_max));
    std::fprintf(file, "firstExpiresMs=%llu\n", hud_state.first_expires_ms);
    std::fprintf(file, "firstBlinkStartMs=%llu\n", hud_state.first_blink_start_ms);
    std::fprintf(file, "damagePopupUntilMs=%llu\n", hud_state.damage_popup_until_ms);
    std::fprintf(file, "remainingMs=%llu\n", remaining_ms);
    std::fprintf(file, "burnActive=%u\n", first_burn ? 1u : 0u);
    std::fprintf(file, "burnTarget=0x%p\n", first_burn ? reinterpret_cast<void *>(first_burn->target) : nullptr);
    std::fprintf(file, "burnVisualKey=0x%llX\n", first_burn ? first_burn->visual_key : 0ull);
    std::fprintf(file, "burnExpiresMs=%llu\n", first_burn ? first_burn->expires_ms : 0ull);
    std::fprintf(file, "burnStartedMs=%llu\n", first_burn ? first_burn->started_ms : 0ull);
    std::fprintf(
        file,
        "burnX=%.6f\n",
        first_burn && first_burn->has_last_position ? static_cast<double>(first_burn->last_position.x) : 0.0);
    std::fprintf(
        file,
        "burnY=%.6f\n",
        first_burn && first_burn->has_last_position ? static_cast<double>(first_burn->last_position.y) : 0.0);
    std::fprintf(
        file,
        "burnHalfW=%.6f\n",
        first_burn ? static_cast<double>(first_burn->render_half_w) : 0.0);
    std::fprintf(
        file,
        "burnHalfH=%.6f\n",
        first_burn ? static_cast<double>(first_burn->render_half_h) : 0.0);
    std::fprintf(file, "burnCount=%u\n", burn_count);
    unsigned int burn_index = 0;
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active ||
            now_ms >= burn.expires_ms ||
            !burn.has_last_position ||
            (!burn.visual_key && !burn.target && !burn.official_combat_core)) {
            continue;
        }
        std::fprintf(file, "burn%uActive=1\n", burn_index);
        std::fprintf(file, "burn%uTarget=0x%p\n", burn_index, reinterpret_cast<void *>(burn.target));
        std::fprintf(file, "burn%uVisualKey=0x%llX\n", burn_index, burn.visual_key);
        std::fprintf(file, "burn%uExpiresMs=%llu\n", burn_index, burn.expires_ms);
        std::fprintf(file, "burn%uStartedMs=%llu\n", burn_index, burn.started_ms);
        std::fprintf(file, "burn%uX=%.6f\n", burn_index, static_cast<double>(burn.last_position.x));
        std::fprintf(file, "burn%uY=%.6f\n", burn_index, static_cast<double>(burn.last_position.y));
        std::fprintf(file, "burn%uHalfW=%.6f\n", burn_index, static_cast<double>(burn.render_half_w));
        std::fprintf(file, "burn%uHalfH=%.6f\n", burn_index, static_cast<double>(burn.render_half_h));
        ++burn_index;
    }
    std::fclose(file);

    MoveFileExA(tmp_path, g_hud_state_path, MOVEFILE_REPLACE_EXISTING);
}

void publish_current_xmark_hud_state(unsigned long long now_ms) {
    const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
    publish_xmark_hud_state(hud_state);
    write_hud_state_file(now_ms, hud_state);
    g_last_hud_write_ms = now_ms;
    g_last_hud_state_had_active = hud_state.active_count > 0;
}

void maybe_publish_current_xmark_hud_state(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_HUD_SHARED_VALUES_ENABLED", true) &&
        !env_bool("MINA_XMARK_HUD_STATE_FILE_ENABLED", true)) {
        return;
    }
    const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
    const bool should_write =
        hud_state.active_count > 0 ||
        g_last_hud_state_had_active ||
        (g_last_hud_write_ms == 0);
    if (!should_write) {
        return;
    }
    if (g_last_hud_write_ms && now_ms - g_last_hud_write_ms < g_hud_write_interval_ms) {
        return;
    }
    publish_xmark_hud_state(hud_state);
    write_hud_state_file(now_ms, hud_state);
    g_last_hud_write_ms = now_ms;
    g_last_hud_state_had_active = hud_state.active_count > 0;
}

void write_state_file(
    int game_state,
    unsigned int room_index,
    float room_time,
    float player_x,
    float player_y,
    unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_ROOM_STATE_WRITE_ENABLED", true) || !g_state_path[0]) {
        return;
    }

    if (!env_bool("MINA_XMARK_ROOM_STATE_VERBOSE", true)) {
        char tmp_path[MAX_PATH * 4]{};
        std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_state_path);
        FILE *file = nullptr;
        if (fopen_s(&file, tmp_path, "wb") != 0 || !file) {
            return;
        }
        std::fprintf(file, "version=2\n");
        std::fprintf(file, "tick=%llu\n", now_ms);
        std::fprintf(file, "gameState=%d\n", game_state);
        std::fprintf(file, "roomIndex=%u\n", room_index);
        std::fprintf(file, "roomIndexHex=0x%08X\n", room_index);
        std::fprintf(file, "roomTime=%.6f\n", static_cast<double>(room_time));
        std::fprintf(file, "playerX=%.6f\n", static_cast<double>(player_x));
        std::fprintf(file, "playerY=%.6f\n", static_cast<double>(player_y));
        std::fclose(file);
        MoveFileExA(tmp_path, g_state_path, MOVEFILE_REPLACE_EXISTING);
        return;
    }

    const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
    publish_xmark_hud_state(hud_state);
    write_hud_state_file(now_ms, hud_state);
    unsigned int active_attachment_count = 0;
    const XMarkAttachment *first_attachment = nullptr;
    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active) {
            continue;
        }
        ++active_attachment_count;
        if (!first_attachment) {
            first_attachment = &attachment;
        }
    }
    unsigned int active_burn_count = 0;
    const XMarkBurnEffect *first_burn = nullptr;
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active || now_ms >= burn.expires_ms) {
            continue;
        }
        ++active_burn_count;
        if (!first_burn) {
            first_burn = &burn;
        }
    }
    unsigned int pending_visual_mark_count = 0;
    const XMarkPendingVisualMark *first_pending_visual_mark = nullptr;
    for (const XMarkPendingVisualMark &pending : g_pending_visual_marks) {
        if (!pending.active) {
            continue;
        }
        if (now_ms >= pending.expires_ms) {
            continue;
        }
        ++pending_visual_mark_count;
        if (!first_pending_visual_mark) {
            first_pending_visual_mark = &pending;
        }
    }
    read_enemy_visual_state_file(now_ms, false);
    const XMarkVisualEnemyHost *first_visual_host =
        g_visual_enemy_host_count > 0 ? &g_visual_enemy_hosts[0] : nullptr;
    const Vec3 first_visual_render_position =
        first_visual_host ? visual_host_render_position(*first_visual_host) : Vec3{0.0f, 0.0f, 0.0f};
    const XMarkOfficialEnemyHost *first_official_enemy =
        g_official_enemy_host_count > 0 ? &g_official_enemy_hosts[0] : nullptr;
    XMarkVisualEnemyHost first_attachment_visual_host{};
    bool first_attachment_visual_host_present = false;
    Vec3 first_attachment_visual_host_render_position{0.0f, 0.0f, 0.0f};
    unsigned long long first_attachment_visual_host_age_ms = 0;
    XMarkOfficialEnemyHost first_attachment_official_host{};
    bool first_attachment_official_host_present = false;
    Vec3 first_attachment_official_position{0.0f, 0.0f, 0.0f};
    unsigned long long first_attachment_official_host_age_ms = 0;
    Vec3 first_attachment_target_center{0.0f, 0.0f, 0.0f};
    bool first_attachment_target_center_present = false;
    float first_attachment_visual_drift_x = 0.0f;
    float first_attachment_visual_drift_y = 0.0f;
    float first_attachment_visual_drift = 0.0f;
    float first_attachment_official_drift_x = 0.0f;
    float first_attachment_official_drift_y = 0.0f;
    float first_attachment_official_drift = 0.0f;
    if (first_attachment && first_attachment->has_last_position) {
        first_attachment_target_center =
            xmark_attachment_target_position_from_mark(first_attachment->last_position);
        first_attachment_target_center_present = true;
    }
    if (first_attachment &&
        first_attachment->visual_follow &&
        first_attachment->visual_key &&
        visual_enemy_host_by_key(first_attachment->visual_key, &first_attachment_visual_host, now_ms)) {
        first_attachment_visual_host_present = true;
        first_attachment_visual_host_render_position =
            visual_host_render_position(first_attachment_visual_host);
        if (first_attachment_visual_host.last_seen_ms && now_ms >= first_attachment_visual_host.last_seen_ms) {
            first_attachment_visual_host_age_ms = now_ms - first_attachment_visual_host.last_seen_ms;
        }
        if (first_attachment_target_center_present) {
            first_attachment_visual_drift_x =
                first_attachment_target_center.x - first_attachment_visual_host_render_position.x;
            first_attachment_visual_drift_y =
                first_attachment_target_center.y - first_attachment_visual_host_render_position.y;
            first_attachment_visual_drift = std::sqrt(
                first_attachment_visual_drift_x * first_attachment_visual_drift_x +
                first_attachment_visual_drift_y * first_attachment_visual_drift_y);
        }
    }
    if (first_attachment &&
        first_attachment->official_follow &&
        official_enemy_host_for_attachment(*first_attachment, &first_attachment_official_host)) {
        first_attachment_official_host_present = true;
        first_attachment_official_position = first_attachment_official_host.position;
        if (first_attachment_official_host.last_seen_ms && now_ms >= first_attachment_official_host.last_seen_ms) {
            first_attachment_official_host_age_ms = now_ms - first_attachment_official_host.last_seen_ms;
        }
        if (first_attachment_target_center_present) {
            first_attachment_official_drift_x =
                first_attachment_target_center.x - first_attachment_official_position.x;
            first_attachment_official_drift_y =
                first_attachment_target_center.y - first_attachment_official_position.y;
            first_attachment_official_drift = std::sqrt(
                first_attachment_official_drift_x * first_attachment_official_drift_x +
                first_attachment_official_drift_y * first_attachment_official_drift_y);
        }
    }
    XMarkBasicFrameState basic_frame_state{};
    bool has_basic_frame_state = read_basic_frame_state(&basic_frame_state);
    const unsigned int basic_frame_state_max_age_ms = env_uint("MINA_XMARK_D3D12_BASIC_FRAME_MAX_AGE_MS", 180);
    if ((!has_basic_frame_state ||
         (basic_frame_state_max_age_ms && now_ms > basic_frame_state.tick + basic_frame_state_max_age_ms)) &&
        read_modapi_player_basic_frame_state(now_ms, &basic_frame_state)) {
        has_basic_frame_state = true;
    }
    const unsigned long long basic_frame_age_ms =
        has_basic_frame_state && now_ms >= basic_frame_state.tick ? now_ms - basic_frame_state.tick : 0ull;
    unsigned int basic_candidate_active_count = 0;
    unsigned int basic_candidate_recent_overlap_count = 0;
    unsigned long long basic_candidate_newest_overlap_age_ms = 0;
    bool basic_candidate_has_newest_overlap = false;
    const unsigned int basic_candidate_state_window_ms =
        env_uint("MINA_XMARK_BASIC_HEALTH_CANDIDATE_OVERLAP_MAX_AGE_MS", 700);
    for (const XMarkBasicHitCandidate &candidate : g_basic_hit_candidates) {
        if (!candidate.active || !candidate.key) {
            continue;
        }
        ++basic_candidate_active_count;
        if (!candidate.last_overlap_ms || now_ms < candidate.last_overlap_ms) {
            continue;
        }
        const unsigned long long age_ms = now_ms - candidate.last_overlap_ms;
        if (!basic_candidate_state_window_ms || age_ms <= basic_candidate_state_window_ms) {
            ++basic_candidate_recent_overlap_count;
            if (!basic_candidate_has_newest_overlap || age_ms < basic_candidate_newest_overlap_age_ms) {
                basic_candidate_has_newest_overlap = true;
                basic_candidate_newest_overlap_age_ms = age_ms;
            }
        }
    }

    char tmp_path[MAX_PATH * 4]{};
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", g_state_path);

    FILE *file = nullptr;
    if (fopen_s(&file, tmp_path, "wb") != 0 || !file) {
        return;
    }
    std::fprintf(file, "version=2\n");
    std::fprintf(file, "tick=%llu\n", now_ms);
    std::fprintf(file, "gameState=%d\n", game_state);
    std::fprintf(file, "roomIndex=%u\n", room_index);
    std::fprintf(file, "roomIndexHex=0x%08X\n", room_index);
    std::fprintf(file, "roomTime=%.6f\n", static_cast<double>(room_time));
    std::fprintf(file, "playerX=%.6f\n", static_cast<double>(player_x));
    std::fprintf(file, "playerY=%.6f\n", static_cast<double>(player_y));
    int current_weapon_item = -1;
    int current_weapon_index = -1;
    int current_weapon_level = -1;
    if (g_mina && g_mina->PlayerGetWeapon_ItemType) {
        __try {
            current_weapon_item = g_mina->PlayerGetWeapon_ItemType();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            current_weapon_item = -1;
        }
    }
    if (g_mina && g_mina->PlayerGetWeapon_WeaponIndex) {
        __try {
            current_weapon_index = g_mina->PlayerGetWeapon_WeaponIndex();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            current_weapon_index = -1;
        }
    }
    if (g_mina && g_mina->PlayerGetWeaponLevel_ItemType && current_weapon_item >= 0) {
        __try {
            current_weapon_level = g_mina->PlayerGetWeaponLevel_ItemType(current_weapon_item);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            current_weapon_level = -1;
        }
    }
    std::fprintf(file, "currentWeaponItem=%d\n", current_weapon_item);
    std::fprintf(file, "currentWeaponIndex=%d\n", current_weapon_index);
    std::fprintf(file, "currentWeaponLevel=%d\n", current_weapon_level);
    std::fprintf(file, "forceClaymoreDefault=%u\n", env_bool("MINA_XMARK_FORCE_CLAYMORE_DEFAULT", false) ? 1u : 0u);
    std::fprintf(file, "forceClaymoreTargetItem=%u\n", env_uint("MINA_XMARK_FORCE_CLAYMORE_ITEM_TYPE", kItemType_Hammer));
    std::fprintf(file, "commandFileEnabled=%u\n", g_command_path[0] ? 1u : 0u);
    std::fprintf(file, "commandFile=%s\n", g_command_path[0] ? g_command_path : "-");
    std::fprintf(file, "debugHotkeysRequireFocus=%u\n", env_bool("MINA_XMARK_REQUIRE_FOCUS_FOR_DEBUG_KEYS", true) ? 1u : 0u);
    std::fprintf(file, "debugHotkeysFocused=%u\n", mina_window_has_foreground() ? 1u : 0u);
    std::fprintf(file, "nativeSpawnCount=%u\n", g_native_spawn_count);
    std::fprintf(file, "nativeDirectCount=%u\n", g_native_direct_count);
    std::fprintf(file, "nativeF0029Count=%u\n", g_native_f0029_count);
    std::fprintf(file, "nativeCraterCount=%u\n", g_native_crater_count);
    std::fprintf(file, "nativeSpawnEnabled=%u\n", g_native_spawn_enabled ? 1u : 0u);
    std::fprintf(file, "nativeDirectEnabled=%u\n", g_native_direct_enabled ? 1u : 0u);
    std::fprintf(file, "nativeSpawnLimited=%u\n", native_spawn_limited() ? 1u : 0u);
    std::fprintf(file, "gameSingletonGlobal=0x%p\n", reinterpret_cast<void *>(game_singleton_global_address()));
    std::fprintf(file, "gameSingleton=0x%p\n", reinterpret_cast<void *>(game_singleton()));
    std::fprintf(file, "worldScaleAddress=0x%p\n", reinterpret_cast<void *>(world_scale_address()));
    std::fprintf(file, "playerEntity=0x%p\n", reinterpret_cast<void *>(player_entity()));
    std::fprintf(file, "entityManager=0x%p\n", reinterpret_cast<void *>(entity_manager()));
    std::fprintf(file, "xmarkRenderBackendEnabled=%u\n", g_xmark_render_backend_enabled ? 1u : 0u);
    std::fprintf(file, "xmarkRenderApiAvailable=%u\n", g_xmark_render_backend_available ? 1u : 0u);
    std::fprintf(file, "xmarkRenderBackendReady=%u\n", g_xmark_render_backend_ready ? 1u : 0u);
    std::fprintf(file, "xmarkRenderBackendQuads=%u\n", g_xmark_render_backend_quads);
    std::fprintf(file, "xmarkRenderDrawCalls=%llu\n", g_xmark_render_draw_calls);
    std::fprintf(file, "xmarkRenderLastDrawQuads=%u\n", g_xmark_render_last_draw_quads);
    std::fprintf(file, "xmarkRenderLastQuadValid=%u\n", g_xmark_render_last_quad_valid ? 1u : 0u);
    std::fprintf(file, "xmarkRenderLastQuadX=%.6f\n", static_cast<double>(g_xmark_render_last_quad_x));
    std::fprintf(file, "xmarkRenderLastQuadY=%.6f\n", static_cast<double>(g_xmark_render_last_quad_y));
    std::fprintf(file, "xmarkRenderLastQuadZ=%.6f\n", static_cast<double>(g_xmark_render_last_quad_z));
    std::fprintf(file, "xmarkRenderLastQuadHalfW=%.6f\n", static_cast<double>(g_xmark_render_last_quad_half_w));
    std::fprintf(file, "xmarkRenderLastQuadHalfH=%.6f\n", static_cast<double>(g_xmark_render_last_quad_half_h));
    std::fprintf(file, "xmarkRenderLastQuadFrame=%u\n", g_xmark_render_last_quad_frame);
    std::fprintf(file, "xmarkRenderRefreshVisualFollow=%u\n", env_bool("MINA_XMARK_RENDER_REFRESH_VISUAL_FOLLOW", true) ? 1u : 0u);
    std::fprintf(file, "xmarkRenderVisualFollowRefreshMs=%u\n", env_uint("MINA_XMARK_RENDER_VISUAL_FOLLOW_REFRESH_MS", 8));
    std::fprintf(file, "xmarkRuntimeOverlayHiddenForPause=%u\n", xmark_runtime_overlays_hidden_for_pause() ? 1u : 0u);
    std::fprintf(file, "xmarkPauseRoomtimeStillMs=%llu\n", g_xmark_pause_roomtime_still_ms);
    std::fprintf(file, "xmarkGameClockReady=%u\n", g_xmark_game_clock_ready ? 1u : 0u);
    std::fprintf(file, "xmarkGameClockMs=%llu\n", g_xmark_game_clock_ms);
    std::fprintf(file, "xmarkGameClockUpdates=%llu\n", g_xmark_game_clock_updates);
    std::fprintf(file, "xmarkGameClockLastElapsed=%.6f\n", static_cast<double>(g_xmark_game_clock_last_elapsed));
    std::fprintf(file, "xmarkMarkerDebugDrawApiAvailable=%u\n", g_xmark_marker_debug_draw_available ? 1u : 0u);
    std::fprintf(file, "xmarkMarkerDebugDrawReady=%u\n", g_xmark_marker_debug_draw_ready ? 1u : 0u);
    std::fprintf(file, "xmarkMarkerDebugDrawCalls=%llu\n", g_xmark_marker_debug_draw_calls);
    std::fprintf(file, "xmarkMarkerDebugLastFrame=%u\n", g_xmark_marker_debug_last_frame);
    std::fprintf(file, "xmarkMarkerDebugDrawStatus=%s\n", g_xmark_marker_debug_draw_status);
    std::fprintf(file, "xmarkBurnDebugDrawReady=%u\n", g_xmark_burn_debug_draw_ready ? 1u : 0u);
    std::fprintf(file, "xmarkBurnDebugDrawCalls=%llu\n", g_xmark_burn_debug_draw_calls);
    std::fprintf(file, "xmarkBurnDebugDrawStatus=%s\n", g_xmark_burn_debug_draw_status);
    std::fprintf(file, "xmarkHudDebugDrawReady=%u\n", g_xmark_hud_debug_draw_ready ? 1u : 0u);
    std::fprintf(file, "xmarkHudDebugDrawCalls=%llu\n", g_xmark_hud_debug_draw_calls);
    std::fprintf(file, "xmarkHudDebugDrawStatus=%s\n", g_xmark_hud_debug_draw_status);
    std::fprintf(file, "xmarkHudRenderApiAvailable=%u\n", g_xmark_hud_render_backend_available ? 1u : 0u);
    std::fprintf(file, "xmarkHudRenderReady=%u\n", g_xmark_hud_render_backend_ready ? 1u : 0u);
    std::fprintf(file, "xmarkHudRenderQuads=%u\n", g_xmark_hud_render_quads);
    std::fprintf(file, "xmarkHudRenderDrawCalls=%llu\n", g_xmark_hud_render_draw_calls);
    std::fprintf(file, "xmarkHudRenderStatus=%s\n", g_xmark_hud_render_backend_status);
    std::fprintf(file, "xmarkHudWorldRenderQuads=%u\n", g_xmark_hud_world_render_quads);
    std::fprintf(file, "xmarkHudWorldRenderLastX=%.6f\n", static_cast<double>(g_xmark_hud_world_render_last_x));
    std::fprintf(file, "xmarkHudWorldRenderLastY=%.6f\n", static_cast<double>(g_xmark_hud_world_render_last_y));
    std::fprintf(file, "f0029RenderActiveCount=%u\n", active_f0029_render_effect_count(now_ms));
    std::fprintf(file, "xmarkBurnActiveCount=%u\n", active_burn_count);
    std::fprintf(file, "xmarkBurnFirstTarget=0x%p\n", first_burn ? reinterpret_cast<void *>(first_burn->target) : nullptr);
    std::fprintf(file, "xmarkBurnFirstVisualKey=0x%llX\n", first_burn ? first_burn->visual_key : 0ull);
    std::fprintf(file, "xmarkBurnFirstOfficialFollow=%u\n", first_burn && first_burn->official_follow ? 1u : 0u);
    std::fprintf(file, "xmarkBurnFirstOfficialCore=0x%p\n", first_burn ? reinterpret_cast<void *>(first_burn->official_combat_core) : nullptr);
    std::fprintf(file, "xmarkBurnFirstTickCount=%u\n", first_burn ? first_burn->tick_count : 0u);
    std::fprintf(file, "xmarkBurnFirstTickAttempts=%u\n", first_burn ? first_burn->tick_attempt_count : 0u);
    std::fprintf(file, "xmarkBurnFirstTicksApplied=%u\n", first_burn ? first_burn->tick_applied_count : 0u);
    std::fprintf(file, "xmarkBurnFirstTicksFailed=%u\n", first_burn ? first_burn->tick_failed_count : 0u);
    std::fprintf(file, "xmarkBurnFirstVisualEmitFrames=%u\n", first_burn ? first_burn->visual_emit_frame_count : 0u);
    std::fprintf(file, "xmarkBurnFirstLastTickMs=%llu\n", first_burn ? first_burn->last_tick_ms : 0ull);
    std::fprintf(file, "xmarkBurnFirstNextTickMs=%llu\n", first_burn ? first_burn->next_tick_ms : 0ull);
    std::fprintf(file, "xmarkBurnFirstExpiresMs=%llu\n", first_burn ? first_burn->expires_ms : 0ull);
    std::fprintf(
        file,
        "xmarkBurnFirstX=%.6f\n",
        first_burn && first_burn->has_last_position ? static_cast<double>(first_burn->last_position.x) : 0.0);
    std::fprintf(
        file,
        "xmarkBurnFirstY=%.6f\n",
        first_burn && first_burn->has_last_position ? static_cast<double>(first_burn->last_position.y) : 0.0);
    std::fprintf(
        file,
        "xmarkBurnFirstHealthOffset=0x%X\n",
        first_burn ? first_burn->target_health_offset : 0u);
    std::fprintf(
        file,
        "xmarkBurnFirstHealthKind=%u\n",
        first_burn ? first_burn->target_health_kind : 0u);
    std::fprintf(file, "lastChargedConsumeMs=%llu\n", g_last_charged_consume_ms);
    std::fprintf(file, "lastChargedConsumeConsumed=%u\n", g_last_charged_consume_consumed);
    std::fprintf(file, "lastChargedConsumeCandidates=%u\n", g_last_charged_consume_candidates);
    std::fprintf(file, "lastChargedConsumeDirection=%s\n", direction_name(g_last_charged_consume_direction));
    std::fprintf(file, "lastChargedConsumeImpactX=%.6f\n", static_cast<double>(g_last_charged_consume_impact_x));
    std::fprintf(file, "lastChargedConsumeImpactY=%.6f\n", static_cast<double>(g_last_charged_consume_impact_y));
    std::fprintf(file, "lastChargedConsumeRadiusX=%.6f\n", static_cast<double>(g_last_charged_consume_radius_x));
    std::fprintf(file, "lastChargedConsumeRadiusY=%.6f\n", static_cast<double>(g_last_charged_consume_radius_y));
    std::fprintf(file, "lastChargedConsumeEffectiveRadiusX=%.6f\n", static_cast<double>(g_last_charged_consume_effective_radius_x));
    std::fprintf(file, "lastChargedConsumeEffectiveRadiusY=%.6f\n", static_cast<double>(g_last_charged_consume_effective_radius_y));
    std::fprintf(file, "xmarkRenderBackendStatus=%s\n", g_xmark_render_backend_status);
    std::fprintf(file, "testEnemyHarnessEnabled=%u\n", g_test_enemy_harness_enabled ? 1u : 0u);
    std::fprintf(file, "testEnemyUseApiSpawn=%u\n", test_enemy_uses_api_spawn() ? 1u : 0u);
    std::fprintf(file, "testEnemyAutoSpawn=%u\n", g_test_enemy_auto_spawn ? 1u : 0u);
    std::fprintf(file, "testEnemyAutoSpawnDone=%u\n", g_test_enemy_auto_spawn_done ? 1u : 0u);
    std::fprintf(
        file,
        "testEnemySpawnContextReady=%u\n",
        test_enemy_spawn_context_ready(game_state, room_index, room_time) ? 1u : 0u);
    std::fprintf(file, "testEnemySpawnCount=%u\n", g_test_enemy_spawn_count);
    std::fprintf(file, "testEnemyType=%u\n", g_test_enemy_type);
    std::fprintf(file, "testEnemyResolvePending=%u\n", g_test_enemy_resolve_pending ? 1u : 0u);
    std::fprintf(file, "testEnemyPreSpawnCount=%u\n", g_test_enemy_pre_spawn_count);
    std::fprintf(file, "lastTestEnemy=0x%p\n", reinterpret_cast<void *>(g_last_test_enemy_entity));
    std::fprintf(file, "lastTestEnemyResolvedMs=%llu\n", g_last_test_enemy_resolved_ms);
    std::fprintf(file, "lastTestEnemyX=%.6f\n", static_cast<double>(g_last_test_enemy_position.x));
    std::fprintf(file, "lastTestEnemyY=%.6f\n", static_cast<double>(g_last_test_enemy_position.y));
    std::fprintf(file, "lastTestEnemyZ=%.6f\n", static_cast<double>(g_last_test_enemy_position.z));
    std::fprintf(file, "integratedF0029Enabled=%u\n", g_integrated_f0029_enabled ? 1u : 0u);
    std::fprintf(file, "nativeCraterEnabled=%u\n", g_native_crater_enabled ? 1u : 0u);
    std::fprintf(file, "integratedF0029CooldownMs=%u\n", g_integrated_f0029_cooldown_ms);
    std::fprintf(file, "basicFrameStatePresent=%u\n", has_basic_frame_state ? 1u : 0u);
    std::fprintf(file, "basicFrameStateTick=%llu\n", has_basic_frame_state ? basic_frame_state.tick : 0ull);
    std::fprintf(file, "basicFrameStateAgeMs=%llu\n", basic_frame_age_ms);
    std::fprintf(file, "basicFrameStateDraw=%llu\n", has_basic_frame_state ? basic_frame_state.draw : 0ull);
    std::fprintf(file, "basicFrameStateFrame=%s\n", has_basic_frame_state && basic_frame_state.frame[0] ? basic_frame_state.frame : "-");
    std::fprintf(file, "basicFrameStateDirection=%s\n", has_basic_frame_state ? direction_name(basic_frame_state.direction) : "-");
    std::fprintf(file, "basicFrameStateSideSmear=%u\n", has_basic_frame_state && basic_frame_state.side_smear ? 1u : 0u);
    std::fprintf(file, "basicFrameStateHasGeometry=%u\n", has_basic_frame_state && basic_frame_state.has_geometry ? 1u : 0u);
    std::fprintf(file, "basicFrameStateHasContact=%u\n", has_basic_frame_state && basic_frame_state.has_contact ? 1u : 0u);
    std::fprintf(file, "basicFrameStateContactX=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.contact_x) : 0.0);
    std::fprintf(file, "basicFrameStateContactY=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.contact_y) : 0.0);
    std::fprintf(file, "basicFrameStateMinX=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.min_x) : 0.0);
    std::fprintf(file, "basicFrameStateMaxX=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.max_x) : 0.0);
    std::fprintf(file, "basicFrameStateMinY=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.min_y) : 0.0);
    std::fprintf(file, "basicFrameStateMaxY=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.max_y) : 0.0);
    std::fprintf(file, "basicFrameStateModApi=%u\n", has_basic_frame_state && basic_frame_state.modapi_authoritative ? 1u : 0u);
    std::fprintf(file, "basicFrameStateAnim=0x%p\n", has_basic_frame_state ? reinterpret_cast<void *>(basic_frame_state.anim_component) : nullptr);
    std::fprintf(file, "basicFrameStateSeqFrameIdx=%u\n", has_basic_frame_state ? basic_frame_state.seq_frame_idx : 0u);
    std::fprintf(file, "basicFrameStateNumSeqFrames=%u\n", has_basic_frame_state ? basic_frame_state.num_seq_frames : 0u);
    std::fprintf(file, "basicFrameStateLoops=%u\n", has_basic_frame_state ? basic_frame_state.loops_played : 0u);
    std::fprintf(file, "basicFrameStateFrameTime=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.current_frame_time) : 0.0);
    std::fprintf(file, "basicFrameStatePlayRate=%.6f\n", has_basic_frame_state ? static_cast<double>(basic_frame_state.play_rate) : 0.0);
    std::fprintf(file, "modapiBasicAnimScanNodes=%u\n", g_modapi_anim_scan_nodes);
    std::fprintf(file, "modapiBasicAnimScanHits=%u\n", g_modapi_anim_scan_hits);
    std::fprintf(file, "modapiBasicAnimLastSeq=%s\n", g_modapi_anim_last_seq[0] ? g_modapi_anim_last_seq : "-");
    std::fprintf(file, "modapiBasicAnimLastSeqFull=%s\n", g_modapi_anim_last_seq_full[0] ? g_modapi_anim_last_seq_full : "-");
    std::fprintf(file, "modapiBasicAnimLastFrameIdx=%u\n", g_modapi_anim_last_frame_idx);
    std::fprintf(file, "modapiBasicAnimSeqList=%s\n", g_modapi_anim_seq_list[0] ? g_modapi_anim_seq_list : "-");
    std::fprintf(file, "modapiAttackAnimCached=0x%p\n", reinterpret_cast<void *>(g_player_attack_anim_component));
    std::fprintf(file, "animPropertyProbeCount=%u\n", g_anim_property_probed_component_count);
    std::fprintf(file, "attackOverlayRenderReady=%u\n", g_attack_overlay_render_backend_ready ? 1u : 0u);
    std::fprintf(file, "attackOverlayRenderQuads=%u\n", g_attack_overlay_render_quads);
    std::fprintf(file, "attackOverlayRenderLastFrame=%u\n", g_attack_overlay_render_last_frame);
    std::fprintf(file, "attackOverlayRenderDrawCalls=%llu\n", g_attack_overlay_render_draw_calls);
    std::fprintf(file, "attackOverlayRenderStatus=%s\n", g_attack_overlay_render_backend_status);
    std::fprintf(file, "basicFrameHitRecheckActive=%u\n", g_last_basic_frame_state_for_hit_active ? 1u : 0u);
    std::fprintf(file, "basicFrameHitRecheckTick=%llu\n", g_last_basic_frame_state_for_hit_active ? g_last_basic_frame_state_for_hit.tick : 0ull);
    std::fprintf(file, "basicFrameHitRecheckFrame=%s\n", g_last_basic_frame_state_for_hit_active && g_last_basic_frame_state_for_hit.frame[0] ? g_last_basic_frame_state_for_hit.frame : "-");
    std::fprintf(file, "basicFrameHitRecheckDirection=%s\n", g_last_basic_frame_state_for_hit_active ? direction_name(g_last_basic_frame_state_for_hit.direction) : "-");
    std::fprintf(file, "basicFrameHitRecheckSeenMs=%llu\n", g_last_basic_frame_state_for_hit_seen_ms);
    std::fprintf(file, "basicFrameHitLastEvalMs=%llu\n", g_last_basic_frame_hit_eval_ms);
    std::fprintf(file, "basicProbeActive=%u\n", g_basic_attack_probe.active ? 1u : 0u);
    std::fprintf(file, "basicProbeDirection=%s\n", g_basic_attack_probe.active ? direction_name(g_basic_attack_probe.direction) : "-");
    std::fprintf(file, "basicProbeContactActive=%u\n", g_basic_attack_probe.contact_active ? 1u : 0u);
    std::fprintf(file, "basicProbeBaselineCount=%u\n", g_basic_attack_probe.baseline_count);
    std::fprintf(file, "basicProbeMarkedCount=%u\n", g_basic_attack_probe.marked_count);
    std::fprintf(file, "basicProbeMarkedEnemyCount=%u\n", g_basic_attack_probe.marked_enemy_count);
    std::fprintf(file, "basicProbeStartedMs=%llu\n", g_basic_attack_probe.started_ms);
    std::fprintf(file, "basicProbeExpiresMs=%llu\n", g_basic_attack_probe.expires_ms);
    std::fprintf(file, "playerWorldGateEnabled=%u\n", env_bool("MINA_XMARK_PLAYER_WORLD_GATE", true) ? 1u : 0u);
    std::fprintf(file, "entityHitQueueAvailable=%u\n", xmark_entity_hit_update_queue_api_available() ? 1u : 0u);
    std::fprintf(file, "entityHitQueueRegistered=%u\n", g_xmark_entity_hit_queue_registered ? 1u : 0u);
    std::fprintf(file, "entityHitQueueWorld=0x%p\n", reinterpret_cast<void *>(g_xmark_entity_hit_queue_world));
    std::fprintf(file, "entityHitQueueLastUpdateMs=%llu\n", g_last_xmark_entity_hit_update_ms);
    std::fprintf(file, "entityHitQueueLastProbeUpdateMs=%llu\n", g_last_xmark_entity_hit_probe_update_ms);
    std::fprintf(file, "entityHitQueueCallbacks=%llu\n", g_xmark_entity_hit_queue_callbacks);
    std::fprintf(file, "entityHitQueueProbeUpdates=%llu\n", g_xmark_entity_hit_queue_probe_updates);
    std::fprintf(file, "spawnPointRoomRegistryEnabled=%u\n", xmark_spawn_point_room_registry_enabled() ? 1u : 0u);
    std::fprintf(file, "spawnPointRoomRegistryGeneration=%u\n", g_official_enemy_registry_generation);
    std::fprintf(file, "spawnPointRoomRegistryReconciles=%llu\n", g_official_enemy_registry_reconcile_count);
    std::fprintf(file, "spawnPointRoomRegistryTargetedRefreshes=%llu\n", g_official_enemy_registry_targeted_refresh_count);
    std::fprintf(file, "spawnPointRoomRegistryInvalidations=%llu\n", g_official_enemy_registry_invalid_count);
    std::fprintf(file, "basicRequireHealthDropForMark=%u\n", basic_mark_requires_health_drop() ? 1u : 0u);
    std::fprintf(file, "basicCandidateActiveCount=%u\n", basic_candidate_active_count);
    std::fprintf(file, "basicCandidateRecentOverlapCount=%u\n", basic_candidate_recent_overlap_count);
    std::fprintf(file, "basicCandidateNewestOverlapAgeMs=%llu\n", basic_candidate_has_newest_overlap ? basic_candidate_newest_overlap_age_ms : 0ull);
    std::fprintf(file, "basicHealthLastDropMs=%llu\n", g_last_basic_health_drop_ms);
    std::fprintf(file, "basicHealthLastDropTarget=0x%p\n", reinterpret_cast<void *>(g_last_basic_health_drop_target));
    std::fprintf(file, "basicHealthLastDropAmount=%.6f\n", static_cast<double>(g_last_basic_health_drop_amount));
    std::fprintf(file, "basicHealthLastDropBefore=%.6f\n", static_cast<double>(g_last_basic_health_drop_before));
    std::fprintf(file, "basicHealthLastDropAfter=%.6f\n", static_cast<double>(g_last_basic_health_drop_after));
    std::fprintf(file, "attachmentLabEnabled=%u\n", env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) ? 1u : 0u);
    std::fprintf(file, "attachmentLabMarked=%u\n", g_attachment_lab_marked ? 1u : 0u);
    std::fprintf(file, "attachmentLabTarget=0x%p\n", reinterpret_cast<void *>(g_attachment_lab_target));
    std::fprintf(file, "attachmentLabAnchorOffset=0x%X\n", g_attachment_lab_anchor_offset);
    std::fprintf(file, "xmarkAttachmentActiveCount=%u\n", active_attachment_count);
    std::fprintf(file, "xmarkAttachmentFirstTarget=0x%p\n", first_attachment ? reinterpret_cast<void *>(first_attachment->target) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstVisualFollow=%u\n", first_attachment && first_attachment->visual_follow ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstVisualKey=0x%llX\n", first_attachment ? first_attachment->visual_key : 0ull);
    std::fprintf(file, "xmarkAttachmentFirstOfficialFollow=%u\n", first_attachment && first_attachment->official_follow ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstOfficialCore=0x%p\n", first_attachment ? reinterpret_cast<void *>(first_attachment->official_combat_core) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstVisualResolvedMs=%llu\n", first_attachment ? first_attachment->last_visual_resolved_ms : 0ull);
    std::fprintf(file, "xmarkAttachmentFirstVisualMissingMs=%llu\n", first_attachment ? first_attachment->last_visual_missing_ms : 0ull);
    std::fprintf(file, "xmarkAttachmentFirstVisualMissingCount=%u\n", first_attachment ? first_attachment->visual_missing_count : 0u);
    std::fprintf(
        file,
        "xmarkAttachmentFirstVisualHoldLast=%u\n",
        first_attachment &&
            first_attachment->visual_follow &&
            first_attachment->has_last_position &&
            first_attachment->last_visual_missing_ms > first_attachment->last_visual_resolved_ms ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstEffect=0x%p\n", first_attachment ? reinterpret_cast<void *>(first_attachment->effect) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstChild=0x%p\n", first_attachment ? reinterpret_cast<void *>(first_attachment->child) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstTargetSource=0x%p\n", first_attachment ? reinterpret_cast<void *>(first_attachment->target_source_base) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstTargetSourceOffset=0x%llX\n", first_attachment ? static_cast<unsigned long long>(first_attachment->target_source_offset) : 0ull);
    std::fprintf(file, "xmarkAttachmentFirstRenderBackend=%u\n", first_attachment && first_attachment->render_backend ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstMarkerDebugDraw=%u\n", first_attachment && first_attachment->marker_debug_draw ? 1u : 0u);
    std::fprintf(
        file,
        "xmarkAttachmentFirstX=%.6f\n",
        first_attachment && first_attachment->has_last_position ? static_cast<double>(first_attachment->last_position.x) : 0.0);
    std::fprintf(
        file,
        "xmarkAttachmentFirstY=%.6f\n",
        first_attachment && first_attachment->has_last_position ? static_cast<double>(first_attachment->last_position.y) : 0.0);
    std::fprintf(
        file,
        "xmarkAttachmentFirstZ=%.6f\n",
        first_attachment && first_attachment->has_last_position ? static_cast<double>(first_attachment->last_position.z) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstTargetCenterPresent=%u\n", first_attachment_target_center_present ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstTargetCenterX=%.6f\n", static_cast<double>(first_attachment_target_center.x));
    std::fprintf(file, "xmarkAttachmentFirstTargetCenterY=%.6f\n", static_cast<double>(first_attachment_target_center.y));
    std::fprintf(file, "xmarkAttachmentFirstVisualHostPresent=%u\n", first_attachment_visual_host_present ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstVisualHostEntry=%s\n", first_attachment_visual_host_present && first_attachment_visual_host.entry[0] ? first_attachment_visual_host.entry : "-");
    std::fprintf(file, "xmarkAttachmentFirstVisualHostStem=%s\n", first_attachment_visual_host_present && first_attachment_visual_host.stem[0] ? first_attachment_visual_host.stem : "-");
    std::fprintf(file, "xmarkAttachmentFirstVisualHostCatalog=%s\n", first_attachment_visual_host_present && first_attachment_visual_host.catalog[0] ? first_attachment_visual_host.catalog : "-");
    std::fprintf(file, "xmarkAttachmentFirstVisualHostRawX=%.6f\n", first_attachment_visual_host_present ? static_cast<double>(first_attachment_visual_host.position.x) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstVisualHostRawY=%.6f\n", first_attachment_visual_host_present ? static_cast<double>(first_attachment_visual_host.position.y) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstVisualHostRenderX=%.6f\n", first_attachment_visual_host_present ? static_cast<double>(first_attachment_visual_host_render_position.x) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstVisualHostRenderY=%.6f\n", first_attachment_visual_host_present ? static_cast<double>(first_attachment_visual_host_render_position.y) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstVisualHostAgeMs=%llu\n", first_attachment_visual_host_age_ms);
    std::fprintf(file, "xmarkAttachmentFirstVisualDriftX=%.6f\n", static_cast<double>(first_attachment_visual_drift_x));
    std::fprintf(file, "xmarkAttachmentFirstVisualDriftY=%.6f\n", static_cast<double>(first_attachment_visual_drift_y));
    std::fprintf(file, "xmarkAttachmentFirstVisualDrift=%.6f\n", static_cast<double>(first_attachment_visual_drift));
    std::fprintf(file, "xmarkAttachmentFirstLastHostRenderX=%.6f\n", first_attachment ? static_cast<double>(first_attachment->last_visual_host_render_position.x) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstLastHostRenderY=%.6f\n", first_attachment ? static_cast<double>(first_attachment->last_visual_host_render_position.y) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstLastHostSeenMs=%llu\n", first_attachment ? first_attachment->last_visual_host_seen_ms : 0ull);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostPresent=%u\n", first_attachment_official_host_present ? 1u : 0u);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostEntity=0x%p\n", first_attachment_official_host_present ? reinterpret_cast<void *>(first_attachment_official_host.entity) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostCore=0x%p\n", first_attachment_official_host_present ? reinterpret_cast<void *>(first_attachment_official_host.combat_core) : nullptr);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostType=%s\n", first_attachment_official_host_present && first_attachment_official_host.component_type[0] ? first_attachment_official_host.component_type : "-");
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostX=%.6f\n", first_attachment_official_host_present ? static_cast<double>(first_attachment_official_position.x) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostY=%.6f\n", first_attachment_official_host_present ? static_cast<double>(first_attachment_official_position.y) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstOfficialHostAgeMs=%llu\n", first_attachment_official_host_age_ms);
    std::fprintf(file, "xmarkAttachmentFirstOfficialDriftX=%.6f\n", static_cast<double>(first_attachment_official_drift_x));
    std::fprintf(file, "xmarkAttachmentFirstOfficialDriftY=%.6f\n", static_cast<double>(first_attachment_official_drift_y));
    std::fprintf(file, "xmarkAttachmentFirstOfficialDrift=%.6f\n", static_cast<double>(first_attachment_official_drift));
    std::fprintf(
        file,
        "xmarkAttachmentFirstHalfW=%.6f\n",
        first_attachment ? static_cast<double>(first_attachment->render_half_w) : 0.0);
    std::fprintf(
        file,
        "xmarkAttachmentFirstHalfH=%.6f\n",
        first_attachment ? static_cast<double>(first_attachment->render_half_h) : 0.0);
    std::fprintf(file, "xmarkAttachmentFirstEffectSlots=%u\n", first_attachment ? first_attachment->effect_position_count : 0u);
    std::fprintf(file, "xmarkAttachmentFirstChildSlots=%u\n", first_attachment ? first_attachment->child_position_count : 0u);
    std::fprintf(file, "xmarkAttachmentFirstExtraSlots=%u\n", first_attachment ? first_attachment->extra_position_count : 0u);
    std::fprintf(file, "xmarkAttachmentWriteEnabled=%u\n", env_bool("MINA_XMARK_ATTACHMENT_WRITE_ENABLED", true) ? 1u : 0u);
    std::fprintf(file, "xmarkPendingVisualMarkCount=%u\n", pending_visual_mark_count);
    std::fprintf(file, "xmarkPendingVisualMarkFirstKey=0x%llX\n", first_pending_visual_mark ? first_pending_visual_mark->host.key : 0ull);
    std::fprintf(file, "xmarkPendingVisualMarkFirstReason=%s\n", first_pending_visual_mark && first_pending_visual_mark->reason[0] ? first_pending_visual_mark->reason : "-");
    std::fprintf(file, "xmarkPendingVisualMarkFirstAttempts=%u\n", first_pending_visual_mark ? first_pending_visual_mark->attempts : 0u);
    std::fprintf(file, "xmarkPendingVisualMarkFirstNextMs=%llu\n", first_pending_visual_mark ? first_pending_visual_mark->next_ms : 0ull);
    std::fprintf(file, "xmarkPendingVisualMarkFirstExpiresMs=%llu\n", first_pending_visual_mark ? first_pending_visual_mark->expires_ms : 0ull);
    std::fprintf(file, "xmarkPendingVisualMarkSpawnAttempts=%u\n", g_pending_visual_mark_spawn_attempts);
    std::fprintf(file, "xmarkPendingVisualMarkRepairs=%u\n", g_pending_visual_mark_repairs);
    std::fprintf(file, "xmarkPendingVisualMarkRetries=%u\n", g_pending_visual_mark_retries);
    std::fprintf(file, "xmarkPendingVisualMarkRefreshMisses=%u\n", g_pending_visual_mark_refresh_misses);
    std::fprintf(file, "xmarkPendingVisualMarkExpired=%u\n", g_pending_visual_mark_expired);
    std::fprintf(file, "xmarkHudActiveCount=%u\n", hud_state.active_count);
    std::fprintf(file, "xmarkHudMode=%u\n", hud_state.mode);
    std::fprintf(file, "xmarkHudVisible=%u\n", hud_state.visible ? 1u : 0u);
    std::fprintf(file, "xmarkHudBlinking=%u\n", hud_state.blinking ? 1u : 0u);
    std::fprintf(file, "xmarkHudDamagePopupVisible=%u\n", hud_state.damage_popup_visible ? 1u : 0u);
    std::fprintf(file, "xmarkHudFirstTarget=0x%p\n", reinterpret_cast<void *>(hud_state.first_target));
    std::fprintf(file, "xmarkHudFirstExpiresMs=%llu\n", hud_state.first_expires_ms);
    std::fprintf(file, "xmarkHudFirstBlinkStartMs=%llu\n", hud_state.first_blink_start_ms);
    std::fprintf(file, "xmarkHudDamagePopupUntilMs=%llu\n", hud_state.damage_popup_until_ms);
    std::fprintf(file, "enemyVisualStateTick=%llu\n", g_visual_enemy_state_tick);
    std::fprintf(file, "enemyVisualHostCount=%u\n", g_visual_enemy_host_count);
    std::fprintf(file, "enemyVisualFirstKey=0x%llX\n", first_visual_host ? first_visual_host->key : 0ull);
    std::fprintf(file, "enemyVisualFirstEntry=%s\n", first_visual_host && first_visual_host->entry[0] ? first_visual_host->entry : "-");
    std::fprintf(file, "enemyVisualFirstStem=%s\n", first_visual_host && first_visual_host->stem[0] ? first_visual_host->stem : "-");
    std::fprintf(file, "enemyVisualFirstCatalog=%s\n", first_visual_host && first_visual_host->catalog[0] ? first_visual_host->catalog : "-");
    std::fprintf(file, "enemyVisualFirstX=%.6f\n", first_visual_host ? static_cast<double>(first_visual_host->position.x) : 0.0);
    std::fprintf(file, "enemyVisualFirstY=%.6f\n", first_visual_host ? static_cast<double>(first_visual_host->position.y) : 0.0);
    std::fprintf(file, "enemyVisualFirstRenderX=%.6f\n", first_visual_host ? static_cast<double>(first_visual_render_position.x) : 0.0);
    std::fprintf(file, "enemyVisualFirstRenderY=%.6f\n", first_visual_host ? static_cast<double>(first_visual_render_position.y) : 0.0);
    std::fprintf(file, "enemyVisualFirstLastHurtMs=%llu\n", first_visual_host ? first_visual_host->last_hurt_flash_ms : 0ull);
    std::fprintf(file, "enemyVisualFirstRecentHurt=%u\n", first_visual_host && first_visual_host->recent_hurt ? 1u : 0u);
    std::fprintf(file, "enemyVisualFlipYFromPlayer=%u\n", env_bool("MINA_XMARK_VISUAL_HOST_FLIP_Y_FROM_PLAYER", true) ? 1u : 0u);
    std::fprintf(file, "officialEnemyApiAvailable=%u\n", xmark_component_api_available() ? 1u : 0u);
    std::fprintf(file, "officialEnemyHealthWriteAvailable=%u\n", xmark_combat_core_health_write_api_available() ? 1u : 0u);
    std::fprintf(file, "officialEnemySnapshotValid=%u\n", g_official_enemy_snapshot_valid ? 1u : 0u);
    std::fprintf(file, "officialEnemyApiFault=%u\n", g_official_enemy_api_fault ? 1u : 0u);
    std::fprintf(file, "officialEnemyScanMs=%llu\n", g_last_official_enemy_scan_ms);
    std::fprintf(file, "officialEnemyScanNodes=%u\n", g_official_enemy_scan_nodes);
    std::fprintf(file, "officialEnemyScanFaults=%u\n", g_official_enemy_scan_faults);
    std::fprintf(file, "officialEnemyHostCount=%u\n", g_official_enemy_host_count);
    std::fprintf(file, "officialEnemyFirstEntity=0x%p\n", first_official_enemy ? reinterpret_cast<void *>(first_official_enemy->entity) : nullptr);
    std::fprintf(file, "officialEnemyFirstCombatCore=0x%p\n", first_official_enemy ? reinterpret_cast<void *>(first_official_enemy->combat_core) : nullptr);
    std::fprintf(file, "officialEnemyFirstType=%s\n", first_official_enemy && first_official_enemy->component_type[0] ? first_official_enemy->component_type : "-");
    std::fprintf(file, "officialEnemyFirstX=%.6f\n", first_official_enemy ? static_cast<double>(first_official_enemy->position.x) : 0.0);
    std::fprintf(file, "officialEnemyFirstY=%.6f\n", first_official_enemy ? static_cast<double>(first_official_enemy->position.y) : 0.0);
    std::fprintf(file, "officialEnemyFirstZ=%.6f\n", first_official_enemy ? static_cast<double>(first_official_enemy->position.z) : 0.0);
    std::fprintf(file, "officialEnemyFirstHealth=%.6f\n", first_official_enemy ? static_cast<double>(first_official_enemy->health) : 0.0);
    std::fprintf(file, "officialEnemyFirstHealthMax=%.6f\n", first_official_enemy ? static_cast<double>(first_official_enemy->health_max) : 0.0);
    std::fprintf(file, "officialEnemyFirstSpawnIdentity=%u\n", first_official_enemy && first_official_enemy->spawn_identity_valid ? 1u : 0u);
    std::fprintf(file, "officialEnemyFirstSpawnPoint=0x%p\n", first_official_enemy ? reinterpret_cast<void *>(first_official_enemy->spawn_point) : nullptr);
    std::fprintf(file, "officialEnemyFirstSpawnNameHash=0x%08X\n", first_official_enemy ? first_official_enemy->spawn_name_hash : 0u);
    std::fprintf(file, "officialEnemyFirstSpawnNameLevelHash=0x%016llX\n", first_official_enemy ? first_official_enemy->spawn_name_level_hash : 0ull);
    std::fprintf(file, "officialEnemyFirstSpawnLayerHash=0x%08X\n", first_official_enemy ? first_official_enemy->spawn_layer_hash : 0u);
    std::fprintf(file, "lastDirection=%s\n", direction_name(g_last_direction));
    std::fprintf(file, "inputWinJDown=%u\n", g_input_win_j_down ? 1u : 0u);
    std::fprintf(file, "inputWinJPress=%u\n", g_input_win_j_press ? 1u : 0u);
    std::fprintf(file, "inputApiKeyJDown=%u\n", g_input_api_key_j_down ? 1u : 0u);
    std::fprintf(file, "inputApiKeyJHeld=%u\n", g_input_api_key_j_held ? 1u : 0u);
    std::fprintf(file, "inputApiActionAttackDown=%u\n", g_input_api_action_attack_down ? 1u : 0u);
    std::fprintf(file, "inputApiActionAttackHeld=%u\n", g_input_api_action_attack_held ? 1u : 0u);
    std::fprintf(file, "inputApiActionAttackPressed=%u\n", g_input_api_action_attack_pressed ? 1u : 0u);
    std::fprintf(file, "inputApiBpadLeftDown=%u\n", g_input_api_bpad_left_down ? 1u : 0u);
    std::fprintf(file, "inputApiBpadLeftHeld=%u\n", g_input_api_bpad_left_held ? 1u : 0u);
    std::fprintf(file, "inputApiBpadLeftPressed=%u\n", g_input_api_bpad_left_pressed ? 1u : 0u);
    std::fprintf(file, "inputAttackDown=%u\n", g_input_attack_down ? 1u : 0u);
    std::fprintf(file, "inputFirstActionDown=%d\n", g_input_first_action_down);
    std::fprintf(file, "inputFirstActionHeld=%d\n", g_input_first_action_held);
    std::fprintf(file, "inputFirstButtonDown=%d\n", g_input_first_button_down);
    std::fprintf(file, "inputFirstButtonHeld=%d\n", g_input_first_button_held);
    std::fclose(file);

    MoveFileExA(tmp_path, g_state_path, MOVEFILE_REPLACE_EXISTING);
}

void append_trace_row(
    int game_state,
    unsigned int room_index,
    float room_time,
    float player_x,
    float player_y,
    unsigned long long now_ms) {
    if (!g_trace_path[0]) {
        return;
    }
    const bool write_header = GetFileAttributesA(g_trace_path) == INVALID_FILE_ATTRIBUTES;
    FILE *file = nullptr;
    if (fopen_s(&file, g_trace_path, "ab") != 0 || !file) {
        return;
    }
    if (write_header) {
        std::fprintf(file, "tick\tgameState\troomIndex\troomIndexHex\troomTime\tplayerX\tplayerY\tnativeSpawnCount\tnativeDirectCount\tnativeF0029Count\tnativeCraterCount\n");
    }
    std::fprintf(
        file,
        "%llu\t%d\t%u\t0x%08X\t%.6f\t%.6f\t%.6f\t%u\t%u\t%u\t%u\n",
        now_ms,
        game_state,
        room_index,
        room_index,
        static_cast<double>(room_time),
        static_cast<double>(player_x),
        static_cast<double>(player_y),
        g_native_spawn_count,
        g_native_direct_count,
        g_native_f0029_count,
        g_native_crater_count);
    std::fclose(file);
}

void spawn_entity_probe(uint32_t entity_type, const char *reason) {
    if (!g_mina || !g_native_spawn_enabled) {
        return;
    }
    if (native_spawn_limited()) {
        return;
    }

    float player_x = 0.0f;
    float player_y = 0.0f;
    g_mina->PlayerGetPos(&player_x, &player_y);
    const unsigned int room_index = g_mina->GetRoomIndex();
    const int game_state = g_mina->GetCurrentGameState();
    const float room_time = g_mina->GetRoomTime();
    ++g_native_spawn_count;

    char message[512]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn native spawn request #%u reason=%s type=%u/%s room=%u state=%d roomTime=%.3f player=(%.3f, %.3f)\n",
        g_native_spawn_count,
        reason ? reason : "<none>",
        entity_type,
        entity_type_name(entity_type),
        room_index,
        game_state,
        static_cast<double>(room_time),
        static_cast<double>(player_x),
        static_cast<double>(player_y));
    g_mina->Log(message);

    g_mina->SetSharedValue("xmark.native.lastSpawnEntityType", static_cast<uintptr_t>(entity_type));
    g_mina->SetSharedValue("xmark.native.lastSpawnRoom", static_cast<uintptr_t>(room_index));
    g_mina->SetSharedValue("xmark.native.lastSpawnCount", static_cast<uintptr_t>(g_native_spawn_count));

    g_mina->SpawnEntity(entity_type);
    g_mina->Log("XMarkBurn native spawn request returned from MinaModAPI::SpawnEntity.\n");
}

bool spawn_direct_anim_effect(
    const char *label,
    const char *reason,
    const char *anb_path_chars,
    const char *sequence_chars,
    const char *asset_palette_chars,
    unsigned int *asset_spawn_count,
    const Vec3 *position_override,
    uintptr_t *effect_out = nullptr,
    uintptr_t *child_out = nullptr,
    int draw_layer = 1,
    float visual_scale = 1.0f) {
    if (effect_out) {
        *effect_out = 0;
    }
    if (child_out) {
        *child_out = 0;
    }
    if (!g_mina || !g_native_direct_enabled) {
        return false;
    }
    if (native_spawn_limited()) {
        if (g_mina) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn direct %s skipped: native spawn limited count=%u limit=%u.\n",
                label ? label : "effect",
                g_native_spawn_count,
                g_native_spawn_limit);
            g_mina->Log(message);
        }
        return false;
    }
    const unsigned int revision = g_mina->GetGameRevision ? g_mina->GetGameRevision() : 0u;
    if (revision != kSupportedGameRevision) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct %s skipped: unsupported game revision %u expected %u.\n",
            label ? label : "effect",
            revision,
            kSupportedGameRevision);
        g_mina->Log(message);
        return false;
    }

    const uintptr_t manager_address = entity_manager();
    if (!manager_address) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct %s skipped: entity manager pointer was null.\n",
            label ? label : "effect");
        g_mina->Log(message);
        return false;
    }

    const uintptr_t base = exe_base();
    const Vec3 position = position_override ? *position_override : official_spawn_position();
    const float clamped_visual_scale = std::max(0.05f, visual_scale);
    const Vec3 scale{clamped_visual_scale, clamped_visual_scale, clamped_visual_scale};
    auto factory = reinterpret_cast<NativeEntityFactoryFn>(base + kNativeEntityFactoryRva);
    auto setup_anim_effect = reinterpret_cast<AnimEffectSetupFn>(base + kAnimEffectSetupRva);
    void *effect = nullptr;
    const bool is_xmark_effect = label && std::strcmp(label, "XMark") == 0;
    const bool force_palette_arg =
        env_bool("MINA_XMARK_NATIVE_FORCE_PALETTE_ARG", false) ||
        (is_xmark_effect && env_bool("MINA_XMARK_NATIVE_FORCE_XMARK_PALETTE_ARG", false));

    ++g_native_spawn_count;
    unsigned int request_number = g_native_spawn_count;
    if (asset_spawn_count) {
        request_number = ++(*asset_spawn_count);
    }
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn direct %s request #%u reason=%s manager=0x%p pos=(%.3f, %.3f, %.3f) anb=%s seq=%s layer=%d scale=%.3f assetPalette=%s paletteArg=%u\n",
        label ? label : "effect",
        request_number,
        reason ? reason : "<none>",
        reinterpret_cast<void *>(manager_address),
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
        anb_path_chars ? anb_path_chars : "<null>",
        sequence_chars ? sequence_chars : "<null>",
        draw_layer,
        static_cast<double>(clamped_visual_scale),
        asset_palette_chars ? asset_palette_chars : "<embedded/default>",
        force_palette_arg ? 1u : 0u);
    g_mina->Log(message);

    __try {
        effect = factory(
            reinterpret_cast<void *>(manager_address),
            ENTITYTYPE_ANIM_EFFECT,
            &position,
            &scale,
            0.0f,
            1,
            nullptr,
            nullptr,
            nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct %s native factory raised an exception.\n",
            label ? label : "effect");
        g_mina->Log(message);
        return false;
    }

    if (!effect) {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct %s native factory returned null.\n",
            label ? label : "effect");
        g_mina->Log(message);
        return false;
    }

    const YcStringView anb_path{anb_path_chars, std::strlen(anb_path_chars)};
    const YcStringView sequence_name{sequence_chars, std::strlen(sequence_chars)};
    YcStringView palette_path{};
    const YcStringView *palette_arg = nullptr;
    if (force_palette_arg && asset_palette_chars && asset_palette_chars[0]) {
        palette_path.data = asset_palette_chars;
        palette_path.length = std::strlen(asset_palette_chars);
        palette_arg = &palette_path;
    }
    __try {
        setup_anim_effect(effect, &anb_path, &sequence_name, draw_layer, 1.0f, palette_arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct %s AnimEffect setup raised an exception.\n",
            label ? label : "effect");
        g_mina->Log(message);
        return false;
    }

    uintptr_t child_address = 0;
    safe_read_ptr(reinterpret_cast<uintptr_t>(effect) + 0x120, &child_address);
    remember_spawned_effect_pointer(reinterpret_cast<uintptr_t>(effect));
    remember_spawned_effect_pointer(child_address);
    if (position_override && env_bool("MINA_XMARK_FORCE_EFFECT_POSITION_AFTER_SETUP", true)) {
        unsigned int effect_position_offset = 0;
        unsigned int child_position_offset = 0;
        bool wrote_effect_position = false;
        bool wrote_child_position = false;
        if (find_position_pair_offset(reinterpret_cast<uintptr_t>(effect), position, &effect_position_offset)) {
            wrote_effect_position = write_position_pair(reinterpret_cast<uintptr_t>(effect), effect_position_offset, position);
        }
        if (find_position_pair_offset(child_address, position, &child_position_offset)) {
            wrote_child_position = write_position_pair(child_address, child_position_offset, position);
        }
        if (env_bool("MINA_XMARK_LOG_FORCE_EFFECT_POSITION", false)) {
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn direct %s force-position effect=%u/0x%X child=%u/0x%X\n",
                label ? label : "effect",
                wrote_effect_position ? 1u : 0u,
                effect_position_offset,
                wrote_child_position ? 1u : 0u,
                child_position_offset);
            g_mina->Log(message);
        }
    }
    if (effect_out) {
        *effect_out = reinterpret_cast<uintptr_t>(effect);
    }
    if (child_out) {
        *child_out = child_address;
    }
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn direct %s effect initialized: effect=0x%p child=0x%p\n",
        label ? label : "effect",
        effect,
        reinterpret_cast<void *>(child_address));
    g_mina->Log(message);
    return true;
}

bool spawn_direct_entity_at(
    const char *label,
    const char *reason,
    uint32_t entity_type,
    const Vec3 &position,
    uintptr_t *entity_out = nullptr) {
    if (entity_out) {
        *entity_out = 0;
    }
    if (!g_mina || !g_test_enemy_harness_enabled || !g_native_direct_enabled) {
        return false;
    }
    if (native_spawn_limited()) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct entity skipped: native spawn limited count=%u limit=%u.\n",
            g_native_spawn_count,
            g_native_spawn_limit);
        g_mina->Log(message);
        return false;
    }
    const unsigned int revision = g_mina->GetGameRevision ? g_mina->GetGameRevision() : 0u;
    if (revision != kSupportedGameRevision) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn direct entity skipped: unsupported game revision %u expected %u.\n",
            revision,
            kSupportedGameRevision);
        g_mina->Log(message);
        return false;
    }

    const uintptr_t manager_address = entity_manager();
    if (!manager_address) {
        g_mina->Log("XMarkBurn direct entity skipped: entity manager pointer was null.\n");
        return false;
    }

    const float scale_value = env_float("MINA_XMARK_TEST_ENEMY_SCALE", 1.0f);
    const Vec3 scale{scale_value, scale_value, scale_value};
    const int spawn_flag = static_cast<int>(env_uint("MINA_XMARK_TEST_ENEMY_SPAWN_FLAG", 1));
    auto factory = reinterpret_cast<NativeEntityFactoryFn>(exe_base() + kNativeEntityFactoryRva);

    ++g_native_spawn_count;
    const unsigned int request_number = ++g_test_enemy_spawn_count;
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn direct entity request #%u reason=%s label=%s type=%u/%s manager=0x%p pos=(%.3f, %.3f, %.3f) scale=%.3f spawnFlag=%d\n",
        request_number,
        reason ? reason : "<none>",
        label ? label : "<none>",
        entity_type,
        entity_type_name(entity_type),
        reinterpret_cast<void *>(manager_address),
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z),
        static_cast<double>(scale_value),
        spawn_flag);
    g_mina->Log(message);

    void *entity = nullptr;
    __try {
        entity = factory(
            reinterpret_cast<void *>(manager_address),
            entity_type,
            &position,
            &scale,
            0.0f,
            spawn_flag,
            nullptr,
            nullptr,
            nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_mina->Log("XMarkBurn direct entity native factory raised an exception.\n");
        return false;
    }

    if (!entity) {
        g_mina->Log("XMarkBurn direct entity native factory returned null.\n");
        if (env_bool("MINA_XMARK_TEST_ENEMY_API_FALLBACK", false) && g_mina->SpawnEntity) {
            g_mina->SpawnEntity(entity_type);
            g_mina->Log("XMarkBurn direct entity requested MinaModAPI::SpawnEntity fallback.\n");
        }
        return false;
    }

    g_last_test_enemy_entity = reinterpret_cast<uintptr_t>(entity);
    g_last_test_enemy_resolved_ms = GetTickCount64();
    g_last_test_enemy_position = position;
    if (entity_out) {
        *entity_out = g_last_test_enemy_entity;
    }
    g_mina->SetSharedValue("xmark.testEnemy.lastEntity", g_last_test_enemy_entity);
    g_mina->SetSharedValue("xmark.testEnemy.type", static_cast<uintptr_t>(entity_type));
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn direct entity spawned: entity=0x%p type=%u/%s\n",
        entity,
        entity_type,
        entity_type_name(entity_type));
    g_mina->Log(message);
    return true;
}

bool spawn_direct_xmark_at(const char *reason, const Vec3 &position, uintptr_t *effect_out = nullptr, uintptr_t *child_out = nullptr) {
    return spawn_direct_anim_effect(
        "XMark",
        reason,
        "effects/xmark.anb.yc",
        "2x",
        "palettes/xmark.pal.yc",
        &g_native_direct_count,
        &position,
        effect_out,
        child_out,
        static_cast<int>(env_uint("MINA_XMARK_DRAW_LAYER", 10)));
}

Vec3 xmark_attachment_mark_position(const Vec3 &target_position) {
    Vec3 position = target_position;
    position.x += env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_X", 0.0f);
    position.y += env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_Y", 0.0f);
    position.z += env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_Z", 0.0f);
    return position;
}

Vec3 xmark_attachment_target_position_from_mark(const Vec3 &mark_position) {
    Vec3 position = mark_position;
    position.x -= env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_X", 0.0f);
    position.y -= env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_Y", 0.0f);
    position.z -= env_float("MINA_XMARK_ATTACHMENT_MARK_OFFSET_Z", 0.0f);
    return position;
}

void capture_xmark_official_body_offset(
    XMarkAttachment &attachment,
    const XMarkRuntimeTarget &target) {
    if (!target.official_follow || !target.entity) {
        return;
    }
    Vec3 entity_position{};
    if (!official_entity_world_position_read(target.entity, &entity_position)) {
        return;
    }
    Vec3 offset{
        target.position.x - entity_position.x,
        target.position.y - entity_position.y,
        target.position.z - entity_position.z,
    };
    if (target.suppress_hud) {
        offset.x -= env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_X", 0.0f);
        offset.y -= env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_Y", 0.6f);
    }
    const float max_offset_x = std::max(
        0.0f,
        env_float("MINA_XMARK_OFFICIAL_BODY_CENTER_MAX_OFFSET_X", 4.0f));
    const float max_offset_y = std::max(
        0.0f,
        env_float("MINA_XMARK_OFFICIAL_BODY_CENTER_MAX_OFFSET_Y", 6.0f));
    if (!std::isfinite(offset.x) || !std::isfinite(offset.y) ||
        std::fabs(offset.x) > max_offset_x ||
        std::fabs(offset.y) > max_offset_y) {
        return;
    }
    attachment.official_body_offset = offset;
    attachment.has_official_body_offset = true;
    attachment.runtime_visual_offset = Vec3{};
    attachment.has_runtime_visual_offset = false;
}

Vec3 xmark_official_body_position(
    const XMarkAttachment &attachment,
    const Vec3 &entity_position) {
    Vec3 position = entity_position;
    if (attachment.has_official_body_offset) {
        position.x += attachment.official_body_offset.x;
        position.y += attachment.official_body_offset.y;
        position.z += attachment.official_body_offset.z;
    }
    return position;
}

constexpr unsigned int kXMarkRenderFrameSize = 16;
constexpr unsigned int kXMarkRenderMarkerFrameCount = 6;
constexpr unsigned int kXMarkRenderFireFrameCount = 5;
constexpr unsigned int kXMarkRenderF0029FrameCount = 5;
constexpr unsigned int kXMarkRenderVerticalSmearFrameCount = 5;
constexpr unsigned int kXMarkRenderFireFrameBase = kXMarkRenderMarkerFrameCount;
constexpr unsigned int kXMarkRenderBurnAuraFrame = kXMarkRenderFireFrameBase + kXMarkRenderFireFrameCount;
constexpr unsigned int kXMarkRenderBurnDamageFrame = kXMarkRenderBurnAuraFrame + 1u;
constexpr unsigned int kXMarkRenderF0029RightFrameBase = kXMarkRenderBurnDamageFrame + 1u;
constexpr unsigned int kXMarkRenderF0029LeftFrameBase = kXMarkRenderF0029RightFrameBase + kXMarkRenderF0029FrameCount;
constexpr unsigned int kXMarkRenderVerticalSmearDownFrameBase = kXMarkRenderF0029LeftFrameBase + kXMarkRenderF0029FrameCount;
constexpr unsigned int kXMarkRenderVerticalSmearUpFrameBase = kXMarkRenderVerticalSmearDownFrameBase + kXMarkRenderVerticalSmearFrameCount;
constexpr unsigned int kXMarkRenderFrameCount = kXMarkRenderVerticalSmearUpFrameBase + kXMarkRenderVerticalSmearFrameCount;
constexpr unsigned int kXMarkRenderMaxQuads = 32;
constexpr unsigned int kXMarkHudRenderPixelScale = 3;
constexpr unsigned int kXMarkHudRenderFrameSize = 12 * kXMarkHudRenderPixelScale;
constexpr unsigned int kXMarkHudRenderFrameCount = 6;
constexpr unsigned int kXMarkHudRenderMaxQuads = 5;
constexpr unsigned int kAttackOverlayRenderFrameWidth = 32;
constexpr unsigned int kAttackOverlayRenderFrameHeight = 40;
constexpr unsigned int kAttackOverlayRenderFrameCount = 5;
constexpr unsigned int kAttackOverlayRenderMaxQuads = 1;

MM_Color xmark_alpha_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    MM_Color color{};
    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;
    return color;
}

MM_Color xmark_render_color_from_index(unsigned char value) {
    if (!value) {
        return xmark_alpha_color(0, 0, 0, 0);
    }
    const unsigned char alpha = static_cast<unsigned char>(
        std::min<unsigned int>(255u, env_uint("MINA_XMARK_RENDER_ALPHA", 235u)));
    if (value == 1u) {
        return xmark_alpha_color(0, 0, 0, alpha);
    }
    if (value == 58u) {
        return xmark_alpha_color(209, 45, 42, alpha);
    }
    if (value == 59u) {
        return xmark_alpha_color(255, 233, 197, alpha);
    }
    switch (value % 3u) {
    case 0:
        return xmark_alpha_color(0, 0, 0, alpha);
    case 1:
        return xmark_alpha_color(209, 45, 42, alpha);
    default:
        return xmark_alpha_color(255, 233, 197, alpha);
    }
}

MM_Color xmark_render_flame_color_from_index(unsigned char value) {
    if (!value) {
        return xmark_alpha_color(0, 0, 0, 0);
    }
    const unsigned char alpha = static_cast<unsigned char>(
        std::min<unsigned int>(255u, env_uint("MINA_XMARK_BURN_FIRE_ALPHA", env_uint("MINA_XMARK_RENDER_ALPHA", 235u))));
    if (value == 1u) {
        return xmark_alpha_color(112, 0, 12, alpha);
    }
    if (value == 2u) {
        return xmark_alpha_color(226, 22, 32, alpha);
    }
    if (value == 3u) {
        return xmark_alpha_color(255, 106, 92, alpha);
    }
    return xmark_render_color_from_index(value);
}

void xmark_render_copy_asset_frame(
    MM_Color *pixels,
    unsigned int atlas_width,
    unsigned int frame,
    unsigned int width,
    unsigned int height,
    const unsigned char *asset_pixels,
    bool flame_palette = false) {
    if (!pixels || !asset_pixels || frame >= kXMarkRenderFrameCount) {
        return;
    }
    const unsigned int copy_width = std::min(width, kXMarkRenderFrameSize);
    const unsigned int copy_height = std::min(height, kXMarkRenderFrameSize);
    const unsigned int pad_x = (kXMarkRenderFrameSize - copy_width) / 2u;
    const unsigned int pad_y = (kXMarkRenderFrameSize - copy_height) / 2u;
    for (unsigned int y = 0; y < copy_height; ++y) {
        for (unsigned int x = 0; x < copy_width; ++x) {
            const unsigned char value = asset_pixels[y * width + x];
            pixels[(pad_y + y) * atlas_width + frame * kXMarkRenderFrameSize + pad_x + x] =
                flame_palette ? xmark_render_flame_color_from_index(value) : xmark_render_color_from_index(value);
        }
    }
}

void xmark_render_put_pixel(MM_Color *pixels, unsigned int atlas_width, unsigned int frame, int x, int y, MM_Color color) {
    if (!pixels || x < 0 || y < 0 || x >= static_cast<int>(kXMarkRenderFrameSize) || y >= static_cast<int>(kXMarkRenderFrameSize)) {
        return;
    }
    pixels[y * atlas_width + frame * kXMarkRenderFrameSize + static_cast<unsigned int>(x)] = color;
}

void xmark_render_draw_generated_frame(MM_Color *pixels, unsigned int atlas_width, unsigned int frame, float extent, bool faint) {
    const MM_Color black = xmark_alpha_color(13, 8, 8, faint ? 140 : 235);
    const MM_Color red = xmark_alpha_color(216, 32, 38, faint ? 120 : 235);
    const MM_Color cream = xmark_alpha_color(255, 232, 174, faint ? 100 : 220);
    const float center = 7.5f;
    const float thickness = frame == 0 ? 0.9f : 1.25f;
    for (int y = 0; y < static_cast<int>(kXMarkRenderFrameSize); ++y) {
        for (int x = 0; x < static_cast<int>(kXMarkRenderFrameSize); ++x) {
            const float fx = static_cast<float>(x);
            const float fy = static_cast<float>(y);
            const float dx = std::fabs(fx - center);
            const float dy = std::fabs(fy - center);
            if (frame == 0) {
                const float d2 = dx * dx + dy * dy;
                if (d2 <= 7.0f) {
                    xmark_render_put_pixel(pixels, atlas_width, frame, x, y, d2 >= 4.6f ? black : (d2 <= 1.4f ? cream : red));
                }
                continue;
            }

            if (dx > extent || dy > extent) {
                continue;
            }
            const float diag_a = std::fabs(fy - fx);
            const float diag_b = std::fabs(fy - (15.0f - fx));
            const float diag = diag_a < diag_b ? diag_a : diag_b;
            if (diag <= thickness + 1.05f) {
                MM_Color color = black;
                if (diag <= thickness) {
                    color = red;
                }
                if (diag <= 0.35f && ((x + y + static_cast<int>(frame)) % 4 == 0)) {
                    color = cream;
                }
                xmark_render_put_pixel(pixels, atlas_width, frame, x, y, color);
            }
        }
    }
}

void xmark_render_draw_burn_shade_frame(MM_Color *pixels, unsigned int atlas_width, unsigned int frame, bool bright) {
    const float center = 7.5f;
    const MM_Color inner = bright
        ? xmark_alpha_color(255, 255, 255, 235)
        : xmark_alpha_color(255, 255, 255, 170);
    const MM_Color mid = bright
        ? xmark_alpha_color(255, 255, 255, 195)
        : xmark_alpha_color(255, 255, 255, 125);
    const MM_Color edge = bright
        ? xmark_alpha_color(255, 255, 255, 145)
        : xmark_alpha_color(255, 255, 255, 82);
    for (int y = 0; y < static_cast<int>(kXMarkRenderFrameSize); ++y) {
        for (int x = 0; x < static_cast<int>(kXMarkRenderFrameSize); ++x) {
            const float dx = std::fabs((static_cast<float>(x) - center) / center);
            const float dy = std::fabs((static_cast<float>(y) - center) / center);
            const float radial_dist = std::sqrt(dx * dx + dy * dy);
            const float edge_dist = std::max(dx, dy);
            if (radial_dist > 1.18f) {
                continue;
            }

            // Dither the palette tint.
            const int pattern = (x * 3 + y * 5 + (bright ? 1 : 0)) & 7;
            if ((!bright && pattern == 0) || (edge_dist > 0.74f && (pattern == 1 || pattern == 5))) {
                continue;
            }

            MM_Color color = edge_dist < 0.42f ? inner : (edge_dist < 0.80f ? mid : edge);
            if (radial_dist > 0.98f) {
                color.a = static_cast<uint8_t>(std::min<unsigned int>(color.a, bright ? 100u : 62u));
            }
            xmark_render_put_pixel(pixels, atlas_width, frame, x, y, color);
        }
    }
}

void xmark_render_make_texture_pixels(MM_Color *pixels) {
    const unsigned int atlas_width = kXMarkRenderFrameSize * kXMarkRenderFrameCount;
    const unsigned int atlas_height = kXMarkRenderFrameSize;
    std::memset(pixels, 0, sizeof(MM_Color) * atlas_width * atlas_height);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 0, xmark_assets::MarkerApply0Width, xmark_assets::MarkerApply0Height, xmark_assets::MarkerApply0Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 1, xmark_assets::MarkerApply1Width, xmark_assets::MarkerApply1Height, xmark_assets::MarkerApply1Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 2, xmark_assets::MarkerApply2Width, xmark_assets::MarkerApply2Height, xmark_assets::MarkerApply2Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 3, xmark_assets::MarkerApply3Width, xmark_assets::MarkerApply3Height, xmark_assets::MarkerApply3Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 4, xmark_assets::MarkerSourceWidth, xmark_assets::MarkerSourceHeight, xmark_assets::MarkerSourcePixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, 5, xmark_assets::MarkerFaintWidth, xmark_assets::MarkerFaintHeight, xmark_assets::MarkerFaintPixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderFireFrameBase + 0u, xmark_assets::Fire0Width, xmark_assets::Fire0Height, xmark_assets::Fire0Pixels, true);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderFireFrameBase + 1u, xmark_assets::Fire1Width, xmark_assets::Fire1Height, xmark_assets::Fire1Pixels, true);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderFireFrameBase + 2u, xmark_assets::Fire2Width, xmark_assets::Fire2Height, xmark_assets::Fire2Pixels, true);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderFireFrameBase + 3u, xmark_assets::Fire3Width, xmark_assets::Fire3Height, xmark_assets::Fire3Pixels, true);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderFireFrameBase + 4u, xmark_assets::Fire4Width, xmark_assets::Fire4Height, xmark_assets::Fire4Pixels, true);
    xmark_render_draw_burn_shade_frame(pixels, atlas_width, kXMarkRenderBurnAuraFrame, false);
    xmark_render_draw_burn_shade_frame(pixels, atlas_width, kXMarkRenderBurnDamageFrame, true);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029RightFrameBase + 0u, xmark_assets::F0029Right0Width, xmark_assets::F0029Right0Height, xmark_assets::F0029Right0Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029RightFrameBase + 1u, xmark_assets::F0029Right1Width, xmark_assets::F0029Right1Height, xmark_assets::F0029Right1Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029RightFrameBase + 2u, xmark_assets::F0029Right2Width, xmark_assets::F0029Right2Height, xmark_assets::F0029Right2Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029RightFrameBase + 3u, xmark_assets::F0029Right3Width, xmark_assets::F0029Right3Height, xmark_assets::F0029Right3Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029RightFrameBase + 4u, xmark_assets::F0029Right4Width, xmark_assets::F0029Right4Height, xmark_assets::F0029Right4Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029LeftFrameBase + 0u, xmark_assets::F0029Left0Width, xmark_assets::F0029Left0Height, xmark_assets::F0029Left0Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029LeftFrameBase + 1u, xmark_assets::F0029Left1Width, xmark_assets::F0029Left1Height, xmark_assets::F0029Left1Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029LeftFrameBase + 2u, xmark_assets::F0029Left2Width, xmark_assets::F0029Left2Height, xmark_assets::F0029Left2Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029LeftFrameBase + 3u, xmark_assets::F0029Left3Width, xmark_assets::F0029Left3Height, xmark_assets::F0029Left3Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderF0029LeftFrameBase + 4u, xmark_assets::F0029Left4Width, xmark_assets::F0029Left4Height, xmark_assets::F0029Left4Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearDownFrameBase + 0u, xmark_assets::VerticalSmearDown0Width, xmark_assets::VerticalSmearDown0Height, xmark_assets::VerticalSmearDown0Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearDownFrameBase + 1u, xmark_assets::VerticalSmearDown1Width, xmark_assets::VerticalSmearDown1Height, xmark_assets::VerticalSmearDown1Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearDownFrameBase + 2u, xmark_assets::VerticalSmearDown2Width, xmark_assets::VerticalSmearDown2Height, xmark_assets::VerticalSmearDown2Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearDownFrameBase + 3u, xmark_assets::VerticalSmearDown3Width, xmark_assets::VerticalSmearDown3Height, xmark_assets::VerticalSmearDown3Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearDownFrameBase + 4u, xmark_assets::VerticalSmearDown4Width, xmark_assets::VerticalSmearDown4Height, xmark_assets::VerticalSmearDown4Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearUpFrameBase + 0u, xmark_assets::VerticalSmearUp0Width, xmark_assets::VerticalSmearUp0Height, xmark_assets::VerticalSmearUp0Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearUpFrameBase + 1u, xmark_assets::VerticalSmearUp1Width, xmark_assets::VerticalSmearUp1Height, xmark_assets::VerticalSmearUp1Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearUpFrameBase + 2u, xmark_assets::VerticalSmearUp2Width, xmark_assets::VerticalSmearUp2Height, xmark_assets::VerticalSmearUp2Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearUpFrameBase + 3u, xmark_assets::VerticalSmearUp3Width, xmark_assets::VerticalSmearUp3Height, xmark_assets::VerticalSmearUp3Pixels);
    xmark_render_copy_asset_frame(
        pixels, atlas_width, kXMarkRenderVerticalSmearUpFrameBase + 4u, xmark_assets::VerticalSmearUp4Width, xmark_assets::VerticalSmearUp4Height, xmark_assets::VerticalSmearUp4Pixels);
}

unsigned int xmark_render_frame_for_attachment(const XMarkAttachment &attachment, unsigned long long now_ms) {
    const unsigned int apply_frame_ms = std::max(20u, env_uint("MINA_XMARK_RENDER_APPLY_FRAME_MS", 80));
    const unsigned int blink_out_ms = env_uint("MINA_XMARK_MARK_BLINK_OUT_MS", 1000);
    const unsigned int blink_ms = std::max(
        40u,
        env_uint("MINA_XMARK_MARK_BLINK_MS", env_uint("MINA_XMARK_HUD_BLINK_MS", 240)));
    const unsigned long long elapsed_ms = attachment.started_ms && now_ms >= attachment.started_ms ? now_ms - attachment.started_ms : 0ull;
    if (elapsed_ms < static_cast<unsigned long long>(apply_frame_ms) * 4ull) {
        return static_cast<unsigned int>(elapsed_ms / apply_frame_ms);
    }
    const unsigned long long remaining_ms = attachment.expires_ms > now_ms ? attachment.expires_ms - now_ms : 0ull;
    if (remaining_ms <= blink_out_ms) {
        const unsigned long long blink_start_ms =
            attachment.expires_ms > blink_out_ms ? attachment.expires_ms - blink_out_ms : attachment.started_ms;
        const unsigned long long blink_elapsed_ms =
            now_ms >= blink_start_ms ? now_ms - blink_start_ms : 0ull;
        return ((blink_elapsed_ms / blink_ms) & 1ull) ? 5u : 4u;
    }
    return 4u;
}

bool xmark_marker_debug_draw_enabled() {
    return env_bool("MINA_XMARK_MARK_DEBUG_DRAW_ENABLED", true);
}

void xmark_marker_debug_copy_asset_frame(
    MM_Color *pixels,
    unsigned int width,
    unsigned int height,
    const unsigned char *asset_pixels) {
    if (!pixels || !asset_pixels) {
        return;
    }
    const unsigned int copy_width = std::min(width, kXMarkRenderFrameSize);
    const unsigned int copy_height = std::min(height, kXMarkRenderFrameSize);
    const unsigned int pad_x = (kXMarkRenderFrameSize - copy_width) / 2u;
    const unsigned int pad_y = (kXMarkRenderFrameSize - copy_height) / 2u;
    for (unsigned int y = 0; y < copy_height; ++y) {
        for (unsigned int x = 0; x < copy_width; ++x) {
            pixels[(pad_y + y) * kXMarkRenderFrameSize + pad_x + x] =
                xmark_render_color_from_index(asset_pixels[y * width + x]);
        }
    }
}

bool xmark_marker_debug_make_frame_pixels(unsigned int frame, MM_Color *pixels) {
    if (!pixels || frame >= kXMarkRenderMarkerFrameCount) {
        return false;
    }
    std::memset(pixels, 0, sizeof(MM_Color) * kXMarkRenderFrameSize * kXMarkRenderFrameSize);
    switch (frame) {
    case 0:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerApply0Width, xmark_assets::MarkerApply0Height, xmark_assets::MarkerApply0Pixels);
        return true;
    case 1:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerApply1Width, xmark_assets::MarkerApply1Height, xmark_assets::MarkerApply1Pixels);
        return true;
    case 2:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerApply2Width, xmark_assets::MarkerApply2Height, xmark_assets::MarkerApply2Pixels);
        return true;
    case 3:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerApply3Width, xmark_assets::MarkerApply3Height, xmark_assets::MarkerApply3Pixels);
        return true;
    case 4:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerSourceWidth, xmark_assets::MarkerSourceHeight, xmark_assets::MarkerSourcePixels);
        return true;
    case 5:
        xmark_marker_debug_copy_asset_frame(
            pixels, xmark_assets::MarkerFaintWidth, xmark_assets::MarkerFaintHeight, xmark_assets::MarkerFaintPixels);
        return true;
    default:
        return false;
    }
}

ycDrawUtil *xmark_marker_debug_draw_util() {
    if (!g_mina) {
        return nullptr;
    }
    char layer_name[64] = "World";
    if (!xmark_read_environment_value(
            "MINA_XMARK_MARK_DEBUG_DRAW_LAYER",
            layer_name,
            sizeof(layer_name))) {
        std::snprintf(layer_name, sizeof(layer_name), "World");
    }
    ycDrawUtil *draw_util = g_mina->GetDebugDraw(layer_name);
    const char *fallback_layers[] = {
        "World",
        "WorldHD",
        "WorldBlend",
        "WorldPersist",
    };
    for (const char *fallback : fallback_layers) {
        if (draw_util || std::strcmp(layer_name, fallback) == 0) {
            continue;
        }
        draw_util = g_mina->GetDebugDraw(fallback);
    }
    return draw_util;
}

ycDrawUtil *xmark_hud_debug_draw_util() {
    if (!g_mina) {
        return nullptr;
    }
    char layer_name[64] = "GameHud";
    if (!xmark_read_environment_value(
            "MINA_XMARK_HUD_DEBUG_DRAW_LAYER",
            layer_name,
            sizeof(layer_name))) {
        std::snprintf(layer_name, sizeof(layer_name), "GameHud");
    }
    ycDrawUtil *draw_util = g_mina->GetDebugDraw(layer_name);
    if (!draw_util && std::strcmp(layer_name, "Hud") != 0) {
        draw_util = g_mina->GetDebugDraw("Hud");
    }
    if (!draw_util && std::strcmp(layer_name, "EngineHUD") != 0) {
        draw_util = g_mina->GetDebugDraw("EngineHUD");
    }
    return draw_util;
}

bool xmark_marker_debug_draw_ensure_initialized() {
    if (!xmark_marker_debug_draw_enabled()) {
        std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "disabled");
        return false;
    }
    if (g_xmark_marker_debug_draw_ready) {
        return true;
    }
    g_xmark_marker_debug_draw_available = xmark_marker_debug_draw_api_available();
    if (!g_xmark_marker_debug_draw_available) {
        std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "api-unavailable");
        if (g_mina && !g_xmark_marker_debug_draw_logged_unavailable) {
            g_mina->Log("XMarkBurn marker debug textured draw dormant: API functions unavailable.\n");
            g_xmark_marker_debug_draw_logged_unavailable = true;
        }
        return false;
    }
    if (!xmark_marker_debug_draw_util()) {
        std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "draw-layer-missing");
        return false;
    }

    if (g_xmark_marker_debug_draw_init_frame >= kXMarkRenderMarkerFrameCount) {
        g_xmark_marker_debug_draw_ready = true;
        std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "ready");
        if (g_mina && !g_xmark_marker_debug_draw_logged_ready) {
            g_mina->Log("XMarkBurn marker debug textured draw ready.\n");
            g_xmark_marker_debug_draw_logged_ready = true;
        }
        return true;
    }

    MM_Color pixels[kXMarkRenderFrameSize * kXMarkRenderFrameSize]{};
    bool ok = false;
    __try {
        const unsigned int frame = g_xmark_marker_debug_draw_init_frame;
        if (!g_xmark_marker_debug_textures[frame]) {
            g_xmark_marker_debug_textures[frame] =
                    g_mina->CreateTexture(kXMarkRenderFrameSize, kXMarkRenderFrameSize);
        }
        ok = g_xmark_marker_debug_textures[frame] &&
            xmark_marker_debug_make_frame_pixels(frame, pixels);
        if (ok) {
            g_mina->UpdateTexture(g_xmark_marker_debug_textures[frame], pixels);
            ++g_xmark_marker_debug_draw_init_frame;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }

    if (!ok) {
        std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "texture-create-failed");
        return false;
    }

    std::snprintf(
        g_xmark_marker_debug_draw_status,
        sizeof(g_xmark_marker_debug_draw_status),
        "warming-%u-of-%u",
        g_xmark_marker_debug_draw_init_frame,
        kXMarkRenderMarkerFrameCount);
    return false;
}

void xmark_hud_debug_make_pixels(
    MM_Color *pixels,
    unsigned int width,
    unsigned int height,
    const unsigned char *asset_pixels) {
    if (!pixels || !asset_pixels) {
        return;
    }
    for (unsigned int i = 0; i < width * height; ++i) {
        pixels[i] = xmark_render_color_from_index(asset_pixels[i]);
    }
}

bool xmark_hud_debug_draw_ensure_initialized() {
    if (!env_bool("MINA_XMARK_HUD_DEBUG_DRAW_ENABLED", true)) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "disabled");
        return false;
    }
    if (g_xmark_hud_debug_draw_ready) {
        return true;
    }
    if (!xmark_marker_debug_draw_api_available()) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "api-unavailable");
        return false;
    }
    if (!xmark_hud_debug_draw_util()) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "draw-layer-missing");
        return false;
    }

    MM_Color hud_pixels[xmark_assets::HudWidth * xmark_assets::HudHeight]{};
    MM_Color fire_pixels[xmark_assets::Fire1Width * xmark_assets::Fire1Height]{};
    xmark_hud_debug_make_pixels(hud_pixels, xmark_assets::HudWidth, xmark_assets::HudHeight, xmark_assets::HudPixels);
    xmark_hud_debug_make_pixels(fire_pixels, xmark_assets::Fire1Width, xmark_assets::Fire1Height, xmark_assets::Fire1Pixels);

    bool ok = true;
    __try {
        if (!g_xmark_hud_debug_texture) {
            g_xmark_hud_debug_texture = g_mina->CreateTexture(xmark_assets::HudWidth, xmark_assets::HudHeight);
        }
        if (!g_xmark_hud_fire_debug_texture) {
            g_xmark_hud_fire_debug_texture = g_mina->CreateTexture(xmark_assets::Fire1Width, xmark_assets::Fire1Height);
        }
        ok = g_xmark_hud_debug_texture && g_xmark_hud_fire_debug_texture;
        if (ok) {
            g_mina->UpdateTexture(g_xmark_hud_debug_texture, hud_pixels);
            g_mina->UpdateTexture(g_xmark_hud_fire_debug_texture, fire_pixels);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "texture-create-failed");
        return false;
    }

    g_xmark_hud_debug_draw_ready = true;
    std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "ready");
    if (g_mina && !g_xmark_hud_debug_draw_logged_ready) {
        g_mina->Log("XMarkBurn HUD debug textured draw ready.\n");
        g_xmark_hud_debug_draw_logged_ready = true;
    }
    return true;
}

bool xmark_marker_debug_draw_handles_markers() {
    return xmark_marker_debug_draw_enabled() && g_xmark_marker_debug_draw_ready;
}

void xmark_marker_debug_draw_attachments(unsigned long long now_ms) {
    if (xmark_runtime_overlays_hidden_for_pause() || xmark_text_display_active()) {
        return;
    }
    if (!xmark_marker_debug_draw_enabled() ||
        !xmark_marker_debug_draw_ensure_initialized() ||
        !g_mina) {
        return;
    }
    ycDrawUtil *draw_util = xmark_marker_debug_draw_util();
    if (!draw_util) {
        return;
    }
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active ||
            !attachment.render_backend ||
            !attachment.marker_debug_draw ||
            !attachment.has_last_position) {
            continue;
        }
        const unsigned int frame = xmark_render_frame_for_attachment(attachment, now_ms);
        if (frame >= kXMarkRenderMarkerFrameCount || !g_xmark_marker_debug_textures[frame]) {
            continue;
        }
        const float half_w = attachment.render_half_w > 0.0f ? attachment.render_half_w : xmark_default_render_half_w();
        const float half_h = attachment.render_half_h > 0.0f ? attachment.render_half_h : xmark_default_render_half_h();
        MM_Vec3 center{attachment.last_position.x, attachment.last_position.y, attachment.last_position.z + env_float("MINA_XMARK_RENDER_Z_OFFSET", 1.0f)};
        __try {
            g_mina->DebugDrawTexturedQuad(
                draw_util,
                g_xmark_marker_debug_textures[frame],
                center,
                half_w * 2.0f,
                half_h * 2.0f,
                white,
                true);
            ++g_xmark_marker_debug_draw_calls;
            g_xmark_marker_debug_last_frame = frame;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            std::snprintf(g_xmark_marker_debug_draw_status, sizeof(g_xmark_marker_debug_draw_status), "draw-fault");
            g_xmark_marker_debug_draw_ready = false;
            return;
        }
    }
}

void xmark_hud_debug_draw(unsigned long long now_ms) {
    if (xmark_text_display_active() ||
        !env_bool("MINA_XMARK_HUD_DEBUG_DRAW_ENABLED", true) ||
        !xmark_hud_debug_draw_ensure_initialized() ||
        !g_mina) {
        return;
    }
    const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
    if (!hud_state.active_count || !hud_state.visible) {
        return;
    }
    ycTexture *texture = hud_state.mode == kXMarkHudModeBurn
        ? g_xmark_hud_fire_debug_texture
        : g_xmark_hud_debug_texture;
    if (!texture) {
        return;
    }
    const float x = env_float("MINA_XMARK_HUD_DEBUG_DRAW_X", 1.78f);
    const float y = env_float("MINA_XMARK_HUD_DEBUG_DRAW_Y", -7.10f);
    const float z = env_float("MINA_XMARK_HUD_DEBUG_DRAW_Z", 0.0f);
    const float width = env_float("MINA_XMARK_HUD_DEBUG_DRAW_W", 0.6f);
    const float height = env_float("MINA_XMARK_HUD_DEBUG_DRAW_H", 0.6f);
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    const bool calibration_rect = env_bool("MINA_XMARK_HUD_DEBUG_DRAW_CALIBRATION_RECT", false);
    const MM_Color calibration_color = xmark_alpha_color(255, 0, 0, 192);

    ycDrawUtil *draw_util = xmark_hud_debug_draw_util();
    if (!draw_util) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "draw-layer-missing");
        return;
    }
    __try {
        if (calibration_rect && api_function_field_ready(offsetof(MinaModAPI, DebugDrawRectSolid))) {
            g_mina->DebugDrawRectSolid(
                draw_util,
                MM_Vec3{x, y, z - 0.01f},
                width,
                height,
                calibration_color,
                true);
        }
        g_mina->DebugDrawTexturedQuad(
            draw_util,
            texture,
            MM_Vec3{x, y, z},
            width,
            height,
            white,
            true);
        ++g_xmark_hud_debug_draw_calls;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(g_xmark_hud_debug_draw_status, sizeof(g_xmark_hud_debug_draw_status), "draw-fault");
        g_xmark_hud_debug_draw_ready = false;
    }
}

bool xmark_burn_debug_draw_enabled() {
    return env_bool("MINA_XMARK_BURN_DEBUG_DRAW_ENABLED", true);
}

void xmark_burn_debug_copy_asset_frame(
    MM_Color *pixels,
    unsigned int width,
    unsigned int height,
    const unsigned char *asset_pixels,
    bool flip_y) {
    if (!pixels || !asset_pixels) {
        return;
    }
    std::memset(pixels, 0, sizeof(MM_Color) * kXMarkRenderFrameSize * kXMarkRenderFrameSize);
    const unsigned int copy_width = std::min(width, kXMarkRenderFrameSize);
    const unsigned int copy_height = std::min(height, kXMarkRenderFrameSize);
    const unsigned int pad_x = (kXMarkRenderFrameSize - copy_width) / 2u;
    const unsigned int pad_y = (kXMarkRenderFrameSize - copy_height) / 2u;
    for (unsigned int y = 0; y < copy_height; ++y) {
        const unsigned int src_y = flip_y ? (copy_height - 1u - y) : y;
        for (unsigned int x = 0; x < copy_width; ++x) {
            pixels[(pad_y + y) * kXMarkRenderFrameSize + pad_x + x] =
                xmark_render_flame_color_from_index(asset_pixels[src_y * width + x]);
        }
    }
}

bool xmark_burn_debug_make_fire_pixels(unsigned int frame, MM_Color *pixels) {
    if (!pixels || frame >= kXMarkRenderFireFrameCount) {
        return false;
    }
    const bool flip_y = env_bool("MINA_XMARK_BURN_FIRE_FLIP_Y", true);
    switch (frame) {
    case 0:
        xmark_burn_debug_copy_asset_frame(
            pixels, xmark_assets::Fire0Width, xmark_assets::Fire0Height, xmark_assets::Fire0Pixels, flip_y);
        return true;
    case 1:
        xmark_burn_debug_copy_asset_frame(
            pixels, xmark_assets::Fire1Width, xmark_assets::Fire1Height, xmark_assets::Fire1Pixels, flip_y);
        return true;
    case 2:
        xmark_burn_debug_copy_asset_frame(
            pixels, xmark_assets::Fire2Width, xmark_assets::Fire2Height, xmark_assets::Fire2Pixels, flip_y);
        return true;
    case 3:
        xmark_burn_debug_copy_asset_frame(
            pixels, xmark_assets::Fire3Width, xmark_assets::Fire3Height, xmark_assets::Fire3Pixels, flip_y);
        return true;
    case 4:
        xmark_burn_debug_copy_asset_frame(
            pixels, xmark_assets::Fire4Width, xmark_assets::Fire4Height, xmark_assets::Fire4Pixels, flip_y);
        return true;
    default:
        return false;
    }
}

bool xmark_burn_debug_make_shade_pixels(unsigned int frame, MM_Color *pixels) {
    if (!pixels || frame >= 2u) {
        return false;
    }
    std::memset(pixels, 0, sizeof(MM_Color) * kXMarkRenderFrameSize * kXMarkRenderFrameSize);
    xmark_render_draw_burn_shade_frame(pixels, kXMarkRenderFrameSize, 0, frame == 1u);
    return true;
}

bool xmark_burn_debug_draw_ensure_initialized() {
    if (!xmark_burn_debug_draw_enabled()) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "disabled");
        return false;
    }
    if (g_xmark_burn_debug_draw_ready) {
        return true;
    }
    if (!xmark_marker_debug_draw_api_available()) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "api-unavailable");
        return false;
    }
    if (!xmark_marker_debug_draw_util()) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "draw-layer-missing");
        return false;
    }

    constexpr unsigned int kBurnTextureCount = kXMarkRenderFireFrameCount + 2u;
    if (g_xmark_burn_debug_draw_init_frame >= kBurnTextureCount) {
        g_xmark_burn_debug_draw_ready = true;
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "ready");
        if (g_mina && !g_xmark_burn_debug_draw_logged_ready) {
            g_mina->Log("XMarkBurn burn debug textured draw ready.\n");
            g_xmark_burn_debug_draw_logged_ready = true;
        }
        return true;
    }

    MM_Color pixels[kXMarkRenderFrameSize * kXMarkRenderFrameSize]{};
    bool ok = false;
    __try {
        const unsigned int init_frame = g_xmark_burn_debug_draw_init_frame;
        if (init_frame < kXMarkRenderFireFrameCount) {
            const unsigned int frame = init_frame;
            if (!g_xmark_burn_debug_fire_textures[frame]) {
                g_xmark_burn_debug_fire_textures[frame] =
                    g_mina->CreateTexture(kXMarkRenderFrameSize, kXMarkRenderFrameSize);
            }
            ok = g_xmark_burn_debug_fire_textures[frame] &&
                xmark_burn_debug_make_fire_pixels(frame, pixels);
            if (ok) {
                g_mina->UpdateTexture(g_xmark_burn_debug_fire_textures[frame], pixels);
            }
        } else {
            const unsigned int frame = init_frame - kXMarkRenderFireFrameCount;
            if (!g_xmark_burn_debug_shade_textures[frame]) {
                g_xmark_burn_debug_shade_textures[frame] =
                    g_mina->CreateTexture(kXMarkRenderFrameSize, kXMarkRenderFrameSize);
            }
            ok = g_xmark_burn_debug_shade_textures[frame] &&
                xmark_burn_debug_make_shade_pixels(frame, pixels);
            if (ok) {
                g_mina->UpdateTexture(g_xmark_burn_debug_shade_textures[frame], pixels);
            }
        }
        if (ok) {
            ++g_xmark_burn_debug_draw_init_frame;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "texture-create-failed");
        return false;
    }

    std::snprintf(
        g_xmark_burn_debug_draw_status,
        sizeof(g_xmark_burn_debug_draw_status),
        "warming-%u-of-%u",
        g_xmark_burn_debug_draw_init_frame,
        kBurnTextureCount);
    return false;
}

MM_Color clashrend_boom_color_from_index(unsigned char value) {
    switch (value) {
    case 1:
        return xmark_alpha_color(0, 0, 0, 255);
    case 58:
        return xmark_alpha_color(209, 45, 42, 255);
    case 59:
        return xmark_alpha_color(255, 233, 197, 255);
    default:
        return xmark_alpha_color(0, 0, 0, 0);
    }
}

MM_Color clashrend_boom_star_color_from_index(unsigned char value) {
    switch (value) {
    case 1:
        return xmark_alpha_color(248, 8, 40, 255);
    case 2:
        return xmark_alpha_color(255, 233, 197, 255);
    case 3:
    case 7:
        return xmark_alpha_color(248, 176, 48, 255);
    case 4:
        return xmark_alpha_color(255, 255, 255, 255);
    default:
        return xmark_alpha_color(0, 0, 0, 0);
    }
}

bool clashrend_boom_star_frame_asset(
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    if (!width || !height || !pixels) {
        return false;
    }
    switch (frame) {
    case 0:
        *width = xmark_assets::BoomStar0Width;
        *height = xmark_assets::BoomStar0Height;
        *pixels = xmark_assets::BoomStar0Pixels;
        return true;
    case 1:
        *width = xmark_assets::BoomStar1Width;
        *height = xmark_assets::BoomStar1Height;
        *pixels = xmark_assets::BoomStar1Pixels;
        return true;
    case 2:
        *width = xmark_assets::BoomStar2Width;
        *height = xmark_assets::BoomStar2Height;
        *pixels = xmark_assets::BoomStar2Pixels;
        return true;
    case 3:
        *width = xmark_assets::BoomStar3Width;
        *height = xmark_assets::BoomStar3Height;
        *pixels = xmark_assets::BoomStar3Pixels;
        return true;
    case 4:
        *width = xmark_assets::BoomStar4Width;
        *height = xmark_assets::BoomStar4Height;
        *pixels = xmark_assets::BoomStar4Pixels;
        return true;
    default:
        return false;
    }
}

bool clashrend_boom_ground_frame_asset(
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    if (!width || !height || !pixels) {
        return false;
    }
    switch (frame) {
    case 0:
        *width = xmark_assets::BoomGround0Width;
        *height = xmark_assets::BoomGround0Height;
        *pixels = xmark_assets::BoomGround0Pixels;
        return true;
    case 1:
        *width = xmark_assets::BoomGround1Width;
        *height = xmark_assets::BoomGround1Height;
        *pixels = xmark_assets::BoomGround1Pixels;
        return true;
    default:
        return false;
    }
}

bool clashrend_boom_native_dome_frame_asset(
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    if (!width || !height || !pixels) {
        return false;
    }
#define CLASHREND_BOOM_NATIVE_DOME_CASE(index) \
    case index: \
        *width = xmark_assets::BoomNativeDome##index##Width; \
        *height = xmark_assets::BoomNativeDome##index##Height; \
        *pixels = xmark_assets::BoomNativeDome##index##Pixels; \
        return true
    switch (frame) {
    CLASHREND_BOOM_NATIVE_DOME_CASE(0);
    CLASHREND_BOOM_NATIVE_DOME_CASE(1);
    CLASHREND_BOOM_NATIVE_DOME_CASE(2);
    CLASHREND_BOOM_NATIVE_DOME_CASE(3);
    CLASHREND_BOOM_NATIVE_DOME_CASE(4);
    default:
        return false;
    }
#undef CLASHREND_BOOM_NATIVE_DOME_CASE
}

bool clashrend_boom_wave_frame_asset(
    bool plus_shape,
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    if (!width || !height || !pixels) {
        return false;
    }
#define CLASHREND_BOOM_WAVE_CASE(index) \
    case index: \
        if (plus_shape) { \
            *width = xmark_assets::BoomWavePlus##index##Width; \
            *height = xmark_assets::BoomWavePlus##index##Height; \
            *pixels = xmark_assets::BoomWavePlus##index##Pixels; \
        } else { \
            *width = xmark_assets::BoomWaveX##index##Width; \
            *height = xmark_assets::BoomWaveX##index##Height; \
            *pixels = xmark_assets::BoomWaveX##index##Pixels; \
        } \
        return true
    switch (frame) {
    CLASHREND_BOOM_WAVE_CASE(0);
    CLASHREND_BOOM_WAVE_CASE(1);
    CLASHREND_BOOM_WAVE_CASE(2);
    CLASHREND_BOOM_WAVE_CASE(3);
    CLASHREND_BOOM_WAVE_CASE(4);
    default:
        return false;
    }
#undef CLASHREND_BOOM_WAVE_CASE
}

bool clashrend_boom_wave_x_frame_asset(
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    return clashrend_boom_wave_frame_asset(false, frame, width, height, pixels);
}

bool clashrend_boom_wave_plus_frame_asset(
    unsigned int frame,
    unsigned int *width,
    unsigned int *height,
    const unsigned char **pixels) {
    return clashrend_boom_wave_frame_asset(true, frame, width, height, pixels);
}

bool clashrend_boom_upload_texture_set(
    ycTexture **textures,
    unsigned int frame_count,
    bool (*asset)(unsigned int, unsigned int *, unsigned int *, const unsigned char **),
    MM_Color (*color_from_index)(unsigned char)) {
    MM_Color colors[81u * 81u]{};
    for (unsigned int frame = 0; frame < frame_count; ++frame) {
        unsigned int width = 0;
        unsigned int height = 0;
        const unsigned char *pixels = nullptr;
        if (!asset(frame, &width, &height, &pixels) || width * height > 81u * 81u) {
            return false;
        }
        std::memset(colors, 0, sizeof(colors));
        const bool flip_y = env_bool("MINA_XMARK_BOOM_FLIP_Y", true);
        for (unsigned int y = 0; y < height; ++y) {
            const unsigned int source_y = flip_y ? height - 1u - y : y;
            for (unsigned int x = 0; x < width; ++x) {
                colors[y * width + x] =
                    color_from_index(pixels[source_y * width + x]);
            }
        }
        if (!textures[frame]) {
            textures[frame] = g_mina->CreateTexture(width, height);
        }
        if (!textures[frame]) {
            return false;
        }
        g_mina->UpdateTexture(textures[frame], colors);
    }
    return true;
}

bool clashrend_boom_debug_draw_ensure_initialized() {
    if (!env_bool("MINA_XMARK_BOOM_PATTERN_ENABLED", true)) {
        return false;
    }
    if (g_clashrend_boom_debug_draw_ready) {
        return true;
    }
    if (!xmark_marker_debug_draw_api_available() || !xmark_marker_debug_draw_util()) {
        return false;
    }

    bool ok = true;
    __try {
        ok = clashrend_boom_upload_texture_set(
                 g_clashrend_boom_native_dome_debug_textures,
                 xmark_assets::BoomNativeDomeFrameCount,
                 clashrend_boom_native_dome_frame_asset,
                 clashrend_boom_star_color_from_index) &&
            clashrend_boom_upload_texture_set(
                 g_clashrend_boom_wave_x_debug_textures,
                 xmark_assets::BoomWaveXFrameCount,
                 clashrend_boom_wave_x_frame_asset,
                 clashrend_boom_color_from_index) &&
            clashrend_boom_upload_texture_set(
                 g_clashrend_boom_wave_plus_debug_textures,
                 xmark_assets::BoomWavePlusFrameCount,
                 clashrend_boom_wave_plus_frame_asset,
                 clashrend_boom_color_from_index);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        return false;
    }
    g_clashrend_boom_debug_draw_ready = true;
    if (g_mina && !g_clashrend_boom_debug_draw_logged_ready) {
        g_mina->Log("XMarkBurn Clashrend Boom debug textured draw ready.\n");
        g_clashrend_boom_debug_draw_logged_ready = true;
    }
    return true;
}

void clashrend_boom_debug_draw_wave(
    ycDrawUtil *draw_util,
    const Vec3 &center,
    bool plus_shape,
    unsigned int frame,
    float units_per_pixel_override = 0.0f) {
    const unsigned int wave_frame = frame % xmark_assets::BoomWaveXFrameCount;
    unsigned int width = 0;
    unsigned int height = 0;
    const unsigned char *pixels = nullptr;
    if (!clashrend_boom_wave_frame_asset(
            plus_shape, wave_frame, &width, &height, &pixels)) {
        return;
    }
    ycTexture *texture = plus_shape
        ? g_clashrend_boom_wave_plus_debug_textures[wave_frame]
        : g_clashrend_boom_wave_x_debug_textures[wave_frame];
    if (!texture) {
        return;
    }
    const float units_per_pixel = units_per_pixel_override > 0.0f
        ? units_per_pixel_override
        : std::max(0.01f, env_float("MINA_XMARK_BOOM_WORLD_UNITS_PER_PIXEL", 0.08f));
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    MM_Vec3 position{
        center.x,
        center.y,
        center.z + env_float("MINA_XMARK_BOOM_EFFECT_Z_OFFSET", 1.1f),
    };
    __try {
        g_mina->DebugDrawTexturedQuad(
            draw_util,
            texture,
            position,
            static_cast<float>(width) * units_per_pixel,
            static_cast<float>(height) * units_per_pixel,
            white,
            true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_clashrend_boom_debug_draw_ready = false;
    }
}

bool clashrend_boom_debug_draw_native_final_x(
    ycDrawUtil *draw_util,
    const ClashrendBoomPatternState &boom,
    unsigned int frame) {
    if (!draw_util || !boom.native_pattern_ready || boom.native_outer_count < 4u) {
        return false;
    }
    const unsigned int dome_frame =
        frame % xmark_assets::BoomNativeDomeFrameCount;
    unsigned int width = 0;
    unsigned int height = 0;
    const unsigned char *pixels = nullptr;
    if (!clashrend_boom_native_dome_frame_asset(
            dome_frame, &width, &height, &pixels)) {
        return false;
    }
    ycTexture *texture = g_clashrend_boom_native_dome_debug_textures[dome_frame];
    if (!texture) {
        return false;
    }
    const Vec3 forward = clashrend_boom_native_forward_step(boom.direction);
    const float units_per_pixel = std::max(
        0.01f,
        env_float("MINA_XMARK_BOOM_FINAL_NATIVE_DOME_UNITS_PER_PIXEL", 0.08f));
    const float offset_x =
        env_float("MINA_XMARK_BOOM_FINAL_NATIVE_DOME_OFFSET_X", 0.0f);
    const float offset_y =
        env_float("MINA_XMARK_BOOM_FINAL_NATIVE_DOME_OFFSET_Y", 0.0f);
    const float z_offset = env_float("MINA_XMARK_BOOM_EFFECT_Z_OFFSET", 1.1f);
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    unsigned int drawn = 0;
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        const ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        if (!slot.corner) {
            continue;
        }
        MM_Vec3 position{
            slot.world_position.x + forward.x * 2.0f + offset_x,
            slot.world_position.y + forward.y * 2.0f + offset_y,
            slot.world_position.z + forward.z * 2.0f + z_offset,
        };
        __try {
            g_mina->DebugDrawTexturedQuad(
                draw_util,
                texture,
                position,
                static_cast<float>(width) * units_per_pixel,
                static_cast<float>(height) * units_per_pixel,
                white,
                true);
            ++drawn;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_clashrend_boom_debug_draw_ready = false;
            return false;
        }
    }
    return drawn == 4u;
}

void clashrend_boom_debug_draw(unsigned long long now_ms) {
    clashrend_boom_debug_draw_geyser(now_ms);
    ClashrendBoomPatternState &boom = g_clashrend_boom_pattern;
    const bool render_all = env_bool("MINA_XMARK_BOOM_MODAPI_RENDER", false);
    const bool render_owned_final =
        env_bool("MINA_XMARK_BOOM_MODAPI_FINAL_X_RENDER", true) &&
        !boom.third_wave_native_effects_spawned;
    if ((!render_all && !render_owned_final) || !boom.active ||
        xmark_runtime_overlays_hidden_for_pause() ||
        !clashrend_boom_debug_draw_ensure_initialized()) {
        return;
    }
    ycDrawUtil *draw_util = xmark_marker_debug_draw_util();
    if (!draw_util || now_ms < boom.started_ms) {
        return;
    }
    const unsigned int wave_duration_ms =
        std::max(40u, env_uint("MINA_XMARK_BOOM_WAVE_DURATION_MS", 175));
    const unsigned int wave_gap_ms = std::max(
        wave_duration_ms,
        std::max(16u, env_uint("MINA_XMARK_BOOM_WAVE_GAP_MS", wave_duration_ms)));
    const unsigned long long elapsed = now_ms - boom.started_ms;
    if (render_all && elapsed < wave_duration_ms) {
        const unsigned int frame = std::min<unsigned int>(
            xmark_assets::BoomStarFrameCount - 1u,
            static_cast<unsigned int>(elapsed * xmark_assets::BoomStarFrameCount / wave_duration_ms));
        clashrend_boom_debug_draw_wave(draw_util, boom.impact, false, frame);
        if (!boom.debug_draw_logged && g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn Cross Blast wave submitted dir=%s frame=%u elapsedMs=%llu impact=(%.3f, %.3f, %.3f).\n",
                direction_name(boom.direction),
                frame,
                elapsed,
                static_cast<double>(boom.impact.x),
                static_cast<double>(boom.impact.y),
                static_cast<double>(boom.impact.z));
            g_mina->Log(message);
            boom.debug_draw_logged = true;
        }
    }
    if (render_all && elapsed >= wave_gap_ms && elapsed < wave_gap_ms + wave_duration_ms) {
        const unsigned long long plus_elapsed = elapsed - wave_gap_ms;
        const unsigned int frame = std::min<unsigned int>(
            xmark_assets::BoomStarFrameCount - 1u,
            static_cast<unsigned int>(plus_elapsed * xmark_assets::BoomStarFrameCount / wave_duration_ms));
        const float step_distance =
            std::max(0.1f, env_float("MINA_XMARK_BOOM_FORWARD_STEP", 1.0f));
        const Vec3 step = clashrend_boom_direction_step(boom.direction, step_distance);
        Vec3 plus_center = boom.impact;
        plus_center.x += step.x;
        plus_center.y += step.y;
        clashrend_boom_debug_draw_wave(draw_util, plus_center, true, frame);
    }
    const unsigned int final_duration_ms = env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE", false)
        ? std::max(
              wave_duration_ms,
              env_uint("MINA_XMARK_BOOM_NATIVE_FINAL_X_DURATION_MS", 360u))
        : wave_duration_ms;
    if (render_owned_final && boom.third_wave_spawned && boom.third_wave_started_ms &&
        now_ms >= boom.third_wave_started_ms &&
        now_ms < boom.third_wave_started_ms + final_duration_ms) {
        const unsigned long long final_elapsed = now_ms - boom.third_wave_started_ms;
        const unsigned int frame = std::min<unsigned int>(
            xmark_assets::BoomStarFrameCount - 1u,
            static_cast<unsigned int>(
                final_elapsed * xmark_assets::BoomStarFrameCount / final_duration_ms));
        const Vec3 step = env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE", false)
            ? clashrend_boom_native_forward_step(boom.direction)
            : clashrend_boom_direction_step(
                boom.direction,
                std::max(0.1f, env_float("MINA_XMARK_BOOM_FORWARD_STEP", 1.0f)));
        Vec3 final_center = boom.impact;
        final_center.x += step.x * 2.0f;
        final_center.y += step.y * 2.0f;
        final_center.z += step.z * 2.0f;
        if (!env_bool("MINA_XMARK_BOOM_FINAL_USE_NATIVE_DOME_CELLS", true) ||
            !clashrend_boom_debug_draw_native_final_x(draw_util, boom, frame)) {
            clashrend_boom_debug_draw_wave(
                draw_util,
                final_center,
                false,
                frame,
                std::max(
                    0.01f,
                    env_float("MINA_XMARK_BOOM_FINAL_X_WORLD_UNITS_PER_PIXEL", 0.08f)));
        }
    }
}

bool xmark_burn_debug_draw_handles_effects() {
    return xmark_burn_debug_draw_enabled() && g_xmark_burn_debug_draw_ready;
}

bool xmark_burn_direct_follow_allowed(const XMarkBurnEffect &burn) {
    if (!burn.active ||
        !burn.official_follow ||
        (!burn.target && !burn.official_combat_core) ||
        burn.lethal_cleanup_applied) {
        return false;
    }
    return !burn.lethal_written_ms ||
        !env_bool("MINA_XMARK_BURN_FREEZE_ON_DEATH", false) ||
        !burn.has_lethal_freeze_position;
}

void commit_xmark_burn_follow_position(
    XMarkBurnEffect &burn,
    const Vec3 &target_position,
    unsigned long long now_ms) {
    burn.last_position = xmark_attachment_mark_position(target_position);
    burn.has_last_position = true;
    burn.last_visual_resolved_ms = now_ms;
    burn.last_visual_missing_ms = 0;
    burn.visual_missing_count = 0;

    if (burn.lethal_written_ms &&
        !env_bool("MINA_XMARK_BURN_FREEZE_ON_DEATH", false)) {
        burn.lethal_freeze_position = burn.last_position;
        burn.has_lethal_freeze_position = true;
    }
}

bool resolve_xmark_burn_direct_follow_position(
    XMarkBurnEffect &burn,
    unsigned long long now_ms,
    bool prefer_live_official_center,
    Vec3 *position_out) {
    if (!xmark_burn_direct_follow_allowed(burn)) {
        return false;
    }

    XMarkOfficialEnemyHost official_host{};
    const bool host_resolved = official_enemy_host_for_burn(burn, &official_host);
    const uintptr_t follow_entity = host_resolved && official_host.entity
        ? official_host.entity
        : burn.target;
    Vec3 target_position{};
    if (!follow_entity ||
        !official_entity_world_position_read(follow_entity, &target_position)) {
        return false;
    }

    if (host_resolved) {
        burn.target = official_host.entity ? official_host.entity : follow_entity;
        if (official_host.combat_core) {
            burn.official_combat_core = official_host.combat_core;
        }
        if (prefer_live_official_center &&
            official_host.active &&
            (burn.suppress_damage || official_host.health > 0.0f)) {
            target_position = official_host.position;
            const float geometry_half = official_enemy_render_half_from_health(
                official_host,
                xmark_default_render_half_w());
            const float health_half = official_enemy_burn_render_half_from_health(
                official_host,
                xmark_default_render_half_w());
            const float render_half = std::max(geometry_half, health_half);
            burn.render_half_w = render_half;
            burn.render_half_h = render_half;
        }
    }
    if (burn.has_runtime_visual_offset) {
        target_position.x += burn.runtime_visual_offset.x;
        target_position.y += burn.runtime_visual_offset.y;
        target_position.z += burn.runtime_visual_offset.z;
    }

    commit_xmark_burn_follow_position(burn, target_position, now_ms);
    if (position_out) {
        *position_out = burn.last_position;
    }
    return true;
}

bool xmark_burn_debug_current_position(XMarkBurnEffect &burn, unsigned long long now_ms, Vec3 *position_out) {
    if (position_out) {
        *position_out = burn.has_last_position ? burn.last_position : Vec3{0.0f, 0.0f, 0.0f};
    }
    if (!burn.active) {
        return false;
    }

    if (env_bool("MINA_XMARK_BURN_DEBUG_DRAW_PREFER_DIRECT_TRANSFORM", true) &&
        resolve_xmark_burn_direct_follow_position(
            burn,
            now_ms,
            env_bool("MINA_XMARK_BURN_DEBUG_DRAW_USE_OFFICIAL_HOST_CENTER", true),
            position_out)) {
        return true;
    }

    if (env_bool("MINA_XMARK_BURN_DEBUG_DRAW_REFRESH_VISUAL_FOLLOW", true) &&
        burn.visual_key) {
        XMarkVisualEnemyHost host{};
        if (visual_enemy_host_by_key(burn.visual_key, &host, now_ms)) {
            const unsigned int stale_ms =
                std::max(16u, env_uint("MINA_XMARK_BURN_DEBUG_DRAW_VISUAL_STALE_MS", 96));
            if (!host.last_seen_ms || now_ms <= host.last_seen_ms + stale_ms) {
                const Vec3 visual_position = xmark_burn_visual_host_render_position(burn, host, now_ms);
                commit_xmark_burn_follow_position(burn, visual_position, now_ms);
                xmark_visual_host_render_halves(host, &burn.render_half_w, &burn.render_half_h);
                if (position_out) {
                    *position_out = burn.last_position;
                }
                return true;
            }
        }
    }
    return burn.has_last_position;
}

void xmark_burn_debug_draw_quad(
    ycDrawUtil *draw_util,
    ycTexture *texture,
    const Vec3 &center,
    float half_w,
    float half_h,
    float z_offset,
    MM_Color color) {
    if (!draw_util || !texture || !g_mina) {
        return;
    }
    Vec3 snapped_center = center;
    if (env_bool("MINA_XMARK_BURN_DEBUG_DRAW_PIXEL_SNAP", true)) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        snapped_center.x = std::round(snapped_center.x / units_per_pixel) * units_per_pixel;
        snapped_center.y = std::round(snapped_center.y / units_per_pixel) * units_per_pixel;
        half_w = std::round((half_w * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
        half_h = std::round((half_h * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
    }
    half_w = std::max(0.001f, half_w);
    half_h = std::max(0.001f, half_h);
    __try {
        g_mina->DebugDrawTexturedQuad(
            draw_util,
            texture,
            MM_Vec3{snapped_center.x, snapped_center.y, snapped_center.z + z_offset},
            half_w * 2.0f,
            half_h * 2.0f,
            color,
            true);
        ++g_xmark_burn_debug_draw_calls;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "draw-fault");
        g_xmark_burn_debug_draw_ready = false;
    }
}

void xmark_burn_debug_draw_effects(unsigned long long now_ms) {
    if (!g_xmark_burn_debug_draw_phase) {
        return;
    }
    if (xmark_runtime_overlays_hidden_for_pause()) {
        bool muriel_text_display_burn = false;
        int game_state = -1;
        __try {
            game_state = g_mina ? g_mina->GetCurrentGameState() : -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            game_state = -1;
        }
        if (game_state == GAMESTATE_TEXTDISPLAY) {
            for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
                XMarkOfficialEnemyHost host{};
                if (burn.active && now_ms < burn.expires_ms &&
                    (burn.suppress_damage ||
                     (official_enemy_host_for_burn(burn, &host) &&
                      xmark_official_host_is_muriel(host)))) {
                    muriel_text_display_burn = true;
                    break;
                }
            }
        }
        if (!muriel_text_display_burn) {
            return;
        }
    }
    if (!xmark_burn_debug_draw_ensure_initialized() || !g_mina) {
        return;
    }
    ycDrawUtil *draw_util = xmark_marker_debug_draw_util();
    if (!draw_util) {
        std::snprintf(g_xmark_burn_debug_draw_status, sizeof(g_xmark_burn_debug_draw_status), "draw-layer-missing");
        return;
    }

    const float half_w = xmark_default_render_half_w();
    const float half_h = xmark_default_render_half_h();
    const float burn_shade_z_offset = env_float("MINA_XMARK_BURN_SHADE_RENDER_Z_OFFSET", 0.26f);
    const float burn_shade_scale_x = std::max(0.1f, env_float("MINA_XMARK_BURN_SHADE_RENDER_SCALE_X", 0.75f));
    const float burn_shade_scale_y = std::max(0.1f, env_float("MINA_XMARK_BURN_SHADE_RENDER_SCALE_Y", 0.75f));
    const float burn_fire_z_offset = env_float("MINA_XMARK_BURN_FIRE_RENDER_Z_OFFSET", 0.22f);
    const float burn_fire_half_w = env_float("MINA_XMARK_BURN_FIRE_RENDER_HALF_WIDTH", 0.46875f);
    const float burn_fire_half_h = env_float("MINA_XMARK_BURN_FIRE_RENDER_HALF_HEIGHT", 0.46875f);
    const float burn_tick_fire_half_w = env_float("MINA_XMARK_BURN_TICK_FIRE_RENDER_HALF_WIDTH", 0.5625f);
    const float burn_tick_fire_half_h = env_float("MINA_XMARK_BURN_TICK_FIRE_RENDER_HALF_HEIGHT", 0.5625f);
    const unsigned int burn_fire_pop_ms = std::max(1u, env_uint("MINA_XMARK_BURN_FIRE_POP_MS", 260));
    const unsigned int burn_cycle_ms = std::max(80u, env_uint("MINA_XMARK_BURN_DEATH_FLAME_CYCLE_MS", 420));
    const unsigned int burn_shade_cycle_ms = std::max(80u, env_uint("MINA_XMARK_BURN_SHADE_CYCLE_MS", 360));
    const unsigned int burn_damage_blink_ms = env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_MS", 160);
    const unsigned int burn_damage_blink_step_ms = std::max(1u, env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_STEP_MS", 55));
    const MM_Color flame_color = xmark_alpha_color(255, 210, 210, 255);

    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active || now_ms >= burn.expires_ms) {
            continue;
        }
        Vec3 burn_position{};
        if (!xmark_burn_debug_current_position(burn, now_ms, &burn_position)) {
            continue;
        }
        ++burn.visual_emit_frame_count;
        if (burn.visual_emit_frame_count == 1u && g_mina &&
            env_bool("MINA_XMARK_BURN_VISUAL_SUBMIT_LOG", false)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn burn visuals submitted backend=DebugDraw target=0x%p pos=(%.3f, %.3f, %.3f) status=%s\n",
                reinterpret_cast<void *>(burn.target),
                static_cast<double>(burn_position.x),
                static_cast<double>(burn_position.y),
                static_cast<double>(burn_position.z),
                g_xmark_burn_debug_draw_status);
            g_mina->Log(message);
        }

        const float burn_half_w = burn.render_half_w > 0.0f ? burn.render_half_w : half_w;
        const float burn_half_h = burn.render_half_h > 0.0f ? burn.render_half_h : half_h;
        const unsigned long long elapsed = burn.started_ms && now_ms >= burn.started_ms
            ? now_ms - burn.started_ms
            : 0ull;
        const unsigned long long shade_phase_ms = elapsed % burn_shade_cycle_ms;
        const float shade_wave = static_cast<float>(shade_phase_ms) / static_cast<float>(burn_shade_cycle_ms);
        const float shade_pulse = shade_wave < 0.5f ? shade_wave * 2.0f : (1.0f - shade_wave) * 2.0f;
        unsigned int shade_frame = shade_pulse > 0.55f ? 1u : 0u;
        unsigned char shade_r = static_cast<unsigned char>(210u + static_cast<unsigned int>(shade_pulse * 45.0f));
        unsigned char shade_g = static_cast<unsigned char>(24u + static_cast<unsigned int>(shade_pulse * 82.0f));
        unsigned char shade_b = static_cast<unsigned char>(6u + static_cast<unsigned int>(shade_pulse * 26.0f));
        unsigned char shade_a = static_cast<unsigned char>(
            std::min<unsigned int>(255u, env_uint("MINA_XMARK_BURN_SHADE_ALPHA", 165u)));
        if (burn.last_tick_ms && now_ms >= burn.last_tick_ms) {
            const unsigned long long damage_elapsed = now_ms - burn.last_tick_ms;
            if (damage_elapsed < burn_damage_blink_ms) {
                const bool blink_hot = ((damage_elapsed / burn_damage_blink_step_ms) & 1ull) == 0ull;
                shade_frame = 1u;
                shade_r = blink_hot ? 255u : 168u;
                shade_g = blink_hot ? 242u : 0u;
                shade_b = blink_hot ? 207u : 22u;
                shade_a = static_cast<unsigned char>(
                    std::min<unsigned int>(255u, env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_ALPHA", 245u)));
            }
        }
        if (env_bool("MINA_XMARK_BURN_SHADE_RENDER_ENABLED", false)) {
            const float fallback_shade_half_w = env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_W", 0.55f);
            const float fallback_shade_half_h = env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_H", 0.55f);
            const float shade_half_w = env_bool("MINA_XMARK_BURN_SHADE_USE_BURN_SIZE", true)
                ? std::max(fallback_shade_half_w, burn_half_w)
                : fallback_shade_half_w;
            const float shade_half_h = env_bool("MINA_XMARK_BURN_SHADE_USE_BURN_SIZE", true)
                ? std::max(fallback_shade_half_h, burn_half_h)
                : fallback_shade_half_h;
            xmark_burn_debug_draw_quad(
                draw_util,
                g_xmark_burn_debug_shade_textures[shade_frame],
                burn_position,
                shade_half_w * burn_shade_scale_x,
                shade_half_h * burn_shade_scale_y,
                burn_shade_z_offset,
                xmark_alpha_color(shade_r, shade_g, shade_b, shade_a));
        }

        const unsigned int burn_fire_after_death_ms =
            env_uint("MINA_XMARK_BURN_FIRE_AFTER_DEATH_MS", 0);
        const bool burn_fire_until_cleanup =
            env_bool("MINA_XMARK_BURN_FIRE_UNTIL_LETHAL_CLEANUP", true);
        const bool burn_fire_can_emit =
            !burn.lethal_written_ms ||
            (burn_fire_until_cleanup && !burn.lethal_cleanup_applied) ||
            now_ms < burn.lethal_written_ms + burn_fire_after_death_ms;
        if (!burn_fire_can_emit) {
            continue;
        }

        const unsigned int flame_count =
            std::max(1u, env_uint("MINA_XMARK_BURN_DEATH_FLAME_COUNT", 6u));
        const float flame_spread_scale = burn.suppress_damage
            ? std::max(1.0f, env_float("MINA_XMARK_MURIEL_BURN_FLAME_SPREAD_SCALE", 1.70f))
            : 1.0f;
        const unsigned long long cycle_index = elapsed / burn_cycle_ms;
        const unsigned long long cycle_phase = elapsed % burn_cycle_ms;
        const float spread_x = std::max(
            0.04f,
            burn_half_w * env_float("MINA_XMARK_BURN_DEATH_FLAME_SIDE_FALL_X", 1.05f) *
                flame_spread_scale);
        const float spread_y = std::max(
            0.04f,
            burn_half_h * env_float("MINA_XMARK_BURN_DEATH_FLAME_FALL_Y", 0.85f) *
                flame_spread_scale);
        for (unsigned int flame_index = 0; flame_index < flame_count; ++flame_index) {
            const unsigned long long stagger =
                (cycle_phase + (burn_cycle_ms * flame_index) / flame_count) % burn_cycle_ms;
            const float phase = static_cast<float>(stagger) / static_cast<float>(burn_cycle_ms);
            const float disperse = 1.0f - ((1.0f - phase) * (1.0f - phase) * (1.0f - phase));
            const uint32_t seed = xmark_burn_seed(
                burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key),
                burn.started_ms,
                static_cast<unsigned int>(cycle_index),
                flame_index);
            const float turn = static_cast<float>(flame_index) / static_cast<float>(flame_count);
            const float angle =
                (turn * 6.2831853f) +
                xmark_noise_signed(seed + 3u) *
                    std::max(
                        0.0f,
                        env_float("MINA_XMARK_BURN_DEATH_FLAME_ANGLE_JITTER_DEG", 52.0f) +
                            env_float("MINA_XMARK_BURN_DEATH_FLAME_EXTRA_ANGLE_JITTER_DEG", 0.0f)) *
                    0.017453292519943295f;
            const float travel_scale = std::max(
                0.2f,
                1.0f +
                    xmark_noise_signed(seed + 6u) *
                        env_float("MINA_XMARK_BURN_DEATH_FLAME_TRAVEL_VARIANCE", 0.32f));
            Vec3 flame_position = burn_position;
            flame_position.x += xmark_noise_signed(seed + 1u) * burn_half_w *
                env_float("MINA_XMARK_BURN_DEATH_FLAME_SPAWN_JITTER_X", 0.26f);
            flame_position.y += xmark_noise_signed(seed + 2u) * burn_half_h *
                env_float("MINA_XMARK_BURN_DEATH_FLAME_SPAWN_JITTER_Y", 0.18f);
            flame_position.x += std::cos(angle) * spread_x * disperse * travel_scale;
            flame_position.y += std::sin(angle) * spread_y * disperse * travel_scale;
            flame_position.x += std::sin((phase + flame_index * 0.37f) * 6.2831853f) *
                burn_half_w * env_float("MINA_XMARK_BURN_FIRE_DRIFT_X", 0.10f);
            flame_position.y += std::cos((phase + flame_index * 0.19f) * 6.2831853f) *
                burn_half_h * env_float("MINA_XMARK_BURN_FIRE_DRIFT_Y", 0.14f);
            const unsigned int fire_frame = std::min<unsigned int>(
                kXMarkRenderFireFrameCount - 1u,
                static_cast<unsigned int>((stagger * kXMarkRenderFireFrameCount) / burn_cycle_ms));
            xmark_burn_debug_draw_quad(
                draw_util,
                g_xmark_burn_debug_fire_textures[fire_frame],
                flame_position,
                burn_fire_half_w,
                burn_fire_half_h,
                burn_fire_z_offset,
                flame_color);
        }

        if (burn.last_tick_ms && now_ms >= burn.last_tick_ms) {
            const unsigned long long tick_elapsed = now_ms - burn.last_tick_ms;
            if (tick_elapsed < burn_fire_pop_ms) {
                const float phase = static_cast<float>(tick_elapsed) / static_cast<float>(burn_fire_pop_ms);
                const float burst = 1.0f - ((1.0f - phase) * (1.0f - phase));
                const unsigned int tick_count = env_bool("MINA_XMARK_BURN_TICK_FIRE_SPLIT", true)
                    ? std::max(2u, env_uint("MINA_XMARK_BURN_TICK_FIRE_COUNT", 3u))
                    : 1u;
                const unsigned int fire_frame = std::min<unsigned int>(
                    kXMarkRenderFireFrameCount - 1u,
                    static_cast<unsigned int>((tick_elapsed * kXMarkRenderFireFrameCount) / burn_fire_pop_ms));
                for (unsigned int i = 0; i < tick_count; ++i) {
                    const uint32_t seed = xmark_burn_seed(
                        burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key),
                        burn.started_ms,
                        burn.tick_count,
                        i + 17u);
                    const float turn = tick_count > 0 ? static_cast<float>(i) / static_cast<float>(tick_count) : 0.0f;
                    const float angle =
                        (turn * 6.2831853f) +
                        xmark_noise_signed(seed + 4u) *
                            (env_float("MINA_XMARK_BURN_TICK_FIRE_ANGLE_JITTER_DEG", 78.0f) +
                             env_float("MINA_XMARK_BURN_TICK_FIRE_EXTRA_ANGLE_JITTER_DEG", 50.0f)) *
                            0.017453292519943295f;
                    const float travel_scale = std::max(
                        0.2f,
                        1.0f +
                            xmark_noise_signed(seed + 5u) *
                                env_float("MINA_XMARK_BURN_TICK_FIRE_TRAVEL_VARIANCE", 0.34f));
                    Vec3 tick_position = burn_position;
                    tick_position.x += env_float("MINA_XMARK_BURN_FIRE_RENDER_OFFSET_X", 0.0f);
                    tick_position.y += env_float("MINA_XMARK_BURN_FIRE_RENDER_OFFSET_Y", -0.10f);
                    tick_position.x += std::cos(angle) * burn_half_w *
                        env_float("MINA_XMARK_BURN_TICK_FIRE_SIDE_X", 0.95f) *
                        flame_spread_scale * burst * travel_scale;
                    tick_position.y += std::sin(angle) * burn_half_h *
                        env_float("MINA_XMARK_BURN_TICK_FIRE_FALL_Y", 0.80f) *
                        flame_spread_scale * burst * travel_scale;
                    tick_position.x += xmark_noise_signed(seed + 2u) * burn_half_w *
                        env_float("MINA_XMARK_BURN_TICK_FIRE_SPAWN_JITTER_X", 0.24f);
                    tick_position.y += xmark_noise_signed(seed + 3u) * burn_half_h *
                        env_float("MINA_XMARK_BURN_TICK_FIRE_SPAWN_JITTER_Y", 0.16f);
                    xmark_burn_debug_draw_quad(
                        draw_util,
                        g_xmark_burn_debug_fire_textures[fire_frame],
                        tick_position,
                        burn_tick_fire_half_w,
                        burn_tick_fire_half_h,
                        burn_fire_z_offset,
                        xmark_alpha_color(255, 220, 200, 255));
                }
            }
        }
    }

    const unsigned int burst_flame_count =
        std::max(1u, env_uint("MINA_XMARK_BURN_DEATH_BURST_COUNT", 9u));
    for (XMarkBurnDeathBurst &burst : g_xmark_burn_death_bursts) {
        if (!burst.active) {
            continue;
        }
        if (now_ms >= burst.expires_ms || burst.expires_ms <= burst.started_ms) {
            burst.active = false;
            continue;
        }
        const unsigned long long elapsed_ms = now_ms >= burst.started_ms ? now_ms - burst.started_ms : 0ull;
        const unsigned long long duration_ms = burst.expires_ms - burst.started_ms;
        const float phase = std::min(1.0f, static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms));
        const float fade = std::max(0.0f, 1.0f - phase);
        const unsigned char alpha = static_cast<unsigned char>(
            std::min<unsigned int>(255u, static_cast<unsigned int>(fade * 255.0f)));
        if (alpha < 8u) {
            continue;
        }
        for (unsigned int i = 0; i < burst_flame_count; ++i) {
            const uint32_t seed = xmark_burn_seed(
                burst.seed_target,
                burst.started_ms,
                i + 113u,
                static_cast<unsigned int>(elapsed_ms / 16ull));
            const float turn = static_cast<float>(i) / static_cast<float>(burst_flame_count);
            const float angle = turn * 6.2831853f + xmark_noise_signed(seed + 1u) * 1.3f;
            const float burst_out = 1.0f - ((1.0f - phase) * (1.0f - phase));
            Vec3 burst_position = burst.position;
            burst_position.x += std::cos(angle) * (1.05f + burst.half_w * 0.28f) * burst_out;
            burst_position.y += std::sin(angle) * (0.95f + burst.half_h * 0.24f) * burst_out;
            burst_position.x += xmark_noise_signed(seed + 4u) * 0.16f;
            burst_position.y += xmark_noise_signed(seed + 5u) * 0.12f;
            const unsigned int fire_frame = std::min<unsigned int>(
                kXMarkRenderFireFrameCount - 1u,
                static_cast<unsigned int>((elapsed_ms * kXMarkRenderFireFrameCount) / duration_ms));
            xmark_burn_debug_draw_quad(
                draw_util,
                g_xmark_burn_debug_fire_textures[fire_frame],
                burst_position,
                burn_tick_fire_half_w,
                burn_tick_fire_half_h,
                burn_fire_z_offset,
                xmark_alpha_color(255, 220, 190, alpha));
        }
    }
}

void clashrend_boom_debug_draw_geyser(unsigned long long now_ms) {
    ClashrendBoomGeyserState &geyser = g_clashrend_boom_geyser;
    if (!geyser.active || now_ms < geyser.started_ms ||
        now_ms >= geyser.expires_ms || xmark_runtime_overlays_hidden_for_pause() ||
        !xmark_burn_debug_draw_ensure_initialized()) {
        return;
    }
    ycDrawUtil *draw_util = xmark_marker_debug_draw_util();
    if (!draw_util) {
        return;
    }

    const unsigned long long elapsed_ms = now_ms - geyser.started_ms;
    const unsigned int frame_ms = std::max(
        24u,
        env_uint("MINA_XMARK_BOOM_GEYSER_FRAME_MS", 52u));
    const float units_per_pixel = xmark_visual_host_units_per_pixel();
    const float pixel_scale = std::max(
        1.0f,
        std::round(env_float("MINA_XMARK_BOOM_GEYSER_PIXEL_SCALE", 1.0f)));
    const float one_x_half_extent =
        static_cast<float>(kXMarkRenderFrameSize) * units_per_pixel * 0.5f;
    const float half_w = one_x_half_extent * pixel_scale;
    const float half_h = one_x_half_extent * pixel_scale;
    const float z_offset = env_float("MINA_XMARK_BOOM_GEYSER_Z_OFFSET", 1.35f);
    const unsigned int geyser_ms = std::max(
        80u,
        env_uint("MINA_XMARK_BOOM_GEYSER_COLUMN_MS", 220u));

    if (elapsed_ms < geyser_ms) {
        const float phase = static_cast<float>(elapsed_ms) / static_cast<float>(geyser_ms);
        const float rise = 1.0f - ((1.0f - phase) * (1.0f - phase));
        const unsigned int column_count = std::max(
            1u,
            env_uint("MINA_XMARK_BOOM_GEYSER_COLUMN_FLAMES", 3u));
        for (unsigned int index = 0; index < column_count; ++index) {
            Vec3 position = geyser.center;
            const float stagger = static_cast<float>(index) / static_cast<float>(column_count);
            position.x += xmark_noise_signed(
                xmark_burn_seed(index + 1u, geyser.started_ms, index, 17u)) * 0.16f;
            position.y += rise * (0.35f + stagger * 0.85f);
            const unsigned int frame = std::min<unsigned int>(
                kXMarkRenderFireFrameCount - 1u,
                static_cast<unsigned int>((elapsed_ms / frame_ms + index) %
                    kXMarkRenderFireFrameCount));
            xmark_burn_debug_draw_quad(
                draw_util,
                g_xmark_burn_debug_fire_textures[frame],
                position,
                half_w * (1.2f - stagger * 0.18f),
                half_h * (1.2f - stagger * 0.18f),
                z_offset,
                xmark_alpha_color(255, 226, 190, 255));
        }
    }

    for (unsigned int index = 0; index < geyser.ember_count; ++index) {
        const ClashrendBoomEmber &ember = geyser.embers[index];
        if (!ember.active || now_ms < ember.started_ms || now_ms >= ember.expires_ms) {
            continue;
        }
        const unsigned long long ember_elapsed = now_ms - ember.started_ms;
        const unsigned long long ember_duration = ember.expires_ms - ember.started_ms;
        const float life = std::min(
            1.0f,
            static_cast<float>(ember_elapsed) /
                static_cast<float>(std::max<unsigned long long>(1ull, ember_duration)));
        const unsigned char alpha = static_cast<unsigned char>(
            std::max(0.0f, 255.0f * (1.0f - life * life)));
        if (alpha < 8u) {
            continue;
        }
        const unsigned int frame = std::min<unsigned int>(
            kXMarkRenderFireFrameCount - 1u,
            static_cast<unsigned int>((ember_elapsed / frame_ms + index) %
                kXMarkRenderFireFrameCount));
        const float scale = std::max(0.58f, 1.0f - life * 0.28f);
        xmark_burn_debug_draw_quad(
            draw_util,
            g_xmark_burn_debug_fire_textures[frame],
            ember.position,
            half_w * scale,
            half_h * scale,
            z_offset,
            xmark_alpha_color(255, 220, 185, alpha));
    }
}

bool xmark_hud_render_enabled() {
    return env_bool("MINA_XMARK_HUD_RENDER_ENABLED", true);
}

void xmark_hud_render_make_texture_pixels(MM_Color *pixels) {
    if (!pixels) {
        return;
    }
    const unsigned int atlas_width = kXMarkHudRenderFrameSize * kXMarkHudRenderFrameCount;
    std::memset(pixels, 0, sizeof(MM_Color) * atlas_width * kXMarkHudRenderFrameSize);
    auto copy_frame = [pixels, atlas_width](
                          unsigned int frame,
                          unsigned int width,
                          unsigned int height,
                          const unsigned char *asset_pixels,
                          bool flame_palette) {
        if (!asset_pixels || frame >= kXMarkHudRenderFrameCount) {
            return;
        }
        const unsigned int copy_width = std::min(width, kXMarkHudRenderFrameSize / kXMarkHudRenderPixelScale);
        const unsigned int copy_height = std::min(height, kXMarkHudRenderFrameSize / kXMarkHudRenderPixelScale);
        const unsigned int scaled_width = copy_width * kXMarkHudRenderPixelScale;
        const unsigned int scaled_height = copy_height * kXMarkHudRenderPixelScale;
        const unsigned int pad_x = (kXMarkHudRenderFrameSize - scaled_width) / 2u;
        const unsigned int pad_y = (kXMarkHudRenderFrameSize - scaled_height) / 2u;
        for (unsigned int y = 0; y < copy_height; ++y) {
            for (unsigned int x = 0; x < copy_width; ++x) {
                const unsigned char value = asset_pixels[y * width + x];
                const MM_Color color = flame_palette
                    ? xmark_render_flame_color_from_index(value)
                    : xmark_render_color_from_index(value);
                const unsigned int dst_x = pad_x + x * kXMarkHudRenderPixelScale;
                const unsigned int dst_y = pad_y + y * kXMarkHudRenderPixelScale;
                for (unsigned int sy = 0; sy < kXMarkHudRenderPixelScale; ++sy) {
                    for (unsigned int sx = 0; sx < kXMarkHudRenderPixelScale; ++sx) {
                        pixels[(dst_y + sy) * atlas_width + frame * kXMarkHudRenderFrameSize + dst_x + sx] =
                            color;
                    }
                }
            }
        }
    };
    copy_frame(0, xmark_assets::HudWidth, xmark_assets::HudHeight, xmark_assets::HudPixels, false);
    copy_frame(1, xmark_assets::Fire1Width, xmark_assets::Fire1Height, xmark_assets::Fire1Pixels, true);
    copy_frame(
        2,
        xmark_assets::BurnDamage1Width,
        xmark_assets::BurnDamage1Height,
        xmark_assets::BurnDamage1Pixels,
        false);

    const auto fill_solid_frame = [pixels, atlas_width](
                                      unsigned int frame,
                                      const MM_Color &color) {
        if (frame >= kXMarkHudRenderFrameCount) {
            return;
        }
        const unsigned int x0 = frame * kXMarkHudRenderFrameSize;
        for (unsigned int y = 0; y < kXMarkHudRenderFrameSize; ++y) {
            for (unsigned int x = 0; x < kXMarkHudRenderFrameSize; ++x) {
                pixels[y * atlas_width + x0 + x] = color;
            }
        }
    };
    fill_solid_frame(3u, xmark_alpha_color(215, 215, 215, 255));
    fill_solid_frame(4u, xmark_alpha_color(79, 21, 7, 255));
    fill_solid_frame(5u, xmark_alpha_color(248, 8, 40, 255));
}

void xmark_hud_render_object_draw(void *, MinaModRenderCtx *ctx) {
    if (!g_mina ||
        !ctx ||
        !ctx->drawCall ||
        !ctx->cmdList ||
        !g_xmark_hud_render_backend_ready ||
        !g_xmark_hud_render_quads ||
        !g_xmark_hud_render_vertex_buffer ||
        !g_xmark_hud_render_index_buffer ||
        !g_xmark_hud_render_texture) {
        return;
    }
    if (xmark_runtime_overlays_hidden_for_pause() || xmark_text_display_active()) {
        return;
    }
    ycRenderDrawCall *dc = ctx->drawCall;
    g_mina->RenderDrawCallSetIndexBuffer(dc, g_xmark_hud_render_index_buffer);
    g_mina->RenderDrawCallSetVertexBuffer(dc, g_xmark_hud_render_vertex_buffer);
    g_mina->RenderDrawCallSetTexture(dc, g_xmark_hud_render_texture);
    g_mina->RenderCmdDrawIndexed(ctx->cmdList, dc, g_xmark_hud_render_quads * 6u, 0u);
    ++g_xmark_hud_render_draw_calls;
}

bool xmark_hud_render_backend_ensure_initialized() {
    if (!xmark_hud_render_enabled()) {
        g_xmark_hud_render_quads = 0;
        std::snprintf(g_xmark_hud_render_backend_status, sizeof(g_xmark_hud_render_backend_status), "disabled");
        return false;
    }
    if (g_xmark_hud_render_backend_ready) {
        return true;
    }
    g_xmark_hud_render_backend_available = xmark_render_api_available();
    if (!g_xmark_hud_render_backend_available) {
        std::snprintf(
            g_xmark_hud_render_backend_status,
            sizeof(g_xmark_hud_render_backend_status),
            "render-api-unavailable");
        return false;
    }

    const unsigned int atlas_width = kXMarkHudRenderFrameSize * kXMarkHudRenderFrameCount;
    const unsigned int atlas_height = kXMarkHudRenderFrameSize;
    MM_Color *pixels = static_cast<MM_Color *>(g_mina->Alloc(sizeof(MM_Color) * atlas_width * atlas_height));
    if (!pixels) {
        std::snprintf(g_xmark_hud_render_backend_status, sizeof(g_xmark_hud_render_backend_status), "texture-alloc-failed");
        return false;
    }
    xmark_hud_render_make_texture_pixels(pixels);

    bool ok = false;
    __try {
        g_xmark_hud_render_texture = g_mina->CreateTexture(atlas_width, atlas_height);
        if (g_xmark_hud_render_texture) {
            g_mina->UpdateTexture(g_xmark_hud_render_texture, pixels);
            g_xmark_hud_render_vertex_buffer = g_mina->CreateVertexBuffer(kXMarkHudRenderMaxQuads * 4u);
            g_xmark_hud_render_index_buffer = g_mina->CreateIndexBuffer(kXMarkHudRenderMaxQuads * 6u);
            uint32_t indices[kXMarkHudRenderMaxQuads * 6u]{};
            for (unsigned int i = 0; i < kXMarkHudRenderMaxQuads; ++i) {
                const uint32_t vertex = i * 4u;
                const unsigned int index = i * 6u;
                indices[index + 0] = vertex + 0u;
                indices[index + 1] = vertex + 1u;
                indices[index + 2] = vertex + 2u;
                indices[index + 3] = vertex + 0u;
                indices[index + 4] = vertex + 2u;
                indices[index + 5] = vertex + 3u;
            }
            if (g_xmark_hud_render_index_buffer) {
                g_mina->UpdateGpuBuffer(g_xmark_hud_render_index_buffer, indices);
            }

            char pass_name[64] = "hudEngine";
            if (!xmark_read_environment_value(
                    "MINA_XMARK_HUD_RENDER_PASS",
                    pass_name,
                    sizeof(pass_name))) {
                std::snprintf(pass_name, sizeof(pass_name), "hudEngine");
            }
            ycRenderPass *pass = g_mina->GetRenderPass(pass_name);
            if (pass) {
                g_xmark_hud_render_object = g_mina->CreateRenderObject(pass, xmark_hud_render_object_draw, nullptr);
            }
            ok = g_xmark_hud_render_vertex_buffer &&
                g_xmark_hud_render_index_buffer &&
                g_xmark_hud_render_object;
            if (ok) {
                std::snprintf(g_xmark_hud_render_backend_status, sizeof(g_xmark_hud_render_backend_status), "ready:%s", pass_name);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_mina->Free(pixels);
    if (!ok) {
        std::snprintf(g_xmark_hud_render_backend_status, sizeof(g_xmark_hud_render_backend_status), "resource-create-failed");
        return false;
    }

    g_xmark_hud_render_backend_ready = true;
    if (g_mina && !g_xmark_hud_render_backend_logged_ready) {
        g_mina->Log("XMarkBurn HUD render backend ready.\n");
        g_xmark_hud_render_backend_logged_ready = true;
    }
    return true;
}

bool xmark_hud_client_size(float *width_out, float *height_out) {
    HWND window = GetActiveWindow();
    DWORD window_pid = 0;
    if (window) {
        GetWindowThreadProcessId(window, &window_pid);
    }
    if (!window || window_pid != GetCurrentProcessId()) {
        window = FindWindowW(nullptr, L"Mina the Hollower");
        window_pid = 0;
        if (window) {
            GetWindowThreadProcessId(window, &window_pid);
        }
    }
    if (!window || window_pid != GetCurrentProcessId()) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return false;
    }
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    if (width <= 0.0f || height <= 0.0f) {
        return false;
    }
    if (width_out) {
        *width_out = width;
    }
    if (height_out) {
        *height_out = height;
    }
    return true;
}

void xmark_hud_render_update_vertices(unsigned long long now_ms) {
    if (xmark_runtime_overlays_hidden_for_pause() || xmark_text_display_active()) {
        g_xmark_hud_render_quads = 0;
        return;
    }
    int game_state = -1;
    __try {
        game_state = g_mina ? g_mina->GetCurrentGameState() : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        game_state = -1;
    }
    if (!gameplay_state_can_spawn_test_enemy(game_state)) {
        g_xmark_hud_render_quads = 0;
        return;
    }
    if (!xmark_hud_render_backend_ensure_initialized()) {
        g_xmark_hud_render_quads = 0;
        return;
    }
    const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
    if (!hud_state.active_count || !hud_state.visible) {
        g_xmark_hud_render_quads = 0;
        return;
    }

    float x = env_float("MINA_XMARK_HUD_RENDER_X", 52.0f);
    float y = env_float("MINA_XMARK_HUD_RENDER_Y", -162.0f);
    if (env_bool("MINA_XMARK_HUD_DYNAMIC_VIEWPORT_Y", true)) {
        float client_height = 0.0f;
        if (xmark_hud_client_size(nullptr, &client_height)) {
            y = -(client_height * 0.5f) +
                env_float("MINA_XMARK_HUD_BOTTOM_BAR_CENTER_MARGIN", 55.0f);
        }
    }
    const float z = env_float("MINA_XMARK_HUD_RENDER_Z", 0.5f);
    float half_w = env_float("MINA_XMARK_HUD_RENDER_HALF_W", 4.0f);
    float half_h = env_float("MINA_XMARK_HUD_RENDER_HALF_H", 4.0f);
    if (env_bool("MINA_XMARK_HUD_RENDER_PIXEL_SNAP", true)) {
        x = std::round(x);
        y = std::round(y);
        half_w = std::max(1.0f, std::round(half_w));
        half_h = std::max(1.0f, std::round(half_h));
    }
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    MM_Vertex_PTC verts[kXMarkHudRenderMaxQuads * 4u]{};
    unsigned int quad = 0;
    const auto emit_hud_quad = [&](float center_x, float center_y, float quad_half_w, float quad_half_h, unsigned int frame, float z_offset = 0.0f) {
        if (quad >= kXMarkHudRenderMaxQuads || frame >= kXMarkHudRenderFrameCount) {
            return;
        }
        if (env_bool("MINA_XMARK_HUD_RENDER_PIXEL_SNAP", true)) {
            center_x = std::round(center_x);
            center_y = std::round(center_y);
            quad_half_w = std::max(1.0f, std::round(quad_half_w));
            quad_half_h = std::max(1.0f, std::round(quad_half_h));
        }
        const float u0 = static_cast<float>(frame) / static_cast<float>(kXMarkHudRenderFrameCount);
        const float u1 = static_cast<float>(frame + 1u) / static_cast<float>(kXMarkHudRenderFrameCount);
        const unsigned int base = quad * 4u;
        verts[base + 0].pos = MM_Vec3{center_x - quad_half_w, center_y + quad_half_h, z + z_offset};
        verts[base + 0].u = u0;
        verts[base + 0].v = 0.0f;
        verts[base + 0].color = white;
        verts[base + 1].pos = MM_Vec3{center_x + quad_half_w, center_y + quad_half_h, z + z_offset};
        verts[base + 1].u = u1;
        verts[base + 1].v = 0.0f;
        verts[base + 1].color = white;
        verts[base + 2].pos = MM_Vec3{center_x + quad_half_w, center_y - quad_half_h, z + z_offset};
        verts[base + 2].u = u1;
        verts[base + 2].v = 1.0f;
        verts[base + 2].color = white;
        verts[base + 3].pos = MM_Vec3{center_x - quad_half_w, center_y - quad_half_h, z + z_offset};
        verts[base + 3].u = u0;
        verts[base + 3].v = 1.0f;
        verts[base + 3].color = white;
        ++quad;
    };

    const float icon_bar_gap = env_float("MINA_XMARK_HUD_ICON_BAR_GAP", 6.0f);
    const auto hud_bar_width_for_health_max = [&](float health_max) {
        const float bar_width_base = env_float(
            "MINA_XMARK_HUD_BAR_BASE_W",
            env_float("MINA_XMARK_HUD_DAMAGE_BAR_BASE_W", 3.5f));
        const float bar_width_per_hp = env_float(
            "MINA_XMARK_HUD_BAR_PER_HP_W",
            env_float("MINA_XMARK_HUD_DAMAGE_BAR_PER_HP_W", 3.0f));
        const float bar_width_min = env_float(
            "MINA_XMARK_HUD_BAR_MIN_W",
            env_float("MINA_XMARK_HUD_DAMAGE_BAR_MIN_W", 8.0f));
        const float bar_width_max = env_float(
            "MINA_XMARK_HUD_BAR_MAX_W",
            env_float("MINA_XMARK_HUD_DAMAGE_BAR_MAX_W", 132.0f));
        const float raw_bar_width = bar_width_base + std::max(0.0f, health_max) * bar_width_per_hp;
        return std::max(bar_width_min, std::min(bar_width_max, raw_bar_width));
    };
    bool hud_bar_anchor_valid = false;
    float hud_bar_left_x = 0.0f;
    float hud_bar_right_x = 0.0f;
    float hud_bar_center_y = y;
    if (env_bool("MINA_XMARK_HUD_USE_FIXED_ENEMY_BAR_BOUNDS", false)) {
        hud_bar_left_x = env_float("MINA_XMARK_HUD_ENEMY_BAR_LEFT_X", -99.0f);
        hud_bar_right_x = env_float("MINA_XMARK_HUD_ENEMY_BAR_RIGHT_X", 92.0f);
        hud_bar_center_y = env_bool("MINA_XMARK_HUD_DYNAMIC_VIEWPORT_Y", true)
            ? y
            : env_float("MINA_XMARK_HUD_ENEMY_BAR_CENTER_Y", -508.0f);
        hud_bar_anchor_valid = hud_bar_right_x > hud_bar_left_x;
    } else if (hud_state.first_health_max > 0.0f) {
        const float reference_health_max =
            env_float("MINA_XMARK_HUD_BAR_REFERENCE_HEALTH_MAX", 24.0f);
        const float reference_bar_width = hud_bar_width_for_health_max(reference_health_max);
        const float reference_bar_right_x = x - half_w - icon_bar_gap;
        const float actual_bar_width = hud_bar_width_for_health_max(hud_state.first_health_max);
        if (env_bool("MINA_XMARK_HUD_BAR_CENTER_ANCHORED", true)) {
            const float bar_center_x =
                reference_bar_right_x -
                reference_bar_width * 0.5f +
                env_float("MINA_XMARK_HUD_BAR_CENTER_OFFSET_X", 0.0f);
            hud_bar_left_x = bar_center_x - actual_bar_width * 0.5f;
            hud_bar_right_x = bar_center_x + actual_bar_width * 0.5f;
        } else {
            hud_bar_left_x = reference_bar_right_x - reference_bar_width;
            hud_bar_right_x = hud_bar_left_x + actual_bar_width;
        }
        const float bar_offset_x = env_float("MINA_XMARK_HUD_BAR_LEFT_OFFSET_X", 0.0f);
        hud_bar_left_x += bar_offset_x;
        hud_bar_right_x += bar_offset_x;
        hud_bar_anchor_valid = true;
    }

    const bool burn_hud_icon = hud_state.mode == kXMarkHudModeBurn;
    float icon_x = x;
    float icon_y = y;
    float icon_half_w = half_w;
    float icon_half_h = half_h;
    if (env_bool("MINA_XMARK_HUD_RENDER_ANCHOR_RIGHT_OF_BAR", true) &&
        hud_bar_anchor_valid) {
        icon_x =
            hud_bar_right_x +
            icon_bar_gap +
            icon_half_w +
            env_float("MINA_XMARK_HUD_RENDER_ANCHOR_OFFSET_X", 6.0f);
        icon_y = hud_bar_center_y;
    }
    if (burn_hud_icon) {
        icon_x += env_float("MINA_XMARK_HUD_RENDER_BURN_OFFSET_X", 0.0f);
        icon_y += env_float("MINA_XMARK_HUD_RENDER_BURN_OFFSET_Y", 0.0f);
        icon_half_w = env_float("MINA_XMARK_HUD_RENDER_BURN_HALF_W", icon_half_w);
        icon_half_h = env_float("MINA_XMARK_HUD_RENDER_BURN_HALF_H", icon_half_h);
    }

    if (env_bool("MINA_XMARK_HUD_RENDER_CUSTOM_HEALTH_BAR", false) &&
        hud_bar_anchor_valid && hud_state.first_health_max > 0.0f) {
        const float bar_width = std::max(2.0f, hud_bar_right_x - hud_bar_left_x);
        const float bar_center_x = (hud_bar_left_x + hud_bar_right_x) * 0.5f;
        const float outline_half_h = env_float("MINA_XMARK_HUD_BAR_OUTLINE_HALF_H", 4.0f);
        const float inner_half_h = std::max(
            1.0f,
            outline_half_h - env_float("MINA_XMARK_HUD_BAR_BORDER_PX", 1.0f));
        const float ratio = std::max(
            0.0f,
            std::min(1.0f, hud_state.first_health / hud_state.first_health_max));
        emit_hud_quad(bar_center_x, hud_bar_center_y, bar_width * 0.5f, outline_half_h, 3u, -0.003f);
        emit_hud_quad(bar_center_x, hud_bar_center_y, std::max(1.0f, bar_width * 0.5f - 1.0f), inner_half_h, 4u, -0.002f);
        if (ratio > 0.0f) {
            const float inner_width = std::max(0.0f, bar_width - 2.0f);
            const float fill_width = inner_width * ratio;
            const float fill_center_x = hud_bar_left_x + 1.0f + fill_width * 0.5f;
            emit_hud_quad(fill_center_x, hud_bar_center_y, std::max(0.5f, fill_width * 0.5f), inner_half_h, 5u, -0.001f);
        }
    }

    emit_hud_quad(icon_x, icon_y, icon_half_w, icon_half_h, burn_hud_icon ? 1u : 0u, 0.001f);
    if (hud_state.mode == kXMarkHudModeBurn && hud_state.damage_popup_visible) {
        const float damage_half_w = env_float("MINA_XMARK_HUD_DAMAGE_RENDER_HALF_W", 18.0f);
        const float damage_half_h = env_float("MINA_XMARK_HUD_DAMAGE_RENDER_HALF_H", 18.0f);
        float damage_x = env_float("MINA_XMARK_HUD_DAMAGE_RENDER_X", -46.0f);
        if (env_bool("MINA_XMARK_HUD_DAMAGE_ANCHOR_LEFT_OF_BAR", true) &&
            hud_bar_anchor_valid) {
            const float left_gap = env_float("MINA_XMARK_HUD_DAMAGE_LEFT_GAP", 6.0f);
            const float anchor_offset_x = env_float("MINA_XMARK_HUD_DAMAGE_ANCHOR_OFFSET_X", 0.0f);
            damage_x = hud_bar_left_x - left_gap - damage_half_w + anchor_offset_x;
        }
        emit_hud_quad(
            damage_x,
            hud_bar_anchor_valid
                ? hud_bar_center_y + env_float("MINA_XMARK_HUD_DAMAGE_ANCHOR_OFFSET_Y", 0.0f)
                : env_float("MINA_XMARK_HUD_DAMAGE_RENDER_Y", y),
            damage_half_w,
            damage_half_h,
            2u);
    }

    __try {
        g_mina->UpdateGpuBuffer(g_xmark_hud_render_vertex_buffer, verts);
        g_xmark_hud_render_quads = quad;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_xmark_hud_render_backend_ready = false;
        g_xmark_hud_render_quads = 0;
        std::snprintf(g_xmark_hud_render_backend_status, sizeof(g_xmark_hud_render_backend_status), "vertex-update-failed");
    }
}

bool xmark_attack_overlay_render_enabled() {
    return env_bool("MINA_XMARK_ATTACK_OVERLAY_RENDER_ENABLED", false);
}

bool xmark_attack_overlay_asset(
    unsigned int frame,
    unsigned int *width_out,
    unsigned int *height_out,
    const unsigned char **pixels_out) {
    if (width_out) {
        *width_out = 0;
    }
    if (height_out) {
        *height_out = 0;
    }
    if (pixels_out) {
        *pixels_out = nullptr;
    }
    switch (frame) {
    case 0:
        if (width_out) {
            *width_out = xmark_assets::HammerUpF0024Width;
        }
        if (height_out) {
            *height_out = xmark_assets::HammerUpF0024Height;
        }
        if (pixels_out) {
            *pixels_out = xmark_assets::HammerUpF0024Pixels;
        }
        return true;
    case 1:
        if (width_out) {
            *width_out = xmark_assets::HammerUpF0025Width;
        }
        if (height_out) {
            *height_out = xmark_assets::HammerUpF0025Height;
        }
        if (pixels_out) {
            *pixels_out = xmark_assets::HammerUpF0025Pixels;
        }
        return true;
    case 2:
        if (width_out) {
            *width_out = xmark_assets::HammerUpF0026Width;
        }
        if (height_out) {
            *height_out = xmark_assets::HammerUpF0026Height;
        }
        if (pixels_out) {
            *pixels_out = xmark_assets::HammerUpF0026Pixels;
        }
        return true;
    case 3:
        if (width_out) {
            *width_out = xmark_assets::HammerUpF0027Width;
        }
        if (height_out) {
            *height_out = xmark_assets::HammerUpF0027Height;
        }
        if (pixels_out) {
            *pixels_out = xmark_assets::HammerUpF0027Pixels;
        }
        return true;
    case 4:
        if (width_out) {
            *width_out = xmark_assets::HammerUpF0028Width;
        }
        if (height_out) {
            *height_out = xmark_assets::HammerUpF0028Height;
        }
        if (pixels_out) {
            *pixels_out = xmark_assets::HammerUpF0028Pixels;
        }
        return true;
    default:
        return false;
    }
}

MM_Color xmark_attack_overlay_color_from_index(unsigned char value) {
    MM_Color color = xmark_render_color_from_index(value);
    if (color.a) {
        color.a = static_cast<uint8_t>(
            std::min<unsigned int>(255u, env_uint("MINA_XMARK_ATTACK_OVERLAY_ALPHA", 255u)));
    }
    return color;
}

void xmark_attack_overlay_render_copy_asset_frame(
    MM_Color *pixels,
    unsigned int atlas_width,
    unsigned int frame,
    unsigned int width,
    unsigned int height,
    const unsigned char *asset_pixels) {
    if (!pixels || !asset_pixels || frame >= kAttackOverlayRenderFrameCount) {
        return;
    }
    const unsigned int copy_width = std::min(width, kAttackOverlayRenderFrameWidth);
    const unsigned int copy_height = std::min(height, kAttackOverlayRenderFrameHeight);
    const unsigned int pad_x = (kAttackOverlayRenderFrameWidth - copy_width) / 2u;
    const unsigned int pad_y = (kAttackOverlayRenderFrameHeight - copy_height) / 2u;
    for (unsigned int y = 0; y < copy_height; ++y) {
        for (unsigned int x = 0; x < copy_width; ++x) {
            const unsigned char value = asset_pixels[y * width + x];
            pixels[(pad_y + y) * atlas_width + frame * kAttackOverlayRenderFrameWidth + pad_x + x] =
                xmark_attack_overlay_color_from_index(value);
        }
    }
}

void xmark_attack_overlay_render_make_texture_pixels(MM_Color *pixels) {
    if (!pixels) {
        return;
    }
    const unsigned int atlas_width = kAttackOverlayRenderFrameWidth * kAttackOverlayRenderFrameCount;
    std::memset(
        pixels,
        0,
        sizeof(MM_Color) * atlas_width * kAttackOverlayRenderFrameHeight);
    for (unsigned int frame = 0; frame < kAttackOverlayRenderFrameCount; ++frame) {
        unsigned int width = 0;
        unsigned int height = 0;
        const unsigned char *asset_pixels = nullptr;
        if (xmark_attack_overlay_asset(frame, &width, &height, &asset_pixels)) {
            xmark_attack_overlay_render_copy_asset_frame(
                pixels,
                atlas_width,
                frame,
                width,
                height,
                asset_pixels);
        }
    }
}

void xmark_attack_overlay_render_object_draw(void *, MinaModRenderCtx *ctx) {
    if (xmark_runtime_overlays_hidden_for_pause()) {
        return;
    }
    if (env_bool("MINA_XMARK_ATTACK_OVERLAY_DRAW_GAMEPLAY_ONLY", true)) {
        int game_state = -1;
        __try {
            game_state = g_mina ? g_mina->GetCurrentGameState() : -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            game_state = -1;
        }
        if (!gameplay_state_can_spawn_test_enemy(game_state)) {
            return;
        }
    }
    const unsigned int max_stale_ms = env_uint("MINA_XMARK_ATTACK_OVERLAY_DRAW_MAX_STALE_MS", 96);
    const unsigned long long now_ms = GetTickCount64();
    if (!g_attack_overlay_render_last_visible_ms ||
        (max_stale_ms && now_ms > g_attack_overlay_render_last_visible_ms + max_stale_ms)) {
        return;
    }
    if (!g_mina ||
        !ctx ||
        !ctx->drawCall ||
        !ctx->cmdList ||
        !g_attack_overlay_render_backend_ready ||
        !g_attack_overlay_render_quads ||
        !g_attack_overlay_render_vertex_buffer ||
        !g_attack_overlay_render_index_buffer ||
        !g_attack_overlay_render_texture) {
        return;
    }
    ycRenderDrawCall *dc = ctx->drawCall;
    g_mina->RenderDrawCallSetIndexBuffer(dc, g_attack_overlay_render_index_buffer);
    g_mina->RenderDrawCallSetVertexBuffer(dc, g_attack_overlay_render_vertex_buffer);
    g_mina->RenderDrawCallSetTexture(dc, g_attack_overlay_render_texture);
    g_mina->RenderCmdDrawIndexed(ctx->cmdList, dc, g_attack_overlay_render_quads * 6u, 0u);
    ++g_attack_overlay_render_draw_calls;
}

bool xmark_attack_overlay_render_backend_ensure_initialized() {
    if (!xmark_attack_overlay_render_enabled()) {
        g_attack_overlay_render_quads = 0;
        std::snprintf(g_attack_overlay_render_backend_status, sizeof(g_attack_overlay_render_backend_status), "disabled");
        return false;
    }
    if (g_attack_overlay_render_backend_ready) {
        return true;
    }
    g_attack_overlay_render_backend_available = xmark_render_api_available();
    if (!g_attack_overlay_render_backend_available) {
        std::snprintf(
            g_attack_overlay_render_backend_status,
            sizeof(g_attack_overlay_render_backend_status),
            "render-api-unavailable");
        return false;
    }

    const unsigned int atlas_width = kAttackOverlayRenderFrameWidth * kAttackOverlayRenderFrameCount;
    const unsigned int atlas_height = kAttackOverlayRenderFrameHeight;
    MM_Color *pixels = static_cast<MM_Color *>(g_mina->Alloc(sizeof(MM_Color) * atlas_width * atlas_height));
    if (!pixels) {
        std::snprintf(g_attack_overlay_render_backend_status, sizeof(g_attack_overlay_render_backend_status), "texture-alloc-failed");
        return false;
    }
    xmark_attack_overlay_render_make_texture_pixels(pixels);

    bool ok = false;
    __try {
        g_attack_overlay_render_texture = g_mina->CreateTexture(atlas_width, atlas_height);
        if (g_attack_overlay_render_texture) {
            g_mina->UpdateTexture(g_attack_overlay_render_texture, pixels);
            g_attack_overlay_render_vertex_buffer = g_mina->CreateVertexBuffer(kAttackOverlayRenderMaxQuads * 4u);
            g_attack_overlay_render_index_buffer = g_mina->CreateIndexBuffer(kAttackOverlayRenderMaxQuads * 6u);
            uint32_t indices[kAttackOverlayRenderMaxQuads * 6u]{};
            for (unsigned int i = 0; i < kAttackOverlayRenderMaxQuads; ++i) {
                const uint32_t vertex = i * 4u;
                const unsigned int index = i * 6u;
                indices[index + 0] = vertex + 0u;
                indices[index + 1] = vertex + 1u;
                indices[index + 2] = vertex + 2u;
                indices[index + 3] = vertex + 0u;
                indices[index + 4] = vertex + 2u;
                indices[index + 5] = vertex + 3u;
            }
            if (g_attack_overlay_render_index_buffer) {
                g_mina->UpdateGpuBuffer(g_attack_overlay_render_index_buffer, indices);
            }

            char pass_name[64] = "transparentOverlay";
            if (!xmark_read_environment_value(
                    "MINA_XMARK_ATTACK_OVERLAY_RENDER_PASS",
                    pass_name,
                    sizeof(pass_name))) {
                std::snprintf(pass_name, sizeof(pass_name), "transparentOverlay");
            }
            ycRenderPass *pass = g_mina->GetRenderPass(pass_name);
            if (pass) {
                g_attack_overlay_render_object = g_mina->CreateRenderObject(pass, xmark_attack_overlay_render_object_draw, nullptr);
            }
            ok = g_attack_overlay_render_vertex_buffer &&
                g_attack_overlay_render_index_buffer &&
                g_attack_overlay_render_object;
            if (ok) {
                std::snprintf(g_attack_overlay_render_backend_status, sizeof(g_attack_overlay_render_backend_status), "ready:%s", pass_name);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_mina->Free(pixels);
    if (!ok) {
        std::snprintf(g_attack_overlay_render_backend_status, sizeof(g_attack_overlay_render_backend_status), "resource-create-failed");
        return false;
    }

    g_attack_overlay_render_backend_ready = true;
    if (g_mina && !g_attack_overlay_render_backend_logged_ready) {
        g_mina->Log("XMarkBurn attack overlay render backend ready.\n");
        g_attack_overlay_render_backend_logged_ready = true;
    }
    return true;
}

int xmark_attack_overlay_frame_from_state(const XMarkBasicFrameState &state) {
    if (std::strcmp(state.frame, "f0024") == 0) {
        return 0;
    }
    if (std::strcmp(state.frame, "f0025") == 0) {
        return env_bool("MINA_XMARK_ATTACK_OVERLAY_DRAW_MIDDLE", true) ? 1 : -1;
    }
    if (std::strcmp(state.frame, "f0026") == 0) {
        return env_bool("MINA_XMARK_ATTACK_OVERLAY_DRAW_MIDDLE", true) ? 2 : -1;
    }
    if (std::strcmp(state.frame, "f0027") == 0) {
        return env_bool("MINA_XMARK_ATTACK_OVERLAY_DRAW_MIDDLE", true) ? 3 : -1;
    }
    if (std::strcmp(state.frame, "f0028") == 0) {
        return 4;
    }
    if (std::strcmp(state.frame, "f0025-f0027") == 0) {
        return env_bool("MINA_XMARK_ATTACK_OVERLAY_DRAW_MIDDLE", true) ? 2 : -1;
    }
    return -1;
}

void xmark_attack_overlay_render_update_vertices(unsigned long long now_ms) {
    if (xmark_runtime_overlays_hidden_for_pause()) {
        g_attack_overlay_render_quads = 0;
        return;
    }
    if (!xmark_attack_overlay_render_backend_ensure_initialized()) {
        g_attack_overlay_render_quads = 0;
        return;
    }

    XMarkBasicFrameState state{};
    if (!read_modapi_player_basic_frame_state(now_ms, &state)) {
        g_attack_overlay_render_quads = 0;
        return;
    }
    if (state.direction != FacingUp) {
        g_attack_overlay_render_quads = 0;
        return;
    }
    const unsigned int attack_window_ms = env_uint("MINA_XMARK_ATTACK_OVERLAY_ATTACK_WINDOW_MS", 260);
    const bool recent_attack_press =
        g_attack_j_pressed_ms &&
        now_ms >= g_attack_j_pressed_ms &&
        (!attack_window_ms || now_ms <= g_attack_j_pressed_ms + attack_window_ms);
    if (env_bool("MINA_XMARK_ATTACK_OVERLAY_REQUIRE_RECENT_ATTACK_PRESS", true) &&
        !recent_attack_press) {
        g_attack_overlay_render_quads = 0;
        return;
    }
    const int frame = xmark_attack_overlay_frame_from_state(state);
    if (frame < 0 || frame >= static_cast<int>(kAttackOverlayRenderFrameCount)) {
        g_attack_overlay_render_quads = 0;
        return;
    }

    Vec3 center{0.0f, 0.0f, env_float("MINA_XMARK_ATTACK_OVERLAY_RENDER_Z", 0.30f)};
    __try {
        g_mina->PlayerGetPos(&center.x, &center.y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_attack_overlay_render_quads = 0;
        return;
    }
    center.x += env_float("MINA_XMARK_ATTACK_OVERLAY_OFFSET_X", 0.0f);
    center.y += env_float("MINA_XMARK_ATTACK_OVERLAY_OFFSET_Y", 1.20f);
    if (frame == 4) {
        center.x += env_float("MINA_XMARK_ATTACK_OVERLAY_F0028_OFFSET_X", 0.0f);
        center.y += env_float("MINA_XMARK_ATTACK_OVERLAY_F0028_OFFSET_Y", 0.0f);
    }

    const float units_per_pixel = std::max(
        0.001f,
        env_float("MINA_XMARK_ATTACK_OVERLAY_UNITS_PER_PIXEL", xmark_visual_host_units_per_pixel()));
    const float scale = std::max(0.01f, env_float("MINA_XMARK_ATTACK_OVERLAY_SCALE", 1.0f));
    float half_w = static_cast<float>(kAttackOverlayRenderFrameWidth) * units_per_pixel * scale * 0.5f;
    float half_h = static_cast<float>(kAttackOverlayRenderFrameHeight) * units_per_pixel * scale * 0.5f;
    if (env_bool("MINA_XMARK_ATTACK_OVERLAY_PIXEL_SNAP", true)) {
        center.x = std::round(center.x / units_per_pixel) * units_per_pixel;
        center.y = std::round(center.y / units_per_pixel) * units_per_pixel;
        half_w = std::round((half_w * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
        half_h = std::round((half_h * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
    }

    const float u0 = static_cast<float>(frame) / static_cast<float>(kAttackOverlayRenderFrameCount);
    const float u1 = static_cast<float>(frame + 1) / static_cast<float>(kAttackOverlayRenderFrameCount);
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    MM_Vertex_PTC verts[kAttackOverlayRenderMaxQuads * 4u]{};
    verts[0].pos = MM_Vec3{center.x - half_w, center.y - half_h, center.z};
    verts[0].u = u0;
    const bool flip_y = env_bool("MINA_XMARK_ATTACK_OVERLAY_FLIP_Y", true);
    const float v_top = flip_y ? 1.0f : 0.0f;
    const float v_bottom = flip_y ? 0.0f : 1.0f;
    verts[0].v = v_top;
    verts[0].color = white;
    verts[1].pos = MM_Vec3{center.x + half_w, center.y - half_h, center.z};
    verts[1].u = u1;
    verts[1].v = v_top;
    verts[1].color = white;
    verts[2].pos = MM_Vec3{center.x + half_w, center.y + half_h, center.z};
    verts[2].u = u1;
    verts[2].v = v_bottom;
    verts[2].color = white;
    verts[3].pos = MM_Vec3{center.x - half_w, center.y + half_h, center.z};
    verts[3].u = u0;
    verts[3].v = v_bottom;
    verts[3].color = white;

    __try {
        g_mina->UpdateGpuBuffer(g_attack_overlay_render_vertex_buffer, verts);
        g_attack_overlay_render_quads = 1;
        g_attack_overlay_render_last_frame = static_cast<unsigned int>(frame);
        g_attack_overlay_render_last_visible_ms = now_ms;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_attack_overlay_render_backend_ready = false;
        g_attack_overlay_render_quads = 0;
        std::snprintf(g_attack_overlay_render_backend_status, sizeof(g_attack_overlay_render_backend_status), "vertex-update-failed");
    }
}

void xmark_render_object_draw(void *, MinaModRenderCtx *ctx) {
    if (xmark_runtime_overlays_hidden_for_pause()) {
        return;
    }
    if (!g_mina || !ctx || !ctx->drawCall || !ctx->cmdList || !g_xmark_render_backend_ready) {
        return;
    }
    if (env_bool("MINA_XMARK_RENDER_UPDATE_IN_DRAW", false) &&
        env_bool("MINA_XMARK_RENDER_UPDATE_IN_DRAW_ALLOW_GPU_UPLOAD", false)) {
        xmark_render_backend_update_vertices(GetTickCount64());
    }
    const unsigned int quad_count = g_xmark_render_backend_quads;
    ycGpuBuffer *vertex_buffer = g_xmark_render_vertex_buffer;
    if (!quad_count || !vertex_buffer) {
        return;
    }
    ++g_xmark_render_draw_calls;
    g_xmark_render_last_draw_quads = quad_count;
    ycRenderDrawCall *dc = ctx->drawCall;
    g_mina->RenderDrawCallSetIndexBuffer(dc, g_xmark_render_index_buffer);
    g_mina->RenderDrawCallSetVertexBuffer(dc, vertex_buffer);
    g_mina->RenderDrawCallSetTexture(dc, g_xmark_render_texture);
    g_mina->RenderCmdDrawIndexed(ctx->cmdList, dc, quad_count * 6u, 0);
}

bool xmark_render_backend_ensure_initialized() {
    if (!g_xmark_render_backend_enabled) {
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "disabled");
        return false;
    }
    if (g_xmark_render_backend_ready) {
        return true;
    }

    g_xmark_render_backend_available = xmark_render_api_available();
    if (!g_xmark_render_backend_available) {
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "render-api-unavailable");
        if (g_mina && !g_xmark_render_backend_logged_unavailable) {
            g_mina->Log("XMarkBurn render backend dormant: ModAPI render functions are unavailable in this runtime.\n");
            g_xmark_render_backend_logged_unavailable = true;
        }
        return false;
    }

    const unsigned int atlas_width = kXMarkRenderFrameSize * kXMarkRenderFrameCount;
    const unsigned int atlas_height = kXMarkRenderFrameSize;
    MM_Color *pixels = static_cast<MM_Color *>(g_mina->Alloc(sizeof(MM_Color) * atlas_width * atlas_height));
    if (!pixels) {
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "texture-alloc-failed");
        return false;
    }
    xmark_render_make_texture_pixels(pixels);

    bool ok = false;
    __try {
        g_xmark_render_texture = g_mina->CreateTexture(atlas_width, atlas_height);
        if (g_xmark_render_texture) {
            g_mina->UpdateTexture(g_xmark_render_texture, pixels);
            bool vertex_buffers_ok = true;
            for (unsigned int i = 0; i < kXMarkRenderVertexBufferCount; ++i) {
                g_xmark_render_vertex_buffers[i] = g_mina->CreateVertexBuffer(kXMarkRenderMaxQuads * 4u);
                vertex_buffers_ok = vertex_buffers_ok && g_xmark_render_vertex_buffers[i];
            }
            g_xmark_render_active_vertex_buffer = 0;
            g_xmark_render_next_vertex_buffer = 1;
            g_xmark_render_vertex_buffer = g_xmark_render_vertex_buffers[0];
            g_xmark_render_index_buffer = g_mina->CreateIndexBuffer(kXMarkRenderMaxQuads * 6u);
            ok = vertex_buffers_ok && g_xmark_render_index_buffer;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_mina->Free(pixels);
    if (!ok) {
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "resource-create-failed");
        return false;
    }

    uint32_t indices[kXMarkRenderMaxQuads * 6u]{};
    for (unsigned int i = 0; i < kXMarkRenderMaxQuads; ++i) {
        const uint32_t vertex = i * 4u;
        const unsigned int index = i * 6u;
        indices[index + 0] = vertex + 0u;
        indices[index + 1] = vertex + 1u;
        indices[index + 2] = vertex + 2u;
        indices[index + 3] = vertex + 0u;
        indices[index + 4] = vertex + 2u;
        indices[index + 5] = vertex + 3u;
    }

    char pass_name[64] = "transparentOverlay";
    if (!xmark_read_environment_value("MINA_XMARK_RENDER_PASS", pass_name, sizeof(pass_name))) {
        std::snprintf(pass_name, sizeof(pass_name), "transparentOverlay");
    }

    bool object_ok = false;
    __try {
        g_mina->UpdateGpuBuffer(g_xmark_render_index_buffer, indices);
        ycRenderPass *pass = g_mina->GetRenderPass(pass_name);
        if (pass) {
            g_xmark_render_object = g_mina->CreateRenderObject(pass, xmark_render_object_draw, nullptr);
        }
        object_ok = g_xmark_render_object != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        object_ok = false;
    }
    if (!object_ok) {
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "render-object-create-failed");
        return false;
    }

    g_xmark_render_backend_ready = true;
    std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "ready:%s", pass_name);
    if (g_mina && !g_xmark_render_backend_logged_ready) {
        char message[256]{};
        std::snprintf(message, sizeof(message), "XMarkBurn render backend ready on pass '%s'.\n", pass_name);
        g_mina->Log(message);
        g_xmark_render_backend_logged_ready = true;
    }
    return true;
}

void xmark_render_backend_update_vertices(unsigned long long now_ms) {
    if (!xmark_render_backend_ensure_initialized()) {
        g_xmark_render_backend_quads = 0;
        return;
    }
    if (env_bool("MINA_XMARK_RENDER_REFRESH_VISUAL_FOLLOW", true)) {
        refresh_visual_follow_attachments_for_render(now_ms);
    }

    MM_Vertex_PTC vertices[kXMarkRenderMaxQuads * 4u]{};
    const float half_w = xmark_default_render_half_w();
    const float half_h = xmark_default_render_half_h();
    const float z_offset = env_float("MINA_XMARK_RENDER_Z_OFFSET", 0.15f);
    const MM_Color white = xmark_alpha_color(255, 255, 255, 255);
    unsigned int quad = 0;
    g_xmark_render_last_quad_valid = false;
    const auto emit_quad = [&](const Vec3 &center, float quad_half_w, float quad_half_h, float quad_z_offset, unsigned int frame, MM_Color color) -> bool {
        if (quad >= kXMarkRenderMaxQuads || frame >= kXMarkRenderFrameCount) {
            return false;
        }
        Vec3 snapped_center = center;
        if (env_bool("MINA_XMARK_RENDER_PIXEL_SNAP", true)) {
            const float units_per_pixel = xmark_visual_host_units_per_pixel();
            snapped_center.x = std::round(snapped_center.x / units_per_pixel) * units_per_pixel;
            snapped_center.y = std::round(snapped_center.y / units_per_pixel) * units_per_pixel;
            quad_half_w = std::round((quad_half_w * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
            quad_half_h = std::round((quad_half_h * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
        }
        quad_half_w = std::max(0.001f, quad_half_w);
        quad_half_h = std::max(0.001f, quad_half_h);
        const float u0 = static_cast<float>(frame) / static_cast<float>(kXMarkRenderFrameCount);
        const float u1 = static_cast<float>(frame + 1u) / static_cast<float>(kXMarkRenderFrameCount);
        const float v0 = 0.0f;
        const float v1 = 1.0f;
        const float z = snapped_center.z + quad_z_offset;
        const unsigned int base = quad * 4u;

        vertices[base + 0].pos = MM_Vec3{snapped_center.x - quad_half_w, snapped_center.y - quad_half_h, z};
        vertices[base + 0].u = u0;
        vertices[base + 0].v = v0;
        vertices[base + 0].color = color;
        vertices[base + 1].pos = MM_Vec3{snapped_center.x + quad_half_w, snapped_center.y - quad_half_h, z};
        vertices[base + 1].u = u1;
        vertices[base + 1].v = v0;
        vertices[base + 1].color = color;
        vertices[base + 2].pos = MM_Vec3{snapped_center.x + quad_half_w, snapped_center.y + quad_half_h, z};
        vertices[base + 2].u = u1;
        vertices[base + 2].v = v1;
        vertices[base + 2].color = color;
        vertices[base + 3].pos = MM_Vec3{snapped_center.x - quad_half_w, snapped_center.y + quad_half_h, z};
        vertices[base + 3].u = u0;
        vertices[base + 3].v = v1;
        vertices[base + 3].color = color;
        ++quad;
        g_xmark_render_last_quad_x = snapped_center.x;
        g_xmark_render_last_quad_y = snapped_center.y;
        g_xmark_render_last_quad_z = snapped_center.z;
        g_xmark_render_last_quad_half_w = quad_half_w;
        g_xmark_render_last_quad_half_h = quad_half_h;
        g_xmark_render_last_quad_frame = frame;
        g_xmark_render_last_quad_valid = true;
        return true;
    };
    const auto emit_burn_quad = [&](
        const Vec3 &center,
        float quad_half_w,
        float quad_half_h,
        float quad_z_offset,
        unsigned int frame,
        MM_Color color,
        bool flip_y) -> bool {
        if (quad >= kXMarkRenderMaxQuads || frame >= kXMarkRenderFrameCount) {
            return false;
        }
        Vec3 snapped_center = center;
        if (env_bool("MINA_XMARK_RENDER_PIXEL_SNAP", true)) {
            const float units_per_pixel = xmark_visual_host_units_per_pixel();
            snapped_center.x = std::round(snapped_center.x / units_per_pixel) * units_per_pixel;
            snapped_center.y = std::round(snapped_center.y / units_per_pixel) * units_per_pixel;
            quad_half_w = std::round((quad_half_w * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
            quad_half_h = std::round((quad_half_h * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
        }
        quad_half_w = std::max(0.001f, quad_half_w);
        quad_half_h = std::max(0.001f, quad_half_h);
        const float u0 = static_cast<float>(frame) / static_cast<float>(kXMarkRenderFrameCount);
        const float u1 = static_cast<float>(frame + 1u) / static_cast<float>(kXMarkRenderFrameCount);
        const float v_top = flip_y ? 1.0f : 0.0f;
        const float v_bottom = flip_y ? 0.0f : 1.0f;
        const float z = snapped_center.z + quad_z_offset;
        const unsigned int base = quad * 4u;

        vertices[base + 0].pos = MM_Vec3{snapped_center.x - quad_half_w, snapped_center.y - quad_half_h, z};
        vertices[base + 0].u = u0;
        vertices[base + 0].v = v_top;
        vertices[base + 0].color = color;
        vertices[base + 1].pos = MM_Vec3{snapped_center.x + quad_half_w, snapped_center.y - quad_half_h, z};
        vertices[base + 1].u = u1;
        vertices[base + 1].v = v_top;
        vertices[base + 1].color = color;
        vertices[base + 2].pos = MM_Vec3{snapped_center.x + quad_half_w, snapped_center.y + quad_half_h, z};
        vertices[base + 2].u = u1;
        vertices[base + 2].v = v_bottom;
        vertices[base + 2].color = color;
        vertices[base + 3].pos = MM_Vec3{snapped_center.x - quad_half_w, snapped_center.y + quad_half_h, z};
        vertices[base + 3].u = u0;
        vertices[base + 3].v = v_bottom;
        vertices[base + 3].color = color;
        ++quad;
        return true;
    };

    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active ||
            !attachment.render_backend ||
            attachment.marker_debug_draw ||
            !attachment.has_last_position) {
            continue;
        }
        const float attachment_half_w = attachment.render_half_w > 0.0f ? attachment.render_half_w : half_w;
        const float attachment_half_h = attachment.render_half_h > 0.0f ? attachment.render_half_h : half_h;
        const unsigned int marker_frame = xmark_render_frame_for_attachment(attachment, now_ms);
        const MM_Color marker_color = marker_frame == 5u
            ? xmark_alpha_color(255, 255, 255, 80)
            : white;
        emit_quad(
            attachment.last_position,
            attachment_half_w,
            attachment_half_h,
            z_offset,
            marker_frame,
            marker_color);
    }

    g_xmark_hud_world_render_quads = 0;
    if (env_bool("MINA_XMARK_HUD_WORLD_RENDER_ENABLED", false)) {
        const XMarkHudState hud_state = current_xmark_hud_state(now_ms);
        if (hud_state.active_count && hud_state.visible) {
            float player_x = 0.0f;
            float player_y = 0.0f;
            __try {
                g_mina->PlayerGetPos(&player_x, &player_y);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                player_x = 0.0f;
                player_y = 0.0f;
            }
            Vec3 hud_position{
                player_x + env_float("MINA_XMARK_HUD_WORLD_OFFSET_X", 11.5f),
                player_y + env_float("MINA_XMARK_HUD_WORLD_OFFSET_Y", -22.0f),
                env_float("MINA_XMARK_HUD_WORLD_Z", 0.0f)};
            const float hud_half_w = env_float("MINA_XMARK_HUD_WORLD_HALF_W", 0.45f);
            const float hud_half_h = env_float("MINA_XMARK_HUD_WORLD_HALF_H", 0.45f);
            const float hud_z_offset = env_float("MINA_XMARK_HUD_WORLD_Z_OFFSET", 1.4f);
            const unsigned int hud_frame =
                hud_state.mode == kXMarkHudModeBurn ? kXMarkRenderFireFrameBase + 1u : 4u;
            if (emit_quad(hud_position, hud_half_w, hud_half_h, hud_z_offset, hud_frame, white)) {
                g_xmark_hud_world_render_quads = 1u;
                g_xmark_hud_world_render_last_x = hud_position.x;
                g_xmark_hud_world_render_last_y = hud_position.y;
            }
        }
    } else {
        g_xmark_hud_world_render_quads = 0;
    }

    const bool burn_render_backend_enabled =
        env_bool("MINA_XMARK_BURN_RENDER_BACKEND_ENABLED", !xmark_burn_debug_draw_handles_effects());
    if (burn_render_backend_enabled) {
    const float burn_shade_z_offset = env_float("MINA_XMARK_BURN_SHADE_RENDER_Z_OFFSET", 0.26f);
    const float burn_shade_scale_x = std::max(0.1f, env_float("MINA_XMARK_BURN_SHADE_RENDER_SCALE_X", 0.75f));
    const float burn_shade_scale_y = std::max(0.1f, env_float("MINA_XMARK_BURN_SHADE_RENDER_SCALE_Y", 0.75f));
    const float burn_fire_z_offset = env_float("MINA_XMARK_BURN_FIRE_RENDER_Z_OFFSET", 0.22f);
    const float burn_fire_half_w = env_float("MINA_XMARK_BURN_FIRE_RENDER_HALF_WIDTH", 0.46875f);
    const float burn_fire_half_h = env_float("MINA_XMARK_BURN_FIRE_RENDER_HALF_HEIGHT", 0.46875f);
    const float burn_tick_fire_half_w = env_float("MINA_XMARK_BURN_TICK_FIRE_RENDER_HALF_WIDTH", 0.5625f);
    const float burn_tick_fire_half_h = env_float("MINA_XMARK_BURN_TICK_FIRE_RENDER_HALF_HEIGHT", 0.5625f);
    const unsigned int burn_fire_pop_ms = std::max(1u, env_uint("MINA_XMARK_BURN_FIRE_POP_MS", 260));
    const unsigned int burn_death_flame_cycle_ms = std::max(80u, env_uint("MINA_XMARK_BURN_DEATH_FLAME_CYCLE_MS", 420));
    const bool burn_fire_flip_y = env_bool("MINA_XMARK_BURN_FIRE_FLIP_Y", true);
    const unsigned int burn_shade_cycle_ms = std::max(80u, env_uint("MINA_XMARK_BURN_SHADE_CYCLE_MS", 360));
    const unsigned int burn_damage_blink_ms = env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_MS", 160);
    const unsigned int burn_damage_blink_step_ms = std::max(1u, env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_STEP_MS", 55));
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active || !burn.has_last_position || now_ms >= burn.expires_ms) {
            continue;
        }
        Vec3 burn_draw_position = burn.last_position;
        XMarkVisualEnemyHost burn_render_host{};
        bool burn_render_host_current = false;
        bool burn_direct_transform_current = false;
        if (env_bool("MINA_XMARK_BURN_RENDER_PREFER_DIRECT_TRANSFORM", true) &&
            resolve_xmark_burn_direct_follow_position(
                burn,
                now_ms,
                false,
                &burn_draw_position)) {
            burn_direct_transform_current = true;
        }
        if (!burn_direct_transform_current &&
            env_bool("MINA_XMARK_BURN_RENDER_REFRESH_VISUAL_FOLLOW", true) &&
            burn.visual_key) {
            if (visual_enemy_host_by_key(burn.visual_key, &burn_render_host, now_ms)) {
                const unsigned int burn_visual_stale_ms =
                    std::max(32u, env_uint("MINA_XMARK_BURN_RENDER_VISUAL_CENTER_STALE_MS", 900));
                if (!burn_render_host.last_seen_ms ||
                    now_ms <= burn_render_host.last_seen_ms + burn_visual_stale_ms) {
                    burn_render_host_current = true;
                    const Vec3 host_render_position =
                        xmark_burn_visual_host_render_position(burn, burn_render_host, now_ms);
                    commit_xmark_burn_follow_position(burn, host_render_position, now_ms);
                    burn_draw_position = burn.last_position;
                    xmark_visual_host_render_halves(
                        burn_render_host,
                        &burn.render_half_w,
                        &burn.render_half_h);
                }
            }
        }
        const float burn_half_w = burn.render_half_w > 0.0f ? burn.render_half_w : half_w;
        const float burn_half_h = burn.render_half_h > 0.0f ? burn.render_half_h : half_h;
        const float fallback_shade_half_w = env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_W", 0.55f);
        const float fallback_shade_half_h = env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_H", 0.55f);
        float shade_half_w = env_bool("MINA_XMARK_BURN_SHADE_USE_BURN_SIZE", true)
            ? std::max(fallback_shade_half_w, burn_half_w)
            : fallback_shade_half_w;
        float shade_half_h = env_bool("MINA_XMARK_BURN_SHADE_USE_BURN_SIZE", true)
            ? std::max(fallback_shade_half_h, burn_half_h)
            : fallback_shade_half_h;
        Vec3 shade_position = burn_draw_position;
        if (env_bool("MINA_XMARK_BURN_SHADE_USE_VISUAL_HOST_SIZE", true) && burn_render_host_current) {
            xmark_visual_host_sprite_halves(burn_render_host, &shade_half_w, &shade_half_h);
            shade_half_w = std::max(fallback_shade_half_w, shade_half_w);
            shade_half_h = std::max(fallback_shade_half_h, shade_half_h);
        }
        const unsigned long long burn_elapsed = burn.started_ms && now_ms >= burn.started_ms ? now_ms - burn.started_ms : 0ull;
        const unsigned long long cycle_index = burn_elapsed / burn_death_flame_cycle_ms;
        const unsigned long long cycle_phase = burn_elapsed % burn_death_flame_cycle_ms;
        const unsigned long long shade_phase = burn_elapsed % burn_shade_cycle_ms;
        const float shade_wave = static_cast<float>(shade_phase) / static_cast<float>(burn_shade_cycle_ms);
        const float shade_pulse = shade_wave < 0.5f ? shade_wave * 2.0f : (1.0f - shade_wave) * 2.0f;
        unsigned char shade_r = static_cast<unsigned char>(210u + static_cast<unsigned int>(shade_pulse * 45.0f));
        unsigned char shade_g = static_cast<unsigned char>(24u + static_cast<unsigned int>(shade_pulse * 82.0f));
        unsigned char shade_b = static_cast<unsigned char>(6u + static_cast<unsigned int>(shade_pulse * 26.0f));
        unsigned char shade_a = static_cast<unsigned char>(
            std::min<unsigned int>(255u, env_uint("MINA_XMARK_BURN_SHADE_ALPHA", 165u)));
        float shade_center_scale = 1.0f;
        if (env_bool("MINA_XMARK_BURN_SHADE_CENTER_PULSE_ENABLED", true)) {
            const float center_min_scale = std::max(
                0.05f,
                std::min(1.0f, env_float("MINA_XMARK_BURN_SHADE_CENTER_PULSE_MIN_SCALE", 0.38f)));
            const float center_full_at = std::max(
                0.05f,
                std::min(0.98f, env_float("MINA_XMARK_BURN_SHADE_CENTER_PULSE_FULL_AT", 0.68f)));
            const float expand_t = std::min(1.0f, shade_wave / center_full_at);
            const float smooth_expand = expand_t * expand_t * (3.0f - 2.0f * expand_t);
            shade_center_scale = center_min_scale + (1.0f - center_min_scale) * smooth_expand;
            if (shade_wave > center_full_at) {
                const float tail_t = (shade_wave - center_full_at) / std::max(0.001f, 1.0f - center_full_at);
                const float tail_alpha = std::max(
                    0.1f,
                    std::min(1.0f, env_float("MINA_XMARK_BURN_SHADE_CENTER_PULSE_TAIL_ALPHA", 0.68f)));
                const float alpha_scale = 1.0f - tail_t * (1.0f - tail_alpha);
                shade_a = static_cast<unsigned char>(
                    std::max(1.0f, std::min(255.0f, static_cast<float>(shade_a) * alpha_scale)));
            }
        }
        unsigned int shade_frame =
            shade_pulse > 0.55f ? kXMarkRenderBurnDamageFrame : kXMarkRenderBurnAuraFrame;
        if (burn.last_tick_ms && now_ms >= burn.last_tick_ms) {
            const unsigned long long damage_blink_elapsed = now_ms - burn.last_tick_ms;
            if (damage_blink_elapsed < burn_damage_blink_ms) {
                const bool blink_hot = ((damage_blink_elapsed / burn_damage_blink_step_ms) & 1ull) == 0ull;
                shade_frame = kXMarkRenderBurnDamageFrame;
                shade_r = blink_hot ? 255u : 148u;
                shade_g = blink_hot ? 184u : 0u;
                shade_b = blink_hot ? 82u : 18u;
                shade_a = static_cast<unsigned char>(
                    std::min<unsigned int>(255u, env_uint("MINA_XMARK_BURN_DAMAGE_BLINK_ALPHA", 245u)));
            }
        }
        if (env_bool("MINA_XMARK_BURN_SHADE_RENDER_ENABLED", false)) {
            emit_burn_quad(
                shade_position,
                shade_half_w * burn_shade_scale_x * shade_center_scale,
                shade_half_h * burn_shade_scale_y * shade_center_scale,
                burn_shade_z_offset,
                shade_frame,
                xmark_alpha_color(shade_r, shade_g, shade_b, shade_a),
                false);
        }
        const unsigned int burn_fire_after_death_ms =
            env_uint("MINA_XMARK_BURN_FIRE_AFTER_DEATH_MS", 0);
        const bool burn_fire_until_cleanup =
            env_bool("MINA_XMARK_BURN_FIRE_UNTIL_LETHAL_CLEANUP", true);
        const bool burn_fire_can_emit =
            !burn.lethal_written_ms ||
            (burn_fire_until_cleanup && !burn.lethal_cleanup_applied) ||
            now_ms < burn.lethal_written_ms + burn_fire_after_death_ms;
        if (!burn_fire_can_emit) {
            continue;
        }
        const float flame_base_x = env_float("MINA_XMARK_BURN_DEATH_FLAME_OFFSET_X", 0.0f);
        const float flame_base_y = env_float("MINA_XMARK_BURN_DEATH_FLAME_OFFSET_Y", 0.02f);
        const float flame_spread_x = std::max(0.04f, env_float("MINA_XMARK_BURN_DEATH_FLAME_SPREAD_X", burn_half_w * 0.52f));
        const float flame_spread_y = std::max(0.04f, env_float("MINA_XMARK_BURN_DEATH_FLAME_SPREAD_Y", burn_half_h * 0.42f));
        const unsigned int death_flame_count =
            std::max(1u, env_uint("MINA_XMARK_BURN_DEATH_FLAME_COUNT", 6u));
        const bool independent_flames = env_bool("MINA_XMARK_BURN_FIRE_INDEPENDENT", true);
        const float flame_drift_x = burn_half_w * env_float("MINA_XMARK_BURN_FIRE_DRIFT_X", 0.10f);
        const float flame_drift_y = burn_half_h * env_float("MINA_XMARK_BURN_FIRE_DRIFT_Y", 0.14f);
        const float flame_spawn_jitter_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_SPAWN_JITTER_X", burn_half_w * 0.28f));
        const float flame_spawn_jitter_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_SPAWN_JITTER_Y", burn_half_h * 0.20f));
        const float flame_random_side_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_RANDOM_SIDE_X", burn_half_w * 0.42f));
        const float flame_random_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_RANDOM_Y", burn_half_h * 0.22f));
        const float flame_base_angle_deg =
            env_float("MINA_XMARK_BURN_DEATH_FLAME_BASE_ANGLE_DEG", 58.0f);
        const float flame_angle_jitter_deg =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_ANGLE_JITTER_DEG", 52.0f));
        const float flame_extra_angle_jitter_deg =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_EXTRA_ANGLE_JITTER_DEG", 38.0f));
        const float flame_angle_vertical_scale =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_ANGLE_VERTICAL_SCALE", 1.0f));
        const float flame_travel_variance =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_TRAVEL_VARIANCE", 0.28f));
        const float flame_side_fall_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_SIDE_FALL_X", flame_spread_x));
        const float flame_fall_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_FLAME_FALL_Y", burn_half_h * 0.20f));
        const float tick_fire_side_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_SIDE_X", burn_half_w * 0.42f));
        const float tick_fire_fall_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_FALL_Y", burn_half_h * 0.18f));
        const float tick_fire_base_angle_deg =
            env_float("MINA_XMARK_BURN_TICK_FIRE_BASE_ANGLE_DEG", 54.0f);
        const float tick_fire_angle_jitter_deg =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_ANGLE_JITTER_DEG", 58.0f));
        const float tick_fire_extra_angle_jitter_deg =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_EXTRA_ANGLE_JITTER_DEG", 44.0f));
        const float tick_fire_angle_vertical_scale =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_ANGLE_VERTICAL_SCALE", 1.0f));
        const float tick_fire_travel_variance =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_TRAVEL_VARIANCE", 0.30f));
        const float tick_fire_spawn_jitter_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_SPAWN_JITTER_X", burn_half_w * 0.24f));
        const float tick_fire_spawn_jitter_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_SPAWN_JITTER_Y", burn_half_h * 0.18f));
        const float tick_fire_random_side_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_RANDOM_SIDE_X", burn_half_w * 0.36f));
        const float tick_fire_random_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_RANDOM_Y", burn_half_h * 0.18f));
        const float tick_fire_shake_x =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_SHAKE_X", burn_half_w * 0.08f));
        const float tick_fire_shake_y =
            std::max(0.0f, env_float("MINA_XMARK_BURN_TICK_FIRE_SHAKE_Y", burn_half_h * 0.05f));
        for (unsigned int flame_index = 0; flame_index < death_flame_count; ++flame_index) {
            Vec3 flame_position = burn_draw_position;
            const unsigned long long staggered_phase =
                (cycle_phase + (burn_death_flame_cycle_ms * flame_index) / death_flame_count) % burn_death_flame_cycle_ms;
            const float phase = static_cast<float>(staggered_phase) / static_cast<float>(burn_death_flame_cycle_ms);
            const uint32_t seed = xmark_burn_seed(
                burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key),
                burn.started_ms,
                static_cast<unsigned int>(cycle_index),
                flame_index);
            const float disperse = 1.0f - ((1.0f - phase) * (1.0f - phase));
            const float spawn_jitter_x = xmark_noise_signed(seed + 1u) * flame_spawn_jitter_x;
            const float spawn_jitter_y = xmark_noise_signed(seed + 2u) * flame_spawn_jitter_y;
            const float base_turn =
                static_cast<float>(flame_index) / static_cast<float>(death_flame_count);
            const float cycle_turn =
                static_cast<float>((cycle_index * 37ull) % 101ull) / 101.0f;
            const float angle_deg =
                flame_base_angle_deg +
                (base_turn + cycle_turn * 0.31f) * 360.0f +
                xmark_noise_signed(seed + 3u) * flame_angle_jitter_deg +
                xmark_noise_signed(seed + 8u) * flame_extra_angle_jitter_deg;
            const float angle_rad = angle_deg * 0.017453292519943295f;
            const float travel_scale_x = 1.0f + xmark_noise_signed(seed + 9u) * flame_travel_variance;
            const float travel_scale_y = 1.0f + xmark_noise_signed(seed + 10u) * flame_travel_variance;
            const float travel_x =
                (flame_side_fall_x + xmark_noise01(seed + 4u) * flame_random_side_x) *
                std::max(0.1f, travel_scale_x);
            const float travel_y =
                (flame_fall_y + xmark_noise01(seed + 5u) * flame_random_y) *
                std::max(0.1f, travel_scale_y);
            const float trajectory_x = std::cos(angle_rad) * travel_x;
            const float trajectory_y =
                std::sin(angle_rad) * travel_y * flame_angle_vertical_scale +
                xmark_noise_signed(seed + 6u) * flame_random_y * 0.35f;
            flame_position.x +=
                flame_base_x +
                spawn_jitter_x +
                std::cos(angle_rad) * flame_spread_x * 0.08f +
                disperse * trajectory_x;
            flame_position.y +=
                flame_base_y +
                spawn_jitter_y +
                std::sin(angle_rad) * flame_spread_y * 0.08f +
                disperse * trajectory_y;
            if (independent_flames) {
                const float wave = std::sin((phase + static_cast<float>(flame_index) * 0.37f) * 6.2831853f);
                const float counter_wave = std::sin((phase + static_cast<float>(flame_index) * 0.19f + 0.25f) * 6.2831853f);
                flame_position.x += wave * flame_drift_x;
                flame_position.y += counter_wave * flame_drift_y;
            }
            const unsigned int fire_frame = std::min<unsigned int>(
                kXMarkRenderFireFrameCount - 1u,
                static_cast<unsigned int>((staggered_phase * kXMarkRenderFireFrameCount) / burn_death_flame_cycle_ms));
            emit_burn_quad(
                flame_position,
                burn_fire_half_w,
                burn_fire_half_h,
                burn_fire_z_offset,
                kXMarkRenderFireFrameBase + fire_frame,
                xmark_alpha_color(255, 184, 184, 255),
                burn_fire_flip_y);
        }

        if (burn.last_tick_ms && now_ms >= burn.last_tick_ms) {
            const unsigned long long tick_elapsed = now_ms - burn.last_tick_ms;
            if (tick_elapsed < burn_fire_pop_ms) {
                const float tick_phase = static_cast<float>(tick_elapsed) / static_cast<float>(burn_fire_pop_ms);
                const unsigned int fire_frame = std::min<unsigned int>(
                    kXMarkRenderFireFrameCount - 1u,
                    static_cast<unsigned int>((tick_elapsed * kXMarkRenderFireFrameCount) / burn_fire_pop_ms));
                const bool split_tick_flames = env_bool("MINA_XMARK_BURN_TICK_FIRE_SPLIT", true);
                const unsigned int tick_flame_count = split_tick_flames
                    ? std::max(2u, env_uint("MINA_XMARK_BURN_TICK_FIRE_COUNT", 3u))
                    : 1u;
                for (unsigned int tick_flame_index = 0; tick_flame_index < tick_flame_count; ++tick_flame_index) {
                    const uint32_t seed = xmark_burn_seed(
                        burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key),
                        burn.started_ms,
                        burn.tick_count,
                        tick_flame_index + 17u);
                    const float shake_phase =
                        (tick_phase + static_cast<float>(tick_flame_index) * 0.37f) * 6.2831853f;
                    Vec3 fire_position = burn_draw_position;
                    fire_position.x += env_float("MINA_XMARK_BURN_FIRE_RENDER_OFFSET_X", 0.0f);
                    fire_position.y += env_float("MINA_XMARK_BURN_FIRE_RENDER_OFFSET_Y", -0.10f);
                    const float tick_burst = 1.0f - ((1.0f - tick_phase) * (1.0f - tick_phase));
                    fire_position.x += xmark_noise_signed(seed + 2u) * tick_fire_spawn_jitter_x;
                    fire_position.y += xmark_noise_signed(seed + 3u) * tick_fire_spawn_jitter_y;
                    const float base_turn = tick_flame_count > 0
                        ? static_cast<float>(tick_flame_index) / static_cast<float>(tick_flame_count)
                        : 0.0f;
                    const float tick_cycle_turn =
                        static_cast<float>((burn.tick_count * 29u) % 97u) / 97.0f;
                    const float tick_angle_deg =
                        tick_fire_base_angle_deg +
                        (base_turn + tick_cycle_turn * 0.37f) * 360.0f +
                        xmark_noise_signed(seed + 4u) * tick_fire_angle_jitter_deg +
                        xmark_noise_signed(seed + 8u) * tick_fire_extra_angle_jitter_deg;
                    const float tick_angle_rad = tick_angle_deg * 0.017453292519943295f;
                    const float tick_travel_scale_x = 1.0f + xmark_noise_signed(seed + 9u) * tick_fire_travel_variance;
                    const float tick_travel_scale_y = 1.0f + xmark_noise_signed(seed + 10u) * tick_fire_travel_variance;
                    const float tick_travel_x =
                        (tick_fire_side_x + xmark_noise01(seed + 5u) * tick_fire_random_side_x) *
                        std::max(0.1f, tick_travel_scale_x);
                    const float tick_travel_y =
                        (tick_fire_fall_y + xmark_noise01(seed + 6u) * tick_fire_random_y) *
                        std::max(0.1f, tick_travel_scale_y);
                    fire_position.x +=
                        tick_burst * std::cos(tick_angle_rad) * tick_travel_x;
                    fire_position.y +=
                        tick_burst *
                        (std::sin(tick_angle_rad) * tick_travel_y * tick_fire_angle_vertical_scale +
                         xmark_noise_signed(seed + 7u) * tick_fire_random_y * 0.35f);
                    fire_position.x += std::sin(shake_phase) * tick_fire_shake_x;
                    fire_position.y += std::cos(shake_phase * 1.31f) * tick_fire_shake_y;
                    emit_burn_quad(
                        fire_position,
                        burn_tick_fire_half_w,
                        burn_tick_fire_half_h,
                        burn_fire_z_offset,
                        kXMarkRenderFireFrameBase + fire_frame,
                        xmark_alpha_color(255, 210, 210, 255),
                        burn_fire_flip_y);
                }
            }
        }
    }

    const unsigned int death_burst_count =
        std::max(1u, env_uint("MINA_XMARK_BURN_DEATH_BURST_COUNT", 9u));
    const float death_burst_jitter_x =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_SPAWN_JITTER_X", 0.16f));
    const float death_burst_jitter_y =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_SPAWN_JITTER_Y", 0.12f));
    const float death_burst_travel_x =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_TRAVEL_X", 1.05f));
    const float death_burst_travel_y =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_TRAVEL_Y", 0.95f));
    const float death_burst_angle_jitter_deg =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_ANGLE_JITTER_DEG", 74.0f));
    const float death_burst_travel_variance =
        std::max(0.0f, env_float("MINA_XMARK_BURN_DEATH_BURST_TRAVEL_VARIANCE", 0.38f));
    for (XMarkBurnDeathBurst &burst : g_xmark_burn_death_bursts) {
        if (!burst.active) {
            continue;
        }
        if (now_ms >= burst.expires_ms || burst.expires_ms <= burst.started_ms) {
            burst.active = false;
            continue;
        }
        const unsigned long long elapsed_ms = now_ms >= burst.started_ms ? now_ms - burst.started_ms : 0ull;
        const unsigned long long duration_ms = burst.expires_ms - burst.started_ms;
        const float phase = std::min(1.0f, static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms));
        const float burst_out = 1.0f - ((1.0f - phase) * (1.0f - phase));
        const float fade = std::max(0.0f, 1.0f - phase);
        const unsigned char alpha = static_cast<unsigned char>(
            std::min<unsigned int>(255u, static_cast<unsigned int>(fade * 255.0f)));
        if (alpha < 8u) {
            continue;
        }

        for (unsigned int burst_index = 0; burst_index < death_burst_count; ++burst_index) {
            const uint32_t seed = xmark_burn_seed(
                burst.seed_target,
                burst.started_ms,
                burst_index + 113u,
                static_cast<unsigned int>(elapsed_ms / 16ull));
            const float base_turn = static_cast<float>(burst_index) / static_cast<float>(death_burst_count);
            const float angle_deg =
                base_turn * 360.0f +
                xmark_noise_signed(seed + 1u) * death_burst_angle_jitter_deg;
            const float angle_rad = angle_deg * 0.017453292519943295f;
            const float travel_scale_x =
                std::max(0.1f, 1.0f + xmark_noise_signed(seed + 2u) * death_burst_travel_variance);
            const float travel_scale_y =
                std::max(0.1f, 1.0f + xmark_noise_signed(seed + 3u) * death_burst_travel_variance);
            const float local_travel_x =
                (death_burst_travel_x + burst.half_w * 0.28f) * travel_scale_x;
            const float local_travel_y =
                (death_burst_travel_y + burst.half_h * 0.24f) * travel_scale_y;
            const float shake_phase =
                (phase + static_cast<float>(burst_index) * 0.23f) * 6.2831853f;
            Vec3 burst_position = burst.position;
            burst_position.x += xmark_noise_signed(seed + 4u) * death_burst_jitter_x;
            burst_position.y += xmark_noise_signed(seed + 5u) * death_burst_jitter_y;
            burst_position.x += std::cos(angle_rad) * local_travel_x * burst_out;
            burst_position.y += std::sin(angle_rad) * local_travel_y * burst_out;
            burst_position.x += std::sin(shake_phase) * burst.half_w * 0.08f;
            burst_position.y += std::cos(shake_phase * 1.41f) * burst.half_h * 0.07f;

            const unsigned int fire_frame = std::min<unsigned int>(
                kXMarkRenderFireFrameCount - 1u,
                static_cast<unsigned int>((elapsed_ms * kXMarkRenderFireFrameCount) / duration_ms));
            emit_burn_quad(
                burst_position,
                burn_tick_fire_half_w,
                burn_tick_fire_half_h,
                burn_fire_z_offset,
                kXMarkRenderFireFrameBase + fire_frame,
                xmark_alpha_color(255, 220, 190, alpha),
                burn_fire_flip_y);
        }
    }

    }

    const float f0029_half_w = env_float("MINA_XMARK_F0029_RENDER_HALF_WIDTH", 0.8f);
    const float f0029_half_h = env_float("MINA_XMARK_F0029_RENDER_HALF_HEIGHT", 0.8f);
    const float f0029_z_offset = env_float("MINA_XMARK_F0029_RENDER_Z_OFFSET", 0.9f);
    const unsigned int f0029_frame_ms = std::max(16u, env_uint("MINA_XMARK_F0029_RENDER_FRAME_MS", 30));
    for (F0029RenderEffect &effect : g_f0029_render_effects) {
        if (!effect.active || !effect.started_ms) {
            continue;
        }
        const unsigned long long elapsed_ms = now_ms >= effect.started_ms ? now_ms - effect.started_ms : 0ull;
        const unsigned long long hard_expires_ms = effect.started_ms + effect.duration_ms;
        const unsigned long long effective_expires_ms =
            effect.side_basic_bound && effect.soft_expires_ms
                ? std::min(hard_expires_ms, effect.soft_expires_ms)
                : hard_expires_ms;
        const bool soft_expired =
            effect.side_basic_bound &&
            effect.soft_expires_ms &&
            now_ms >= effect.soft_expires_ms;
        if (now_ms >= hard_expires_ms || soft_expired) {
            effect.active = false;
            continue;
        }
        const unsigned int local_frame = std::min<unsigned int>(
            kXMarkRenderF0029FrameCount - 1u,
            static_cast<unsigned int>(elapsed_ms / f0029_frame_ms));
        const unsigned int base_frame =
            effect.direction == FacingLeft ? kXMarkRenderF0029LeftFrameBase : kXMarkRenderF0029RightFrameBase;
        const unsigned long long effective_duration_ms =
            effective_expires_ms > effect.started_ms
                ? effective_expires_ms - effect.started_ms
                : 1ull;
        const float life_phase = effective_duration_ms
            ? std::min(1.0f, static_cast<float>(elapsed_ms) / static_cast<float>(effective_duration_ms))
            : 1.0f;
        const float travel_phase =
            life_phase * life_phase * (3.0f - (2.0f * life_phase));
        Vec3 draw_position = effect.position;
        const float travel_distance =
            env_float("MINA_XMARK_F0029_RENDER_TRAVEL_DISTANCE", 1.25f) * travel_phase;
        draw_position.x += effect.direction == FacingLeft ? -travel_distance : travel_distance;
        const float fade_start = std::max(
            0.0f,
            std::min(0.95f, env_float("MINA_XMARK_F0029_RENDER_FADE_START", 0.58f)));
        const float fade_phase = life_phase > fade_start
            ? std::min(1.0f, (life_phase - fade_start) / std::max(0.01f, 1.0f - fade_start))
            : 0.0f;
        const unsigned char alpha = static_cast<unsigned char>(
            std::max(0.0f, 255.0f * (1.0f - fade_phase)));
        const float blink_start = std::max(
            0.0f,
            std::min(0.95f, env_float("MINA_XMARK_F0029_RENDER_BLINK_START", 0.66f)));
        const unsigned int blink_ms =
            std::max(16u, env_uint("MINA_XMARK_F0029_RENDER_BLINK_MS", 38));
        if (env_bool("MINA_XMARK_F0029_RENDER_BLINK_ENABLED", true) &&
            life_phase >= blink_start) {
            const unsigned long long blink_started_ms = static_cast<unsigned long long>(
                static_cast<float>(effective_duration_ms) * blink_start);
            if ((((elapsed_ms - std::min(elapsed_ms, blink_started_ms)) / blink_ms) & 1ull) != 0ull) {
                continue;
            }
        }
        const float shrink = std::max(
            0.0f,
            std::min(0.75f, env_float("MINA_XMARK_F0029_RENDER_FADE_SHRINK", 0.25f)));
        const float render_scale = 1.0f - (shrink * fade_phase);
        emit_quad(
            draw_position,
            f0029_half_w * render_scale,
            f0029_half_h * render_scale,
            f0029_z_offset,
            base_frame + local_frame,
            xmark_alpha_color(255, 255, 255, alpha));
    }

    const float vertical_smear_half_w = env_float("MINA_XMARK_VERTICAL_SMEAR_RENDER_HALF_WIDTH", 0.8f);
    const float vertical_smear_half_h = env_float("MINA_XMARK_VERTICAL_SMEAR_RENDER_HALF_HEIGHT", 0.8f);
    const float vertical_smear_z_offset = env_float("MINA_XMARK_VERTICAL_SMEAR_RENDER_Z_OFFSET", 0.9f);
    const unsigned int vertical_smear_frame_ms =
        std::max(16u, env_uint("MINA_XMARK_VERTICAL_SMEAR_RENDER_FRAME_MS", 34));
    for (VerticalSmearRenderEffect &effect : g_vertical_smear_render_effects) {
        if (!effect.active || !effect.started_ms) {
            continue;
        }
        const unsigned long long elapsed_ms = now_ms >= effect.started_ms ? now_ms - effect.started_ms : 0ull;
        if (now_ms >= effect.started_ms + effect.duration_ms ||
            (effect.soft_expires_ms && now_ms >= effect.soft_expires_ms)) {
            effect.active = false;
            continue;
        }
        const unsigned int local_frame = std::min<unsigned int>(
            kXMarkRenderVerticalSmearFrameCount - 1u,
            static_cast<unsigned int>(elapsed_ms / vertical_smear_frame_ms));
        const unsigned int base_frame = effect.direction == FacingUp
            ? kXMarkRenderVerticalSmearUpFrameBase
            : kXMarkRenderVerticalSmearDownFrameBase;
        emit_quad(
            effect.position,
            vertical_smear_half_w,
            vertical_smear_half_h,
            vertical_smear_z_offset,
            base_frame + local_frame,
            white);
    }

    if (env_bool("MINA_XMARK_RENDER_SCREEN_PROBE", false)) {
        const Vec3 probe_center{
            env_float("MINA_XMARK_RENDER_SCREEN_PROBE_X", 0.0f),
            env_float("MINA_XMARK_RENDER_SCREEN_PROBE_Y", 0.0f),
            env_float("MINA_XMARK_RENDER_SCREEN_PROBE_Z", 0.5f)};
        emit_quad(
            probe_center,
            env_float("MINA_XMARK_RENDER_SCREEN_PROBE_HALF_W", 48.0f),
            env_float("MINA_XMARK_RENDER_SCREEN_PROBE_HALF_H", 48.0f),
            0.0f,
            env_uint("MINA_XMARK_RENDER_SCREEN_PROBE_FRAME", 4u),
            white);
    }

    if (quad == 0 && env_bool("MINA_XMARK_RENDER_SKIP_EMPTY_UPLOAD", true)) {
        g_xmark_render_backend_quads = 0;
        g_xmark_render_last_quad_valid = false;
        return;
    }

    unsigned int write_buffer_index = 0;
    if (!env_bool("MINA_XMARK_RENDER_SINGLE_VERTEX_BUFFER", false)) {
        write_buffer_index = g_xmark_render_next_vertex_buffer % kXMarkRenderVertexBufferCount;
        if (write_buffer_index == g_xmark_render_active_vertex_buffer) {
            write_buffer_index = (write_buffer_index + 1u) % kXMarkRenderVertexBufferCount;
        }
    }
    ycGpuBuffer *write_buffer = g_xmark_render_vertex_buffers[write_buffer_index];
    if (!write_buffer) {
        g_xmark_render_backend_ready = false;
        g_xmark_render_backend_quads = 0;
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "vertex-buffer-missing");
        return;
    }

    __try {
        g_mina->UpdateGpuBuffer(write_buffer, vertices);
        g_xmark_render_active_vertex_buffer = write_buffer_index;
        if (env_bool("MINA_XMARK_RENDER_SINGLE_VERTEX_BUFFER", false)) {
            g_xmark_render_next_vertex_buffer = 0;
        } else {
            g_xmark_render_next_vertex_buffer = (write_buffer_index + 1u) % kXMarkRenderVertexBufferCount;
            if (g_xmark_render_next_vertex_buffer == g_xmark_render_active_vertex_buffer) {
                g_xmark_render_next_vertex_buffer = (g_xmark_render_next_vertex_buffer + 1u) % kXMarkRenderVertexBufferCount;
            }
        }
        g_xmark_render_vertex_buffer = write_buffer;
        g_xmark_render_backend_quads = quad;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_xmark_render_backend_ready = false;
        g_xmark_render_backend_quads = 0;
        std::snprintf(g_xmark_render_backend_status, sizeof(g_xmark_render_backend_status), "vertex-update-failed");
    }
}

