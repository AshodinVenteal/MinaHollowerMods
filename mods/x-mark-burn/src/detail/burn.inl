bool xmark_burn_palette_api_available() {
    return api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPalette)) &&
           api_function_field_ready(offsetof(MinaModAPI, GameAnimSetPalette)) &&
           api_function_field_ready(offsetof(MinaModAPI, ClonePalette)) &&
           api_function_field_ready(offsetof(MinaModAPI, ReleasePalette)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteGetWidth)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteGetLocalColor)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteWrite)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteWriteIndex)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteSetGroup)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteGetGroup)) &&
           api_function_field_ready(offsetof(MinaModAPI, PaletteGetGroupPal));
}

void capture_xmark_marked_palette(XMarkEnemyStatusRecord &status) {
    if (status.marked_original_palette ||
        !status.active ||
        !xmark_burn_palette_api_available()) {
        return;
    }

    XMarkOfficialEnemyHost host{};
    const bool host_resolved =
        (status.combat_core &&
         official_enemy_host_by_combat_core(status.combat_core, &host)) ||
        (status.entity && official_enemy_host_by_entity(status.entity, &host));
    if (!host_resolved || !host.game_anim) {
        return;
    }

    __try {
        ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(
            reinterpret_cast<ycComponent *>(host.game_anim));
        ycPaletteTexture *snapshot_source = live_palette;
        if (!status.training_target &&
            live_palette &&
            g_mina->PaletteGetGroup(live_palette) >= 0) {
            ycPaletteTexture *group_palette =
                g_mina->PaletteGetGroupPal(live_palette);
            if (group_palette) {
                snapshot_source = group_palette;
            }
        }
        if (snapshot_source) {
            status.marked_original_palette =
                g_mina->ClonePalette(snapshot_source);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        status.marked_original_palette = nullptr;
    }
}

void transfer_xmark_marked_palette_to_burn(
    XMarkEnemyStatusRecord &status,
    XMarkBurnEffect &burn) {
    if (burn.original_palette || !status.marked_original_palette) {
        return;
    }
    burn.original_palette = status.marked_original_palette;
    status.marked_original_palette = nullptr;
}

void finish_xmark_burn_palette_release(XMarkBurnEffect &burn) {
    xmark_weak_ptr_destroy(burn.palette_anim_weak);
    burn.palette_anim_weak = nullptr;
    for (unsigned int i = 0; i < burn.palette_tree_count &&
         i < _countof(burn.palette_tree_anim_weak); ++i) {
        xmark_weak_ptr_destroy(burn.palette_tree_anim_weak[i]);
        burn.palette_tree_anim_weak[i] = nullptr;
        if (burn.palette_tree_original[i] && xmark_burn_palette_api_available()) {
            __try {
                g_mina->ReleasePalette(burn.palette_tree_original[i]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        burn.palette_tree_original[i] = nullptr;
    }
    burn.palette_tree_count = 0;
    if (xmark_burn_palette_api_available()) {
        for (ycPaletteTexture **palette : {
                 &burn.original_palette,
                 &burn.burn_palette,
                 &burn.burn_palette_hot,
                 &burn.burn_palette_bright}) {
            if (!*palette) {
                continue;
            }
            __try {
                g_mina->ReleasePalette(*palette);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            *palette = nullptr;
        }
    } else {
        burn.original_palette = nullptr;
        burn.burn_palette = nullptr;
        burn.burn_palette_hot = nullptr;
        burn.burn_palette_bright = nullptr;
    }
    burn.palette_applied = false;
    burn.palette_restore_pending = false;
    burn.palette_apply_after_ms = 0;
    burn.palette_next_switch_ms = 0;
    burn.palette_next_anim_resolve_ms = 0;
    burn.palette_restore_until_ms = 0;
    burn.palette_next_restore_ms = 0;
    burn.palette_phase = 0;
}

int xmark_burn_palette_snapshot_index(
    const XMarkBurnEffect &burn,
    ycComponent *anim) {
    if (!anim) {
        return -1;
    }
    for (unsigned int i = 0; i < burn.palette_tree_count &&
         i < _countof(burn.palette_tree_anim_weak); ++i) {
        if (xmark_weak_ptr_get(burn.palette_tree_anim_weak[i]) == anim) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool capture_xmark_burn_anim_palette(
    XMarkBurnEffect &burn,
    ycComponent *anim) {
    if (!anim || !xmark_burn_palette_api_available()) {
        return false;
    }
    if (xmark_burn_palette_snapshot_index(burn, anim) >= 0) {
        return true;
    }
    if (burn.palette_tree_count >= _countof(burn.palette_tree_anim_weak)) {
        return false;
    }

    ycPaletteTexture *snapshot = nullptr;
    MM_WeakPtr *anim_weak = nullptr;
    __try {
        ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(anim);
        const bool live_is_burn_palette =
            live_palette == burn.burn_palette ||
            live_palette == burn.burn_palette_hot ||
            live_palette == burn.burn_palette_bright;
        ycPaletteTexture *snapshot_source = live_is_burn_palette
            ? burn.original_palette
            : live_palette;
        snapshot = snapshot_source
            ? g_mina->ClonePalette(snapshot_source)
            : nullptr;
        anim_weak = snapshot ? xmark_weak_ptr_create(anim) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        snapshot = nullptr;
        anim_weak = nullptr;
    }
    if (!snapshot || !anim_weak) {
        if (anim_weak) {
            xmark_weak_ptr_destroy(anim_weak);
        }
        if (snapshot) {
            __try {
                g_mina->ReleasePalette(snapshot);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        return false;
    }

    const unsigned int slot = burn.palette_tree_count++;
    burn.palette_tree_anim_weak[slot] = anim_weak;
    burn.palette_tree_original[slot] = snapshot;
    return true;
}

bool restore_xmark_burn_anim_palette(
    XMarkBurnEffect &burn,
    ycComponent *anim) {
    const int snapshot_index = xmark_burn_palette_snapshot_index(burn, anim);
    if (snapshot_index < 0) {
        return false;
    }
    ycPaletteTexture *palette =
        burn.palette_tree_original[static_cast<unsigned int>(snapshot_index)];
    if (!palette) {
        return false;
    }
    __try {
        g_mina->GameAnimSetPalette(anim, palette);
        return g_mina->GameAnimGetPalette(anim) == palette;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void capture_muriel_palette_tree(
    ycEntity *entity,
    XMarkBurnEffect &burn,
    unsigned int depth,
    unsigned int *nodes,
    unsigned int max_nodes) {
    if (!entity || !nodes || depth > 4u || *nodes >= max_nodes ||
        burn.palette_tree_count >= _countof(burn.palette_tree_anim_weak) ||
        !g_mina || !g_mina->EntityGetChildren || !g_mina->ComponentGetType ||
        !g_mina->ComponentIsa || !g_mina->GameAnimGetPalette ||
        !g_mina->ClonePalette || !g_mina->Alloc || !g_mina->Free) {
        return;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!child_count) {
        return;
    }
    const size_t cap = std::min<size_t>(child_count, 64u);
    ycComponent **children = static_cast<ycComponent **>(
        g_mina->Alloc(sizeof(ycComponent *) * cap));
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
    for (size_t i = 0; i < limit && *nodes < max_nodes &&
         burn.palette_tree_count < _countof(burn.palette_tree_anim_weak); ++i) {
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
            continue;
        }
        if (is_entity) {
            capture_muriel_palette_tree(
                reinterpret_cast<ycEntity *>(component),
                burn,
                depth + 1u,
                nodes,
                max_nodes);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }
        __try {
            ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(component);
            ycPaletteTexture *palette_clone = live_palette
                ? g_mina->ClonePalette(live_palette)
                : nullptr;
            MM_WeakPtr *anim_weak = palette_clone
                ? xmark_weak_ptr_create(component)
                : nullptr;
            if (!palette_clone || !anim_weak) {
                if (palette_clone) {
                    g_mina->ReleasePalette(palette_clone);
                }
                continue;
            }
            const unsigned int slot = burn.palette_tree_count++;
            burn.palette_tree_anim_weak[slot] = anim_weak;
            burn.palette_tree_original[slot] = palette_clone;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_mina->Free(children);
}

unsigned int restore_muriel_captured_palette_tree(XMarkBurnEffect &burn) {
    unsigned int restored = 0;
    for (unsigned int i = 0; i < burn.palette_tree_count &&
         i < _countof(burn.palette_tree_anim_weak); ++i) {
        ycComponent *anim = static_cast<ycComponent *>(
            xmark_weak_ptr_get(burn.palette_tree_anim_weak[i]));
        ycPaletteTexture *palette = burn.palette_tree_original[i];
        if (!anim || !palette) {
            continue;
        }
        __try {
            g_mina->GameAnimSetPalette(anim, palette);
            ++restored;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return restored;
}

bool restore_muriel_palette_ledger_now(
    XMarkBurnEffect &burn,
    unsigned int *restored_out,
    unsigned int *verified_out) {
    unsigned int restored = restore_muriel_captured_palette_tree(burn);

    ycComponent *captured_anim = burn.palette_anim_weak
        ? static_cast<ycComponent *>(xmark_weak_ptr_get(burn.palette_anim_weak))
        : nullptr;
    XMarkOfficialEnemyHost current_host{};
    const bool current_host_resolved = official_enemy_host_for_burn(burn, &current_host);
    ycComponent *current_anim = current_host_resolved && current_host.game_anim
        ? reinterpret_cast<ycComponent *>(current_host.game_anim)
        : captured_anim;

    for (ycComponent *anim : {captured_anim, current_anim}) {
        if (!anim) {
            continue;
        }
        if (restore_xmark_burn_anim_palette(burn, anim)) {
            continue;
        }
        __try {
            ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(anim);
            const bool live_is_burn_palette =
                live_palette == burn.burn_palette ||
                live_palette == burn.burn_palette_hot ||
                live_palette == burn.burn_palette_bright;
            if (live_is_burn_palette && burn.original_palette) {
                g_mina->GameAnimSetPalette(anim, burn.original_palette);
                ++restored;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    unsigned int verified = 0;
    unsigned int live_snapshots = 0;
    for (unsigned int i = 0; i < burn.palette_tree_count &&
         i < _countof(burn.palette_tree_anim_weak); ++i) {
        ycComponent *anim = static_cast<ycComponent *>(
            xmark_weak_ptr_get(burn.palette_tree_anim_weak[i]));
        ycPaletteTexture *palette = burn.palette_tree_original[i];
        if (!anim || !palette) {
            continue;
        }
        ++live_snapshots;
        __try {
            if (g_mina->GameAnimGetPalette(anim) == palette) {
                ++verified;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (restored_out) {
        *restored_out = restored;
    }
    if (verified_out) {
        *verified_out = verified;
    }
    return live_snapshots > 0u && verified == live_snapshots;
}

bool restore_xmark_burn_palette_now(XMarkBurnEffect &burn) {
    if (!xmark_burn_palette_api_available() || !burn.original_palette) {
        return false;
    }
    bool restored = false;
    ycComponent *captured_anim = burn.palette_anim_weak
        ? static_cast<ycComponent *>(xmark_weak_ptr_get(burn.palette_anim_weak))
        : nullptr;
    XMarkOfficialEnemyHost current_host{};
    const bool current_host_resolved = official_enemy_host_for_burn(burn, &current_host);
    ycComponent *current_anim =
        current_host_resolved && current_host.game_anim
            ? reinterpret_cast<ycComponent *>(current_host.game_anim)
            : captured_anim;
    if (!captured_anim && !current_anim) {
        return restored;
    }
    __try {
        ycPaletteTexture *restore_palette = burn.original_palette;
        const bool muriel_palette =
            burn.suppress_damage ||
            (current_host_resolved && xmark_official_host_is_muriel(current_host));
        if (!muriel_palette &&
            env_bool("MINA_XMARK_BURN_PALETTE_RESTORE_GROUP_PALETTE", true) &&
            g_mina->PaletteGetGroup(restore_palette) >= 0) {
            ycPaletteTexture *group_palette = g_mina->PaletteGetGroupPal(restore_palette);
            if (group_palette) {
                restore_palette = group_palette;
            }
        }
        if (!muriel_palette) {
            for (unsigned int i = 0; i < burn.palette_tree_count &&
                 i < _countof(burn.palette_tree_anim_weak); ++i) {
                ycComponent *tree_anim = static_cast<ycComponent *>(
                    xmark_weak_ptr_get(burn.palette_tree_anim_weak[i]));
                if (!tree_anim) {
                    continue;
                }
                g_mina->GameAnimSetPalette(tree_anim, restore_palette);
                restored = true;
            }
        }
        if (captured_anim) {
            g_mina->GameAnimSetPalette(captured_anim, restore_palette);
            restored = true;
        }
        if (current_anim && current_anim != captured_anim) {
            ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(current_anim);
            if (muriel_palette ||
                live_palette == burn.burn_palette ||
                live_palette == burn.burn_palette_hot ||
                live_palette == burn.burn_palette_bright) {
                g_mina->GameAnimSetPalette(current_anim, restore_palette);
                restored = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        restored = false;
    }
    return restored;
}

unsigned int restore_muriel_burn_palette_tree(
    ycEntity *entity,
    XMarkBurnEffect &burn,
    ycPaletteTexture *restore_palette,
    unsigned int depth,
    unsigned int *nodes,
    unsigned int max_nodes) {
    if (!entity || !restore_palette || !nodes || depth > 4u || *nodes >= max_nodes ||
        !g_mina || !g_mina->EntityGetChildren || !g_mina->ComponentGetType ||
        !g_mina->ComponentIsa || !g_mina->GameAnimGetPalette ||
        !g_mina->GameAnimSetPalette || !g_mina->Alloc || !g_mina->Free) {
        return 0;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!child_count) {
        return 0;
    }
    const size_t cap = std::min<size_t>(child_count, 64u);
    ycComponent **children = static_cast<ycComponent **>(
        g_mina->Alloc(sizeof(ycComponent *) * cap));
    if (!children) {
        return 0;
    }
    size_t read_count = 0;
    __try {
        read_count = g_mina->EntityGetChildren(entity, children, cap);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read_count = 0;
    }
    unsigned int restored = 0;
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
            continue;
        }
        if (is_entity) {
            restored += restore_muriel_burn_palette_tree(
                reinterpret_cast<ycEntity *>(component),
                burn,
                restore_palette,
                depth + 1u,
                nodes,
                max_nodes);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }
        __try {
            ycPaletteTexture *live_palette = g_mina->GameAnimGetPalette(component);
            if (live_palette == burn.burn_palette ||
                live_palette == burn.burn_palette_hot ||
                live_palette == burn.burn_palette_bright) {
                g_mina->GameAnimSetPalette(component, restore_palette);
                ++restored;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_mina->Free(children);
    return restored;
}

bool restore_muriel_palette_file_now(XMarkBurnEffect &burn) {
    if (!burn.suppress_damage || !g_mina) {
        return false;
    }
    ycComponent *captured_anim = burn.palette_anim_weak
        ? static_cast<ycComponent *>(xmark_weak_ptr_get(burn.palette_anim_weak))
        : nullptr;
    XMarkOfficialEnemyHost current_host{};
    const bool current_host_resolved = official_enemy_host_for_burn(burn, &current_host);
    ycComponent *current_anim =
        current_host_resolved && current_host.game_anim
            ? reinterpret_cast<ycComponent *>(current_host.game_anim)
            : captured_anim;
    if (!captured_anim && !current_anim && !burn.palette_tree_count) {
        return false;
    }
    bool restored = restore_muriel_captured_palette_tree(burn) > 0u;
    ycPaletteTexture *canonical_palette = nullptr;
    __try {
        const bool direct_palette_ready =
            api_function_field_ready(offsetof(MinaModAPI, CreatePalette)) &&
            api_function_field_ready(offsetof(MinaModAPI, GameAnimSetPalette)) &&
            api_function_field_ready(offsetof(MinaModAPI, ReleasePalette));
        ycPaletteTexture *restore_palette = burn.original_palette;
        if (!restore_palette && direct_palette_ready) {
            canonical_palette = g_mina->CreatePalette(
                "palettes/NPCs/murielRabbit.pal.yc");
            restore_palette = canonical_palette;
        }
        ycComponent *restore_anims[2] = {captured_anim, current_anim};
        for (unsigned int i = 0; i < _countof(restore_anims); ++i) {
            ycComponent *anim = restore_anims[i];
            if (!anim || (i > 0u && anim == restore_anims[0])) {
                continue;
            }
            if (restore_palette) {
                g_mina->GameAnimSetPalette(anim, restore_palette);
                restored = true;
            } else if (api_function_field_ready(offsetof(MinaModAPI, GameAnimSetPaletteFile))) {
                g_mina->GameAnimSetPaletteFile(
                    anim,
                    "palettes/NPCs/murielRabbit.pal.yc");
                restored = true;
            }
        }
        if (restore_palette && current_host_resolved && current_host.entity &&
            official_enemy_init_rtti() && xmark_game_anim_init_rtti()) {
            unsigned int nodes = 0;
            restored = restore_muriel_burn_palette_tree(
                reinterpret_cast<ycEntity *>(current_host.entity),
                burn,
                restore_palette,
                0u,
                &nodes,
                128u) > 0u || restored;
        }
        if (canonical_palette) {
            g_mina->ReleasePalette(canonical_palette);
            canonical_palette = nullptr;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (canonical_palette &&
            api_function_field_ready(offsetof(MinaModAPI, ReleasePalette))) {
            __try {
                g_mina->ReleasePalette(canonical_palette);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        restored = false;
    }
    return restored;
}

void release_xmark_burn_palette(XMarkBurnEffect &burn, bool restore) {
    if (restore) {
        if (burn.suppress_damage) {
            restore_muriel_palette_ledger_now(burn, nullptr, nullptr);
        } else {
            restore_xmark_burn_palette_now(burn);
            restore_muriel_palette_file_now(burn);
        }
    }
    finish_xmark_burn_palette_release(burn);
}

void begin_xmark_burn_palette_restore(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (burn.suppress_damage) {
        unsigned int restored = 0;
        unsigned int verified = 0;
        const unsigned int snapshot_count = burn.palette_tree_count;
        const bool exact_restore_verified = restore_muriel_palette_ledger_now(
            burn,
            &restored,
            &verified);
        if (g_mina && env_bool("MINA_XMARK_MURIEL_PALETTE_RESTORE_LOG", true)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn Muriel palette end now=%llu expires=%llu snapshots=%u restored=%u verified=%u exact=%u\n",
                now_ms,
                burn.expires_ms,
                snapshot_count,
                restored,
                verified,
                exact_restore_verified ? 1u : 0u);
            g_mina->Log(message);
        }
        finish_xmark_burn_palette_release(burn);
        return;
    }
    if (!burn.palette_applied || !burn.original_palette) {
        release_xmark_burn_palette(burn, true);
        return;
    }
    restore_xmark_burn_palette_now(burn);
    restore_muriel_palette_file_now(burn);
    burn.palette_applied = false;
    burn.palette_restore_pending = true;
    burn.palette_next_restore_ms = now_ms;
    burn.palette_restore_until_ms = now_ms +
        std::max(
            100u,
            burn.suppress_damage
                ? env_uint("MINA_XMARK_MURIEL_BURN_PALETTE_RESTORE_SETTLE_MS", 3000)
                : env_uint("MINA_XMARK_BURN_PALETTE_RESTORE_SETTLE_MS", 650));
}

void update_xmark_burn_palette_restore(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!burn.palette_restore_pending) {
        return;
    }
    if (now_ms >= burn.palette_next_restore_ms) {
        restore_xmark_burn_palette_now(burn);
        restore_muriel_palette_file_now(burn);
        burn.palette_next_restore_ms = now_ms +
            std::max(
                16u,
                burn.suppress_damage
                    ? env_uint("MINA_XMARK_MURIEL_BURN_PALETTE_RESTORE_INTERVAL_MS", 200)
                    : env_uint("MINA_XMARK_BURN_PALETTE_RESTORE_INTERVAL_MS", 64));
    }
    if (now_ms >= burn.palette_restore_until_ms) {
        restore_xmark_burn_palette_now(burn);
        restore_muriel_palette_file_now(burn);
        finish_xmark_burn_palette_release(burn);
    }
}

bool apply_xmark_burn_palette(
    XMarkBurnEffect &burn,
    const XMarkOfficialEnemyHost &host) {
    if (!env_bool("MINA_XMARK_BURN_NATIVE_PALETTE_ENABLED", true) ||
        !xmark_burn_palette_api_available() || !host.game_anim) {
        return false;
    }
    ycComponent *anim = reinterpret_cast<ycComponent *>(host.game_anim);
    ycPaletteTexture *source = nullptr;
    ycPaletteTexture *original = nullptr;
    ycPaletteTexture *burn_palette = nullptr;
    ycPaletteTexture *burn_palette_hot = nullptr;
    ycPaletteTexture *burn_palette_bright = nullptr;
    MM_Color *deep_colors = nullptr;
    MM_Color *hot_colors = nullptr;
    MM_Color *bright_colors = nullptr;
    bool applied = false;
    const bool original_from_mark = burn.original_palette != nullptr;
    __try {
        source = g_mina->GameAnimGetPalette(anim);
        capture_xmark_burn_anim_palette(burn, anim);
        if (source) {
            original = original_from_mark
                ? burn.original_palette
                : g_mina->ClonePalette(source);
            burn_palette = g_mina->ClonePalette(source);
            burn_palette_hot = g_mina->ClonePalette(source);
            burn_palette_bright = g_mina->ClonePalette(source);
        }
        if (original && burn_palette && burn_palette_hot && burn_palette_bright) {
            for (ycPaletteTexture *palette : {burn_palette, burn_palette_hot, burn_palette_bright}) {
                g_mina->PaletteSetGroup(palette, -1);
            }
            const uint32_t width = g_mina->PaletteGetWidth(burn_palette);
            deep_colors = static_cast<MM_Color *>(std::calloc(width, sizeof(MM_Color)));
            hot_colors = static_cast<MM_Color *>(std::calloc(width, sizeof(MM_Color)));
            bright_colors = static_cast<MM_Color *>(std::calloc(width, sizeof(MM_Color)));
            if (!deep_colors || !hot_colors || !bright_colors) {
                __leave;
            }
            for (uint32_t i = 0; i < width; ++i) {
                MM_Color color{};
                g_mina->PaletteGetLocalColor(source, i, &color);
                if (!color.a) {
                    deep_colors[i] = color;
                    hot_colors[i] = color;
                    bright_colors[i] = color;
                    continue;
                }
                const unsigned int luminance =
                    (static_cast<unsigned int>(color.r) * 3u +
                     static_cast<unsigned int>(color.g) * 6u +
                     static_cast<unsigned int>(color.b)) / 10u;
                MM_Color deep{};
                MM_Color hot{};
                MM_Color bright{};
                if (luminance < 72u) {
                    deep = MM_Color{40, 0, 4, color.a};
                    hot = MM_Color{58, 0, 5, color.a};
                    bright = MM_Color{74, 2, 5, color.a};
                } else if (luminance < 176u) {
                    deep = MM_Color{138, 5, 10, color.a};
                    hot = MM_Color{188, 8, 12, color.a};
                    bright = MM_Color{226, 20, 12, color.a};
                } else {
                    deep = MM_Color{226, 28, 16, color.a};
                    hot = MM_Color{255, 58, 24, color.a};
                    bright = MM_Color{255, 112, 42, color.a};
                }
                deep_colors[i] = deep;
                hot_colors[i] = hot;
                bright_colors[i] = bright;
            }
            if (width) {
                g_mina->PaletteWrite(
                    burn_palette,
                    deep_colors,
                    0,
                    static_cast<int32_t>(width));
                g_mina->PaletteWrite(
                    burn_palette_hot,
                    hot_colors,
                    0,
                    static_cast<int32_t>(width));
                g_mina->PaletteWrite(
                    burn_palette_bright,
                    bright_colors,
                    0,
                    static_cast<int32_t>(width));
            }
            g_mina->GameAnimSetPalette(anim, burn_palette);
            applied = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        applied = false;
    }
    std::free(deep_colors);
    std::free(hot_colors);
    std::free(bright_colors);
    if (!applied) {
        if (original && !original_from_mark) {
            __try { g_mina->ReleasePalette(original); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        for (ycPaletteTexture *palette : {burn_palette, burn_palette_hot, burn_palette_bright}) {
            if (!palette) {
                continue;
            }
            __try { g_mina->ReleasePalette(palette); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        return false;
    }
    burn.palette_anim_weak = xmark_weak_ptr_create(anim);
    burn.original_palette = original;
    burn.burn_palette = burn_palette;
    burn.burn_palette_hot = burn_palette_hot;
    burn.burn_palette_bright = burn_palette_bright;
    burn.palette_applied = true;
    burn.palette_phase = 0;
    burn.palette_next_switch_ms = burn.palette_apply_after_ms +
        std::max(50u, env_uint("MINA_XMARK_BURN_PALETTE_CYCLE_MS", 120));
    burn.palette_next_anim_resolve_ms = burn.palette_apply_after_ms +
        std::max(
            16u,
            env_uint(
                burn.suppress_damage
                    ? "MINA_XMARK_MURIEL_BURN_PALETTE_ANIM_POLL_MS"
                    : "MINA_XMARK_BURN_PALETTE_ANIM_POLL_MS",
                burn.suppress_damage ? 100u : 32u));
    return true;
}

void refresh_xmark_burn_palette(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!burn.palette_applied) {
        if (!burn.active || !burn.palette_apply_after_ms || now_ms < burn.palette_apply_after_ms) {
            return;
        }
        XMarkOfficialEnemyHost host{};
        if (official_enemy_host_for_burn(burn, &host) && host.game_anim) {
            apply_xmark_burn_palette(burn, host);
        }
        return;
    }
    if (!burn.palette_applied || !burn.burn_palette ||
        !xmark_burn_palette_api_available()) {
        return;
    }
    ycComponent *anim = burn.palette_anim_weak
        ? static_cast<ycComponent *>(xmark_weak_ptr_get(burn.palette_anim_weak))
        : nullptr;
    bool migrated_anim = false;
    const bool muriel_palette = burn.suppress_damage;
    ycComponent *current_anim = nullptr;
    const bool resolve_current_anim =
        !anim || !burn.palette_next_anim_resolve_ms ||
        now_ms >= burn.palette_next_anim_resolve_ms;
    if (resolve_current_anim) {
        burn.palette_next_anim_resolve_ms = now_ms +
            std::max(
                16u,
                env_uint(
                    muriel_palette
                        ? "MINA_XMARK_MURIEL_BURN_PALETTE_ANIM_POLL_MS"
                        : "MINA_XMARK_BURN_PALETTE_ANIM_POLL_MS",
                    muriel_palette ? 100u : 32u));
        XMarkOfficialEnemyHost current_host{};
        if (official_enemy_host_for_burn(burn, &current_host) && current_host.game_anim) {
            current_anim = reinterpret_cast<ycComponent *>(current_host.game_anim);
        }
    }
    if (current_anim && current_anim != anim) {
        if (anim && !restore_xmark_burn_anim_palette(burn, anim) &&
            burn.original_palette) {
            __try {
                g_mina->GameAnimSetPalette(anim, burn.original_palette);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        capture_xmark_burn_anim_palette(burn, current_anim);
        xmark_weak_ptr_destroy(burn.palette_anim_weak);
        burn.palette_anim_weak = xmark_weak_ptr_create(current_anim);
        anim = current_anim;
        migrated_anim = true;
    }
    if (!anim) {
        return;
    }
    if (!migrated_anim && now_ms < burn.palette_next_switch_ms) {
        return;
    }
    __try {
        if (!migrated_anim) {
            burn.palette_phase = (burn.palette_phase + 1u) % 3u;
        }
        ycPaletteTexture *next_palette = burn.palette_phase == 0u
            ? burn.burn_palette
            : (burn.palette_phase == 1u ? burn.burn_palette_hot : burn.burn_palette_bright);
        g_mina->GameAnimSetPalette(anim, next_palette);
        burn.palette_next_switch_ms = now_ms +
            std::max(50u, env_uint("MINA_XMARK_BURN_PALETTE_CYCLE_MS", 120));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        release_xmark_burn_palette(burn, false);
    }
}

XMarkBurnEffect *alloc_xmark_burn_effect_slot() {
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active && !burn.palette_restore_pending) {
            return &burn;
        }
    }
    return &g_xmark_burn_effects[0];
}

void end_xmark_burn_effect(XMarkBurnEffect &burn, unsigned long long now_ms) {
    begin_xmark_burn_palette_restore(burn, now_ms);
    if (burn.target) {
        shorten_xmark_hud_mark(burn.target, now_ms);
    }
    if (XMarkEnemyStatusRecord *status = xmark_enemy_status_by_id(burn.status_id)) {
        if (status->burn_index == static_cast<unsigned int>(&burn - g_xmark_burn_effects) + 1u) {
            xmark_enemy_status_release(*status);
        }
    }
    burn.active = false;
    bool another_burn_active = false;
    for (const XMarkBurnEffect &other : g_xmark_burn_effects) {
        if (&other != &burn && other.active && now_ms < other.expires_ms) {
            another_burn_active = true;
            break;
        }
    }
    if (!another_burn_active) {
        stop_xmark_owned_burn_tick_sfx("burn-end");
    }
}

void spawn_xmark_burn_death_burst(const XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_DEATH_BURST_ENABLED", true) || !burn.has_last_position) {
        return;
    }

    XMarkBurnDeathBurst *slot = nullptr;
    for (XMarkBurnDeathBurst &burst : g_xmark_burn_death_bursts) {
        if (!burst.active || now_ms >= burst.expires_ms) {
            slot = &burst;
            break;
        }
    }
    if (!slot) {
        slot = &g_xmark_burn_death_bursts[0];
    }

    *slot = XMarkBurnDeathBurst{};
    slot->active = true;
    slot->position = burn.has_lethal_freeze_position ? burn.lethal_freeze_position : burn.last_position;
    slot->started_ms = now_ms;
    slot->expires_ms = now_ms + std::max(1u, env_uint("MINA_XMARK_BURN_DEATH_BURST_MS", 360));
    slot->seed_target = burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key);
    slot->half_w = burn.render_half_w > 0.0f ? burn.render_half_w : env_float("MINA_XMARK_BURN_DEATH_BURST_FALLBACK_HALF_W", 0.8f);
    slot->half_h = burn.render_half_h > 0.0f ? burn.render_half_h : env_float("MINA_XMARK_BURN_DEATH_BURST_FALLBACK_HALF_H", 0.8f);

    if (g_mina && env_bool("MINA_XMARK_BURN_DEATH_BURST_LOG", false)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn death burst spawned target=0x%p pos=(%.3f, %.3f, %.3f) durationMs=%u\n",
            reinterpret_cast<void *>(burn.target),
            static_cast<double>(slot->position.x),
            static_cast<double>(slot->position.y),
            static_cast<double>(slot->position.z),
            static_cast<unsigned int>(slot->expires_ms - slot->started_ms));
        g_mina->Log(message);
    }
}

bool apply_xmark_burn_lethal_cleanup(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_LETHAL_CLEANUP_ENABLED", false) ||
        burn.lethal_cleanup_applied ||
        !burn.lethal_written_ms ||
        !burn.target ||
        !probable_heap_object(burn.target) ||
        !g_mina ||
        !g_mina->EntityGetWorldTransform ||
        !g_mina->EntitySetWorldTransform) {
        return false;
    }
    const unsigned int grace_ms = env_uint("MINA_XMARK_BURN_LETHAL_CLEANUP_GRACE_MS", 120);
    if (now_ms < burn.lethal_written_ms + grace_ms) {
        return false;
    }

    bool moved = false;
    MM_Transform transform{};
    __try {
        transform = g_mina->EntityGetWorldTransform(reinterpret_cast<ycEntity *>(burn.target));
        const float offset_x = env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_OFFSET_X", 0.0f);
        const float offset_y = env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_OFFSET_Y", -96.0f);
        const float scale = std::max(0.0f, env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_SCALE", 0.0f));
        transform.t.x += offset_x;
        transform.t.y += offset_y;
        transform.s.x = scale;
        transform.s.y = scale;
        transform.s.z = scale;
        g_mina->EntitySetWorldTransform(reinterpret_cast<ycEntity *>(burn.target), &transform);
        moved = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        moved = false;
    }

    if (moved && burn.official_combat_core && g_mina->CombatCoreSetHealth) {
        __try {
            g_mina->CombatCoreSetHealth(
                reinterpret_cast<ycComponent *>(burn.official_combat_core),
                env_float("MINA_XMARK_BURN_DAMAGE_KILL_HEALTH", 0.0f));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    burn.lethal_cleanup_applied = moved;
    if (moved && g_mina && env_bool("MINA_XMARK_BURN_LETHAL_CLEANUP_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn lethal cleanup target=0x%p officialCore=0x%p moved=%u offset=(%.3f, %.3f) scale=%.3f pos=(%.3f, %.3f, %.3f)\n",
            reinterpret_cast<void *>(burn.target),
            reinterpret_cast<void *>(burn.official_combat_core),
            moved ? 1u : 0u,
            static_cast<double>(env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_OFFSET_X", 0.0f)),
            static_cast<double>(env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_OFFSET_Y", -96.0f)),
            static_cast<double>(env_float("MINA_XMARK_BURN_LETHAL_CLEANUP_SCALE", 0.0f)),
            static_cast<double>(burn.last_position.x),
            static_cast<double>(burn.last_position.y),
            static_cast<double>(burn.last_position.z));
        g_mina->Log(message);
    }
    if (moved && env_bool("MINA_XMARK_BURN_END_ON_LETHAL_CLEANUP", true)) {
        spawn_xmark_burn_death_burst(burn, now_ms);
        end_xmark_burn_effect(burn, now_ms);
    }
    return moved;
}

bool capture_xmark_burn_lethal_freeze_position(XMarkBurnEffect &burn) {
    if (burn.has_lethal_freeze_position) {
        return true;
    }
    Vec3 freeze_position{};
    if (burn.target && official_entity_world_position_read(burn.target, &freeze_position)) {
        burn.lethal_freeze_position = freeze_position;
        burn.has_lethal_freeze_position = true;
        return true;
    }
    if (burn.has_last_position) {
        burn.lethal_freeze_position = burn.last_position;
        burn.has_lethal_freeze_position = true;
        return true;
    }
    return false;
}

bool apply_xmark_burn_lethal_motion_freeze(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_FREEZE_ON_DEATH", false) ||
        !burn.lethal_written_ms ||
        burn.lethal_cleanup_applied ||
        !burn.target ||
        !probable_heap_object(burn.target) ||
        !g_mina ||
        !g_mina->EntityGetWorldTransform ||
        !g_mina->EntitySetWorldTransform ||
        !capture_xmark_burn_lethal_freeze_position(burn)) {
        return false;
    }

    bool froze = false;
    __try {
        MM_Transform transform = g_mina->EntityGetWorldTransform(reinterpret_cast<ycEntity *>(burn.target));
        transform.t.x = burn.lethal_freeze_position.x;
        transform.t.y = burn.lethal_freeze_position.y;
        transform.t.z = burn.lethal_freeze_position.z;
        g_mina->EntitySetWorldTransform(reinterpret_cast<ycEntity *>(burn.target), &transform);
        froze = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        froze = false;
    }

    if (froze) {
        burn.last_position = burn.lethal_freeze_position;
        burn.has_last_position = true;
        burn.visual_velocity_x = 0.0f;
        burn.visual_velocity_y = 0.0f;
        burn.has_visual_velocity = false;
        if (!burn.lethal_freeze_logged && g_mina && env_bool("MINA_XMARK_BURN_FREEZE_ON_DEATH_LOG", true)) {
            burn.lethal_freeze_logged = true;
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn lethal movement freeze target=0x%p officialCore=0x%p nowMs=%llu pos=(%.3f, %.3f, %.3f)\n",
                reinterpret_cast<void *>(burn.target),
                reinterpret_cast<void *>(burn.official_combat_core),
                now_ms,
                static_cast<double>(burn.lethal_freeze_position.x),
                static_cast<double>(burn.lethal_freeze_position.y),
                static_cast<double>(burn.lethal_freeze_position.z));
            g_mina->Log(message);
        }
    }
    return froze;
}

Vec3 xmark_burn_visual_host_render_position(
    XMarkBurnEffect &burn,
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms) {
    Vec3 host_render_position = visual_host_render_position(host);
    const unsigned long long host_seen_ms = host.last_seen_ms ? host.last_seen_ms : now_ms;
    if (burn.last_visual_host_seen_ms && host_seen_ms > burn.last_visual_host_seen_ms) {
        const unsigned long long delta_ms = host_seen_ms - burn.last_visual_host_seen_ms;
        const float max_sample_jump =
            env_float("MINA_XMARK_BURN_VISUAL_FOLLOW_VELOCITY_SAMPLE_MAX_JUMP", 4.0f);
        const float dx = host_render_position.x - burn.last_visual_host_render_position.x;
        const float dy = host_render_position.y - burn.last_visual_host_render_position.y;
        if (delta_ms > 0 &&
            std::isfinite(dx) &&
            std::isfinite(dy) &&
            dx * dx + dy * dy <= max_sample_jump * max_sample_jump) {
            burn.visual_velocity_x = dx / static_cast<float>(delta_ms);
            burn.visual_velocity_y = dy / static_cast<float>(delta_ms);
            burn.has_visual_velocity = true;
        } else {
            burn.visual_velocity_x = 0.0f;
            burn.visual_velocity_y = 0.0f;
            burn.has_visual_velocity = false;
        }
        burn.last_visual_host_render_position = host_render_position;
        burn.last_visual_host_seen_ms = host_seen_ms;
    } else if (!burn.last_visual_host_seen_ms) {
        burn.last_visual_host_render_position = host_render_position;
        burn.last_visual_host_seen_ms = host_seen_ms;
        burn.visual_velocity_x = 0.0f;
        burn.visual_velocity_y = 0.0f;
        burn.has_visual_velocity = false;
    } else if (host_seen_ms == burn.last_visual_host_seen_ms &&
               burn.has_visual_velocity &&
               now_ms > host_seen_ms) {
        const unsigned int max_extrapolate_ms =
            std::max(16u, env_uint("MINA_XMARK_BURN_VISUAL_FOLLOW_EXTRAPOLATE_MAX_MS", 160));
        const unsigned long long extrapolate_ms =
            std::min<unsigned long long>(now_ms - host_seen_ms, max_extrapolate_ms);
        const float max_speed_per_ms =
            std::max(0.001f, env_float("MINA_XMARK_BURN_VISUAL_FOLLOW_MAX_SPEED_PER_MS", 999.0f));
        const float vx = std::max(-max_speed_per_ms, std::min(max_speed_per_ms, burn.visual_velocity_x));
        const float vy = std::max(-max_speed_per_ms, std::min(max_speed_per_ms, burn.visual_velocity_y));
        host_render_position.x += vx * static_cast<float>(extrapolate_ms);
        host_render_position.y += vy * static_cast<float>(extrapolate_ms);
    }
    return host_render_position;
}

bool refresh_xmark_burn_effect_position(XMarkBurnEffect &burn, unsigned long long now_ms, const Vec3 &player_api) {
    if (burn.official_follow) {
        if (burn.lethal_written_ms &&
            resolve_xmark_burn_direct_follow_position(
                burn,
                now_ms,
                false,
                nullptr)) {
            return true;
        }

        XMarkVisualEnemyHost visual_host{};
        bool visual_center_resolved = false;
        Vec3 visual_center{};
        if (env_bool("MINA_XMARK_BURN_OFFICIAL_FOLLOW_USE_VISUAL_CENTER", true) &&
            burn.visual_key &&
            visual_enemy_host_by_key(burn.visual_key, &visual_host, now_ms)) {
            const unsigned int visual_stale_ms =
                std::max(16u, env_uint("MINA_XMARK_BURN_OFFICIAL_FOLLOW_VISUAL_CENTER_STALE_MS", 96));
            if (!visual_host.last_seen_ms || now_ms <= visual_host.last_seen_ms + visual_stale_ms) {
                visual_center = xmark_burn_visual_host_render_position(burn, visual_host, now_ms);
                visual_center_resolved = true;
            }
        }

        XMarkOfficialEnemyHost host{};
        const bool host_resolved =
            official_enemy_host_for_burn(burn, &host) &&
            host.health > 0.0f &&
            host.health_max > 0.0f;
        float direct_health = 0.0f;
        const bool direct_core_alive =
            !host_resolved &&
            burn.official_combat_core &&
            combat_core_health_read(burn.official_combat_core, &direct_health) &&
            direct_health > 0.0f;
        const bool hold_full_duration =
            env_bool("MINA_XMARK_BURN_HOLD_FULL_DURATION", true) &&
            burn.has_last_position &&
            now_ms < burn.expires_ms;
        if (!host_resolved && !direct_core_alive && !hold_full_duration) {
            return false;
        }
        if (!visual_center_resolved && !host_resolved && !direct_core_alive && hold_full_duration) {
            burn.has_last_position = true;
            return true;
        }

        Vec3 target_position = visual_center_resolved
            ? visual_center
            : (host_resolved ? host.position : burn.last_position);
        if (visual_center_resolved && host_resolved) {
            burn.runtime_visual_offset.x = visual_center.x - host.position.x;
            burn.runtime_visual_offset.y = visual_center.y - host.position.y;
            burn.runtime_visual_offset.z = visual_center.z - host.position.z;
            burn.has_runtime_visual_offset = true;
        } else if (host_resolved && burn.has_runtime_visual_offset) {
            target_position.x += burn.runtime_visual_offset.x;
            target_position.y += burn.runtime_visual_offset.y;
            target_position.z += burn.runtime_visual_offset.z;
        }

        if (host_resolved) {
            burn.target = host.entity;
            burn.official_combat_core = host.combat_core;
        }
        commit_xmark_burn_follow_position(burn, target_position, now_ms);
        if (host_resolved) {
            burn.render_half_w = official_enemy_burn_render_half_from_health(host, xmark_default_render_half_w());
            burn.render_half_h = official_enemy_burn_render_half_from_health(host, xmark_default_render_half_h());
        }
        return true;
    }

    if (burn.visual_follow && burn.visual_key) {
        if (g_visual_enemy_state_explicit_zero && !xmark_visual_host_currently_present(burn.visual_key)) {
            const unsigned int explicit_zero_despawn_ms =
                env_uint("MINA_XMARK_BURN_VISUAL_EXPLICIT_ZERO_DESPAWN_MS", 120);
            const unsigned long long last_resolved_ms =
                burn.last_visual_resolved_ms ? burn.last_visual_resolved_ms : burn.started_ms;
            if (!last_resolved_ms || now_ms >= last_resolved_ms + explicit_zero_despawn_ms) {
                return false;
            }
        }

        XMarkVisualEnemyHost host{};
        bool resolved = visual_enemy_host_by_key(burn.visual_key, &host, now_ms);
        if (resolved && host.last_seen_ms) {
            const unsigned int stale_despawn_ms =
                std::max(32u, env_uint("MINA_XMARK_BURN_VISUAL_STALE_DESPAWN_MS", 260));
            if (now_ms > host.last_seen_ms + stale_despawn_ms) {
                resolved = false;
            }
        }
        if (resolved) {
            const Vec3 host_render_position = xmark_burn_visual_host_render_position(burn, host, now_ms);
            commit_xmark_burn_follow_position(burn, host_render_position, now_ms);
            xmark_visual_host_render_halves(host, &burn.render_half_w, &burn.render_half_h);

            if (burn.target && probable_heap_object(burn.target)) {
                Vec3 runtime_anchor{};
                bool runtime_resolved = runtime_object_anchor_at_offset(
                    burn.target,
                    burn.target_anchor_offset,
                    &runtime_anchor);
                if (!runtime_resolved) {
                    unsigned int refreshed_offset = 0;
                    runtime_resolved = runtime_object_anchor(
                        burn.target,
                        player_api,
                        &runtime_anchor,
                        &refreshed_offset,
                        0.0f,
                        192.0f);
                    if (runtime_resolved) {
                        burn.target_anchor_offset = refreshed_offset;
                    }
                }
                if (runtime_resolved) {
                    const Vec3 runtime_position = runtime_anchor_to_spawn_position(runtime_anchor);
                    burn.runtime_visual_offset.x = host_render_position.x - runtime_position.x;
                    burn.runtime_visual_offset.y = host_render_position.y - runtime_position.y;
                    burn.runtime_visual_offset.z = host_render_position.z - runtime_position.z;
                    burn.has_runtime_visual_offset = true;
                }
            }
            return true;
        }

        if (burn.has_last_position) {
            if (!burn.last_visual_missing_ms ||
                burn.last_visual_missing_ms <= burn.last_visual_resolved_ms) {
                burn.last_visual_missing_ms = now_ms;
            }
            ++burn.visual_missing_count;
            if (env_bool("MINA_XMARK_BURN_HOLD_FULL_DURATION", true) &&
                now_ms < burn.expires_ms) {
                return true;
            }
            const unsigned int missing_despawn_ms =
                env_uint("MINA_XMARK_BURN_VISUAL_MISSING_DESPAWN_MS", 160);
            return now_ms < burn.last_visual_missing_ms + missing_despawn_ms;
        }
    }

    if (!burn.target || !probable_heap_object(burn.target)) {
        return burn.has_last_position;
    }

    Vec3 anchor{};
    bool resolved = runtime_object_anchor_at_offset(burn.target, burn.target_anchor_offset, &anchor);
    if (!resolved && burn.has_last_position) {
        unsigned int refreshed_offset = 0;
        resolved = runtime_object_anchor_near_position(
            burn.target,
            player_api,
            burn.last_position,
            &anchor,
            &refreshed_offset);
        if (resolved) {
            burn.target_anchor_offset = refreshed_offset;
        }
    }
    if (!resolved) {
        unsigned int refreshed_offset = 0;
        resolved = runtime_object_anchor(burn.target, player_api, &anchor, &refreshed_offset);
        if (resolved) {
            burn.target_anchor_offset = refreshed_offset;
        }
    }
    if (!resolved) {
        return burn.has_last_position;
    }

    Vec3 target_position = runtime_anchor_to_spawn_position(anchor);
    if (burn.visual_follow && burn.has_runtime_visual_offset) {
        target_position.x += burn.runtime_visual_offset.x;
        target_position.y += burn.runtime_visual_offset.y;
        target_position.z += burn.runtime_visual_offset.z;
    }
    burn.last_position = xmark_attachment_mark_position(target_position);
    burn.has_last_position = true;
    return true;
}

bool apply_runtime_target_to_burn_effect(
    XMarkBurnEffect &burn,
    const XMarkRuntimeTarget &target,
    unsigned long long now_ms,
    const char *reason) {
    if (!target.entity) {
        return false;
    }
    const bool has_damage_route =
        (target.official_combat_core != 0) ||
        (target.official_follow && target.official_combat_core != 0) ||
        (target.health_like && target.health_offset != 0);
    if (!has_damage_route) {
        return false;
    }

    burn.target = target.entity;
    burn.target_source_base = target.source_base;
    burn.target_source_offset = target.source_offset;
    burn.target_anchor_offset = target.anchor_offset;
    burn.target_health_offset = target.health_offset;
    burn.target_health_kind = target.health_kind;
    burn.target_health_like = target.health_like;
    if (target.official_combat_core) {
        burn.official_combat_core = target.official_combat_core;
        burn.official_follow = true;
    } else if (target.official_follow) {
        burn.official_follow = true;
    }
    if (target.render_half_w > 0.0f) {
        burn.render_half_w = target.render_half_w;
    }
    if (target.render_half_h > 0.0f) {
        burn.render_half_h = target.render_half_h;
    }
    if (target.visual_key) {
        burn.visual_key = target.visual_key;
        burn.visual_follow = true;
    }
    if (target.visual_entry[0]) {
        copy_visual_token(burn.visual_entry, sizeof(burn.visual_entry), target.visual_entry);
    }
    if (target.visual_stem[0]) {
        copy_visual_token(burn.visual_stem, sizeof(burn.visual_stem), target.visual_stem);
    }
    if (target.visual_catalog[0]) {
        copy_visual_token(burn.visual_catalog, sizeof(burn.visual_catalog), target.visual_catalog);
    }
    if (g_mina && env_bool("MINA_XMARK_BURN_DAMAGE_PROMOTE_LOG", true)) {
        char message[576]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn damage target promoted reason=%s target=0x%p officialCore=0x%p official=%u health=0x%X/kind%u hp=%.3f/%.3f visualKey=0x%llX ageMs=%llu\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(burn.target),
            reinterpret_cast<void *>(burn.official_combat_core),
            burn.official_follow ? 1u : 0u,
            burn.target_health_offset,
            burn.target_health_kind,
            static_cast<double>(target.health_value),
            static_cast<double>(target.health_max),
            burn.visual_key,
            now_ms >= burn.started_ms ? now_ms - burn.started_ms : 0ull);
        g_mina->Log(message);
    }
    return true;
}

bool promote_xmark_burn_effect_to_damage_target(XMarkBurnEffect &burn, unsigned long long now_ms) {
    if (!burn.active) {
        return false;
    }
    if (burn.official_combat_core && xmark_combat_core_health_write_api_available()) {
        return true;
    }
    if (burn.target_health_like && burn.target && burn.target_health_offset) {
        return true;
    }

    if (g_last_world &&
        env_bool("MINA_XMARK_BURN_DAMAGE_FORCE_OFFICIAL_SCAN", true) &&
        xmark_combat_full_scan_fallback_allowed()) {
        const bool needs_scan =
            !g_official_enemy_snapshot_valid ||
            g_official_enemy_host_count == 0 ||
            !g_last_official_enemy_scan_ms ||
            now_ms >= g_last_official_enemy_scan_ms +
                env_uint("MINA_XMARK_BURN_DAMAGE_FORCE_OFFICIAL_SCAN_STALE_MS", 500);
        if (needs_scan) {
            g_last_official_enemy_scan_ms = 0;
            update_official_enemy_snapshot(g_last_world, now_ms);
        }
    }

    XMarkOfficialEnemyHost official_host{};
    if (official_enemy_host_for_burn(burn, &official_host) &&
        official_host.health > 0.0f &&
        official_host.health_max > 0.0f) {
        XMarkRuntimeTarget target{};
        if (runtime_target_from_official_enemy_host(official_host, &target)) {
            target.visual_key = burn.visual_key;
            target.visual_follow = burn.visual_follow;
            copy_visual_token(target.visual_entry, sizeof(target.visual_entry), burn.visual_entry);
            copy_visual_token(target.visual_stem, sizeof(target.visual_stem), burn.visual_stem);
            copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), burn.visual_catalog);
            return apply_runtime_target_to_burn_effect(burn, target, now_ms, "official-existing");
        }
    }

    Vec3 expected_position = burn.has_last_position
        ? xmark_attachment_target_position_from_mark(burn.last_position)
        : Vec3{0.0f, 0.0f, 0.0f};
    XMarkVisualEnemyHost visual_host{};
    if (burn.visual_key && visual_enemy_host_by_key(burn.visual_key, &visual_host, now_ms)) {
        expected_position = visual_host_render_position(visual_host);
    }

    if (official_enemy_host_near_position(
            expected_position,
            env_float("MINA_XMARK_BURN_DAMAGE_PROMOTE_OFFICIAL_RADIUS_X", 8.0f),
            env_float("MINA_XMARK_BURN_DAMAGE_PROMOTE_OFFICIAL_RADIUS_Y", 8.0f),
            &official_host) &&
        official_host.health > 0.0f &&
        official_host.health_max > 0.0f) {
        XMarkRuntimeTarget target{};
        if (runtime_target_from_official_enemy_host(official_host, &target)) {
            target.visual_key = burn.visual_key;
            target.visual_follow = burn.visual_follow;
            copy_visual_token(target.visual_entry, sizeof(target.visual_entry), burn.visual_entry);
            copy_visual_token(target.visual_stem, sizeof(target.visual_stem), burn.visual_stem);
            copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), burn.visual_catalog);
            return apply_runtime_target_to_burn_effect(burn, target, now_ms, "official-near-visual");
        }
    }

    const bool allow_runtime_damage_fallback =
        env_bool("MINA_XMARK_BURN_DAMAGE_RUNTIME_FALLBACK", false);

    if (allow_runtime_damage_fallback && visual_host.active) {
        XMarkRuntimeTarget target{};
        if (find_runtime_target_near_visual_host(visual_host, &target, now_ms) &&
            (target.official_combat_core || (target.health_like && target.health_offset))) {
            return apply_runtime_target_to_burn_effect(burn, target, now_ms, "visual-runtime-near");
        }
    }

    if (!allow_runtime_damage_fallback) {
        if (g_mina && env_bool("MINA_XMARK_BURN_DAMAGE_PROMOTE_MISS_LOG", true)) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn damage target promote missed official-only target=0x%p visualKey=0x%llX expected=(%.3f, %.3f, %.3f)\n",
                reinterpret_cast<void *>(burn.target),
                burn.visual_key,
                static_cast<double>(expected_position.x),
                static_cast<double>(expected_position.y),
                static_cast<double>(expected_position.z));
            g_mina->Log(message);
        }
        return false;
    }

    constexpr unsigned int kMaxTargets = 128;
    XMarkRuntimeTarget targets[kMaxTargets]{};
    unsigned int reads = 0;
    const unsigned int count = collect_runtime_targets(targets, kMaxTargets, &reads, nullptr, nullptr);
    const float max_distance = env_float("MINA_XMARK_BURN_DAMAGE_PROMOTE_RUNTIME_MAX_DISTANCE", 32.0f);
    const float max_distance_sq = max_distance * max_distance;
    bool found = false;
    XMarkRuntimeTarget best{};
    float best_score = FLT_MAX;
    unsigned int health_like_count = 0;
    for (unsigned int i = 0; i < count; ++i) {
        const XMarkRuntimeTarget &target = targets[i];
        if (!(target.health_like && target.health_offset)) {
            continue;
        }
        ++health_like_count;
        const float distance_sq = xy_distance_sq(target.position, expected_position);
        if (distance_sq > max_distance_sq) {
            continue;
        }
        if (!found || distance_sq < best_score) {
            found = true;
            best = target;
            best_score = distance_sq;
        }
    }
    if (found) {
        apply_visual_host_identity_to_runtime_target(visual_host, &best);
        return apply_runtime_target_to_burn_effect(burn, best, now_ms, "runtime-health-near-visual");
    }

    if (g_mina && env_bool("MINA_XMARK_BURN_DAMAGE_PROMOTE_MISS_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn damage target promote missed target=0x%p visualKey=0x%llX runtimeCandidates=%u healthLike=%u reads=%u expected=(%.3f, %.3f, %.3f)\n",
            reinterpret_cast<void *>(burn.target),
            burn.visual_key,
            count,
            health_like_count,
            reads,
            static_cast<double>(expected_position.x),
            static_cast<double>(expected_position.y),
            static_cast<double>(expected_position.z));
        g_mina->Log(message);
    }
    return false;
}

void finalize_muriel_xmark_consumption(uintptr_t target, uintptr_t combat_core) {
    for (XMarkAttachment &other : g_xmark_attachments) {
        const bool same_identity =
            (target && other.target == target) ||
            (combat_core && other.official_combat_core == combat_core);
        if (other.active && other.suppress_hud && same_identity) {
            other.active = false;
        }
    }
    clear_xmark_hud_mark(target);

    g_basic_attack_probe = XMarkBasicAttackProbe{};
    g_attack_smear_contact_pending = false;
    g_attack_smear_contact_pending_ms = 0;
    g_last_muriel_mark_attack_press_ms = g_attack_j_pressed_ms;
}

bool muriel_has_active_mark_status(
    uintptr_t target,
    uintptr_t combat_core,
    unsigned long long now_ms) {
    XMarkEnemyStatusRecord *status = xmark_enemy_status_find(target, combat_core);
    return status && status->active && status->training_target &&
        status->phase == XMarkEnemyStatusPhase::Marked &&
        now_ms < status->state_expires_ms;
}

bool consume_xmark_attachment_for_burn(XMarkAttachment &attachment, unsigned long long now_ms, const char *reason) {
    now_ms = xmark_status_now_ms();
    if (!attachment.active || !attachment.has_last_position) {
        return false;
    }
    const uintptr_t previous_hud_target = attachment.suppress_hud
        ? 0
        : (attachment.target ? attachment.target : static_cast<uintptr_t>(attachment.visual_key));
    promote_xmark_attachment_to_official_enemy(attachment, now_ms);
    XMarkOfficialEnemyHost attached_host{};
    const bool attachment_is_muriel =
        env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) &&
        (attachment.suppress_hud ||
         (official_enemy_host_for_attachment(attachment, &attached_host) &&
          xmark_official_host_is_muriel(attached_host)));
    if (attachment_is_muriel) {
        attachment.suppress_hud = true;
        attachment.target_health_like = false;
        clear_xmark_hud_mark(attachment.target);
    }
    if ((!attachment.official_follow || !attachment.official_combat_core) &&
        attachment.has_last_position) {
        if (g_last_world && xmark_combat_full_scan_fallback_allowed()) {
            const bool needs_scan =
                !g_official_enemy_snapshot_valid ||
                g_official_enemy_host_count == 0 ||
                !g_last_official_enemy_scan_ms ||
                now_ms >= g_last_official_enemy_scan_ms +
                    env_uint("MINA_XMARK_BURN_CONSUME_OFFICIAL_SCAN_STALE_MS", 250);
            if (needs_scan) {
                g_last_official_enemy_scan_ms = 0;
                update_official_enemy_snapshot(g_last_world, now_ms);
            }
        }
        const Vec3 consumed_mark_target =
            xmark_attachment_target_position_from_mark(attachment.last_position);
        XMarkOfficialEnemyHost consumed_host{};
        if (official_enemy_host_near_position(
                consumed_mark_target,
                env_float("MINA_XMARK_BURN_CONSUME_BIND_RADIUS_X", 2.5f),
                env_float("MINA_XMARK_BURN_CONSUME_BIND_RADIUS_Y", 2.5f),
                &consumed_host) &&
            consumed_host.entity &&
            consumed_host.combat_core &&
            consumed_host.health > 0.0f &&
            consumed_host.health_max > 0.0f) {
            attachment.target = consumed_host.entity;
            attachment.official_combat_core = consumed_host.combat_core;
            attachment.official_follow = true;
            attachment.render_half_w = official_enemy_render_half_from_health(
                consumed_host,
                xmark_default_render_half_w());
            attachment.render_half_h = official_enemy_render_half_from_health(
                consumed_host,
                xmark_default_render_half_h());
        }
    }
    if (!attachment_is_muriel &&
        env_bool("MINA_XMARK_BURN_CONSUME_ACQUIRE_VISUAL_KEY", true) &&
        (!attachment.visual_key || !attachment.visual_follow)) {
        Vec3 visual_link_position = attachment.has_last_position
            ? xmark_attachment_target_position_from_mark(attachment.last_position)
            : Vec3{0.0f, 0.0f, 0.0f};
        XMarkOfficialEnemyHost official_host{};
        if (official_enemy_host_for_attachment(attachment, &official_host) &&
            official_host.health > 0.0f &&
            official_host.health_max > 0.0f) {
            visual_link_position = official_host.position;
        } else if (attachment.target && probable_heap_object(attachment.target)) {
            Vec3 direct_position{};
            if (official_entity_world_position_read(attachment.target, &direct_position)) {
                visual_link_position = direct_position;
            }
        }
        apply_nearest_visual_host_identity_to_attachment(
            attachment,
            visual_link_position,
            now_ms,
            "burn-consume",
            true);
    }
    XMarkBurnEffect *slot =
        find_xmark_burn_effect(attachment.target, attachment.visual_key, attachment.official_combat_core);
    const bool suppress_damage =
        env_bool("MINA_XMARK_MURIEL_BURN_DAMAGE_SUPPRESSED", true) &&
        attachment_is_muriel;
    if (slot && slot->active) {
        slot->suppress_damage = slot->suppress_damage || suppress_damage;
        const uintptr_t burn_hud_target = slot->suppress_damage
            ? 0
            : (slot->target ? slot->target : static_cast<uintptr_t>(slot->visual_key));
        if (previous_hud_target && previous_hud_target != burn_hud_target) {
            shorten_xmark_hud_mark(previous_hud_target, now_ms);
        }
        if (burn_hud_target) {
            upsert_xmark_hud_mark_mode(
                burn_hud_target,
                slot->expires_ms,
                kXMarkHudModeBurn);
        }
        XMarkEnemyStatusRecord *status = xmark_enemy_status_bind(
            slot->target,
            slot->official_combat_core,
            XMarkEnemyStatusPhase::Burning,
            now_ms,
            slot->expires_ms);
        slot->status_id = xmark_enemy_status_id(status);
        if (status) {
            status->training_target = attachment_is_muriel;
            status->attachment_index = 0;
            status->burn_index =
                static_cast<unsigned int>(slot - g_xmark_burn_effects) + 1u;
        }
        attachment.active = false;
        if (attachment_is_muriel) {
            finalize_muriel_xmark_consumption(slot->target, slot->official_combat_core);
            apply_muriel_regular_dialogue_state(true);
        }
        publish_current_xmark_hud_state(now_ms);
        if (g_mina && env_bool("MINA_XMARK_BURN_LOG", true)) {
            char message[320]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn consume kept existing burn reason=%s target=0x%p officialCore=0x%p expiresInMs=%llu\n",
                reason ? reason : "<none>",
                reinterpret_cast<void *>(slot->target),
                reinterpret_cast<void *>(slot->official_combat_core),
                now_ms < slot->expires_ms ? slot->expires_ms - now_ms : 0ull);
            g_mina->Log(message);
        }
        return true;
    }
    if (!slot) {
        slot = alloc_xmark_burn_effect_slot();
    }
    if (!slot) {
        return false;
    }

    const bool tick_until_death = env_bool("MINA_XMARK_BURN_TICK_UNTIL_DEATH", false);
    const unsigned int duration_ms = std::max(
        1u,
        attachment_is_muriel
            ? env_uint("MINA_XMARK_MURIEL_BURN_DURATION_MS", 5000)
            : (tick_until_death
                ? env_uint("MINA_XMARK_BURN_TICK_UNTIL_DEATH_MAX_MS", 12000)
                : env_uint("MINA_XMARK_BURN_DURATION_MS", 3000)));
    const unsigned int tick_ms = std::max(1u, env_uint("MINA_XMARK_BURN_TICK_MS", 500));
    release_xmark_burn_palette(*slot, true);
    *slot = XMarkBurnEffect{};
    slot->active = true;
    slot->target = attachment.target;
    slot->visual_key = attachment.visual_key;
    slot->target_source_base = attachment.target_source_base;
    slot->target_source_offset = attachment.target_source_offset;
    slot->target_anchor_offset = attachment.target_anchor_offset;
    slot->target_health_offset = attachment.target_health_offset;
    slot->target_health_kind = attachment.target_health_kind;
    slot->target_health_like = attachment.target_health_like;
    slot->suppress_damage = suppress_damage;
    slot->official_follow = attachment.official_follow;
    slot->official_combat_core = attachment.official_combat_core;
    slot->started_ms = now_ms;
    slot->expires_ms = now_ms + duration_ms;
    slot->palette_apply_after_ms = now_ms +
        std::max(
            1u,
            attachment_is_muriel
                ? env_uint("MINA_XMARK_MURIEL_BURN_PALETTE_APPLY_DELAY_MS", 50)
                : env_uint("MINA_XMARK_BURN_PALETTE_APPLY_DELAY_MS", 400));
    slot->next_tick_ms = now_ms + env_uint("MINA_XMARK_BURN_FIRST_TICK_DELAY_MS", 0);
    slot->last_position = attachment.last_position;
    slot->last_visual_host_render_position = attachment.last_visual_host_render_position;
    slot->last_visual_host_seen_ms = attachment.last_visual_host_seen_ms;
    slot->last_visual_resolved_ms = attachment.last_visual_resolved_ms;
    slot->last_visual_missing_ms = attachment.last_visual_missing_ms;
    slot->visual_missing_count = attachment.visual_missing_count;
    slot->visual_velocity_x = attachment.visual_velocity_x;
    slot->visual_velocity_y = attachment.visual_velocity_y;
    slot->render_half_w = attachment.render_half_w;
    slot->render_half_h = attachment.render_half_h;
    slot->has_last_position = attachment.has_last_position;
    slot->has_visual_velocity = attachment.has_visual_velocity;
    slot->has_runtime_visual_offset = attachment.has_runtime_visual_offset;
    slot->visual_follow = attachment.visual_follow;
    slot->runtime_visual_offset = attachment.runtime_visual_offset;
    copy_visual_token(slot->visual_entry, sizeof(slot->visual_entry), attachment.visual_entry);
    copy_visual_token(slot->visual_stem, sizeof(slot->visual_stem), attachment.visual_stem);
    copy_visual_token(slot->visual_catalog, sizeof(slot->visual_catalog), attachment.visual_catalog);
    XMarkEnemyStatusRecord *status = xmark_enemy_status_bind(
        slot->target,
        slot->official_combat_core,
        XMarkEnemyStatusPhase::Burning,
        now_ms,
        slot->expires_ms);
    slot->status_id = xmark_enemy_status_id(status);
    if (status) {
        status->training_target = attachment_is_muriel;
        status->attachment_index = 0;
        status->burn_index =
            static_cast<unsigned int>(slot - g_xmark_burn_effects) + 1u;
        transfer_xmark_marked_palette_to_burn(*status, *slot);
    }
    if (!slot->suppress_damage) {
        promote_xmark_burn_effect_to_damage_target(*slot, now_ms);
    }
    XMarkOfficialEnemyHost burn_host{};
    if (official_enemy_host_for_burn(*slot, &burn_host) && burn_host.health_max > 0.0f) {
        slot->render_half_w = official_enemy_burn_render_half_from_health(burn_host, xmark_default_render_half_w());
        slot->render_half_h = official_enemy_burn_render_half_from_health(burn_host, xmark_default_render_half_h());
    }

    const uintptr_t hud_target = slot->suppress_damage
        ? 0
        : (slot->target ? slot->target : static_cast<uintptr_t>(slot->visual_key));
    if (previous_hud_target && previous_hud_target != hud_target) {
        shorten_xmark_hud_mark(previous_hud_target, now_ms);
    }
    if (hud_target) {
        upsert_xmark_hud_mark_mode(hud_target, slot->expires_ms, kXMarkHudModeBurn);
        if (slot->official_combat_core && g_mina && g_mina->CombatCoreGetHealth && g_mina->CombatCoreGetHealthMax) {
            float health = 0.0f;
            float health_max = 0.0f;
            __try {
                health = g_mina->CombatCoreGetHealth(reinterpret_cast<ycComponent *>(slot->official_combat_core));
                health_max = g_mina->CombatCoreGetHealthMax(reinterpret_cast<ycComponent *>(slot->official_combat_core));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                health = 0.0f;
                health_max = 0.0f;
            }
            record_xmark_hud_mark_health(hud_target, health, health_max);
        }
    }
    attachment.active = false;
    if (attachment_is_muriel) {
        finalize_muriel_xmark_consumption(slot->target, slot->official_combat_core);
        apply_muriel_regular_dialogue_state(true);
    }
    publish_current_xmark_hud_state(now_ms);
    play_xmark_burn_ignite_sfx(now_ms);

    if (g_mina && env_bool("MINA_XMARK_BURN_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn consume mark reason=%s target=0x%p officialCore=0x%p official=%u visualKey=0x%llX health=0x%X/kind%u durationMs=%u tickMs=%u pos=(%.3f, %.3f, %.3f)\n",
            reason ? reason : "<none>",
            reinterpret_cast<void *>(slot->target),
            reinterpret_cast<void *>(slot->official_combat_core),
            slot->official_follow ? 1u : 0u,
            slot->visual_key,
            slot->target_health_offset,
            slot->target_health_kind,
            duration_ms,
            tick_ms,
            static_cast<double>(slot->last_position.x),
            static_cast<double>(slot->last_position.y),
            static_cast<double>(slot->last_position.z));
        g_mina->Log(message);
    }
    return true;
}

bool xmark_attachment_current_health_for_charged_consume(
    const XMarkAttachment &attachment,
    float *health_out) {
    if (health_out) {
        *health_out = 0.0f;
    }
    if (attachment.official_combat_core &&
        combat_core_health_read(attachment.official_combat_core, health_out)) {
        return true;
    }
    if (attachment.target_health_like &&
        attachment.target &&
        attachment.target_health_offset &&
        read_health_at_offset(
            attachment.target,
            attachment.target_health_offset,
            attachment.target_health_kind,
            health_out,
            nullptr)) {
        return true;
    }
    return false;
}

bool xmark_charged_consume_probe_matches_attachment(
    const XMarkChargedConsumeProbe &probe,
    const XMarkAttachment &attachment) {
    if (!probe.active || !attachment.active) {
        return false;
    }
    if (probe.official_combat_core &&
        attachment.official_combat_core &&
        probe.official_combat_core == attachment.official_combat_core) {
        return true;
    }
    if (probe.target && attachment.target && probe.target == attachment.target) {
        return true;
    }
    return probe.visual_key && attachment.visual_key && probe.visual_key == attachment.visual_key;
}

XMarkAttachment *xmark_attachment_for_charged_consume_probe(
    const XMarkChargedConsumeProbe &probe) {
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || !attachment.has_last_position) {
            continue;
        }
        if (find_xmark_burn_effect(
                attachment.target,
                attachment.visual_key,
                attachment.official_combat_core)) {
            continue;
        }
        if (xmark_charged_consume_probe_matches_attachment(probe, attachment)) {
            return &attachment;
        }
    }
    return nullptr;
}

XMarkChargedConsumeProbe *xmark_charged_consume_probe_slot_for_attachment(
    const XMarkAttachment &attachment) {
    XMarkChargedConsumeProbe *free_slot = nullptr;
    for (XMarkChargedConsumeProbe &probe : g_charged_consume_probes) {
        if (xmark_charged_consume_probe_matches_attachment(probe, attachment)) {
            return &probe;
        }
        if (!probe.active && !free_slot) {
            free_slot = &probe;
        }
    }
    return free_slot ? free_slot : &g_charged_consume_probes[0];
}

unsigned int consume_or_arm_xmark_charged_health_probe(
    XMarkAttachment &attachment,
    unsigned long long now_ms,
    const Vec3 &impact_position,
    int direction,
    const char *reason) {
    const bool strict_charged_frame_contact =
        reason && std::strcmp(reason, "charged-slam-frame") == 0;
    XMarkOfficialEnemyHost contact_host{};
    if (strict_charged_frame_contact &&
        env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) &&
        official_enemy_host_for_attachment(attachment, &contact_host) &&
        xmark_official_host_is_muriel(contact_host)) {
        const bool active_mark = muriel_has_active_mark_status(
            attachment.target,
            attachment.official_combat_core,
            now_ms);
        const bool frame_overlap = muriel_drawn_charged_frame_overlaps_host(
            g_last_muriel_charged_frame_state,
            contact_host,
            now_ms);
        if (!active_mark || !frame_overlap) {
            if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn Muriel charged consume rejected activeMark=%u frameOverlap=%u frame=%s geometry=%u contact=%u point=(%.3f, %.3f) host=(%.3f, %.3f) ageMs=%llu.\n",
                    active_mark ? 1u : 0u,
                    frame_overlap ? 1u : 0u,
                    g_last_muriel_charged_frame_state.frame[0]
                        ? g_last_muriel_charged_frame_state.frame
                        : "<none>",
                    g_last_muriel_charged_frame_state.has_geometry ? 1u : 0u,
                    g_last_muriel_charged_frame_state.has_contact ? 1u : 0u,
                    static_cast<double>(g_last_muriel_charged_frame_state.contact_x),
                    static_cast<double>(g_last_muriel_charged_frame_state.contact_y),
                    static_cast<double>(contact_host.position.x),
                    static_cast<double>(contact_host.position.y),
                    g_last_muriel_charged_frame_state.tick &&
                            now_ms >= g_last_muriel_charged_frame_state.tick
                        ? now_ms - g_last_muriel_charged_frame_state.tick
                        : 0ull);
                g_mina->Log(message);
            }
            return 0u;
        }
        return consume_xmark_attachment_for_burn(
                   attachment,
                   now_ms,
                   "muriel-charged-contact")
            ? 1u
            : 0u;
    }
    if (!env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_REQUIRE_HEALTH_DROP", true)) {
        return consume_xmark_attachment_for_burn(attachment, now_ms, reason) ? 1u : 0u;
    }
    if (strict_charged_frame_contact &&
        env_bool("MINA_XMARK_BURN_CHARGED_STRICT_FRAME_CONTACT_CONSUMES", true)) {
        return consume_xmark_attachment_for_burn(
                   attachment,
                   now_ms,
                   "charged-slam-strict-frame-contact")
            ? 1u
            : 0u;
    }

    float current_health = 0.0f;
    if (!xmark_attachment_current_health_for_charged_consume(attachment, &current_health)) {
        if (env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_ALLOW_NO_HEALTH_READ", false)) {
            return consume_xmark_attachment_for_burn(attachment, now_ms, reason) ? 1u : 0u;
        }
        if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn charged consume armed rejected: no health read target=0x%p core=0x%p visualKey=0x%llX reason=%s\n",
                reinterpret_cast<void *>(attachment.target),
                reinterpret_cast<void *>(attachment.official_combat_core),
                attachment.visual_key,
                reason ? reason : "<none>");
            g_mina->Log(message);
        }
        return 0u;
    }
    if (!(current_health > 0.0f)) {
        return 0u;
    }

    const float min_drop =
        std::max(0.0f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_DROP_MIN", 0.01f));
    const unsigned int recent_drop_window_ms = std::max(
        16u,
        env_uint("MINA_XMARK_BURN_CHARGED_CONSUME_RECENT_DROP_MS", 400));
    const bool dropped_since_last_observation =
        attachment.has_observed_health &&
        current_health + min_drop <= attachment.observed_health;
    const bool has_recent_observed_drop =
        attachment.recent_health_drop_ms &&
        now_ms >= attachment.recent_health_drop_ms &&
        now_ms - attachment.recent_health_drop_ms <= recent_drop_window_ms &&
        current_health <= attachment.health_after_recent_drop + min_drop;
    const bool allow_preimpact_recent_drop =
        env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_ALLOW_PREIMPACT_RECENT_DROP", false);
    const bool recent_drop_during_current_charge =
        strict_charged_frame_contact &&
        env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_ALLOW_CURRENT_CHARGE_DROP", true) &&
        g_attack_j_pressed_ms &&
        attachment.recent_health_drop_ms >= g_attack_j_pressed_ms &&
        now_ms >= attachment.recent_health_drop_ms &&
        now_ms - attachment.recent_health_drop_ms <= recent_drop_window_ms;
    if (dropped_since_last_observation ||
        (has_recent_observed_drop &&
         (allow_preimpact_recent_drop || recent_drop_during_current_charge))) {
        xmark_attachment_observe_health(attachment, current_health, now_ms);
        return consume_xmark_attachment_for_burn(
                   attachment,
                   now_ms,
                   "charged-slam-recent-health-drop")
            ? 1u
            : 0u;
    }
    xmark_attachment_observe_health(attachment, current_health, now_ms);
    const unsigned int probe_window_ms =
        std::max(16u, env_uint("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_PROBE_WINDOW_MS", 260));
    XMarkChargedConsumeProbe *slot = xmark_charged_consume_probe_slot_for_attachment(attachment);
    if (!slot) {
        return 0u;
    }
    const bool continuing_probe =
        slot->active &&
        xmark_charged_consume_probe_matches_attachment(*slot, attachment);
    if (continuing_probe) {
        if (current_health + min_drop <= slot->baseline_health) {
            slot->active = false;
            return consume_xmark_attachment_for_burn(
                       attachment,
                       now_ms,
                       "charged-slam-health-drop")
                ? 1u
                : 0u;
        }

        const unsigned int max_total_window_ms = std::max(
            probe_window_ms,
            env_uint("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_PROBE_MAX_TOTAL_MS", 1200));
        const unsigned long long maximum_expiry = slot->started_ms + max_total_window_ms;
        const unsigned long long requested_expiry = now_ms + probe_window_ms;
        slot->expires_ms = std::min(
            maximum_expiry,
            std::max(slot->expires_ms, requested_expiry));
        slot->impact_position = impact_position;
        slot->direction = direction;
        if (env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_HOLD_MARK_WHILE_PROBING", true)) {
            attachment.expires_ms = std::max(attachment.expires_ms, slot->expires_ms);
            if (!attachment.suppress_hud) {
                upsert_xmark_hud_mark(attachment.target, attachment.expires_ms);
            }
        }
        return 0u;
    }

    *slot = XMarkChargedConsumeProbe{};
    slot->active = true;
    slot->target = attachment.target;
    slot->official_combat_core = attachment.official_combat_core;
    slot->visual_key = attachment.visual_key;
    slot->started_ms = now_ms;
    slot->expires_ms = now_ms + probe_window_ms;
    slot->baseline_health = current_health;
    slot->impact_position = impact_position;
    slot->direction = direction;
    std::snprintf(slot->reason, sizeof(slot->reason), "%s", reason ? reason : "charged-slam-frame");
    if (env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_HOLD_MARK_WHILE_PROBING", true)) {
        attachment.expires_ms = std::max(attachment.expires_ms, slot->expires_ms);
        if (!attachment.suppress_hud) {
            upsert_xmark_hud_mark(attachment.target, attachment.expires_ms);
        }
    }

    if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
        char message[448]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn charged consume health probe armed target=0x%p core=0x%p visualKey=0x%llX hp=%.3f windowMs=%u reason=%s dir=%s impact=(%.3f, %.3f)\n",
            reinterpret_cast<void *>(slot->target),
            reinterpret_cast<void *>(slot->official_combat_core),
            slot->visual_key,
            static_cast<double>(slot->baseline_health),
            probe_window_ms,
            slot->reason,
            direction_name(direction),
            static_cast<double>(impact_position.x),
            static_cast<double>(impact_position.y));
        g_mina->Log(message);
    }
    return 0u;
}

void update_xmark_charged_consume_health_probes(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_REQUIRE_HEALTH_DROP", true)) {
        return;
    }
    const float min_drop =
        std::max(0.0f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_DROP_MIN", 0.01f));
    for (XMarkChargedConsumeProbe &probe : g_charged_consume_probes) {
        if (!probe.active) {
            continue;
        }
        if (now_ms >= probe.expires_ms) {
            if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn charged consume health probe expired target=0x%p core=0x%p visualKey=0x%llX baseline=%.3f reason=%s\n",
                    reinterpret_cast<void *>(probe.target),
                    reinterpret_cast<void *>(probe.official_combat_core),
                    probe.visual_key,
                    static_cast<double>(probe.baseline_health),
                    probe.reason[0] ? probe.reason : "-");
                g_mina->Log(message);
            }
            probe.active = false;
            continue;
        }
        XMarkAttachment *attachment = xmark_attachment_for_charged_consume_probe(probe);
        if (!attachment) {
            probe.active = false;
            continue;
        }
        float current_health = 0.0f;
        if (!xmark_attachment_current_health_for_charged_consume(*attachment, &current_health)) {
            continue;
        }
        if (current_health + min_drop <= probe.baseline_health) {
            probe.active = false;
            if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn charged consume health probe confirmed target=0x%p core=0x%p hp=%.3f->%.3f reason=%s\n",
                    reinterpret_cast<void *>(attachment->target),
                    reinterpret_cast<void *>(attachment->official_combat_core),
                    static_cast<double>(probe.baseline_health),
                    static_cast<double>(current_health),
                    probe.reason[0] ? probe.reason : "-");
                g_mina->Log(message);
            }
            consume_xmark_attachment_for_burn(*attachment, now_ms, "charged-slam-health-drop");
        }
    }
}

unsigned int consume_first_xmark_attachment_for_burn(unsigned long long now_ms, const char *reason) {
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || !attachment.has_last_position) {
            continue;
        }
        return consume_xmark_attachment_for_burn(attachment, now_ms, reason) ? 1u : 0u;
    }
    return 0u;
}

bool xmark_attachment_contact_position_for_burn_consume(
    XMarkAttachment &attachment,
    unsigned long long now_ms,
    Vec3 *position_out,
    bool *official_out) {
    if (position_out) {
        *position_out = attachment.has_last_position
            ? xmark_attachment_target_position_from_mark(attachment.last_position)
            : Vec3{0.0f, 0.0f, 0.0f};
    }
    if (official_out) {
        *official_out = false;
    }
    if (!attachment.active || !attachment.has_last_position) {
        return false;
    }

    if (attachment.suppress_hud) {
        if (position_out) {
            *position_out = xmark_attachment_target_position_from_mark(attachment.last_position);
        }
        if (official_out) {
            *official_out = true;
        }
        return true;
    }

    promote_xmark_attachment_to_official_enemy(attachment, now_ms);
    if (attachment.official_follow) {
        XMarkOfficialEnemyHost host{};
        const bool host_resolved = official_enemy_host_for_attachment(attachment, &host) &&
            host.health > 0.0f &&
            host.health_max > 0.0f;
        const uintptr_t follow_entity =
            host_resolved && host.entity ? host.entity : attachment.target;
        const uintptr_t follow_core =
            host_resolved && host.combat_core ? host.combat_core : attachment.official_combat_core;
        float direct_health = 0.0f;
        const bool direct_health_ok =
            follow_core &&
            combat_core_health_read(follow_core, &direct_health);
        if (direct_health_ok && direct_health <= 0.0f) {
            return false;
        }

        Vec3 direct_position{};
        const bool direct_position_ok =
            direct_health_ok &&
            direct_health > 0.0f &&
            official_entity_world_position_read(follow_entity, &direct_position);
        if (direct_position_ok || host_resolved) {
            const Vec3 target_position = direct_position_ok ? direct_position : host.position;
            attachment.target = follow_entity;
            attachment.official_combat_core = follow_core;
            attachment.target_health_like = true;
            attachment.last_position = xmark_attachment_mark_position(target_position);
            attachment.has_last_position = true;
            attachment.last_visual_resolved_ms = now_ms;
            attachment.last_visual_missing_ms = 0;
            attachment.visual_missing_count = 0;
            if (host_resolved) {
                attachment.render_half_w = official_enemy_render_half_from_health(host, xmark_default_render_half_w());
                attachment.render_half_h = official_enemy_render_half_from_health(host, xmark_default_render_half_h());
            }
            if (position_out) {
                *position_out = target_position;
            }
            if (official_out) {
                *official_out = true;
            }
            return true;
        }
    }

    if (attachment.visual_follow && attachment.visual_key) {
        XMarkVisualEnemyHost visual_host{};
        if (visual_enemy_host_by_key(attachment.visual_key, &visual_host, now_ms)) {
            const Vec3 target_position = visual_host_render_position(visual_host);
            attachment.last_position = xmark_attachment_mark_position(target_position);
            attachment.has_last_position = true;
            xmark_visual_host_render_halves(visual_host, &attachment.render_half_w, &attachment.render_half_h);
            if (position_out) {
                *position_out = target_position;
            }
            return true;
        }
    }

    return attachment.has_last_position;
}

void expand_charged_consume_radius_for_attachment(
    XMarkAttachment &attachment,
    unsigned long long now_ms,
    float *radius_x,
    float *radius_y,
    float *fallback_radius_x,
    float *fallback_radius_y) {
    if (!attachment.active) {
        return;
    }

    if (env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_SCALE_RADIUS_BY_CONTACT_BOUNDS", true)) {
        const float contact_scale = std::max(
            0.0f,
            env_float("MINA_XMARK_BURN_CHARGED_CONSUME_CONTACT_BOUNDS_SCALE", 1.0f));
        const float contact_extra_x = std::min(
            std::max(0.0f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_CONTACT_BOUNDS_MAX_X", 8.0f)),
            std::max(0.0f, attachment.contact_half_w) * contact_scale);
        const float contact_extra_y = std::min(
            std::max(0.0f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_CONTACT_BOUNDS_MAX_Y", 8.0f)),
            std::max(0.0f, attachment.contact_half_h) * contact_scale);
        if (radius_x) {
            *radius_x += contact_extra_x;
        }
        if (radius_y) {
            *radius_y += contact_extra_y;
        }
        if (fallback_radius_x) {
            *fallback_radius_x += contact_extra_x;
        }
        if (fallback_radius_y) {
            *fallback_radius_y += contact_extra_y;
        }
    }

    if (!env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_SCALE_RADIUS_BY_HEALTH_MAX", true)) {
        return;
    }

    promote_xmark_attachment_to_official_enemy(attachment, now_ms);
    XMarkOfficialEnemyHost host{};
    if (!official_enemy_host_for_attachment(attachment, &host) ||
        !(host.health_max > 0.0f)) {
        return;
    }

    const float threshold =
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_SCALE_THRESHOLD", 24.0f);
    const float health_over_threshold = host.health_max - threshold;
    if (!(health_over_threshold > 0.0f)) {
        return;
    }

    const float extra_x = std::min(
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_SCALE_MAX_X", 16.0f),
        health_over_threshold *
            env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_SCALE_X", 0.20f));
    const float extra_y = std::min(
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_SCALE_MAX_Y", 12.0f),
        health_over_threshold *
            env_float("MINA_XMARK_BURN_CHARGED_CONSUME_HEALTH_SCALE_Y", 0.16f));

    if (radius_x) {
        *radius_x += std::max(0.0f, extra_x);
    }
    if (radius_y) {
        *radius_y += std::max(0.0f, extra_y);
    }
    if (fallback_radius_x) {
        *fallback_radius_x += std::max(0.0f, extra_x);
    }
    if (fallback_radius_y) {
        *fallback_radius_y += std::max(0.0f, extra_y);
    }
}

unsigned int consume_xmark_attachments_near_position_for_burn(
    unsigned long long now_ms,
    const Vec3 &impact_position,
    int direction,
    const char *reason) {
    const float radius_x = std::max(0.1f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_RADIUS_X", 4.0f));
    const float radius_y = std::max(0.1f, env_float("MINA_XMARK_BURN_CHARGED_CONSUME_RADIUS_Y", 3.5f));
    const float fallback_radius_x = std::max(
        radius_x,
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_FALLBACK_RADIUS_X", 9.0f));
    const float fallback_radius_y = std::max(
        radius_y,
        env_float("MINA_XMARK_BURN_CHARGED_CONSUME_FALLBACK_RADIUS_Y", 7.0f));
    const unsigned int max_consumed = std::max(1u, env_uint("MINA_XMARK_BURN_CHARGED_CONSUME_MAX", 8));
    const bool require_contact = env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_REQUIRE_CONTACT", true);
    const bool fallback_nearest =
        require_contact &&
        env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_FALLBACK_NEAREST", false);
    const bool arm_fallback_band =
        require_contact &&
        env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_ARM_FALLBACK_BAND", false);
    unsigned int consumed = 0;
    XMarkAttachment *nearest_fallback = nullptr;
    Vec3 nearest_position{};
    float nearest_dx = 0.0f;
    float nearest_dy = 0.0f;
    bool nearest_official = false;
    float nearest_score = FLT_MAX;
    unsigned int active_candidates = 0;
    float max_effective_radius_x = radius_x;
    float max_effective_radius_y = radius_y;
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || !attachment.has_last_position) {
            continue;
        }
        if (find_xmark_burn_effect(attachment.target, attachment.visual_key, attachment.official_combat_core)) {
            continue;
        }
        const bool muriel_contact_target =
            reason && std::strcmp(reason, "charged-slam-frame") == 0 &&
            env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true) &&
            attachment.suppress_hud &&
            now_ms < attachment.expires_ms;
        if (muriel_contact_target) {
            XMarkOfficialEnemyHost muriel_host{};
            const bool active_mark = muriel_has_active_mark_status(
                attachment.target,
                attachment.official_combat_core,
                now_ms);
            const bool resolved_muriel =
                official_enemy_host_for_attachment(attachment, &muriel_host) &&
                xmark_official_host_is_muriel(muriel_host);
            const bool frame_overlap = resolved_muriel &&
                muriel_drawn_charged_frame_overlaps_host(
                    g_last_muriel_charged_frame_state,
                    muriel_host,
                    now_ms);
            if (!active_mark || !resolved_muriel || !frame_overlap) {
                if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
                    char message[448]{};
                    std::snprintf(
                        message,
                        sizeof(message),
                        "XMarkBurn Muriel attachment consume rejected activeMark=%u resolved=%u frameOverlap=%u point=(%.3f, %.3f) host=(%.3f, %.3f).\n",
                        active_mark ? 1u : 0u,
                        resolved_muriel ? 1u : 0u,
                        frame_overlap ? 1u : 0u,
                        static_cast<double>(g_last_muriel_charged_frame_state.contact_x),
                        static_cast<double>(g_last_muriel_charged_frame_state.contact_y),
                        static_cast<double>(muriel_host.position.x),
                        static_cast<double>(muriel_host.position.y));
                    g_mina->Log(message);
                }
                continue;
            }
            Vec3 contact_position{};
            bool official_contact = false;
            if (!xmark_attachment_contact_position_for_burn_consume(
                    attachment,
                    now_ms,
                    &contact_position,
                    &official_contact)) {
                continue;
            }
            ++active_candidates;
            if (consume_xmark_attachment_for_burn(
                    attachment,
                    now_ms,
                    "muriel-charged-contact")) {
                ++consumed;
            }
            if (consumed >= max_consumed) {
                break;
            }
            continue;
        }
        const bool exact_core_health_authority =
            reason &&
            std::strcmp(reason, "charged-slam-frame") == 0 &&
            attachment.official_combat_core &&
            env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_REQUIRE_HEALTH_DROP", true) &&
            env_bool("MINA_XMARK_BURN_CHARGED_EXACT_CORE_HEALTH_AUTHORITY", false) &&
            !(g_mina &&
              g_mina->GetCurrentGameState() == GAMESTATE_CHECKPOINTROOM &&
              g_mina->GetRoomIndex() == 2u);
        if (exact_core_health_authority) {
            ++active_candidates;
            consumed += consume_or_arm_xmark_charged_health_probe(
                attachment,
                now_ms,
                impact_position,
                direction,
                reason);
            if (consumed >= max_consumed) {
                break;
            }
            continue;
        }
        Vec3 contact_position{};
        bool official_contact = false;
        if (!xmark_attachment_contact_position_for_burn_consume(
                attachment,
                now_ms,
                &contact_position,
                &official_contact)) {
            continue;
        }
        ++active_candidates;
        float effective_radius_x = radius_x;
        float effective_radius_y = radius_y;
        float effective_fallback_radius_x = fallback_radius_x;
        float effective_fallback_radius_y = fallback_radius_y;
        expand_charged_consume_radius_for_attachment(
            attachment,
            now_ms,
            &effective_radius_x,
            &effective_radius_y,
            &effective_fallback_radius_x,
            &effective_fallback_radius_y);
        max_effective_radius_x = std::max(max_effective_radius_x, effective_radius_x);
        max_effective_radius_y = std::max(max_effective_radius_y, effective_radius_y);
        const float dx = contact_position.x - impact_position.x;
        const float dy = contact_position.y - impact_position.y;
        if (require_contact &&
            (std::fabs(dx) > effective_radius_x || std::fabs(dy) > effective_radius_y)) {
            const bool inside_fallback_band =
                std::fabs(dx) <= effective_fallback_radius_x &&
                std::fabs(dy) <= effective_fallback_radius_y;
            if (arm_fallback_band && inside_fallback_band) {
                consumed += consume_or_arm_xmark_charged_health_probe(
                    attachment,
                    now_ms,
                    impact_position,
                    direction,
                    "charged-slam-impact-fallback-band");
            } else if (fallback_nearest && inside_fallback_band) {
                const float score = (dx * dx) + (dy * dy);
                if (score < nearest_score) {
                    nearest_score = score;
                    nearest_fallback = &attachment;
                    nearest_position = contact_position;
                    nearest_dx = dx;
                    nearest_dy = dy;
                    nearest_official = official_contact;
                }
            }
            continue;
        }
        const unsigned int confirmed =
            consume_or_arm_xmark_charged_health_probe(
                attachment,
                now_ms,
                impact_position,
                direction,
                reason);
        consumed += confirmed;
        if (consumed >= max_consumed) {
            break;
        }
    }
    if (consumed == 0 &&
        reason && std::strcmp(reason, "charged-slam-frame") == 0 &&
        env_bool("MINA_XMARK_MURIEL_MECHANIC_ENABLED", true)) {
        for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
            XMarkOfficialEnemyHost host =
                official_enemy_host_with_resolved_refs(g_official_enemy_hosts[i]);
            if (!host.active || !host.entity || !host.combat_core ||
                !xmark_official_host_is_muriel(host) ||
                find_xmark_burn_effect(host.entity, 0, host.combat_core)) {
                continue;
            }
            XMarkEnemyStatusRecord *status =
                xmark_enemy_status_find(host.entity, host.combat_core);
            if (!status || status->phase != XMarkEnemyStatusPhase::Marked ||
                !status->training_target ||
                now_ms >= status->state_expires_ms ||
                !muriel_drawn_charged_frame_overlaps_host(
                    g_last_muriel_charged_frame_state,
                    host,
                    now_ms)) {
                continue;
            }
            Vec3 muriel_position = host.position;
            official_entity_world_position_read(host.entity, &muriel_position);
            XMarkAttachment recovered{};
            recovered.active = true;
            recovered.target = host.entity;
            recovered.official_combat_core = host.combat_core;
            recovered.official_follow = true;
            recovered.target_health_like = false;
            recovered.suppress_hud = true;
            recovered.last_position = xmark_attachment_mark_position(muriel_position);
            recovered.has_last_position = true;
            recovered.render_half_w = official_enemy_render_half_from_health(
                host,
                xmark_default_render_half_w());
            recovered.render_half_h = official_enemy_render_half_from_health(
                host,
                xmark_default_render_half_h());
            if (consume_xmark_attachment_for_burn(
                    recovered,
                    now_ms,
                    "muriel-charged-status-recovery")) {
                ++consumed;
                ++active_candidates;
                if (g_mina && env_bool("MINA_XMARK_MURIEL_LOG", true)) {
                    g_mina->Log(
                        "XMarkBurn Muriel burn recovered from marked status after attachment handoff.\n");
                }
                break;
            }
        }
    }
    if (consumed == 0 && nearest_fallback) {
        if (consume_or_arm_xmark_charged_health_probe(
                *nearest_fallback,
                now_ms,
                impact_position,
                direction,
                "charged-slam-impact-nearest")) {
            ++consumed;
            if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_LOG", true)) {
                char fallback_message[448]{};
                std::snprintf(
                    fallback_message,
                    sizeof(fallback_message),
                    "XMarkBurn charged consume nearest fallback target=0x%p official=%u dx=%.3f dy=%.3f contact=(%.3f, %.3f, %.3f) radius=(%.3f, %.3f)\n",
                    reinterpret_cast<void *>(nearest_fallback->target),
                    nearest_official ? 1u : 0u,
                    static_cast<double>(nearest_dx),
                    static_cast<double>(nearest_dy),
                    static_cast<double>(nearest_position.x),
                    static_cast<double>(nearest_position.y),
                    static_cast<double>(nearest_position.z),
                    static_cast<double>(fallback_radius_x),
                    static_cast<double>(fallback_radius_y));
                g_mina->Log(fallback_message);
            }
        }
    }
    g_last_charged_consume_ms = now_ms;
    g_last_charged_consume_consumed = consumed;
    g_last_charged_consume_candidates = active_candidates;
    g_last_charged_consume_direction = direction;
    g_last_charged_consume_impact_x = impact_position.x;
    g_last_charged_consume_impact_y = impact_position.y;
    g_last_charged_consume_radius_x = radius_x;
    g_last_charged_consume_radius_y = radius_y;
    g_last_charged_consume_effective_radius_x = max_effective_radius_x;
    g_last_charged_consume_effective_radius_y = max_effective_radius_y;
    if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_LOG", true)) {
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn charged consume impact reason=%s dir=%s consumed=%u candidates=%u pos=(%.3f, %.3f, %.3f) radius=(%.3f, %.3f) fallback=(%.3f, %.3f) requireContact=%u\n",
            reason ? reason : "<none>",
            direction_name(direction),
            consumed,
            active_candidates,
            static_cast<double>(impact_position.x),
            static_cast<double>(impact_position.y),
            static_cast<double>(impact_position.z),
            static_cast<double>(radius_x),
            static_cast<double>(radius_y),
            static_cast<double>(fallback_radius_x),
            static_cast<double>(fallback_radius_y),
            require_contact ? 1u : 0u);
        g_mina->Log(message);
    }
    return consumed;
}

void queue_xmark_burn_charged_consume(unsigned long long now_ms, int direction, const char *reason) {
    if (!env_bool("MINA_XMARK_BURN_CONSUME_ON_CHARGED_RELEASE", false)) {
        return;
    }
    const unsigned int delay_ms = env_uint(
        "MINA_XMARK_BURN_CHARGED_IMPACT_DELAY_MS",
        env_uint("MINA_XMARK_NATIVE_CRATER_IMPACT_FRAME_DELAY_MS", 190));
    g_attack_burn_consume_pending = true;
    g_attack_burn_consume_pending_direction = direction;
    g_attack_burn_consume_pending_ms = now_ms + delay_ms;
    if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_QUEUE_LOG", true)) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn charged burn consume queued reason=%s dir=%s delayMs=%u\n",
            reason ? reason : "<none>",
            direction_name(direction),
            delay_ms);
        g_mina->Log(message);
    }
}

void maybe_fire_pending_xmark_burn_charged_consume(unsigned long long now_ms) {
    if (!g_attack_burn_consume_pending || now_ms < g_attack_burn_consume_pending_ms) {
        return;
    }
    const int direction = g_attack_burn_consume_pending_direction;
    g_attack_burn_consume_pending = false;
    g_attack_burn_consume_pending_ms = 0;
    const Vec3 impact_position = crater_position_for_direction(direction);
    consume_xmark_attachments_near_position_for_burn(
        now_ms,
        impact_position,
        direction,
        "charged-slam-impact");
}

void maybe_auto_consume_xmark_for_burn(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_BURN_AUTO_CONSUME_TEST", false)) {
        return;
    }
    const unsigned int delay_ms = env_uint("MINA_XMARK_BURN_AUTO_CONSUME_DELAY_MS", 700);
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || !attachment.started_ms) {
            continue;
        }
        if (now_ms >= attachment.started_ms + delay_ms) {
            consume_xmark_attachment_for_burn(attachment, now_ms, "auto-test");
            return;
        }
    }
}

void update_xmark_burn_effects(unsigned long long now_ms) {
    bool has_active_burn = false;
    bool has_active_damage_burn = false;
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        update_xmark_burn_palette_restore(burn, now_ms);
        if (burn.active) {
            has_active_burn = true;
            has_active_damage_burn = has_active_damage_burn || !burn.suppress_damage;
        }
    }
    if (!has_active_burn) {
        stop_xmark_owned_burn_tick_sfx("burn-idle");
        return;
    }

    Vec3 player_api{0.0f, 0.0f, 0.0f};
    if (g_mina) {
        g_mina->PlayerGetPos(&player_api.x, &player_api.y);
    }
    if (has_active_damage_burn &&
        env_bool("MINA_XMARK_BURN_FORCE_STATE_READ", true)) {
        read_enemy_visual_state_file(now_ms, true);
    }
    if (has_active_damage_burn && g_last_world &&
        env_bool("MINA_XMARK_BURN_DAMAGE_FORCE_OFFICIAL_SCAN", true) &&
        xmark_combat_full_scan_fallback_allowed()) {
        const bool needs_scan =
            !g_official_enemy_snapshot_valid ||
            g_official_enemy_host_count == 0 ||
            !g_last_official_enemy_scan_ms ||
            now_ms >= g_last_official_enemy_scan_ms +
                env_uint("MINA_XMARK_BURN_DAMAGE_FORCE_OFFICIAL_SCAN_STALE_MS", 500);
        if (needs_scan) {
            g_last_official_enemy_scan_ms = 0;
            update_official_enemy_snapshot(g_last_world, now_ms);
        }
    }

    const unsigned int tick_ms = std::max(1u, env_uint("MINA_XMARK_BURN_TICK_MS", 500));
    const float damage = env_float("MINA_XMARK_BURN_DAMAGE_PER_TICK", 1.0f);
    const unsigned int popup_ms = env_uint("MINA_XMARK_BURN_HUD_DAMAGE_POP_MS", 350);
    const bool tick_until_death = env_bool("MINA_XMARK_BURN_TICK_UNTIL_DEATH", false);
    const unsigned int until_death_max_ms =
        std::max(1u, env_uint("MINA_XMARK_BURN_TICK_UNTIL_DEATH_MAX_MS", 12000));
    const unsigned int max_ticks = tick_until_death ? 0u : env_uint("MINA_XMARK_BURN_MAX_TICKS", 6);

    bool published = false;
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active) {
            continue;
        }
        refresh_xmark_burn_palette(burn, now_ms);
        float bound_health = 0.0f;
        const bool bound_health_read =
            burn.official_combat_core &&
            combat_core_health_read(burn.official_combat_core, &bound_health);
        if (!burn.suppress_damage &&
            bound_health_read && bound_health <= 0.0f && !burn.lethal_written_ms) {
            burn.lethal_written_ms = now_ms;
            burn.next_tick_ms = 0;
            capture_xmark_burn_lethal_freeze_position(burn);
            const unsigned int death_linger_ms =
                env_uint("MINA_XMARK_BURN_HUD_DEATH_LINGER_MS", 700);
            burn.expires_ms = std::min(
                burn.expires_ms,
                now_ms + static_cast<unsigned long long>(death_linger_ms));
            const uintptr_t hud_target = burn.target
                ? burn.target
                : static_cast<uintptr_t>(burn.visual_key);
            if (hud_target) {
                upsert_xmark_hud_mark_mode(hud_target, burn.expires_ms, kXMarkHudModeBurn);
                record_xmark_hud_mark_health(hud_target, 0.0f, 0.0f);
                start_xmark_hud_death_linger(hud_target, now_ms, 0.0f);
            }
            published = true;
        }
        const bool until_death_window_active =
            !burn.suppress_damage &&
            tick_until_death &&
            burn.started_ms &&
            now_ms < burn.started_ms + until_death_max_ms &&
            burn.official_combat_core &&
            combat_core_is_alive(burn.official_combat_core);
        if (now_ms >= burn.expires_ms && !until_death_window_active) {
            end_xmark_burn_effect(burn, now_ms);
            published = true;
            continue;
        }
        if (until_death_window_active && burn.expires_ms < burn.started_ms + until_death_max_ms) {
            burn.expires_ms = burn.started_ms + until_death_max_ms;
        }
        const bool lethal_frozen = apply_xmark_burn_lethal_motion_freeze(burn, now_ms);
        const unsigned int fixed_position_refresh_ms = std::max(
            16u,
            env_uint("MINA_XMARK_BURN_FIXED_POSITION_REFRESH_MS", 32u));
        const bool debug_draw_position_fresh =
            xmark_burn_debug_draw_handles_effects() &&
            burn.has_last_position &&
            burn.last_visual_resolved_ms &&
            now_ms < burn.last_visual_resolved_ms + fixed_position_refresh_ms;
        if (!lethal_frozen && !debug_draw_position_fresh) {
            if (!refresh_xmark_burn_effect_position(burn, now_ms, player_api)) {
                end_xmark_burn_effect(burn, now_ms);
                published = true;
                continue;
            }
        }
        if (burn.death_anim_verify_next_ms && now_ms >= burn.death_anim_verify_next_ms) {
            char phase[64]{};
            std::snprintf(
                phase,
                sizeof(phase),
                "post-force-%u",
                burn.death_anim_verify_step);
            trace_xmark_burn_death_target(burn, now_ms, phase);
            ++burn.death_anim_verify_step;
            const unsigned int max_verify_steps =
                std::max(1u, env_uint("MINA_XMARK_BURN_FORCE_DEATH_ANIM_VERIFY_STEPS", 4));
            if (burn.death_anim_verify_step < max_verify_steps) {
                burn.death_anim_verify_next_ms =
                    now_ms + env_uint("MINA_XMARK_BURN_FORCE_DEATH_ANIM_VERIFY_INTERVAL_MS", 48);
            } else {
                burn.death_anim_verify_next_ms = 0;
            }
        }
        if (apply_xmark_burn_lethal_cleanup(burn, now_ms)) {
            published = true;
        }

        unsigned int guard = 0;
        while (burn.active &&
               burn.next_tick_ms &&
               now_ms >= burn.next_tick_ms &&
               (now_ms < burn.expires_ms ||
                 (!burn.suppress_damage &&
                  tick_until_death &&
                  burn.started_ms &&
                 now_ms < burn.started_ms + until_death_max_ms &&
                 burn.official_combat_core &&
                 combat_core_is_alive(burn.official_combat_core))) &&
               (burn.suppress_damage || !max_ticks || burn.tick_count < max_ticks) &&
               guard++ < 8u) {
            ++burn.tick_attempt_count;
            if (!burn.suppress_damage) {
                promote_xmark_burn_effect_to_damage_target(burn, now_ms);
            }
            float new_health = 0.0f;
            bool damaged = false;
            bool killed_this_tick = false;
            if (!burn.suppress_damage &&
                burn.official_follow &&
                burn.official_combat_core &&
                xmark_combat_core_health_write_api_available()) {
                XMarkOfficialEnemyHost host{};
                const bool host_resolved = official_enemy_host_for_burn(burn, &host);
                uintptr_t combat_core = host_resolved && host.combat_core
                    ? host.combat_core
                    : burn.official_combat_core;
                float current_health = 0.0f;
                const bool current_health_read =
                    combat_core && combat_core_health_read(combat_core, &current_health);
                if (!current_health_read && host_resolved && host.health > 0.0f) {
                    current_health = host.health;
                }
                if (combat_core && current_health > 0.0f) {
                    const bool lethal_candidate =
                        env_bool("MINA_XMARK_BURN_DAMAGE_KILL_ON_ZERO", true) &&
                        current_health <= damage + env_float("MINA_XMARK_BURN_DAMAGE_KILL_EPSILON", 0.001f);
                    char scripted_boss_type[96]{};
                    const bool scripted_boss_guard =
                        lethal_candidate &&
                        xmark_burn_target_is_scripted_boss(
                            burn,
                            host_resolved ? &host : nullptr,
                            scripted_boss_type,
                            sizeof(scripted_boss_type));
                    const bool finishing_hit = lethal_candidate && !scripted_boss_guard;
                    const float next_health = scripted_boss_guard
                        ? std::max(
                            env_float("MINA_XMARK_BURN_SCRIPTED_BOSS_MIN_HEALTH", 0.01f),
                            current_health - damage)
                        : (finishing_hit
                            ? env_float("MINA_XMARK_BURN_DAMAGE_KILL_HEALTH", 0.0f)
                            : std::max(0.0f, current_health - damage));
                    if (scripted_boss_guard && !burn.scripted_boss_guard_logged && g_mina) {
                        burn.scripted_boss_guard_logged = true;
                        char message[384]{};
                        std::snprintf(
                            message,
                            sizeof(message),
                            "XMarkBurn scripted boss lethal guard target=0x%p core=0x%p type=%s health=%.3f floor=%.3f\n",
                            reinterpret_cast<void *>(burn.target),
                            reinterpret_cast<void *>(combat_core),
                            scripted_boss_type[0] ? scripted_boss_type : "<unknown>",
                            static_cast<double>(current_health),
                            static_cast<double>(next_health));
                        g_mina->Log(message);
                    }
                    if (finishing_hit) {
                        trace_xmark_burn_death_target(burn, now_ms, "pre-lethal");
                    }
                    bool wrote_health = false;
                    __try {
                        g_mina->CombatCoreSetHealth(
                            reinterpret_cast<ycComponent *>(combat_core),
                            next_health);
                        wrote_health = true;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        wrote_health = false;
                    }
                    if (wrote_health) {
                        if (host_resolved && host.entity) {
                            burn.target = host.entity;
                        }
                        burn.official_combat_core = combat_core;
                        new_health = next_health;
                        damaged = true;
                        killed_this_tick = finishing_hit;
                        if (finishing_hit && !burn.lethal_written_ms) {
                            burn.lethal_written_ms = now_ms;
                        }
                        if (finishing_hit) {
                            capture_xmark_burn_lethal_freeze_position(burn);
                            apply_xmark_burn_lethal_motion_freeze(burn, now_ms);
                            force_xmark_burn_death_animation(burn, now_ms);
                            trace_xmark_burn_death_target(burn, now_ms, "post-lethal");
                        }
                        burn.last_tick_ms = now_ms;
                    }
                }
            }
            if (!burn.suppress_damage &&
                !damaged &&
                env_bool("MINA_XMARK_BURN_DAMAGE_RUNTIME_FALLBACK", false) &&
                burn.target_health_like &&
                burn.target &&
                burn.target_health_offset) {
                damaged = write_damage_to_health_at_offset(
                    burn.target,
                    burn.target_health_offset,
                    burn.target_health_kind,
                    damage,
                    &new_health);
                if (damaged) {
                    burn.last_tick_ms = now_ms;
                }
            }
            if (burn.suppress_damage) {
                burn.last_tick_ms = now_ms;
                ++burn.tick_applied_count;
                play_xmark_burn_tick_sfx(now_ms);
            } else if (!damaged && env_bool("MINA_XMARK_BURN_VISUAL_BLINK_ON_FAILED_DAMAGE", true)) {
                burn.last_tick_ms = now_ms;
            }
            if (damaged) {
                ++burn.tick_applied_count;
                play_xmark_burn_tick_sfx(now_ms);
            } else if (!burn.suppress_damage) {
                ++burn.tick_failed_count;
            }
            ++burn.tick_count;
            if (killed_this_tick && env_bool("MINA_XMARK_BURN_STOP_TICKS_AFTER_KILL", true)) {
                burn.next_tick_ms = 0;
            } else {
                burn.next_tick_ms += tick_ms;
            }

            const uintptr_t hud_target = burn.suppress_damage
                ? 0
                : (burn.target ? burn.target : static_cast<uintptr_t>(burn.visual_key));
            if (hud_target) {
                upsert_xmark_hud_mark_mode(hud_target, burn.expires_ms, kXMarkHudModeBurn);
                float health_max = 0.0f;
                if (burn.official_combat_core && g_mina && g_mina->CombatCoreGetHealthMax) {
                    __try {
                        health_max = g_mina->CombatCoreGetHealthMax(reinterpret_cast<ycComponent *>(burn.official_combat_core));
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        health_max = 0.0f;
                    }
                }
                record_xmark_hud_mark_health(hud_target, new_health, health_max);
                if (killed_this_tick) {
                    start_xmark_hud_death_linger(hud_target, now_ms, health_max);
                }
                set_xmark_hud_damage_popup(hud_target, now_ms + popup_ms);
            }
            published = true;

            if (g_mina && env_bool("MINA_XMARK_BURN_TICK_LOG", false)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn tick target=0x%p officialCore=0x%p official=%u damaged=%u killed=%u tick=%u newHealth=%.3f health=0x%X/kind%u pos=(%.3f, %.3f, %.3f)\n",
                    reinterpret_cast<void *>(burn.target),
                    reinterpret_cast<void *>(burn.official_combat_core),
                    burn.official_follow ? 1u : 0u,
                    damaged ? 1u : 0u,
                    killed_this_tick ? 1u : 0u,
                    burn.tick_count,
                    static_cast<double>(new_health),
                    burn.target_health_offset,
                    burn.target_health_kind,
                    static_cast<double>(burn.last_position.x),
                    static_cast<double>(burn.last_position.y),
                    static_cast<double>(burn.last_position.z));
                g_mina->Log(message);
            }
        }
    }

    if (published) {
        publish_current_xmark_hud_state(now_ms);
    }
}

