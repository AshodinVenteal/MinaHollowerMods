bool runtime_target_from_basic_baseline(XMarkHealthBaseline &baseline, const Vec3 &player_api, XMarkRuntimeTarget *target_out) {
    if (!target_out || !baseline.entity || pointer_is_rejected_runtime_target(baseline.entity) ||
        !probable_heap_object(baseline.entity)) {
        return false;
    }

    Vec3 anchor{};
    unsigned int anchor_offset = baseline.anchor_offset;
    bool resolved_anchor = anchor_offset &&
        runtime_object_anchor_at_offset(baseline.entity, anchor_offset, &anchor);
    if (!resolved_anchor) {
        resolved_anchor = runtime_object_anchor(baseline.entity, player_api, &anchor, &anchor_offset);
    }
    if (!resolved_anchor) {
        return false;
    }
    baseline.anchor_offset = anchor_offset;
    baseline.anchor = anchor;

    float health_value = 0.0f;
    float health_max = 0.0f;
    bool resolved_health = read_health_at_offset(
        baseline.entity,
        baseline.health_offset,
        baseline.health_kind,
        &health_value,
        &health_max);
    if (!resolved_health) {
        unsigned int health_offset = 0;
        unsigned int health_kind = 0;
        resolved_health = runtime_target_healthlike(
            baseline.entity,
            &health_offset,
            &health_value,
            &health_max,
            &health_kind);
        if (resolved_health) {
            baseline.health_offset = health_offset;
            baseline.health_kind = health_kind;
        }
    }
    if (!resolved_health) {
        return false;
    }

    const Vec3 official_base = official_spawn_position();
    const float dx = anchor.x - player_api.x;
    const float dy = anchor.y - player_api.y;
    XMarkRuntimeTarget target{};
    target.entity = baseline.entity;
    target.anchor = anchor;
    target.position.x = anchor.x + (official_base.x - player_api.x);
    target.position.y = anchor.y + (official_base.y - player_api.y);
    target.position.z = official_base.z;
    target.distance_sq = (dx * dx) + (dy * dy);
    target.anchor_offset = baseline.anchor_offset;
    target.health_offset = baseline.health_offset;
    target.health_value = health_value;
    target.health_max = health_max;
    target.health_kind = baseline.health_kind;
    target.health_like = true;
    *target_out = target;
    return true;
}

bool runtime_target_from_test_enemy(int direction, XMarkRuntimeTarget *target_out) {
    if (!target_out || !g_last_test_enemy_entity || !probable_heap_object(g_last_test_enemy_entity)) {
        return false;
    }

    if (env_bool("MINA_XMARK_TEST_ENEMY_PREFER_CHILD", true)) {
        XMarkRuntimeTarget child_target{};
        if (runtime_target_from_entity_pointer(
                g_last_test_enemy_entity,
                g_last_test_enemy_position,
                direction,
                &child_target)) {
            *target_out = child_target;
            return true;
        }
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    const Vec3 official_base = official_spawn_position();
    player_api.z = official_base.z;

    Vec3 anchor{};
    unsigned int anchor_offset = 0;
    bool used_static_fallback = false;
    if (!runtime_object_anchor(
            g_last_test_enemy_entity,
            player_api,
            &anchor,
            &anchor_offset,
            env_float("MINA_XMARK_TEST_ENEMY_TARGET_MIN_RADIUS", 0.1f),
            env_float("MINA_XMARK_TEST_ENEMY_TARGET_MAX_RADIUS", 160.0f))) {
        if (runtime_object_anchor_scan_near_position(
                g_last_test_enemy_entity,
                player_api,
                g_last_test_enemy_position,
                &anchor,
                &anchor_offset,
                "test-enemy-contact")) {
            used_static_fallback = false;
        } else if (env_bool("MINA_XMARK_TEST_ENEMY_CONTACT_PROMOTE_SCAN", false)) {
        XMarkRuntimeTarget targets[128]{};
        unsigned int reads = 0;
        Vec3 scan_player_api{0.0f, 0.0f, 0.0f};
        const unsigned int count = collect_runtime_targets(
            targets,
            static_cast<unsigned int>(sizeof(targets) / sizeof(targets[0])),
            &reads,
            &scan_player_api,
            nullptr);

        int best_index = -1;
        float best_score = FLT_MAX;
        const float max_radius = env_float("MINA_XMARK_TEST_ENEMY_CONTACT_RESOLVE_RADIUS", 24.0f);
        const float max_radius_sq = max_radius * max_radius;
        for (unsigned int i = 0; i < count; ++i) {
            XMarkRuntimeTarget &candidate = targets[i];
            const float dx_spawn = candidate.position.x - g_last_test_enemy_position.x;
            const float dy_spawn = candidate.position.y - g_last_test_enemy_position.y;
            const float spawn_dist_sq = (dx_spawn * dx_spawn) + (dy_spawn * dy_spawn);
            if (spawn_dist_sq > max_radius_sq) {
                continue;
            }
            runtime_target_direction_delta_for_direction(
                candidate,
                scan_player_api,
                direction,
                &candidate.forward_delta,
                &candidate.lateral_delta);
            candidate.facing_match = runtime_target_in_basic_contact_band(candidate, scan_player_api, direction);
            float score = spawn_dist_sq + (candidate.lateral_delta * candidate.lateral_delta * 0.35f);
            if (!candidate.health_like) {
                score += env_float("MINA_XMARK_TEST_ENEMY_CONTACT_NO_HEALTH_PENALTY", 250.0f);
            }
            if (!candidate.facing_match) {
                score += env_float("MINA_XMARK_TEST_ENEMY_CONTACT_OUT_OF_BAND_PENALTY", 80.0f);
            }
            if (score < best_score) {
                best_score = score;
                best_index = static_cast<int>(i);
            }
        }

        if (best_index >= 0) {
            XMarkRuntimeTarget target = targets[best_index];
            g_last_test_enemy_entity = target.entity;
            g_last_test_enemy_resolved_ms = GetTickCount64();
            target.score = best_score;
            if (g_mina && env_bool("MINA_XMARK_TEST_ENEMY_CONTACT_RESOLVE_LOG", true)) {
                char message[640]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn test enemy contact promoted runtime target=0x%p healthLike=%u anchorOffset=0x%X pos=(%.3f, %.3f, %.3f) score=%.3f count=%u reads=%u\n",
                    reinterpret_cast<void *>(target.entity),
                    target.health_like ? 1u : 0u,
                    target.anchor_offset,
                    static_cast<double>(target.position.x),
                    static_cast<double>(target.position.y),
                    static_cast<double>(target.position.z),
                    static_cast<double>(target.score),
                    count,
                    reads);
                g_mina->Log(message);
            }
            *target_out = target;
            return true;
        }

        if (!env_bool("MINA_XMARK_TEST_ENEMY_STATIC_POSITION_FALLBACK", true)) {
            return false;
        }
        anchor = g_last_test_enemy_position;
        anchor_offset = 0;
        used_static_fallback = true;
        } else {
            return false;
        }
    }

    XMarkRuntimeTarget target{};
    target.entity = g_last_test_enemy_entity;
    target.source_base = g_last_test_enemy_entity;
    target.source_offset = 0;
    target.anchor = anchor;
    target.anchor_offset = anchor_offset;
    target.position = used_static_fallback ? g_last_test_enemy_position : runtime_anchor_to_spawn_position(anchor);
    if (used_static_fallback) {
        target.position.x += env_float("MINA_XMARK_TEST_ENEMY_STATIC_MARK_OFFSET_X", 3.0f);
        target.position.y += env_float("MINA_XMARK_TEST_ENEMY_STATIC_MARK_OFFSET_Y", 2.7f);
        target.position.z += env_float("MINA_XMARK_TEST_ENEMY_STATIC_MARK_OFFSET_Z", 0.0f);
    } else {
        target.position.x += env_float("MINA_XMARK_TEST_ENEMY_ANCHOR_MARK_OFFSET_X", 0.0f);
        target.position.y += env_float("MINA_XMARK_TEST_ENEMY_ANCHOR_MARK_OFFSET_Y", 0.0f);
        target.position.z += env_float("MINA_XMARK_TEST_ENEMY_ANCHOR_MARK_OFFSET_Z", 0.0f);
    }
    const float dx = anchor.x - player_api.x;
    const float dy = anchor.y - player_api.y;
    target.distance_sq = (dx * dx) + (dy * dy);
    runtime_target_healthlike(
        target.entity,
        &target.health_offset,
        &target.health_value,
        &target.health_max,
        &target.health_kind);
    target.health_like = target.health_max > 0.0f;
    runtime_target_direction_delta_for_direction(target, player_api, direction, &target.forward_delta, &target.lateral_delta);
    target.facing_match = used_static_fallback ? true : runtime_target_in_basic_contact_band(target, player_api, direction);
    target.score = (target.lateral_delta * target.lateral_delta) + (target.forward_delta * target.forward_delta * 0.1f);
    *target_out = target;
    return true;
}

void maybe_mark_test_enemy_on_basic_contact(unsigned long long now_ms, int direction, const char *source) {
    if (!env_bool("MINA_XMARK_TEST_ENEMY_TAG_ON_BASIC_CONTACT", false)) {
        return;
    }

    XMarkRuntimeTarget target{};
    if (!runtime_target_from_test_enemy(direction, &target)) {
        if (g_mina && g_last_test_enemy_entity) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn test enemy contact tag missed: source=%s target=0x%p anchor-unresolved.\n",
                source ? source : "smear",
                reinterpret_cast<void *>(g_last_test_enemy_entity));
            g_mina->Log(message);
        }
        return;
    }

    if (env_bool("MINA_XMARK_TEST_ENEMY_TAG_REQUIRE_HEALTH", true) && !target.health_like) {
        if (g_mina && env_bool("MINA_XMARK_TEST_ENEMY_CONTACT_RESOLVE_LOG", true)) {
            g_mina->Log("XMarkBurn test enemy contact tag rejected: resolved target was not health-like.\n");
        }
        return;
    }
    if (env_bool("MINA_XMARK_TEST_ENEMY_TAG_REQUIRE_CONTACT_BAND", true) && !target.facing_match) {
        if (g_mina && env_bool("MINA_XMARK_TEST_ENEMY_CONTACT_RESOLVE_LOG", true)) {
            char rejected_message[256]{};
            std::snprintf(
                rejected_message,
                sizeof(rejected_message),
                "XMarkBurn test enemy contact tag rejected: target outside basic contact band forward=%.3f lateral=%.3f.\n",
                static_cast<double>(target.forward_delta),
                static_cast<double>(target.lateral_delta));
            g_mina->Log(rejected_message);
        }
        return;
    }

    if (g_mina) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn test enemy contact tag source=%s dir=%s target=0x%p healthLike=%u health=%.3f/%.3f anchorOffset=0x%X staticFallback=%u pos=(%.3f, %.3f, %.3f) forward=%.3f lateral=%.3f tick=%llu\n",
            source ? source : "smear",
            direction_name(direction),
            reinterpret_cast<void *>(target.entity),
            target.health_like ? 1u : 0u,
            static_cast<double>(target.health_value),
            static_cast<double>(target.health_max),
            target.anchor_offset,
            target.anchor_offset == 0 ? 1u : 0u,
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            static_cast<double>(target.forward_delta),
            static_cast<double>(target.lateral_delta),
            now_ms);
        g_mina->Log(message);
    }

    if (spawn_tracked_xmark_for_target("test-enemy-basic-contact", target)) {
        basic_probe_remember_marked(target.entity);
    }
}

void activate_basic_attack_contact_probe(unsigned long long now_ms, int direction, const char *source) {
    if (!g_basic_attack_probe.active) {
        return;
    }
    const bool contact_was_active = g_basic_attack_probe.contact_active;
    const int previous_direction = g_basic_attack_probe.direction;
    if (g_basic_attack_probe.direction != direction) {
        const unsigned int merge_window_ms = env_uint(
            "MINA_XMARK_BASIC_HEALTH_PROBE_MERGE_WINDOW_MS",
            env_uint("MINA_XMARK_BASIC_HEALTH_WINDOW_MS", 850) + 250u);
        const bool can_adopt_frame_direction =
            env_bool("MINA_XMARK_BASIC_CONTACT_ADOPT_FRAME_DIRECTION", true) &&
            (!merge_window_ms ||
             (now_ms >= g_basic_attack_probe.started_ms &&
              now_ms <= g_basic_attack_probe.started_ms + merge_window_ms));
        if (!can_adopt_frame_direction) {
            return;
        }
        g_basic_attack_probe.direction = direction;
    }
    if (!g_basic_attack_probe.active) {
        return;
    }
    if (env_bool("MINA_XMARK_BASIC_CONTACT_REQUIRE_RECENT_FRAME", true) &&
        !recent_basic_frame_state_for_direction(now_ms, direction)) {
        if (g_mina && env_bool("MINA_XMARK_BASIC_CONTACT_FRAME_GATE_LOG", false)) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn basic contact rejected: no recent f0014-f0028 frame source=%s dir=%s.\n",
                source ? source : "smear",
                direction_name(direction));
            g_mina->Log(message);
        }
        return;
    }

    g_basic_attack_probe.contact_active = true;
    g_basic_attack_probe.health_check_armed = true;
    g_basic_attack_probe.next_health_check_ms = now_ms;
    g_basic_attack_probe.health_check_count = 0;
    g_basic_attack_probe.contact_started_ms = now_ms;
    const unsigned int contact_window_ms = env_uint("MINA_XMARK_BASIC_HEALTH_CONTACT_WINDOW_MS", 260);
    const unsigned long long contact_expires = now_ms + contact_window_ms;
    if (contact_expires > g_basic_attack_probe.expires_ms) {
        g_basic_attack_probe.expires_ms = contact_expires;
    }

    if (g_mina &&
        env_bool("MINA_XMARK_BASIC_HEALTH_PROBE_LOG", false) &&
        (!contact_was_active || previous_direction != g_basic_attack_probe.direction)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic health probe contact-opened by %s dir=%s baselines=%u contactWindowMs=%u\n",
            source ? source : "smear",
            direction_name(direction),
            g_basic_attack_probe.baseline_count,
            contact_window_ms);
        g_mina->Log(message);
    }

    maybe_mark_test_enemy_on_basic_contact(now_ms, direction, source);
    if (!basic_mark_requires_health_drop() &&
        env_bool("MINA_XMARK_BASIC_HURT_VISUAL_ON_CONTACT", true) &&
        g_basic_attack_probe.marked_enemy_count < env_uint("MINA_XMARK_BASIC_HEALTH_MAX_MARKS", 16)) {
        Vec3 player_api{0.0f, 0.0f, 0.0f};
        if (g_mina) {
            g_mina->PlayerGetPos(&player_api.x, &player_api.y);
        }
        maybe_mark_recent_hurt_visual_host_for_basic_attack(
            now_ms,
            player_api,
            env_uint("MINA_XMARK_BASIC_HEALTH_MAX_MARKS", 16));
    }
}

void queue_basic_attack_contact_probe(unsigned long long now_ms, int direction, unsigned int delay_ms) {
    g_attack_smear_contact_pending = true;
    g_attack_smear_contact_pending_direction = direction;
    g_attack_smear_contact_pending_ms = now_ms + delay_ms;
}

void maybe_open_pending_basic_attack_contact(unsigned long long now_ms) {
    if (!g_attack_smear_contact_pending || now_ms < g_attack_smear_contact_pending_ms) {
        return;
    }
    const int direction = g_attack_smear_contact_pending_direction;
    g_attack_smear_contact_pending = false;
    activate_basic_attack_contact_probe(now_ms, direction, "hammer-smear");
}

void start_basic_attack_health_probe(
    unsigned long long now_ms,
    int direction,
    bool allow_pre_frame_baseline = false,
    bool begin_new_attack = false) {
    const XMarkBasicAttackProbe previous_probe = g_basic_attack_probe;
    const unsigned int merge_window_ms = env_uint(
        "MINA_XMARK_BASIC_HEALTH_PROBE_MERGE_WINDOW_MS",
        env_uint("MINA_XMARK_BASIC_HEALTH_WINDOW_MS", 850) + 250u);
    const bool allow_cross_direction_merge =
        env_bool("MINA_XMARK_BASIC_HEALTH_MERGE_ACTIVE_PROBES_ANY_DIRECTION", true);
    const bool merge_previous_probe =
        !begin_new_attack &&
        previous_probe.active &&
        env_bool("MINA_XMARK_BASIC_HEALTH_MERGE_ACTIVE_PROBES", true) &&
        (previous_probe.direction == direction || allow_cross_direction_merge) &&
        (!merge_window_ms ||
         (now_ms >= previous_probe.started_ms &&
          now_ms <= previous_probe.started_ms + merge_window_ms));
    const unsigned long long discovery_event = merge_previous_probe
        ? g_official_enemy_attack_discovery_event
        : ++g_basic_attack_discovery_event_counter;
    if (!merge_previous_probe) {
        g_official_enemy_attack_discovery_event = discovery_event;
        g_official_enemy_attack_discovery_attempts = 0;
        g_last_official_enemy_attack_discovery_ms = 0;
    }

    g_basic_attack_probe = XMarkBasicAttackProbe{};
    if (!env_bool("MINA_XMARK_BASIC_HEALTH_PROBE_ENABLED", true)) {
        return;
    }
    XMarkBasicFrameState basic_frame_state{};
    if (!allow_pre_frame_baseline &&
        env_bool("MINA_XMARK_BASIC_HEALTH_REQUIRE_RECENT_BASIC_FRAME", true) &&
        !recent_basic_frame_state_for_direction(now_ms, direction, &basic_frame_state)) {
        if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_FRAME_GATE_LOG", false)) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn basic health probe rejected: no recent f0014-f0028 frame dir=%s.\n",
                direction_name(direction));
            g_mina->Log(message);
        }
        return;
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    reconcile_official_enemy_room_registry(now_ms);

    if (g_last_world &&
        env_bool("MINA_XMARK_ROOM_REGISTRY_ATTACK_DISCOVERY", true)) {
        unsigned int room_index = 0xFFFFFFFFu;
        __try {
            room_index = g_mina->GetRoomIndex();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            room_index = 0xFFFFFFFFu;
        }
        if (room_index != g_official_enemy_attack_discovery_room) {
            g_official_enemy_attack_discovery_room = room_index;
            g_official_enemy_attack_discovery_attempts = 0;
            g_last_official_enemy_attack_discovery_ms = 0;
        }
        reconcile_official_enemy_room_registry(now_ms, true);
        const unsigned int max_attempts = std::max(
            1u,
            env_uint("MINA_XMARK_ROOM_REGISTRY_ATTACK_DISCOVERY_ATTEMPTS", 1));
        const unsigned int min_snapshot_gap_ms = std::max(
            50u,
            env_uint("MINA_XMARK_ROOM_REGISTRY_ATTACK_DISCOVERY_MIN_SCAN_GAP_MS", 150));
        const bool snapshot_gap_ready =
            !g_last_official_enemy_scan_ms ||
            now_ms >= g_last_official_enemy_scan_ms + min_snapshot_gap_ms;
        const unsigned int stale_snapshot_ms = std::max(
            250u,
            env_uint("MINA_XMARK_ROOM_REGISTRY_ATTACK_DISCOVERY_STALE_MS", 1000));
        const bool discovery_needed =
            !xmark_spawn_point_room_registry_authoritative() ||
            !g_last_official_enemy_scan_ms ||
            now_ms >= g_last_official_enemy_scan_ms + stale_snapshot_ms;
        if (discovery_needed &&
            g_official_enemy_attack_discovery_attempts < max_attempts &&
            snapshot_gap_ready) {
            g_last_official_enemy_scan_ms = 0;
            update_official_enemy_snapshot(g_last_world, now_ms);
            ++g_official_enemy_attack_discovery_attempts;
            g_last_official_enemy_attack_discovery_ms = now_ms;
            reconcile_official_enemy_room_registry(now_ms, true);
        }
    }

    if (g_last_world &&
        env_bool("MINA_XMARK_BASIC_HEALTH_FORCE_OFFICIAL_SNAPSHOT", false) &&
        xmark_combat_full_scan_fallback_allowed()) {
        const unsigned int stale_ms = env_uint("MINA_XMARK_BASIC_HEALTH_FORCE_OFFICIAL_SNAPSHOT_STALE_MS", 2500);
        const unsigned int attack_refresh_cooldown_ms = std::max(
            100u,
            env_uint("MINA_XMARK_BASIC_HEALTH_ATTACK_SNAPSHOT_COOLDOWN_MS", 500));
        const bool attack_refresh_due =
            env_bool("MINA_XMARK_BASIC_HEALTH_REFRESH_SNAPSHOT_ON_ATTACK", true) &&
            (!g_last_official_enemy_scan_ms ||
             now_ms >= g_last_official_enemy_scan_ms + attack_refresh_cooldown_ms);
        const bool missing_snapshot =
            !g_official_enemy_snapshot_valid ||
            !g_last_official_enemy_scan_ms;
        const unsigned int empty_refresh_ms = std::max(
            500u,
            env_uint("MINA_XMARK_BASIC_HEALTH_EMPTY_SNAPSHOT_REFRESH_MS", 2500));
        const bool empty_snapshot_due =
            g_official_enemy_snapshot_valid &&
            g_official_enemy_host_count == 0 &&
            g_last_official_enemy_scan_ms > 0 &&
            now_ms >= g_last_official_enemy_scan_ms + empty_refresh_ms;
        const bool stale_snapshot =
            stale_ms > 0 &&
            g_last_official_enemy_scan_ms > 0 &&
            now_ms >= g_last_official_enemy_scan_ms &&
            now_ms - g_last_official_enemy_scan_ms >= stale_ms;
        if (attack_refresh_due || missing_snapshot || empty_snapshot_due || stale_snapshot) {
            g_last_official_enemy_scan_ms = 0;
            update_official_enemy_snapshot(g_last_world, now_ms);
        }
    }

    g_basic_attack_probe.active = true;
    g_basic_attack_probe.direction = direction;
    g_basic_attack_probe.started_ms = merge_previous_probe ? previous_probe.started_ms : now_ms;
    g_basic_attack_probe.expires_ms = now_ms + env_uint("MINA_XMARK_BASIC_HEALTH_WINDOW_MS", 500);
    if (merge_previous_probe && previous_probe.expires_ms > g_basic_attack_probe.expires_ms) {
        g_basic_attack_probe.expires_ms = previous_probe.expires_ms;
    }
    g_basic_attack_probe.contact_active = merge_previous_probe && previous_probe.contact_active;
    g_basic_attack_probe.contact_started_ms = merge_previous_probe ? previous_probe.contact_started_ms : 0;
    g_basic_attack_probe.last_runtime_scan_ms = 0;
    if (merge_previous_probe) {
        basic_probe_copy_marked_from_previous(previous_probe);
        const unsigned int previous_baseline_count =
            previous_probe.baseline_count <
                    static_cast<unsigned int>(sizeof(previous_probe.baselines) / sizeof(previous_probe.baselines[0]))
                ? previous_probe.baseline_count
                : static_cast<unsigned int>(sizeof(previous_probe.baselines) / sizeof(previous_probe.baselines[0]));
        for (unsigned int i = 0; i < previous_baseline_count; ++i) {
            basic_probe_add_or_merge_baseline(previous_probe.baselines[i]);
        }
    }

    const unsigned int max_baselines = static_cast<unsigned int>(sizeof(g_basic_attack_probe.baselines) / sizeof(g_basic_attack_probe.baselines[0]));
    unsigned int official_baseline_count = 0;
    if (env_bool("MINA_XMARK_OFFICIAL_BASIC_HEALTH_BASELINES", true) &&
        g_official_enemy_snapshot_valid) {
        const bool official_range_filter =
            env_bool("MINA_XMARK_BASIC_HEALTH_OFFICIAL_BASELINE_RANGE_FILTER", true);
        const float official_max_distance =
            env_float("MINA_XMARK_BASIC_HEALTH_OFFICIAL_BASELINE_MAX_DISTANCE", 24.0f);
        const float official_max_distance_sq = official_max_distance * official_max_distance;
        struct OfficialBaselineCandidate {
            unsigned int host_index;
            float distance_sq;
        };
        OfficialBaselineCandidate candidates[32]{};
        const unsigned int candidate_limit = std::min<unsigned int>(
            static_cast<unsigned int>(sizeof(candidates) / sizeof(candidates[0])),
            std::max(1u, env_uint("MINA_XMARK_BASIC_HEALTH_OFFICIAL_BASELINE_LIMIT", 16)));
        unsigned int candidate_count = 0;
        for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
            const XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
            if (!host.active || !host.entity || !host.combat_core ||
                !(host.health > 0.0f) || !(host.health_max > 0.0f)) {
                continue;
            }
            const float dx = host.position.x - player_api.x;
            const float dy = host.position.y - player_api.y;
            const float distance_sq = dx * dx + dy * dy;
            if (official_range_filter && official_max_distance > 0.0f) {
                if (distance_sq > official_max_distance_sq) {
                    continue;
                }
            }

            unsigned int insert_at = candidate_count;
            while (insert_at > 0 && candidates[insert_at - 1].distance_sq > distance_sq) {
                if (insert_at < candidate_limit) {
                    candidates[insert_at] = candidates[insert_at - 1];
                }
                --insert_at;
            }
            if (insert_at < candidate_limit) {
                candidates[insert_at] = OfficialBaselineCandidate{i, distance_sq};
                if (candidate_count < candidate_limit) {
                    ++candidate_count;
                }
            }
        }

        for (unsigned int candidate_index = 0; candidate_index < candidate_count; ++candidate_index) {
            const XMarkOfficialEnemyHost &host =
                g_official_enemy_hosts[candidates[candidate_index].host_index];
            XMarkHealthBaseline baseline{};
            baseline.entity = host.entity;
            baseline.official_combat_core = host.combat_core;
            baseline.anchor = host.position;
            baseline.health_value = host.health;
            float live_health = 0.0f;
            if (combat_core_health_read(host.combat_core, &live_health)) {
                baseline.health_value = live_health;
            }
            baseline.health_max = host.health_max;
            baseline.official = true;
            if (basic_probe_add_or_merge_baseline(baseline)) {
                ++official_baseline_count;
            }
            if (g_basic_attack_probe.baseline_count >= max_baselines) {
                break;
            }
        }
    }

    constexpr unsigned int kMaxTargets = 96;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int reads = 0;
    unsigned int count = 0;
    const bool collect_runtime_baselines =
        env_bool("MINA_XMARK_BASIC_HEALTH_USE_RUNTIME_BASELINES", false);
    if (collect_runtime_baselines) {
        count = collect_runtime_targets(targets, kMaxTargets, &reads, &player_api, nullptr);
    }
    const bool require_contact_band_at_arm = env_bool("MINA_XMARK_BASIC_HEALTH_REQUIRE_CONTACT_BAND_AT_ARM", false);
    for (unsigned int i = 0; i < count; ++i) {
        if (!targets[i].health_like ||
            (require_contact_band_at_arm && !runtime_target_in_basic_contact_band(targets[i], player_api, direction))) {
            continue;
        }
        float history_health = targets[i].health_value;
        runtime_health_history_for_target(targets[i], &history_health);
        XMarkHealthBaseline baseline{};
        baseline.entity = targets[i].entity;
        baseline.health_offset = targets[i].health_offset;
        baseline.anchor_offset = targets[i].anchor_offset;
        baseline.health_kind = targets[i].health_kind;
        baseline.anchor = targets[i].anchor;
        baseline.health_value = history_health;
        baseline.health_max = targets[i].health_max;
        basic_probe_add_or_merge_baseline(baseline);
        if (g_basic_attack_probe.baseline_count >= max_baselines) {
            break;
        }
    }

    if (g_basic_attack_probe.baseline_count == 0) {
        g_basic_attack_probe.active = false;
        g_basic_attack_probe.contact_active = false;
        return;
    }

    if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_PROBE_LOG", false)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic health probe armed dir=%s frame=%s baselines=%u official=%u candidates=%u reads=%u windowMs=%u merged=%u waitingForContact=1\n",
            direction_name(direction),
            basic_frame_state.frame[0] ? basic_frame_state.frame : "-",
            g_basic_attack_probe.baseline_count,
            official_baseline_count,
            count,
            reads,
            env_uint("MINA_XMARK_BASIC_HEALTH_WINDOW_MS", 500),
            merge_previous_probe ? 1u : 0u);
        g_mina->Log(message);
    }
}

bool basic_probe_baseline_matches_target(
    const XMarkHealthBaseline &baseline,
    const XMarkRuntimeTarget &target) {
    const bool baseline_official = baseline.official && baseline.official_combat_core;
    const bool target_official = target.official_follow && target.official_combat_core;
    if (baseline_official || target_official) {
        return baseline_official &&
            target_official &&
            baseline.official_combat_core == target.official_combat_core;
    }
    if (baseline.entity == target.entity &&
        (!env_bool("MINA_XMARK_BASIC_HEALTH_MATCH_OFFSET", true) || baseline.health_offset == target.health_offset)) {
        return true;
    }
    if (!env_bool("MINA_XMARK_BASIC_HEALTH_POSITION_MATCH", true)) {
        return false;
    }
    if (env_bool("MINA_XMARK_BASIC_HEALTH_POSITION_MATCH_OFFSET", true) &&
        baseline.health_offset != target.health_offset) {
        return false;
    }
    const float radius = env_float("MINA_XMARK_BASIC_HEALTH_POSITION_MATCH_RADIUS", 8.0f);
    const float dx = baseline.anchor.x - target.anchor.x;
    const float dy = baseline.anchor.y - target.anchor.y;
    return (dx * dx) + (dy * dy) <= radius * radius;
}

XMarkAttachment *active_xmark_attachment_for_damage_target(
    const XMarkHealthBaseline &baseline,
    const XMarkRuntimeTarget &target) {
    const uintptr_t entity = target.entity ? target.entity : baseline.entity;
    const uintptr_t combat_core = target.official_combat_core
        ? target.official_combat_core
        : baseline.official_combat_core;
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active) {
            continue;
        }
        if (combat_core && attachment.official_combat_core == combat_core) {
            return &attachment;
        }
        if (entity && attachment.target == entity) {
            return &attachment;
        }
    }
    return nullptr;
}

void remember_xmark_basic_damage(
    const XMarkHealthBaseline &baseline,
    const XMarkRuntimeTarget &target,
    float damage,
    unsigned long long now_ms) {
    XMarkAttachment *attachment = active_xmark_attachment_for_damage_target(baseline, target);
    if (!attachment || !std::isfinite(damage) || damage <= 0.0f) {
        return;
    }
    attachment->last_basic_damage = damage;
    attachment->last_basic_damage_ms = now_ms;
}

bool defer_charged_sized_drop_from_basic_probe(
    XMarkHealthBaseline &baseline,
    const XMarkRuntimeTarget &target,
    float drop,
    unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BASIC_DEFER_CHARGED_SIZED_DROP", true)) {
        return false;
    }
    XMarkAttachment *attachment = active_xmark_attachment_for_damage_target(baseline, target);
    if (!attachment || !std::isfinite(attachment->last_basic_damage) || attachment->last_basic_damage <= 0.0f) {
        return false;
    }
    const float charged_ratio = std::max(
        1.05f,
        env_float("MINA_XMARK_BASIC_CHARGED_SIZED_DROP_RATIO", 1.5f));
    if (drop + 0.001f < attachment->last_basic_damage * charged_ratio) {
        return false;
    }

    attachment->health_before_recent_drop = baseline.health_value;
    attachment->health_after_recent_drop = target.health_value;
    attachment->recent_health_drop_ms = now_ms;
    attachment->observed_health = target.health_value;
    attachment->observed_health_ms = now_ms;
    attachment->has_observed_health = true;
    baseline.health_value = target.health_value;
    baseline.anchor = target.anchor;
    g_basic_attack_probe.active = false;
    g_basic_attack_probe.contact_active = false;

    if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic probe deferred charged-sized drop target=0x%p core=0x%p hp=%.3f->%.3f drop=%.3f lastBasic=%.3f ratio=%.2f\n",
            reinterpret_cast<void *>(target.entity ? target.entity : baseline.entity),
            reinterpret_cast<void *>(target.official_combat_core
                ? target.official_combat_core
                : baseline.official_combat_core),
            static_cast<double>(attachment->health_before_recent_drop),
            static_cast<double>(attachment->health_after_recent_drop),
            static_cast<double>(drop),
            static_cast<double>(attachment->last_basic_damage),
            static_cast<double>(charged_ratio));
        g_mina->Log(message);
    }
    return true;
}

bool mark_basic_probe_target(
    const char *reason,
    XMarkHealthBaseline &baseline,
    XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    float min_drop,
    unsigned int max_marks,
    unsigned int reads) {
    if (basic_probe_already_marked(target.entity) ||
        basic_probe_already_marked(target.official_combat_core) ||
        basic_probe_already_marked(baseline.entity) ||
        basic_probe_already_marked(baseline.official_combat_core)) {
        return false;
    }
    const float drop = baseline.health_value - target.health_value;
    if (drop < min_drop) {
        return false;
    }
    const unsigned long long drop_now_ms = GetTickCount64();
    XMarkBurnEffect *active_burn = find_xmark_burn_effect(
        target.entity ? target.entity : baseline.entity,
        target.visual_key,
        target.official_combat_core
            ? target.official_combat_core
            : baseline.official_combat_core);
    const float burn_tick_damage = env_float("MINA_XMARK_BURN_DAMAGE_PER_TICK", 1.0f);
    const float burn_tick_tolerance = std::max(
        0.001f,
        env_float("MINA_XMARK_BASIC_REJECT_BURN_TICK_TOLERANCE", 0.05f));
    const unsigned int burn_tick_reject_ms = std::max(
        16u,
        env_uint("MINA_XMARK_BASIC_REJECT_BURN_TICK_WINDOW_MS", 180));
    const bool is_recent_owned_burn_tick =
        active_burn &&
        active_burn->last_tick_ms &&
        drop_now_ms >= active_burn->last_tick_ms &&
        drop_now_ms - active_burn->last_tick_ms <= burn_tick_reject_ms &&
        std::fabs(drop - burn_tick_damage) <= burn_tick_tolerance;
    if (is_recent_owned_burn_tick) {
        baseline.health_value = target.health_value;
        baseline.anchor = target.anchor;
        if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn basic probe ignored owned burn tick target=0x%p core=0x%p hp=%.3f drop=%.3f tickAgeMs=%llu\n",
                reinterpret_cast<void *>(target.entity ? target.entity : baseline.entity),
                reinterpret_cast<void *>(target.official_combat_core
                    ? target.official_combat_core
                    : baseline.official_combat_core),
                static_cast<double>(target.health_value),
                static_cast<double>(drop),
                drop_now_ms - active_burn->last_tick_ms);
            g_mina->Log(message);
        }
        return false;
    }
    const bool official_damage_target =
        (target.official_follow && target.official_combat_core) ||
        (baseline.official && baseline.official_combat_core);
    const bool trust_confirmed_basic_health_drop = env_bool(
        "MINA_XMARK_BASIC_TRUST_CONFIRMED_HEALTH_DROP",
        true);
    if (official_damage_target) {
        const uintptr_t live_entity = target.entity ? target.entity : baseline.entity;
        const uintptr_t live_core = target.official_combat_core
            ? target.official_combat_core
            : baseline.official_combat_core;
        Vec3 live_center{};
        float live_half_w = 0.0f;
        float live_half_h = 0.0f;
        if (official_enemy_live_contact_bounds_read(
                live_entity,
                live_core,
                &live_center,
                &live_half_w,
                &live_half_h)) {
            target.anchor = live_center;
            target.position = live_center;
            target.contact_half_w = live_half_w;
            target.contact_half_h = live_half_h;
        } else if (official_entity_world_position_read(live_entity, &live_center)) {
            target.anchor = live_center;
            target.position = live_center;
        }
        XMarkOfficialEnemyHost candidate_host{};
        candidate_host.active = true;
        candidate_host.entity = target.entity ? target.entity : baseline.entity;
        candidate_host.combat_core = target.official_combat_core
            ? target.official_combat_core
            : baseline.official_combat_core;
        candidate_host.position = target.position;
        candidate_host.health = target.health_value;
        candidate_host.health_max = target.health_max > 0.0f ? target.health_max : baseline.health_max;
        candidate_host.last_seen_ms = GetTickCount64();
        std::snprintf(candidate_host.component_type, sizeof(candidate_host.component_type), "CombatCore");
        char reject_reason[96]{};
        if (!xmark_official_host_allowed_for_xmark(
                candidate_host,
                candidate_host.last_seen_ms,
                reject_reason,
                sizeof(reject_reason))) {
            if (g_mina && env_bool("MINA_XMARK_OFFICIAL_ENEMY_WHITELIST_LOG", false)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn basic health delta rejected non-whitelisted reason=%s target=0x%p baseline=0x%p officialCore=0x%p reject=%s\n",
                    reason ? reason : "<none>",
                    reinterpret_cast<void *>(target.entity),
                    reinterpret_cast<void *>(baseline.entity),
                    reinterpret_cast<void *>(candidate_host.combat_core),
                    reject_reason[0] ? reject_reason : "-");
                g_mina->Log(message);
            }
            return false;
        }
    }
    if (!trust_confirmed_basic_health_drop &&
        env_bool("MINA_XMARK_OFFICIAL_BASIC_REQUIRE_FRAME_CONTACT", true)) {
        XMarkBasicFrameState contact_state{};
        const unsigned long long contact_now_ms = GetTickCount64();
        if (!recent_basic_frame_state_for_direction(
                contact_now_ms,
                g_basic_attack_probe.direction,
                &contact_state) ||
            !basic_frame_state_overlaps_runtime_target(contact_state, target)) {
            baseline.health_value = target.health_value;
            baseline.anchor = target.anchor;
            if (g_mina && env_bool("MINA_XMARK_OFFICIAL_BASIC_CONTACT_REJECT_LOG", false)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn basic HP drop rejected outside weapon footprint reason=%s dir=%s frame=%s target=0x%p pos=(%.3f, %.3f) hp=%.3f->%.3f\n",
                    reason ? reason : "<none>",
                    direction_name(g_basic_attack_probe.direction),
                    contact_state.frame[0] ? contact_state.frame : "-",
                    reinterpret_cast<void *>(target.entity),
                    static_cast<double>(target.position.x),
                    static_cast<double>(target.position.y),
                    static_cast<double>(baseline.health_value),
                    static_cast<double>(target.health_value));
                g_mina->Log(message);
            }
            return false;
        }
    }
    if (!official_damage_target &&
        (pointer_is_rejected_runtime_target(target.entity) ||
         pointer_is_rejected_runtime_target(baseline.entity))) {
        if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_LOG_PLAYER_REJECT", false)) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn basic health delta rejected player-side target=0x%p baseline=0x%p officialCore=0x%p\n",
                reinterpret_cast<void *>(target.entity),
                reinterpret_cast<void *>(baseline.entity),
                reinterpret_cast<void *>(target.official_combat_core));
            g_mina->Log(message);
        }
        return false;
    }
    const unsigned long long damage_now_ms = drop_now_ms;
    if (defer_charged_sized_drop_from_basic_probe(
            baseline,
            target,
            drop,
            damage_now_ms)) {
        return true;
    }
    g_last_basic_health_drop_ms = damage_now_ms;
    g_last_basic_health_drop_target = target.entity ? target.entity : baseline.entity;
    g_last_basic_health_drop_amount = drop;
    g_last_basic_health_drop_before = baseline.health_value;
    g_last_basic_health_drop_after = target.health_value;
    const float dead_health_threshold =
        env_float("MINA_XMARK_NATIVE_FINAL_HIT_DEATH_PROBE_HEALTH_THRESHOLD", 0.001f);
    if (official_damage_target && target.health_value <= dead_health_threshold) {
        XMarkOfficialEnemyHost death_host{};
        death_host.active = true;
        death_host.entity = target.entity ? target.entity : baseline.entity;
        death_host.combat_core = target.official_combat_core
            ? target.official_combat_core
            : baseline.official_combat_core;
        death_host.position = target.position;
        death_host.health = target.health_value;
        death_host.health_max = target.health_max > 0.0f ? target.health_max : baseline.health_max;
        death_host.last_seen_ms = g_last_basic_health_drop_ms;
        std::snprintf(death_host.component_type, sizeof(death_host.component_type), "CombatCore");
        queue_xmark_native_final_hit_death_watch(death_host, g_last_basic_health_drop_ms);

        basic_probe_remember_marked(death_host.entity);
        basic_probe_remember_marked(death_host.combat_core);
        return true;
    }
    const bool target_in_contact_band =
        runtime_target_in_basic_contact_band(target, player_api, g_basic_attack_probe.direction);
    const bool official_direct =
        target.official_follow &&
        env_bool("MINA_XMARK_OFFICIAL_BASIC_DIRECT_MARK", true);

    runtime_target_direction_delta_for_direction(
        target,
        player_api,
        g_basic_attack_probe.direction,
        &target.forward_delta,
        &target.lateral_delta);
    target.facing_match = runtime_target_in_basic_contact_band(target, player_api, g_basic_attack_probe.direction);
    target.score = (target.lateral_delta * target.lateral_delta) + (target.forward_delta * target.forward_delta * 0.1f);
    if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_MARK_LOG", false)) {
        char message[640]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic health delta mark reason=%s dir=%s target=0x%p officialCore=0x%p baseline=0x%p health=%.3f->%.3f drop=%.3f anchorOffset=0x%X pos=(%.3f, %.3f, %.3f) forward=%.3f lateral=%.3f score=%.3f reads=%u\n",
            reason ? reason : "<none>",
            direction_name(g_basic_attack_probe.direction),
            reinterpret_cast<void *>(target.entity),
            reinterpret_cast<void *>(target.official_combat_core),
            reinterpret_cast<void *>(baseline.entity),
            static_cast<double>(baseline.health_value),
            static_cast<double>(target.health_value),
            static_cast<double>(drop),
            target.anchor_offset,
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            static_cast<double>(target.forward_delta),
            static_cast<double>(target.lateral_delta),
            static_cast<double>(target.score),
            reads);
        g_mina->Log(message);
    }
    if (!official_direct && env_bool("MINA_XMARK_BASIC_HEALTH_PREFER_VISUAL_HOST", true)) {
        const Vec3 visual_space_position = render_position_to_visual_host_space(target.position);
        XMarkVisualEnemyHost visual_host{};
        const unsigned long long visual_now_ms = GetTickCount64();
        const bool trust_hp_drop_for_visual_host =
            env_bool("MINA_XMARK_BASIC_HEALTH_TRUST_DROP_FOR_VISUAL_HOST", false);
        const bool require_frame_overlap =
            env_bool("MINA_XMARK_BASIC_REQUIRE_FRAME_CONTACT_OVERLAP", true) &&
            !trust_confirmed_basic_health_drop &&
            !trust_hp_drop_for_visual_host;
        XMarkBasicFrameState overlap_state{};
        const bool have_overlap_state =
            !require_frame_overlap ||
            recent_basic_frame_state_for_direction(
                visual_now_ms,
                g_basic_attack_probe.direction,
                &overlap_state);
        if (env_bool("MINA_XMARK_BASIC_HEALTH_FORCE_VISUAL_STATE_READ", false)) {
            read_enemy_visual_state_file(visual_now_ms, true);
        }
        if (require_frame_overlap && !have_overlap_state) {
            if (g_mina && env_bool("MINA_XMARK_BASIC_FRAME_CONTACT_LOG_REJECT", false)) {
                char reject_message[320]{};
                std::snprintf(
                    reject_message,
                    sizeof(reject_message),
                    "XMarkBurn basic health delta rejected reason=%s: no recent weapon-contact geometry dir=%s.\n",
                    reason ? reason : "<none>",
                    direction_name(g_basic_attack_probe.direction));
                g_mina->Log(reject_message);
            }
            return false;
        }
        bool used_contact_candidate = false;
        bool found_visual_host = find_basic_attack_candidate_visual_host_for_health_drop(
            target,
            player_api,
            g_basic_attack_probe.direction,
            &visual_host,
            visual_now_ms);
        used_contact_candidate = found_visual_host;
        if (!found_visual_host) {
            found_visual_host =
                find_nearest_visual_enemy_host_to_visual_position(visual_space_position, &visual_host, visual_now_ms);
            if (found_visual_host &&
                require_frame_overlap &&
                !basic_frame_state_overlaps_visual_host(overlap_state, visual_host)) {
                found_visual_host = false;
            }
        }
        bool used_contact_fallback = false;
        if (!found_visual_host && env_bool("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_CONTACT_FALLBACK", true)) {
            found_visual_host = find_basic_attack_visual_enemy_host_for_damage(
                target,
                player_api,
                g_basic_attack_probe.direction,
                &visual_host,
                visual_now_ms);
            used_contact_fallback = found_visual_host;
        }
        if (found_visual_host) {
            if (!used_contact_candidate &&
                require_frame_overlap &&
                !basic_frame_state_overlaps_visual_host(overlap_state, visual_host)) {
                if (g_mina && env_bool("MINA_XMARK_BASIC_FRAME_CONTACT_LOG_REJECT", false)) {
                    char reject_message[480]{};
                    std::snprintf(
                        reject_message,
                        sizeof(reject_message),
                        "XMarkBurn basic health delta rejected reason=%s: host outside weapon footprint key=0x%llX frame=%s.\n",
                        reason ? reason : "<none>",
                        visual_host.key,
                        overlap_state.frame[0] ? overlap_state.frame : "-");
                    g_mina->Log(reject_message);
                }
                return false;
            }
            if (g_mina) {
                char visual_message[640]{};
                std::snprintf(
                    visual_message,
                    sizeof(visual_message),
                    "XMarkBurn basic health delta using visual host reason=%s candidate=%u fallback=%u targetContact=%u frameContact=%u hostKey=0x%llX entry=%s stem=%s rawPos=(%.3f, %.3f) expectedRaw=(%.3f, %.3f) renderPos=(%.3f, %.3f)\n",
                    reason ? reason : "<none>",
                    used_contact_candidate ? 1u : 0u,
                    used_contact_fallback ? 1u : 0u,
                    target_in_contact_band ? 1u : 0u,
                    (used_contact_candidate || require_frame_overlap) ? 1u : 0u,
                    visual_host.key,
                    visual_host.entry[0] ? visual_host.entry : "-",
                    visual_host.stem[0] ? visual_host.stem : "-",
                    static_cast<double>(visual_host.position.x),
                    static_cast<double>(visual_host.position.y),
                    static_cast<double>(visual_space_position.x),
                    static_cast<double>(visual_space_position.y),
                    static_cast<double>(target.position.x),
                    static_cast<double>(target.position.y));
                g_mina->Log(visual_message);
            }
            if (ensure_xmark_for_visual_host(
                    used_contact_candidate ? "basic-health-delta-contact-candidate" : reason,
                    visual_host)) {
                link_xmark_visual_attachment_to_damage_target(visual_host.key, target, visual_now_ms);
                remember_xmark_basic_damage(baseline, target, drop, visual_now_ms);
                if (used_contact_candidate) {
                    for (XMarkBasicHitCandidate &candidate : g_basic_hit_candidates) {
                        if (candidate.active && candidate.key == visual_host.key) {
                            candidate.last_mark_ms = visual_now_ms;
                            break;
                        }
                    }
                }
                basic_probe_remember_marked(target.entity);
                basic_probe_remember_marked(target.official_combat_core);
                basic_probe_remember_marked(baseline.entity);
                basic_probe_remember_marked(baseline.official_combat_core);
                basic_probe_note_mark_success();
                if (g_basic_attack_probe.marked_enemy_count >= max_marks) {
                    g_basic_attack_probe.active = false;
                }
                return true;
            }
        } else if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_LOG_MISS", false)) {
            char visual_miss[384]{};
            std::snprintf(
                visual_miss,
                sizeof(visual_miss),
                "XMarkBurn basic health delta visual host miss reason=%s targetContact=%u expectedRaw=(%.3f, %.3f) renderPos=(%.3f, %.3f)\n",
                reason ? reason : "<none>",
                target_in_contact_band ? 1u : 0u,
                static_cast<double>(visual_space_position.x),
                static_cast<double>(visual_space_position.y),
                static_cast<double>(target.position.x),
                static_cast<double>(target.position.y));
            g_mina->Log(visual_miss);
        }
    }
    const bool hp_direct_fallback =
        !official_direct &&
        env_bool("MINA_XMARK_BASIC_HEALTH_DIRECT_FALLBACK_ON_VISUAL_MISS", true);
    if (!official_direct &&
        env_bool("MINA_XMARK_BASIC_HEALTH_REQUIRE_VISUAL_HOST_FOR_MARK", true) &&
        !hp_direct_fallback) {
        return false;
    }
    const bool require_contact_band =
        !trust_confirmed_basic_health_drop &&
        (official_direct
            ? env_bool("MINA_XMARK_OFFICIAL_BASIC_REQUIRE_CONTACT_BAND", false)
            : env_bool("MINA_XMARK_BASIC_HEALTH_REQUIRE_CONTACT_BAND", true));
    const bool fallback_requires_contact_band =
        !trust_confirmed_basic_health_drop &&
        (!hp_direct_fallback ||
         env_bool("MINA_XMARK_BASIC_HEALTH_DIRECT_FALLBACK_REQUIRE_CONTACT_BAND", true));
    if (require_contact_band && !target_in_contact_band && fallback_requires_contact_band) {
        return false;
    }
    if (hp_direct_fallback && g_mina) {
        char fallback_message[512]{};
        std::snprintf(
            fallback_message,
            sizeof(fallback_message),
            "XMarkBurn basic health delta direct fallback reason=%s dir=%s target=0x%p healthDrop=%.3f targetContact=%u pos=(%.3f, %.3f, %.3f)\n",
            reason ? reason : "<none>",
            direction_name(g_basic_attack_probe.direction),
            reinterpret_cast<void *>(target.entity),
            static_cast<double>(drop),
            target_in_contact_band ? 1u : 0u,
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z));
        g_mina->Log(fallback_message);
    }
    if (spawn_tracked_xmark_for_target(reason, target)) {
        remember_xmark_basic_damage(baseline, target, drop, damage_now_ms);
        basic_probe_remember_marked(target.entity);
        basic_probe_remember_marked(target.official_combat_core);
        basic_probe_remember_marked(baseline.entity);
        basic_probe_remember_marked(baseline.official_combat_core);
        basic_probe_note_mark_success();
        if (g_basic_attack_probe.marked_enemy_count >= max_marks) {
            g_basic_attack_probe.active = false;
        }
        return true;
    }
    return false;
}

bool maybe_mark_recent_hurt_visual_host_for_basic_attack(
    unsigned long long now_ms,
    const Vec3 &player_api,
    unsigned int max_marks) {
    const bool allow_hurt_flash_damage_confirm =
        env_bool("MINA_XMARK_BASIC_HEALTH_ALLOW_HURT_FLASH_CONFIRM", false);
    if ((basic_mark_requires_health_drop() && !allow_hurt_flash_damage_confirm) ||
        !env_bool("MINA_XMARK_BASIC_HURT_VISUAL_FALLBACK", true) ||
        g_basic_attack_probe.marked_enemy_count >= max_marks ||
        !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }

    const unsigned int max_hurt_age_ms = env_uint("MINA_XMARK_BASIC_HURT_VISUAL_MAX_AGE_MS", 450);
    const bool require_contact_band = env_bool("MINA_XMARK_BASIC_HURT_VISUAL_REQUIRE_CONTACT_BAND", true);
    const bool prefer_trained = visual_enemy_host_preference_configured();
    const float preferred_forward = env_float("MINA_XMARK_BASIC_HURT_VISUAL_PREFERRED_FORWARD", 14.0f);
    const float age_weight = env_float("MINA_XMARK_BASIC_HURT_VISUAL_AGE_WEIGHT", 0.02f);
    const float lateral_weight = env_float("MINA_XMARK_BASIC_HURT_VISUAL_LATERAL_WEIGHT", 2.0f);
    const float forward_weight = env_float("MINA_XMARK_BASIC_HURT_VISUAL_FORWARD_WEIGHT", 0.35f);
    const bool require_frame_overlap =
        env_bool("MINA_XMARK_BASIC_REQUIRE_FRAME_CONTACT_OVERLAP", true);
    XMarkBasicFrameState overlap_state{};
    const bool have_latest_overlap_state =
        !require_frame_overlap ||
        recent_basic_frame_state_for_direction(
            now_ms,
            g_basic_attack_probe.direction,
            &overlap_state);
    const unsigned int frame_overlap_sticky_ms = env_uint(
        "MINA_XMARK_BASIC_HURT_VISUAL_FRAME_OVERLAP_STICKY_MS",
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_OVERLAP_STICKY_MS", 650));

    bool found = false;
    XMarkVisualEnemyHost best{};
    XMarkRuntimeTarget best_target{};
    XMarkBasicFrameState best_overlap_state{};
    float best_score = FLT_MAX;
    unsigned long long best_hurt_age_ms = 0;

    for (unsigned int pass = 0; pass < (prefer_trained ? 2u : 1u); ++pass) {
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            const XMarkVisualEnemyHost &host = g_visual_enemy_hosts[i];
            if (!host.active || !host.last_hurt_flash_ms) {
                continue;
            }
            const bool trained_match = visual_enemy_host_matches_preferred_training(host);
            if (prefer_trained && pass == 0 && !trained_match) {
                continue;
            }
            XMarkBasicFrameState host_overlap_state = overlap_state;
            bool host_has_frame_overlap = !require_frame_overlap;
            if (require_frame_overlap) {
                host_has_frame_overlap =
                    have_latest_overlap_state &&
                    basic_frame_state_overlaps_visual_host(overlap_state, host);
                if (!host_has_frame_overlap) {
                    host_has_frame_overlap = recent_basic_frame_sample_overlaps_visual_host(
                        now_ms,
                        g_basic_attack_probe.direction,
                        host,
                        frame_overlap_sticky_ms,
                        &host_overlap_state,
                        nullptr);
                }
                if (!host_has_frame_overlap) {
                    continue;
                }
            }
            if (now_ms < host.last_hurt_flash_ms) {
                continue;
            }
            if (require_frame_overlap &&
                env_bool("MINA_XMARK_BASIC_HURT_VISUAL_REQUIRE_FRESH_FRAME_HURT", true) &&
                !hurt_flash_matches_basic_frame(host.last_hurt_flash_ms, host_overlap_state, now_ms)) {
                continue;
            }
            if (env_bool("MINA_XMARK_BASIC_HURT_VISUAL_REQUIRE_AFTER_PROBE_START", true) &&
                g_basic_attack_probe.started_ms &&
                host.last_hurt_flash_ms + env_uint("MINA_XMARK_BASIC_HURT_VISUAL_PROBE_START_GRACE_MS", 80) <
                    g_basic_attack_probe.started_ms) {
                continue;
            }
            const unsigned long long hurt_age_ms = now_ms - host.last_hurt_flash_ms;
            if (max_hurt_age_ms && hurt_age_ms > max_hurt_age_ms) {
                continue;
            }

            XMarkRuntimeTarget target{};
            if (!runtime_target_from_visual_host(host, &target)) {
                continue;
            }
            if (basic_probe_already_marked(target.entity)) {
                continue;
            }

            float forward = 0.0f;
            float lateral = 0.0f;
            runtime_target_direction_delta_for_direction(
                target,
                player_api,
                g_basic_attack_probe.direction,
                &forward,
                &lateral);
            const bool in_contact_band =
                runtime_target_in_basic_contact_band(target, player_api, g_basic_attack_probe.direction);
            if (require_contact_band && !in_contact_band) {
                continue;
            }

            const float forward_delta = forward - preferred_forward;
            float score =
                (static_cast<float>(hurt_age_ms) * age_weight) +
                (lateral * lateral * lateral_weight) +
                (forward_delta * forward_delta * forward_weight);
            if (trained_match) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
            }
            if (!found || score < best_score) {
                found = true;
                best = host;
                best_target = target;
                best_overlap_state = host_overlap_state;
                best_score = score;
                best_hurt_age_ms = hurt_age_ms;
            }
        }
        if (found || !prefer_trained) {
            break;
        }
    }

    if (!found) {
        return false;
    }

    const unsigned int mark_repeat_ms = env_uint(
        "MINA_XMARK_BASIC_HURT_VISUAL_MARK_REPEAT_MS",
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_MARK_REPEAT_MS", 180));
    XMarkBasicHitCandidate *best_candidate = basic_hit_candidate_for_host(best, now_ms);
    if (best_candidate &&
        best_candidate->last_mark_ms &&
        now_ms >= best_candidate->last_mark_ms &&
        mark_repeat_ms &&
        now_ms <= best_candidate->last_mark_ms + mark_repeat_ms) {
        return false;
    }

    if (g_mina) {
        char message[640]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic hurt visual fallback mark dir=%s frame=%s key=0x%llX entry=%s stem=%s hurtAgeMs=%llu pos=(%.3f, %.3f, %.3f) score=%.3f\n",
            direction_name(g_basic_attack_probe.direction),
            best_overlap_state.frame[0] ? best_overlap_state.frame : "-",
            best.key,
            best.entry[0] ? best.entry : "-",
            best.stem[0] ? best.stem : "-",
            best_hurt_age_ms,
            static_cast<double>(best_target.position.x),
            static_cast<double>(best_target.position.y),
            static_cast<double>(best_target.position.z),
            static_cast<double>(best_score));
        g_mina->Log(message);
    }

    if (ensure_xmark_for_visual_host("basic-hurt-visual-fallback", best)) {
        if (best_candidate) {
            best_candidate->last_mark_ms = now_ms;
        }
        basic_probe_remember_marked(best_target.entity ? best_target.entity : best.key);
        basic_probe_note_mark_success();
        if (g_basic_attack_probe.marked_enemy_count >= max_marks) {
            g_basic_attack_probe.active = false;
        }
        return true;
    }
    return false;
}

bool basic_frame_host_recently_hurt(
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms,
    unsigned int max_hurt_age_ms,
    unsigned long long *hurt_age_out) {
    if (hurt_age_out) {
        *hurt_age_out = 0;
    }
    if (!host.active || !host.last_hurt_flash_ms || now_ms < host.last_hurt_flash_ms) {
        return false;
    }
    const unsigned long long hurt_age_ms = now_ms - host.last_hurt_flash_ms;
    if (max_hurt_age_ms && hurt_age_ms > max_hurt_age_ms) {
        return false;
    }
    if (hurt_age_out) {
        *hurt_age_out = hurt_age_ms;
    }
    return true;
}

bool hurt_flash_matches_basic_frame(
    unsigned long long hurt_ms,
    const XMarkBasicFrameState &state,
    unsigned long long now_ms) {
    if (!hurt_ms || !state.tick || now_ms < hurt_ms) {
        return false;
    }
    if (!env_bool("MINA_XMARK_BASIC_FRAME_HIT_REQUIRE_FRESH_HURT", true)) {
        return true;
    }
    const unsigned int before_grace_ms =
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_HURT_BEFORE_FRAME_GRACE_MS", 32);
    const unsigned int after_window_ms =
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_HURT_AFTER_FRAME_WINDOW_MS", 700);
    const unsigned long long earliest =
        state.tick > before_grace_ms ? state.tick - before_grace_ms : 0ull;
    const unsigned long long latest = state.tick + after_window_ms;
    return hurt_ms >= earliest && (!after_window_ms || hurt_ms <= latest);
}

XMarkBasicHitCandidate *basic_hit_candidate_for_host(
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms) {
    if (!host.active || !host.key) {
        return nullptr;
    }
    for (XMarkBasicHitCandidate &candidate : g_basic_hit_candidates) {
        if (candidate.active && candidate.key == host.key) {
            return &candidate;
        }
    }

    XMarkBasicHitCandidate *slot = nullptr;
    const unsigned int stale_ms = env_uint("MINA_XMARK_BASIC_FRAME_HIT_CANDIDATE_STALE_MS", 2000);
    for (XMarkBasicHitCandidate &candidate : g_basic_hit_candidates) {
        if (!candidate.active ||
            (stale_ms && candidate.last_overlap_ms && now_ms > candidate.last_overlap_ms + stale_ms)) {
            slot = &candidate;
            break;
        }
    }
    if (!slot) {
        slot = &g_basic_hit_candidates[0];
    }

    *slot = XMarkBasicHitCandidate{};
    slot->active = true;
    slot->key = host.key;
    return slot;
}

bool find_basic_attack_candidate_visual_host_for_health_drop(
    const XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    int direction,
    XMarkVisualEnemyHost *host_out,
    unsigned long long now_ms) {
    if (!host_out) {
        return false;
    }
    *host_out = XMarkVisualEnemyHost{};

    if (!env_bool("MINA_XMARK_BASIC_HEALTH_USE_CONTACT_CANDIDATE", !xmark_official_damage_gate_enabled())) {
        return false;
    }

    read_enemy_visual_state_file(now_ms, false);
    const Vec3 expected_visual_position = render_position_to_visual_host_space(target.position);
    const unsigned int max_overlap_age_ms =
        env_uint("MINA_XMARK_BASIC_HEALTH_CANDIDATE_OVERLAP_MAX_AGE_MS", 700);
    const unsigned int max_hurt_age_ms =
        env_uint("MINA_XMARK_BASIC_HEALTH_CANDIDATE_HURT_MAX_AGE_MS", 1000);
    const unsigned int mark_repeat_ms =
        env_uint("MINA_XMARK_BASIC_HEALTH_CANDIDATE_MARK_REPEAT_MS", 140);
    const bool require_direction =
        env_bool("MINA_XMARK_BASIC_HEALTH_CANDIDATE_REQUIRE_DIRECTION", true);
    const bool require_expected_distance =
        env_bool("MINA_XMARK_BASIC_HEALTH_CANDIDATE_REQUIRE_EXPECTED_DISTANCE", true);
    const float max_expected_distance =
        env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_MAX_DISTANCE", 12.0f);
    const float max_expected_distance_sq = max_expected_distance * max_expected_distance;
    const float age_weight = env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_AGE_WEIGHT", 0.55f);
    const float distance_weight = env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_DISTANCE_WEIGHT", 0.75f);
    const float lateral_weight = env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_LATERAL_WEIGHT", 0.35f);
    const float forward_weight = env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_FORWARD_WEIGHT", 0.08f);

    bool found = false;
    XMarkVisualEnemyHost best{};
    float best_score = FLT_MAX;
    unsigned long long best_overlap_age_ms = 0;
    unsigned long long best_hurt_age_ms = 0;
    float best_expected_distance_sq = FLT_MAX;

    for (XMarkBasicHitCandidate &candidate : g_basic_hit_candidates) {
        if (!candidate.active || !candidate.key || !candidate.last_overlap_ms) {
            continue;
        }
        if (now_ms < candidate.last_overlap_ms) {
            continue;
        }
        const unsigned long long overlap_age_ms = now_ms - candidate.last_overlap_ms;
        if (max_overlap_age_ms && overlap_age_ms > max_overlap_age_ms) {
            continue;
        }
        if (require_direction && direction >= FacingRight && direction <= FacingDown && candidate.direction != direction) {
            continue;
        }
        if (candidate.last_mark_ms &&
            now_ms >= candidate.last_mark_ms &&
            now_ms <= candidate.last_mark_ms + mark_repeat_ms) {
            continue;
        }

        XMarkVisualEnemyHost host = candidate.host;
        XMarkVisualEnemyHost current_host{};
        if (visual_enemy_host_by_key(candidate.key, &current_host, now_ms)) {
            host = current_host;
        }
        if (!host.active) {
            continue;
        }

        const float expected_dx = host.position.x - expected_visual_position.x;
        const float expected_dy = host.position.y - expected_visual_position.y;
        const float expected_distance_sq = (expected_dx * expected_dx) + (expected_dy * expected_dy);
        if (require_expected_distance && expected_distance_sq > max_expected_distance_sq) {
            continue;
        }

        XMarkRuntimeTarget visual_target{};
        visual_target.anchor = host.position;
        visual_target.position = visual_host_render_position(host);
        visual_target.visual_key = host.key;
        visual_target.visual_follow = true;
        float forward = 0.0f;
        float lateral = 0.0f;
        runtime_target_direction_delta_for_direction(visual_target, player_api, direction, &forward, &lateral);

        unsigned long long hurt_age_ms = max_hurt_age_ms ? max_hurt_age_ms : 0;
        float hurt_bonus = 0.0f;
        if (candidate.last_hurt_ms && now_ms >= candidate.last_hurt_ms) {
            hurt_age_ms = now_ms - candidate.last_hurt_ms;
            if (!max_hurt_age_ms || hurt_age_ms <= max_hurt_age_ms) {
                hurt_bonus = env_float("MINA_XMARK_BASIC_HEALTH_CANDIDATE_HURT_BONUS", 200.0f);
            }
        }

        float score =
            (static_cast<float>(overlap_age_ms) * age_weight) +
            (expected_distance_sq * distance_weight) +
            (lateral * lateral * lateral_weight) +
            (std::max(0.0f, forward) * forward_weight) -
            hurt_bonus;
        if (visual_enemy_host_matches_preferred_training(host)) {
            score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
        }

        if (!found || score < best_score) {
            found = true;
            best = host;
            best_score = score;
            best_overlap_age_ms = overlap_age_ms;
            best_hurt_age_ms = hurt_age_ms;
            best_expected_distance_sq = expected_distance_sq;
        }
    }

    if (!found) {
        return false;
    }

    best.distance_sq = best_expected_distance_sq;
    *host_out = best;
    if (g_mina && env_bool("MINA_XMARK_BASIC_HEALTH_CANDIDATE_LOG", false)) {
        char message[704]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn HP-confirmed contact candidate key=0x%llX entry=%s stem=%s dir=%s overlapAgeMs=%llu hurtAgeMs=%llu expectedDist=%.3f score=%.3f target=(%.3f, %.3f) host=(%.3f, %.3f)\n",
            best.key,
            best.entry[0] ? best.entry : "-",
            best.stem[0] ? best.stem : "-",
            direction_name(direction),
            best_overlap_age_ms,
            best_hurt_age_ms,
            static_cast<double>(std::sqrt(std::max(0.0f, best_expected_distance_sq))),
            static_cast<double>(best_score),
            static_cast<double>(expected_visual_position.x),
            static_cast<double>(expected_visual_position.y),
            static_cast<double>(best.position.x),
            static_cast<double>(best.position.y));
        g_mina->Log(message);
    }
    return true;
}

void remember_recent_basic_frame_sample(unsigned long long now_ms, const XMarkBasicFrameState &state) {
    if (!xmark_basic_frame_name_allowed(state.frame) || (!state.has_geometry && !state.has_contact)) {
        return;
    }
    XMarkRecentBasicFrameSample &sample =
        g_recent_basic_frame_samples[g_recent_basic_frame_sample_cursor %
                                     (sizeof(g_recent_basic_frame_samples) / sizeof(g_recent_basic_frame_samples[0]))];
    sample.active = true;
    sample.seen_ms = now_ms;
    sample.state = state;
    ++g_recent_basic_frame_sample_cursor;
}

bool recent_basic_frame_sample_overlaps_visual_host(
    unsigned long long now_ms,
    int direction,
    const XMarkVisualEnemyHost &host,
    unsigned int max_age_ms,
    XMarkBasicFrameState *sample_out,
    unsigned long long *age_out) {
    if (age_out) {
        *age_out = 0;
    }
    bool found = false;
    unsigned long long best_age_ms = 0;
    XMarkBasicFrameState best{};
    for (const XMarkRecentBasicFrameSample &sample : g_recent_basic_frame_samples) {
        if (!sample.active || !sample.seen_ms || now_ms < sample.seen_ms) {
            continue;
        }
        const unsigned long long age_ms = now_ms - sample.seen_ms;
        if (max_age_ms && age_ms > max_age_ms) {
            continue;
        }
        if (direction >= FacingRight && direction <= FacingDown && sample.state.direction != direction) {
            continue;
        }
        if (!basic_frame_state_overlaps_visual_host(sample.state, host)) {
            continue;
        }
        if (!found || age_ms < best_age_ms) {
            found = true;
            best_age_ms = age_ms;
            best = sample.state;
        }
    }
    if (!found) {
        return false;
    }
    if (sample_out) {
        *sample_out = best;
    }
    if (age_out) {
        *age_out = best_age_ms;
    }
    return true;
}

void write_basic_frame_hit_state(
    unsigned long long now_ms,
    const XMarkBasicFrameState &state,
    const char *source,
    unsigned int host_count,
    unsigned int overlap_count,
    unsigned int sticky_overlap_count,
    unsigned int hurt_count,
    unsigned int hurt_overlap_count,
    const XMarkVisualEnemyHost *best_host,
    bool best_overlap,
    bool best_recent_hurt,
    bool best_sticky_ready,
    unsigned long long best_hurt_age_ms,
    float best_score,
    bool marked) {
    if (!g_basic_hit_state_path[0]) {
        return;
    }
    FILE *file = nullptr;
    if (fopen_s(&file, g_basic_hit_state_path, "wb") != 0 || !file) {
        return;
    }
    std::fprintf(file, "version=1\n");
    std::fprintf(file, "nowMs=%llu\n", now_ms);
    std::fprintf(file, "source=%s\n", source && source[0] ? source : "-");
    std::fprintf(file, "frameTick=%llu\n", state.tick);
    std::fprintf(file, "draw=%llu\n", state.draw);
    std::fprintf(file, "frame=%s\n", state.frame[0] ? state.frame : "-");
    std::fprintf(file, "direction=%d\n", state.direction);
    std::fprintf(file, "directionName=%s\n", direction_name(state.direction));
    std::fprintf(file, "sideSmear=%u\n", state.side_smear ? 1u : 0u);
    std::fprintf(file, "hasGeometry=%u\n", state.has_geometry ? 1u : 0u);
    std::fprintf(file, "hasContact=%u\n", state.has_contact ? 1u : 0u);
    std::fprintf(file, "contactX=%.6f\n", static_cast<double>(state.contact_x));
    std::fprintf(file, "contactY=%.6f\n", static_cast<double>(state.contact_y));
    std::fprintf(file, "minX=%.6f\n", static_cast<double>(state.min_x));
    std::fprintf(file, "maxX=%.6f\n", static_cast<double>(state.max_x));
    std::fprintf(file, "minY=%.6f\n", static_cast<double>(state.min_y));
    std::fprintf(file, "maxY=%.6f\n", static_cast<double>(state.max_y));
    std::fprintf(file, "hostCount=%u\n", host_count);
    std::fprintf(file, "overlapCount=%u\n", overlap_count);
    std::fprintf(file, "stickyOverlapCount=%u\n", sticky_overlap_count);
    std::fprintf(file, "hurtCount=%u\n", hurt_count);
    std::fprintf(file, "hurtOverlapCount=%u\n", hurt_overlap_count);
    std::fprintf(file, "bestFound=%u\n", best_host && best_host->active ? 1u : 0u);
    std::fprintf(file, "bestOverlap=%u\n", best_overlap ? 1u : 0u);
    std::fprintf(file, "bestRecentHurt=%u\n", best_recent_hurt ? 1u : 0u);
    std::fprintf(file, "bestStickyReady=%u\n", best_sticky_ready ? 1u : 0u);
    std::fprintf(file, "bestHurtAgeMs=%llu\n", best_hurt_age_ms);
    std::fprintf(file, "bestScore=%.6f\n", static_cast<double>(best_score));
    std::fprintf(file, "marked=%u\n", marked ? 1u : 0u);
    if (best_host && best_host->active) {
        std::fprintf(file, "bestKey=0x%llX\n", best_host->key);
        std::fprintf(file, "bestEntry=%s\n", best_host->entry[0] ? best_host->entry : "-");
        std::fprintf(file, "bestStem=%s\n", best_host->stem[0] ? best_host->stem : "-");
        std::fprintf(file, "bestCatalog=%s\n", best_host->catalog[0] ? best_host->catalog : "-");
        std::fprintf(file, "bestPosX=%.6f\n", static_cast<double>(best_host->position.x));
        std::fprintf(file, "bestPosY=%.6f\n", static_cast<double>(best_host->position.y));
        std::fprintf(file, "bestHalfW=%.6f\n", static_cast<double>(best_host->half_w));
        std::fprintf(file, "bestHalfH=%.6f\n", static_cast<double>(best_host->half_h));
        std::fprintf(file, "bestLastSeenMs=%llu\n", best_host->last_seen_ms);
        std::fprintf(file, "bestLastHurtMs=%llu\n", best_host->last_hurt_flash_ms);
    }
    std::fclose(file);
}

bool maybe_trace_basic_frame_enemy_hit(
    unsigned long long now_ms,
    const XMarkBasicFrameState &state,
    const char *source) {
    if (!env_bool("MINA_XMARK_BASIC_FRAME_HIT_TRACKER_ENABLED", true) ||
        !xmark_basic_frame_name_allowed(state.frame) ||
        (!state.has_geometry && !state.has_contact)) {
        return false;
    }
    const bool same_trace_tick = state.tick == g_last_basic_frame_hit_trace_tick;
    if (same_trace_tick &&
        !env_bool("MINA_XMARK_BASIC_FRAME_HIT_RECHECK_SAME_TICK", true)) {
        return false;
    }
    if (!same_trace_tick) {
        g_last_basic_frame_hit_trace_tick = state.tick;
        remember_recent_basic_frame_sample(now_ms, state);
    }
    g_last_basic_frame_hit_eval_ms = now_ms;

    const bool has_hosts = read_enemy_visual_state_file(now_ms, false);
    const unsigned int max_hurt_age_ms = env_uint("MINA_XMARK_BASIC_FRAME_HIT_HURT_MAX_AGE_MS", 450);
    const unsigned int overlap_sticky_ms =
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_OVERLAP_STICKY_MS", 360);
    const unsigned int hurt_sticky_ms =
        env_uint("MINA_XMARK_BASIC_FRAME_HIT_HURT_STICKY_MS", 500);
    const float center_x = state.has_contact ? state.contact_x : (state.min_x + state.max_x) * 0.5f;
    const float center_y = state.has_contact ? state.contact_y : (state.min_y + state.max_y) * 0.5f;

    unsigned int overlap_count = 0;
    unsigned int sticky_overlap_count = 0;
    unsigned int hurt_count = 0;
    unsigned int hurt_overlap_count = 0;
    bool found_best = false;
    bool best_overlap = false;
    bool best_recent_hurt = false;
    unsigned long long best_hurt_age_ms = 0;
    float best_score = FLT_MAX;
    XMarkVisualEnemyHost best{};
    bool best_sticky_ready = false;
    XMarkBasicHitCandidate *best_candidate = nullptr;

    if (has_hosts) {
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            const XMarkVisualEnemyHost &host = g_visual_enemy_hosts[i];
            if (!host.active) {
                continue;
            }
            const bool overlap = basic_frame_state_overlaps_visual_host(state, host);
            XMarkBasicFrameState sticky_overlap_state{};
            unsigned long long sticky_overlap_age_ms = 0;
            const bool sticky_overlap =
                !overlap &&
                recent_basic_frame_sample_overlaps_visual_host(
                    now_ms,
                    state.direction,
                    host,
                    overlap_sticky_ms,
                    &sticky_overlap_state,
                    &sticky_overlap_age_ms);
            unsigned long long hurt_age_ms = 0;
            bool recent_hurt =
                basic_frame_host_recently_hurt(host, now_ms, max_hurt_age_ms, &hurt_age_ms);
            if (recent_hurt &&
                !hurt_flash_matches_basic_frame(host.last_hurt_flash_ms, state, now_ms)) {
                recent_hurt = false;
                hurt_age_ms = 0;
            }
            if (overlap) {
                ++overlap_count;
            }
            if (sticky_overlap) {
                ++sticky_overlap_count;
            }
            if (recent_hurt) {
                ++hurt_count;
            }
            if ((overlap || sticky_overlap) && recent_hurt) {
                ++hurt_overlap_count;
            }

            XMarkBasicHitCandidate *candidate = nullptr;
            if (overlap || sticky_overlap || recent_hurt) {
                candidate = basic_hit_candidate_for_host(host, now_ms);
            }
            if (candidate) {
                candidate->host = host;
                candidate->direction = state.direction;
                if (overlap) {
                    candidate->last_overlap_ms = now_ms;
                    candidate->last_frame_tick = state.tick;
                } else if (sticky_overlap) {
                    candidate->last_overlap_ms =
                        now_ms >= sticky_overlap_age_ms ? now_ms - sticky_overlap_age_ms : now_ms;
                    candidate->last_frame_tick = sticky_overlap_state.tick;
                }
                if (recent_hurt) {
                    candidate->last_hurt_ms = host.last_hurt_flash_ms ? host.last_hurt_flash_ms : now_ms;
                }
            }

            const bool candidate_overlap_recent =
                candidate &&
                candidate->last_overlap_ms &&
                now_ms >= candidate->last_overlap_ms &&
                (!overlap_sticky_ms || now_ms <= candidate->last_overlap_ms + overlap_sticky_ms);
            bool candidate_hurt_recent =
                candidate &&
                candidate->last_hurt_ms &&
                now_ms >= candidate->last_hurt_ms &&
                (!hurt_sticky_ms || now_ms <= candidate->last_hurt_ms + hurt_sticky_ms);
            if (candidate_hurt_recent &&
                !hurt_flash_matches_basic_frame(candidate->last_hurt_ms, state, now_ms)) {
                candidate_hurt_recent = false;
            }
            const bool sticky_ready = candidate_overlap_recent && candidate_hurt_recent;

            const float dx = host.position.x - center_x;
            const float dy = host.position.y - center_y;
            const float distance_score = (dx * dx) + (dy * dy);
            float class_score = 3000000.0f;
            if (overlap && recent_hurt) {
                class_score = 0.0f;
            } else if (sticky_overlap && recent_hurt) {
                class_score = 250000.0f;
            } else if (sticky_ready) {
                class_score = 500000.0f;
            } else if (overlap) {
                class_score = 1000000.0f;
            } else if (sticky_overlap) {
                class_score = 1250000.0f;
            } else if (recent_hurt) {
                class_score = 2000000.0f;
            }
            const float score = class_score + distance_score + (static_cast<float>(hurt_age_ms) * 0.01f);
            if (!found_best || score < best_score) {
                found_best = true;
                best = host;
                best_overlap = overlap;
                best_recent_hurt = recent_hurt;
                best_sticky_ready = sticky_ready;
                best_hurt_age_ms = hurt_age_ms;
                best_score = score;
                best_candidate = candidate;
            }
        }
    }

    bool marked = false;
    if (found_best &&
        ((best_overlap && best_recent_hurt) || best_sticky_ready) &&
        env_bool("MINA_XMARK_BASIC_FRAME_HIT_MARK_ENABLED", true) &&
        !basic_mark_requires_health_drop() &&
        g_last_basic_frame_hit_mark_tick != state.tick) {
        XMarkRuntimeTarget target{};
        const bool target_resolved = runtime_target_from_visual_host(best, &target);
        const unsigned int mark_repeat_ms = env_uint("MINA_XMARK_BASIC_FRAME_HIT_MARK_REPEAT_MS", 180);
        const bool candidate_recently_marked =
            best_candidate &&
            best_candidate->last_mark_ms &&
            now_ms >= best_candidate->last_mark_ms &&
            now_ms <= best_candidate->last_mark_ms + mark_repeat_ms;
        if (!candidate_recently_marked &&
            ensure_xmark_for_visual_host("basic-frame-hurt-contact", best)) {
            marked = true;
            g_last_basic_frame_hit_mark_tick = state.tick;
            if (best_candidate) {
                best_candidate->last_mark_ms = now_ms;
            }
            if (target_resolved && target.entity) {
                basic_probe_remember_marked(target.entity);
            }
            basic_probe_note_mark_success();
        }
    }

    write_basic_frame_hit_state(
        now_ms,
        state,
        source,
        g_visual_enemy_host_count,
        overlap_count,
        sticky_overlap_count,
        hurt_count,
        hurt_overlap_count,
        found_best ? &best : nullptr,
        best_overlap,
        best_recent_hurt,
        best_sticky_ready,
        best_hurt_age_ms,
        found_best ? best_score : 0.0f,
        marked);

    const bool should_log =
        env_bool("MINA_XMARK_BASIC_FRAME_HIT_LOG", true) &&
        (marked || hurt_overlap_count > 0 || best_sticky_ready ||
         now_ms >= g_last_basic_frame_hit_log_ms + env_uint("MINA_XMARK_BASIC_FRAME_HIT_LOG_MS", 250));
    if (g_mina && should_log) {
        g_last_basic_frame_hit_log_ms = now_ms;
        char message[768]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn basic frame hit trace source=%s frame=%s dir=%s hosts=%u overlap=%u stickyOverlap=%u hurt=%u hurtOverlap=%u bestKey=0x%llX best=%s overlapBest=%u hurtBest=%u stickyBest=%u hurtAgeMs=%llu marked=%u score=%.3f\n",
            source && source[0] ? source : "-",
            state.frame[0] ? state.frame : "-",
            direction_name(state.direction),
            g_visual_enemy_host_count,
            overlap_count,
            sticky_overlap_count,
            hurt_count,
            hurt_overlap_count,
            found_best ? best.key : 0ull,
            found_best && best.stem[0] ? best.stem : "-",
            best_overlap ? 1u : 0u,
            best_recent_hurt ? 1u : 0u,
            best_sticky_ready ? 1u : 0u,
            best_hurt_age_ms,
            marked ? 1u : 0u,
            static_cast<double>(found_best ? best_score : 0.0f));
        g_mina->Log(message);
    }

    return marked;
}

void maybe_apply_basic_attack_health_probe(unsigned long long now_ms) {
    if (!g_basic_attack_probe.active) {
        return;
    }
    const unsigned int max_marks = env_uint("MINA_XMARK_BASIC_HEALTH_MAX_MARKS", 16);
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    if (g_basic_attack_probe.contact_active) {
        maybe_mark_muriel_on_basic_contact(now_ms, player_api, max_marks);
    }
    if (env_bool("MINA_XMARK_EVENT_ARMED_HEALTH_CHECKS", true) &&
        !g_basic_attack_probe.health_check_armed) {
        return;
    }
    if (now_ms >= g_basic_attack_probe.expires_ms) {
        if (env_bool("MINA_XMARK_BASIC_HURT_VISUAL_ON_EXPIRE", !xmark_official_damage_gate_enabled())) {
            if (maybe_mark_recent_hurt_visual_host_for_basic_attack(now_ms, player_api, max_marks)) {
                g_basic_attack_probe.active = false;
                return;
            }
        }
        if (g_mina &&
            g_basic_attack_probe.marked_enemy_count == 0 &&
            env_bool("MINA_XMARK_BASIC_HEALTH_PROBE_LOG", false)) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn basic health probe expired dir=%s baselines=%u no-damage-match\n",
                direction_name(g_basic_attack_probe.direction),
                g_basic_attack_probe.baseline_count);
            g_mina->Log(message);
        }
        g_basic_attack_probe.active = false;
        return;
    }

    if (env_bool("MINA_XMARK_BASIC_HURT_VISUAL_EARLY", !xmark_official_damage_gate_enabled()) &&
        (g_basic_attack_probe.contact_active ||
         env_bool("MINA_XMARK_BASIC_HURT_VISUAL_ALLOW_WITHOUT_CONTACT", false)) &&
        maybe_mark_recent_hurt_visual_host_for_basic_attack(now_ms, player_api, max_marks) &&
        !g_basic_attack_probe.active) {
        return;
    }

    if (now_ms - g_basic_attack_probe.started_ms < env_uint("MINA_XMARK_BASIC_HEALTH_MIN_AGE_MS", 40)) {
        return;
    }

    const float min_drop = env_float("MINA_XMARK_BASIC_HEALTH_DROP_MIN", 0.01f);

    if (!g_basic_attack_probe.contact_active) {
        if (env_bool("MINA_XMARK_BASIC_HURT_VISUAL_ALLOW_WITHOUT_CONTACT", false) &&
            maybe_mark_recent_hurt_visual_host_for_basic_attack(now_ms, player_api, max_marks) &&
            !g_basic_attack_probe.active) {
            return;
        }
        return;
    }

    const unsigned int health_poll_ms =
        std::max(8u, env_uint("MINA_XMARK_BASIC_HEALTH_POLL_MS", 32));
    if (g_basic_attack_probe.last_health_poll_ms &&
        now_ms < g_basic_attack_probe.last_health_poll_ms + health_poll_ms) {
        return;
    }
    g_basic_attack_probe.last_health_poll_ms = now_ms;

    for (unsigned int i = 0; i < g_basic_attack_probe.baseline_count; ++i) {
        XMarkHealthBaseline &baseline = g_basic_attack_probe.baselines[i];

        if (baseline.official) {
            XMarkOfficialEnemyHost host{};
            XMarkRuntimeTarget target{};
            const bool direct_health_enabled =
                env_bool("MINA_XMARK_OFFICIAL_BASIC_DIRECT_HEALTH_READ", true);
            float current_health = 0.0f;
            const bool direct_health_read =
                direct_health_enabled &&
                combat_core_health_read(baseline.official_combat_core, &current_health);
            bool target_ready = false;

            if (direct_health_read) {
                target.entity = baseline.entity;
                target.official_combat_core = baseline.official_combat_core;
                target.anchor = baseline.anchor;
                target.position = baseline.anchor;
                target.health_value = current_health;
                target.health_max = baseline.health_max;
                target.health_like = true;
                target.official_follow = true;
                target.render_half_w = xmark_default_render_half_w();
                target.render_half_h = xmark_default_render_half_h();
                runtime_target_direction_delta_for_direction(
                    target,
                    player_api,
                    g_basic_attack_probe.direction,
                    &target.forward_delta,
                    &target.lateral_delta);
                target.facing_match = true;
                copy_visual_token(target.visual_stem, sizeof(target.visual_stem), "official-combat-core-direct");
                copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), "ModAPI");
                target_ready = true;

                if (current_health + min_drop <= baseline.health_value &&
                    official_enemy_host_by_combat_core(baseline.official_combat_core, &host)) {
                    XMarkRuntimeTarget resolved_target{};
                    if (runtime_target_from_official_enemy_host(host, &resolved_target)) {
                        target = resolved_target;
                        target.health_value = current_health;
                    }
                }
                if (current_health + min_drop <= baseline.health_value) {
                    Vec3 live_center{};
                    float live_half_w = 0.0f;
                    float live_half_h = 0.0f;
                    if (official_enemy_live_contact_bounds_read(
                            target.entity,
                            target.official_combat_core,
                            &live_center,
                            &live_half_w,
                            &live_half_h)) {
                        target.anchor = live_center;
                        target.position = live_center;
                        target.contact_half_w = live_half_w;
                        target.contact_half_h = live_half_h;
                    } else if (official_entity_world_position_read(target.entity, &live_center)) {
                        target.anchor = live_center;
                        target.position = live_center;
                    }
                }
            } else if (official_enemy_host_by_combat_core(baseline.official_combat_core, &host)) {
                target_ready = runtime_target_from_official_enemy_host(host, &target);
            }
            if (!target_ready) {
                continue;
            }
            if (mark_basic_probe_target(
                    "official-basic-health-delta",
                    baseline,
                    target,
                    player_api,
                    min_drop,
                    max_marks,
                    g_official_enemy_host_count) &&
                !g_basic_attack_probe.active) {
                return;
            }
            continue;
        }

        XMarkRuntimeTarget target{};
        if (!runtime_target_from_basic_baseline(baseline, player_api, &target)) {
            continue;
        }
        if (mark_basic_probe_target(
                "basic-health-delta",
                baseline,
                target,
                player_api,
                min_drop,
                max_marks,
                g_basic_attack_probe.baseline_count) &&
            !g_basic_attack_probe.active) {
                return;
        }
    }

    if (env_bool("MINA_XMARK_BASIC_HURT_VISUAL_AFTER_HEALTH_MISS", !xmark_official_damage_gate_enabled()) &&
        maybe_mark_recent_hurt_visual_host_for_basic_attack(now_ms, player_api, max_marks) &&
        !g_basic_attack_probe.active) {
        return;
    }

    if (!env_bool("MINA_XMARK_BASIC_HEALTH_RESCAN_CURRENT_TARGETS", !xmark_official_damage_gate_enabled())) {
        return;
    }
    const unsigned int rescan_ms = env_uint("MINA_XMARK_BASIC_HEALTH_RESCAN_MS", 55);
    if (now_ms - g_basic_attack_probe.last_runtime_scan_ms < rescan_ms) {
        return;
    }
    g_basic_attack_probe.last_runtime_scan_ms = now_ms;

    constexpr unsigned int kMaxTargets = 96;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int reads = 0;
    Vec3 scan_player_api{0.0f, 0.0f, 0.0f};
    const unsigned int count = collect_runtime_targets(targets, kMaxTargets, &reads, &scan_player_api, nullptr);
    if (count == 0) {
        return;
    }

    for (unsigned int target_index = 0; target_index < count; ++target_index) {
        XMarkRuntimeTarget &target = targets[target_index];
        if (!target.health_like || basic_probe_already_marked(target.entity)) {
            continue;
        }
        for (unsigned int baseline_index = 0; baseline_index < g_basic_attack_probe.baseline_count; ++baseline_index) {
            XMarkHealthBaseline &baseline = g_basic_attack_probe.baselines[baseline_index];
            if (!basic_probe_baseline_matches_target(baseline, target)) {
                continue;
            }
            if (mark_basic_probe_target(
                    "basic-health-delta-rescan",
                    baseline,
                    target,
                    scan_player_api,
                    min_drop,
                    max_marks,
                    reads) &&
                !g_basic_attack_probe.active) {
                return;
            }
            break;
        }
    }

    if (!env_bool("MINA_XMARK_BASIC_HEALTH_HISTORY_FALLBACK", false)) {
        return;
    }

    for (unsigned int target_index = 0; target_index < count; ++target_index) {
        XMarkRuntimeTarget &target = targets[target_index];
        if (!target.health_like || basic_probe_already_marked(target.entity)) {
            continue;
        }

        float history_health = 0.0f;
        if (!runtime_health_history_for_target(target, &history_health)) {
            continue;
        }

        XMarkHealthBaseline history_baseline{};
        history_baseline.entity = target.entity;
        history_baseline.health_offset = target.health_offset;
        history_baseline.anchor_offset = target.anchor_offset;
        history_baseline.health_kind = target.health_kind;
        history_baseline.anchor = target.anchor;
        history_baseline.health_value = history_health;
        history_baseline.health_max = target.health_max;
        if (mark_basic_probe_target(
                "basic-health-history-rescan",
                history_baseline,
                target,
                scan_player_api,
                min_drop,
                max_marks,
                reads) &&
            !g_basic_attack_probe.active) {
            return;
        }
    }
}

bool test_enemy_pre_spawn_seen(uintptr_t entity) {
    if (!entity) {
        return true;
    }
    const unsigned int count = g_test_enemy_pre_spawn_count <
            static_cast<unsigned int>(sizeof(g_test_enemy_pre_spawn_entities) / sizeof(g_test_enemy_pre_spawn_entities[0]))
        ? g_test_enemy_pre_spawn_count
        : static_cast<unsigned int>(sizeof(g_test_enemy_pre_spawn_entities) / sizeof(g_test_enemy_pre_spawn_entities[0]));
    for (unsigned int i = 0; i < count; ++i) {
        if (g_test_enemy_pre_spawn_entities[i] == entity) {
            return true;
        }
    }
    return false;
}

void capture_test_enemy_pre_spawn_targets() {
    g_test_enemy_pre_spawn_count = 0;
    XMarkRuntimeTarget targets[160]{};
    unsigned int reads = 0;
    g_runtime_collect_include_non_health_for_test = true;
    const unsigned int count = collect_runtime_targets(
        targets,
        static_cast<unsigned int>(sizeof(targets) / sizeof(targets[0])),
        &reads,
        nullptr,
        nullptr);
    g_runtime_collect_include_non_health_for_test = false;

    const unsigned int max_entries = static_cast<unsigned int>(sizeof(g_test_enemy_pre_spawn_entities) / sizeof(g_test_enemy_pre_spawn_entities[0]));
    for (unsigned int i = 0; i < count && g_test_enemy_pre_spawn_count < max_entries; ++i) {
        g_test_enemy_pre_spawn_entities[g_test_enemy_pre_spawn_count++] = targets[i].entity;
    }

    if (g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn test enemy pre-spawn snapshot targets=%u stored=%u reads=%u\n",
            count,
            g_test_enemy_pre_spawn_count,
            reads);
        g_mina->Log(message);
    }
}

void arm_test_enemy_runtime_resolve(unsigned long long now_ms) {
    g_test_enemy_resolve_pending = true;
    g_last_test_enemy_resolve_scan_ms = 0;
    g_test_enemy_resolve_until_ms = now_ms + env_uint("MINA_XMARK_TEST_ENEMY_RESOLVE_WINDOW_MS", 2000);
}

bool runtime_target_from_entity_pointer(
    uintptr_t entity,
    const Vec3 &expected_position,
    int direction,
    XMarkRuntimeTarget *target_out);

void maybe_mark_attachment_lab_target(const XMarkRuntimeTarget &target) {
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) ||
        !env_bool("MINA_XMARK_ATTACHMENT_LAB_AUTO_MARK_ON_RESOLVE", true)) {
        return;
    }
    if (g_attachment_lab_marked && g_attachment_lab_target == target.entity) {
        return;
    }

    clear_xmark_attachments_for_lab();
    g_attachment_lab_target = target.entity;
    g_attachment_lab_anchor_offset = target.anchor_offset;
    g_attachment_lab_base_anchor_x = target.anchor.x;
    g_attachment_lab_base_anchor_y = target.anchor.y;
    g_attachment_lab_base_anchor_z = target.anchor.z;
    g_attachment_lab_marked = spawn_tracked_xmark_for_target("attachment-lab-resolved-target", target);

    if (g_mina) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn attachment lab mark target=0x%p anchorOffset=0x%X anchor=(%.3f, %.3f, %.3f) marked=%u\n",
            reinterpret_cast<void *>(target.entity),
            target.anchor_offset,
            static_cast<double>(target.anchor.x),
            static_cast<double>(target.anchor.y),
            static_cast<double>(target.anchor.z),
            g_attachment_lab_marked ? 1u : 0u);
        g_mina->Log(message);
    }
}

bool mark_last_test_enemy_for_attachment_lab(const char *reason) {
    const unsigned long long now_ms = GetTickCount64();
    if (env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_PREFER_VISUAL", true)) {
        XMarkVisualEnemyHost visual_host{};
        if (find_nearest_visual_enemy_host(&visual_host, now_ms) &&
            spawn_tracked_xmark_for_visual_host("key-M-visual-host", visual_host)) {
            return true;
        }
        if (env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_REQUIRE_VISUAL", false)) {
            if (g_mina && env_bool("MINA_XMARK_VISUAL_HOST_MARK_LOG", true)) {
                g_mina->Log("XMarkBurn manual mark visual-host target missed; visual target required, not falling back.\n");
            }
            return false;
        }
    }
    const unsigned int max_age_ms = env_uint("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_MAX_AGE_MS", 12000);
    const bool resolved_recently =
        g_last_test_enemy_resolved_ms &&
        (!max_age_ms || now_ms - g_last_test_enemy_resolved_ms <= max_age_ms);
    if (!g_last_test_enemy_entity || !resolved_recently) {
        if (env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_VISUAL_FALLBACK", true)) {
            XMarkVisualEnemyHost visual_host{};
            if (find_nearest_visual_enemy_host(&visual_host, now_ms) &&
                spawn_tracked_xmark_for_visual_host("key-M-visual-host", visual_host)) {
                return true;
            }
        }
        if (g_mina) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn attachment lab manual mark skipped: no recently resolved test enemy pointer entity=0x%p resolvedAgeMs=%llu.\n",
                reinterpret_cast<void *>(g_last_test_enemy_entity),
                g_last_test_enemy_resolved_ms ? now_ms - g_last_test_enemy_resolved_ms : 0ull);
            g_mina->Log(message);
        }
        return false;
    }

    XMarkRuntimeTarget target{};
    if (!runtime_target_from_entity_pointer(
            g_last_test_enemy_entity,
            g_last_test_enemy_position,
            g_last_direction,
            &target)) {
        if (env_bool("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_VISUAL_FALLBACK", true)) {
            XMarkVisualEnemyHost visual_host{};
            if (find_nearest_visual_enemy_host(&visual_host, now_ms) &&
                spawn_tracked_xmark_for_visual_host("key-M-visual-host-resolve-miss", visual_host)) {
                return true;
            }
        }
        if (g_mina) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn attachment lab manual mark missed reason=%s entity=0x%p expected=(%.3f, %.3f, %.3f).\n",
                reason ? reason : "<none>",
                reinterpret_cast<void *>(g_last_test_enemy_entity),
                static_cast<double>(g_last_test_enemy_position.x),
                static_cast<double>(g_last_test_enemy_position.y),
                static_cast<double>(g_last_test_enemy_position.z));
            g_mina->Log(message);
        }
        return false;
    }

    bool marked = false;
    if (env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false)) {
        maybe_mark_attachment_lab_target(target);
        marked = g_attachment_lab_marked;
    } else {
        marked = spawn_tracked_xmark_for_target(reason, target);
    }
    if (g_mina) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn attachment lab manual mark resolved reason=%s target=0x%p source=0x%p+0x%llX anchorOffset=0x%X pos=(%.3f, %.3f, %.3f) marked=%u.\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(target.entity),
            reinterpret_cast<void *>(target.source_base),
            static_cast<unsigned long long>(target.source_offset),
            target.anchor_offset,
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            marked ? 1u : 0u);
        g_mina->Log(message);
    }
    return marked;
}

void update_attachment_lab_target_motion(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) ||
        !env_bool("MINA_XMARK_ATTACHMENT_LAB_DRIVE_TARGET", false) ||
        !g_attachment_lab_target ||
        !g_attachment_lab_anchor_offset ||
        !probable_heap_object(g_attachment_lab_target)) {
        return;
    }
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_DRIVE_CHILD_TARGET", false) &&
        g_last_test_enemy_entity &&
        g_attachment_lab_target != g_last_test_enemy_entity) {
        if (g_mina && env_bool("MINA_XMARK_ATTACHMENT_LAB_DRIVE_SKIP_LOG", true) &&
            now_ms - g_attachment_lab_last_log_ms >= env_uint("MINA_XMARK_ATTACHMENT_UPDATE_LOG_MS", 300)) {
            g_attachment_lab_last_log_ms = now_ms;
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn attachment lab drive skipped: target=0x%p root=0x%p child-driving disabled.\n",
                reinterpret_cast<void *>(g_attachment_lab_target),
                reinterpret_cast<void *>(g_last_test_enemy_entity));
            g_mina->Log(message);
        }
        return;
    }

    const float amplitude_x = env_float("MINA_XMARK_ATTACHMENT_LAB_DRIVE_AMPLITUDE_X", 2.0f);
    const float amplitude_y = env_float("MINA_XMARK_ATTACHMENT_LAB_DRIVE_AMPLITUDE_Y", 0.0f);
    const float period_ms = env_float("MINA_XMARK_ATTACHMENT_LAB_DRIVE_PERIOD_MS", 2200.0f);
    if (period_ms <= 0.0f) {
        return;
    }

    constexpr float pi = 3.14159265358979323846f;
    const float phase = (static_cast<float>(now_ms % static_cast<unsigned long long>(period_ms)) / period_ms) * 2.0f * pi;
    Vec3 driven_anchor{
        g_attachment_lab_base_anchor_x + std::sin(phase) * amplitude_x,
        g_attachment_lab_base_anchor_y + std::cos(phase) * amplitude_y,
        g_attachment_lab_base_anchor_z,
    };
    write_position_pair(g_attachment_lab_target, g_attachment_lab_anchor_offset, driven_anchor);
}

bool runtime_target_from_entity_pointer(
    uintptr_t entity,
    const Vec3 &expected_position,
    int direction,
    XMarkRuntimeTarget *target_out) {
    if (!target_out || !entity || !probable_heap_object(entity)) {
        return false;
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    auto make_target = [&](uintptr_t target_ptr, uintptr_t source_base, uintptr_t source_offset, const Vec3 &anchor, unsigned int anchor_offset, XMarkRuntimeTarget *out) -> bool {
        if (!out) {
            return false;
        }
        XMarkRuntimeTarget target{};
        target.entity = target_ptr;
        target.source_base = source_base;
        target.source_offset = source_offset;
        target.anchor = anchor;
        target.position = runtime_anchor_to_spawn_position(anchor);
        const float dx = anchor.x - player_api.x;
        const float dy = anchor.y - player_api.y;
        target.distance_sq = (dx * dx) + (dy * dy);
        target.anchor_offset = anchor_offset;
        runtime_target_healthlike(
            target.entity,
            &target.health_offset,
            &target.health_value,
            &target.health_max,
            &target.health_kind);
        target.health_like = target.health_max > 0.0f;
        runtime_target_direction_delta_for_direction(target, player_api, direction, &target.forward_delta, &target.lateral_delta);
        target.facing_match = true;
        target.score = xy_distance_sq(target.position, expected_position);
        *out = target;
        return true;
    };

    XMarkRuntimeTarget best_target{};
    bool has_root = false;
    bool has_child = false;
    float best_child_score = FLT_MAX;

    Vec3 anchor{};
    unsigned int anchor_offset = 0;
    bool resolved = runtime_object_anchor_scan_near_position(
        entity,
        player_api,
        expected_position,
        &anchor,
        &anchor_offset,
        "direct-spawn-pointer");
    if (!resolved) {
        resolved = runtime_object_anchor(entity, player_api, &anchor, &anchor_offset, 0.0f, 192.0f);
    }
    if (resolved) {
        has_root = make_target(entity, entity, 0, anchor, anchor_offset, &best_target);
    }

    const unsigned int child_scan_bytes = env_uint("MINA_XMARK_ATTACHMENT_LAB_ENTITY_CHILD_SCAN_BYTES", 0x4000);
    const unsigned int capped_child_scan_bytes = child_scan_bytes > 0x10000u ? 0x10000u : child_scan_bytes;
    float best_score = FLT_MAX;
    uintptr_t best_ptr = 0;
    unsigned int best_source_offset = 0;
    unsigned int best_anchor_offset = 0;
    Vec3 best_anchor{};

    for (unsigned int source_offset = 0; source_offset < capped_child_scan_bytes; source_offset += 8) {
        uintptr_t child = 0;
        if (!safe_read_ptr(entity + source_offset, &child) ||
            child == entity ||
            !probable_heap_object(child) ||
            pointer_is_rejected_runtime_target(child) ||
            pointer_is_spawned_effect(child)) {
            continue;
        }

        Vec3 child_anchor{};
        unsigned int child_anchor_offset = 0;
        bool child_resolved = runtime_object_anchor_scan_near_position(
            child,
            player_api,
            expected_position,
            &child_anchor,
            &child_anchor_offset,
            "direct-spawn-child");
        if (!child_resolved) {
            child_resolved = runtime_object_anchor(child, player_api, &child_anchor, &child_anchor_offset, 0.0f, 192.0f);
        }
        if (!child_resolved) {
            continue;
        }

        const Vec3 child_position = runtime_anchor_to_spawn_position(child_anchor);
        const float score = xy_distance_sq(child_position, expected_position);
        const float child_max_dist = env_float("MINA_XMARK_ATTACHMENT_LAB_CHILD_MAX_DIST", 24.0f);
        if (score > child_max_dist * child_max_dist) {
            continue;
        }
        if (score < best_score) {
            best_score = score;
            best_ptr = child;
            best_source_offset = source_offset;
            best_anchor_offset = child_anchor_offset;
            best_anchor = child_anchor;
        }
    }

    if (best_ptr) {
        XMarkRuntimeTarget child_target{};
        has_child = make_target(best_ptr, entity, best_source_offset, best_anchor, best_anchor_offset, &child_target);
        if (has_child) {
            best_child_score = child_target.score;
        }
        if (g_mina && env_bool("MINA_XMARK_ATTACHMENT_LAB_CHILD_RESOLVE_LOG", true)) {
            char message[512]{};
            const Vec3 best_position = runtime_anchor_to_spawn_position(best_anchor);
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn attachment lab child resolve entity=0x%p child=0x%p sourceOffset=0x%X anchorOffset=0x%X pos=(%.3f, %.3f, %.3f) score=%.3f\n",
                reinterpret_cast<void *>(entity),
                reinterpret_cast<void *>(best_ptr),
                best_source_offset,
                best_anchor_offset,
                static_cast<double>(best_position.x),
                static_cast<double>(best_position.y),
                static_cast<double>(best_position.z),
                static_cast<double>(best_score));
            g_mina->Log(message);
        }
        if (has_child && env_bool("MINA_XMARK_ATTACHMENT_LAB_PREFER_CHILD", true)) {
            best_target = child_target;
        }
    }

    if (!has_root && !has_child) {
        return false;
    }
    if (has_child && !env_bool("MINA_XMARK_ATTACHMENT_LAB_PREFER_CHILD", true) && !has_root) {
        XMarkRuntimeTarget child_target{};
        if (make_target(best_ptr, entity, best_source_offset, best_anchor, best_anchor_offset, &child_target)) {
            best_target = child_target;
        }
    }
    if (g_mina && env_bool("MINA_XMARK_ATTACHMENT_LAB_RESOLVE_CHOICE_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn attachment lab resolve choice root=%u child=%u chosen=0x%p source=0x%p+0x%llX anchorOffset=0x%X pos=(%.3f, %.3f, %.3f) score=%.3f\n",
            has_root ? 1u : 0u,
            has_child ? 1u : 0u,
            reinterpret_cast<void *>(best_target.entity),
            reinterpret_cast<void *>(entity),
            static_cast<unsigned long long>(best_target.source_offset),
            best_target.anchor_offset,
            static_cast<double>(best_target.position.x),
            static_cast<double>(best_target.position.y),
            static_cast<double>(best_target.position.z),
            static_cast<double>(best_target.score));
        g_mina->Log(message);
    }
    *target_out = best_target;
    return true;
}

void maybe_resolve_test_enemy_runtime_target(unsigned long long now_ms) {
    if (!g_test_enemy_resolve_pending) {
        return;
    }
    if (now_ms > g_test_enemy_resolve_until_ms) {
        g_test_enemy_resolve_pending = false;
        g_last_test_enemy_entity = 0;
        g_last_test_enemy_resolved_ms = 0;
        if (g_mina) {
            g_mina->Log("XMarkBurn test enemy runtime resolve expired without a new target.\n");
        }
        return;
    }
    const unsigned int scan_interval_ms = env_uint("MINA_XMARK_TEST_ENEMY_RESOLVE_SCAN_MS", 90);
    if (g_last_test_enemy_resolve_scan_ms && now_ms - g_last_test_enemy_resolve_scan_ms < scan_interval_ms) {
        return;
    }
    g_last_test_enemy_resolve_scan_ms = now_ms;

    if (env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) && g_last_test_enemy_entity) {
        XMarkRuntimeTarget direct_target{};
        if (runtime_target_from_entity_pointer(
                g_last_test_enemy_entity,
                g_last_test_enemy_position,
                g_last_direction,
                &direct_target)) {
            g_test_enemy_resolve_pending = false;
            maybe_mark_attachment_lab_target(direct_target);
            if (g_mina) {
                char message[640]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn attachment lab direct resolve target=0x%p healthLike=%u anchorOffset=0x%X anchor=(%.3f, %.3f, %.3f) pos=(%.3f, %.3f, %.3f) score=%.3f\n",
                    reinterpret_cast<void *>(direct_target.entity),
                    direct_target.health_like ? 1u : 0u,
                    direct_target.anchor_offset,
                    static_cast<double>(direct_target.anchor.x),
                    static_cast<double>(direct_target.anchor.y),
                    static_cast<double>(direct_target.anchor.z),
                    static_cast<double>(direct_target.position.x),
                    static_cast<double>(direct_target.position.y),
                    static_cast<double>(direct_target.position.z),
                    static_cast<double>(direct_target.score));
                g_mina->Log(message);
            }
            return;
        }
        if (g_mina && env_bool("MINA_XMARK_ATTACHMENT_LAB_DIRECT_RESOLVE_LOG", true)) {
            g_mina->Log("XMarkBurn attachment lab direct resolve missed; falling back to runtime target scan.\n");
        }
    }

    XMarkRuntimeTarget targets[192]{};
    unsigned int reads = 0;
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    g_runtime_collect_include_non_health_for_test = true;
    const unsigned int count = collect_runtime_targets(
        targets,
        static_cast<unsigned int>(sizeof(targets) / sizeof(targets[0])),
        &reads,
        &player_api,
        nullptr);
    g_runtime_collect_include_non_health_for_test = false;

    int best_index = -1;
    float best_score = FLT_MAX;
    const float max_radius = env_float("MINA_XMARK_TEST_ENEMY_RESOLVE_RADIUS", 96.0f);
    const float max_radius_sq = max_radius * max_radius;
    for (unsigned int i = 0; i < count; ++i) {
        const XMarkRuntimeTarget &target = targets[i];
        if (test_enemy_pre_spawn_seen(target.entity)) {
            continue;
        }
        const float dx_spawn = target.position.x - g_last_test_enemy_position.x;
        const float dy_spawn = target.position.y - g_last_test_enemy_position.y;
        const float dist_spawn_sq = (dx_spawn * dx_spawn) + (dy_spawn * dy_spawn);
        if (dist_spawn_sq > max_radius_sq) {
            continue;
        }
        float score = dist_spawn_sq;
        if (target.health_like) {
            score -= env_float("MINA_XMARK_TEST_ENEMY_HEALTHLIKE_BONUS", 200.0f);
        }
        if (score < best_score) {
            best_score = score;
            best_index = static_cast<int>(i);
        }
    }

    if (best_index < 0) {
        return;
    }

    const XMarkRuntimeTarget &target = targets[best_index];
    g_last_test_enemy_entity = target.entity;
    g_last_test_enemy_resolved_ms = now_ms;
    g_test_enemy_resolve_pending = false;
    if (g_mina) {
        char message[640]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn test enemy runtime resolved target=0x%p healthLike=%u health=%.3f/%.3f anchorOffset=0x%X anchor=(%.3f, %.3f, %.3f) pos=(%.3f, %.3f, %.3f) count=%u reads=%u score=%.3f\n",
            reinterpret_cast<void *>(target.entity),
            target.health_like ? 1u : 0u,
            static_cast<double>(target.health_value),
            static_cast<double>(target.health_max),
            target.anchor_offset,
            static_cast<double>(target.anchor.x),
            static_cast<double>(target.anchor.y),
            static_cast<double>(target.anchor.z),
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            count,
            reads,
            static_cast<double>(best_score));
        g_mina->Log(message);
    }
    maybe_mark_attachment_lab_target(target);
}

Vec3 xmark_enemy_test_position() {
    const Vec3 base = official_spawn_position();
    const float side_x = env_float("MINA_XMARK_G_ENEMY_TEST_SIDE_OFFSET_X", 4.5f);
    const float side_y = env_float("MINA_XMARK_G_ENEMY_TEST_SIDE_OFFSET_Y", -0.25f);
    const float vertical_x = env_float("MINA_XMARK_G_ENEMY_TEST_VERTICAL_OFFSET_X", 0.0f);
    const float vertical_y = env_float("MINA_XMARK_G_ENEMY_TEST_VERTICAL_OFFSET_Y", 4.0f);
    return offset_from_direction(base, g_last_direction, side_x, side_y, vertical_x, vertical_y);
}

bool gameplay_state_can_spawn_test_enemy(int game_state) {
    return game_state >= GAMESTATE_INTROBEACH && game_state <= GAMESTATE_GYM_WORLDLOADTEST2;
}

bool test_enemy_uses_api_spawn() {
    return env_bool(
        "MINA_XMARK_TEST_ENEMY_USE_API_SPAWN",
        true);
}

bool test_enemy_direct_spawn_enabled() {
    return env_bool("MINA_XMARK_TEST_ENEMY_DIRECT_ENABLED", false);
}

Vec3 test_enemy_spawn_position() {
    const Vec3 base = official_spawn_position();
    const float side_x = env_float("MINA_XMARK_TEST_ENEMY_SIDE_OFFSET_X", -3.0f);
    const float side_y = env_float("MINA_XMARK_TEST_ENEMY_SIDE_OFFSET_Y", 0.5f);
    const float vertical_x = env_float("MINA_XMARK_TEST_ENEMY_VERTICAL_OFFSET_X", 0.0f);
    const float vertical_y = env_float("MINA_XMARK_TEST_ENEMY_VERTICAL_OFFSET_Y", 3.0f);
    Vec3 position = offset_from_direction(base, g_last_direction, side_x, side_y, vertical_x, vertical_y);
    position.z += env_float("MINA_XMARK_TEST_ENEMY_Z_OFFSET", 0.0f);
    return position;
}

bool test_enemy_spawn_context_ready(int game_state, unsigned int room_index, float room_time) {
    const bool use_api_spawn = test_enemy_uses_api_spawn();
    if (!g_test_enemy_harness_enabled || !g_native_spawn_enabled) {
        return false;
    }
    if (!use_api_spawn && (!g_native_direct_enabled || !test_enemy_direct_spawn_enabled())) {
        return false;
    }
    if (!gameplay_state_can_spawn_test_enemy(game_state) || is_pseudo_room(room_index)) {
        return false;
    }
    if (room_time < env_float("MINA_XMARK_TEST_ENEMY_MIN_ROOM_TIME", 1.0f)) {
        return false;
    }
    if (use_api_spawn) {
        return g_mina && g_mina->SpawnEntity;
    }
    if (!player_entity()) {
        return false;
    }
    return entity_manager() != 0;
}

bool spawn_test_enemy_near_mina(const char *reason) {
    g_test_enemy_type = env_uint("MINA_XMARK_TEST_ENEMY_TYPE", g_test_enemy_type);
    const Vec3 position = test_enemy_spawn_position();
    capture_test_enemy_pre_spawn_targets();
    g_last_test_enemy_position = position;
    if (env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false)) {
        g_attachment_lab_marked = false;
        g_attachment_lab_target = 0;
        g_attachment_lab_anchor_offset = 0;
        g_attachment_lab_base_anchor_x = 0.0f;
        g_attachment_lab_base_anchor_y = 0.0f;
        g_attachment_lab_base_anchor_z = 0.0f;
        g_attachment_lab_last_log_ms = 0;
    }

    const bool use_api_spawn = test_enemy_uses_api_spawn();
    if (use_api_spawn) {
        if (!g_mina || !g_mina->SpawnEntity) {
            return false;
        }
        if (native_spawn_limited()) {
            if (g_mina) {
                g_mina->Log("XMarkBurn test enemy API spawn skipped: native spawn limit reached.\n");
            }
            return false;
        }
        ++g_native_spawn_count;
        const unsigned int request_number = ++g_test_enemy_spawn_count;
        g_last_test_enemy_entity = 0;
        g_last_test_enemy_resolved_ms = 0;
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn API entity request #%u reason=%s label=test-enemy type=%u/%s desiredPos=(%.3f, %.3f, %.3f) resolveWindowMs=%u\n",
            request_number,
            reason ? reason : "<none>",
            g_test_enemy_type,
            entity_type_name(g_test_enemy_type),
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            env_uint("MINA_XMARK_TEST_ENEMY_RESOLVE_WINDOW_MS", 2000));
        g_mina->Log(message);
        __try {
            g_mina->SpawnEntity(g_test_enemy_type);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_mina->Log("XMarkBurn test enemy API SpawnEntity raised an exception.\n");
            return false;
        }
        g_mina->SetSharedValue("xmark.testEnemy.type", static_cast<uintptr_t>(g_test_enemy_type));
        arm_test_enemy_runtime_resolve(GetTickCount64());
        return true;
    }

    if (!test_enemy_direct_spawn_enabled()) {
        if (g_mina) {
            g_mina->Log("XMarkBurn test enemy direct native factory spawn skipped: disabled by crash guard.\n");
        }
        return false;
    }

    const bool spawned = spawn_direct_entity_at(
        "test-enemy",
        reason,
        g_test_enemy_type,
        position,
        &g_last_test_enemy_entity);
    if (spawned) {
        arm_test_enemy_runtime_resolve(GetTickCount64());
        if (env_bool("MINA_XMARK_ATTACHMENT_LAB_ENABLED", false) &&
            env_bool("MINA_XMARK_ATTACHMENT_LAB_MARK_IMMEDIATELY_AFTER_SPAWN", false)) {
            mark_last_test_enemy_for_attachment_lab(reason);
        }
    }
    return spawned;
}

void maybe_force_default_claymore(unsigned long long now_ms, int game_state, unsigned int room_index) {
    if (!g_mina || !env_bool("MINA_XMARK_FORCE_CLAYMORE_DEFAULT", false)) {
        return;
    }
    if (!gameplay_state_can_spawn_test_enemy(game_state) || is_pseudo_room(room_index)) {
        return;
    }
    if (!g_mina->PlayerSetWeapon_ItemType) {
        return;
    }

    const int target_item = static_cast<int>(env_uint("MINA_XMARK_FORCE_CLAYMORE_ITEM_TYPE", kItemType_Hammer));
    const int target_level = static_cast<int>(env_uint("MINA_XMARK_FORCE_CLAYMORE_LEVEL", 1));
    int before_item = -1;
    int before_index = -1;
    if (g_mina->PlayerGetWeapon_ItemType) {
        __try {
            before_item = g_mina->PlayerGetWeapon_ItemType();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            before_item = -1;
        }
    }
    if (g_mina->PlayerGetWeapon_WeaponIndex) {
        __try {
            before_index = g_mina->PlayerGetWeapon_WeaponIndex();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            before_index = -1;
        }
    }

    bool changed = before_item != target_item;
    if (changed) {
        __try {
            g_mina->PlayerSetWeapon_ItemType(target_item);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_mina) {
                g_mina->Log("XMarkBurn force Claymore PlayerSetWeapon_ItemType raised an exception.\n");
            }
            return;
        }
    }
    if (target_level >= 0 && g_mina->PlayerSetWeaponLevel_ItemType) {
        __try {
            g_mina->PlayerSetWeaponLevel_ItemType(target_item, target_level);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_mina) {
                g_mina->Log("XMarkBurn force Claymore PlayerSetWeaponLevel_ItemType raised an exception.\n");
            }
        }
    }
    if (env_bool("MINA_XMARK_FORCE_CLAYMORE_WEAPON_INDEX_ENABLED", false) &&
        g_mina->PlayerSetWeapon_WeaponIndex) {
        const int target_index = static_cast<int>(env_uint("MINA_XMARK_FORCE_CLAYMORE_WEAPON_INDEX", 1));
        __try {
            g_mina->PlayerSetWeapon_WeaponIndex(target_index);
            changed = changed || before_index != target_index;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (g_mina) {
                g_mina->Log("XMarkBurn force Claymore PlayerSetWeapon_WeaponIndex raised an exception.\n");
            }
        }
    }

    const unsigned int log_interval_ms = env_uint("MINA_XMARK_FORCE_CLAYMORE_LOG_MS", 2000);
    const bool verbose_log = env_bool("MINA_XMARK_FORCE_CLAYMORE_VERBOSE_LOG", false);
    if (g_mina &&
        (changed ||
         (verbose_log &&
          (!g_last_force_claymore_log_ms ||
           now_ms - g_last_force_claymore_log_ms >= log_interval_ms)))) {
        g_last_force_claymore_log_ms = now_ms;
        int after_item = before_item;
        int after_index = before_index;
        if (g_mina->PlayerGetWeapon_ItemType) {
            __try {
                after_item = g_mina->PlayerGetWeapon_ItemType();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                after_item = -1;
            }
        }
        if (g_mina->PlayerGetWeapon_WeaponIndex) {
            __try {
                after_index = g_mina->PlayerGetWeapon_WeaponIndex();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                after_index = -1;
            }
        }
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn force Claymore active beforeItem=%d afterItem=%d beforeIndex=%d afterIndex=%d targetItem=%d level=%d changed=%u\n",
            before_item,
            after_item,
            before_index,
            after_index,
            target_item,
            target_level,
            changed ? 1u : 0u);
        g_mina->Log(message);
    }
}

bool spawn_direct_xmark_enemy_test(const char *reason) {
    const unsigned long long now_ms = GetTickCount64();
    if (env_bool("MINA_XMARK_G_PREFER_OFFICIAL_ENEMY_STATE", true) &&
        g_official_enemy_snapshot_valid &&
        g_official_enemy_host_count > 0) {
        Vec3 player_api{0.0f, 0.0f, 0.0f};
        if (g_mina && g_mina->PlayerGetPos) {
            g_mina->PlayerGetPos(&player_api.x, &player_api.y);
        } else {
            player_api = official_spawn_position();
        }

        const float max_dist = env_float("MINA_XMARK_G_OFFICIAL_MAX_DIST", 256.0f);
        const float max_dist_sq = max_dist > 0.0f ? max_dist * max_dist : FLT_MAX;
        const XMarkOfficialEnemyHost *best_host = nullptr;
        float best_dist_sq = FLT_MAX;
        for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
            const XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
            if (!host.active || !host.entity || !host.combat_core || !(host.health > 0.0f)) {
                continue;
            }
            const float dx = host.position.x - player_api.x;
            const float dy = host.position.y - player_api.y;
            const float dist_sq = dx * dx + dy * dy;
            if (dist_sq > max_dist_sq || dist_sq >= best_dist_sq) {
                continue;
            }
            best_dist_sq = dist_sq;
            best_host = &host;
        }

        if (best_host) {
            XMarkRuntimeTarget official_target{};
            const bool resolved = runtime_target_from_official_enemy_host(*best_host, &official_target);
            if (g_mina && env_bool("MINA_XMARK_OFFICIAL_G_MARK_LOG", true)) {
                char message[640]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn G official-target reason=%s resolved=%u count=%u entity=0x%p core=0x%p hp=%.3f/%.3f pos=(%.3f, %.3f, %.3f) dist=%.3f type=%s\n",
                    reason ? reason : "<none>",
                    resolved ? 1u : 0u,
                    g_official_enemy_host_count,
                    reinterpret_cast<void *>(best_host->entity),
                    reinterpret_cast<void *>(best_host->combat_core),
                    static_cast<double>(best_host->health),
                    static_cast<double>(best_host->health_max),
                    static_cast<double>(best_host->position.x),
                    static_cast<double>(best_host->position.y),
                    static_cast<double>(best_host->position.z),
                    static_cast<double>(std::sqrt(best_dist_sq)),
                    best_host->component_type[0] ? best_host->component_type : "-");
                g_mina->Log(message);
            }
            if (resolved) {
                return spawn_tracked_xmark_for_target(reason ? reason : "key-G-official-enemy", official_target);
            }
        } else if (g_mina && env_bool("MINA_XMARK_OFFICIAL_G_MARK_LOG", true)) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn G official-target missed count=%u maxDist=%.3f; falling back.\n",
                g_official_enemy_host_count,
                static_cast<double>(max_dist));
            g_mina->Log(message);
        }
    }

    if (env_bool("MINA_XMARK_G_PREFER_VISUAL_ENEMY_STATE", true)) {
        XMarkVisualEnemyHost visual_host{};
        if (find_nearest_visual_enemy_host(&visual_host, now_ms)) {
            return spawn_tracked_xmark_for_visual_host(reason ? reason : "key-G-visual-host", visual_host);
        }
        if (env_bool("MINA_XMARK_G_REQUIRE_VISUAL_ENEMY_STATE", false)) {
            if (g_mina && env_bool("MINA_XMARK_VISUAL_HOST_MARK_LOG", true)) {
                g_mina->Log("XMarkBurn G visual-host target missed; visual target required, not falling back.\n");
            }
            return false;
        }
        if (g_mina && env_bool("MINA_XMARK_VISUAL_HOST_MARK_LOG", true)) {
            g_mina->Log("XMarkBurn G visual-host target missed; falling back to runtime target scan.\n");
        }
    }

    const Vec3 base = official_spawn_position();
    const int game_state = g_mina ? g_mina->GetCurrentGameState() : 0;
    XMarkDatabaseTarget target{};
    unsigned int considered = 0;
    unsigned int level_matched = 0;
    const bool found_database = find_nearest_database_enemy_target(base, game_state, &target, &considered, &level_matched);
    XMarkRuntimeTarget runtime_target{};
    unsigned int runtime_candidates = 0;
    unsigned int runtime_reads = 0;
    const bool found_runtime = find_nearest_runtime_target(&runtime_target, &runtime_candidates, &runtime_reads);
    const Vec3 position = found_runtime ? runtime_target.position : base;
    char message[1024]{};
    if (found_runtime) {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn G nearest-runtime-target reason=%s state=%d dir=%s facingMatch=%u healthLike=%u healthKind=%u healthOffset=0x%X health=%.3f/%.3f candidates=%u reads=%u entity=0x%p source=0x%p+0x%llX anchorOffset=0x%X anchor=(%.3f, %.3f, %.3f) base=(%.3f, %.3f, %.3f) pos=(%.3f, %.3f, %.3f) dist=%.3f forward=%.3f lateral=%.3f score=%.3f dbBest=%s/%s dbDist=%.3f\n",
            reason ? reason : "<none>",
            game_state,
            direction_name(g_last_direction),
            runtime_target.facing_match ? 1u : 0u,
            runtime_target.health_like ? 1u : 0u,
            runtime_target.health_kind,
            runtime_target.health_offset,
            static_cast<double>(runtime_target.health_value),
            static_cast<double>(runtime_target.health_max),
            runtime_candidates,
            runtime_reads,
            reinterpret_cast<void *>(runtime_target.entity),
            reinterpret_cast<void *>(runtime_target.source_base),
            static_cast<unsigned long long>(runtime_target.source_offset),
            runtime_target.anchor_offset,
            static_cast<double>(runtime_target.anchor.x),
            static_cast<double>(runtime_target.anchor.y),
            static_cast<double>(runtime_target.anchor.z),
            static_cast<double>(base.x),
            static_cast<double>(base.y),
            static_cast<double>(base.z),
            static_cast<double>(runtime_target.position.x),
            static_cast<double>(runtime_target.position.y),
            static_cast<double>(runtime_target.position.z),
            static_cast<double>(std::sqrt(runtime_target.distance_sq)),
            static_cast<double>(runtime_target.forward_delta),
            static_cast<double>(runtime_target.lateral_delta),
            static_cast<double>(runtime_target.score),
            (found_database && target.placement) ? target.placement->level_key : "none",
            (found_database && target.placement) ? target.placement->room_name : "none",
            found_database ? static_cast<double>(std::sqrt(target.distance_sq)) : -1.0);
    } else if (found_database && target.placement) {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn G runtime-target missed reason=%s state=%d candidates=%u reads=%u dbBest localEntry=%s catalog=%s level=%s room=%s entity=%u editor=(%.1f, %.1f) base=(%.3f, %.3f, %.3f) dbPos=(%.3f, %.3f, %.3f) dbDist=%.3f levelMatches=%u/%u noSpawn=1\n",
            reason ? reason : "<none>",
            game_state,
            runtime_candidates,
            runtime_reads,
            target.placement->local_entry,
            target.placement->catalog_name,
            target.placement->level_key,
            target.placement->room_name,
            target.placement->entity_index,
            static_cast<double>(target.placement->editor_x),
            static_cast<double>(target.placement->editor_y),
            static_cast<double>(base.x),
            static_cast<double>(base.y),
            static_cast<double>(base.z),
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            static_cast<double>(std::sqrt(target.distance_sq)),
            level_matched,
            considered);
    } else {
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn G runtime-target and db-target missed reason=%s state=%d candidates=%u reads=%u levelMatches=%u/%u dir=%s noSpawn=1\n",
            reason ? reason : "<none>",
            game_state,
            runtime_candidates,
            runtime_reads,
            level_matched,
            considered,
            direction_name(g_last_direction));
    }
    if (g_mina) {
        g_mina->Log(message);
    }
    return found_runtime && spawn_tracked_xmark_for_target(reason, runtime_target);
}

bool spawn_debug_xmark_on_player(const char *reason) {
    XMarkRuntimeTarget target{};
    target.entity = player_entity();
    target.source_base = target.entity;
    target.position = official_spawn_position();
    target.anchor = target.position;
    target.anchor_offset = kXMarkStaticRenderAnchor;
    target.render_half_w = std::max(0.001f, env_float("MINA_XMARK_DEBUG_PLAYER_MARK_HALF_W", xmark_default_render_half_w()));
    target.render_half_h = std::max(0.001f, env_float("MINA_XMARK_DEBUG_PLAYER_MARK_HALF_H", xmark_default_render_half_h()));
    target.facing_match = true;
    copy_visual_token(target.visual_entry, sizeof(target.visual_entry), "debug-player");
    copy_visual_token(target.visual_stem, sizeof(target.visual_stem), "debug-player");
    copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), "ModAPI");
    const bool marked = target.entity && spawn_tracked_xmark_for_target(reason ? reason : "debug-player", target);
    if (g_mina) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn debug player mark reason=%s player=0x%p pos=(%.3f, %.3f, %.3f) marked=%u\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(target.entity),
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            marked ? 1u : 0u);
        g_mina->Log(message);
    }
    return marked;
}

bool spawn_debug_hud_mark_only(const char *reason) {
    const uintptr_t target = 0x584D41524B485544ull; // "XMARKHUD"; never dereferenced by HUD health lookup.
    const unsigned long long now_ms = GetTickCount64();
    const unsigned int duration_ms = env_uint("MINA_XMARK_HUD_DEBUG_MARK_MS", 2000);
    upsert_xmark_hud_mark(target, now_ms + std::max(250u, duration_ms));
    publish_current_xmark_hud_state(now_ms);
    if (g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn debug HUD-only mark reason=%s target=0x%p durationMs=%u\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(target),
            duration_ms);
        g_mina->Log(message);
    }
    return true;
}

