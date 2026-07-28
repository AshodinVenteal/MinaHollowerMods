bool spawn_tracked_xmark_for_target(const char *reason, const XMarkRuntimeTarget &target) {
    const bool muriel_training_target =
        target.suppress_hud && reason && xmark_ascii_contains_ci(reason, "muriel");
    const unsigned int duration_ms = muriel_training_target
        ? env_uint("MINA_XMARK_MURIEL_MARK_DURATION_MS", 10000)
        : env_uint("MINA_XMARK_MARK_DURATION_MS", 3000);
    const unsigned long long now_ms = xmark_status_now_ms();
    if (XMarkEnemyStatusRecord *status =
            xmark_enemy_status_find(target.entity, target.official_combat_core)) {
        if (status->phase == XMarkEnemyStatusPhase::Burning &&
            now_ms < status->state_expires_ms) {
            return false;
        }
    }
    const Vec3 mark_position = xmark_attachment_mark_position(target.position);
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        const bool same_runtime_target = attachment.target == target.entity;
        const bool same_visual_target =
            target.visual_follow &&
            attachment.visual_follow &&
            target.visual_key &&
            attachment.visual_key == target.visual_key;
        const bool same_official_target =
            target.official_follow &&
            attachment.official_follow &&
            target.official_combat_core &&
            attachment.official_combat_core == target.official_combat_core;
        const bool same_source_slot =
            attachment.target_source_base &&
            attachment.target_source_base == target.source_base &&
            attachment.target_source_offset == target.source_offset;
        if (attachment.active && (same_runtime_target || same_visual_target || same_official_target || same_source_slot)) {
            const float refresh_jump = env_float("MINA_XMARK_ATTACHMENT_REFRESH_MAX_JUMP", 3.0f);
            const bool refresh_anchor_stable =
                !attachment.has_last_position ||
                target.visual_follow ||
                target.official_follow ||
                xy_distance_sq(attachment.last_position, mark_position) <= refresh_jump * refresh_jump;
            if (refresh_anchor_stable) {
                attachment.target = target.entity;
                attachment.visual_key = target.visual_key;
                attachment.visual_follow = target.visual_follow;
                attachment.official_follow = target.official_follow;
                attachment.suppress_hud = target.suppress_hud;
                attachment.official_combat_core = target.official_combat_core;
                attachment.target_source_base = target.source_base;
                attachment.target_source_offset = target.source_offset;
                attachment.target_anchor_offset = target.anchor_offset;
                attachment.target_health_offset = target.health_offset;
                attachment.target_health_kind = target.health_kind;
                attachment.target_health_like = target.health_like;
                if (target.health_max > 0.0f && std::isfinite(target.health_value)) {
                    attachment.observed_health = target.health_value;
                    attachment.observed_health_ms = now_ms;
                    attachment.recent_health_drop_ms = 0;
                    attachment.has_observed_health = true;
                }
                attachment.last_position = mark_position;
                attachment.render_half_w = target.render_half_w > 0.0f ? target.render_half_w : xmark_default_render_half_w();
                attachment.render_half_h = target.render_half_h > 0.0f ? target.render_half_h : xmark_default_render_half_h();
                attachment.contact_half_w = std::max(0.0f, target.contact_half_w);
                attachment.contact_half_h = std::max(0.0f, target.contact_half_h);
                attachment.has_last_position = true;
                if (target.official_follow) {
                    if (!attachment.has_official_body_offset) {
                        capture_xmark_official_body_offset(attachment, target);
                    }
                    attachment.runtime_visual_offset = Vec3{};
                    attachment.has_runtime_visual_offset = false;
                }
                copy_visual_token(attachment.visual_entry, sizeof(attachment.visual_entry), target.visual_entry);
                copy_visual_token(attachment.visual_stem, sizeof(attachment.visual_stem), target.visual_stem);
                copy_visual_token(attachment.visual_catalog, sizeof(attachment.visual_catalog), target.visual_catalog);
                if (target.visual_follow) {
                    attachment.last_visual_resolved_ms = now_ms;
                    attachment.last_visual_missing_ms = 0;
                    attachment.visual_missing_count = 0;
                }
            }
            if (!attachment.render_backend &&
                env_bool("MINA_XMARK_ATTACHMENT_REBIND_ON_REFRESH", true) &&
                attachment.effect_position_count == 0 &&
                attachment.child_position_count == 0 &&
                attachment.extra_position_count == 0) {
                bind_xmark_attachment_position_slots(
                    attachment,
                    attachment.has_last_position ? attachment.last_position : mark_position);
            }
            attachment.expires_ms = now_ms + duration_ms;
            attachment.started_ms = now_ms;
            if (g_mina && env_bool("MINA_XMARK_TRACKED_MARK_LOG", true)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn tracked XMark refreshed reason=%s target=0x%p targetAnchor=0x%X anchorStable=%u durationMs=%u\n",
                    reason ? reason : "<none>",
                    reinterpret_cast<void *>(target.entity),
                    target.anchor_offset,
                    refresh_anchor_stable ? 1u : 0u,
                    duration_ms);
                g_mina->Log(message);
            }
            if (attachment.suppress_hud) {
                clear_xmark_hud_mark(target.entity);
            } else {
                upsert_xmark_hud_mark(target.entity, attachment.expires_ms);
                record_xmark_hud_mark_health(
                    target.entity,
                    target.health_value,
                    target.health_max);
            }
            XMarkEnemyStatusRecord *status = xmark_enemy_status_bind(
                attachment.target,
                attachment.official_combat_core,
                XMarkEnemyStatusPhase::Marked,
                now_ms,
                attachment.expires_ms);
            attachment.status_id = xmark_enemy_status_id(status);
            if (status) {
                status->training_target = attachment.suppress_hud;
                status->attachment_index =
                    static_cast<unsigned int>(&attachment - g_xmark_attachments) + 1u;
                status->burn_index = 0;
                capture_xmark_marked_palette(*status);
            }
            publish_current_xmark_hud_state(now_ms);
            return true;
        }
    }

    const bool use_marker_debug_draw = xmark_marker_debug_draw_ensure_initialized();
    const bool allow_marker_render_fallback =
        !use_marker_debug_draw &&
        env_bool("MINA_XMARK_MARK_RENDER_BACKEND_FALLBACK", false);
    const bool use_shared_render_backend =
        allow_marker_render_fallback && xmark_render_backend_ensure_initialized();
    const bool use_render_backend = use_marker_debug_draw || use_shared_render_backend;
    uintptr_t effect = 0;
    uintptr_t child = 0;
    if (!use_marker_debug_draw && env_bool("MINA_XMARK_MARK_REQUIRE_DEBUG_DRAW", false)) {
        if (g_mina) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn tracked XMark skipped reason=%s: marker DebugDraw required but unavailable status=%s renderFallback=%u\n",
                reason ? reason : "<none>",
                g_xmark_marker_debug_draw_status,
                allow_marker_render_fallback ? 1u : 0u);
            g_mina->Log(message);
        }
        return false;
    }
    if (!use_render_backend && g_xmark_render_backend_required) {
        if (g_mina) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn tracked XMark skipped reason=%s: render backend required but unavailable status=%s\n",
                reason ? reason : "<none>",
                g_xmark_render_backend_status);
            g_mina->Log(message);
        }
        return false;
    }
    if (!use_render_backend && !spawn_direct_xmark_at(reason, mark_position, &effect, &child)) {
        return false;
    }

    XMarkAttachment *slot = nullptr;
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (attachment.active && attachment.target == target.entity) {
            slot = &attachment;
            break;
        }
    }
    if (!slot) {
        for (XMarkAttachment &attachment : g_xmark_attachments) {
            if (!attachment.active) {
                slot = &attachment;
                break;
            }
        }
    }
    if (!slot) {
        slot = &g_xmark_attachments[0];
    }

    *slot = XMarkAttachment{};
    slot->active = true;
    slot->target = target.entity;
    slot->effect = effect;
    slot->child = child;
    slot->visual_key = target.visual_key;
    slot->visual_follow = target.visual_follow;
    slot->official_follow = target.official_follow;
    slot->suppress_hud = target.suppress_hud;
    slot->official_combat_core = target.official_combat_core;
    slot->target_source_base = target.source_base;
    slot->target_source_offset = target.source_offset;
    slot->target_anchor_offset = target.anchor_offset;
    slot->target_health_offset = target.health_offset;
    slot->target_health_kind = target.health_kind;
    slot->target_health_like = target.health_like;
    slot->apply_sfx_pending = !muriel_training_target;
    if (target.health_max > 0.0f && std::isfinite(target.health_value)) {
        slot->observed_health = target.health_value;
        slot->observed_health_ms = now_ms;
        slot->has_observed_health = true;
    }
    slot->expires_ms = now_ms + duration_ms;
    slot->started_ms = now_ms;
    slot->last_position = mark_position;
    slot->render_half_w = target.render_half_w > 0.0f ? target.render_half_w : xmark_default_render_half_w();
    slot->render_half_h = target.render_half_h > 0.0f ? target.render_half_h : xmark_default_render_half_h();
    slot->contact_half_w = std::max(0.0f, target.contact_half_w);
    slot->contact_half_h = std::max(0.0f, target.contact_half_h);
    slot->has_last_position = true;
    slot->render_backend = use_render_backend;
    slot->marker_debug_draw = use_marker_debug_draw;
    capture_xmark_official_body_offset(*slot, target);
    copy_visual_token(slot->visual_entry, sizeof(slot->visual_entry), target.visual_entry);
    copy_visual_token(slot->visual_stem, sizeof(slot->visual_stem), target.visual_stem);
    copy_visual_token(slot->visual_catalog, sizeof(slot->visual_catalog), target.visual_catalog);
    if (slot->visual_follow) {
        slot->last_visual_resolved_ms = now_ms;
        slot->last_visual_missing_ms = 0;
        slot->visual_missing_count = 0;
    }
    if (slot->suppress_hud) {
        clear_xmark_hud_mark(slot->target);
    } else {
        upsert_xmark_hud_mark(slot->target, slot->expires_ms);
        record_xmark_hud_mark_health(
            slot->target,
            target.health_value,
            target.health_max);
    }
    XMarkEnemyStatusRecord *status = xmark_enemy_status_bind(
        slot->target,
        slot->official_combat_core,
        XMarkEnemyStatusPhase::Marked,
        now_ms,
        slot->expires_ms);
    slot->status_id = xmark_enemy_status_id(status);
    if (status) {
        status->training_target = slot->suppress_hud;
        status->attachment_index =
            static_cast<unsigned int>(slot - g_xmark_attachments) + 1u;
        status->burn_index = 0;
        capture_xmark_marked_palette(*status);
    }

    if (!slot->render_backend) {
        bind_xmark_attachment_position_slots(*slot, mark_position);
    }

    if (g_mina && env_bool("MINA_XMARK_TRACKED_MARK_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn tracked XMark target=0x%p officialCore=0x%p effect=0x%p child=0x%p targetAnchor=0x%X backend=%s effectPos=%s0x%X/%u childPos=%s0x%X/%u extraSlots=%u durationMs=%u\n",
            reinterpret_cast<void *>(slot->target),
            reinterpret_cast<void *>(slot->official_combat_core),
            reinterpret_cast<void *>(slot->effect),
            reinterpret_cast<void *>(slot->child),
            slot->target_anchor_offset,
            slot->marker_debug_draw ? "debug-draw" : (slot->render_backend ? "render" : "anim-effect"),
            slot->effect_position_base ? "" : "missing/",
            slot->effect_position_offset,
            slot->effect_position_count,
            slot->child_position_base ? "" : "missing/",
            slot->child_position_offset,
            slot->child_position_count,
            slot->extra_position_count,
            duration_ms);
        g_mina->Log(message);
    }
    publish_current_xmark_hud_state(now_ms);
    return slot->render_backend || slot->effect_position_base || slot->child_position_base || slot->extra_position_count > 0;
}

bool spawn_tracked_xmark_for_visual_host(const char *reason, const XMarkVisualEnemyHost &host) {
    XMarkRuntimeTarget target{};
    const unsigned long long now_ms = GetTickCount64();
    const bool use_runtime_target =
        env_bool("MINA_XMARK_VISUAL_HOST_MARK_USE_RUNTIME_TARGET", true) &&
        find_runtime_target_near_visual_host(host, &target, now_ms);
    if (!use_runtime_target && !runtime_target_from_visual_host(host, &target)) {
        return false;
    }
    if (g_mina && env_bool("MINA_XMARK_VISUAL_HOST_MARK_LOG", true)) {
        char message[640]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn visual-host mark reason=%s key=0x%llX runtime=%u target=0x%p entry=%s stem=%s catalog=%s tex=%ux%u pos=(%.3f, %.3f, %.3f) dist=%.3f\n",
            reason ? reason : "<none>",
            host.key,
            use_runtime_target ? 1u : 0u,
            reinterpret_cast<void *>(target.entity),
            host.entry[0] ? host.entry : "-",
            host.stem[0] ? host.stem : "-",
            host.catalog[0] ? host.catalog : "-",
            host.texture_width,
            host.texture_height,
            static_cast<double>(target.position.x),
            static_cast<double>(target.position.y),
            static_cast<double>(target.position.z),
            static_cast<double>(std::sqrt(std::max(0.0f, target.distance_sq))));
        g_mina->Log(message);
    }
    return spawn_tracked_xmark_for_target(reason, target);
}

bool xmark_visual_host_currently_present(unsigned long long key) {
    if (!key) {
        return false;
    }
    for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
        if (g_visual_enemy_hosts[i].active && g_visual_enemy_hosts[i].key == key) {
            return true;
        }
    }
    return false;
}

bool xmark_attachment_active_for_visual_host(const XMarkVisualEnemyHost &host, unsigned long long now_ms) {
    if (!host.active || !host.key) {
        return false;
    }
    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || now_ms >= attachment.expires_ms) {
            continue;
        }
        if (attachment.visual_follow && attachment.visual_key == host.key) {
            return true;
        }
    }
    return false;
}

bool refresh_active_xmark_attachment_for_visual_host(const XMarkVisualEnemyHost &host, unsigned long long now_ms) {
    if (!host.active || !host.key) {
        return false;
    }

    bool repaired = false;
    float half_w = xmark_default_render_half_w();
    float half_h = xmark_default_render_half_h();
    xmark_visual_host_render_halves(host, &half_w, &half_h);

    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || now_ms >= attachment.expires_ms) {
            continue;
        }
        if (!attachment.visual_follow || attachment.visual_key != host.key) {
            continue;
        }

        const Vec3 host_render_position = visual_host_pinned_render_position(attachment, host, now_ms);
        const Vec3 mark_position = xmark_attachment_mark_position(host_render_position);
        attachment.last_position = mark_position;
        attachment.has_last_position = true;
        attachment.render_half_w = half_w;
        attachment.render_half_h = half_h;
        attachment.last_visual_resolved_ms = now_ms;
        attachment.last_visual_missing_ms = 0;
        attachment.visual_missing_count = 0;
        copy_visual_token(attachment.visual_entry, sizeof(attachment.visual_entry), host.entry);
        copy_visual_token(attachment.visual_stem, sizeof(attachment.visual_stem), host.stem);
        copy_visual_token(attachment.visual_catalog, sizeof(attachment.visual_catalog), host.catalog);
        repaired = true;
    }

    if (repaired) {
        ++g_pending_visual_mark_repairs;
    }
    return repaired;
}

bool link_xmark_visual_attachment_to_damage_target(
    unsigned long long visual_key,
    const XMarkRuntimeTarget &target,
    unsigned long long now_ms) {
    if (!visual_key) {
        return false;
    }

    bool linked = false;
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active ||
            now_ms >= attachment.expires_ms ||
            !attachment.visual_follow ||
            attachment.visual_key != visual_key) {
            continue;
        }
        const uintptr_t old_target = attachment.target;
        if (target.entity) {
            attachment.target = target.entity;
        }
        if (target.official_combat_core) {
            attachment.official_combat_core = target.official_combat_core;
            attachment.official_follow = true;
        } else if (target.official_follow) {
            attachment.official_follow = true;
        }
        if (target.health_like) {
            attachment.target_health_like = true;
            attachment.target_health_offset = target.health_offset;
            attachment.target_health_kind = target.health_kind;
        }
        if (target.render_half_w > 0.0f) {
            attachment.render_half_w = target.render_half_w;
        }
        if (target.render_half_h > 0.0f) {
            attachment.render_half_h = target.render_half_h;
        }
        if (env_bool("MINA_XMARK_LINK_OFFICIAL_KEEP_VISUAL_CENTER_OFFSET", false)) {
            const Vec3 current_visual_center = attachment.has_last_position
                ? xmark_attachment_target_position_from_mark(attachment.last_position)
                : target.position;
            attachment.runtime_visual_offset.x = current_visual_center.x - target.position.x;
            attachment.runtime_visual_offset.y = current_visual_center.y - target.position.y;
            attachment.runtime_visual_offset.z = current_visual_center.z - target.position.z;
            attachment.has_runtime_visual_offset = true;
            attachment.last_position = xmark_attachment_mark_position(current_visual_center);
            attachment.has_last_position = true;
        } else if (target.official_follow || target.official_combat_core) {
            attachment.runtime_visual_offset = Vec3{};
            attachment.has_runtime_visual_offset = false;
            capture_xmark_official_body_offset(attachment, target);
            attachment.last_position = xmark_attachment_mark_position(target.position);
            attachment.has_last_position = true;
        }
        if (old_target && old_target != attachment.target) {
            shorten_xmark_hud_mark(old_target, now_ms);
            if (!attachment.suppress_hud) {
                upsert_xmark_hud_mark(attachment.target, attachment.expires_ms);
            }
            publish_current_xmark_hud_state(now_ms);
        }
        if (g_mina && env_bool("MINA_XMARK_LINK_OFFICIAL_DAMAGE_TARGET_LOG", true)) {
            char message[640]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn linked visual XMark to official damage target key=0x%llX oldTarget=0x%p newTarget=0x%p officialCore=0x%p offset=(%.3f, %.3f, %.3f) pos=(%.3f, %.3f, %.3f)\n",
                visual_key,
                reinterpret_cast<void *>(old_target),
                reinterpret_cast<void *>(attachment.target),
                reinterpret_cast<void *>(attachment.official_combat_core),
                static_cast<double>(attachment.runtime_visual_offset.x),
                static_cast<double>(attachment.runtime_visual_offset.y),
                static_cast<double>(attachment.runtime_visual_offset.z),
                static_cast<double>(attachment.last_position.x),
                static_cast<double>(attachment.last_position.y),
                static_cast<double>(attachment.last_position.z));
            g_mina->Log(message);
        }
        linked = true;
    }
    return linked;
}

void refresh_visual_follow_attachments_for_render(unsigned long long now_ms) {
    static unsigned long long last_refresh_ms = 0;
    const unsigned int refresh_ms = env_uint("MINA_XMARK_RENDER_VISUAL_FOLLOW_REFRESH_MS", 8);
    if (refresh_ms &&
        last_refresh_ms &&
        now_ms >= last_refresh_ms &&
        now_ms - last_refresh_ms < refresh_ms) {
        return;
    }
    last_refresh_ms = now_ms;

    bool cleared_any = false;
    bool needs_visual_bridge = false;
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || now_ms >= attachment.expires_ms) {
            continue;
        }

        if (attachment.official_follow &&
            env_bool("MINA_XMARK_OFFICIAL_RENDER_BODY_CENTER_ENABLED", true)) {
            XMarkOfficialEnemyHost host{};
            if (official_enemy_host_for_attachment(attachment, &host) &&
                host.active) {
                attachment.target = host.entity;
                attachment.official_combat_core = host.combat_core;
                if (host.body_center_offset_valid) {
                    attachment.official_body_offset = host.body_center_offset;
                    attachment.has_official_body_offset = true;
                }
                attachment.runtime_visual_offset = Vec3{};
                attachment.has_runtime_visual_offset = false;
                attachment.last_position = xmark_attachment_mark_position(host.position);
                attachment.render_half_w = official_enemy_render_half_from_health(
                    host,
                    xmark_default_render_half_w());
                attachment.render_half_h = official_enemy_render_half_from_health(
                    host,
                    xmark_default_render_half_h());
                attachment.has_last_position = true;
                attachment.last_visual_resolved_ms = now_ms;
                attachment.last_visual_missing_ms = 0;
                attachment.visual_missing_count = 0;
                continue;
            }
        }

        if (attachment.visual_follow && attachment.visual_key) {
            needs_visual_bridge = true;
        }
    }

    if (!needs_visual_bridge) {
        return;
    }

    if (!read_enemy_visual_state_file(now_ms, true)) {
        if (g_visual_enemy_state_explicit_zero) {
            for (XMarkAttachment &attachment : g_xmark_attachments) {
                if (!attachment.active || attachment.official_follow || !attachment.visual_follow) {
                    continue;
                }
                attachment.active = false;
                shorten_xmark_hud_mark(attachment.target, now_ms);
            }
            publish_current_xmark_hud_state(now_ms);
        }
        return;
    }

    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active ||
            attachment.official_follow ||
            !attachment.visual_follow ||
            !attachment.visual_key ||
            now_ms >= attachment.expires_ms) {
            continue;
        }
        if (g_visual_enemy_state_explicit_zero &&
            !xmark_visual_host_currently_present(attachment.visual_key)) {
            attachment.active = false;
            shorten_xmark_hud_mark(attachment.target, now_ms);
            cleared_any = true;
            continue;
        }

        XMarkVisualEnemyHost host{};
        if (visual_enemy_host_by_key(attachment.visual_key, &host, now_ms)) {
            refresh_active_xmark_attachment_for_visual_host(host, now_ms);
        }
    }
    if (cleared_any) {
        publish_current_xmark_hud_state(now_ms);
    }
}

bool refresh_pending_xmark_visual_mark_host(XMarkPendingVisualMark &pending, unsigned long long now_ms) {
    if (!pending.active || !pending.host.active || !pending.host.key) {
        return false;
    }

    XMarkVisualEnemyHost refreshed{};
    if (visual_enemy_host_by_key(pending.host.key, &refreshed, now_ms)) {
        pending.host = refreshed;
        return true;
    }

    if (env_bool("MINA_XMARK_VISUAL_MARK_RETRY_REACQUIRE_NEAR_LAST", true)) {
        XMarkAttachment matcher{};
        matcher.visual_follow = true;
        matcher.visual_key = pending.host.key;
        copy_visual_token(matcher.visual_entry, sizeof(matcher.visual_entry), pending.host.entry);
        copy_visual_token(matcher.visual_stem, sizeof(matcher.visual_stem), pending.host.stem);
        copy_visual_token(matcher.visual_catalog, sizeof(matcher.visual_catalog), pending.host.catalog);
        const Vec3 render_position = visual_host_render_position(pending.host);
        const Vec3 visual_position = render_position_to_visual_host_space(render_position);
        if (find_nearest_visual_enemy_host_to_visual_position(
                visual_position,
                &refreshed,
                now_ms,
                &matcher)) {
            pending.host = refreshed;
            return true;
        }
    }

    ++g_pending_visual_mark_refresh_misses;
    return false;
}

void schedule_xmark_visual_mark_retry(
    const char *reason,
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms,
    unsigned int attempts_done) {
    if (!env_bool("MINA_XMARK_VISUAL_MARK_RETRY_ENABLED", true) ||
        !host.active ||
        !host.key) {
        return;
    }

    const unsigned int max_attempts = std::max(1u, env_uint("MINA_XMARK_VISUAL_MARK_RETRY_ATTEMPTS", 5));
    if (attempts_done >= max_attempts) {
        return;
    }

    XMarkPendingVisualMark *slot = nullptr;
    for (XMarkPendingVisualMark &pending : g_pending_visual_marks) {
        if (pending.active && pending.host.key == host.key) {
            slot = &pending;
            break;
        }
    }
    if (!slot) {
        for (XMarkPendingVisualMark &pending : g_pending_visual_marks) {
            if (!pending.active || now_ms >= pending.expires_ms) {
                slot = &pending;
                break;
            }
        }
    }
    if (!slot) {
        slot = &g_pending_visual_marks[0];
    }

    const unsigned int delay_ms = std::max(16u, env_uint("MINA_XMARK_VISUAL_MARK_RETRY_DELAY_MS", 80));
    const unsigned int window_ms = std::max(delay_ms, env_uint("MINA_XMARK_VISUAL_MARK_RETRY_WINDOW_MS", 700));
    *slot = XMarkPendingVisualMark{};
    slot->active = true;
    slot->host = host;
    slot->attempts = attempts_done;
    slot->next_ms = now_ms + delay_ms;
    slot->expires_ms = now_ms + window_ms;
    std::snprintf(slot->reason, sizeof(slot->reason), "%s", reason && reason[0] ? reason : "visual-mark-retry");
}

bool ensure_xmark_for_visual_host(const char *reason, const XMarkVisualEnemyHost &host) {
    const unsigned long long now_ms = GetTickCount64();
    const bool spawned = spawn_tracked_xmark_for_visual_host(reason, host);
    if (!xmark_attachment_active_for_visual_host(host, xmark_status_now_ms())) {
        schedule_xmark_visual_mark_retry(reason, host, now_ms, spawned ? 1u : 0u);
    }
    return spawned || xmark_attachment_active_for_visual_host(host, xmark_status_now_ms());
}

void maybe_process_pending_xmark_visual_mark_retries(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_VISUAL_MARK_RETRY_ENABLED", true)) {
        return;
    }

    const unsigned int max_attempts = std::max(1u, env_uint("MINA_XMARK_VISUAL_MARK_RETRY_ATTEMPTS", 5));
    const unsigned int delay_ms = std::max(16u, env_uint("MINA_XMARK_VISUAL_MARK_RETRY_DELAY_MS", 80));
    for (XMarkPendingVisualMark &pending : g_pending_visual_marks) {
        if (!pending.active) {
            continue;
        }
        if (now_ms >= pending.expires_ms ||
            pending.attempts >= max_attempts) {
            pending.active = false;
            ++g_pending_visual_mark_expired;
            continue;
        }
        if (now_ms < pending.next_ms) {
            continue;
        }

        const bool host_refreshed = refresh_pending_xmark_visual_mark_host(pending, now_ms);
        if (xmark_attachment_active_for_visual_host(pending.host, now_ms)) {
            if (host_refreshed) {
                refresh_active_xmark_attachment_for_visual_host(pending.host, now_ms);
            }
            pending.next_ms = now_ms + delay_ms;
            continue;
        }
        if (!host_refreshed) {
            pending.next_ms = now_ms + delay_ms;
            continue;
        }

        ++pending.attempts;
        ++g_pending_visual_mark_retries;
        ++g_pending_visual_mark_spawn_attempts;
        const bool spawned = spawn_tracked_xmark_for_visual_host(pending.reason, pending.host);
        if (spawned || xmark_attachment_active_for_visual_host(pending.host, xmark_status_now_ms())) {
            refresh_active_xmark_attachment_for_visual_host(pending.host, xmark_status_now_ms());
            pending.next_ms = now_ms + delay_ms;
            continue;
        }
        pending.next_ms = now_ms + delay_ms;
    }
}

bool spawn_direct_xmark_probe(const char *reason) {
    return spawn_direct_anim_effect(
        "XMark",
        reason,
        "effects/xmark.anb.yc",
        "2x",
        "palettes/xmark.pal.yc",
        &g_native_direct_count,
        nullptr);
}

bool spawn_render_f0029_at(
    const char *reason,
    const Vec3 &position,
    int direction,
    bool side_basic_bound,
    unsigned long long source_tick,
    unsigned long long source_draw) {
    if (!env_bool("MINA_XMARK_F0029_RENDER_BACKEND_ENABLED", true)) {
        return false;
    }
    if (!xmark_render_backend_ensure_initialized()) {
        if (g_mina) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn render f0029 skipped reason=%s: render backend unavailable status=%s\n",
                reason ? reason : "<none>",
                g_xmark_render_backend_status);
            g_mina->Log(message);
        }
        return false;
    }

    const unsigned long long now_ms = GetTickCount64();
    const unsigned int duration_ms = side_basic_bound
        ? std::max(60u, env_uint("MINA_XMARK_F0029_RENDER_SIDE_BASIC_MAX_MS", 260))
        : std::max(60u, env_uint("MINA_XMARK_F0029_RENDER_DURATION_MS", 150));
    const unsigned int side_basic_grace_ms =
        std::max(16u, env_uint("MINA_XMARK_F0029_RENDER_SIDE_BASIC_GRACE_MS", 54));
    const int normalized_direction = direction == FacingLeft ? FacingLeft : FacingRight;
    if (env_bool("MINA_XMARK_F0029_RENDER_DEDUPE_ENABLED", true)) {
        const bool lock_existing_direction =
            env_bool("MINA_XMARK_F0029_RENDER_DEDUPE_LOCK_DIRECTION", true);
        const bool dedupe_any_active =
            env_bool("MINA_XMARK_F0029_RENDER_DEDUPE_ANY_ACTIVE", true);
        const unsigned int dedupe_ms = std::max(
            duration_ms,
            env_uint("MINA_XMARK_F0029_RENDER_DEDUPE_MS", 320));
        const float dedupe_radius = std::max(0.0f, env_float("MINA_XMARK_F0029_RENDER_DEDUPE_RADIUS", 1.75f));
        const float dedupe_radius_sq = dedupe_radius * dedupe_radius;
        for (F0029RenderEffect &effect : g_f0029_render_effects) {
            if (!effect.active || !effect.started_ms || now_ms >= effect.started_ms + effect.duration_ms) {
                continue;
            }
            const bool same_side_basic_source =
                side_basic_bound &&
                effect.side_basic_bound &&
                ((source_tick && effect.source_tick == source_tick) ||
                 (source_draw && effect.source_draw == source_draw));
            if (side_basic_bound && !same_side_basic_source) {
                continue;
            }
            if (now_ms > effect.started_ms + dedupe_ms ||
                (!dedupe_any_active && !lock_existing_direction && effect.direction != normalized_direction)) {
                continue;
            }
            if (!dedupe_any_active) {
                const float dx = effect.position.x - position.x;
                const float dy = effect.position.y - position.y;
                if ((dx * dx) + (dy * dy) > dedupe_radius_sq) {
                    continue;
                }
            }
            if (env_bool("MINA_XMARK_F0029_RENDER_DEDUPE_REFRESH_POSITION", false)) {
                effect.position = position;
            }
            if (side_basic_bound && effect.direction == normalized_direction) {
                effect.side_basic_bound = true;
                effect.soft_expires_ms = now_ms + side_basic_grace_ms;
                effect.source_tick = source_tick ? source_tick : effect.source_tick;
                effect.source_draw = source_draw ? source_draw : effect.source_draw;
            }
            if (g_mina && env_bool("MINA_XMARK_F0029_RENDER_DEDUPE_LOG", false)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn render f0029 deduped reason=%s dir=%s pos=(%.3f, %.3f, %.3f) existingAgeMs=%llu\n",
                    reason ? reason : "<none>",
                    direction_name(normalized_direction),
                    static_cast<double>(position.x),
                    static_cast<double>(position.y),
                    static_cast<double>(position.z),
                    now_ms - effect.started_ms);
                g_mina->Log(message);
            }
            return true;
        }
    }

    F0029RenderEffect *slot = nullptr;
    for (F0029RenderEffect &effect : g_f0029_render_effects) {
        if (!effect.active || (effect.started_ms && now_ms >= effect.started_ms + effect.duration_ms)) {
            slot = &effect;
            break;
        }
    }
    if (!slot) {
        slot = &g_f0029_render_effects[0];
    }

    *slot = F0029RenderEffect{};
    slot->active = true;
    slot->side_basic_bound = side_basic_bound;
    slot->position = position;
    slot->direction = normalized_direction;
    slot->started_ms = now_ms;
    slot->soft_expires_ms = side_basic_bound ? now_ms + side_basic_grace_ms : 0;
    slot->source_tick = source_tick;
    slot->source_draw = source_draw;
    slot->duration_ms = duration_ms;
    ++g_native_f0029_count;

    if (g_mina && env_bool("MINA_XMARK_F0029_RENDER_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn render f0029 request #%u reason=%s dir=%s bound=%u sourceTick=%llu sourceDraw=%llu pos=(%.3f, %.3f, %.3f) durationMs=%u softMs=%llu\n",
            g_native_f0029_count,
            reason ? reason : "<none>",
            direction_name(slot->direction),
            slot->side_basic_bound ? 1u : 0u,
            slot->source_tick,
            slot->source_draw,
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            duration_ms,
            slot->soft_expires_ms);
        g_mina->Log(message);
    }
    return true;
}

void refresh_f0029_side_basic_lifetime(
    unsigned long long now_ms,
    int direction,
    unsigned long long source_tick,
    unsigned long long source_draw) {
    if (!env_bool("MINA_XMARK_F0029_RENDER_BOUND_TO_SIDE_BASIC_FRAMES", true)) {
        return;
    }
    const int normalized_direction = direction == FacingLeft ? FacingLeft : FacingRight;
    const unsigned int side_basic_grace_ms =
        std::max(16u, env_uint("MINA_XMARK_F0029_RENDER_SIDE_BASIC_GRACE_MS", 54));
    for (F0029RenderEffect &effect : g_f0029_render_effects) {
        if (!effect.active || !effect.side_basic_bound || effect.direction != normalized_direction) {
            continue;
        }
        if (effect.started_ms && now_ms >= effect.started_ms + effect.duration_ms) {
            effect.active = false;
            continue;
        }
        effect.soft_expires_ms = now_ms + side_basic_grace_ms;
        effect.source_tick = source_tick ? source_tick : effect.source_tick;
        effect.source_draw = source_draw ? source_draw : effect.source_draw;
    }
}

bool spawn_render_vertical_smear_at(
    const char *reason,
    const Vec3 &position,
    int direction,
    unsigned long long source_tick,
    unsigned long long source_draw) {
    if (!env_bool("MINA_XMARK_VERTICAL_SMEAR_ENABLED", false) ||
        (direction != FacingUp && direction != FacingDown)) {
        return false;
    }

    const unsigned long long now_ms = GetTickCount64();
    const unsigned int frame_ms =
        std::max(16u, env_uint("MINA_XMARK_VERTICAL_SMEAR_RENDER_FRAME_MS", 34));
    const unsigned int duration_ms = std::max(
        frame_ms * kXMarkRenderVerticalSmearFrameCount,
        env_uint("MINA_XMARK_VERTICAL_SMEAR_RENDER_DURATION_MS", 180));
    const unsigned int grace_ms =
        std::max(32u, env_uint("MINA_XMARK_VERTICAL_SMEAR_RENDER_FRAME_GRACE_MS", 90));

    for (VerticalSmearRenderEffect &effect : g_vertical_smear_render_effects) {
        if (!effect.active || !effect.started_ms || now_ms >= effect.started_ms + effect.duration_ms) {
            continue;
        }
        const bool same_source =
            (source_tick && effect.source_tick == source_tick) ||
            (source_draw && effect.source_draw == source_draw);
        if (same_source || effect.direction == direction) {
            effect.soft_expires_ms = now_ms + grace_ms;
            return true;
        }
    }

    VerticalSmearRenderEffect *slot = nullptr;
    for (VerticalSmearRenderEffect &effect : g_vertical_smear_render_effects) {
        if (!effect.active || !effect.started_ms || now_ms >= effect.started_ms + effect.duration_ms) {
            slot = &effect;
            break;
        }
    }
    if (!slot) {
        slot = &g_vertical_smear_render_effects[0];
    }

    *slot = VerticalSmearRenderEffect{};
    slot->active = true;
    slot->position = position;
    slot->direction = direction;
    slot->started_ms = now_ms;
    slot->soft_expires_ms = now_ms + grace_ms;
    slot->source_tick = source_tick;
    slot->source_draw = source_draw;
    slot->duration_ms = duration_ms;

    if (g_mina && env_bool("MINA_XMARK_VERTICAL_SMEAR_LOG", false)) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn vertical basic smear spawned reason=%s dir=%s sourceTick=%llu sourceDraw=%llu pos=(%.3f, %.3f, %.3f) durationMs=%u\n",
            reason ? reason : "<none>",
            direction_name(direction),
            source_tick,
            source_draw,
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z),
            duration_ms);
        g_mina->Log(message);
    }
    return true;
}

void refresh_vertical_basic_smear_lifetime(
    unsigned long long now_ms,
    int direction,
    unsigned long long source_tick,
    unsigned long long source_draw) {
    const unsigned int grace_ms =
        std::max(32u, env_uint("MINA_XMARK_VERTICAL_SMEAR_RENDER_FRAME_GRACE_MS", 90));
    for (VerticalSmearRenderEffect &effect : g_vertical_smear_render_effects) {
        if (!effect.active || effect.direction != direction) {
            continue;
        }
        if (effect.started_ms && now_ms >= effect.started_ms + effect.duration_ms) {
            effect.active = false;
            continue;
        }
        effect.soft_expires_ms = now_ms + grace_ms;
        effect.source_tick = source_tick ? source_tick : effect.source_tick;
        effect.source_draw = source_draw ? source_draw : effect.source_draw;
    }
}

bool spawn_direct_f0029_at(const char *reason, const Vec3 &position) {
    if (env_bool("MINA_XMARK_F0029_USE_RENDER_BACKEND", true)) {
        return spawn_render_f0029_at(reason, position, FacingRight);
    }
    if (!env_bool("MINA_XMARK_NATIVE_F0029_DIRECT_ENABLED", false)) {
        if (g_mina && !g_native_f0029_direct_skip_logged) {
            g_mina->Log("XMarkBurn direct f0029 native anim-effect spawn is disabled; basic-frame detection remains active.\n");
            g_native_f0029_direct_skip_logged = true;
        }
        return false;
    }
    return spawn_direct_anim_effect(
        "f0029",
        reason,
        "effects/f0029.anb.yc",
        "2x",
        "palettes/f0029.pal.yc",
        &g_native_f0029_count,
        &position,
        nullptr,
        nullptr,
        static_cast<int>(env_uint("MINA_XMARK_F0029_DRAW_LAYER", 1)));
}

bool spawn_direct_f0029_attack_at(
    const char *reason,
    const Vec3 &position,
    int direction,
    bool side_basic_bound = false,
    unsigned long long source_tick = 0,
    unsigned long long source_draw = 0) {
    if (env_bool("MINA_XMARK_F0029_USE_RENDER_BACKEND", true)) {
        return spawn_render_f0029_at(reason, position, direction, side_basic_bound, source_tick, source_draw);
    }
    if (!env_bool("MINA_XMARK_NATIVE_F0029_DIRECT_ENABLED", false)) {
        if (g_mina && !g_native_f0029_direct_skip_logged) {
            g_mina->Log("XMarkBurn direct f0029 native anim-effect spawn is disabled; basic-frame detection remains active.\n");
            g_native_f0029_direct_skip_logged = true;
        }
        return false;
    }
    return spawn_direct_anim_effect(
        direction == FacingLeft ? "f0029-left" : "f0029",
        reason,
        direction == FacingLeft ? "effects/f0029_left.anb.yc" : "effects/f0029.anb.yc",
        "2x",
        "palettes/f0029.pal.yc",
        &g_native_f0029_count,
        &position,
        nullptr,
        nullptr,
        static_cast<int>(env_uint("MINA_XMARK_F0029_DRAW_LAYER", 1)));
}

bool spawn_direct_f0029_probe(const char *reason) {
    if (env_bool("MINA_XMARK_F0029_USE_RENDER_BACKEND", true)) {
        return spawn_render_f0029_at(reason, official_spawn_position(), FacingRight);
    }
    if (!env_bool("MINA_XMARK_NATIVE_F0029_DIRECT_ENABLED", false)) {
        if (g_mina && !g_native_f0029_direct_skip_logged) {
            g_mina->Log("XMarkBurn direct f0029 native anim-effect spawn is disabled; basic-frame detection remains active.\n");
            g_native_f0029_direct_skip_logged = true;
        }
        return false;
    }
    return spawn_direct_anim_effect(
        "f0029",
        reason,
        "effects/f0029.anb.yc",
        "2x",
        "palettes/f0029.pal.yc",
        &g_native_f0029_count,
        nullptr,
        nullptr,
        nullptr,
        static_cast<int>(env_uint("MINA_XMARK_F0029_DRAW_LAYER", 1)));
}

Vec3 offset_from_direction(
    const Vec3 &base,
    int direction,
    float side_x,
    float side_y,
    float vertical_x,
    float vertical_y) {
    Vec3 out = base;
    const float side_distance = std::fabs(side_x);
    switch (direction) {
    case FacingLeft:
        out.x -= side_distance;
        out.y += side_y;
        break;
    case FacingUp:
        out.x += vertical_x;
        out.y -= vertical_y;
        break;
    case FacingDown:
        out.x += vertical_x;
        out.y += vertical_y;
        break;
    case FacingRight:
    default:
        out.x += side_distance;
        out.y += side_y;
        break;
    }
    return out;
}

Vec3 f0029_attack_position_for_direction(int direction) {
    const Vec3 base = official_spawn_position();
    // Calibrated f0029 side-swing defaults.
    const float side_x = env_float("MINA_XMARK_F0029_ATTACK_SIDE_OFFSET_X", 1.0f);
    const float side_y = env_float("MINA_XMARK_F0029_ATTACK_SIDE_OFFSET_Y", 0.5f);
    const float vertical_x = env_float("MINA_XMARK_F0029_ATTACK_VERTICAL_OFFSET_X", 0.0f);
    const float vertical_y = env_float("MINA_XMARK_F0029_ATTACK_VERTICAL_OFFSET_Y", 1.65f);
    Vec3 out = offset_from_direction(base, direction, side_x, side_y, vertical_x, vertical_y);
    const float shared_screen_x = env_float("MINA_XMARK_F0029_ATTACK_SCREEN_OFFSET_X", 0.0f);
    if (direction == FacingLeft) {
        out.x += env_float("MINA_XMARK_F0029_ATTACK_LEFT_SCREEN_OFFSET_X", 2.0f);
    } else if (direction == FacingRight) {
        out.x += env_float("MINA_XMARK_F0029_ATTACK_RIGHT_SCREEN_OFFSET_X", -2.0f);
    } else {
        out.x += shared_screen_x;
    }
    out.y += env_float("MINA_XMARK_F0029_ATTACK_SCREEN_OFFSET_Y", 0.0f);
    if (env_bool("MINA_XMARK_F0029_ATTACK_PROJECT_Z_TO_Y", true)) {
        out.y += out.z * env_float("MINA_XMARK_F0029_ATTACK_Z_TO_Y_SCALE", 1.0f);
    }
    return out;
}

Vec3 f0029_attack_position() {
    return f0029_attack_position_for_direction(g_last_direction);
}

Vec3 crater_position_for_direction(int direction) {
    Vec3 out = official_spawn_position();
    const float inward = env_float("MINA_XMARK_NATIVE_CRATER_INWARD_OFFSET", 0.35f);
    switch (direction) {
    case FacingLeft:
        out.x += env_float("MINA_XMARK_NATIVE_CRATER_LEFT_X", -0.1f);
        out.y += env_float("MINA_XMARK_NATIVE_CRATER_LEFT_Y", 0.2f);
        out.x += inward;
        break;
    case FacingUp:
        out.x += env_float("MINA_XMARK_NATIVE_CRATER_UP_X", -0.1f);
        out.y += env_float("MINA_XMARK_NATIVE_CRATER_UP_Y", -0.9f);
        out.y += inward;
        break;
    case FacingDown:
        out.x += env_float("MINA_XMARK_NATIVE_CRATER_DOWN_X", 0.2f);
        out.y += env_float("MINA_XMARK_NATIVE_CRATER_DOWN_Y", 1.1f);
        out.y -= inward;
        break;
    case FacingRight:
    default:
        out.x += env_float("MINA_XMARK_NATIVE_CRATER_RIGHT_X", 0.0f);
        out.y += env_float("MINA_XMARK_NATIVE_CRATER_RIGHT_Y", 0.5f);
        out.x -= inward;
        break;
    }
    out.z += env_float("MINA_XMARK_NATIVE_CRATER_Z_OFFSET", -0.05f);
    return out;
}

bool spawn_direct_crater_at(const char *reason, const Vec3 &position, int direction) {
    char label[64]{};
    std::snprintf(label, sizeof(label), "HammerCrater-%s", direction_name(direction));
    const char *path = "effects/hammerCrater_right.anb.yc";
    switch (direction) {
    case FacingLeft:
        path = "effects/hammerCrater_left.anb.yc";
        break;
    case FacingUp:
        path = "effects/hammerCrater_up.anb.yc";
        break;
    case FacingDown:
        path = "effects/hammerCrater_down.anb.yc";
        break;
    case FacingRight:
    default:
        path = "effects/hammerCrater_right.anb.yc";
        break;
    }
    return spawn_direct_anim_effect(
        label,
        reason,
        env_bool("MINA_XMARK_NATIVE_CRATER_SMALL", false) ? "effects/hammerCraterSmall.anb.yc" : path,
        "2x",
        "palettes/global.pal.yc",
        &g_native_crater_count,
        &position,
        nullptr,
        nullptr,
        static_cast<int>(env_uint("MINA_XMARK_NATIVE_CRATER_DRAW_LAYER", 1)));
}

Vec3 clashrend_boom_direction_step(int direction, float distance) {
    switch (direction) {
    case FacingLeft:
        return Vec3{-distance, 0.0f, 0.0f};
    case FacingUp:
        return Vec3{0.0f, distance, 0.0f};
    case FacingDown:
        return Vec3{0.0f, -distance, 0.0f};
    case FacingRight:
    default:
        return Vec3{distance, 0.0f, 0.0f};
    }
}

void play_clashrend_boom_wave_sfx(const char *reason) {
    char sound_name[96]{};
    if (!xmark_read_environment_value(
            "MINA_XMARK_BOOM_REPEAT_SFX_NAME",
            sound_name,
            sizeof(sound_name))) {
        std::snprintf(sound_name, sizeof(sound_name), "%s", "blast");
    }
    xmark_play_sound_name(sound_name, reason);
}

void arm_clashrend_boom_pattern(
    unsigned long long released_ms,
    unsigned long long held_ms,
    int direction) {
    const unsigned int full_hold_ms = env_uint(
        "MINA_XMARK_BOOM_FULL_HOLD_MS",
        env_uint("MINA_XMARK_NATIVE_CRATER_CHARGE_MIN_MS", 650));
    if (!env_bool("MINA_XMARK_BOOM_PATTERN_ENABLED", true) || held_ms < full_hold_ms) {
        return;
    }
    g_clashrend_boom_pattern = ClashrendBoomPatternState{};
    g_clashrend_boom_geyser = ClashrendBoomGeyserState{};
    g_clashrend_boom_pattern.pending = true;
    g_clashrend_boom_pattern.direction = direction;
    g_clashrend_boom_pattern.released_ms = released_ms;
    g_clashrend_boom_pattern.impact = crater_position_for_direction(direction);
    if (g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast armed dir=%s heldMs=%llu thresholdMs=%u.\n",
            direction_name(direction),
            held_ms,
            full_hold_ms);
        g_mina->Log(message);
    }
}

void start_clashrend_boom_pattern(
    unsigned long long now_ms,
    int direction,
    const Vec3 &impact) {
    if (!g_clashrend_boom_pattern.pending || g_clashrend_boom_pattern.active) {
        return;
    }
    const unsigned int pending_window_ms =
        std::max(100u, env_uint("MINA_XMARK_BOOM_PENDING_WINDOW_MS", 900));
    if (g_clashrend_boom_pattern.released_ms &&
        GetTickCount64() > g_clashrend_boom_pattern.released_ms + pending_window_ms) {
        g_clashrend_boom_pattern = ClashrendBoomPatternState{};
        return;
    }
    const int release_direction = g_clashrend_boom_pattern.direction;
    g_clashrend_boom_pattern.pending = false;
    g_clashrend_boom_pattern.active = true;
    g_clashrend_boom_pattern.direction = release_direction;
    g_clashrend_boom_pattern.started_ms = now_ms;
    g_clashrend_boom_pattern.impact = impact;
    if (g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast started dir=%s impactDir=%s impact=(%.3f, %.3f, %.3f).\n",
            direction_name(release_direction),
            direction_name(direction),
            static_cast<double>(impact.x),
            static_cast<double>(impact.y),
            static_cast<double>(impact.z));
        g_mina->Log(message);
    }
}

bool clashrend_boom_native_reshape_api_available() {
    return g_mina &&
        api_function_field_ready(offsetof(MinaModAPI, PlayerGetWorld)) &&
        api_function_field_ready(offsetof(MinaModAPI, WorldGetGameRootEntity)) &&
        api_function_field_ready(offsetof(MinaModAPI, EntityGetChildren)) &&
        api_function_field_ready(offsetof(MinaModAPI, ComponentGetType)) &&
        api_function_field_ready(offsetof(MinaModAPI, ComponentIsa)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqNameNoDir)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumSeqFrames)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetWorldTransform)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimGetLocalPosition)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetLocalPosition)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimPlay)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetVisible)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetPaused)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetSeqFrameIdx)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetFrameTime)) &&
        api_function_field_ready(offsetof(MinaModAPI, GameAnimSetPlayRate));
}

bool clashrend_boom_has_native_outer(
    const ClashrendBoomPatternState &boom,
    ycComponent *component) {
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        if (boom.native_outer[index].component == component) {
            return true;
        }
    }
    return false;
}

bool clashrend_boom_read_native_anim(
    ycComponent *component,
    char *sequence,
    size_t sequence_size,
    uint32_t *frame_count,
    MM_Transform *world,
    MM_Vec3 *local_position) {
    if (!component || !sequence || !sequence_size || !frame_count || !world || !local_position) {
        return false;
    }
    bool read = false;
    __try {
        copy_mm_string_to_cstr(
            sequence,
            sequence_size,
            g_mina->GameAnimGetSeqNameNoDir(component));
        *frame_count = g_mina->GameAnimGetNumSeqFrames(component);
        g_mina->GameAnimGetWorldTransform(component, world);
        g_mina->GameAnimGetLocalPosition(component, local_position);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    return read;
}

void clashrend_boom_capture_native_anims(
    ycEntity *entity,
    ClashrendBoomPatternState &boom,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes,
    size_t max_children_per_entity) {
    if (!entity || !nodes || depth > max_depth || *nodes >= max_nodes) {
        return;
    }
    ++(*nodes);
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    boom.native_largest_child_count = std::max<unsigned int>(
        boom.native_largest_child_count,
        static_cast<unsigned int>(std::min<size_t>(child_count, UINT_MAX)));
    const size_t child_cap = std::min(child_count, max_children_per_entity);
    if (child_count > child_cap) {
        ++boom.native_truncated_entities;
    }
    if (!child_cap) {
        return;
    }
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

    const float capture_radius =
        std::max(3.0f, env_float("MINA_XMARK_BOOM_NATIVE_RESHAPE_RADIUS", 5.0f));
    const float capture_radius_sq = capture_radius * capture_radius;
    const size_t limit = std::min(read_count, child_cap);
    for (size_t cursor = limit; cursor > 0 && *nodes < max_nodes; --cursor) {
        const size_t index = cursor - 1u;
        ycComponent *component = children[index];
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
            clashrend_boom_capture_native_anims(
                reinterpret_cast<ycEntity *>(component),
                boom,
                depth + 1u,
                max_depth,
                nodes,
                max_nodes,
                max_children_per_entity);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }

        char sequence[48]{};
        uint32_t frame_count = 0;
        MM_Transform world{};
        MM_Vec3 local_position{};
        if (!clashrend_boom_read_native_anim(
                component,
                sequence,
                sizeof(sequence),
                &frame_count,
                &world,
                &local_position)) {
            continue;
        }
        const Vec3 world_position{world.t.x, world.t.y, world.t.z};
        if (xy_distance_sq(world_position, boom.impact) > capture_radius_sq) {
            continue;
        }
        if (std::strcmp(sequence, "burrowBonkFast") == 0 && frame_count == 5u) {
            if (boom.native_outer_count < 8u &&
                !clashrend_boom_has_native_outer(boom, component)) {
                ClashrendBoomPatternState::NativeAnim &slot =
                    boom.native_outer[boom.native_outer_count++];
                slot.component = component;
                slot.local_position = local_position;
                slot.world_position = world_position;
            }
        } else if (!boom.native_center &&
            std::strcmp(sequence, "Default") == 0 && frame_count == 5u &&
            clashrend_boom_matches_native_explosion_anim(component, boom.impact)) {
            boom.native_center = component;
            boom.native_center_local = local_position;
        }
    }
    g_mina->Free(children);
}

Vec3 clashrend_boom_native_forward_step(int direction) {
    const float spacing_x =
        std::max(0.25f, env_float("MINA_XMARK_BOOM_NATIVE_CELL_SPACING_X", 1.9f));
    const float spacing_y =
        std::max(0.25f, env_float("MINA_XMARK_BOOM_NATIVE_CELL_SPACING_Y", 1.3f));
    switch (direction) {
    case FacingLeft:
        return Vec3{-spacing_x, 0.0f, 0.0f};
    case FacingUp:
        return Vec3{0.0f, spacing_y, 0.0f};
    case FacingDown:
        return Vec3{0.0f, -spacing_y, 0.0f};
    case FacingRight:
    default:
        return Vec3{spacing_x, 0.0f, 0.0f};
    }
}

void clashrend_boom_set_native_anim(
    ycComponent *component,
    const char *sequence,
    const MM_Vec3 &position,
    bool visible,
    bool paused,
    bool restart,
    float play_rate) {
    if (!component || !sequence || !sequence[0]) {
        return;
    }
    __try {
        g_mina->GameAnimSetLocalPosition(component, position);
        if (restart && visible) {
            g_mina->GameAnimSetPaused(component, false);
            g_mina->GameAnimPlay(component, sequence, -1, play_rate, true);
            g_mina->GameAnimSetSeqFrameIdx(component, 0u);
            g_mina->GameAnimSetFrameTime(component, 0.0f);
        }
        g_mina->GameAnimSetPlayRate(component, play_rate);
        g_mina->GameAnimSetPaused(component, paused);
        g_mina->GameAnimSetVisible(component, visible);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool clashrend_boom_capture_native_pattern(
    ClashrendBoomPatternState &boom,
    unsigned long long status_now_ms) {
    if (boom.native_pattern_ready ||
        !env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE", false) ||
        !clashrend_boom_native_reshape_api_available() ||
        !official_enemy_init_rtti() ||
        !xmark_game_anim_init_rtti()) {
        return boom.native_pattern_ready;
    }
    const unsigned long long wall_now_ms = GetTickCount64();
    if (boom.native_next_scan_ms && wall_now_ms < boom.native_next_scan_ms) {
        return false;
    }
    boom.native_next_scan_ms = wall_now_ms +
        std::max(8u, env_uint("MINA_XMARK_BOOM_NATIVE_RESHAPE_SCAN_MS", 16u));
    ++boom.native_capture_scans;

    World *world = nullptr;
    ycEntity *root = nullptr;
    __try {
        world = g_mina->PlayerGetWorld();
        root = world ? g_mina->WorldGetGameRootEntity(world) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        root = nullptr;
    }
    if (!root) {
        return false;
    }
    unsigned int nodes = 0;
    const size_t max_children_per_entity = std::max<size_t>(
        128u,
        env_uint("MINA_XMARK_BOOM_NATIVE_RESHAPE_MAX_CHILDREN", 4096u));
    clashrend_boom_capture_native_anims(
        root,
        boom,
        0u,
        env_uint("MINA_XMARK_BOOM_NATIVE_RESHAPE_MAX_DEPTH", 10u),
        &nodes,
        env_uint("MINA_XMARK_BOOM_NATIVE_RESHAPE_MAX_NODES", 1024u),
        max_children_per_entity);
    boom.native_last_scan_nodes = nodes;
    if (boom.native_outer_count < 8u || !boom.native_center) {
        if (g_mina && env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE_LOG", false) &&
            (boom.native_capture_scans == 1u ||
             boom.native_capture_scans == 8u ||
             boom.native_capture_scans == 32u ||
             boom.native_capture_scans == 64u)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn Cross Blast native capture pending scan=%u nodes=%u outer=%u center=%u largestChildren=%u truncated=%u impact=(%.3f,%.3f).\n",
                boom.native_capture_scans,
                boom.native_last_scan_nodes,
                boom.native_outer_count,
                boom.native_center ? 1u : 0u,
                boom.native_largest_child_count,
                boom.native_truncated_entities,
                static_cast<double>(boom.impact.x),
                static_cast<double>(boom.impact.y));
            g_mina->Log(message);
        }
        return false;
    }

    Vec3 center{};
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        center.x += boom.native_outer[index].world_position.x;
        center.y += boom.native_outer[index].world_position.y;
        center.z += boom.native_outer[index].world_position.z;
    }
    const float reciprocal = 1.0f / static_cast<float>(boom.native_outer_count);
    center.x *= reciprocal;
    center.y *= reciprocal;
    center.z *= reciprocal;
    boom.impact = center;

    const float corner_x_min =
        std::max(0.2f, env_float("MINA_XMARK_BOOM_NATIVE_CORNER_MIN_X", 0.75f));
    const float corner_y_min =
        std::max(0.2f, env_float("MINA_XMARK_BOOM_NATIVE_CORNER_MIN_Y", 0.5f));
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        slot.corner =
            std::fabs(slot.world_position.x - center.x) >= corner_x_min &&
            std::fabs(slot.world_position.y - center.y) >= corner_y_min;
    }
    boom.native_pattern_ready = true;
    if (g_mina && env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE_LOG", false)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast native pattern captured outer=%u center=0x%p scans=%u impact=(%.3f,%.3f).\n",
            boom.native_outer_count,
            reinterpret_cast<void *>(boom.native_center),
            boom.native_capture_scans,
            static_cast<double>(center.x),
            static_cast<double>(center.y));
        g_mina->Log(message);
    }
    start_clashrend_boom_pattern(status_now_ms, boom.direction, center);
    return true;
}

void clashrend_boom_play_native_x_wave(
    ClashrendBoomPatternState &boom,
    float forward_steps) {
    const Vec3 forward = clashrend_boom_native_forward_step(boom.direction);
    const float play_rate =
        std::max(0.25f, env_float("MINA_XMARK_BOOM_NATIVE_PLAY_RATE", 1.35f));
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        MM_Vec3 position = slot.local_position;
        position.x += forward.x * forward_steps;
        position.y += forward.y * forward_steps;
        position.z += forward.z * forward_steps;
        clashrend_boom_set_native_anim(
            slot.component,
            "burrowBonkFast",
            position,
            slot.corner,
            !slot.corner,
            slot.corner,
            play_rate);
    }
    MM_Vec3 center_position = boom.native_center_local;
    center_position.x += forward.x * forward_steps;
    center_position.y += forward.y * forward_steps;
    center_position.z += forward.z * forward_steps;
    clashrend_boom_set_native_anim(
        boom.native_center,
        "Default",
        center_position,
        false,
        true,
        false,
        play_rate);
}

void clashrend_boom_begin_native_x_wave(ClashrendBoomPatternState &boom) {
    clashrend_boom_play_native_x_wave(boom, 0.0f);
    boom.first_wave_spawned = true;
}

void clashrend_boom_begin_native_plus_wave(ClashrendBoomPatternState &boom) {
    const Vec3 forward = clashrend_boom_native_forward_step(boom.direction);
    const float play_rate =
        std::max(0.25f, env_float("MINA_XMARK_BOOM_NATIVE_PLAY_RATE", 1.35f));
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        MM_Vec3 position = slot.local_position;
        if (!slot.corner) {
            position.x += forward.x;
            position.y += forward.y;
            position.z += forward.z;
        }
        clashrend_boom_set_native_anim(
            slot.component,
            "burrowBonkFast",
            position,
            !slot.corner,
            slot.corner,
            !slot.corner,
            play_rate);
    }
    MM_Vec3 center_position = boom.native_center_local;
    center_position.x += forward.x;
    center_position.y += forward.y;
    center_position.z += forward.z;
    clashrend_boom_set_native_anim(
        boom.native_center,
        "Default",
        center_position,
        true,
        false,
        true,
        play_rate);
    boom.second_wave_spawned = true;
    boom.second_wave_started_ms = xmark_status_now_ms();
}

void clashrend_boom_log_native_x_state(
    const ClashrendBoomPatternState &boom,
    const char *phase) {
    if (!g_mina || !phase ||
        !env_bool("MINA_XMARK_BOOM_NATIVE_FINAL_X_STATE_LOG", true) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimIsVisible)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimIsPaused)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqFrameIdx)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumLoopsPlayed))) {
        return;
    }
    unsigned int corners = 0;
    unsigned int visible = 0;
    unsigned int paused = 0;
    unsigned int min_frame = UINT_MAX;
    unsigned int max_frame = 0;
    unsigned int max_loops = 0;
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        const ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        if (!slot.corner || !slot.component) {
            continue;
        }
        ++corners;
        __try {
            visible += g_mina->GameAnimIsVisible(slot.component) ? 1u : 0u;
            paused += g_mina->GameAnimIsPaused(slot.component) ? 1u : 0u;
            const unsigned int frame = g_mina->GameAnimGetSeqFrameIdx(slot.component);
            min_frame = std::min(min_frame, frame);
            max_frame = std::max(max_frame, frame);
            max_loops = std::max(
                max_loops,
                g_mina->GameAnimGetNumLoopsPlayed(slot.component));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "XMarkBurn Cross Blast native final X %s corners=%u visible=%u paused=%u frames=%u-%u loops=%u.\n",
        phase,
        corners,
        visible,
        paused,
        min_frame == UINT_MAX ? 0u : min_frame,
        max_frame,
        max_loops);
    g_mina->Log(message);
}

void clashrend_boom_drive_native_final_x_wave(
    ClashrendBoomPatternState &boom,
    unsigned long long elapsed_ms,
    unsigned int duration_ms) {
    const Vec3 forward = clashrend_boom_native_forward_step(boom.direction);
    const unsigned int frame_count = 5u;
    const unsigned int frame = std::min<unsigned int>(
        frame_count - 1u,
        static_cast<unsigned int>(
            elapsed_ms * frame_count / std::max(1u, duration_ms)));
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        MM_Vec3 position = slot.local_position;
        position.x += forward.x * 2.0f;
        position.y += forward.y * 2.0f;
        position.z += forward.z * 2.0f;
        __try {
            g_mina->GameAnimSetLocalPosition(slot.component, position);
            if (slot.corner) {
                if (elapsed_ms == 0u) {
                    g_mina->GameAnimPlay(
                        slot.component,
                        "burrowBonkFast",
                        -1,
                        1.0f,
                        true);
                }
                g_mina->GameAnimSetPaused(slot.component, true);
                g_mina->GameAnimSetSeqFrameIdx(slot.component, frame);
                g_mina->GameAnimSetFrameTime(slot.component, 0.0f);
                g_mina->GameAnimSetVisible(slot.component, true);
            } else {
                g_mina->GameAnimSetPaused(slot.component, true);
                g_mina->GameAnimSetVisible(slot.component, false);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (boom.native_center) {
        MM_Vec3 center_position = boom.native_center_local;
        center_position.x += forward.x * 2.0f;
        center_position.y += forward.y * 2.0f;
        center_position.z += forward.z * 2.0f;
        __try {
            g_mina->GameAnimSetLocalPosition(boom.native_center, center_position);
            if (elapsed_ms == 0u) {
                g_mina->GameAnimPlay(
                    boom.native_center,
                    "Default",
                    -1,
                    1.0f,
                    true);
            }
            g_mina->GameAnimSetPaused(boom.native_center, true);
            g_mina->GameAnimSetSeqFrameIdx(boom.native_center, frame);
            g_mina->GameAnimSetFrameTime(boom.native_center, 0.0f);
            g_mina->GameAnimSetVisible(boom.native_center, true);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

void clashrend_boom_apply_native_final_x_wave(ClashrendBoomPatternState &boom) {
    clashrend_boom_drive_native_final_x_wave(
        boom,
        0u,
        std::max(80u, env_uint("MINA_XMARK_BOOM_NATIVE_FINAL_X_DURATION_MS", 360u)));
    clashrend_boom_log_native_x_state(boom, "started");
}

void clashrend_boom_hide_native_pattern_components(ClashrendBoomPatternState &boom) {
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        clashrend_boom_set_native_anim(
            slot.component,
            "burrowBonkFast",
            slot.local_position,
            false,
            true,
            false,
            1.0f);
    }
    clashrend_boom_set_native_anim(
        boom.native_center,
        "Default",
        boom.native_center_local,
        false,
        true,
        false,
        1.0f);
}

bool clashrend_boom_spawn_native_final_x(const Vec3 &center) {
    const float spacing_x = std::max(
        0.25f,
        env_float("MINA_XMARK_BOOM_NATIVE_CELL_SPACING_X", 1.9f));
    const float spacing_y = std::max(
        0.25f,
        env_float("MINA_XMARK_BOOM_NATIVE_CELL_SPACING_Y", 1.3f));
    const int draw_layer = static_cast<int>(
        env_uint("MINA_XMARK_BOOM_NATIVE_FINAL_X_DRAW_LAYER", 1u));
    static constexpr float kCorners[4][2] = {
        {-1.0f, -1.0f},
        {1.0f, -1.0f},
        {-1.0f, 1.0f},
        {1.0f, 1.0f},
    };

    unsigned int spawned = 0;
    for (const auto &corner : kCorners) {
        Vec3 position = center;
        position.x += corner[0] * spacing_x;
        position.y += corner[1] * spacing_y;
        if (spawn_direct_anim_effect(
                "CrossBlastFinalOuter",
                "cross-blast-final-x",
                "effects/explosion.anb.yc",
                "burrowBonkFast",
                nullptr,
                nullptr,
                &position,
                nullptr,
                nullptr,
                draw_layer,
                1.0f)) {
            ++spawned;
        }
    }
    if (g_mina && env_bool("MINA_XMARK_BOOM_GEYSER_LOG", false)) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast repeated first X effects=%u/4 center=(%.3f,%.3f).\n",
            spawned,
            static_cast<double>(center.x),
            static_cast<double>(center.y));
        g_mina->Log(message);
    }
    return spawned == 4u;
}

void start_clashrend_boom_geyser(const Vec3 &center, unsigned long long now_ms) {
    ClashrendBoomGeyserState &geyser = g_clashrend_boom_geyser;
    geyser = ClashrendBoomGeyserState{};
    geyser.active = env_bool("MINA_XMARK_BOOM_GEYSER_ENABLED", true);
    if (!geyser.active) {
        return;
    }
    geyser.center = center;
    geyser.started_ms = now_ms;
    const unsigned int duration_ms = std::max(
        300u,
        env_uint("MINA_XMARK_BOOM_GEYSER_DURATION_MS", 950u));
    geyser.expires_ms = now_ms + duration_ms;
    geyser.ember_count = std::min<unsigned int>(
        _countof(geyser.embers),
        std::max(4u, env_uint("MINA_XMARK_BOOM_GEYSER_EMBER_COUNT", 12u)));
    const float ring_radius = std::max(
        0.25f,
        env_float("MINA_XMARK_BOOM_GEYSER_RING_RADIUS", 2.15f));
    const float arc_height = std::max(
        0.2f,
        env_float("MINA_XMARK_BOOM_GEYSER_ARC_HEIGHT", 2.35f));
    const unsigned int flight_ms = std::max(
        250u,
        env_uint("MINA_XMARK_BOOM_GEYSER_FLIGHT_MS", 650u));
    const unsigned int stagger_ms = env_uint("MINA_XMARK_BOOM_GEYSER_STAGGER_MS", 18u);
    for (unsigned int index = 0; index < geyser.ember_count; ++index) {
        ClashrendBoomEmber &ember = geyser.embers[index];
        const uint32_t seed = xmark_burn_seed(
            static_cast<uintptr_t>(index + 1u), now_ms, index, 0xC105u);
        const float angle =
            (static_cast<float>(index) / static_cast<float>(geyser.ember_count)) *
            6.283185307f;
        ember.active = true;
        ember.origin = center;
        ember.origin.x += std::cos(angle) * 0.08f;
        ember.origin.y += std::sin(angle) * 0.08f;
        ember.landing = center;
        ember.landing.x += std::cos(angle) * ring_radius;
        ember.landing.y += std::sin(angle) * ring_radius * std::max(
            0.25f,
            env_float("MINA_XMARK_BOOM_GEYSER_OVAL_Y_SCALE", 0.58f));
        ember.position = ember.origin;
        ember.arc_height = arc_height * (0.94f + xmark_noise_signed(seed + 3u) * 0.06f);
        ember.flight_ms = flight_ms;
        ember.started_ms = now_ms + static_cast<unsigned long long>(index * stagger_ms);
        ember.expires_ms = ember.started_ms + duration_ms;
    }
    geyser.expires_ms += static_cast<unsigned long long>(
        geyser.ember_count > 0u ? (geyser.ember_count - 1u) * stagger_ms : 0u);
    if (g_mina && env_bool("MINA_XMARK_BOOM_GEYSER_LOG", false)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast geyser started embers=%u center=(%.3f,%.3f,%.3f).\n",
            geyser.ember_count,
            static_cast<double>(center.x),
            static_cast<double>(center.y),
            static_cast<double>(center.z));
        g_mina->Log(message);
    }
}

void clashrend_boom_begin_native_final_x_wave(ClashrendBoomPatternState &boom) {
    const bool native_spawn_final =
        env_bool("MINA_XMARK_BOOM_NATIVE_FINAL_X_SPAWN", true);
    const bool owned_final = native_spawn_final ||
        env_bool("MINA_XMARK_BOOM_MODAPI_FINAL_X_RENDER", false);
    if (owned_final) {
        clashrend_boom_hide_native_pattern_components(boom);
    } else {
        clashrend_boom_apply_native_final_x_wave(boom);
    }
    const unsigned long long now_ms = xmark_status_now_ms();
    boom.third_wave_spawned = true;
    boom.third_wave_reasserted = false;
    boom.third_wave_started_ms = now_ms;
    const Vec3 forward = clashrend_boom_native_forward_step(boom.direction);
    Vec3 final_center = boom.impact;
    final_center.x += forward.x * 2.0f;
    final_center.y += forward.y * 2.0f;
    final_center.z += forward.z * 2.0f;
    boom.third_wave_native_effects_spawned = false;
    Vec3 geyser_center = final_center;
    geyser_center.y +=
        env_float("MINA_XMARK_BOOM_GEYSER_ORIGIN_OFFSET_Y", 0.5f);
    start_clashrend_boom_geyser(geyser_center, now_ms);
    if (g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast final X started center=(%.3f,%.3f,%.3f).\n",
            static_cast<double>(final_center.x),
            static_cast<double>(final_center.y),
            static_cast<double>(final_center.z));
        g_mina->Log(message);
    }
}

bool clashrend_boom_ember_has_hit(
    const ClashrendBoomEmber &ember,
    uintptr_t combat_core) {
    for (unsigned int index = 0; index < ember.hit_core_count; ++index) {
        if (ember.hit_cores[index] == combat_core) {
            return true;
        }
    }
    return false;
}

void clashrend_boom_ember_record_hit(
    ClashrendBoomEmber &ember,
    uintptr_t combat_core) {
    if (!combat_core || clashrend_boom_ember_has_hit(ember, combat_core) ||
        ember.hit_core_count >= _countof(ember.hit_cores)) {
        return;
    }
    ember.hit_cores[ember.hit_core_count++] = combat_core;
}

bool clashrend_boom_damage_enemy_with_ember(
    ClashrendBoomEmber &ember,
    const XMarkOfficialEnemyHost &source_host,
    unsigned long long now_ms) {
    XMarkOfficialEnemyHost host = official_enemy_host_with_resolved_refs(source_host);
    if (!host.active || !host.entity || !host.combat_core ||
        clashrend_boom_ember_has_hit(ember, host.combat_core)) {
        return false;
    }

    Vec3 center = host.position;
    __try {
        const MM_Transform transform = g_mina->EntityGetWorldTransform(
            reinterpret_cast<ycEntity *>(host.entity));
        center = Vec3{transform.t.x, transform.t.y, transform.t.z};
        if (host.body_center_offset_valid) {
            center.x += host.body_center_offset.x;
            center.y += host.body_center_offset.y;
            center.z += host.body_center_offset.z;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    const float host_half_w = std::min(
        3.0f,
        std::max(0.24f, host.visual_bounds_valid ? host.visual_half_w : host.bounds_half_w));
    const float host_half_h = std::min(
        3.5f,
        std::max(0.24f, host.visual_bounds_valid ? host.visual_half_h : host.bounds_half_h));
    const float ember_half_w = std::max(
        0.08f,
        env_float("MINA_XMARK_BOOM_GEYSER_DAMAGE_HALF_W", 0.28f));
    const float ember_half_h = std::max(
        0.08f,
        env_float("MINA_XMARK_BOOM_GEYSER_DAMAGE_HALF_H", 0.28f));
    if (std::fabs(ember.position.x - center.x) > host_half_w + ember_half_w ||
        std::fabs(ember.position.y - center.y) > host_half_h + ember_half_h) {
        return false;
    }

    clashrend_boom_ember_record_hit(ember, host.combat_core);
    if (host.muriel_no_damage || !xmark_combat_core_health_write_api_available()) {
        return true;
    }
    float health = 0.0f;
    if (!combat_core_health_read(host.combat_core, &health) || health <= 0.0f) {
        return true;
    }
    const float damage = std::max(
        0.0f,
        env_float("MINA_XMARK_BOOM_GEYSER_EMBER_DAMAGE", 1.0f));
    XMarkBurnEffect boss_guard{};
    boss_guard.target = host.entity;
    boss_guard.official_combat_core = host.combat_core;
    char scripted_boss_type[96]{};
    const bool scripted_boss =
        health <= damage + 0.001f &&
        xmark_burn_target_is_scripted_boss(
            boss_guard,
            &host,
            scripted_boss_type,
            sizeof(scripted_boss_type));
    const float next_health = scripted_boss
        ? std::max(
            env_float("MINA_XMARK_BURN_SCRIPTED_BOSS_MIN_HEALTH", 0.01f),
            health - damage)
        : std::max(0.0f, health - damage);
    bool wrote = false;
    __try {
        g_mina->CombatCoreSetHealth(
            reinterpret_cast<ycComponent *>(host.combat_core),
            next_health);
        wrote = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wrote = false;
    }
    if (wrote && next_health <= 0.0f) {
        XMarkBurnEffect death{};
        death.target = host.entity;
        death.official_combat_core = host.combat_core;
        death.official_follow = true;
        death.last_position = center;
        death.has_last_position = true;
        force_xmark_burn_death_animation(death, now_ms);
    }
    if (wrote && g_mina && env_bool("MINA_XMARK_BOOM_GEYSER_DAMAGE_LOG", false)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast ember hit target=0x%p core=0x%p health=%.3f next=%.3f pos=(%.3f,%.3f).\n",
            reinterpret_cast<void *>(host.entity),
            reinterpret_cast<void *>(host.combat_core),
            static_cast<double>(health),
            static_cast<double>(next_health),
            static_cast<double>(ember.position.x),
            static_cast<double>(ember.position.y));
        g_mina->Log(message);
    }
    return wrote;
}

void update_clashrend_boom_geyser(unsigned long long now_ms) {
    ClashrendBoomGeyserState &geyser = g_clashrend_boom_geyser;
    if (!geyser.active || xmark_runtime_overlays_hidden_for_pause()) {
        return;
    }
    if (now_ms >= geyser.expires_ms) {
        geyser = ClashrendBoomGeyserState{};
        return;
    }
    const bool descending_only =
        env_bool("MINA_XMARK_BOOM_GEYSER_DAMAGE_DESCENDING_ONLY", true);
    for (unsigned int index = 0; index < geyser.ember_count; ++index) {
        ClashrendBoomEmber &ember = geyser.embers[index];
        if (!ember.active || now_ms < ember.started_ms) {
            continue;
        }
        if (now_ms >= ember.expires_ms) {
            ember.active = false;
            continue;
        }
        const float phase = std::min(
            1.0f,
            static_cast<float>(now_ms - ember.started_ms) /
                static_cast<float>(std::max(1u, ember.flight_ms)));
        const float eased = phase * phase * (3.0f - 2.0f * phase);
        const float elevation = 4.0f * ember.arc_height * phase * (1.0f - phase);
        ember.position.x = ember.origin.x + (ember.landing.x - ember.origin.x) * eased;
        ember.position.y =
            ember.origin.y + (ember.landing.y - ember.origin.y) * eased + elevation;
        ember.position.z = ember.origin.z;
        const bool descending = phase >= 0.5f;
        if (descending_only && !descending) {
            continue;
        }
        for (unsigned int host_index = 0;
             host_index < g_official_enemy_host_count;
             ++host_index) {
            clashrend_boom_damage_enemy_with_ember(
                ember,
                g_official_enemy_hosts[host_index],
                now_ms);
        }
    }
}

void clashrend_boom_finish_native_pattern(ClashrendBoomPatternState &boom) {
    for (unsigned int index = 0; index < boom.native_outer_count; ++index) {
        ClashrendBoomPatternState::NativeAnim &slot = boom.native_outer[index];
        __try {
            g_mina->GameAnimPlay(
                slot.component,
                "burrowBonkFast",
                1,
                8.0f,
                true);
            g_mina->GameAnimSetPaused(slot.component, false);
            g_mina->GameAnimSetVisible(slot.component, false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (boom.native_center) {
        __try {
            g_mina->GameAnimPlay(
                boom.native_center,
                "Default",
                1,
                8.0f,
                true);
            g_mina->GameAnimSetPaused(boom.native_center, false);
            g_mina->GameAnimSetVisible(boom.native_center, false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    boom.active = false;
}

void maybe_start_clashrend_boom_from_native_impact() {
    MinaCrossBlastImpactSharedState impact{};
    if (!read_cross_blast_impact_state(&impact) ||
        impact.sequence == g_clashrend_boom_last_native_impact_sequence) {
        return;
    }
    const unsigned long long wall_now_ms = GetTickCount64();
    const unsigned long long max_age_ms =
        std::max(50u, env_uint("MINA_XMARK_BOOM_NATIVE_IMPACT_MAX_AGE_MS", 1200));
    if (wall_now_ms < impact.tick || wall_now_ms - impact.tick > max_age_ms) {
        g_clashrend_boom_last_native_impact_sequence = impact.sequence;
        return;
    }
    if (g_clashrend_boom_pattern.active) {
        g_clashrend_boom_last_native_impact_sequence = impact.sequence;
        return;
    }
    if (!g_clashrend_boom_pattern.pending) {
        g_clashrend_boom_pattern = ClashrendBoomPatternState{};
        g_clashrend_boom_geyser = ClashrendBoomGeyserState{};
        g_clashrend_boom_pattern.pending = true;
        g_clashrend_boom_pattern.direction = impact.direction;
        g_clashrend_boom_pattern.released_ms = impact.tick;
    }
    g_clashrend_boom_last_native_impact_sequence = impact.sequence;
    start_clashrend_boom_pattern(
        xmark_status_now_ms(),
        impact.direction,
        crater_position_for_direction(impact.direction));
}

bool clashrend_boom_matches_native_explosion_anim(ycComponent *anim, const Vec3 &impact) {
    if (!anim ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqNameNoDir)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumSeqFrames)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetSeqFrameIdx)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetCurrentFrameBound)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimGetWorldTransform))) {
        return false;
    }

    char sequence[32]{};
    uint32_t frame = 0;
    uint32_t frame_count = 0;
    MM_AABB bound{};
    MM_Transform transform{};
    bool read = false;
    __try {
        copy_mm_string_to_cstr(
            sequence,
            sizeof(sequence),
            g_mina->GameAnimGetSeqNameNoDir(anim));
        frame = g_mina->GameAnimGetSeqFrameIdx(anim);
        frame_count = g_mina->GameAnimGetNumSeqFrames(anim);
        g_mina->GameAnimGetCurrentFrameBound(anim, &bound);
        g_mina->GameAnimGetWorldTransform(anim, &transform);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    if (!read || std::strcmp(sequence, "Default") != 0 || frame_count != 5u || frame >= 5u) {
        return false;
    }

    static constexpr float kFrameWidths[5] = {16.0f, 24.0f, 24.0f, 16.0f, 14.0f};
    static constexpr float kFrameHeights[5] = {23.0f, 25.0f, 25.0f, 21.0f, 14.0f};
    const float width = std::fabs(bound.extents.x) * 2.0f;
    const float height = std::fabs(bound.extents.y) * 2.0f;
    const auto dimensions_match = [&](float scale, float tolerance) {
        return std::fabs(width * scale - kFrameWidths[frame]) <= tolerance &&
            std::fabs(height * scale - kFrameHeights[frame]) <= tolerance;
    };
    if (!dimensions_match(1.0f, 1.1f) && !dimensions_match(10.0f, 1.1f)) {
        return false;
    }

    const Vec3 position{transform.t.x, transform.t.y, transform.t.z};
    const float radius = std::max(1.5f, env_float("MINA_XMARK_BOOM_NATIVE_HIDE_RADIUS", 3.5f));
    return xy_distance_sq(position, impact) <= radius * radius;
}

void clashrend_boom_hide_native_explosion_anims(
    ycEntity *entity,
    const Vec3 &impact,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes,
    unsigned int *hidden) {
    if (!entity || !nodes || !hidden || depth > max_depth || *nodes >= max_nodes) {
        return;
    }
    ++(*nodes);
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    if (!child_count) {
        return;
    }

    const size_t child_cap = std::min<size_t>(child_count, 64u);
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
    for (size_t index = 0; index < limit && *nodes < max_nodes; ++index) {
        ycComponent *component = children[index];
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
            clashrend_boom_hide_native_explosion_anims(
                reinterpret_cast<ycEntity *>(component),
                impact,
                depth + 1u,
                max_depth,
                nodes,
                max_nodes,
                hidden);
        } else if (is_game_anim && clashrend_boom_matches_native_explosion_anim(component, impact)) {
            __try {
                g_mina->GameAnimSetVisible(component, false);
                ++(*hidden);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }
    g_mina->Free(children);
}

void clashrend_boom_suppress_native_pattern(ClashrendBoomPatternState &boom) {
    if (!env_bool("MINA_XMARK_BOOM_NATIVE_GAMEANIM_HIDE", false)) {
        return;
    }
    const unsigned int scan_limit =
        std::max(1u, env_uint("MINA_XMARK_BOOM_NATIVE_HIDE_SCANS", 10));
    if (boom.native_suppression_scans >= scan_limit ||
        !g_mina ||
        !api_function_field_ready(offsetof(MinaModAPI, PlayerGetWorld)) ||
        !api_function_field_ready(offsetof(MinaModAPI, WorldGetGameRootEntity)) ||
        !api_function_field_ready(offsetof(MinaModAPI, EntityGetChildren)) ||
        !api_function_field_ready(offsetof(MinaModAPI, GameAnimSetVisible)) ||
        !official_enemy_init_rtti() ||
        !xmark_game_anim_init_rtti()) {
        return;
    }
    ++boom.native_suppression_scans;
    World *world = nullptr;
    ycEntity *root = nullptr;
    __try {
        world = g_mina->PlayerGetWorld();
        root = world ? g_mina->WorldGetGameRootEntity(world) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        root = nullptr;
    }
    if (!root) {
        return;
    }
    unsigned int nodes = 0;
    unsigned int hidden = 0;
    clashrend_boom_hide_native_explosion_anims(
        root,
        boom.impact,
        0,
        env_uint("MINA_XMARK_BOOM_NATIVE_HIDE_MAX_DEPTH", 10),
        &nodes,
        env_uint("MINA_XMARK_BOOM_NATIVE_HIDE_MAX_NODES", 768),
        &hidden);
    boom.native_suppressed_anims += hidden;
    if (hidden && g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Cross Blast hid %u native explosion anims (total=%u scan=%u).\n",
            hidden,
            boom.native_suppressed_anims,
            boom.native_suppression_scans);
        g_mina->Log(message);
    }
}

void update_clashrend_boom_pattern(unsigned long long now_ms) {
    ClashrendBoomPatternState &boom = g_clashrend_boom_pattern;
    if ((!boom.pending && !boom.active) || xmark_runtime_overlays_hidden_for_pause()) {
        return;
    }
    if (boom.pending && !boom.active) {
        const unsigned int pending_window_ms =
            std::max(600u, env_uint("MINA_XMARK_BOOM_PENDING_WINDOW_MS", 1100u));
        if (boom.released_ms && GetTickCount64() > boom.released_ms + pending_window_ms) {
            boom = ClashrendBoomPatternState{};
            return;
        }
        clashrend_boom_capture_native_pattern(boom, now_ms);
        if (!boom.active) {
            return;
        }
    }
    if (env_bool("MINA_XMARK_BOOM_NATIVE_RESHAPE", false)) {
        if (!boom.native_pattern_ready && !clashrend_boom_capture_native_pattern(boom, now_ms)) {
            return;
        }
        if (!boom.first_wave_spawned) {
            clashrend_boom_begin_native_x_wave(boom);
            return;
        }
        const unsigned int wave_gap_ms =
            std::max(40u, env_uint("MINA_XMARK_BOOM_NATIVE_WAVE_GAP_MS", 125u));
        const unsigned int wave_duration_ms =
            std::max(80u, env_uint("MINA_XMARK_BOOM_NATIVE_WAVE_DURATION_MS", 240u));
        if (!boom.second_wave_spawned && now_ms >= boom.started_ms + wave_gap_ms) {
            clashrend_boom_begin_native_plus_wave(boom);
            if (env_bool("MINA_XMARK_BOOM_PLAY_PLUS_SFX", true)) {
                play_clashrend_boom_wave_sfx("boom-plus-wave");
            }
        }
        if (!boom.third_wave_spawned && boom.second_wave_spawned &&
            boom.second_wave_started_ms &&
            now_ms >= boom.second_wave_started_ms + wave_gap_ms) {
            clashrend_boom_begin_native_final_x_wave(boom);
            if (env_bool("MINA_XMARK_BOOM_PLAY_FINAL_X_SFX", true)) {
                play_clashrend_boom_wave_sfx("boom-final-x-wave");
            }
        }
        const unsigned int final_reassert_ms = std::max(
            8u,
            env_uint("MINA_XMARK_BOOM_NATIVE_FINAL_X_REASSERT_MS", 32u));
        if (!env_bool("MINA_XMARK_BOOM_NATIVE_FINAL_X_SPAWN", true) &&
            !env_bool("MINA_XMARK_BOOM_MODAPI_FINAL_X_RENDER", false) &&
            boom.third_wave_spawned && !boom.third_wave_reasserted &&
            boom.third_wave_started_ms &&
            now_ms >= boom.third_wave_started_ms + final_reassert_ms) {
            clashrend_boom_apply_native_final_x_wave(boom);
            boom.third_wave_reasserted = true;
            if (g_mina && env_bool("MINA_XMARK_BOOM_EVENT_LOG", true)) {
                g_mina->Log("XMarkBurn Cross Blast final X reasserted.\n");
            }
        }
        const unsigned int final_x_duration_ms = std::max(
            wave_duration_ms,
            env_uint("MINA_XMARK_BOOM_NATIVE_FINAL_X_DURATION_MS", 360u));
        if (!env_bool("MINA_XMARK_BOOM_NATIVE_FINAL_X_SPAWN", true) &&
            !env_bool("MINA_XMARK_BOOM_MODAPI_FINAL_X_RENDER", false) &&
            boom.third_wave_spawned && boom.third_wave_started_ms &&
            now_ms >= boom.third_wave_started_ms &&
            now_ms < boom.third_wave_started_ms + final_x_duration_ms) {
            clashrend_boom_drive_native_final_x_wave(
                boom,
                now_ms - boom.third_wave_started_ms,
                final_x_duration_ms);
        }
        if (boom.third_wave_spawned && boom.third_wave_started_ms &&
            now_ms >= boom.third_wave_started_ms + final_x_duration_ms) {
            clashrend_boom_log_native_x_state(boom, "finishing");
            clashrend_boom_finish_native_pattern(boom);
        }
        return;
    }
    clashrend_boom_suppress_native_pattern(boom);
    if (!boom.first_wave_spawned) {
        boom.first_wave_spawned = true;
        if (env_bool("MINA_XMARK_BOOM_PLAY_INITIAL_SFX", false)) {
            play_clashrend_boom_wave_sfx("boom-x-wave");
        }
        return;
    }
    const unsigned int wave_duration_ms =
        std::max(40u, env_uint("MINA_XMARK_BOOM_WAVE_DURATION_MS", 175));
    const unsigned int wave_gap_ms = std::max(
        wave_duration_ms,
        std::max(16u, env_uint("MINA_XMARK_BOOM_WAVE_GAP_MS", wave_duration_ms)));
    if (!boom.second_wave_spawned && now_ms >= boom.started_ms + wave_gap_ms) {
        boom.second_wave_spawned = true;
        if (env_bool("MINA_XMARK_BOOM_PLAY_PLUS_SFX", true)) {
            play_clashrend_boom_wave_sfx("boom-plus-wave");
        }
    }
    if (!boom.third_wave_spawned && now_ms >= boom.started_ms + wave_gap_ms * 2ull) {
        boom.third_wave_spawned = true;
        boom.third_wave_started_ms = now_ms;
        const float step_distance =
            std::max(0.1f, env_float("MINA_XMARK_BOOM_FORWARD_STEP", 1.0f));
        const Vec3 step = clashrend_boom_direction_step(boom.direction, step_distance);
        Vec3 final_center = boom.impact;
        final_center.x += step.x * 2.0f;
        final_center.y += step.y * 2.0f;
        final_center.z += step.z * 2.0f;
        start_clashrend_boom_geyser(final_center, now_ms);
        if (env_bool("MINA_XMARK_BOOM_PLAY_FINAL_X_SFX", true)) {
            play_clashrend_boom_wave_sfx("boom-final-x-wave");
        }
    }
    if (boom.third_wave_spawned &&
        now_ms >= boom.started_ms + wave_gap_ms * 2ull + wave_duration_ms) {
        boom.active = false;
    }
}

