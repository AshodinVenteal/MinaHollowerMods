const char *level_key_for_game_state(int game_state) {
    switch (game_state) {
    case GAMESTATE_INTROBEACH:
        return "introBeach";
    case GAMESTATE_BAYOU:
    case GAMESTATE_BAYOU_SCRATCH:
    case GAMESTATE_HUB_BAYOU_OVERWORLD:
        return "bayou";
    case GAMESTATE_CRYPTA:
        return "cryptA";
    case GAMESTATE_CRYPTB:
        return "cryptB";
    case GAMESTATE_BONEBEACHA:
    case GAMESTATE_BONEBEACH_SCRATCH:
    case GAMESTATE_BONEBEACH_OVERWORLD:
        return "boneBeachA";
    case GAMESTATE_BONEBEACHB:
        return "boneBeachB";
    case GAMESTATE_SEPTEMBURGA:
        return "septemburgA";
    case GAMESTATE_SEPTEMBURGB:
    case GAMESTATE_SEPTEMBURG_SCRATCH:
    case GAMESTATE_SEPTEMBURG_OVERWORLD:
        return "septemburgB";
    case GAMESTATE_SEPTEMBURG_SEWER:
        return "septemburgSewer";
    case GAMESTATE_FROZENTRAINYARDA:
        return "frozenTrainyardA";
    case GAMESTATE_FROZENTRAINYARDB:
        return "frozenTrainyardB";
    case GAMESTATE_FROZENTRAINYARDC:
    case GAMESTATE_FROZENTRAINYARD_SCRATCH:
        return "frozenTrainyardC";
    case GAMESTATE_ASTRALORRERY:
    case GAMESTATE_ASTRALORRERY_SCRATCH:
    case GAMESTATE_ASTRAL_ORRERY_MIRROR_HUB:
        return "astralOrrery";
    case GAMESTATE_MANSION:
    case GAMESTATE_MANSION_SCRATCH:
    case GAMESTATE_HUB_MANSION:
        return "mansion";
    case GAMESTATE_HUB_OVERWORLD_EAST:
        return "hub_overworld_east";
    case GAMESTATE_HUB_OVERWORLD_WEST:
        return "hub_overworld_west";
    case GAMESTATE_HUB_OVERWORLD_SOUTH:
        return "hub_overworld_south";
    default:
        return nullptr;
    }
}

float nonzero_env_float(const char *name, float fallback) {
    const float value = env_float(name, fallback);
    return std::fabs(value) < 0.0001f ? fallback : value;
}

Vec3 database_enemy_native_position(const XMarkEnemyPlacement &placement, const Vec3 &base) {
    const float scale_x = nonzero_env_float("MINA_XMARK_DB_COORD_SCALE_X", 13.333333f);
    const float scale_y = nonzero_env_float("MINA_XMARK_DB_COORD_SCALE_Y", -24.0f);
    const float offset_x = env_float("MINA_XMARK_DB_COORD_OFFSET_X", 0.0f);
    const float offset_y = env_float("MINA_XMARK_DB_COORD_OFFSET_Y", 0.0f);
    const float target_offset_x = env_float("MINA_XMARK_DB_TARGET_OFFSET_X", 0.0f);
    const float target_offset_y = env_float("MINA_XMARK_DB_TARGET_OFFSET_Y", 0.0f);

    Vec3 position = base;
    position.x = (placement.editor_x / scale_x) + offset_x + target_offset_x;
    position.y = (placement.editor_y / scale_y) + offset_y + target_offset_y;
    return position;
}

bool find_nearest_database_enemy_target(
    const Vec3 &base,
    int game_state,
    XMarkDatabaseTarget *out_target,
    unsigned int *out_considered,
    unsigned int *out_level_matched) {
    if (out_target) {
        out_target->placement = nullptr;
        out_target->position = base;
        out_target->distance_sq = FLT_MAX;
    }
    if (out_considered) {
        *out_considered = 0;
    }
    if (out_level_matched) {
        *out_level_matched = 0;
    }

    const char *level_key = level_key_for_game_state(game_state);
    const bool prefer_level = env_bool("MINA_XMARK_G_DB_PREFER_GAMESTATE_LEVEL", true) && level_key;
    const float radius = env_float("MINA_XMARK_G_DB_RADIUS", 240.0f);
    const float radius_sq = radius * radius;
    bool found = false;
    bool found_level_match = false;
    XMarkDatabaseTarget best{};
    best.position = base;
    best.distance_sq = FLT_MAX;

    for (unsigned int i = 0; i < kXMarkEnemyPlacementCount; ++i) {
        const XMarkEnemyPlacement &placement = kXMarkEnemyPlacements[i];
        if (out_considered) {
            ++(*out_considered);
        }
        const bool level_matches = level_key && std::strcmp(placement.level_key, level_key) == 0;
        if (level_matches && out_level_matched) {
            ++(*out_level_matched);
        }
        if (prefer_level && found_level_match && !level_matches) {
            continue;
        }

        const Vec3 position = database_enemy_native_position(placement, base);
        const float dx = position.x - base.x;
        const float dy = position.y - base.y;
        const float distance_sq = (dx * dx) + (dy * dy);
        if (distance_sq > radius_sq) {
            continue;
        }
        if (prefer_level && level_matches && !found_level_match) {
            found_level_match = true;
            found = false;
            best.distance_sq = FLT_MAX;
        } else if (prefer_level && found_level_match && !level_matches) {
            continue;
        }
        if (!found || distance_sq < best.distance_sq) {
            best.placement = &placement;
            best.position = position;
            best.distance_sq = distance_sq;
            found = true;
        }
    }

    if (!found && prefer_level) {
        for (unsigned int i = 0; i < kXMarkEnemyPlacementCount; ++i) {
            const XMarkEnemyPlacement &placement = kXMarkEnemyPlacements[i];
            const Vec3 position = database_enemy_native_position(placement, base);
            const float dx = position.x - base.x;
            const float dy = position.y - base.y;
            const float distance_sq = (dx * dx) + (dy * dy);
            if (distance_sq <= radius_sq && distance_sq < best.distance_sq) {
                best.placement = &placement;
                best.position = position;
                best.distance_sq = distance_sq;
                found = true;
            }
        }
    }

    if (found && out_target) {
        *out_target = best;
    }
    return found;
}

bool pointer_is_rejected_runtime_target(uintptr_t ptr) {
    if (!ptr) {
        return true;
    }
    if (pointer_is_spawned_effect(ptr)) {
        return true;
    }
    const uintptr_t player = player_entity();
    uintptr_t owner = 0;
    uintptr_t owner_f8 = 0;
    if (player) {
        safe_read_ptr(player + 0x50, &owner);
    }
    if (owner) {
        safe_read_ptr(owner + 0xf8, &owner_f8);
    }
    const uintptr_t owner_radius = static_cast<uintptr_t>(env_uint("MINA_XMARK_G_REJECT_OWNER_NEAR_BYTES", 0x2000));
    return ptr == player ||
        ptr == owner ||
        ptr == owner_f8 ||
        ptr == entity_manager() ||
        pointer_near(ptr, owner, owner_radius) ||
        pointer_near(ptr, owner_f8, owner_radius);
}

bool runtime_pointer_source_rejected(uintptr_t source_base) {
    if (!source_base || source_base == entity_manager()) {
        return false;
    }
    if (pointer_is_spawned_effect(source_base)) {
        return true;
    }
    const uintptr_t player = player_entity();
    uintptr_t owner = 0;
    uintptr_t owner_f8 = 0;
    if (player) {
        safe_read_ptr(player + 0x50, &owner);
    }
    if (owner) {
        safe_read_ptr(owner + 0xf8, &owner_f8);
    }
    const uintptr_t owner_radius = static_cast<uintptr_t>(env_uint("MINA_XMARK_G_REJECT_SOURCE_OWNER_NEAR_BYTES", 0x2000));
    return source_base == player ||
        source_base == owner ||
        source_base == owner_f8 ||
        pointer_near(source_base, owner, owner_radius) ||
        pointer_near(source_base, owner_f8, owner_radius);
}

bool runtime_object_anchor(
    uintptr_t ptr,
    const Vec3 &player_api,
    Vec3 *anchor_out,
    unsigned int *offset_out,
    float min_radius_override = -1.0f,
    float max_radius_override = -1.0f) {
    if (!ptr || !anchor_out || pointer_is_rejected_runtime_target(ptr) || !probable_heap_object(ptr)) {
        return false;
    }
    const unsigned int primary = env_uint("MINA_XMARK_G_TARGET_POS_OFFSET", 0x25c);
    const unsigned int offsets[] = {
        primary,
        0xf0,
        0xa0,
        0x0,
        0x50,
        0x1e0,
        0x230,
        0x2d0,
        0x320,
        0x4b0,
        0x550,
        0x5f0,
        0x640,
        0x6e0,
        0x730,
        0x7d0,
        0x960,
        0xa00,
        0xa50,
        0xb40,
        0xc30,
        0x268,
        0x66c,
        0x678,
        0xa7c,
        0xa88,
        0xe8c,
        0xe98,
    };
    const float max_abs = env_float("MINA_XMARK_G_TARGET_POS_MAX_ABS", 10000.0f);
    const float max_radius = max_radius_override > 0.0f ? max_radius_override : env_float("MINA_XMARK_G_RUNTIME_RADIUS", 96.0f);
    const float min_radius = min_radius_override >= 0.0f ? min_radius_override : env_float("MINA_XMARK_G_RUNTIME_MIN_RADIUS", 4.0f);
    const float max_radius_sq = max_radius * max_radius;
    const float min_radius_sq = min_radius * min_radius;
    for (unsigned int offset : offsets) {
        float x = 0.0f;
        float y = 0.0f;
        if (!safe_read_float(ptr + offset, &x) || !safe_read_float(ptr + offset + 4, &y)) {
            continue;
        }
        if (!std::isfinite(x) || !std::isfinite(y)) {
            continue;
        }
        if (std::fabs(x) > max_abs || std::fabs(y) > max_abs ||
            (std::fabs(x) < 0.001f && std::fabs(y) < 0.001f) ||
            (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f)) {
            continue;
        }
        const float dx = x - player_api.x;
        const float dy = y - player_api.y;
        const float distance_sq = (dx * dx) + (dy * dy);
        if (distance_sq < min_radius_sq || distance_sq > max_radius_sq) {
            continue;
        }
        anchor_out->x = x;
        anchor_out->y = y;
        anchor_out->z = player_api.z;
        if (offset_out) {
            *offset_out = offset;
        }
        return true;
    }
    return false;
}

bool runtime_object_anchor_scan_near_position(
    uintptr_t ptr,
    const Vec3 &player_api,
    const Vec3 &preferred_position,
    Vec3 *anchor_out,
    unsigned int *offset_out,
    const char *reason) {
    if (!env_bool("MINA_XMARK_TARGET_BROAD_POSITION_SCAN", true) ||
        !ptr || !anchor_out || pointer_is_rejected_runtime_target(ptr) || !probable_heap_object(ptr)) {
        return false;
    }

    const Vec3 official_base = official_spawn_position();
    const float max_abs = env_float("MINA_XMARK_G_TARGET_POS_MAX_ABS", 10000.0f);
    const float max_radius = env_float("MINA_XMARK_G_RUNTIME_RADIUS", 96.0f);
    const float min_radius = env_float("MINA_XMARK_G_RUNTIME_MIN_RADIUS", 0.1f);
    const float max_radius_sq = max_radius * max_radius;
    const float min_radius_sq = min_radius * min_radius;
    const float max_match = env_float("MINA_XMARK_TARGET_BROAD_POSITION_MAX_DIST", 10.0f);
    const float max_match_sq = max_match * max_match;
    const unsigned int scan_bytes = env_uint("MINA_XMARK_TARGET_BROAD_POSITION_SCAN_BYTES", 0x2000);
    const unsigned int capped_scan_bytes = scan_bytes > 0x8000u ? 0x8000u : scan_bytes;

    bool found = false;
    float best_score = FLT_MAX;
    Vec3 best_anchor{};
    unsigned int best_offset = 0;
    for (unsigned int offset = 0; offset + 4 < capped_scan_bytes; offset += 4) {
        float x = 0.0f;
        float y = 0.0f;
        if (!safe_read_float(ptr + offset, &x) || !safe_read_float(ptr + offset + 4, &y)) {
            continue;
        }
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::fabs(x) > max_abs || std::fabs(y) > max_abs ||
            (std::fabs(x) < 0.001f && std::fabs(y) < 0.001f) ||
            (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f)) {
            continue;
        }
        const float dx = x - player_api.x;
        const float dy = y - player_api.y;
        const float radius_sq = (dx * dx) + (dy * dy);
        if (radius_sq < min_radius_sq || radius_sq > max_radius_sq) {
            continue;
        }

        Vec3 position{};
        position.x = x + (official_base.x - player_api.x);
        position.y = y + (official_base.y - player_api.y);
        position.z = official_base.z;
        const float score = xy_distance_sq(position, preferred_position);
        if (score > max_match_sq) {
            continue;
        }
        if (!found || score < best_score) {
            found = true;
            best_score = score;
            best_anchor = Vec3{x, y, player_api.z};
            best_offset = offset;
        }
    }

    if (!found) {
        return false;
    }
    *anchor_out = best_anchor;
    if (offset_out) {
        *offset_out = best_offset;
    }
    if (g_mina && env_bool("MINA_XMARK_TARGET_BROAD_POSITION_SCAN_LOG", true)) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn target broad position resolved reason=%s target=0x%p offset=0x%X score=%.3f anchor=(%.3f, %.3f, %.3f)\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(ptr),
            best_offset,
            static_cast<double>(best_score),
            static_cast<double>(best_anchor.x),
            static_cast<double>(best_anchor.y),
            static_cast<double>(best_anchor.z));
        g_mina->Log(message);
    }
    return true;
}

bool runtime_object_anchor_near_position(
    uintptr_t ptr,
    const Vec3 &player_api,
    const Vec3 &preferred_position,
    Vec3 *anchor_out,
    unsigned int *offset_out) {
    if (!ptr || !anchor_out || pointer_is_rejected_runtime_target(ptr) || !probable_heap_object(ptr)) {
        return false;
    }

    const unsigned int primary = env_uint("MINA_XMARK_G_TARGET_POS_OFFSET", 0x25c);
    const unsigned int offsets[] = {
        primary,
        0x25c,
        0xf0,
        0xa0,
        0x50,
        0x1e0,
        0x230,
        0x2d0,
        0x320,
        0x4b0,
        0x550,
        0x5f0,
        0x640,
        0x6e0,
        0x730,
        0x7d0,
        0x960,
        0xa00,
        0xa50,
        0xb40,
        0xc30,
        0x268,
        0x66c,
        0x678,
        0xa7c,
        0xa88,
        0xe8c,
        0xe98,
    };

    const Vec3 official_base = official_spawn_position();
    const float max_abs = env_float("MINA_XMARK_G_TARGET_POS_MAX_ABS", 10000.0f);
    const float max_radius = env_float("MINA_XMARK_G_RUNTIME_RADIUS", 96.0f);
    const float min_radius = env_float("MINA_XMARK_G_RUNTIME_MIN_RADIUS", 0.1f);
    const float max_radius_sq = max_radius * max_radius;
    const float min_radius_sq = min_radius * min_radius;
    const float max_reacquire = env_float("MINA_XMARK_ATTACHMENT_REACQUIRE_MAX_DIST", 8.0f);
    const float max_reacquire_sq = max_reacquire * max_reacquire;

    bool found = false;
    float best_score = FLT_MAX;
    Vec3 best_anchor{};
    unsigned int best_offset = 0;
    for (unsigned int offset : offsets) {
        float x = 0.0f;
        float y = 0.0f;
        if (!safe_read_float(ptr + offset, &x) || !safe_read_float(ptr + offset + 4, &y)) {
            continue;
        }
        if (!std::isfinite(x) || !std::isfinite(y) ||
            std::fabs(x) > max_abs || std::fabs(y) > max_abs ||
            (std::fabs(x) < 0.001f && std::fabs(y) < 0.001f) ||
            (std::fabs(x) <= 4.0f && std::fabs(y) <= 4.0f)) {
            continue;
        }

        Vec3 anchor{x, y, player_api.z};
        const float dx = anchor.x - player_api.x;
        const float dy = anchor.y - player_api.y;
        const float radius_sq = (dx * dx) + (dy * dy);
        if (radius_sq < min_radius_sq || radius_sq > max_radius_sq) {
            continue;
        }

        Vec3 position{};
        position.x = anchor.x + (official_base.x - player_api.x);
        position.y = anchor.y + (official_base.y - player_api.y);
        position.z = official_base.z;
        const float score = xy_distance_sq(position, preferred_position);
        if (score > max_reacquire_sq) {
            continue;
        }
        if (!found || score < best_score) {
            found = true;
            best_score = score;
            best_anchor = anchor;
            best_offset = offset;
        }
    }

    if (!found) {
        return runtime_object_anchor_scan_near_position(
            ptr,
            player_api,
            preferred_position,
            anchor_out,
            offset_out,
            "attachment-near-last");
    }
    *anchor_out = best_anchor;
    if (offset_out) {
        *offset_out = best_offset;
    }
    return true;
}

bool runtime_object_anchor_at_offset(uintptr_t ptr, unsigned int offset, Vec3 *anchor_out) {
    if (!ptr || !anchor_out || pointer_is_rejected_runtime_target(ptr) || !probable_heap_object(ptr)) {
        return false;
    }

    float x = 0.0f;
    float y = 0.0f;
    if (!safe_read_float(ptr + offset, &x) || !safe_read_float(ptr + offset + 4, &y)) {
        return false;
    }
    const float max_abs = env_float("MINA_XMARK_G_TARGET_POS_MAX_ABS", 10000.0f);
    if (!std::isfinite(x) || !std::isfinite(y) ||
        std::fabs(x) > max_abs || std::fabs(y) > max_abs ||
        (std::fabs(x) < 0.001f && std::fabs(y) < 0.001f)) {
        return false;
    }

    anchor_out->x = x;
    anchor_out->y = y;
    anchor_out->z = 0.0f;
    return true;
}

Vec3 runtime_anchor_to_spawn_position(const Vec3 &anchor) {
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    const Vec3 official_base = official_spawn_position();
    Vec3 position{};
    position.x = anchor.x + (official_base.x - player_api.x);
    position.y = anchor.y + (official_base.y - player_api.y);
    position.z = official_base.z;
    return position;
}

bool find_position_pair_offset(uintptr_t base, const Vec3 &position, unsigned int *offset_out) {
    if (!base || !offset_out || !probable_heap_object(base)) {
        return false;
    }
    unsigned int offsets[1]{};
    const unsigned int count = find_position_pair_offsets(
        base,
        position,
        offsets,
        static_cast<unsigned int>(sizeof(offsets) / sizeof(offsets[0])),
        env_float("MINA_XMARK_EFFECT_POSITION_MATCH_TOLERANCE", 0.075f));
    if (!count) {
        return false;
    }
    *offset_out = offsets[0];
    return true;
}

unsigned int find_position_pair_offsets(
    uintptr_t base,
    const Vec3 &position,
    unsigned int *offsets_out,
    unsigned int max_offsets,
    float tolerance) {
    if (!base || !offsets_out || !max_offsets || !probable_heap_object(base)) {
        return 0;
    }
    const unsigned int scan_bytes = env_uint("MINA_XMARK_EFFECT_POSITION_SCAN_BYTES", 0x600);
    const unsigned int capped_scan_bytes = scan_bytes > 0x2000u ? 0x2000u : scan_bytes;
    unsigned int found = 0;
    for (unsigned int offset = 0; offset + 4 < capped_scan_bytes; offset += 4) {
        float x = 0.0f;
        float y = 0.0f;
        if (!safe_read_float(base + offset, &x) || !safe_read_float(base + offset + 4, &y)) {
            continue;
        }
        if (std::fabs(x - position.x) <= tolerance && std::fabs(y - position.y) <= tolerance) {
            offsets_out[found++] = offset;
            if (found >= max_offsets) {
                break;
            }
        }
    }
    return found;
}

bool write_position_pair(uintptr_t base, unsigned int offset, const Vec3 &position) {
    if (!base || !probable_heap_object(base)) {
        return false;
    }
    bool ok = safe_write_float(base + offset, position.x);
    ok = safe_write_float(base + offset + 4, position.y) && ok;
    if (env_bool("MINA_XMARK_WRITE_EFFECT_Z", true)) {
        safe_write_float(base + offset + 8, position.z);
    }
    return ok;
}

bool official_enemy_live_contact_bounds_read(
    uintptr_t entity,
    uintptr_t combat_core,
    Vec3 *center_out,
    float *half_w_out,
    float *half_h_out) {
    if (!env_bool("MINA_XMARK_OFFICIAL_DEFENSE_BOUNDS_ENABLED", false) ||
        !g_mina ||
        !g_mina->EntityGetWorldTransform ||
        !entity ||
        !combat_core ||
        !probable_heap_object(entity) ||
        !probable_heap_object(combat_core)) {
        return false;
    }

    MM_Transform transform{};
    bool transform_ok = false;
    __try {
        transform = g_mina->EntityGetWorldTransform(reinterpret_cast<ycEntity *>(entity));
        transform_ok =
            std::isfinite(transform.t.x) &&
            std::isfinite(transform.t.y) &&
            std::isfinite(transform.t.z);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        transform_ok = false;
    }
    if (!transform_ok) {
        return false;
    }
    return official_combat_core_defense_bounds(
        reinterpret_cast<ycComponent *>(combat_core),
        transform,
        center_out,
        half_w_out,
        half_h_out);
}

void write_attachment_position_slots(XMarkAttachment &attachment, const Vec3 &position) {
    if (attachment.effect_position_base && attachment.effect_position_count > 0) {
        for (unsigned int i = 0; i < attachment.effect_position_count; ++i) {
            write_position_pair(attachment.effect_position_base, attachment.effect_position_offsets[i], position);
        }
    }
    if (attachment.child_position_base && attachment.child_position_count > 0) {
        for (unsigned int i = 0; i < attachment.child_position_count; ++i) {
            write_position_pair(attachment.child_position_base, attachment.child_position_offsets[i], position);
        }
    }
    for (unsigned int i = 0; i < attachment.extra_position_count; ++i) {
        write_position_pair(attachment.extra_position_bases[i], attachment.extra_position_offsets[i], position);
    }
}

void clear_xmark_attachments_for_lab() {
    if (!env_bool("MINA_XMARK_ATTACHMENT_LAB_SINGLE_MARK", false)) {
        return;
    }
    const Vec3 offscreen{
        env_float("MINA_XMARK_ATTACHMENT_LAB_CLEAR_X", -9999.0f),
        env_float("MINA_XMARK_ATTACHMENT_LAB_CLEAR_Y", -9999.0f),
        0.0f,
    };
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active) {
            continue;
        }
        write_attachment_position_slots(attachment, offscreen);
        attachment.active = false;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        mark.active = false;
    }
}

void maybe_log_xmark_attachment_update(
    unsigned long long now_ms,
    const XMarkAttachment &attachment,
    const Vec3 &position,
    bool resolved,
    bool wrote_any) {
    if (!g_mina || !env_bool("MINA_XMARK_ATTACHMENT_UPDATE_LOG", false)) {
        return;
    }
    const unsigned int interval_ms = env_uint("MINA_XMARK_ATTACHMENT_UPDATE_LOG_MS", 300);
    if (g_attachment_lab_last_log_ms && now_ms - g_attachment_lab_last_log_ms < interval_ms) {
        return;
    }
    g_attachment_lab_last_log_ms = now_ms;

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn attachment update target=0x%p visualKey=0x%llX visualFollow=%u visualHoldLast=%u source=0x%p+0x%llX effect=0x%p child=0x%p anchorOffset=0x%X effectSlots=%u childSlots=%u extraSlots=%u resolved=%u wrote=%u pos=(%.3f, %.3f, %.3f)\n",
        reinterpret_cast<void *>(attachment.target),
        attachment.visual_key,
        attachment.visual_follow ? 1u : 0u,
        attachment.visual_follow &&
            attachment.has_last_position &&
            attachment.last_visual_missing_ms > attachment.last_visual_resolved_ms ? 1u : 0u,
        reinterpret_cast<void *>(attachment.target_source_base),
        static_cast<unsigned long long>(attachment.target_source_offset),
        reinterpret_cast<void *>(attachment.effect),
        reinterpret_cast<void *>(attachment.child),
        attachment.target_anchor_offset,
        attachment.effect_position_count,
        attachment.child_position_count,
        attachment.extra_position_count,
        resolved ? 1u : 0u,
        wrote_any ? 1u : 0u,
        static_cast<double>(position.x),
        static_cast<double>(position.y),
        static_cast<double>(position.z));
    g_mina->Log(message);
}

bool resolve_effect_position_slot(
    uintptr_t object,
    const Vec3 &match_position,
    uintptr_t *base_out,
    unsigned int *offset_out) {
    if (!object || !base_out || !offset_out || !probable_heap_object(object)) {
        return false;
    }
    unsigned int resolved_offset = 0;
    if (!find_position_pair_offset(object, match_position, &resolved_offset)) {
        return false;
    }
    *base_out = object;
    *offset_out = resolved_offset;
    return true;
}

bool attachment_has_position_slot(const XMarkAttachment &attachment, uintptr_t base, unsigned int offset) {
    if (!base) {
        return true;
    }
    if (attachment.effect_position_base == base) {
        for (unsigned int i = 0; i < attachment.effect_position_count; ++i) {
            if (attachment.effect_position_offsets[i] == offset) {
                return true;
            }
        }
    }
    if (attachment.child_position_base == base) {
        for (unsigned int i = 0; i < attachment.child_position_count; ++i) {
            if (attachment.child_position_offsets[i] == offset) {
                return true;
            }
        }
    }
    for (unsigned int i = 0; i < attachment.extra_position_count; ++i) {
        if (attachment.extra_position_bases[i] == base && attachment.extra_position_offsets[i] == offset) {
            return true;
        }
    }
    return false;
}

unsigned int append_extra_position_slots_from_object(
    XMarkAttachment &attachment,
    uintptr_t object,
    const Vec3 &match_position,
    float tolerance,
    unsigned int max_extra_slots) {
    if (!object || !probable_heap_object(object) || attachment.extra_position_count >= max_extra_slots) {
        return 0;
    }
    unsigned int offsets[kXMarkPositionSlotMax]{};
    const unsigned int remaining = max_extra_slots - attachment.extra_position_count;
    const unsigned int count = find_position_pair_offsets(
        object,
        match_position,
        offsets,
        std::min(remaining, kXMarkPositionSlotMax),
        tolerance);
    unsigned int added = 0;
    for (unsigned int i = 0; i < count && attachment.extra_position_count < max_extra_slots; ++i) {
        if (attachment_has_position_slot(attachment, object, offsets[i])) {
            continue;
        }
        const unsigned int slot = attachment.extra_position_count++;
        attachment.extra_position_bases[slot] = object;
        attachment.extra_position_offsets[slot] = offsets[i];
        ++added;
    }
    return added;
}

unsigned int append_nested_extra_position_slots(
    XMarkAttachment &attachment,
    uintptr_t root,
    const Vec3 &match_position,
    float tolerance,
    unsigned int max_extra_slots) {
    if (!root || !probable_heap_object(root) || attachment.extra_position_count >= max_extra_slots) {
        return 0;
    }
    unsigned int added = 0;
    const uintptr_t player = player_entity();
    const uintptr_t manager = entity_manager();
    const unsigned int scan_bytes = env_uint("MINA_XMARK_ATTACHMENT_NESTED_POSITION_SCAN_BYTES", 0x1000);
    const unsigned int capped_scan_bytes = scan_bytes > 0x4000u ? 0x4000u : scan_bytes;
    for (unsigned int offset = 0; offset < capped_scan_bytes && attachment.extra_position_count < max_extra_slots; offset += 8) {
        uintptr_t ptr = 0;
        if (!safe_read_ptr(root + offset, &ptr) ||
            !ptr ||
            ptr == root ||
            ptr == player ||
            ptr == manager ||
            ptr == attachment.target ||
            ptr == attachment.target_source_base ||
            ptr == attachment.effect ||
            ptr == attachment.child ||
            !probable_heap_object(ptr)) {
            continue;
        }
        added += append_extra_position_slots_from_object(
            attachment,
            ptr,
            match_position,
            tolerance,
            max_extra_slots);
    }
    return added;
}

bool bind_xmark_attachment_position_slots(XMarkAttachment &attachment, const Vec3 &match_position) {
    if (!env_bool("MINA_XMARK_ATTACHMENT_BIND_ENABLED", true)) {
        attachment.effect_position_count = 0;
        attachment.effect_position_base = 0;
        attachment.effect_position_offset = 0;
        attachment.child_position_count = 0;
        attachment.child_position_base = 0;
        attachment.child_position_offset = 0;
        attachment.extra_position_count = 0;
        return false;
    }
    const float tolerance = env_float("MINA_XMARK_EFFECT_POSITION_MATCH_TOLERANCE", 0.075f);
    const unsigned int max_slots = env_uint("MINA_XMARK_ATTACHMENT_POSITION_SLOT_MAX", 1);
    if (max_slots == 0) {
        attachment.effect_position_count = 0;
        attachment.effect_position_base = 0;
        attachment.effect_position_offset = 0;
        attachment.child_position_count = 0;
        attachment.child_position_base = 0;
        attachment.child_position_offset = 0;
        attachment.extra_position_count = 0;
        return false;
    }
    const unsigned int capped_max_slots = std::min(max_slots, kXMarkPositionSlotMax);
    const unsigned int extra_max_slots = std::min(
        env_uint("MINA_XMARK_ATTACHMENT_EXTRA_POSITION_SLOT_MAX", capped_max_slots),
        kXMarkPositionSlotMax);

    attachment.effect_position_count = 0;
    attachment.effect_position_base = 0;
    attachment.effect_position_offset = 0;
    attachment.extra_position_count = 0;
    if (env_bool("MINA_XMARK_ATTACHMENT_WRITE_EFFECT_OBJECT", false) &&
        attachment.effect &&
        probable_heap_object(attachment.effect)) {
        attachment.effect_position_count = find_position_pair_offsets(
            attachment.effect,
            match_position,
            attachment.effect_position_offsets,
            capped_max_slots,
            tolerance);
        if (attachment.effect_position_count > 0) {
            attachment.effect_position_base = attachment.effect;
            attachment.effect_position_offset = attachment.effect_position_offsets[0];
        }
    }

    attachment.child_position_count = 0;
    attachment.child_position_base = 0;
    attachment.child_position_offset = 0;
    if (attachment.child && probable_heap_object(attachment.child)) {
        attachment.child_position_count = find_position_pair_offsets(
            attachment.child,
            match_position,
            attachment.child_position_offsets,
            capped_max_slots,
            tolerance);
        if (attachment.child_position_count > 0) {
            attachment.child_position_base = attachment.child;
            attachment.child_position_offset = attachment.child_position_offsets[0];
        }
    }

    if (env_bool("MINA_XMARK_ATTACHMENT_SCAN_NESTED_EFFECT_OBJECTS", false) && extra_max_slots > 0) {
        append_nested_extra_position_slots(attachment, attachment.effect, match_position, tolerance, extra_max_slots);
        append_nested_extra_position_slots(attachment, attachment.child, match_position, tolerance, extra_max_slots);
    }

    if (env_bool("MINA_XMARK_ATTACHMENT_BIND_LOG", false) && g_mina) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn attachment bind target=0x%p effect=0x%p slots=%u child=0x%p slots=%u extraSlots=%u match=(%.3f, %.3f, %.3f)\n",
            reinterpret_cast<void *>(attachment.target),
            reinterpret_cast<void *>(attachment.effect),
            attachment.effect_position_count,
            reinterpret_cast<void *>(attachment.child),
            attachment.child_position_count,
            attachment.extra_position_count,
            static_cast<double>(match_position.x),
            static_cast<double>(match_position.y),
            static_cast<double>(match_position.z));
        g_mina->Log(message);
    }

    return attachment.effect_position_count > 0 || attachment.child_position_count > 0 || attachment.extra_position_count > 0;
}

void update_xmark_attachments(unsigned long long now_ms) {
    clashrend_boom_debug_draw(now_ms);
    bool has_active_attachment = false;
    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (attachment.active) {
            has_active_attachment = true;
            break;
        }
    }
    if (!has_active_attachment) {
        if (env_bool("MINA_XMARK_RENDER_UPDATE_IN_FIXED", true)) {
            xmark_render_backend_update_vertices(now_ms);
        }
        return;
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    if (env_bool("MINA_XMARK_VISUAL_FOLLOW_FORCE_STATE_READ", !xmark_official_damage_gate_enabled())) {
        read_enemy_visual_state_file(now_ms, true);
    }

    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active) {
            continue;
        }
        if (now_ms >= attachment.expires_ms) {
            attachment.active = false;
            continue;
        }

        if (attachment.official_follow) {
            XMarkVisualEnemyHost visual_host{};
            bool visual_center_resolved = false;
            Vec3 visual_center{};
            if (env_bool("MINA_XMARK_OFFICIAL_FOLLOW_USE_VISUAL_CENTER", true) &&
                attachment.visual_key &&
                visual_enemy_host_by_key(attachment.visual_key, &visual_host, now_ms)) {
                const unsigned int visual_stale_ms =
                    std::max(16u, env_uint("MINA_XMARK_OFFICIAL_FOLLOW_VISUAL_CENTER_STALE_MS", 96));
                if (!visual_host.last_seen_ms || now_ms <= visual_host.last_seen_ms + visual_stale_ms) {
                    visual_center = visual_host_render_position(visual_host);
                    visual_center_resolved = true;
                }
            }

            XMarkOfficialEnemyHost host{};
            const unsigned int official_resolve_interval_ms = std::max(
                16u,
                env_uint("MINA_XMARK_OFFICIAL_FOLLOW_RESOLVE_INTERVAL_MS", 500));
            const bool refresh_official_host =
                !attachment.last_reacquire_ms ||
                now_ms >= attachment.last_reacquire_ms + official_resolve_interval_ms;
            bool resolved = false;
            if (refresh_official_host) {
                attachment.last_reacquire_ms = now_ms;
                resolved = official_enemy_host_for_attachment(attachment, &host) &&
                    host.health > 0.0f &&
                    host.health_max > 0.0f;
                if (resolved) {
                    attachment.target = host.entity;
                    attachment.official_combat_core = host.combat_core;
                    if (host.body_center_offset_valid) {
                        attachment.official_body_offset = host.body_center_offset;
                        attachment.has_official_body_offset = true;
                    }
                    attachment.render_half_w = official_enemy_render_half_from_health(
                        host,
                        xmark_default_render_half_w());
                    attachment.render_half_h = official_enemy_render_half_from_health(
                        host,
                        xmark_default_render_half_h());
                }
            }
            const uintptr_t follow_entity =
                resolved && host.entity ? host.entity : attachment.target;
            const uintptr_t follow_core =
                resolved && host.combat_core ? host.combat_core : attachment.official_combat_core;
            float direct_health = 0.0f;
            Vec3 direct_position{};
            const bool direct_health_read_ok =
                follow_core &&
                combat_core_health_read(follow_core, &direct_health);
            const bool direct_alive =
                direct_health_read_ok &&
                direct_health > 0.0f;
            const bool direct_position_ok =
                (direct_alive || attachment.suppress_hud) &&
                official_entity_world_position_read(follow_entity, &direct_position);
            if (direct_health_read_ok) {
                xmark_attachment_observe_health(attachment, direct_health, now_ms);
            }
            if (!attachment.suppress_hud &&
                direct_health_read_ok &&
                direct_health <= 0.0f &&
                env_bool("MINA_XMARK_OFFICIAL_FOLLOW_DESPAWN_ON_DEAD_CORE", true)) {
                attachment.active = false;
                shorten_xmark_hud_mark(attachment.target, now_ms);
                publish_current_xmark_hud_state(now_ms);
                if (g_mina && env_bool("MINA_XMARK_OFFICIAL_FOLLOW_DESPAWN_LOG", false)) {
                    char message[384]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn official-follow mark despawned on dead core target=0x%p officialCore=0x%p health=%.3f\n",
                        reinterpret_cast<void *>(attachment.target),
                        reinterpret_cast<void *>(attachment.official_combat_core),
                        static_cast<double>(direct_health));
                    g_mina->Log(message);
                }
                continue;
            }
            if (env_bool("MINA_XMARK_OFFICIAL_FOLLOW_PREFER_DIRECT_TRANSFORM", true) &&
                direct_position_ok) {
                Vec3 target_position = visual_center_resolved
                    ? visual_center
                    : xmark_official_body_position(attachment, direct_position);
                if (attachment.suppress_hud) {
                    target_position.x += env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_X", 0.0f);
                    target_position.y += env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_Y", 0.6f);
                }
                if (visual_center_resolved) {
                    attachment.runtime_visual_offset.x = visual_center.x - direct_position.x;
                    attachment.runtime_visual_offset.y = visual_center.y - direct_position.y;
                    attachment.runtime_visual_offset.z = visual_center.z - direct_position.z;
                    attachment.has_runtime_visual_offset = true;
                } else {
                    attachment.runtime_visual_offset = Vec3{};
                    attachment.has_runtime_visual_offset = false;
                }
                const Vec3 position = xmark_attachment_mark_position(target_position);
                attachment.target = follow_entity;
                attachment.official_combat_core = follow_core;
                attachment.last_position = position;
                attachment.has_last_position = true;
                attachment.last_visual_resolved_ms = now_ms;
                attachment.last_visual_missing_ms = 0;
                attachment.visual_missing_count = 0;
                bool wrote_any = false;
                if (!attachment.render_backend) {
                    if (attachment.effect_position_count == 0 &&
                        attachment.child_position_count == 0 &&
                        attachment.extra_position_count == 0) {
                        bind_xmark_attachment_position_slots(attachment, position);
                    }
                    if (env_bool("MINA_XMARK_ATTACHMENT_WRITE_ENABLED", true)) {
                        write_attachment_position_slots(attachment, position);
                        wrote_any = attachment.effect_position_count > 0 ||
                            attachment.child_position_count > 0 ||
                            attachment.extra_position_count > 0;
                    }
                }
                maybe_log_xmark_attachment_update(now_ms, attachment, position, true, wrote_any);
                if (g_mina && env_bool("MINA_XMARK_OFFICIAL_FOLLOW_DIRECT_TRANSFORM_LOG", false)) {
                    char message[448]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn official-follow direct transform target=0x%p officialCore=0x%p health=%.3f pos=(%.3f, %.3f, %.3f) resolved=%u\n",
                        reinterpret_cast<void *>(attachment.target),
                        reinterpret_cast<void *>(attachment.official_combat_core),
                        static_cast<double>(direct_health),
                        static_cast<double>(direct_position.x),
                        static_cast<double>(direct_position.y),
                        static_cast<double>(direct_position.z),
                        resolved ? 1u : 0u);
                    g_mina->Log(message);
                }
                continue;
            }
            if (!resolved) {
                if (direct_position_ok) {
                    Vec3 target_position = visual_center_resolved
                        ? visual_center
                        : xmark_official_body_position(attachment, direct_position);
                    if (visual_center_resolved) {
                        attachment.runtime_visual_offset.x = visual_center.x - direct_position.x;
                        attachment.runtime_visual_offset.y = visual_center.y - direct_position.y;
                        attachment.runtime_visual_offset.z = visual_center.z - direct_position.z;
                        attachment.has_runtime_visual_offset = true;
                    } else {
                        attachment.runtime_visual_offset = Vec3{};
                        attachment.has_runtime_visual_offset = false;
                    }
                    const Vec3 position = xmark_attachment_mark_position(target_position);
                    attachment.last_position = position;
                    attachment.has_last_position = true;
                    attachment.last_visual_resolved_ms = now_ms;
                    attachment.last_visual_missing_ms = 0;
                    attachment.visual_missing_count = 0;
                    maybe_log_xmark_attachment_update(now_ms, attachment, position, true, false);
                    if (g_mina && env_bool("MINA_XMARK_OFFICIAL_FOLLOW_DIRECT_TRANSFORM_LOG", false)) {
                        char message[448]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn official-follow direct transform target=0x%p officialCore=0x%p health=%.3f pos=(%.3f, %.3f, %.3f)\n",
                            reinterpret_cast<void *>(attachment.target),
                            reinterpret_cast<void *>(attachment.official_combat_core),
                            static_cast<double>(direct_health),
                            static_cast<double>(direct_position.x),
                            static_cast<double>(direct_position.y),
                            static_cast<double>(direct_position.z));
                        g_mina->Log(message);
                    }
                    continue;
                }
                if (direct_alive && visual_center_resolved) {
                    const Vec3 position = xmark_attachment_mark_position(visual_center);
                    attachment.last_position = position;
                    attachment.has_last_position = true;
                    attachment.last_visual_resolved_ms = now_ms;
                    attachment.last_visual_missing_ms = 0;
                    attachment.visual_missing_count = 0;
                    maybe_log_xmark_attachment_update(now_ms, attachment, position, true, false);
                    continue;
                }
                if (direct_alive &&
                    attachment.has_last_position &&
                    env_bool("MINA_XMARK_OFFICIAL_FOLLOW_HOLD_LAST_ON_SNAPSHOT_MISS", true)) {
                    maybe_log_xmark_attachment_update(now_ms, attachment, attachment.last_position, true, false);
                    continue;
                }

                attachment.active = false;
                shorten_xmark_hud_mark(attachment.target, now_ms);
                publish_current_xmark_hud_state(now_ms);
                if (g_mina && env_bool("MINA_XMARK_OFFICIAL_FOLLOW_DESPAWN_LOG", false)) {
                    char message[384]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn official-follow mark despawned target=0x%p officialCore=0x%p resolved=%u directAlive=%u\n",
                        reinterpret_cast<void *>(attachment.target),
                        reinterpret_cast<void *>(attachment.official_combat_core),
                        resolved ? 1u : 0u,
                        direct_alive ? 1u : 0u);
                    g_mina->Log(message);
                }
                continue;
            }

            Vec3 target_position = visual_center_resolved ? visual_center : host.position;
            if (visual_center_resolved) {
                attachment.runtime_visual_offset.x = visual_center.x - host.position.x;
                attachment.runtime_visual_offset.y = visual_center.y - host.position.y;
                attachment.runtime_visual_offset.z = visual_center.z - host.position.z;
                attachment.has_runtime_visual_offset = true;
            } else {
                attachment.runtime_visual_offset = Vec3{};
                attachment.has_runtime_visual_offset = false;
            }
            const Vec3 position = xmark_attachment_mark_position(target_position);
            attachment.target = host.entity;
            attachment.official_combat_core = host.combat_core;
            attachment.last_position = position;
            attachment.render_half_w = official_enemy_render_half_from_health(host, xmark_default_render_half_w());
            attachment.render_half_h = official_enemy_render_half_from_health(host, xmark_default_render_half_h());
            attachment.has_last_position = true;
            attachment.last_visual_resolved_ms = now_ms;
            attachment.last_visual_missing_ms = 0;
            attachment.visual_missing_count = 0;
            bool wrote_any = false;
            if (!attachment.render_backend) {
                if (attachment.effect_position_count == 0 &&
                    attachment.child_position_count == 0 &&
                    attachment.extra_position_count == 0) {
                    bind_xmark_attachment_position_slots(attachment, position);
                }
                if (env_bool("MINA_XMARK_ATTACHMENT_WRITE_ENABLED", true)) {
                    write_attachment_position_slots(attachment, position);
                    wrote_any = attachment.effect_position_count > 0 ||
                        attachment.child_position_count > 0 ||
                        attachment.extra_position_count > 0;
                }
            }
            maybe_log_xmark_attachment_update(now_ms, attachment, position, true, wrote_any);
            continue;
        }

        if (attachment.visual_follow && attachment.visual_key) {
            if (attachment.official_combat_core &&
                env_bool("MINA_XMARK_VISUAL_FOLLOW_DESPAWN_ON_COMBAT_CORE_DEAD", true) &&
                !combat_core_is_alive(attachment.official_combat_core)) {
                attachment.active = false;
                shorten_xmark_hud_mark(attachment.target, now_ms);
                publish_current_xmark_hud_state(now_ms);
                if (g_mina && env_bool("MINA_XMARK_VISUAL_FOLLOW_DEATH_FADE_LOG", false)) {
                    char message[384]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn visual-follow combat-core dead; despawned mark key=0x%llX officialCore=0x%p\n",
                        attachment.visual_key,
                        reinterpret_cast<void *>(attachment.official_combat_core));
                    g_mina->Log(message);
                }
                continue;
            }
            if (g_visual_enemy_state_explicit_zero &&
                !xmark_visual_host_currently_present(attachment.visual_key)) {
                const unsigned int explicit_zero_despawn_ms =
                    env_uint("MINA_XMARK_VISUAL_FOLLOW_EXPLICIT_ZERO_DESPAWN_MS", 260);
                const unsigned long long last_resolved_ms =
                    attachment.last_visual_resolved_ms ? attachment.last_visual_resolved_ms : attachment.started_ms;
                const bool inside_zero_grace =
                    explicit_zero_despawn_ms &&
                    last_resolved_ms &&
                    now_ms >= last_resolved_ms &&
                    now_ms < last_resolved_ms + explicit_zero_despawn_ms;
                if (!inside_zero_grace) {
                    attachment.active = false;
                    shorten_xmark_hud_mark(attachment.target, now_ms);
                    publish_current_xmark_hud_state(now_ms);
                    if (g_mina && env_bool("MINA_XMARK_VISUAL_FOLLOW_DEATH_FADE_LOG", false)) {
                        char message[384]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn visual-follow explicit-zero; despawned mark key=0x%llX pos=(%.3f, %.3f, %.3f)\n",
                            attachment.visual_key,
                            static_cast<double>(attachment.last_position.x),
                            static_cast<double>(attachment.last_position.y),
                            static_cast<double>(attachment.last_position.z));
                        g_mina->Log(message);
                    }
                    continue;
                }
            }
            XMarkVisualEnemyHost host{};
            bool resolved = visual_enemy_host_by_key(attachment.visual_key, &host, now_ms);
            if (!resolved &&
                attachment.has_last_position &&
                env_bool("MINA_XMARK_VISUAL_FOLLOW_REACQUIRE_NEAR_LAST", false)) {
                const Vec3 expected_render_position =
                    xmark_attachment_target_position_from_mark(attachment.last_position);
                const Vec3 expected_visual_position =
                    render_position_to_visual_host_space(expected_render_position);
                if (find_nearest_visual_enemy_host_to_visual_position(
                        expected_visual_position,
                        &host,
                        now_ms,
                        &attachment)) {
                    resolved = true;
                    if (host.key) {
                        attachment.visual_key = host.key;
                        if (!attachment.target || !probable_heap_object(attachment.target)) {
                            attachment.target = static_cast<uintptr_t>(
                                0xD400000000000000ull | (host.key & 0x00FFFFFFFFFFFFFFull));
                        }
                    }
                    if (g_mina && env_bool("MINA_XMARK_VISUAL_FOLLOW_REACQUIRE_LOG", false)) {
                        char message[512]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn visual-follow reacquired key=0x%llX entry=%s stem=%s expectedVisual=(%.3f, %.3f)\n",
                            host.key,
                            host.entry[0] ? host.entry : "-",
                            host.stem[0] ? host.stem : "-",
                            static_cast<double>(expected_visual_position.x),
                            static_cast<double>(expected_visual_position.y));
                        g_mina->Log(message);
                    }
                }
            }
            if (resolved && host.last_seen_ms) {
                const unsigned int stale_despawn_ms =
                    std::max(32u, env_uint("MINA_XMARK_VISUAL_FOLLOW_STALE_DESPAWN_MS", 900));
                if (now_ms > host.last_seen_ms + stale_despawn_ms) {
                    resolved = false;
                }
            }
            const bool allow_runtime_fallback =
                !resolved &&
                !attachment.render_backend &&
                env_bool("MINA_XMARK_VISUAL_FOLLOW_RUNTIME_FALLBACK", false) &&
                attachment.target &&
                probable_heap_object(attachment.target);
            bool wrote_any = false;
            if (resolved) {
                Vec3 host_render_position = visual_host_pinned_render_position(attachment, host, now_ms);

                if (attachment.target && probable_heap_object(attachment.target)) {
                    Vec3 runtime_anchor{};
                    bool runtime_resolved = runtime_object_anchor_at_offset(
                        attachment.target,
                        attachment.target_anchor_offset,
                        &runtime_anchor);
                    if (!runtime_resolved) {
                        unsigned int refreshed_offset = 0;
                        runtime_resolved = runtime_object_anchor(
                            attachment.target,
                            player_api,
                            &runtime_anchor,
                            &refreshed_offset,
                            0.0f,
                            192.0f);
                        if (runtime_resolved) {
                            attachment.target_anchor_offset = refreshed_offset;
                        }
                    }
                    if (runtime_resolved) {
                        const Vec3 runtime_position = runtime_anchor_to_spawn_position(runtime_anchor);
                        attachment.runtime_visual_offset.x = host_render_position.x - runtime_position.x;
                        attachment.runtime_visual_offset.y = host_render_position.y - runtime_position.y;
                        attachment.runtime_visual_offset.z = host_render_position.z - runtime_position.z;
                        attachment.has_runtime_visual_offset = true;
                    }
                }

                const Vec3 position = xmark_attachment_mark_position(host_render_position);
                attachment.last_position = position;
                xmark_visual_host_render_halves(host, &attachment.render_half_w, &attachment.render_half_h);
                attachment.has_last_position = true;
                attachment.last_visual_resolved_ms = now_ms;
                attachment.last_visual_missing_ms = 0;
                attachment.visual_missing_count = 0;
                if (!attachment.render_backend) {
                    if (attachment.effect_position_count == 0 &&
                        attachment.child_position_count == 0 &&
                        attachment.extra_position_count == 0) {
                        bind_xmark_attachment_position_slots(attachment, position);
                    }
                    if (env_bool("MINA_XMARK_ATTACHMENT_WRITE_ENABLED", true)) {
                        write_attachment_position_slots(attachment, position);
                        wrote_any = attachment.effect_position_count > 0 ||
                            attachment.child_position_count > 0 ||
                            attachment.extra_position_count > 0;
                    }
                }
            } else if (attachment.has_last_position) {
                if (!attachment.last_visual_missing_ms ||
                    attachment.last_visual_missing_ms <= attachment.last_visual_resolved_ms) {
                    attachment.last_visual_missing_ms = now_ms;
                }
                ++attachment.visual_missing_count;
                const bool previously_resolved =
                    attachment.last_visual_resolved_ms != 0 &&
                    attachment.last_visual_resolved_ms >= attachment.started_ms;
                const unsigned int apply_grace_ms =
                    env_uint("MINA_XMARK_VISUAL_FOLLOW_APPLY_GRACE_MS", 300);
                const bool in_apply_grace =
                    !previously_resolved &&
                    attachment.started_ms &&
                    now_ms >= attachment.started_ms &&
                    now_ms - attachment.started_ms < apply_grace_ms;
                const unsigned int missing_despawn_ms = previously_resolved
                    ? env_uint("MINA_XMARK_VISUAL_FOLLOW_RESOLVED_MISSING_DESPAWN_MS", 260)
                    : env_uint("MINA_XMARK_VISUAL_FOLLOW_MISSING_DESPAWN_MS", 260);
                if (!in_apply_grace &&
                    !allow_runtime_fallback &&
                    attachment.last_visual_missing_ms > 0 &&
                    now_ms >= attachment.last_visual_missing_ms + missing_despawn_ms) {
                    attachment.active = false;
                    shorten_xmark_hud_mark(attachment.target, now_ms);
                    publish_current_xmark_hud_state(now_ms);
                    if (g_mina && env_bool("MINA_XMARK_VISUAL_FOLLOW_DEATH_FADE_LOG", false)) {
                        char message[448]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn visual-follow missing; despawned mark key=0x%llX missingMs=%llu pos=(%.3f, %.3f, %.3f)\n",
                            attachment.visual_key,
                            now_ms - attachment.last_visual_missing_ms,
                            static_cast<double>(attachment.last_position.x),
                            static_cast<double>(attachment.last_position.y),
                            static_cast<double>(attachment.last_position.z));
                        g_mina->Log(message);
                    }
                    continue;
                }
                if (g_mina && env_bool("MINA_XMARK_VISUAL_FOLLOW_HOLD_LAST_LOG", false)) {
                    char message[448]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn visual-follow hold-last key=0x%llX missingCount=%u pos=(%.3f, %.3f, %.3f)\n",
                        attachment.visual_key,
                        attachment.visual_missing_count,
                        static_cast<double>(attachment.last_position.x),
                        static_cast<double>(attachment.last_position.y),
                        static_cast<double>(attachment.last_position.z));
                    g_mina->Log(message);
                }
            }
            maybe_log_xmark_attachment_update(
                now_ms,
                attachment,
                attachment.has_last_position ? attachment.last_position : Vec3{0.0f, 0.0f, 0.0f},
                resolved,
                wrote_any);
            if (resolved || !allow_runtime_fallback) {
                continue;
            }
        }

        if (env_bool("MINA_XMARK_ATTACHMENT_REFRESH_TARGET_FROM_SOURCE", true) &&
            attachment.target_source_base &&
            attachment.target_source_offset &&
            probable_heap_object(attachment.target_source_base)) {
            uintptr_t refreshed_target = 0;
            if (safe_read_ptr(attachment.target_source_base + attachment.target_source_offset, &refreshed_target) &&
                refreshed_target &&
                refreshed_target != attachment.target_source_base &&
                probable_heap_object(refreshed_target) &&
                !pointer_is_rejected_runtime_target(refreshed_target) &&
                !pointer_is_spawned_effect(refreshed_target)) {
                if (refreshed_target != attachment.target && g_mina && env_bool("MINA_XMARK_ATTACHMENT_TARGET_REFRESH_LOG", true)) {
                    char message[384]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn attachment target refreshed source=0x%p+0x%llX old=0x%p new=0x%p\n",
                        reinterpret_cast<void *>(attachment.target_source_base),
                        static_cast<unsigned long long>(attachment.target_source_offset),
                        reinterpret_cast<void *>(attachment.target),
                        reinterpret_cast<void *>(refreshed_target));
                    g_mina->Log(message);
                }
                attachment.target = refreshed_target;
            }
        }

        if (env_bool("MINA_XMARK_ATTACHMENT_DYNAMIC_REACQUIRE_FROM_ROOT", true) &&
            attachment.target_source_base &&
            probable_heap_object(attachment.target_source_base)) {
            const unsigned int reacquire_ms = env_uint("MINA_XMARK_ATTACHMENT_DYNAMIC_REACQUIRE_MS", 140);
            if (!attachment.last_reacquire_ms || now_ms - attachment.last_reacquire_ms >= reacquire_ms) {
                attachment.last_reacquire_ms = now_ms;
                Vec3 expected_position = attachment.has_last_position
                    ? xmark_attachment_target_position_from_mark(attachment.last_position)
                    : g_last_test_enemy_position;
                XMarkRuntimeTarget reacquired{};
                if (runtime_target_from_entity_pointer(
                        attachment.target_source_base,
                        expected_position,
                        g_last_direction,
                        &reacquired)) {
                    const bool changed =
                        reacquired.entity != attachment.target ||
                        reacquired.source_offset != attachment.target_source_offset ||
                        reacquired.anchor_offset != attachment.target_anchor_offset;
                    attachment.target = reacquired.entity;
                    attachment.target_source_offset = reacquired.source_offset;
                    attachment.target_anchor_offset = reacquired.anchor_offset;
                    attachment.target_health_offset = reacquired.health_offset;
                    attachment.target_health_kind = reacquired.health_kind;
                    attachment.target_health_like = reacquired.health_like;
                    if (changed && g_mina && env_bool("MINA_XMARK_ATTACHMENT_DYNAMIC_REACQUIRE_LOG", true)) {
                        char message[512]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn attachment dynamic reacquire root=0x%p target=0x%p sourceOffset=0x%llX anchorOffset=0x%X pos=(%.3f, %.3f, %.3f)\n",
                            reinterpret_cast<void *>(attachment.target_source_base),
                            reinterpret_cast<void *>(attachment.target),
                            static_cast<unsigned long long>(attachment.target_source_offset),
                            attachment.target_anchor_offset,
                            static_cast<double>(reacquired.position.x),
                            static_cast<double>(reacquired.position.y),
                            static_cast<double>(reacquired.position.z));
                        g_mina->Log(message);
                    }
                }
            }
        }

        if (attachment.render_backend && attachment.target_anchor_offset == kXMarkStaticRenderAnchor) {
            attachment.has_last_position = true;
            maybe_log_xmark_attachment_update(now_ms, attachment, attachment.last_position, true, false);
            continue;
        }

        if (!probable_heap_object(attachment.target)) {
            attachment.active = false;
            continue;
        }

        Vec3 anchor{};
        bool resolved = false;
        const bool pin_target_offset = env_bool("MINA_XMARK_ATTACHMENT_PIN_TARGET_OFFSET", false);
        if (pin_target_offset) {
            resolved = runtime_object_anchor_at_offset(attachment.target, attachment.target_anchor_offset, &anchor);
        }
        if (!resolved && !pin_target_offset && attachment.has_last_position && env_bool("MINA_XMARK_ATTACHMENT_PREFER_NEAR_LAST", true)) {
            unsigned int refreshed_offset = 0;
            resolved = runtime_object_anchor_near_position(
                attachment.target,
                player_api,
                attachment.last_position,
                &anchor,
                &refreshed_offset);
            if (resolved) {
                attachment.target_anchor_offset = refreshed_offset;
            }
        }
        if (!resolved) {
            resolved = runtime_object_anchor_at_offset(attachment.target, attachment.target_anchor_offset, &anchor);
        }
        if (resolved && !pin_target_offset && attachment.has_last_position) {
            const Vec3 candidate_position = runtime_anchor_to_spawn_position(anchor);
            const float max_jump = env_float("MINA_XMARK_ATTACHMENT_MAX_JUMP", 3.0f);
            if (xy_distance_sq(candidate_position, attachment.last_position) > max_jump * max_jump) {
                unsigned int refreshed_offset = 0;
                Vec3 refreshed_anchor{};
                if (runtime_object_anchor_near_position(
                        attachment.target,
                        player_api,
                        attachment.last_position,
                        &refreshed_anchor,
                        &refreshed_offset)) {
                    anchor = refreshed_anchor;
                    attachment.target_anchor_offset = refreshed_offset;
                }
            }
        }
        if (!resolved) {
            unsigned int refreshed_offset = 0;
            resolved = runtime_object_anchor(attachment.target, player_api, &anchor, &refreshed_offset);
            if (resolved) {
                attachment.target_anchor_offset = refreshed_offset;
            }
        }
        if (!resolved) {
            attachment.active = false;
            continue;
        }

        Vec3 target_position = runtime_anchor_to_spawn_position(anchor);
        if (attachment.visual_follow && attachment.has_runtime_visual_offset) {
            target_position.x += attachment.runtime_visual_offset.x;
            target_position.y += attachment.runtime_visual_offset.y;
            target_position.z += attachment.runtime_visual_offset.z;
        }
        const Vec3 position = xmark_attachment_mark_position(target_position);
        if (attachment.render_backend) {
            attachment.last_position = position;
            attachment.has_last_position = true;
            maybe_log_xmark_attachment_update(now_ms, attachment, position, resolved, false);
            continue;
        }
        const Vec3 match_position = attachment.has_last_position ? attachment.last_position : position;
        if (attachment.effect_position_count == 0 && attachment.child_position_count == 0 && attachment.extra_position_count == 0) {
            bind_xmark_attachment_position_slots(attachment, match_position);
        }

        bool wrote_any = false;
        const bool write_enabled = env_bool("MINA_XMARK_ATTACHMENT_WRITE_ENABLED", true);
        if (!write_enabled) {
            attachment.last_position = position;
            attachment.has_last_position = true;
            maybe_log_xmark_attachment_update(now_ms, attachment, position, resolved, false);
            continue;
        }
        if (attachment.effect_position_base && attachment.effect_position_count > 0) {
            bool wrote_effect = false;
            for (unsigned int i = 0; i < attachment.effect_position_count; ++i) {
                wrote_effect = write_position_pair(attachment.effect_position_base, attachment.effect_position_offsets[i], position) || wrote_effect;
            }
            if (!wrote_effect) {
                attachment.effect_position_base = 0;
                attachment.effect_position_count = 0;
            }
            wrote_any = wrote_effect || wrote_any;
        }
        if (attachment.child_position_base && attachment.child_position_count > 0) {
            bool wrote_child = false;
            for (unsigned int i = 0; i < attachment.child_position_count; ++i) {
                wrote_child = write_position_pair(attachment.child_position_base, attachment.child_position_offsets[i], position) || wrote_child;
            }
            if (!wrote_child) {
                attachment.child_position_base = 0;
                attachment.child_position_count = 0;
            }
            wrote_any = wrote_child || wrote_any;
        }
        if (attachment.extra_position_count > 0) {
            bool wrote_extra = false;
            unsigned int kept = 0;
            for (unsigned int i = 0; i < attachment.extra_position_count; ++i) {
                const uintptr_t base = attachment.extra_position_bases[i];
                const unsigned int offset = attachment.extra_position_offsets[i];
                if (write_position_pair(base, offset, position)) {
                    attachment.extra_position_bases[kept] = base;
                    attachment.extra_position_offsets[kept] = offset;
                    ++kept;
                    wrote_extra = true;
                }
            }
            attachment.extra_position_count = kept;
            wrote_any = wrote_extra || wrote_any;
        }
        if (wrote_any) {
            attachment.last_position = position;
            attachment.has_last_position = true;
        }
        maybe_log_xmark_attachment_update(now_ms, attachment, position, resolved, wrote_any);
    }
    xmark_marker_debug_draw_attachments(now_ms);
    xmark_burn_debug_draw_effects(now_ms);
    if (env_bool("MINA_XMARK_RENDER_UPDATE_IN_FIXED", true)) {
        xmark_render_backend_update_vertices(now_ms);
    }
}

bool runtime_target_seen(const XMarkRuntimeTarget *targets, unsigned int count, uintptr_t entity) {
    for (unsigned int i = 0; i < count; ++i) {
        if (targets[i].entity == entity) {
            return true;
        }
    }
    return false;
}

bool plausible_health_pair(float a, float b, float *value_out, float *max_out) {
    if (!std::isfinite(a) || !std::isfinite(b)) {
        return false;
    }
    if (a < 1.0f || b < 1.0f || a > 5000.0f || b > 5000.0f) {
        return false;
    }
    const float value = a < b ? a : b;
    const float max_value = a > b ? a : b;
    const float min_max = env_float("MINA_XMARK_G_HEALTH_MIN_MAX", 2.0f);
    const float max_ratio = env_float("MINA_XMARK_G_HEALTH_MAX_RATIO", 64.0f);
    if (value > max_value || max_value < min_max || max_value / value > max_ratio) {
        return false;
    }
    if (value_out) {
        *value_out = value;
    }
    if (max_out) {
        *max_out = max_value;
    }
    return true;
}

bool runtime_target_healthlike(uintptr_t ptr, unsigned int *offset_out, float *value_out, float *max_out, unsigned int *kind_out) {
    if (!ptr || !probable_heap_object(ptr)) {
        return false;
    }

    const unsigned int priority_offsets[] = {
        0x40,
        0x48,
        0x58,
        0x68,
        0x70,
        0x78,
        0x80,
        0x8c,
        0x90,
        0x98,
        0xa8,
        0xb4,
        0xc0,
        0xd0,
        0xf8,
        0x100,
        0x110,
        0x174,
        0x1a0,
        0x1b8,
        0x1e0,
        0x21c,
        0x240,
        0x2a0,
    };
    const auto try_offset = [&](unsigned int offset) -> bool {
        float fa = 0.0f;
        float fb = 0.0f;
        if (safe_read_float(ptr + offset, &fa) && safe_read_float(ptr + offset + 4, &fb) &&
            plausible_health_pair(fa, fb, value_out, max_out)) {
            if (offset_out) {
                *offset_out = offset;
            }
            if (kind_out) {
                *kind_out = 1u;
            }
            return true;
        }

        uint32_t ia = 0u;
        uint32_t ib = 0u;
        if (safe_read_u32(ptr + offset, &ia) && safe_read_u32(ptr + offset + 4, &ib)) {
            const float fa_int = static_cast<float>(ia);
            const float fb_int = static_cast<float>(ib);
            if (plausible_health_pair(fa_int, fb_int, value_out, max_out)) {
                if (offset_out) {
                    *offset_out = offset;
                }
                if (kind_out) {
                    *kind_out = 2u;
                }
                return true;
            }
        }
        return false;
    };

    for (unsigned int offset : priority_offsets) {
        if (try_offset(offset)) {
            return true;
        }
    }

    const unsigned int scan_bytes = env_uint("MINA_XMARK_G_HEALTH_SCAN_BYTES", 0x300);
    const unsigned int capped_scan_bytes = scan_bytes > 0x1000u ? 0x1000u : scan_bytes;
    for (unsigned int offset = 0; offset + 4 < capped_scan_bytes; offset += 4) {
        if (try_offset(offset)) {
            return true;
        }
    }
    return false;
}

void consider_runtime_pointer(
    uintptr_t ptr,
    uintptr_t source_base,
    uintptr_t source_offset,
    const Vec3 &player_api,
    const Vec3 &official_base,
    XMarkRuntimeTarget *targets,
    unsigned int *target_count,
    unsigned int max_targets) {
    if (!targets || !target_count || *target_count >= max_targets || runtime_target_seen(targets, *target_count, ptr)) {
        return;
    }
    if (runtime_pointer_source_rejected(source_base)) {
        return;
    }

    Vec3 anchor{};
    unsigned int anchor_offset = 0;
    if (!runtime_object_anchor(ptr, player_api, &anchor, &anchor_offset)) {
        return;
    }

    unsigned int health_offset = 0;
    float health_value = 0.0f;
    float health_max = 0.0f;
    unsigned int health_kind = 0;
    const bool health_like = runtime_target_healthlike(ptr, &health_offset, &health_value, &health_max, &health_kind);
    if (!health_like &&
        env_bool("MINA_XMARK_G_RUNTIME_REQUIRE_HEALTHLIKE", true) &&
        !g_runtime_collect_include_non_health_for_test) {
        return;
    }

    const float dx = anchor.x - player_api.x;
    const float dy = anchor.y - player_api.y;
    XMarkRuntimeTarget &target = targets[*target_count];
    target.entity = ptr;
    target.source_base = source_base;
    target.source_offset = source_offset;
    target.anchor = anchor;
    target.position.x = anchor.x + (official_base.x - player_api.x);
    target.position.y = anchor.y + (official_base.y - player_api.y);
    target.position.z = official_base.z;
    target.distance_sq = (dx * dx) + (dy * dy);
    target.anchor_offset = anchor_offset;
    target.health_offset = health_offset;
    target.health_value = health_value;
    target.health_max = health_max;
    target.health_kind = health_kind;
    target.health_like = health_like;
    ++(*target_count);
}

void scan_runtime_pointer_block(
    uintptr_t base,
    unsigned int scan_bytes,
    const Vec3 &player_api,
    const Vec3 &official_base,
    XMarkRuntimeTarget *targets,
    unsigned int *target_count,
    unsigned int max_targets,
    unsigned int *pointers_read) {
    if (!base || !probable_heap_object(base)) {
        return;
    }
    const unsigned int capped_scan_bytes = scan_bytes > 0x8000u ? 0x8000u : scan_bytes;
    for (unsigned int offset = 0; offset < capped_scan_bytes && (!target_count || *target_count < max_targets); offset += 8) {
        uintptr_t ptr = 0;
        if (!safe_read_ptr(base + offset, &ptr)) {
            continue;
        }
        if (pointers_read) {
            ++(*pointers_read);
        }
        if (!probable_heap_object(ptr)) {
            continue;
        }
        consider_runtime_pointer(ptr, base, offset, player_api, official_base, targets, target_count, max_targets);
    }
}

unsigned int collect_runtime_targets(
    XMarkRuntimeTarget *targets,
    unsigned int max_targets,
    unsigned int *pointers_read_out,
    Vec3 *player_api_out,
    Vec3 *official_base_out) {
    if (pointers_read_out) {
        *pointers_read_out = 0;
    }
    if (!targets || max_targets == 0) {
        return 0;
    }

    const uintptr_t manager = entity_manager();
    const uintptr_t player = player_entity();
    if (!manager || !player) {
        return 0;
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    const Vec3 official_base = official_spawn_position();
    player_api.z = official_base.z;
    if (player_api_out) {
        *player_api_out = player_api;
    }
    if (official_base_out) {
        *official_base_out = official_base;
    }

    unsigned int target_count = 0;
    unsigned int pointers_read = 0;
    uintptr_t owner = 0;
    uintptr_t game = game_singleton();
    safe_read_ptr(player + 0x50, &owner);

    const unsigned int manager_scan = env_uint("MINA_XMARK_G_RUNTIME_MANAGER_SCAN_BYTES", 0x1800);
    const unsigned int object_scan = env_uint("MINA_XMARK_G_RUNTIME_OBJECT_SCAN_BYTES", 0x400);

    scan_runtime_pointer_block(manager, manager_scan, player_api, official_base, targets, &target_count, max_targets, &pointers_read);
    scan_runtime_pointer_block(player, object_scan, player_api, official_base, targets, &target_count, max_targets, &pointers_read);
    scan_runtime_pointer_block(owner, object_scan, player_api, official_base, targets, &target_count, max_targets, &pointers_read);
    scan_runtime_pointer_block(game, object_scan, player_api, official_base, targets, &target_count, max_targets, &pointers_read);

    const unsigned int seed_count = target_count;
    const unsigned int second_level_scan = env_uint("MINA_XMARK_G_RUNTIME_SECOND_LEVEL_SCAN_BYTES", 0x100);
    for (unsigned int i = 0; i < seed_count && target_count < max_targets; ++i) {
        scan_runtime_pointer_block(
            targets[i].entity,
            second_level_scan,
            player_api,
            official_base,
            targets,
            &target_count,
            max_targets,
            &pointers_read);
    }

    if (pointers_read_out) {
        *pointers_read_out = pointers_read;
    }
    return target_count;
}

void apply_visual_host_identity_to_runtime_target(const XMarkVisualEnemyHost &host, XMarkRuntimeTarget *target) {
    if (!target) {
        return;
    }
    const Vec3 render_position = visual_host_render_position(host);
    target->visual_key = host.key;
    target->visual_follow = true;
    target->position = render_position;
    xmark_visual_host_render_halves(host, &target->render_half_w, &target->render_half_h);
    target->visual_texture_width = host.texture_width;
    target->visual_texture_height = host.texture_height;
    copy_visual_token(target->visual_entry, sizeof(target->visual_entry), host.entry);
    copy_visual_token(target->visual_stem, sizeof(target->visual_stem), host.stem);
    copy_visual_token(target->visual_catalog, sizeof(target->visual_catalog), host.catalog);
}

bool find_runtime_target_near_visual_host(
    const XMarkVisualEnemyHost &host,
    XMarkRuntimeTarget *target_out,
    unsigned long long now_ms) {
    if (!target_out || !host.active || !host.key) {
        return false;
    }
    const Vec3 visual_position = visual_host_render_position(host);
    const float max_distance = env_float("MINA_XMARK_VISUAL_FOLLOW_RUNTIME_MAX_DISTANCE", 12.0f);
    const float max_distance_sq = max_distance * max_distance;

    XMarkRuntimeTarget candidate{};
    const unsigned long long test_enemy_max_age_ms =
        env_uint("MINA_XMARK_TEST_ENEMY_MANUAL_MARK_MAX_AGE_MS", 12000);
    if (g_last_test_enemy_entity &&
        g_last_test_enemy_resolved_ms &&
        (!test_enemy_max_age_ms || now_ms <= g_last_test_enemy_resolved_ms + test_enemy_max_age_ms) &&
        runtime_target_from_entity_pointer(
            g_last_test_enemy_entity,
            visual_position,
            g_last_direction,
            &candidate)) {
        if (xy_distance_sq(candidate.position, visual_position) <= max_distance_sq) {
            apply_visual_host_identity_to_runtime_target(host, &candidate);
            *target_out = candidate;
            return true;
        }
    }

    constexpr unsigned int kMaxTargets = 96;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int reads = 0;
    const unsigned int count = collect_runtime_targets(targets, kMaxTargets, &reads, nullptr, nullptr);
    bool found = false;
    XMarkRuntimeTarget best{};
    float best_score = FLT_MAX;
    const bool prefer_healthlike = env_bool("MINA_XMARK_VISUAL_FOLLOW_RUNTIME_PREFER_HEALTHLIKE", true);
    for (unsigned int i = 0; i < count; ++i) {
        XMarkRuntimeTarget target = targets[i];
        const float distance_sq = xy_distance_sq(target.position, visual_position);
        if (distance_sq > max_distance_sq) {
            continue;
        }
        float score = distance_sq;
        if (prefer_healthlike && target.health_like) {
            score -= env_float("MINA_XMARK_VISUAL_FOLLOW_RUNTIME_HEALTHLIKE_BONUS", 1000.0f);
        }
        if (!found || score < best_score) {
            found = true;
            best = target;
            best_score = score;
        }
    }
    if (!found) {
        return false;
    }

    apply_visual_host_identity_to_runtime_target(host, &best);
    *target_out = best;
    return true;
}

void runtime_target_direction_delta_for_direction(
    const XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    int direction,
    float *forward_out,
    float *lateral_out) {
    const float raw_dx = target.anchor.x - player_api.x;
    const float dx = env_bool("MINA_XMARK_BASIC_CONTACT_FLIP_X", false) ? -raw_dx : raw_dx;
    const float dy = target.anchor.y - player_api.y;
    float forward = dx;
    float lateral = std::fabs(dy);
    switch (direction) {
    case FacingLeft:
        forward = -dx;
        lateral = std::fabs(dy);
        break;
    case FacingUp:
        forward = -dy;
        lateral = std::fabs(dx);
        break;
    case FacingDown:
        forward = dy;
        lateral = std::fabs(dx);
        break;
    case FacingRight:
    default:
        forward = dx;
        lateral = std::fabs(dy);
        break;
    }
    if (forward_out) {
        *forward_out = forward;
    }
    if (lateral_out) {
        *lateral_out = lateral;
    }
}

void runtime_target_direction_delta(const XMarkRuntimeTarget &target, const Vec3 &player_api, float *forward_out, float *lateral_out) {
    runtime_target_direction_delta_for_direction(target, player_api, g_last_direction, forward_out, lateral_out);
}

bool runtime_target_in_basic_contact_band(const XMarkRuntimeTarget &target, const Vec3 &player_api, int direction) {
    float forward = 0.0f;
    float lateral = 0.0f;
    runtime_target_direction_delta_for_direction(target, player_api, direction, &forward, &lateral);
    const float min_forward = env_float("MINA_XMARK_BASIC_CONTACT_MIN_FORWARD", -2.0f);
    const float max_forward = env_float("MINA_XMARK_BASIC_CONTACT_MAX_FORWARD", 42.0f);
    const float max_lateral = env_float("MINA_XMARK_BASIC_CONTACT_MAX_LATERAL", 28.0f);
    return forward >= min_forward && forward <= max_forward && lateral <= max_lateral;
}

bool basic_frame_state_overlaps_runtime_target(
    const XMarkBasicFrameState &state,
    const XMarkRuntimeTarget &target) {
    if (!xmark_basic_frame_name_allowed(state.frame) ||
        (!state.has_geometry && !state.has_contact)) {
        return false;
    }

    const bool has_combat_bounds =
        std::isfinite(target.contact_half_w) && target.contact_half_w > 0.0f &&
        std::isfinite(target.contact_half_h) && target.contact_half_h > 0.0f;
    float half_w = has_combat_bounds
        ? target.contact_half_w
        : std::max(0.1f, env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_FALLBACK_HALF_W", 4.0f));
    float half_h = has_combat_bounds
        ? target.contact_half_h
        : std::max(0.1f, env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_FALLBACK_HALF_H", 4.0f));
    if (!has_combat_bounds && target.health_max > 0.0f) {
        const float threshold = env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_HEALTH_SCALE_THRESHOLD", 24.0f);
        const float health_over_threshold = std::max(0.0f, target.health_max - threshold);
        const float extra = std::min(
            std::max(0.0f, env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_HEALTH_SCALE_MAX", 5.0f)),
            health_over_threshold *
                std::max(0.0f, env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_HEALTH_SCALE", 0.02f)));
        half_w += extra;
        half_h += extra;
    }

    const bool vertical_attack = state.direction == FacingUp || state.direction == FacingDown;
    const float padding_x = std::max(
        0.0f,
        env_float("MINA_XMARK_OFFICIAL_BASIC_CONTACT_PADDING_X", 1.0f));
    const float padding_y = std::max(
        0.0f,
        env_float(
            vertical_attack
                ? "MINA_XMARK_OFFICIAL_BASIC_VERTICAL_CONTACT_PADDING_Y"
                : "MINA_XMARK_OFFICIAL_BASIC_CONTACT_PADDING_Y",
            vertical_attack ? 1.5f : 1.0f));

    if (state.has_geometry) {
        return state.max_x + padding_x >= target.position.x - half_w &&
            state.min_x - padding_x <= target.position.x + half_w &&
            state.max_y + padding_y >= target.position.y - half_h &&
            state.min_y - padding_y <= target.position.y + half_h;
    }

    return std::fabs(state.contact_x - target.position.x) <= half_w + padding_x &&
        std::fabs(state.contact_y - target.position.y) <= half_h + padding_y;
}

bool muriel_drawn_basic_frame_overlaps_target(
    const XMarkBasicFrameState &state,
    const XMarkRuntimeTarget &target) {
    if (!xmark_basic_frame_name_allowed(state.frame) ||
        (!state.has_geometry && !state.has_contact) ||
        (env_bool("MINA_XMARK_MURIEL_REQUIRE_DRAWN_GEOMETRY", true) &&
         !state.has_geometry)) {
        return false;
    }

    const float half_w = std::max(0.1f, target.contact_half_w);
    const float half_h = std::max(0.1f, target.contact_half_h);
    const float padding_x = std::max(
        0.0f,
        env_float("MINA_XMARK_MURIEL_CONTACT_PADDING_X", 0.0f));
    const float padding_y = std::max(
        0.0f,
        env_float("MINA_XMARK_MURIEL_CONTACT_PADDING_Y", 0.0f));
    if (state.has_contact &&
        std::fabs(state.contact_x - target.position.x) <= half_w + padding_x &&
        std::fabs(state.contact_y - target.position.y) <= half_h + padding_y) {
        return true;
    }
    if (state.has_geometry &&
        env_bool("MINA_XMARK_MURIEL_BASIC_ALLOW_GEOMETRY_FALLBACK", true)) {
        return state.max_x + padding_x >= target.position.x - half_w &&
            state.min_x - padding_x <= target.position.x + half_w &&
            state.max_y + padding_y >= target.position.y - half_h &&
            state.min_y - padding_y <= target.position.y + half_h;
    }
    return false;
}

bool muriel_drawn_charged_frame_overlaps_host(
    const XMarkBasicFrameState &state,
    const XMarkOfficialEnemyHost &host,
    unsigned long long now_ms) {
    if (!xmark_charged_frame_name_allowed(state.frame) ||
        !state.tick ||
        now_ms > state.tick +
            std::max(1u, env_uint("MINA_XMARK_MURIEL_CHARGED_FRAME_MAX_AGE_MS", 80)) ||
        (env_bool("MINA_XMARK_MURIEL_CHARGED_REQUIRE_DRAWN_GEOMETRY", false) &&
         !state.has_geometry)) {
        return false;
    }

    const Vec3 position = host.position;
    const float half_w = std::max(
        0.1f,
        env_float("MINA_XMARK_MURIEL_CHARGED_CONTACT_HALF_W", 1.50f));
    const float half_h = std::max(
        0.1f,
        env_float("MINA_XMARK_MURIEL_CHARGED_CONTACT_HALF_H", 1.75f));
    const float padding_x = std::max(
        0.0f,
        env_float("MINA_XMARK_MURIEL_CHARGED_CONTACT_PADDING_X", 0.0f));
    const float padding_y = std::max(
        0.0f,
        env_float("MINA_XMARK_MURIEL_CHARGED_CONTACT_PADDING_Y", 0.0f));
    if (state.has_contact &&
        std::fabs(state.contact_x - position.x) <= half_w + padding_x &&
        std::fabs(state.contact_y - position.y) <= half_h + padding_y) {
        return true;
    }
    if (state.has_geometry &&
        env_bool("MINA_XMARK_MURIEL_CHARGED_ALLOW_GEOMETRY_FALLBACK", false)) {
        return state.max_x + padding_x >= position.x - half_w &&
            state.min_x - padding_x <= position.x + half_w &&
            state.max_y + padding_y >= position.y - half_h &&
            state.min_y - padding_y <= position.y + half_h;
    }
    return false;
}

bool find_basic_attack_visual_enemy_host_for_damage(
    const XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    int direction,
    XMarkVisualEnemyHost *host_out,
    unsigned long long now_ms) {
    if (!host_out || !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }

    const bool require_frame_overlap =
        env_bool("MINA_XMARK_BASIC_REQUIRE_FRAME_CONTACT_OVERLAP", true);
    XMarkBasicFrameState overlap_state{};
    if (require_frame_overlap &&
        !recent_basic_frame_state_for_direction(now_ms, direction, &overlap_state)) {
        return false;
    }

    const Vec3 expected_visual_position = render_position_to_visual_host_space(target.position);
    const float max_expected_distance = env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_FALLBACK_MAX_DISTANCE", 48.0f);
    const float max_expected_distance_sq = max_expected_distance * max_expected_distance;
    const bool require_contact_band = env_bool("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_FALLBACK_REQUIRE_CONTACT_BAND", true);
    const bool prefer_trained = visual_enemy_host_preference_configured();
    const float preferred_forward = env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_PREFERRED_FORWARD", 16.0f);
    const float expected_weight = env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_EXPECTED_WEIGHT", 0.15f);
    const float lateral_weight = env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_LATERAL_WEIGHT", 2.0f);
    const float forward_weight = env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_FORWARD_WEIGHT", 0.35f);

    bool found = false;
    XMarkVisualEnemyHost best{};
    float best_score = FLT_MAX;
    float best_expected_distance_sq = FLT_MAX;
    for (unsigned int pass = 0; pass < (prefer_trained ? 2u : 1u); ++pass) {
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            const XMarkVisualEnemyHost &host = g_visual_enemy_hosts[i];
            if (!host.active) {
                continue;
            }
            const bool trained_match = visual_enemy_host_matches_preferred_training(host);
            if (prefer_trained && pass == 0 && !trained_match) {
                continue;
            }
            if (require_frame_overlap &&
                !basic_frame_state_overlaps_visual_host(overlap_state, host)) {
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
            const bool in_contact_band = runtime_target_in_basic_contact_band(visual_target, player_api, direction);
            if (require_contact_band && !in_contact_band) {
                continue;
            }

            const float expected_dx = host.position.x - expected_visual_position.x;
            const float expected_dy = host.position.y - expected_visual_position.y;
            const float expected_distance_sq = (expected_dx * expected_dx) + (expected_dy * expected_dy);
            if (!in_contact_band && expected_distance_sq > max_expected_distance_sq) {
                continue;
            }

            const float forward_delta = forward - preferred_forward;
            float score =
                (expected_distance_sq * expected_weight) +
                (lateral * lateral * lateral_weight) +
                (forward_delta * forward_delta * forward_weight);
            if (trained_match) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
            }
            if (!found || score < best_score) {
                found = true;
                best = host;
                best_score = score;
                best_expected_distance_sq = expected_distance_sq;
            }
        }
        if (found || !prefer_trained) {
            break;
        }
    }

    if (!found) {
        return false;
    }
    best.distance_sq = best_expected_distance_sq;
    *host_out = best;
    return true;
}

bool runtime_target_in_basic_side_edge_band(const XMarkRuntimeTarget &target, const Vec3 &player_api, int direction) {
    return runtime_target_in_basic_contact_band(target, player_api, direction);
}

bool runtime_target_in_facing_cone(const XMarkRuntimeTarget &target, const Vec3 &player_api) {
    float forward = 0.0f;
    float lateral = 0.0f;
    runtime_target_direction_delta(target, player_api, &forward, &lateral);
    const float min_forward = env_float("MINA_XMARK_G_RUNTIME_FACING_MIN_FORWARD", 4.0f);
    const float max_lateral = env_float("MINA_XMARK_G_RUNTIME_FACING_MAX_LATERAL", 32.0f);
    return forward >= min_forward && lateral <= max_lateral;
}

float runtime_target_score(XMarkRuntimeTarget *target, const Vec3 &player_api) {
    if (!target) {
        return FLT_MAX;
    }
    runtime_target_direction_delta(*target, player_api, &target->forward_delta, &target->lateral_delta);
    target->facing_match = runtime_target_in_facing_cone(*target, player_api);

    const float dist = std::sqrt(std::max(0.0f, target->distance_sq));
    const float preferred_forward = env_float("MINA_XMARK_G_RUNTIME_PREFERRED_FORWARD", 18.0f);
    const float lane_weight = env_float("MINA_XMARK_G_RUNTIME_LANE_WEIGHT", 1.7f);
    const float forward_weight = env_float("MINA_XMARK_G_RUNTIME_FORWARD_WEIGHT", 0.65f);
    const float distance_weight = env_float("MINA_XMARK_G_RUNTIME_DISTANCE_WEIGHT", 0.2f);
    const float not_facing_penalty = env_float("MINA_XMARK_G_RUNTIME_NOT_FACING_PENALTY", 300.0f);
    const float near_player_radius = env_float("MINA_XMARK_G_RUNTIME_NEAR_PLAYER_RADIUS", 8.0f);
    const float near_player_penalty = env_float("MINA_XMARK_G_RUNTIME_NEAR_PLAYER_PENALTY", 35.0f);
    const float no_health_penalty = env_float("MINA_XMARK_G_RUNTIME_NO_HEALTH_PENALTY", 10000.0f);
    const float preferred_forward_delta = target->forward_delta - preferred_forward;

    float score =
        (target->lateral_delta * target->lateral_delta * lane_weight) +
        (preferred_forward_delta * preferred_forward_delta * forward_weight) +
        (target->distance_sq * distance_weight);
    if (!target->facing_match) {
        score += not_facing_penalty;
    }
    if (dist < near_player_radius) {
        const float delta = near_player_radius - dist;
        score += delta * delta * near_player_penalty;
    }
    if (!target->health_like) {
        score += no_health_penalty;
    }
    target->score = score;
    return score;
}

bool find_nearest_runtime_target(XMarkRuntimeTarget *out_target, unsigned int *out_candidates, unsigned int *out_reads) {
    if (out_target) {
        *out_target = XMarkRuntimeTarget{};
    }
    if (out_candidates) {
        *out_candidates = 0;
    }
    if (out_reads) {
        *out_reads = 0;
    }

    constexpr unsigned int kMaxTargets = 96;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int pointers_read = 0;
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    const unsigned int target_count = collect_runtime_targets(targets, kMaxTargets, &pointers_read, &player_api, nullptr);

    if (out_candidates) {
        *out_candidates = target_count;
    }
    if (out_reads) {
        *out_reads = pointers_read;
    }
    if (target_count == 0) {
        return false;
    }

    bool found_health_like = false;
    unsigned int best_index = 0;
    float best_score = FLT_MAX;
    for (unsigned int i = 0; i < target_count; ++i) {
        const float score = runtime_target_score(&targets[i], player_api);
        if (targets[i].health_like && (!found_health_like || score < best_score)) {
            found_health_like = true;
            best_score = score;
            best_index = i;
        }
    }
    if (!found_health_like && env_bool("MINA_XMARK_G_RUNTIME_REQUIRE_HEALTHLIKE", true)) {
        return false;
    }
    if (!found_health_like) {
        best_score = runtime_target_score(&targets[0], player_api);
        best_index = 0;
        for (unsigned int i = 1; i < target_count; ++i) {
            const float score = runtime_target_score(&targets[i], player_api);
            if (score < best_score) {
                best_score = score;
                best_index = i;
            }
        }
    }
    if (out_target) {
        *out_target = targets[best_index];
    }
    return true;
}

bool runtime_health_history_for_target(const XMarkRuntimeTarget &target, float *health_out) {
    const unsigned int count = g_runtime_health_history_count <
            static_cast<unsigned int>(sizeof(g_runtime_health_history) / sizeof(g_runtime_health_history[0]))
        ? g_runtime_health_history_count
        : static_cast<unsigned int>(sizeof(g_runtime_health_history) / sizeof(g_runtime_health_history[0]));
    for (unsigned int i = 0; i < count; ++i) {
        const XMarkHealthBaseline &entry = g_runtime_health_history[i];
        const bool entry_official = entry.official && entry.official_combat_core;
        const bool target_official = target.official_follow && target.official_combat_core;
        if (entry_official || target_official) {
            if (!entry_official ||
                !target_official ||
                entry.official_combat_core != target.official_combat_core) {
                continue;
            }
            if (health_out) {
                *health_out = entry.health_value;
            }
            return true;
        }
        if (entry.entity == target.entity &&
            (!env_bool("MINA_XMARK_BASIC_HEALTH_MATCH_OFFSET", true) || entry.health_offset == target.health_offset)) {
            if (health_out) {
                *health_out = entry.health_value;
            }
            return true;
        }
    }
    return false;
}

void refresh_runtime_health_history(unsigned long long now_ms, bool force) {
    if (!force) {
        if (!env_bool("MINA_XMARK_HEALTH_HISTORY_ENABLED", false)) {
            return;
        }
        if (g_basic_attack_probe.active) {
            return;
        }
        const unsigned int interval_ms = env_uint("MINA_XMARK_HEALTH_HISTORY_INTERVAL_MS", 120);
        if (now_ms - g_last_runtime_health_history_ms < interval_ms) {
            return;
        }
    }

    constexpr unsigned int kMaxTargets = 96;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int reads = 0;
    const unsigned int count = collect_runtime_targets(targets, kMaxTargets, &reads, nullptr, nullptr);
    const unsigned int max_history = static_cast<unsigned int>(sizeof(g_runtime_health_history) / sizeof(g_runtime_health_history[0]));
    g_runtime_health_history_count = 0;
    for (unsigned int i = 0; i < count && g_runtime_health_history_count < max_history; ++i) {
        if (!targets[i].health_like) {
            continue;
        }
        XMarkHealthBaseline &entry = g_runtime_health_history[g_runtime_health_history_count++];
        entry.entity = targets[i].entity;
        entry.official_combat_core = targets[i].official_combat_core;
        entry.official = targets[i].official_follow && targets[i].official_combat_core;
        entry.health_offset = targets[i].health_offset;
        entry.health_value = targets[i].health_value;
    }
    g_last_runtime_health_history_ms = now_ms;
}

bool basic_probe_already_marked(uintptr_t entity) {
    const unsigned int max_marked = static_cast<unsigned int>(sizeof(g_basic_attack_probe.marked) / sizeof(g_basic_attack_probe.marked[0]));
    const unsigned int count = g_basic_attack_probe.marked_count < max_marked ? g_basic_attack_probe.marked_count : max_marked;
    for (unsigned int i = 0; i < count; ++i) {
        if (g_basic_attack_probe.marked[i] == entity) {
            return true;
        }
    }
    return false;
}

void basic_probe_remember_marked(uintptr_t entity) {
    if (!entity || basic_probe_already_marked(entity)) {
        return;
    }
    const unsigned int index = g_basic_attack_probe.marked_count % static_cast<unsigned int>(sizeof(g_basic_attack_probe.marked) / sizeof(g_basic_attack_probe.marked[0]));
    g_basic_attack_probe.marked[index] = entity;
    ++g_basic_attack_probe.marked_count;
}

void basic_probe_note_mark_success() {
    ++g_basic_attack_probe.marked_enemy_count;
}

void basic_probe_copy_marked_from_previous(const XMarkBasicAttackProbe &previous) {
    const unsigned int max_marked =
        static_cast<unsigned int>(sizeof(previous.marked) / sizeof(previous.marked[0]));
    const unsigned int count = previous.marked_count < max_marked ? previous.marked_count : max_marked;
    for (unsigned int i = 0; i < count; ++i) {
        basic_probe_remember_marked(previous.marked[i]);
    }
    g_basic_attack_probe.marked_enemy_count = previous.marked_enemy_count;
}

bool maybe_mark_muriel_on_basic_contact(
    unsigned long long now_ms,
    const Vec3 &player_api,
    unsigned int max_marks) {
    if (!env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) ||
        !g_basic_attack_probe.active ||
        now_ms >= g_basic_attack_probe.expires_ms ||
        !g_basic_attack_probe.contact_active ||
        g_basic_attack_probe.marked_enemy_count >= max_marks) {
        return false;
    }

    if (g_mina) {
        int game_state = -1;
        __try {
            game_state = g_mina->GetCurrentGameState();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            game_state = -1;
        }
        if (game_state == GAMESTATE_TEXTDISPLAY) {
            return false;
        }
    }

    XMarkBasicFrameState frame_state{};
    if (!read_basic_frame_state(&frame_state) ||
        !xmark_basic_frame_name_allowed(frame_state.frame)) {
        return false;
    }
    const unsigned int muriel_frame_max_age_ms =
        std::max(1u, env_uint("MINA_XMARK_MURIEL_BASIC_FRAME_MAX_AGE_MS", 80));
    if (!frame_state.tick ||
        now_ms > frame_state.tick + muriel_frame_max_age_ms ||
        (g_attack_j_pressed_ms && frame_state.tick < g_attack_j_pressed_ms)) {
        return false;
    }
    const bool same_attack_press =
        g_attack_j_pressed_ms &&
        g_last_muriel_mark_attack_press_ms == g_attack_j_pressed_ms;
    const bool same_rendered_frame =
        frame_state.tick == g_last_muriel_mark_frame_tick &&
        frame_state.draw == g_last_muriel_mark_frame_draw;
    if (same_attack_press || same_rendered_frame) {
        return false;
    }

    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        XMarkOfficialEnemyHost host =
            official_enemy_host_with_resolved_refs(g_official_enemy_hosts[i]);
        if (!host.active || !host.entity || !host.combat_core ||
            !xmark_official_host_is_muriel(host) ||
            basic_probe_already_marked(host.entity) ||
            basic_probe_already_marked(host.combat_core)) {
            continue;
        }

        XMarkRuntimeTarget target{};
        if (!runtime_target_from_official_enemy_host(host, &target)) {
            continue;
        }
        target.anchor = host.position;
        target.position = host.position;
        target.contact_half_w = std::max(
            0.1f,
            env_float("MINA_XMARK_MURIEL_CONTACT_HALF_W", 0.65f));
        target.contact_half_h = std::max(
            0.1f,
            env_float("MINA_XMARK_MURIEL_CONTACT_HALF_H", 0.80f));
        runtime_target_direction_delta_for_direction(
            target,
            player_api,
            frame_state.direction,
            &target.forward_delta,
            &target.lateral_delta);
        target.facing_match = true;
        if (!muriel_drawn_basic_frame_overlaps_target(frame_state, target)) {
            continue;
        }
        target.position.x += env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_X", 0.0f);
        target.position.y += env_float("MINA_XMARK_MURIEL_MARK_CENTER_OFFSET_Y", 0.6f);
        if (!spawn_tracked_xmark_for_target("muriel-basic-contact", target)) {
            continue;
        }

        basic_probe_remember_marked(host.entity);
        basic_probe_remember_marked(host.combat_core);
        basic_probe_note_mark_success();
        g_last_muriel_mark_attack_press_ms = g_attack_j_pressed_ms;
        g_last_muriel_mark_frame_tick = frame_state.tick;
        g_last_muriel_mark_frame_draw = frame_state.draw;
        if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
            g_mina->Log("XMarkBurn Muriel marked from basic weapon contact; HP unchanged.\n");
        }
        return true;
    }
    return false;
}

void maybe_mark_muriel_on_current_basic_frame(unsigned long long now_ms) {
    if (!g_basic_attack_probe.active || !g_basic_attack_probe.contact_active) {
        return;
    }
    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    maybe_mark_muriel_on_basic_contact(
        now_ms,
        player_api,
        env_uint("MINA_XMARK_BASIC_HEALTH_MAX_MARKS", 16));
}

bool basic_probe_baselines_identity_match(
    const XMarkHealthBaseline &a,
    const XMarkHealthBaseline &b) {
    if (a.official || b.official) {
        return a.official &&
            b.official &&
            a.official_combat_core &&
            b.official_combat_core &&
            a.official_combat_core == b.official_combat_core;
    }
    if (!a.entity || !b.entity || a.entity != b.entity) {
        return false;
    }
    return !env_bool("MINA_XMARK_BASIC_HEALTH_MATCH_OFFSET", true) ||
        a.health_offset == b.health_offset;
}

bool basic_probe_add_or_merge_baseline(const XMarkHealthBaseline &candidate) {
    const unsigned int max_baselines =
        static_cast<unsigned int>(sizeof(g_basic_attack_probe.baselines) / sizeof(g_basic_attack_probe.baselines[0]));
    for (unsigned int i = 0; i < g_basic_attack_probe.baseline_count; ++i) {
        XMarkHealthBaseline &existing = g_basic_attack_probe.baselines[i];
        if (!basic_probe_baselines_identity_match(existing, candidate)) {
            continue;
        }

        const float preserved_health = std::max(existing.health_value, candidate.health_value);
        const float preserved_max = std::max(existing.health_max, candidate.health_max);
        const uintptr_t preserved_core = candidate.official_combat_core
            ? candidate.official_combat_core
            : existing.official_combat_core;
        const bool preserved_official = candidate.official || (existing.official && preserved_core);

        existing = candidate;
        existing.official_combat_core = preserved_core;
        existing.official = preserved_official;
        existing.health_value = preserved_health;
        existing.health_max = preserved_max;
        return true;
    }

    if (g_basic_attack_probe.baseline_count >= max_baselines) {
        return false;
    }
    g_basic_attack_probe.baselines[g_basic_attack_probe.baseline_count++] = candidate;
    return true;
}

bool basic_probe_baseline_for_target(const XMarkRuntimeTarget &target, float *baseline_out) {
    for (unsigned int i = 0; i < g_basic_attack_probe.baseline_count; ++i) {
        const XMarkHealthBaseline &baseline = g_basic_attack_probe.baselines[i];
        const bool baseline_official = baseline.official && baseline.official_combat_core;
        const bool target_official = target.official_follow && target.official_combat_core;
        if (baseline_official || target_official) {
            if (!baseline_official ||
                !target_official ||
                baseline.official_combat_core != target.official_combat_core) {
                continue;
            }
            if (baseline_out) {
                *baseline_out = baseline.health_value;
            }
            return true;
        }
        if (baseline.entity == target.entity &&
            (!env_bool("MINA_XMARK_BASIC_HEALTH_MATCH_OFFSET", true) || baseline.health_offset == target.health_offset)) {
            if (baseline_out) {
                *baseline_out = baseline.health_value;
            }
            return true;
        }
    }
    return false;
}

bool read_health_at_offset(uintptr_t entity, unsigned int offset, unsigned int health_kind, float *health_out, float *max_out) {
    if (!entity || !probable_heap_object(entity)) {
        return false;
    }

    const auto try_float_pair = [&]() -> bool {
        float a = 0.0f;
        float b = 0.0f;
        return safe_read_float(entity + offset, &a) &&
            safe_read_float(entity + offset + 4, &b) &&
            plausible_health_pair(a, b, health_out, max_out);
    };

    const auto try_int_pair = [&]() -> bool {
        uint32_t a = 0u;
        uint32_t b = 0u;
        if (!safe_read_u32(entity + offset, &a) || !safe_read_u32(entity + offset + 4, &b)) {
            return false;
        }
        return plausible_health_pair(static_cast<float>(a), static_cast<float>(b), health_out, max_out);
    };

    if (health_kind == 1u && try_float_pair()) {
        return true;
    }
    if (health_kind == 2u && try_int_pair()) {
        return true;
    }
    return try_float_pair() || try_int_pair();
}

bool write_damage_to_health_at_offset(
    uintptr_t entity,
    unsigned int offset,
    unsigned int health_kind,
    float damage,
    float *new_health_out = nullptr) {
    if (!entity || !offset || !probable_heap_object(entity)) {
        return false;
    }
    const float clamped_damage = std::max(0.0f, damage);
    if (clamped_damage <= 0.0f) {
        return false;
    }

    const auto try_float_pair = [&]() -> bool {
        float a = 0.0f;
        float b = 0.0f;
        float current = 0.0f;
        float max_value = 0.0f;
        if (!safe_read_float(entity + offset, &a) ||
            !safe_read_float(entity + offset + 4, &b) ||
            !plausible_health_pair(a, b, &current, &max_value)) {
            return false;
        }
        const bool first_is_current = a <= b;
        const float next = std::max(0.0f, current - clamped_damage);
        const uintptr_t write_address = entity + offset + (first_is_current ? 0u : 4u);
        if (!safe_write_float(write_address, next)) {
            return false;
        }
        if (new_health_out) {
            *new_health_out = next;
        }
        return true;
    };

    const auto try_int_pair = [&]() -> bool {
        uint32_t a = 0u;
        uint32_t b = 0u;
        float current = 0.0f;
        float max_value = 0.0f;
        if (!safe_read_u32(entity + offset, &a) ||
            !safe_read_u32(entity + offset + 4, &b) ||
            !plausible_health_pair(static_cast<float>(a), static_cast<float>(b), &current, &max_value)) {
            return false;
        }
        const bool first_is_current = a <= b;
        const uint32_t amount = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(clamped_damage)));
        const uint32_t current_u32 = first_is_current ? a : b;
        const uint32_t next = current_u32 > amount ? current_u32 - amount : 0u;
        const uintptr_t write_address = entity + offset + (first_is_current ? 0u : 4u);
        if (!safe_write_u32(write_address, next)) {
            return false;
        }
        if (new_health_out) {
            *new_health_out = static_cast<float>(next);
        }
        return true;
    };

    if (health_kind == 1u && try_float_pair()) {
        return true;
    }
    if (health_kind == 2u && try_int_pair()) {
        return true;
    }
    return try_float_pair() || try_int_pair();
}

XMarkBurnEffect *find_xmark_burn_effect(
    uintptr_t target,
    unsigned long long visual_key,
    uintptr_t official_combat_core = 0) {
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active) {
            continue;
        }
        if ((official_combat_core &&
             burn.official_combat_core == official_combat_core) ||
            (target && burn.target == target) ||
            (visual_key && burn.visual_key == visual_key)) {
            return &burn;
        }
    }
    return nullptr;
}

constexpr char kMurielRegularChargedDialogue[] =
    "Solid slice, nice speed. Remember that the real power of a greatsword "
    "comes from the arms and core. <s>Hi-YAH!</s>";
constexpr char kMurielBurnDialogue[] =
    "<s><f cn=weap>A-AGHH! It burns!!!</f></s> ... Wait. Actually I'm fine? "
    "<f cn=mina>Meens</f>, that sort of explosive fire is meant for enemies, okay?";

bool muriel_dialogue_memory_matches(uintptr_t address, const char *text) {
    if (!address || !text) {
        return false;
    }
    const size_t length = std::strlen(text);
    __try {
        return std::memcmp(reinterpret_cast<const void *>(address), text, length) == 0 &&
            *reinterpret_cast<const char *>(address + length) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void scan_muriel_regular_dialogue_targets(unsigned long long now_ms) {
    if (g_muriel_regular_dialogue_target_count ||
        g_muriel_regular_dialogue_scan_attempts >=
            env_uint("MINA_XMARK_MURIEL_DIALOGUE_SCAN_MAX_ATTEMPTS", 3) ||
        (g_last_muriel_regular_dialogue_scan_ms &&
         now_ms < g_last_muriel_regular_dialogue_scan_ms +
             env_uint("MINA_XMARK_MURIEL_DIALOGUE_SCAN_RETRY_MS", 1000))) {
        return;
    }
    g_last_muriel_regular_dialogue_scan_ms = now_ms;
    ++g_muriel_regular_dialogue_scan_attempts;

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const uintptr_t max_address =
        reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
    const size_t needle_length = std::strlen(kMurielBurnDialogue);
    const size_t min_region_size = std::max<size_t>(
        0x10000,
        env_uint("MINA_XMARK_MURIEL_DIALOGUE_SCAN_MIN_REGION_KB", 1024) * 1024ull);
    const size_t max_region_size = std::max<size_t>(
        min_region_size,
        env_uint("MINA_XMARK_MURIEL_DIALOGUE_SCAN_MAX_REGION_MB", 16) * 1024ull * 1024ull);

    uintptr_t cursor = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    while (cursor < max_address &&
           g_muriel_regular_dialogue_target_count <
               static_cast<unsigned int>(sizeof(g_muriel_regular_dialogue_targets) /
                                         sizeof(g_muriel_regular_dialogue_targets[0]))) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<const void *>(cursor), &mbi, sizeof(mbi))) {
            break;
        }
        const uintptr_t base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const size_t region_size = mbi.RegionSize;
        const DWORD protection =
            mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
        const bool writable_private_region =
            mbi.State == MEM_COMMIT &&
            mbi.Type == MEM_PRIVATE &&
            (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY) &&
            region_size >= min_region_size &&
            region_size <= max_region_size;
        if (writable_private_region && region_size > needle_length) {
            __try {
                const char *begin = reinterpret_cast<const char *>(base);
                const char *end = begin + region_size;
                const char *match = begin;
                while (match + needle_length < end) {
                    match = std::search(
                        match,
                        end,
                        kMurielBurnDialogue,
                        kMurielBurnDialogue + needle_length);
                    if (match == end || match + needle_length >= end) {
                        break;
                    }
                    if (match[needle_length] == 0) {
                        g_muriel_regular_dialogue_targets[
                            g_muriel_regular_dialogue_target_count++] =
                                reinterpret_cast<uintptr_t>(match);
                    }
                    ++match;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        const uintptr_t next = base + region_size;
        if (next <= cursor) {
            break;
        }
        cursor = next;
    }
    if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Muriel dialogue targets found=%u attempt=%u.\n",
            g_muriel_regular_dialogue_target_count,
            g_muriel_regular_dialogue_scan_attempts);
        g_mina->Log(message);
    }
    if (g_muriel_regular_dialogue_target_count) {
        g_muriel_regular_dialogue_burn_text_active = true;
    }
}

bool muriel_burn_dialogue_active(unsigned long long now_ms) {
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active || !burn.suppress_damage || now_ms >= burn.expires_ms) {
            continue;
        }
        XMarkEnemyStatusRecord *status = xmark_enemy_status_by_id(burn.status_id);
        if (status && status->active && status->training_target &&
            status->phase == XMarkEnemyStatusPhase::Burning &&
            now_ms < status->state_expires_ms) {
            return true;
        }
        XMarkOfficialEnemyHost host{};
        if (official_enemy_host_for_burn(burn, &host) &&
            xmark_official_host_is_muriel(host)) {
            return true;
        }
    }
    return false;
}

void apply_muriel_regular_dialogue_state(bool burn_active) {
    const char *replacement = burn_active
        ? kMurielBurnDialogue
        : kMurielRegularChargedDialogue;
    const size_t replacement_length = std::strlen(replacement);
    for (unsigned int i = 0; i < g_muriel_regular_dialogue_target_count; ++i) {
        const uintptr_t address = g_muriel_regular_dialogue_targets[i];
        if (!muriel_dialogue_memory_matches(address, kMurielBurnDialogue) &&
            !muriel_dialogue_memory_matches(address, kMurielRegularChargedDialogue)) {
            continue;
        }
        __try {
            std::memcpy(reinterpret_cast<void *>(address), replacement, replacement_length + 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
    }
    g_muriel_regular_dialogue_burn_text_active = burn_active;
}

void update_muriel_regular_dialogue_state(unsigned long long now_ms, unsigned int room_index) {
    if (!env_bool("MINA_XMARK_MURIEL_DYNAMIC_DIALOGUE_ENABLED", true)) {
        return;
    }
    if (!g_muriel_regular_dialogue_target_count && room_index == 14u) {
        scan_muriel_regular_dialogue_targets(now_ms);
    }
    if (!g_muriel_regular_dialogue_target_count) {
        return;
    }
    const bool burn_active = muriel_burn_dialogue_active(now_ms);
    if (burn_active != g_muriel_regular_dialogue_burn_text_active) {
        apply_muriel_regular_dialogue_state(burn_active);
        if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
            g_mina->Log(
                burn_active
                    ? "XMarkBurn Muriel dialogue switched to burn reaction.\n"
                    : "XMarkBurn Muriel dialogue restored to regular charged-slam reaction.\n");
        }
    }
}

