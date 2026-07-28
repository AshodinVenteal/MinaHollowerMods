bool env_token_list_contains(const char *name, const char *token) {
    if (!name || !token || !token[0]) {
        return false;
    }
    char value[512]{};
    if (!xmark_read_environment_value(name, value, sizeof(value))) {
        return false;
    }

    const char *cursor = value;
    while (*cursor) {
        while (*cursor == ',' || *cursor == ';' || *cursor == ' ' || *cursor == '\t') {
            ++cursor;
        }
        if (!*cursor) {
            break;
        }
        const char *start = cursor;
        while (*cursor && *cursor != ',' && *cursor != ';' && *cursor != ' ' && *cursor != '\t') {
            ++cursor;
        }
        const size_t count = static_cast<size_t>(cursor - start);
        if (count > 0 && count < 128) {
            char candidate[128]{};
            std::memcpy(candidate, start, count);
            candidate[count] = 0;
            if (_stricmp(candidate, token) == 0) {
                return true;
            }
        }
    }
    return false;
}

bool read_basic_frame_state_file(XMarkBasicFrameState *state_out) {
    if (!state_out || !g_basic_frame_state_path[0] ||
        !env_bool("MINA_XMARK_BASIC_FRAME_STATE_FILE_ENABLED", true)) {
        return false;
    }
    const unsigned long long now_ms = GetTickCount64();
    const unsigned int min_poll_ms =
        env_uint("MINA_XMARK_BASIC_FRAME_STATE_MIN_POLL_MS", 4);
    if (g_cached_basic_frame_file_read_ms &&
        now_ms < g_cached_basic_frame_file_read_ms + min_poll_ms) {
        if (g_cached_basic_frame_file_state_valid) {
            *state_out = g_cached_basic_frame_file_state;
        }
        return g_cached_basic_frame_file_state_valid;
    }
    g_cached_basic_frame_file_read_ms = now_ms;

    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExA(g_basic_frame_state_path, GetFileExInfoStandard, &attributes)) {
        if (g_cached_basic_frame_file_state_valid) {
            *state_out = g_cached_basic_frame_file_state;
        }
        return g_cached_basic_frame_file_state_valid;
    }
    const bool file_unchanged =
        g_cached_basic_frame_file_identity_valid &&
        CompareFileTime(&attributes.ftLastWriteTime, &g_cached_basic_frame_file_write_time) == 0 &&
        attributes.nFileSizeHigh == g_cached_basic_frame_file_size_high &&
        attributes.nFileSizeLow == g_cached_basic_frame_file_size_low;
    if (file_unchanged && g_cached_basic_frame_file_state_valid) {
        *state_out = g_cached_basic_frame_file_state;
        return true;
    }

    FILE *file = nullptr;
    if (fopen_s(&file, g_basic_frame_state_path, "rb") != 0 || !file) {
        if (g_cached_basic_frame_file_state_valid) {
            *state_out = g_cached_basic_frame_file_state;
        }
        return g_cached_basic_frame_file_state_valid;
    }

    XMarkBasicFrameState state{};
    char line[192]{};
    while (std::fgets(line, sizeof(line), file)) {
        char *newline = std::strchr(line, '\n');
        if (newline) {
            *newline = 0;
        }
        if (std::strncmp(line, "tick=", 5) == 0) {
            state.tick = std::strtoull(line + 5, nullptr, 0);
        } else if (std::strncmp(line, "draw=", 5) == 0) {
            state.draw = std::strtoull(line + 5, nullptr, 0);
        } else if (std::strncmp(line, "frame=", 6) == 0) {
            std::snprintf(state.frame, sizeof(state.frame), "%s", line + 6);
        } else if (std::strncmp(line, "direction=", 10) == 0) {
            state.direction = static_cast<int>(std::strtol(line + 10, nullptr, 0));
        } else if (std::strncmp(line, "sideSmear=", 10) == 0) {
            state.side_smear = std::strtoul(line + 10, nullptr, 0) != 0;
        } else if (std::strncmp(line, "hasGeometry=", 12) == 0) {
            state.has_geometry = std::strtoul(line + 12, nullptr, 0) != 0;
        } else if (std::strncmp(line, "hasContact=", 11) == 0) {
            state.has_contact = std::strtoul(line + 11, nullptr, 0) != 0;
        } else if (std::strncmp(line, "contactX=", 9) == 0) {
            state.contact_x = static_cast<float>(std::strtod(line + 9, nullptr));
        } else if (std::strncmp(line, "contactY=", 9) == 0) {
            state.contact_y = static_cast<float>(std::strtod(line + 9, nullptr));
        } else if (std::strncmp(line, "minX=", 5) == 0) {
            state.min_x = static_cast<float>(std::strtod(line + 5, nullptr));
        } else if (std::strncmp(line, "maxX=", 5) == 0) {
            state.max_x = static_cast<float>(std::strtod(line + 5, nullptr));
        } else if (std::strncmp(line, "minY=", 5) == 0) {
            state.min_y = static_cast<float>(std::strtod(line + 5, nullptr));
        } else if (std::strncmp(line, "maxY=", 5) == 0) {
            state.max_y = static_cast<float>(std::strtod(line + 5, nullptr));
        }
    }
    std::fclose(file);

    if (!state.tick || state.direction < FacingRight || state.direction > FacingDown) {
        if (g_cached_basic_frame_file_state_valid) {
            *state_out = g_cached_basic_frame_file_state;
        }
        return g_cached_basic_frame_file_state_valid;
    }
    state.has_contact =
        state.has_contact &&
        std::isfinite(state.contact_x) &&
        std::isfinite(state.contact_y);
    state.has_geometry =
        state.has_geometry &&
        std::isfinite(state.min_x) &&
        std::isfinite(state.max_x) &&
        std::isfinite(state.min_y) &&
        std::isfinite(state.max_y) &&
        state.max_x > state.min_x &&
        state.max_y > state.min_y;

    if (g_mina &&
        env_bool("MINA_XMARK_D3D12_BASIC_FRAME_REQUIRE_NEAR_PLAYER", true) &&
        (state.has_contact || state.has_geometry)) {
        Vec3 player{};
        g_mina->PlayerGetPos(&player.x, &player.y);
        const float state_x = state.has_contact
            ? state.contact_x
            : (state.min_x + state.max_x) * 0.5f;
        const float state_y = state.has_contact
            ? state.contact_y
            : (state.min_y + state.max_y) * 0.5f;
        const float max_distance = std::max(
            1.0f,
            env_float("MINA_XMARK_D3D12_BASIC_FRAME_MAX_PLAYER_DISTANCE", 48.0f));
        const float dx = state_x - player.x;
        const float dy = state_y - player.y;
        if (!std::isfinite(player.x) ||
            !std::isfinite(player.y) ||
            !std::isfinite(state_x) ||
            !std::isfinite(state_y) ||
            dx * dx + dy * dy > max_distance * max_distance) {
            return false;
        }
    }
    g_cached_basic_frame_file_state = state;
    g_cached_basic_frame_file_state_valid = true;
    g_cached_basic_frame_file_write_time = attributes.ftLastWriteTime;
    g_cached_basic_frame_file_size_high = attributes.nFileSizeHigh;
    g_cached_basic_frame_file_size_low = attributes.nFileSizeLow;
    g_cached_basic_frame_file_identity_valid = true;
    *state_out = g_cached_basic_frame_file_state;
    return true;
}

struct MinaXMarkBasicFrameSharedState {
    unsigned int version;
    unsigned int size;
    unsigned long long sequence;
    unsigned long long tick;
    unsigned long long draw;
    char frame[32];
    int direction;
    unsigned int side_smear;
    unsigned int has_geometry;
    unsigned int has_contact;
    float contact_x;
    float contact_y;
    float min_x;
    float max_x;
    float min_y;
    float max_y;
};

struct MinaCrossBlastImpactSharedState {
    unsigned int version;
    unsigned int size;
    unsigned long long sequence;
    unsigned long long tick;
    unsigned long long draw;
    int direction;
};

using MinaXMarkReadBasicFrameStateFn = unsigned int (*)(void *, unsigned int);
using MinaXMarkReadCrossBlastImpactStateFn = unsigned int (*)(void *, unsigned int);
using MinaXMarkArmChargedCraterFn = unsigned int (*)(int, unsigned long long);

MinaXMarkReadBasicFrameStateFn resolve_basic_frame_state_reader() {
    const char *export_name = "MinaXMark_ReadBasicFrameState";
    HMODULE named_module = GetModuleHandleW(L"d3d12.dll");
    if (named_module) {
        auto reader = reinterpret_cast<MinaXMarkReadBasicFrameStateFn>(
            GetProcAddress(named_module, export_name));
        if (reader) {
            return reader;
        }
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    MinaXMarkReadBasicFrameStateFn reader = nullptr;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            reader = reinterpret_cast<MinaXMarkReadBasicFrameStateFn>(
                GetProcAddress(entry.hModule, export_name));
            if (reader) {
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return reader;
}

MinaXMarkReadCrossBlastImpactStateFn resolve_cross_blast_impact_state_reader() {
    const char *export_name = "MinaXMark_ReadCrossBlastImpactState";
    HMODULE named_module = GetModuleHandleW(L"d3d12.dll");
    if (named_module) {
        auto reader = reinterpret_cast<MinaXMarkReadCrossBlastImpactStateFn>(
            GetProcAddress(named_module, export_name));
        if (reader) {
            return reader;
        }
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    MinaXMarkReadCrossBlastImpactStateFn reader = nullptr;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            reader = reinterpret_cast<MinaXMarkReadCrossBlastImpactStateFn>(
                GetProcAddress(entry.hModule, export_name));
            if (reader) {
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return reader;
}

bool read_cross_blast_impact_state(MinaCrossBlastImpactSharedState *state_out) {
    if (!state_out || !env_bool("MINA_XMARK_BOOM_NATIVE_IMPACT_BRIDGE", true)) {
        return false;
    }
    static MinaXMarkReadCrossBlastImpactStateFn reader = nullptr;
    static unsigned long long last_resolve_ms = 0;
    if (!reader) {
        const unsigned long long now_ms = GetTickCount64();
        if (last_resolve_ms && now_ms < last_resolve_ms + 1000) {
            return false;
        }
        last_resolve_ms = now_ms;
        reader = resolve_cross_blast_impact_state_reader();
        if (!reader) {
            return false;
        }
        if (g_mina) {
            g_mina->Log("XMarkBurn Cross Blast native-impact bridge resolved.");
        }
    }
    MinaCrossBlastImpactSharedState state{};
    if (reader(&state, sizeof(state)) != sizeof(state) ||
        state.version != 1 || state.size != sizeof(state) || !state.tick ||
        state.direction < FacingRight || state.direction > FacingDown) {
        return false;
    }
    *state_out = state;
    return true;
}

MinaXMarkArmChargedCraterFn resolve_charged_crater_arm() {
    const char *export_name = "MinaXMark_ArmChargedCrater";
    HMODULE named_module = GetModuleHandleW(L"d3d12.dll");
    if (named_module) {
        auto arm = reinterpret_cast<MinaXMarkArmChargedCraterFn>(
            GetProcAddress(named_module, export_name));
        if (arm) {
            return arm;
        }
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        return nullptr;
    }
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    MinaXMarkArmChargedCraterFn arm = nullptr;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            arm = reinterpret_cast<MinaXMarkArmChargedCraterFn>(
                GetProcAddress(entry.hModule, export_name));
            if (arm) {
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return arm;
}

void arm_d3d12_charged_crater(int direction, unsigned long long source_tick) {
    static MinaXMarkArmChargedCraterFn arm = nullptr;
    static unsigned long long last_resolve_ms = 0;
    if (!arm) {
        const unsigned long long now_ms = GetTickCount64();
        if (last_resolve_ms && now_ms < last_resolve_ms + 1000) {
            return;
        }
        last_resolve_ms = now_ms;
        arm = resolve_charged_crater_arm();
    }
    if (arm) {
        arm(direction, source_tick);
    }
}

bool read_basic_frame_state_shared(XMarkBasicFrameState *state_out) {
    if (!state_out || !env_bool("MINA_XMARK_BASIC_FRAME_SHARED_ENABLED", true)) {
        return false;
    }
    static MinaXMarkReadBasicFrameStateFn reader = nullptr;
    static unsigned long long last_resolve_ms = 0;
    if (!reader) {
        const unsigned long long now_ms = GetTickCount64();
        if (last_resolve_ms && now_ms < last_resolve_ms + 1000) {
            return false;
        }
        last_resolve_ms = now_ms;
        reader = resolve_basic_frame_state_reader();
        if (!reader) {
            return false;
        }
        if (g_mina) {
            g_mina->Log("XMarkBurn basic-frame shared bridge resolved.");
        }
    }
    MinaXMarkBasicFrameSharedState shared{};
    if (reader(&shared, sizeof(shared)) != sizeof(shared) ||
        shared.version != 1 || shared.size != sizeof(shared) || !shared.tick) {
        return false;
    }
    XMarkBasicFrameState state{};
    state.tick = shared.tick;
    state.draw = shared.draw;
    std::snprintf(state.frame, sizeof(state.frame), "%s", shared.frame);
    state.direction = shared.direction;
    state.side_smear = shared.side_smear != 0;
    state.has_geometry = shared.has_geometry != 0;
    state.has_contact = shared.has_contact != 0;
    state.contact_x = shared.contact_x;
    state.contact_y = shared.contact_y;
    state.min_x = shared.min_x;
    state.max_x = shared.max_x;
    state.min_y = shared.min_y;
    state.max_y = shared.max_y;
    if (state.direction < FacingRight || state.direction > FacingDown) {
        return false;
    }
    *state_out = state;
    return true;
}

bool read_basic_frame_state(XMarkBasicFrameState *state_out) {
    if (read_basic_frame_state_shared(state_out)) {
        return true;
    }
    return read_basic_frame_state_file(state_out);
}

bool official_enemy_init_rtti();
bool read_modapi_player_basic_frame_state(unsigned long long now_ms, XMarkBasicFrameState *state_out);
bool read_preferred_player_attack_frame_state(unsigned long long now_ms, XMarkBasicFrameState *state_out);
bool api_function_field_ready(size_t field_offset);
MM_WeakPtr *xmark_weak_ptr_create(void *ptr);
void xmark_weak_ptr_destroy(MM_WeakPtr *weak);
void *xmark_weak_ptr_get(MM_WeakPtr *weak);
unsigned long long xmark_status_now_ms();
void xmark_probe_anim_properties(ycComponent *anim);

bool xmark_basic_frame_name_allowed(const char *frame) {
    if (!frame || !frame[0]) {
        return false;
    }
    return std::strcmp(frame, "f0014") == 0 ||
        std::strcmp(frame, "f0015") == 0 ||
        std::strcmp(frame, "f0016") == 0 ||
        std::strcmp(frame, "f0017") == 0 ||
        std::strcmp(frame, "f0018") == 0 ||
        std::strcmp(frame, "f0019") == 0 ||
        std::strcmp(frame, "f0020-f0021") == 0 ||
        std::strcmp(frame, "f0022") == 0 ||
        std::strcmp(frame, "f0023") == 0 ||
        std::strcmp(frame, "f0024") == 0 ||
        std::strcmp(frame, "f0025") == 0 ||
        std::strcmp(frame, "f0026") == 0 ||
        std::strcmp(frame, "f0027") == 0 ||
        std::strcmp(frame, "f0025-f0027") == 0 ||
        std::strcmp(frame, "f0028") == 0;
}

bool xmark_charged_frame_name_allowed(const char *frame) {
    if (!frame || !frame[0]) {
        return false;
    }
    return std::strcmp(frame, "f0001") == 0 ||
        std::strcmp(frame, "f0003") == 0 ||
        std::strcmp(frame, "f0005") == 0;
}

bool xmark_side_basic_start_frame_for_f0029(const char *frame) {
    if (!frame || !frame[0]) {
        return false;
    }
    return std::strcmp(frame, "f0014") == 0;
}

bool xmark_side_basic_visible_frame_for_f0029(const char *frame) {
    if (!frame || !frame[0]) {
        return false;
    }
    return std::strcmp(frame, "f0014") == 0 ||
        std::strcmp(frame, "f0015") == 0 ||
        std::strcmp(frame, "f0016") == 0 ||
        std::strcmp(frame, "f0017") == 0;
}

bool xmark_vertical_basic_start_frame_for_smear(const char *frame, int direction) {
    if (!frame || !frame[0]) {
        return false;
    }
    if (direction == FacingDown) {
        return std::strcmp(frame, "f0020-f0021") == 0;
    }
    if (direction == FacingUp) {
        return std::strcmp(frame, "f0025") == 0 ||
            std::strcmp(frame, "f0025-f0027") == 0;
    }
    return false;
}

bool xmark_vertical_basic_initial_frame_for_smear(const char *frame, int direction) {
    if (!frame || !frame[0]) {
        return false;
    }
    return (direction == FacingDown && std::strcmp(frame, "f0019") == 0) ||
        (direction == FacingUp && std::strcmp(frame, "f0024") == 0);
}

bool xmark_vertical_basic_visible_frame_for_smear(const char *frame, int direction) {
    if (!frame || !frame[0]) {
        return false;
    }
    if (direction == FacingDown) {
        return std::strcmp(frame, "f0019") == 0 ||
            std::strcmp(frame, "f0020-f0021") == 0 ||
            std::strcmp(frame, "f0022") == 0 ||
            std::strcmp(frame, "f0023") == 0;
    }
    if (direction == FacingUp) {
        return std::strcmp(frame, "f0024") == 0 ||
            std::strcmp(frame, "f0025") == 0 ||
            std::strcmp(frame, "f0026") == 0 ||
            std::strcmp(frame, "f0027") == 0 ||
            std::strcmp(frame, "f0025-f0027") == 0 ||
            std::strcmp(frame, "f0028") == 0;
    }
    return false;
}

bool recent_basic_frame_state_for_direction(
    unsigned long long now_ms,
    int direction,
    XMarkBasicFrameState *state_out = nullptr) {
    XMarkBasicFrameState state{};
    bool has_state = read_preferred_player_attack_frame_state(now_ms, &state);
    const unsigned int max_age_ms = env_uint("MINA_XMARK_D3D12_BASIC_FRAME_MAX_AGE_MS", 180);
    if (has_state && !state.modapi_authoritative &&
        max_age_ms &&
        now_ms > state.tick + max_age_ms &&
        read_modapi_player_basic_frame_state(now_ms, &state)) {
        has_state = true;
    }
    if (!has_state &&
        read_modapi_player_basic_frame_state(now_ms, &state)) {
        has_state = true;
    }
    if (!has_state) {
        return false;
    }
    if (!xmark_basic_frame_name_allowed(state.frame) &&
        read_modapi_player_basic_frame_state(now_ms, &state)) {
        has_state = true;
    }
    if (!xmark_basic_frame_name_allowed(state.frame)) {
        return false;
    }
    int state_direction = state.direction;
    const bool requested_side = direction == FacingLeft || direction == FacingRight;
    const bool state_side = state.direction == FacingLeft || state.direction == FacingRight;
    if (requested_side &&
        state_side &&
        g_basic_smear_animation_lock.active &&
        g_basic_smear_animation_lock.direction == direction) {
        const unsigned int lock_ms = env_uint("MINA_XMARK_BASIC_SMEAR_ANIMATION_LOCK_MS", 360);
        if (!lock_ms || now_ms <= g_basic_smear_animation_lock.started_ms + lock_ms) {
            state_direction = direction;
            state.direction = direction;
        }
    }
    if (direction >= FacingRight && direction <= FacingDown && state_direction != direction) {
        return false;
    }
    if (max_age_ms && now_ms > state.tick + max_age_ms) {
        return false;
    }
    if (state_out) {
        *state_out = state;
    }
    return true;
}

bool basic_frame_state_overlaps_visual_host(
    const XMarkBasicFrameState &state,
    const XMarkVisualEnemyHost &host) {
    if (!host.active || !xmark_basic_frame_name_allowed(state.frame)) {
        return false;
    }

    const bool vertical_attack = state.direction == FacingUp || state.direction == FacingDown;
    const float host_half_fallback = env_float("MINA_XMARK_BASIC_FRAME_HOST_HALF_FALLBACK", 0.85f);
    float host_half_w = host.half_w > 0.0f && std::isfinite(host.half_w) ? host.half_w : host_half_fallback;
    float host_half_h = host.half_h > 0.0f && std::isfinite(host.half_h) ? host.half_h : host_half_fallback;
    if (host.texture_width > 0 && host.texture_height > 0) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        host_half_w = std::max(host_half_w, static_cast<float>(host.texture_width) * units_per_pixel * 0.5f);
        host_half_h = std::max(host_half_h, static_cast<float>(host.texture_height) * units_per_pixel * 0.5f);
    }
    if (vertical_attack) {
        host_half_w += std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_VERTICAL_HOST_EXTRA_HALF_W", 0.35f));
        host_half_h += std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_VERTICAL_HOST_EXTRA_HALF_H", 0.55f));
    }

    if (state.has_geometry) {
        float padding = std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_CONTACT_PADDING", 0.35f));
        float attack_min_y = state.min_y;
        float attack_max_y = state.max_y;
        if (vertical_attack) {
            padding = std::max(
                padding,
                std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_VERTICAL_CONTACT_PADDING", 1.6f)));
            const float reach_bonus =
                std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_VERTICAL_REACH_BONUS_PIXELS", 0.5f)) *
                xmark_visual_host_units_per_pixel();
            if (state.direction == FacingUp) {
                attack_min_y -= reach_bonus;
            } else {
                attack_max_y += reach_bonus;
            }
        }
        const float host_min_x = host.position.x - host_half_w;
        const float host_max_x = host.position.x + host_half_w;
        const float host_min_y = host.position.y - host_half_h;
        const float host_max_y = host.position.y + host_half_h;
        return state.max_x + padding >= host_min_x &&
            state.min_x - padding <= host_max_x &&
            attack_max_y + padding >= host_min_y &&
            attack_min_y - padding <= host_max_y;
    }

    if (env_bool("MINA_XMARK_BASIC_FRAME_REQUIRE_GEOMETRY", true) || !state.has_contact) {
        return false;
    }

    float radius = std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_CONTACT_RADIUS", 1.4f));
    if (vertical_attack) {
        radius = std::max(
            radius,
            std::max(0.0f, env_float("MINA_XMARK_BASIC_FRAME_VERTICAL_CONTACT_RADIUS", 2.4f)));
    }
    const float dx = state.contact_x - host.position.x;
    const float dy = state.contact_y - host.position.y;
    const float inflated_radius = radius + std::max(host_half_w, host_half_h);
    return (dx * dx) + (dy * dy) <= inflated_radius * inflated_radius;
}

bool recent_basic_frame_contact_overlaps_visual_host(
    unsigned long long now_ms,
    int direction,
    const XMarkVisualEnemyHost &host,
    XMarkBasicFrameState *state_out = nullptr) {
    XMarkBasicFrameState state{};
    if (!recent_basic_frame_state_for_direction(now_ms, direction, &state)) {
        return false;
    }
    if (!basic_frame_state_overlaps_visual_host(state, host)) {
        return false;
    }
    if (state_out) {
        *state_out = state;
    }
    return true;
}

char *next_tsv_token(char **cursor) {
    if (!cursor || !*cursor) {
        return nullptr;
    }
    char *start = *cursor;
    char *end = std::strchr(start, '\t');
    if (end) {
        *end = 0;
        *cursor = end + 1;
    } else {
        *cursor = start + std::strlen(start);
    }
    char *newline = std::strpbrk(start, "\r\n");
    if (newline) {
        *newline = 0;
    }
    return start;
}

void copy_visual_token(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) {
        return;
    }
    dest[0] = 0;
    if (!src || src[0] == 0 || (src[0] == '-' && src[1] == 0)) {
        return;
    }
    std::snprintf(dest, dest_size, "%s", src);
}

bool read_enemy_visual_state_file(unsigned long long now_ms, bool force) {
    if (!env_bool("MINA_XMARK_ENEMY_VISUAL_STATE_ENABLED", true) || !g_enemy_visual_state_path[0]) {
        return false;
    }

    const unsigned int min_poll_ms = env_uint("MINA_XMARK_ENEMY_VISUAL_STATE_MIN_POLL_MS", 16);
    const unsigned int poll_ms = std::max(min_poll_ms, env_uint("MINA_XMARK_ENEMY_VISUAL_STATE_POLL_MS", 50));
    if (!force &&
        g_last_visual_enemy_state_read_ms &&
        now_ms >= g_last_visual_enemy_state_read_ms &&
        now_ms - g_last_visual_enemy_state_read_ms < poll_ms) {
        return g_visual_enemy_host_count > 0;
    }
    g_last_visual_enemy_state_read_ms = now_ms;

    XMarkVisualEnemyHost previous_hosts[64]{};
    const unsigned int previous_host_count =
        std::min<unsigned int>(g_visual_enemy_host_count, static_cast<unsigned int>(sizeof(previous_hosts) / sizeof(previous_hosts[0])));
    for (unsigned int i = 0; i < previous_host_count; ++i) {
        previous_hosts[i] = g_visual_enemy_hosts[i];
    }
    const unsigned long long previous_tick = g_visual_enemy_state_tick;
    const unsigned int stale_hold_ms = env_uint("MINA_XMARK_ENEMY_VISUAL_STATE_STALE_HOLD_MS", 180);

    FILE *file = nullptr;
    if (fopen_s(&file, g_enemy_visual_state_path, "rb") != 0 || !file) {
        if (stale_hold_ms > 0 &&
            previous_host_count > 0 &&
            previous_tick &&
            now_ms <= previous_tick + stale_hold_ms) {
            return true;
        }
        g_visual_enemy_host_count = 0;
        g_visual_enemy_state_tick = 0;
        return false;
    }

    XMarkVisualEnemyHost hosts[64]{};
    unsigned int host_count = 0;
    unsigned long long tick = 0;
    bool saw_declared_count = false;
    unsigned int declared_count = 0;
    const Vec3 base = official_spawn_position();
    const unsigned long long state_max_age_ms = env_uint("MINA_XMARK_ENEMY_VISUAL_STATE_MAX_AGE_MS", 700);
    const unsigned long long host_max_age_ms = env_uint("MINA_XMARK_ENEMY_VISUAL_HOST_MAX_AGE_MS", 700);

    char line[512]{};
    while (std::fgets(line, sizeof(line), file)) {
        char *newline = std::strpbrk(line, "\r\n");
        if (newline) {
            *newline = 0;
        }
        if (std::strncmp(line, "tick=", 5) == 0) {
            tick = std::strtoull(line + 5, nullptr, 0);
            continue;
        }
        if (std::strncmp(line, "count=", 6) == 0) {
            saw_declared_count = true;
            declared_count = static_cast<unsigned int>(std::strtoul(line + 6, nullptr, 0));
            continue;
        }
        if (line[0] == 0 || std::strncmp(line, "version=", 8) == 0 ||
            std::strncmp(line, "key\t", 4) == 0) {
            continue;
        }
        if (host_count >= static_cast<unsigned int>(sizeof(hosts) / sizeof(hosts[0]))) {
            break;
        }

        char *cursor = line;
        const char *key_token = next_tsv_token(&cursor);
        const char *entry_token = next_tsv_token(&cursor);
        const char *stem_token = next_tsv_token(&cursor);
        const char *catalog_token = next_tsv_token(&cursor);
        const char *sig_token = next_tsv_token(&cursor);
        const char *w_token = next_tsv_token(&cursor);
        const char *h_token = next_tsv_token(&cursor);
        const char *x_token = next_tsv_token(&cursor);
        const char *y_token = next_tsv_token(&cursor);
        const char *half_w_token = next_tsv_token(&cursor);
        const char *half_h_token = next_tsv_token(&cursor);
        const char *last_seen_token = next_tsv_token(&cursor);
        const char *last_hurt_token = next_tsv_token(&cursor);
        const char *recent_hurt_token = next_tsv_token(&cursor);
        if (!key_token || !x_token || !y_token || !last_seen_token) {
            continue;
        }

        XMarkVisualEnemyHost host{};
        host.key = std::strtoull(key_token, nullptr, 0);
        host.texture_signature = sig_token ? std::strtoull(sig_token, nullptr, 0) : 0ull;
        host.texture_width = w_token ? static_cast<unsigned int>(std::strtoul(w_token, nullptr, 0)) : 0u;
        host.texture_height = h_token ? static_cast<unsigned int>(std::strtoul(h_token, nullptr, 0)) : 0u;
        host.position.x = x_token ? static_cast<float>(std::strtod(x_token, nullptr)) : 0.0f;
        host.position.y = y_token ? static_cast<float>(std::strtod(y_token, nullptr)) : 0.0f;
        host.position.z = base.z;
        host.half_w = half_w_token ? static_cast<float>(std::strtod(half_w_token, nullptr)) : 0.0f;
        host.half_h = half_h_token ? static_cast<float>(std::strtod(half_h_token, nullptr)) : 0.0f;
        host.last_seen_ms = std::strtoull(last_seen_token, nullptr, 0);
        host.last_hurt_flash_ms = last_hurt_token ? std::strtoull(last_hurt_token, nullptr, 0) : 0ull;
        host.recent_hurt = recent_hurt_token && std::strtoul(recent_hurt_token, nullptr, 0) != 0;
        if (!host.key ||
            !std::isfinite(host.position.x) ||
            !std::isfinite(host.position.y) ||
            (host_max_age_ms && host.last_seen_ms && now_ms > host.last_seen_ms + host_max_age_ms)) {
            continue;
        }
        copy_visual_token(host.entry, sizeof(host.entry), entry_token);
        copy_visual_token(host.stem, sizeof(host.stem), stem_token);
        copy_visual_token(host.catalog, sizeof(host.catalog), catalog_token);
        host.active = true;
        hosts[host_count++] = host;
    }
    std::fclose(file);

    const bool explicit_zero_hosts = saw_declared_count && declared_count == 0;
    const bool hold_explicit_zero =
        explicit_zero_hosts && env_bool("MINA_XMARK_ENEMY_VISUAL_STATE_HOLD_EXPLICIT_ZERO", false);
    g_visual_enemy_state_explicit_zero = explicit_zero_hosts;
    if (explicit_zero_hosts && !hold_explicit_zero) {
        host_count = 0;
    } else if (state_max_age_ms && tick && now_ms > tick + state_max_age_ms) {
        host_count = 0;
    }

    if ((!explicit_zero_hosts || hold_explicit_zero) && stale_hold_ms > 0 && previous_host_count > 0) {
        const unsigned int max_hurt_age_ms = env_uint("MINA_XMARK_BASIC_HURT_VISUAL_MAX_AGE_MS", 450);
        for (unsigned int previous_index = 0;
             previous_index < previous_host_count && host_count < static_cast<unsigned int>(sizeof(hosts) / sizeof(hosts[0]));
             ++previous_index) {
            XMarkVisualEnemyHost previous = previous_hosts[previous_index];
            if (!previous.active || !previous.key) {
                continue;
            }
            bool already_present = false;
            for (unsigned int host_index = 0; host_index < host_count; ++host_index) {
                if (hosts[host_index].key == previous.key) {
                    already_present = true;
                    break;
                }
            }
            if (already_present) {
                continue;
            }
            const bool recently_seen =
                previous.last_seen_ms &&
                now_ms >= previous.last_seen_ms &&
                now_ms <= previous.last_seen_ms + stale_hold_ms;
            const bool recently_hurt =
                previous.last_hurt_flash_ms &&
                now_ms >= previous.last_hurt_flash_ms &&
                (!max_hurt_age_ms || now_ms <= previous.last_hurt_flash_ms + max_hurt_age_ms);
            if (!recently_seen && !recently_hurt) {
                continue;
            }
            previous.active = true;
            hosts[host_count++] = previous;
        }
    }

    g_visual_enemy_host_count = host_count;
    g_visual_enemy_state_tick = tick;
    for (unsigned int i = 0; i < host_count; ++i) {
        g_visual_enemy_hosts[i] = hosts[i];
    }

    static unsigned int last_logged_count = 0xFFFFFFFFu;
    if (g_mina && env_bool("MINA_XMARK_ENEMY_VISUAL_STATE_LOG", true) && host_count != last_logged_count) {
        last_logged_count = host_count;
        const XMarkVisualEnemyHost *first_host = host_count > 0 ? &g_visual_enemy_hosts[0] : nullptr;
        char message[640]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn enemy visual state read hosts=%u tick=%llu firstKey=0x%llX firstEntry=%s firstStem=%s firstCatalog=%s firstPos=(%.3f, %.3f) path=%s\n",
            host_count,
            tick,
            first_host ? first_host->key : 0ull,
            first_host && first_host->entry[0] ? first_host->entry : "-",
            first_host && first_host->stem[0] ? first_host->stem : "-",
            first_host && first_host->catalog[0] ? first_host->catalog : "-",
            first_host ? static_cast<double>(first_host->position.x) : 0.0,
            first_host ? static_cast<double>(first_host->position.y) : 0.0,
            g_enemy_visual_state_path);
        g_mina->Log(message);
    }
    return host_count > 0;
}

bool visual_enemy_host_by_key(unsigned long long key, XMarkVisualEnemyHost *host_out, unsigned long long now_ms) {
    if (!key || !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }
    for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
        if (g_visual_enemy_hosts[i].active && g_visual_enemy_hosts[i].key == key) {
            if (host_out) {
                *host_out = g_visual_enemy_hosts[i];
            }
            return true;
        }
    }
    return false;
}

bool visual_enemy_host_matches_preferred_training(const XMarkVisualEnemyHost &host) {
    if (!host.active) {
        return false;
    }
    return
        env_token_list_contains("MINA_XMARK_VISUAL_HOST_PREFERRED_STEMS", host.stem) ||
        env_token_list_contains("MINA_XMARK_VISUAL_HOST_PREFERRED_ENTRIES", host.entry) ||
        env_token_list_contains("MINA_XMARK_VISUAL_HOST_PREFERRED_CATALOGS", host.catalog);
}

bool visual_enemy_host_preference_configured() {
    char value[8]{};
    return
        xmark_read_environment_value("MINA_XMARK_VISUAL_HOST_PREFERRED_STEMS", value, sizeof(value)) ||
        xmark_read_environment_value("MINA_XMARK_VISUAL_HOST_PREFERRED_ENTRIES", value, sizeof(value)) ||
        xmark_read_environment_value("MINA_XMARK_VISUAL_HOST_PREFERRED_CATALOGS", value, sizeof(value));
}

bool find_nearest_visual_enemy_host(XMarkVisualEnemyHost *host_out, unsigned long long now_ms) {
    if (!host_out || !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }

    const Vec3 base = official_spawn_position();
    const float max_distance = env_float("MINA_XMARK_VISUAL_HOST_MAX_DISTANCE", 160.0f);
    const float max_distance_sq = max_distance * max_distance;
    const bool prefer_trained = visual_enemy_host_preference_configured();
    bool found = false;
    XMarkVisualEnemyHost best{};
    float best_score = FLT_MAX;
    for (unsigned int pass = 0; pass < (prefer_trained ? 2u : 1u); ++pass) {
        found = false;
        best = XMarkVisualEnemyHost{};
        best_score = FLT_MAX;
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            XMarkVisualEnemyHost host = g_visual_enemy_hosts[i];
            if (!host.active) {
                continue;
            }
            const bool trained_match = visual_enemy_host_matches_preferred_training(host);
            if (prefer_trained && pass == 0 && !trained_match) {
                continue;
            }
            const float dx = host.position.x - base.x;
            const float dy = host.position.y - base.y;
            const float distance_sq = (dx * dx) + (dy * dy);
            if (distance_sq > max_distance_sq) {
                continue;
            }
            host.distance_sq = distance_sq;
            float score = distance_sq;
            if (trained_match) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
            } else if (host.stem[0] && std::strcmp(host.stem, "trooper") == 0) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_TROOPER_BONUS", 0.0f);
            }
            if (!found || score < best_score) {
                found = true;
                best = host;
                best_score = score;
            }
        }
        if (found || !prefer_trained) {
            break;
        }
    }
    if (!found) {
        return false;
    }
    *host_out = best;
    return true;
}

Vec3 visual_host_render_position(const XMarkVisualEnemyHost &host) {
    const Vec3 render_base = official_spawn_position();
    Vec3 player_api = render_base;
    if (g_mina && g_mina->PlayerGetPos) {
        __try {
            g_mina->PlayerGetPos(&player_api.x, &player_api.y);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            player_api = render_base;
        }
    }

    Vec3 position = render_base;
    if (env_bool("MINA_XMARK_VISUAL_HOST_BRIDGE_FROM_PLAYER_API", true)) {
        position.x = render_base.x + (host.position.x - player_api.x);
        if (env_bool("MINA_XMARK_VISUAL_HOST_FLIP_Y_FROM_PLAYER", true)) {
            position.y = render_base.y - (host.position.y - player_api.y);
        } else {
            position.y = render_base.y + (host.position.y - player_api.y);
        }
    } else {
        position = host.position;
        if (env_bool("MINA_XMARK_VISUAL_HOST_FLIP_Y_FROM_PLAYER", true)) {
            position.y = render_base.y - (host.position.y - render_base.y);
        }
    }
    position.x += env_float("MINA_XMARK_VISUAL_HOST_RENDER_OFFSET_X", 0.0f);
    position.y += env_float("MINA_XMARK_VISUAL_HOST_RENDER_OFFSET_Y", 0.0f);
    position.z = render_base.z + env_float("MINA_XMARK_VISUAL_HOST_RENDER_OFFSET_Z", 0.0f);
    return position;
}

Vec3 visual_host_pinned_render_position(
    XMarkAttachment &attachment,
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms) {
    Vec3 host_render_position = visual_host_render_position(host);
    const unsigned long long host_seen_ms = host.last_seen_ms ? host.last_seen_ms : now_ms;
    if (attachment.last_visual_host_seen_ms &&
        host_seen_ms > attachment.last_visual_host_seen_ms) {
        const unsigned long long delta_ms = host_seen_ms - attachment.last_visual_host_seen_ms;
        const float max_sample_jump =
            env_float("MINA_XMARK_VISUAL_FOLLOW_VELOCITY_SAMPLE_MAX_JUMP", 4.0f);
        const float dx = host_render_position.x - attachment.last_visual_host_render_position.x;
        const float dy = host_render_position.y - attachment.last_visual_host_render_position.y;
        if (delta_ms > 0 &&
            std::isfinite(dx) &&
            std::isfinite(dy) &&
            dx * dx + dy * dy <= max_sample_jump * max_sample_jump) {
            attachment.visual_velocity_x = dx / static_cast<float>(delta_ms);
            attachment.visual_velocity_y = dy / static_cast<float>(delta_ms);
            attachment.has_visual_velocity = true;
        } else {
            attachment.visual_velocity_x = 0.0f;
            attachment.visual_velocity_y = 0.0f;
            attachment.has_visual_velocity = false;
        }
        attachment.last_visual_host_render_position = host_render_position;
        attachment.last_visual_host_seen_ms = host_seen_ms;
    } else if (!attachment.last_visual_host_seen_ms) {
        attachment.last_visual_host_render_position = host_render_position;
        attachment.last_visual_host_seen_ms = host_seen_ms;
        attachment.visual_velocity_x = 0.0f;
        attachment.visual_velocity_y = 0.0f;
        attachment.has_visual_velocity = false;
    } else if (host_seen_ms == attachment.last_visual_host_seen_ms &&
               attachment.has_visual_velocity &&
               now_ms > host_seen_ms) {
        const unsigned int max_extrapolate_ms =
            std::max(16u, env_uint("MINA_XMARK_VISUAL_FOLLOW_EXTRAPOLATE_MAX_MS", 160));
        const unsigned long long extrapolate_ms =
            std::min<unsigned long long>(now_ms - host_seen_ms, max_extrapolate_ms);
        const float max_speed_per_ms =
            std::max(0.001f, env_float("MINA_XMARK_VISUAL_FOLLOW_MAX_SPEED_PER_MS", 0.05f));
        const float vx = std::max(-max_speed_per_ms, std::min(max_speed_per_ms, attachment.visual_velocity_x));
        const float vy = std::max(-max_speed_per_ms, std::min(max_speed_per_ms, attachment.visual_velocity_y));
        host_render_position.x += vx * static_cast<float>(extrapolate_ms);
        host_render_position.y += vy * static_cast<float>(extrapolate_ms);
    }
    return host_render_position;
}

Vec3 render_position_to_visual_host_space(const Vec3 &render_position) {
    const Vec3 render_base = official_spawn_position();
    Vec3 player_api = render_base;
    if (g_mina && g_mina->PlayerGetPos) {
        __try {
            g_mina->PlayerGetPos(&player_api.x, &player_api.y);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            player_api = render_base;
        }
    }

    Vec3 position = render_position;
    if (env_bool("MINA_XMARK_VISUAL_HOST_BRIDGE_FROM_PLAYER_API", true)) {
        position.x = player_api.x + (render_position.x - render_base.x);
        if (env_bool("MINA_XMARK_VISUAL_HOST_FLIP_Y_FROM_PLAYER", true)) {
            position.y = player_api.y - (render_position.y - render_base.y);
        } else {
            position.y = player_api.y + (render_position.y - render_base.y);
        }
    }
    position.z = 0.0f;
    return position;
}

float xmark_default_render_half_w() {
    return env_float("MINA_XMARK_RENDER_HALF_WIDTH", 0.45f);
}

float xmark_default_render_half_h() {
    return env_float("MINA_XMARK_RENDER_HALF_HEIGHT", 0.45f);
}

float xmark_visual_host_units_per_pixel() {
    return std::max(0.001f, env_float("MINA_XMARK_VISUAL_HOST_UNITS_PER_PIXEL", 0.1f));
}

float xmark_clamp_render_half(float value) {
    const float min_half = env_float("MINA_XMARK_VISUAL_HOST_MARK_MIN_HALF", 0.8f);
    const float max_half = env_float("MINA_XMARK_VISUAL_HOST_MARK_MAX_HALF", 0.85f);
    return std::min(max_half, std::max(min_half, value));
}

void xmark_clamp_render_halves_preserve_aspect(float *half_w, float *half_h) {
    if (!half_w || !half_h) {
        return;
    }

    float w = *half_w;
    float h = *half_h;
    if (!std::isfinite(w) || w <= 0.0f) {
        w = xmark_default_render_half_w();
    }
    if (!std::isfinite(h) || h <= 0.0f) {
        h = xmark_default_render_half_h();
    }

    const float min_half = std::max(0.001f, env_float("MINA_XMARK_VISUAL_HOST_MARK_MIN_HALF", 0.8f));
    const float max_half = std::max(min_half, env_float("MINA_XMARK_VISUAL_HOST_MARK_MAX_HALF", 0.85f));
    const float largest = std::max(w, h);
    if (largest > 0.0f && largest > max_half) {
        const float scale = max_half / largest;
        w *= scale;
        h *= scale;
    } else if (largest > 0.0f && largest < min_half) {
        const float scale = min_half / largest;
        w *= scale;
        h *= scale;
    }

    *half_w = w;
    *half_h = h;
}

void xmark_visual_host_render_halves(const XMarkVisualEnemyHost &host, float *half_w_out, float *half_h_out) {
    float half_w = xmark_default_render_half_w();
    float half_h = xmark_default_render_half_h();
    if (env_bool("MINA_XMARK_VISUAL_HOST_MARK_SCALE_ENABLED", true) &&
        host.texture_width > 0 &&
        host.texture_height > 0) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        const float scale = env_float("MINA_XMARK_VISUAL_HOST_MARK_SCALE", 0.5f);
        half_w = static_cast<float>(host.texture_width) * units_per_pixel * scale * 0.5f;
        half_h = static_cast<float>(host.texture_height) * units_per_pixel * scale * 0.5f;
        if (env_bool("MINA_XMARK_VISUAL_HOST_MARK_SQUARE", false)) {
            const float square_half = std::max(half_w, half_h);
            half_w = square_half;
            half_h = square_half;
        }
    }
    if (env_bool("MINA_XMARK_VISUAL_HOST_MARK_PRESERVE_MARK_ASPECT", true)) {
        const float marker_aspect = std::max(0.01f, env_float("MINA_XMARK_VISUAL_HOST_MARK_ASPECT", 1.0f));
        if (half_w > 0.0f && half_h > 0.0f) {
            if (half_w / half_h > marker_aspect) {
                half_w = half_h * marker_aspect;
            } else {
                half_h = half_w / marker_aspect;
            }
        }
    }
    xmark_clamp_render_halves_preserve_aspect(&half_w, &half_h);
    if (env_bool("MINA_XMARK_VISUAL_HOST_MARK_PIXEL_SNAP", true)) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        const float source_w_px = std::max(1.0f, env_float("MINA_XMARK_VISUAL_HOST_MARK_SOURCE_W_PX", 16.0f));
        const float source_h_px = std::max(1.0f, env_float("MINA_XMARK_VISUAL_HOST_MARK_SOURCE_H_PX", 16.0f));
        const float min_scale = std::max(1.0f, env_float("MINA_XMARK_VISUAL_HOST_MARK_MIN_PIXEL_SCALE", 1.0f));
        const float current_scale = std::max(
            min_scale,
            std::max((half_w * 2.0f) / (source_w_px * units_per_pixel),
                     (half_h * 2.0f) / (source_h_px * units_per_pixel)));
        const float snapped_w_px = std::max(source_w_px * min_scale, std::round(source_w_px * current_scale));
        const float snapped_h_px = std::max(source_h_px * min_scale, std::round(source_h_px * current_scale));
        half_w = snapped_w_px * units_per_pixel * 0.5f;
        half_h = snapped_h_px * units_per_pixel * 0.5f;
    }
    if (half_w_out) {
        *half_w_out = half_w;
    }
    if (half_h_out) {
        *half_h_out = half_h;
    }
}

void xmark_visual_host_sprite_halves(const XMarkVisualEnemyHost &host, float *half_w_out, float *half_h_out) {
    float half_w = host.half_w > 0.0f && std::isfinite(host.half_w) ? host.half_w : 0.0f;
    float half_h = host.half_h > 0.0f && std::isfinite(host.half_h) ? host.half_h : 0.0f;
    if (host.texture_width > 0 && host.texture_height > 0) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        half_w = std::max(half_w, static_cast<float>(host.texture_width) * units_per_pixel * 0.5f);
        half_h = std::max(half_h, static_cast<float>(host.texture_height) * units_per_pixel * 0.5f);
    }
    if (!std::isfinite(half_w) || half_w <= 0.0f) {
        half_w = std::max(0.001f, env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_W", 0.55f));
    }
    if (!std::isfinite(half_h) || half_h <= 0.0f) {
        half_h = std::max(0.001f, env_float("MINA_XMARK_BURN_FALLBACK_SHADE_HALF_H", 0.55f));
    }
    if (env_bool("MINA_XMARK_BURN_SHADE_PIXEL_SNAP", true)) {
        const float units_per_pixel = xmark_visual_host_units_per_pixel();
        half_w = std::round((half_w * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
        half_h = std::round((half_h * 2.0f) / units_per_pixel) * units_per_pixel * 0.5f;
    }
    if (half_w_out) {
        *half_w_out = std::max(0.001f, half_w);
    }
    if (half_h_out) {
        *half_h_out = std::max(0.001f, half_h);
    }
}

bool find_nearest_visual_enemy_host_to_visual_position(
    const Vec3 &visual_position,
    XMarkVisualEnemyHost *host_out,
    unsigned long long now_ms,
    const XMarkAttachment *attachment = nullptr) {
    if (!host_out || !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }

    const float max_distance = attachment
        ? env_float("MINA_XMARK_VISUAL_FOLLOW_REACQUIRE_MAX_DISTANCE", 48.0f)
        : env_float("MINA_XMARK_BASIC_HEALTH_VISUAL_HOST_MAX_DISTANCE", 8.0f);
    const float max_distance_sq = max_distance * max_distance;
    bool found = false;
    XMarkVisualEnemyHost best{};
    float best_score = FLT_MAX;
    const bool prefer_trained = visual_enemy_host_preference_configured();
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
            bool identity_match = false;
            if (attachment) {
                identity_match =
                    (attachment->visual_stem[0] && host.stem[0] && std::strcmp(attachment->visual_stem, host.stem) == 0) ||
                    (attachment->visual_entry[0] && host.entry[0] && std::strcmp(attachment->visual_entry, host.entry) == 0) ||
                    (attachment->visual_catalog[0] && host.catalog[0] && std::strcmp(attachment->visual_catalog, host.catalog) == 0);
                if (env_bool("MINA_XMARK_VISUAL_FOLLOW_REACQUIRE_REQUIRE_IDENTITY", true) && !identity_match) {
                    continue;
                }
            }
            const float dx = host.position.x - visual_position.x;
            const float dy = host.position.y - visual_position.y;
            const float distance_sq = (dx * dx) + (dy * dy);
            if (distance_sq > max_distance_sq) {
                continue;
            }
            float score = distance_sq;
            if (trained_match) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
            }
            if (identity_match) {
                score -= env_float("MINA_XMARK_VISUAL_FOLLOW_REACQUIRE_IDENTITY_BONUS", 250000.0f);
            }
            if (!found || score < best_score) {
                found = true;
                best = host;
                best_score = score;
            }
        }
        if (found || !prefer_trained) {
            break;
        }
    }
    if (!found) {
        return false;
    }
    best.distance_sq = std::max(0.0f, best_score);
    *host_out = best;
    return true;
}

bool runtime_target_from_visual_host(const XMarkVisualEnemyHost &host, XMarkRuntimeTarget *target_out) {
    if (!target_out || !host.active || !host.key) {
        return false;
    }
    const Vec3 base = official_spawn_position();
    const Vec3 render_position = visual_host_render_position(host);
    XMarkRuntimeTarget target{};
    target.entity = static_cast<uintptr_t>(0xD400000000000000ull | (host.key & 0x00FFFFFFFFFFFFFFull));
    target.visual_key = host.key;
    target.visual_follow = true;
    target.anchor = render_position;
    target.position = render_position;
    const float dx = target.position.x - base.x;
    const float dy = target.position.y - base.y;
    target.distance_sq = (dx * dx) + (dy * dy);
    target.score = target.distance_sq;
    target.facing_match = true;
    target.health_like = false;
    xmark_visual_host_render_halves(host, &target.render_half_w, &target.render_half_h);
    target.visual_texture_width = host.texture_width;
    target.visual_texture_height = host.texture_height;
    copy_visual_token(target.visual_entry, sizeof(target.visual_entry), host.entry);
    copy_visual_token(target.visual_stem, sizeof(target.visual_stem), host.stem);
    copy_visual_token(target.visual_catalog, sizeof(target.visual_catalog), host.catalog);
    *target_out = target;
    return true;
}

bool find_nearest_visual_enemy_host_to_render_position(
    const Vec3 &render_position,
    XMarkVisualEnemyHost *host_out,
    unsigned long long now_ms,
    const char *reason) {
    if (host_out) {
        *host_out = XMarkVisualEnemyHost{};
    }
    if (!host_out || !read_enemy_visual_state_file(now_ms, false)) {
        return false;
    }

    const Vec3 visual_position = render_position_to_visual_host_space(render_position);
    const float max_visual_distance =
        std::max(0.1f, env_float("MINA_XMARK_OFFICIAL_VISUAL_LINK_MAX_VISUAL_DISTANCE", 48.0f));
    const float max_render_distance =
        std::max(0.1f, env_float("MINA_XMARK_OFFICIAL_VISUAL_LINK_MAX_RENDER_DISTANCE", 48.0f));
    const float max_visual_distance_sq = max_visual_distance * max_visual_distance;
    const float max_render_distance_sq = max_render_distance * max_render_distance;
    const bool prefer_trained = visual_enemy_host_preference_configured();

    bool found = false;
    XMarkVisualEnemyHost best{};
    Vec3 best_render_position{};
    float best_score = FLT_MAX;
    float best_visual_distance_sq = FLT_MAX;
    float best_render_distance_sq = FLT_MAX;
    for (unsigned int pass = 0; pass < (prefer_trained ? 2u : 1u); ++pass) {
        for (unsigned int i = 0; i < g_visual_enemy_host_count; ++i) {
            const XMarkVisualEnemyHost &host = g_visual_enemy_hosts[i];
            if (!host.active || !host.key) {
                continue;
            }
            const bool trained_match = visual_enemy_host_matches_preferred_training(host);
            if (prefer_trained && pass == 0 && !trained_match) {
                continue;
            }

            const float visual_dx = host.position.x - visual_position.x;
            const float visual_dy = host.position.y - visual_position.y;
            const float visual_distance_sq = (visual_dx * visual_dx) + (visual_dy * visual_dy);
            if (visual_distance_sq > max_visual_distance_sq) {
                continue;
            }

            const Vec3 host_render_position = visual_host_render_position(host);
            const float render_dx = host_render_position.x - render_position.x;
            const float render_dy = host_render_position.y - render_position.y;
            const float render_distance_sq = (render_dx * render_dx) + (render_dy * render_dy);
            if (render_distance_sq > max_render_distance_sq) {
                continue;
            }

            float score = visual_distance_sq + render_distance_sq * 0.5f;
            if (trained_match) {
                score -= env_float("MINA_XMARK_VISUAL_HOST_PREFERRED_BONUS", 100000.0f);
            }
            if (!found || score < best_score) {
                found = true;
                best = host;
                best_render_position = host_render_position;
                best_score = score;
                best_visual_distance_sq = visual_distance_sq;
                best_render_distance_sq = render_distance_sq;
            }
        }
        if (found || !prefer_trained) {
            break;
        }
    }

    if (!found) {
        return false;
    }

    best.distance_sq = std::max(0.0f, best_score);
    *host_out = best;
    if (g_mina && env_bool("MINA_XMARK_OFFICIAL_VISUAL_LINK_LOG", false)) {
        char message[704]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn official visual link reason=%s key=0x%llX entry=%s stem=%s render=(%.3f, %.3f) hostRender=(%.3f, %.3f) visualDist=%.3f renderDist=%.3f\n",
            reason ? reason : "<none>",
            best.key,
            best.entry[0] ? best.entry : "-",
            best.stem[0] ? best.stem : "-",
            static_cast<double>(render_position.x),
            static_cast<double>(render_position.y),
            static_cast<double>(best_render_position.x),
            static_cast<double>(best_render_position.y),
            static_cast<double>(std::sqrt(std::max(0.0f, best_visual_distance_sq))),
            static_cast<double>(std::sqrt(std::max(0.0f, best_render_distance_sq))));
        g_mina->Log(message);
    }
    return true;
}

void apply_visual_host_identity_to_runtime_target_fields(
    const XMarkVisualEnemyHost &host,
    XMarkRuntimeTarget *target) {
    if (!target || !host.active || !host.key) {
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

bool apply_nearest_visual_host_identity_to_runtime_target(
    const Vec3 &render_position,
    XMarkRuntimeTarget *target,
    unsigned long long now_ms,
    const char *reason) {
    XMarkVisualEnemyHost host{};
    if (!find_nearest_visual_enemy_host_to_render_position(render_position, &host, now_ms, reason)) {
        return false;
    }
    apply_visual_host_identity_to_runtime_target_fields(host, target);
    return true;
}

void apply_visual_host_identity_to_attachment_fields(
    XMarkAttachment &attachment,
    const XMarkVisualEnemyHost &host,
    const Vec3 &source_render_position,
    unsigned long long now_ms,
    bool recenter) {
    if (!host.active || !host.key) {
        return;
    }
    const Vec3 host_render_position = visual_host_render_position(host);
    attachment.visual_key = host.key;
    attachment.visual_follow = true;
    attachment.runtime_visual_offset.x = host_render_position.x - source_render_position.x;
    attachment.runtime_visual_offset.y = host_render_position.y - source_render_position.y;
    attachment.runtime_visual_offset.z = host_render_position.z - source_render_position.z;
    attachment.has_runtime_visual_offset = true;
    attachment.last_visual_host_render_position = host_render_position;
    attachment.last_visual_host_seen_ms = host.last_seen_ms ? host.last_seen_ms : now_ms;
    attachment.last_visual_resolved_ms = now_ms;
    attachment.last_visual_missing_ms = 0;
    attachment.visual_missing_count = 0;
    attachment.visual_velocity_x = 0.0f;
    attachment.visual_velocity_y = 0.0f;
    attachment.has_visual_velocity = false;
    xmark_visual_host_render_halves(host, &attachment.render_half_w, &attachment.render_half_h);
    copy_visual_token(attachment.visual_entry, sizeof(attachment.visual_entry), host.entry);
    copy_visual_token(attachment.visual_stem, sizeof(attachment.visual_stem), host.stem);
    copy_visual_token(attachment.visual_catalog, sizeof(attachment.visual_catalog), host.catalog);
    if (recenter) {
        attachment.last_position = xmark_attachment_mark_position(host_render_position);
        attachment.has_last_position = true;
    }
}

bool apply_nearest_visual_host_identity_to_attachment(
    XMarkAttachment &attachment,
    const Vec3 &render_position,
    unsigned long long now_ms,
    const char *reason,
    bool recenter) {
    XMarkVisualEnemyHost host{};
    if (!find_nearest_visual_enemy_host_to_render_position(render_position, &host, now_ms, reason)) {
        return false;
    }
    apply_visual_host_identity_to_attachment_fields(attachment, host, render_position, now_ms, recenter);
    return true;
}

bool xmark_hud_target_health(uintptr_t target, float *health_out, float *max_out) {
    if (health_out) {
        *health_out = 0.0f;
    }
    if (max_out) {
        *max_out = 0.0f;
    }
    if (!target) {
        return false;
    }

    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (!attachment.active || attachment.target != target) {
            continue;
        }
        XMarkOfficialEnemyHost host{};
        if (official_enemy_host_for_attachment(attachment, &host) && host.health_max > 0.0f) {
            if (health_out) {
                *health_out = host.health;
            }
            if (max_out) {
                *max_out = host.health_max;
            }
            return true;
        }
        if (g_mina && attachment.official_combat_core) {
            __try {
                const float health = g_mina->CombatCoreGetHealth(reinterpret_cast<ycComponent *>(attachment.official_combat_core));
                const float health_max = g_mina->CombatCoreGetHealthMax(reinterpret_cast<ycComponent *>(attachment.official_combat_core));
                if (health_max > 0.0f) {
                    if (health_out) {
                        *health_out = health;
                    }
                    if (max_out) {
                        *max_out = health_max;
                    }
                    return true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        if (attachment.target_health_offset &&
            read_health_at_offset(
                attachment.target,
                attachment.target_health_offset,
                attachment.target_health_kind,
                health_out,
                max_out) &&
            max_out &&
            *max_out > 0.0f) {
            return true;
        }
    }

    XMarkOfficialEnemyHost host{};
    if (official_enemy_host_by_entity(target, &host) && host.health_max > 0.0f) {
        if (health_out) {
            *health_out = host.health;
        }
        if (max_out) {
            *max_out = host.health_max;
        }
        return true;
    }
    return false;
}

bool xmark_target_suppresses_hud(uintptr_t target) {
    if (!target) {
        return false;
    }
    for (const XMarkAttachment &attachment : g_xmark_attachments) {
        if (attachment.active && attachment.suppress_hud &&
            (attachment.target == target || attachment.official_combat_core == target)) {
            return true;
        }
    }
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (burn.active && burn.suppress_damage &&
            (burn.target == target || burn.official_combat_core == target)) {
            return true;
        }
    }
    for (const XMarkEnemyStatusRecord &status : g_xmark_enemy_status) {
        if (status.active && status.training_target &&
            (status.entity == target || status.combat_core == target)) {
            return true;
        }
    }
    for (unsigned int i = 0; i < g_official_enemy_host_count; ++i) {
        const XMarkOfficialEnemyHost &host = g_official_enemy_hosts[i];
        if (host.active && host.muriel_no_damage &&
            (host.entity == target || host.combat_core == target)) {
            return true;
        }
    }
    return false;
}

void clear_suppressed_xmark_hud_aliases(uintptr_t target) {
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active &&
            (mark.target == target || xmark_target_suppresses_hud(mark.target))) {
            mark = {};
        }
    }
}

XMarkHudState current_xmark_hud_state(unsigned long long now_ms) {
    XMarkHudState state{};
    const unsigned int blink_out_ms = env_uint("MINA_XMARK_MARK_BLINK_OUT_MS", 1000);
    const unsigned int blink_pulse_ms = std::max(40u, env_uint("MINA_XMARK_HUD_BLINK_MS", 240));
    bool any_steady = false;
    bool any_blink_visible = false;
    bool selected_has_damage_popup = false;
    bool selected_is_burn = false;
    unsigned long long selected_expires_ms = 0;

    for (const XMarkHudMark &mark : g_xmark_hud_marks) {
        if (!mark.active || now_ms >= mark.expires_ms) {
            continue;
        }
        if (xmark_target_suppresses_hud(mark.target)) {
            continue;
        }
        const unsigned int mode = mark.mode ? mark.mode : kXMarkHudModeMark;
        const bool burn_mode = mode == kXMarkHudModeBurn;
        const bool damage_popup_active = mark.damage_popup_until_ms > now_ms;
        const bool death_linger = mark.death_linger;
        const unsigned long long blink_start = death_linger && mark.death_blink_start_ms
            ? mark.death_blink_start_ms
            : burn_mode
            ? mark.expires_ms
            : (mark.expires_ms > blink_out_ms ? mark.expires_ms - blink_out_ms : now_ms);
        bool mark_visible = true;
        ++state.active_count;
        const bool select_target =
            !state.first_target ||
            (damage_popup_active && !selected_has_damage_popup) ||
            (damage_popup_active == selected_has_damage_popup &&
             burn_mode && !selected_is_burn) ||
            (damage_popup_active == selected_has_damage_popup &&
             burn_mode == selected_is_burn &&
             mark.expires_ms > selected_expires_ms);
        if (select_target) {
            state.first_target = mark.target;
            state.first_expires_ms = mark.expires_ms;
            state.first_blink_start_ms = blink_start;
            state.mode = mode;
            selected_has_damage_popup = damage_popup_active;
            selected_is_burn = burn_mode;
            selected_expires_ms = mark.expires_ms;
            state.first_health = 0.0f;
            state.first_health_max = 0.0f;
            if (mark.last_health_max > 0.0f) {
                state.first_health = mark.last_health;
                state.first_health_max = mark.last_health_max;
            }
        }
        if (death_linger && now_ms >= blink_start) {
            state.blinking = true;
            mark_visible = ((now_ms - blink_start) / blink_pulse_ms) % 2u == 0u;
            if (mark_visible) {
                any_blink_visible = true;
            }
        } else if (burn_mode) {
            any_steady = true;
        } else if (now_ms < blink_start) {
            any_steady = true;
        } else {
            state.blinking = true;
            mark_visible = ((now_ms - blink_start) / blink_pulse_ms) % 2u == 0u;
            if (mark_visible) {
                any_blink_visible = true;
            }
        }
        if (damage_popup_active) {
            state.damage_popup_visible = true;
            state.damage_popup_until_ms = std::max(state.damage_popup_until_ms, mark.damage_popup_until_ms);
        }
    }

    state.visible = state.active_count > 0 && (any_steady || any_blink_visible);
    if (!state.active_count) {
        state.mode = kXMarkHudModeNone;
    } else if (!state.mode) {
        state.mode = kXMarkHudModeMark;
    }
    return state;
}

void upsert_xmark_hud_mark_mode(uintptr_t target, unsigned long long expires_ms, unsigned int mode) {
    if (!target || !expires_ms) {
        return;
    }
    if (xmark_target_suppresses_hud(target)) {
        clear_suppressed_xmark_hud_aliases(target);
        return;
    }
    XMarkHudMark *slot = nullptr;
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.target == target) {
            slot = &mark;
            break;
        }
    }
    if (!slot) {
        for (XMarkHudMark &mark : g_xmark_hud_marks) {
            if (!mark.active || xmark_status_now_ms() >= mark.expires_ms) {
                slot = &mark;
                break;
            }
        }
    }
    if (!slot) {
        slot = &g_xmark_hud_marks[0];
    }
    const bool same_target = slot->active && slot->target == target;
    if (!same_target) {
        slot->last_health = 0.0f;
        slot->last_health_max = 0.0f;
        slot->damage_popup_until_ms = 0;
    }
    slot->active = true;
    slot->target = target;
    slot->expires_ms = expires_ms;
    slot->mode = mode ? mode : kXMarkHudModeMark;
    slot->death_linger = false;
    slot->death_blink_start_ms = 0;
    if (slot->mode != kXMarkHudModeBurn) {
        slot->damage_popup_until_ms = 0;
    }
}

void upsert_xmark_hud_mark(uintptr_t target, unsigned long long expires_ms) {
    upsert_xmark_hud_mark_mode(target, expires_ms, kXMarkHudModeMark);
}

void set_xmark_hud_damage_popup(uintptr_t target, unsigned long long until_ms) {
    if (!target || !until_ms) {
        return;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.target == target) {
            mark.damage_popup_until_ms = std::max(mark.damage_popup_until_ms, until_ms);
            return;
        }
    }
}

void record_xmark_hud_mark_health(uintptr_t target, float health, float health_max) {
    if (!target || !(health_max > 0.0f)) {
        return;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.target == target) {
            mark.last_health = std::max(0.0f, health);
            mark.last_health_max = health_max;
            return;
        }
    }
}

void start_xmark_hud_death_linger(uintptr_t target, unsigned long long now_ms, float health_max) {
    if (!target || !env_bool("MINA_XMARK_BURN_HUD_DEATH_LINGER_ENABLED", true)) {
        return;
    }
    const unsigned int linger_ms =
        std::max(1u, env_uint("MINA_XMARK_BURN_HUD_DEATH_LINGER_MS", 900));
    const unsigned int blink_delay_ms =
        env_uint("MINA_XMARK_BURN_HUD_DEATH_BLINK_DELAY_MS", 0);
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (!mark.active || mark.target != target) {
            continue;
        }
        mark.mode = kXMarkHudModeBurn;
        mark.death_linger = true;
        mark.death_blink_start_ms = now_ms + blink_delay_ms;
        mark.expires_ms = now_ms + linger_ms;
        mark.damage_popup_until_ms = std::max(
            mark.damage_popup_until_ms,
            now_ms + env_uint("MINA_XMARK_BURN_HUD_DEATH_DAMAGE_POP_MS", 350));
        mark.last_health = 0.0f;
        if (health_max > 0.0f) {
            mark.last_health_max = health_max;
        }
        return;
    }
    upsert_xmark_hud_mark_mode(target, now_ms + linger_ms, kXMarkHudModeBurn);
    start_xmark_hud_death_linger(target, now_ms, health_max);
}

void shorten_xmark_hud_mark(uintptr_t target, unsigned long long expires_ms) {
    if (!target || !expires_ms) {
        return;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.target == target && mark.expires_ms > expires_ms) {
            mark.expires_ms = expires_ms;
        }
    }
}

void clear_xmark_hud_mark(uintptr_t target) {
    if (!target) {
        return;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.target == target) {
            mark = {};
        }
    }
}

void shift_xmark_timers(unsigned long long delta_ms) {
    if (delta_ms == 0) {
        return;
    }
    for (XMarkHudMark &mark : g_xmark_hud_marks) {
        if (mark.active && mark.expires_ms > 0) {
            mark.expires_ms += delta_ms;
        }
        if (mark.active && mark.damage_popup_until_ms > 0) {
            mark.damage_popup_until_ms += delta_ms;
        }
    }
    for (XMarkAttachment &attachment : g_xmark_attachments) {
        if (attachment.active && attachment.expires_ms > 0) {
            attachment.expires_ms += delta_ms;
        }
    }
    for (XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (!burn.active) {
            continue;
        }
        if (burn.expires_ms > 0) {
            burn.expires_ms += delta_ms;
        }
        if (burn.next_tick_ms > 0) {
            burn.next_tick_ms += delta_ms;
        }
        if (burn.last_tick_ms > 0) {
            burn.last_tick_ms += delta_ms;
        }
    }
}

void update_xmark_pause_timer(
    unsigned long long now_ms,
    unsigned int room_index,
    float room_time,
    int game_state) {
    if (!env_bool("MINA_XMARK_PAUSE_FREEZES_TIMERS", true)) {
        g_xmark_runtime_overlay_hidden_for_pause = false;
        g_xmark_pause_roomtime_still_ms = 0;
        return;
    }
    if (env_bool("MINA_XMARK_GAME_TIME_TIMERS_ENABLED", true) &&
        g_xmark_game_clock_ready) {
        bool paused = false;
        bool pause_read = false;
        if (g_mina && g_last_world && g_mina->WorldIsPaused) {
            __try {
                paused = g_mina->WorldIsPaused(g_last_world);
                pause_read = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                paused = false;
                pause_read = false;
            }
        }
        g_xmark_runtime_overlay_hidden_for_pause =
            env_bool("MINA_XMARK_HIDE_WORLD_OVERLAYS_WHILE_PAUSED", true) &&
            pause_read && paused;
        g_xmark_pause_roomtime_still_ms = paused
            ? std::min<unsigned long long>(
                5000ull,
                g_xmark_pause_roomtime_still_ms +
                    (g_xmark_timer_last_real_ms ? now_ms - g_xmark_timer_last_real_ms : 0ull))
            : 0ull;
        g_xmark_timer_last_real_ms = now_ms;
        g_xmark_timer_last_room = room_index;
        g_xmark_timer_last_room_time = room_time;
        return;
    }
    if (!g_xmark_timer_last_real_ms ||
        g_xmark_timer_last_room != room_index ||
        room_time < g_xmark_timer_last_room_time ||
        !gameplay_state_can_spawn_test_enemy(game_state)) {
        g_xmark_timer_last_real_ms = now_ms;
        g_xmark_timer_last_room = room_index;
        g_xmark_timer_last_room_time = room_time;
        g_xmark_runtime_overlay_hidden_for_pause = false;
        g_xmark_pause_roomtime_still_ms = 0;
        return;
    }

    const unsigned long long real_delta = now_ms - g_xmark_timer_last_real_ms;
    bool authoritative_paused = false;
    bool authoritative_pause_read = false;
    if (g_mina && g_last_world && g_mina->WorldIsPaused) {
        __try {
            authoritative_paused = g_mina->WorldIsPaused(g_last_world);
            authoritative_pause_read = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            authoritative_paused = false;
            authoritative_pause_read = false;
        }
    }
    if (authoritative_pause_read) {
        g_xmark_runtime_overlay_hidden_for_pause =
            env_bool("MINA_XMARK_HIDE_WORLD_OVERLAYS_WHILE_PAUSED", true) &&
            authoritative_paused;
        g_xmark_pause_roomtime_still_ms = authoritative_paused
            ? std::min<unsigned long long>(5000ull, g_xmark_pause_roomtime_still_ms + real_delta)
            : 0ull;
        const unsigned int max_shift_ms = env_uint("MINA_XMARK_PAUSE_FREEZE_MAX_SHIFT_MS", 250);
        if (authoritative_paused && real_delta > 0 && real_delta <= max_shift_ms) {
            shift_xmark_timers(real_delta);
        }
        g_xmark_timer_last_real_ms = now_ms;
        g_xmark_timer_last_room = room_index;
        g_xmark_timer_last_room_time = room_time;
        return;
    }

    const float room_delta = room_time - g_xmark_timer_last_room_time;
    const unsigned int max_shift_ms = env_uint("MINA_XMARK_PAUSE_FREEZE_MAX_SHIFT_MS", 250);
    const float epsilon = env_float("MINA_XMARK_PAUSE_ROOMTIME_EPSILON", 0.0005f);
    const bool room_time_stopped = real_delta > 0 && room_delta <= epsilon;
    if (room_time_stopped) {
        g_xmark_pause_roomtime_still_ms = std::min<unsigned long long>(
            5000ull,
            g_xmark_pause_roomtime_still_ms + real_delta);
    } else {
        g_xmark_pause_roomtime_still_ms = 0;
    }
    const unsigned int hide_after_ms = env_uint("MINA_XMARK_PAUSE_OVERLAY_HIDE_AFTER_MS", 64);
    const bool hide_immediately = hide_after_ms == 0;
    g_xmark_runtime_overlay_hidden_for_pause =
        env_bool("MINA_XMARK_HIDE_WORLD_OVERLAYS_WHILE_PAUSED", true) &&
        room_time_stopped &&
        (hide_immediately || g_xmark_pause_roomtime_still_ms >= hide_after_ms);

    if (real_delta > 0 && real_delta <= max_shift_ms && room_time_stopped) {
        shift_xmark_timers(real_delta);
    }

    g_xmark_timer_last_real_ms = now_ms;
    g_xmark_timer_last_room = room_index;
    g_xmark_timer_last_room_time = room_time;
}

bool xmark_runtime_overlays_hidden_for_pause() {
    if (!env_bool("MINA_XMARK_HIDE_WORLD_OVERLAYS_WHILE_PAUSED", true)) {
        return false;
    }
    if (g_mina && g_last_world && g_mina->WorldIsPaused) {
        bool paused = false;
        bool read = false;
        __try {
            paused = g_mina->WorldIsPaused(g_last_world);
            read = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            paused = false;
            read = false;
        }
        if (read) {
            return paused;
        }
    }
    return g_xmark_runtime_overlay_hidden_for_pause;
}

bool xmark_text_display_active() {
    if (!g_mina) {
        return false;
    }
    int game_state = -1;
    __try {
        game_state = g_mina->GetCurrentGameState();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        game_state = -1;
    }
    return game_state == GAMESTATE_TEXTDISPLAY;
}

float env_float(const char *name, float fallback) {
    xmark_environment_cache_initialize();
    if (g_xmark_environment_cache_active) {
        const XMarkEnvironmentCacheSlot *cached = xmark_environment_cache_find(name);
        return cached && cached->float_valid ? cached->float_value : fallback;
    }
    char value[64]{};
    if (!xmark_read_environment_value(name, value, sizeof(value))) {
        return fallback;
    }
    char *end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (!end || end == value) {
        return fallback;
    }
    return static_cast<float>(parsed);
}

uint32_t xmark_mix_u32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value;
}

uint32_t xmark_burn_seed(uintptr_t target, unsigned long long started_ms, unsigned int a, unsigned int b) {
    uint32_t seed = static_cast<uint32_t>(target);
    seed ^= static_cast<uint32_t>(target >> 32);
    seed ^= static_cast<uint32_t>(started_ms);
    seed ^= static_cast<uint32_t>(started_ms >> 32);
    seed ^= a * 0x9E3779B9u;
    seed ^= b * 0x85EBCA6Bu;
    return xmark_mix_u32(seed);
}

float xmark_noise01(uint32_t seed) {
    return static_cast<float>(xmark_mix_u32(seed) & 0xFFFFu) / 65535.0f;
}

float xmark_noise_signed(uint32_t seed) {
    return xmark_noise01(seed) * 2.0f - 1.0f;
}

uintptr_t exe_base() {
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
}

bool safe_read_ptr(uintptr_t address, uintptr_t *out) {
    if (!out || !address) {
        return false;
    }
    __try {
        *out = *reinterpret_cast<uintptr_t *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

bool safe_read_float(uintptr_t address, float *out) {
    if (!out || !address) {
        return false;
    }
    __try {
        *out = *reinterpret_cast<float *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0.0f;
        return false;
    }
}

bool safe_read_u32(uintptr_t address, uint32_t *out) {
    if (!out || !address) {
        return false;
    }
    __try {
        *out = *reinterpret_cast<uint32_t *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0u;
        return false;
    }
}

bool safe_read_i32(uintptr_t address, int32_t *out) {
    if (!out || !address) {
        return false;
    }
    __try {
        *out = *reinterpret_cast<int32_t *>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

bool safe_write_float(uintptr_t address, float value) {
    if (!address) {
        return false;
    }
    __try {
        *reinterpret_cast<float *>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_u32(uintptr_t address, uint32_t value) {
    if (!address) {
        return false;
    }
    __try {
        *reinterpret_cast<uint32_t *>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool probable_heap_object(uintptr_t value) {
    if (value < 0x10000) {
        return false;
    }
    const uintptr_t base = exe_base();
    if (base && value >= base && value < base + 0x2000000) {
        return false;
    }
    if ((value & 0xfffull) == 0) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(reinterpret_cast<void *>(value), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD protect = mbi.Protect & ~(PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE);
    return protect != 0 && protect != PAGE_NOACCESS;
}

bool pointer_near(uintptr_t value, uintptr_t ref, uintptr_t radius) {
    if (!value || !ref || !radius) {
        return false;
    }
    return value > ref ? value - ref <= radius : ref - value <= radius;
}

void remember_spawned_effect_pointer(uintptr_t ptr) {
    if (!ptr) {
        return;
    }
    for (unsigned int i = 0; i < g_rejected_spawned_effect_count; ++i) {
        if (g_rejected_spawned_effects[i] == ptr) {
            return;
        }
    }
    g_rejected_spawned_effects[g_rejected_spawned_effect_count % 256u] = ptr;
    ++g_rejected_spawned_effect_count;
}

bool pointer_is_spawned_effect(uintptr_t ptr) {
    if (!ptr) {
        return false;
    }
    const unsigned int count = g_rejected_spawned_effect_count < 256u ? g_rejected_spawned_effect_count : 256u;
    const uintptr_t radius = static_cast<uintptr_t>(env_uint("MINA_XMARK_G_REJECT_SPAWNED_EFFECT_NEAR_BYTES", 0x80));
    for (unsigned int i = 0; i < count; ++i) {
        if (pointer_near(ptr, g_rejected_spawned_effects[i], radius)) {
            return true;
        }
    }
    return false;
}

bool native_spawn_limited() {
    return g_native_spawn_limit != 0 && g_native_spawn_count >= g_native_spawn_limit;
}

using XMarkInputQuery = bool (*)(uint32_t);

bool safe_input_call(XMarkInputQuery fn, uint32_t input) {
    if (!fn) {
        return false;
    }
    __try {
        return fn(input);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool key_pressed(uint32_t yc_key, int win_key, bool *was_down) {
    bool down = false;
    if (g_mina) {
        down = safe_input_call(g_mina->IsKeyDown, yc_key) || safe_input_call(g_mina->IsKeyHeld, yc_key);
    }
    down = down || ((GetAsyncKeyState(win_key) & 0x8000) != 0);
    const bool pressed = down && was_down && !*was_down;
    if (was_down) {
        *was_down = down;
    }
    return pressed;
}

bool debug_key_pressed(int win_key, bool *was_down) {
    const bool down = (GetAsyncKeyState(win_key) & 0x8000) != 0;
    const bool pressed = down && was_down && !*was_down;
    if (was_down) {
        *was_down = down;
    }
    return pressed;
}

bool debug_key_is_down(int win_key) {
    return (GetAsyncKeyState(win_key) & 0x8000) != 0;
}

bool key_is_down(uint32_t yc_key, int win_key) {
    bool down = false;
    if (g_mina) {
        down = safe_input_call(g_mina->IsKeyDown, yc_key) || safe_input_call(g_mina->IsKeyHeld, yc_key);
    }
    return down || ((GetAsyncKeyState(win_key) & 0x8000) != 0);
}

bool action_is_down(uint32_t action) {
    return g_mina && (safe_input_call(g_mina->IsActionDown, action) || safe_input_call(g_mina->IsActionHeld, action));
}

bool direction_is_down(uint32_t yc_key, int win_key, uint32_t action) {
    return key_is_down(yc_key, win_key) || action_is_down(action);
}

bool mina_window_has_foreground() {
    HWND foreground = GetForegroundWindow();
    if (!foreground) {
        return false;
    }
    DWORD foreground_pid = 0;
    GetWindowThreadProcessId(foreground, &foreground_pid);
    return foreground_pid == GetCurrentProcessId();
}

bool debug_hotkeys_allowed() {
    return !env_bool("MINA_XMARK_REQUIRE_FOCUS_FOR_DEBUG_KEYS", true) || mina_window_has_foreground();
}

void reset_debug_key_latches() {
    g_win_g_was_down = false;
    g_win_h_was_down = false;
    g_win_m_was_down = false;
    g_win_n_was_down = false;
    g_win_t_was_down = false;
}

const char *direction_name(int direction) {
    switch (direction) {
    case FacingLeft:
        return "left";
    case FacingUp:
        return "up";
    case FacingDown:
        return "down";
    case FacingRight:
    default:
        return "right";
    }
}

bool xmark_direction_is_side(int direction) {
    return direction == FacingLeft || direction == FacingRight;
}

bool xmark_direction_is_vertical(int direction) {
    return direction == FacingUp || direction == FacingDown;
}

int resolve_side_direction_from_basic_frame_geometry(const XMarkBasicFrameState &state, int fallback_direction) {
    if (env_bool("MINA_XMARK_F0029_D3D_TRUST_FRAME_DIRECTION", false) &&
        xmark_direction_is_side(state.direction)) {
        return state.direction;
    }
    if (!env_bool("MINA_XMARK_F0029_D3D_RESOLVE_SIDE_FROM_CONTACT", true)) {
        return fallback_direction;
    }
    if (!xmark_direction_is_side(fallback_direction)) {
        fallback_direction = xmark_direction_is_side(state.direction) ? state.direction : g_last_side_direction;
    }

    const Vec3 base = official_spawn_position();
    const float epsilon = std::max(0.0f, env_float("MINA_XMARK_F0029_D3D_SIDE_CONTACT_EPSILON", 0.08f));
    if (state.has_contact && std::isfinite(state.contact_x)) {
        const float dx = state.contact_x - base.x;
        if (std::fabs(dx) > epsilon) {
            return dx < 0.0f ? FacingLeft : FacingRight;
        }
    }
    if (state.has_geometry &&
        std::isfinite(state.min_x) &&
        std::isfinite(state.max_x) &&
        state.max_x > state.min_x) {
        const float center_x = (state.min_x + state.max_x) * 0.5f;
        const float dx = center_x - base.x;
        if (std::fabs(dx) > epsilon) {
            return dx < 0.0f ? FacingLeft : FacingRight;
        }
    }
    return fallback_direction;
}

bool xmark_direction_allowed_for_basic_smear_lock(int direction, bool side_only) {
    return side_only ? xmark_direction_is_side(direction) : (xmark_direction_is_side(direction) || xmark_direction_is_vertical(direction));
}

bool basic_mark_requires_health_drop() {
    return env_bool("MINA_XMARK_BASIC_REQUIRE_HEALTH_DROP_FOR_MARK", true);
}

void reset_basic_smear_animation_lock() {
    g_basic_smear_animation_lock = {};
}

int lock_basic_smear_direction_from_animation(
    unsigned long long now_ms,
    const XMarkBasicFrameState &state,
    bool side_only,
    bool has_recent_attack_press,
    unsigned long long attack_press_ms) {
    if (!env_bool("MINA_XMARK_BASIC_SMEAR_ANIMATION_LOCK_ENABLED", true) ||
        !xmark_direction_allowed_for_basic_smear_lock(state.direction, side_only)) {
        return state.direction;
    }

    const unsigned int lock_ms = env_uint("MINA_XMARK_BASIC_SMEAR_ANIMATION_LOCK_MS", 360);
    const bool lock_expired =
        g_basic_smear_animation_lock.active &&
        lock_ms &&
        now_ms >= g_basic_smear_animation_lock.started_ms + lock_ms;
    const bool different_attack =
        g_basic_smear_animation_lock.active &&
        has_recent_attack_press &&
        attack_press_ms &&
        g_basic_smear_animation_lock.attack_press_ms &&
        attack_press_ms != g_basic_smear_animation_lock.attack_press_ms;
    const bool direction_not_allowed =
        g_basic_smear_animation_lock.active &&
        !xmark_direction_allowed_for_basic_smear_lock(g_basic_smear_animation_lock.direction, side_only);
    const bool observed_frame_direction_changed =
        g_basic_smear_animation_lock.active &&
        env_bool("MINA_XMARK_BASIC_SMEAR_ANIMATION_LOCK_RESET_ON_FRAME_DIRECTION_CHANGE", false) &&
        xmark_direction_allowed_for_basic_smear_lock(state.direction, side_only) &&
        g_basic_smear_animation_lock.direction != state.direction;

    if (!g_basic_smear_animation_lock.active ||
        lock_expired ||
        different_attack ||
        direction_not_allowed ||
        observed_frame_direction_changed) {
        g_basic_smear_animation_lock.active = true;
        g_basic_smear_animation_lock.attack_press_ms = has_recent_attack_press ? attack_press_ms : 0;
        g_basic_smear_animation_lock.source_tick = state.tick;
        g_basic_smear_animation_lock.draw = state.draw;
        g_basic_smear_animation_lock.started_ms = now_ms;
        g_basic_smear_animation_lock.direction = state.direction;
        std::snprintf(
            g_basic_smear_animation_lock.frame,
            sizeof(g_basic_smear_animation_lock.frame),
            "%s",
            state.frame[0] ? state.frame : "");
    }

    return g_basic_smear_animation_lock.direction;
}

void update_last_direction_from_input() {
    if (direction_is_down(YC_KEY_LEFT, VK_LEFT, kGameAct_DpadLeft) || key_is_down(YC_KEY_A, 'A')) {
        g_last_direction = FacingLeft;
        g_last_side_direction = FacingLeft;
    } else if (direction_is_down(YC_KEY_RIGHT, VK_RIGHT, kGameAct_DpadRight) || key_is_down(YC_KEY_D, 'D')) {
        g_last_direction = FacingRight;
        g_last_side_direction = FacingRight;
    } else if (direction_is_down(YC_KEY_UP, VK_UP, kGameAct_DpadUp) || key_is_down(YC_KEY_W, 'W')) {
        g_last_direction = FacingUp;
    } else if (direction_is_down(YC_KEY_DOWN, VK_DOWN, kGameAct_DpadDown) || key_is_down(YC_KEY_S, 'S')) {
        g_last_direction = FacingDown;
    }
}

int current_side_facing_from_input_or_memory() {
    if (direction_is_down(YC_KEY_LEFT, VK_LEFT, kGameAct_DpadLeft) || key_is_down(YC_KEY_A, 'A')) {
        g_last_side_direction = FacingLeft;
        return FacingLeft;
    }
    if (direction_is_down(YC_KEY_RIGHT, VK_RIGHT, kGameAct_DpadRight) || key_is_down(YC_KEY_D, 'D')) {
        g_last_side_direction = FacingRight;
        return FacingRight;
    }
    if (g_last_direction == FacingLeft || g_last_direction == FacingRight) {
        g_last_side_direction = g_last_direction;
        return g_last_direction;
    }
    return g_last_side_direction;
}

int current_attack_direction_from_input() {
    if (direction_is_down(YC_KEY_UP, VK_UP, kGameAct_DpadUp) || key_is_down(YC_KEY_W, 'W')) {
        return FacingUp;
    }
    if (direction_is_down(YC_KEY_DOWN, VK_DOWN, kGameAct_DpadDown) || key_is_down(YC_KEY_S, 'S')) {
        return FacingDown;
    }
    if (direction_is_down(YC_KEY_LEFT, VK_LEFT, kGameAct_DpadLeft) || key_is_down(YC_KEY_A, 'A')) {
        return FacingLeft;
    }
    if (direction_is_down(YC_KEY_RIGHT, VK_RIGHT, kGameAct_DpadRight) || key_is_down(YC_KEY_D, 'D')) {
        return FacingRight;
    }
    return g_last_direction;
}

bool xmark_game_anim_api_available() {
    return g_mina &&
        g_mina->Hash64 &&
        g_mina->PlayerGetComponent &&
        g_mina->ComponentGetParent &&
        g_mina->EntityGetChildren &&
        g_mina->ComponentGetType &&
        g_mina->ComponentIsa &&
        g_mina->GameAnimGetSeqNameNoDir;
}

bool xmark_game_anim_init_rtti() {
    if (g_game_anim_rtti_initialized) {
        return rtti_valid(g_game_anim_rtti);
    }
    g_game_anim_rtti_initialized = true;
    if (!xmark_game_anim_api_available()) {
        return false;
    }
    __try {
        g_game_anim_rtti = rtti_from_hash(g_mina->Hash64("GameAnim", 8));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_game_anim_rtti = {};
        return false;
    }
    return rtti_valid(g_game_anim_rtti);
}

void copy_mm_string_to_cstr(char *dest, size_t dest_size, MM_StringRef ref) {
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

void append_modapi_anim_seq_for_state(const char *seq, uint32_t frame_idx) {
    if (!seq || !seq[0]) {
        return;
    }
    char token[96]{};
    std::snprintf(token, sizeof(token), "%s#%u", seq, frame_idx);
    if (std::strstr(g_modapi_anim_seq_list, token)) {
        return;
    }
    const size_t used = std::strlen(g_modapi_anim_seq_list);
    if (used + std::strlen(token) + 2 >= sizeof(g_modapi_anim_seq_list)) {
        return;
    }
    std::snprintf(
        g_modapi_anim_seq_list + used,
        sizeof(g_modapi_anim_seq_list) - used,
        "%s%s",
        used > 0 ? "," : "",
        token);
}

bool xmark_frame_from_anim_sequence(const char *seq, char *frame_out, size_t frame_out_size) {
    if (!seq || !seq[0] || !frame_out || frame_out_size == 0) {
        return false;
    }
    frame_out[0] = 0;
    static const char *kExactFrames[] = {
        "f0014",
        "f0015",
        "f0016",
        "f0017",
        "f0018",
        "f0019",
        "f0022",
        "f0023",
        "f0024",
        "f0025",
        "f0026",
        "f0027",
        "f0028",
    };
    for (const char *frame : kExactFrames) {
        if (std::strstr(seq, frame)) {
            std::snprintf(frame_out, frame_out_size, "%s", frame);
            return true;
        }
    }
    if (std::strstr(seq, "f0020") || std::strstr(seq, "f0021")) {
        std::snprintf(frame_out, frame_out_size, "%s", "f0020-f0021");
        return true;
    }
    if (std::strstr(seq, "f0025") || std::strstr(seq, "f0026") || std::strstr(seq, "f0027")) {
        std::snprintf(frame_out, frame_out_size, "%s", "f0025-f0027");
        return true;
    }
    return false;
}

bool xmark_frame_from_indexed_anim_sequence(
    const char *seq_no_dir,
    const char *seq_full,
    uint32_t frame_idx,
    char *frame_out,
    size_t frame_out_size) {
    if (!frame_out || frame_out_size == 0) {
        return false;
    }
    frame_out[0] = 0;

    if (seq_full && seq_full[0] &&
        xmark_frame_from_anim_sequence(seq_full, frame_out, frame_out_size)) {
        return true;
    }
    if (seq_no_dir && seq_no_dir[0] &&
        xmark_frame_from_anim_sequence(seq_no_dir, frame_out, frame_out_size)) {
        return true;
    }

    const char *directed = seq_full && seq_full[0] ? seq_full : "";
    const char *base = seq_no_dir && seq_no_dir[0] ? seq_no_dir : "";
    const bool allow_no_dir_fallback =
        env_bool("MINA_XMARK_MODAPI_BASIC_FRAME_ALLOW_NODIR_SMACK_FALLBACK", true);
    const bool is_side_smack =
        std::strcmp(directed, "smack") == 0;
    const bool is_down_smack =
        std::strcmp(directed, "smack_D") == 0 ||
        (allow_no_dir_fallback && !directed[0] && std::strcmp(base, "smack_D") == 0);
    const bool is_up_smack =
        std::strcmp(directed, "smack_U") == 0 ||
        (allow_no_dir_fallback && !directed[0] && std::strcmp(base, "smack_U") == 0);

    auto map_sequence_index = [&](unsigned int first_frame) -> bool {
        const unsigned int clamped_idx = std::min<unsigned int>(frame_idx, 4u);
        std::snprintf(frame_out, frame_out_size, "f%04u", first_frame + clamped_idx);
        return true;
    };

    if (is_side_smack && frame_idx <= 4u) {
        return map_sequence_index(14u);
    }
    if (is_down_smack && frame_idx <= 4u) {
        return map_sequence_index(19u);
    }
    if (is_up_smack && frame_idx <= 4u) {
        return map_sequence_index(24u);
    }

    const bool is_hammer_swing = std::strcmp(directed, "attackHammerSwing") == 0;
    const bool is_hammer_swing_down = std::strcmp(directed, "attackHammerSwing_D") == 0;
    const bool is_hammer_swing_up = std::strcmp(directed, "attackHammerSwing_U") == 0;
    if (frame_idx == 1u && (is_hammer_swing || is_hammer_swing_down || is_hammer_swing_up)) {
        const unsigned int contact_frame = is_hammer_swing_down ? 3u : (is_hammer_swing_up ? 5u : 1u);
        std::snprintf(frame_out, frame_out_size, "f%04u", contact_frame);
        return true;
    }

    if (allow_no_dir_fallback &&
        std::strcmp(base, "smack") == 0 &&
        frame_idx <= 4u) {
        const int direction = current_attack_direction_from_input();
        if (direction == FacingUp) {
            return map_sequence_index(24u);
        }
        if (direction == FacingDown) {
            return map_sequence_index(19u);
        }
        return map_sequence_index(14u);
    }
    return false;
}

int xmark_direction_from_basic_frame_name(const char *frame) {
    if (!frame || !frame[0]) {
        return current_attack_direction_from_input();
    }
    if (std::strcmp(frame, "f0019") == 0 ||
        std::strcmp(frame, "f0020-f0021") == 0 ||
        std::strcmp(frame, "f0022") == 0 ||
        std::strcmp(frame, "f0023") == 0) {
        return FacingDown;
    }
    if (std::strcmp(frame, "f0024") == 0 ||
        std::strcmp(frame, "f0025") == 0 ||
        std::strcmp(frame, "f0026") == 0 ||
        std::strcmp(frame, "f0027") == 0 ||
        std::strcmp(frame, "f0025-f0027") == 0 ||
        std::strcmp(frame, "f0028") == 0) {
        return FacingUp;
    }
    if (std::strcmp(frame, "f0003") == 0) {
        return FacingDown;
    }
    if (std::strcmp(frame, "f0005") == 0) {
        return FacingUp;
    }
    if (xmark_direction_is_side(g_attack_j_start_direction)) {
        return g_attack_j_start_direction;
    }
    if (xmark_direction_is_side(g_attack_j_active_direction)) {
        return g_attack_j_active_direction;
    }
    if (xmark_direction_is_side(g_last_side_direction)) {
        return g_last_side_direction;
    }
    return current_side_facing_from_input_or_memory();
}

void fill_modapi_basic_frame_geometry(XMarkBasicFrameState *state) {
    if (!state) {
        return;
    }
    Vec3 player{0.0f, 0.0f, 0.0f};
    if (g_mina && g_mina->PlayerGetPos) {
        g_mina->PlayerGetPos(&player.x, &player.y);
    }
    const float reach = std::max(1.0f, env_float("MINA_XMARK_MODAPI_BASIC_FRAME_REACH", 18.0f));
    const float vertical_reach = reach + std::max(
        0.0f,
        env_float("MINA_XMARK_MODAPI_BASIC_FRAME_VERTICAL_REACH_BONUS_PIXELS", 0.5f));
    const float half = std::max(1.0f, env_float("MINA_XMARK_MODAPI_BASIC_FRAME_HALF_WIDTH", 8.0f));
    state->has_contact = true;
    state->has_geometry = true;
    switch (state->direction) {
    case FacingLeft:
        state->contact_x = player.x - reach;
        state->contact_y = player.y;
        state->min_x = player.x - reach;
        state->max_x = player.x;
        state->min_y = player.y - half;
        state->max_y = player.y + half;
        break;
    case FacingUp:
        state->contact_x = player.x;
        state->contact_y = player.y - vertical_reach;
        state->min_x = player.x - half;
        state->max_x = player.x + half;
        state->min_y = player.y - vertical_reach;
        state->max_y = player.y;
        break;
    case FacingDown:
        state->contact_x = player.x;
        state->contact_y = player.y + vertical_reach;
        state->min_x = player.x - half;
        state->max_x = player.x + half;
        state->min_y = player.y;
        state->max_y = player.y + vertical_reach;
        break;
    case FacingRight:
    default:
        state->contact_x = player.x + reach;
        state->contact_y = player.y;
        state->min_x = player.x;
        state->max_x = player.x + reach;
        state->min_y = player.y - half;
        state->max_y = player.y + half;
        break;
    }
}

void cache_player_attack_anim(ycComponent *component) {
    if (!component || reinterpret_cast<uintptr_t>(component) == g_player_attack_anim_component) {
        return;
    }
    xmark_weak_ptr_destroy(g_player_attack_anim_weak);
    g_player_attack_anim_weak = xmark_weak_ptr_create(component);
    g_player_attack_anim_component = reinterpret_cast<uintptr_t>(component);
    xmark_probe_anim_properties(component);
}

void clear_player_attack_anim_cache() {
    xmark_weak_ptr_destroy(g_player_attack_anim_weak);
    g_player_attack_anim_weak = nullptr;
    g_player_attack_anim_component = 0;
    g_player_attack_anim_observed_seq[0] = 0;
    g_player_attack_anim_observed_frame_idx = 0xFFFFFFFFu;
    g_player_attack_anim_observed_loops = 0xFFFFFFFFu;
    g_player_attack_anim_observed_tick = 0;
    g_player_attack_anim_observed_draw = 0;
    g_player_attack_anim_last_read_ms = 0;
    g_player_attack_anim_last_scan_ms = 0;
    g_player_attack_anim_last_read_valid = false;
    g_player_attack_anim_last_read_state = {};
}

bool read_modapi_attack_anim_component(
    ycComponent *component,
    unsigned long long now_ms,
    XMarkBasicFrameState *state_out,
    bool cache_on_success) {
    if (!component || !state_out) {
        return false;
    }

    char seq[64]{};
    char seq_full[96]{};
    uint32_t frame_idx = 0;
    uint32_t loops_played = 0;
    uint32_t num_frames = 0;
    float frame_time = 0.0f;
    float play_rate = 1.0f;
    bool visible = true;
    bool read = false;
    __try {
        copy_mm_string_to_cstr(seq, sizeof(seq), g_mina->GameAnimGetSeqNameNoDir(component));
        if (g_mina->GameAnimGetSeqName) {
            copy_mm_string_to_cstr(seq_full, sizeof(seq_full), g_mina->GameAnimGetSeqName(component));
        }
        if (g_mina->GameAnimGetSeqFrameIdx) {
            frame_idx = g_mina->GameAnimGetSeqFrameIdx(component);
        }
        if (api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumLoopsPlayed))) {
            loops_played = g_mina->GameAnimGetNumLoopsPlayed(component);
        }
        if (api_function_field_ready(offsetof(MinaModAPI, GameAnimGetNumSeqFrames))) {
            num_frames = g_mina->GameAnimGetNumSeqFrames(component);
        }
        if (api_function_field_ready(offsetof(MinaModAPI, GameAnimGetCurrentFrameTime))) {
            frame_time = g_mina->GameAnimGetCurrentFrameTime(component);
        }
        if (api_function_field_ready(offsetof(MinaModAPI, GameAnimGetPlayRate))) {
            play_rate = g_mina->GameAnimGetPlayRate(component);
        }
        if (api_function_field_ready(offsetof(MinaModAPI, GameAnimIsVisible))) {
            visible = g_mina->GameAnimIsVisible(component);
        }
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    if (!read || (!seq[0] && !seq_full[0])) {
        return false;
    }

    const char *observed_seq = seq_full[0] ? seq_full : seq;
    const uintptr_t component_ptr = reinterpret_cast<uintptr_t>(component);
    const bool frame_changed =
        component_ptr != g_player_attack_anim_component ||
        std::strcmp(g_player_attack_anim_observed_seq, observed_seq) != 0 ||
        g_player_attack_anim_observed_frame_idx != frame_idx ||
        g_player_attack_anim_observed_loops != loops_played;
    if (frame_changed) {
        std::snprintf(
            g_player_attack_anim_observed_seq,
            sizeof(g_player_attack_anim_observed_seq),
            "%s",
            observed_seq);
        g_player_attack_anim_observed_frame_idx = frame_idx;
        g_player_attack_anim_observed_loops = loops_played;
        g_player_attack_anim_observed_tick = now_ms;
        g_player_attack_anim_observed_draw = ++g_modapi_basic_frame_counter;
    }

    std::snprintf(g_modapi_anim_last_seq, sizeof(g_modapi_anim_last_seq), "%s", seq);
    std::snprintf(g_modapi_anim_last_seq_full, sizeof(g_modapi_anim_last_seq_full), "%s", seq_full);
    g_modapi_anim_last_frame_idx = frame_idx;
    append_modapi_anim_seq_for_state(observed_seq, frame_idx);

    char frame[32]{};
    if (!visible ||
        !xmark_frame_from_indexed_anim_sequence(seq, seq_full, frame_idx, frame, sizeof(frame)) ||
        (!xmark_basic_frame_name_allowed(frame) && !xmark_charged_frame_name_allowed(frame))) {
        return false;
    }

    XMarkBasicFrameState state{};
    state.tick = g_player_attack_anim_observed_tick ? g_player_attack_anim_observed_tick : now_ms;
    state.draw = g_player_attack_anim_observed_draw;
    std::snprintf(state.frame, sizeof(state.frame), "%s", frame);
    state.direction = xmark_direction_from_basic_frame_name(state.frame);
    state.side_smear = xmark_direction_is_side(state.direction);
    state.anim_component = component_ptr;
    state.seq_frame_idx = frame_idx;
    state.num_seq_frames = num_frames;
    state.loops_played = loops_played;
    state.current_frame_time = frame_time;
    state.play_rate = play_rate;
    state.modapi_authoritative = true;
    state.anim_visible = visible;
    fill_modapi_basic_frame_geometry(&state);
    if (cache_on_success) {
        cache_player_attack_anim(component);
    }
    *state_out = state;
    return true;
}

bool scan_modapi_entity_for_basic_anim(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int max_nodes,
    unsigned long long now_ms,
    XMarkBasicFrameState *state_out) {
    if (!entity || !state_out || depth > max_depth || g_modapi_anim_scan_nodes >= max_nodes) {
        return false;
    }
    ++g_modapi_anim_scan_nodes;
    size_t child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    if (child_count == 0) {
        return false;
    }
    const size_t max_children = std::min<size_t>(child_count, env_uint("MINA_XMARK_MODAPI_PLAYER_ANIM_CHILD_CAP", 64));
    ycComponent **children = static_cast<ycComponent **>(g_mina->Alloc(sizeof(ycComponent *) * max_children));
    if (!children) {
        return false;
    }
    const size_t read_count = g_mina->EntityGetChildren(entity, children, max_children);
    const size_t limit = std::min(read_count, max_children);
    bool found = false;
    for (size_t i = 0; i < limit && !found; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        MM_Rtti component_type = g_mina->ComponentGetType(component);
        const bool is_entity =
            rtti_equal(component_type, g_official_entity_rtti) ||
            (rtti_valid(g_official_entity_rtti) && g_mina->ComponentIsa(component, g_official_entity_rtti));
        if (is_entity) {
            found = scan_modapi_entity_for_basic_anim(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                max_nodes,
                now_ms,
                state_out);
            continue;
        }
        const bool is_game_anim =
            rtti_equal(component_type, g_game_anim_rtti) ||
            (rtti_valid(g_game_anim_rtti) && g_mina->ComponentIsa(component, g_game_anim_rtti));
        if (!is_game_anim) {
            continue;
        }
        ++g_modapi_anim_scan_hits;
        found = read_modapi_attack_anim_component(component, now_ms, state_out, true);
    }
    g_mina->Free(children);
    return found;
}

bool read_modapi_player_basic_frame_state(unsigned long long now_ms, XMarkBasicFrameState *state_out) {
    if (!state_out ||
        !env_bool("MINA_XMARK_MODAPI_PLAYER_BASIC_FRAME_STATE_ENABLED", true) ||
        !xmark_game_anim_api_available() ||
        !official_enemy_init_rtti() ||
        !xmark_game_anim_init_rtti()) {
        return false;
    }
    if (g_player_attack_anim_last_read_ms == now_ms) {
        if (g_player_attack_anim_last_read_valid) {
            *state_out = g_player_attack_anim_last_read_state;
        }
        return g_player_attack_anim_last_read_valid;
    }
    g_player_attack_anim_last_read_ms = now_ms;
    g_player_attack_anim_last_read_valid = false;
    ycComponent *cached_anim = static_cast<ycComponent *>(xmark_weak_ptr_get(g_player_attack_anim_weak));
    if (!cached_anim && g_player_attack_anim_weak) {
        clear_player_attack_anim_cache();
    }
    if (!cached_anim) {
        const unsigned int attack_window_ms =
            env_uint("MINA_XMARK_MODAPI_PLAYER_ANIM_SCAN_ATTACK_WINDOW_MS", 900);
        const bool recent_attack =
            g_input_attack_down ||
            g_attack_j_latched ||
            (g_attack_j_pressed_ms &&
             now_ms >= g_attack_j_pressed_ms &&
             (!attack_window_ms || now_ms <= g_attack_j_pressed_ms + attack_window_ms));
        if (!recent_attack) {
            return false;
        }
        const unsigned int scan_poll_ms = std::max(
            16u,
            env_uint("MINA_XMARK_MODAPI_PLAYER_ANIM_SCAN_POLL_MS", 32));
        if (g_player_attack_anim_last_scan_ms &&
            now_ms >= g_player_attack_anim_last_scan_ms &&
            now_ms - g_player_attack_anim_last_scan_ms < scan_poll_ms) {
            return false;
        }
        g_player_attack_anim_last_scan_ms = now_ms;
    }
    ycComponent *player_component = nullptr;
    ycEntity *player_entity = nullptr;
    __try {
        player_component = g_mina->PlayerGetComponent();
        player_entity = player_component ? g_mina->ComponentGetParent(player_component) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        player_component = nullptr;
        player_entity = nullptr;
    }
    if (!player_entity) {
        return false;
    }
    g_modapi_anim_scan_nodes = 0;
    g_modapi_anim_scan_hits = 0;
    g_modapi_anim_seq_list[0] = 0;
    XMarkBasicFrameState state{};
    bool found = cached_anim &&
        read_modapi_attack_anim_component(cached_anim, now_ms, &state, false);
    if (!found) {
        found = scan_modapi_entity_for_basic_anim(
            player_entity,
            0,
            env_uint("MINA_XMARK_MODAPI_PLAYER_ANIM_MAX_DEPTH", 8),
            env_uint("MINA_XMARK_MODAPI_PLAYER_ANIM_MAX_NODES", 128),
            now_ms,
            &state);
    }
    if (!found) {
        return false;
    }
    if (g_mina && env_bool("MINA_XMARK_MODAPI_PLAYER_BASIC_FRAME_LOG", false)) {
        const unsigned int log_ms = env_uint("MINA_XMARK_MODAPI_PLAYER_BASIC_FRAME_LOG_MS", 250);
        if (!g_last_modapi_basic_frame_log_ms || now_ms - g_last_modapi_basic_frame_log_ms >= log_ms) {
            g_last_modapi_basic_frame_log_ms = now_ms;
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn ModAPI attack frame seq=%s frame=%s idx=%u/%u loops=%u time=%.4f rate=%.3f dir=%s cached=0x%p nodes=%u anims=%u\n",
                g_modapi_anim_last_seq_full[0]
                    ? g_modapi_anim_last_seq_full
                    : (g_modapi_anim_last_seq[0] ? g_modapi_anim_last_seq : "-"),
                state.frame[0] ? state.frame : "-",
                g_modapi_anim_last_frame_idx,
                state.num_seq_frames,
                state.loops_played,
                static_cast<double>(state.current_frame_time),
                static_cast<double>(state.play_rate),
                direction_name(state.direction),
                reinterpret_cast<void *>(state.anim_component),
                g_modapi_anim_scan_nodes,
                g_modapi_anim_scan_hits);
            g_mina->Log(message);
        }
    }
    g_player_attack_anim_last_read_state = state;
    g_player_attack_anim_last_read_valid = true;
    *state_out = state;
    return true;
}

bool read_preferred_player_attack_frame_state(
    unsigned long long now_ms,
    XMarkBasicFrameState *state_out) {
    if (!state_out) {
        return false;
    }
    if (env_bool("MINA_XMARK_MODAPI_ATTACK_STATE_AUTHORITATIVE", true) &&
        read_modapi_player_basic_frame_state(now_ms, state_out)) {
        return true;
    }
    if (read_basic_frame_state(state_out)) {
        return true;
    }
    return read_modapi_player_basic_frame_state(now_ms, state_out);
}

void init_player_hammer_anim_trace_paths() {
    if (g_player_hammer_anim_trace_flag_path[0]) {
        return;
    }
    char exe_path[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameA(nullptr, exe_path, static_cast<DWORD>(sizeof(exe_path)));
    if (!length || length >= sizeof(exe_path)) {
        return;
    }
    char *slash = std::strrchr(exe_path, '\\');
    if (!slash) {
        return;
    }
    *slash = 0;
    std::snprintf(
        g_player_hammer_anim_trace_flag_path,
        sizeof(g_player_hammer_anim_trace_flag_path),
        "%s\\mod_workspace\\graphics_probe\\player_hammer_anim_trace.enabled",
        exe_path);
    std::snprintf(
        g_player_hammer_anim_trace_output_path,
        sizeof(g_player_hammer_anim_trace_output_path),
        "%s\\mod_workspace\\graphics_probe\\player_hammer_anim_trace.tsv",
        exe_path);
}

bool player_hammer_anim_trace_sequence(const char *seq, const char *seq_full) {
    const auto relevant = [](const char *name) {
        return name && name[0] &&
            (std::strstr(name, "Wall") ||
             std::strstr(name, "wall") ||
             std::strstr(name, "Bounce") ||
             std::strstr(name, "bounce") ||
             std::strstr(name, "Hammer") ||
             std::strstr(name, "hammer") ||
             std::strstr(name, "Charge") ||
             std::strstr(name, "charge") ||
             std::strstr(name, "Boom") ||
             std::strstr(name, "boom") ||
             std::strstr(name, "Blast") ||
             std::strstr(name, "blast") ||
             std::strstr(name, "smack") ||
             std::strstr(name, "Smack"));
    };
    return relevant(seq) || relevant(seq_full);
}

PlayerHammerAnimTraceSlot *player_hammer_anim_trace_slot(uintptr_t component) {
    PlayerHammerAnimTraceSlot *free_slot = nullptr;
    for (PlayerHammerAnimTraceSlot &slot : g_player_hammer_anim_trace_slots) {
        if (slot.component == component) {
            return &slot;
        }
        if (!slot.component && !free_slot) {
            free_slot = &slot;
        }
    }
    return free_slot;
}

void write_player_hammer_anim_trace(
    unsigned long long now_ms,
    unsigned int depth,
    uintptr_t component,
    const char *seq,
    const char *seq_full,
    uint32_t frame_idx) {
    FILE *file = nullptr;
    if (!g_player_hammer_anim_trace_output_path[0] ||
        fopen_s(&file, g_player_hammer_anim_trace_output_path, "ab") != 0 ||
        !file) {
        return;
    }
    float player_x = 0.0f;
    float player_y = 0.0f;
    if (g_mina && g_mina->PlayerGetPos) {
        g_mina->PlayerGetPos(&player_x, &player_y);
    }
    std::fprintf(
        file,
        "%llu\t%u\t0x%p\t%s\t%s\t%u\t%.3f\t%.3f\n",
        now_ms,
        depth,
        reinterpret_cast<void *>(component),
        seq && seq[0] ? seq : "-",
        seq_full && seq_full[0] ? seq_full : "-",
        frame_idx,
        static_cast<double>(player_x),
        static_cast<double>(player_y));
    std::fclose(file);
}

void trace_player_hammer_anim_entity(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes,
    unsigned long long now_ms) {
    if (!entity || !nodes || depth > max_depth || *nodes >= max_nodes) {
        return;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    if (!child_count) {
        return;
    }
    const size_t child_cap = std::min<size_t>(child_count, 64);
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
    for (size_t i = 0; i < limit && *nodes < max_nodes; ++i) {
        ycComponent *component = children[i];
        if (!component) {
            continue;
        }
        MM_Rtti component_type{};
        bool is_entity = false;
        bool is_game_anim = false;
        __try {
            component_type = g_mina->ComponentGetType(component);
            is_entity = rtti_equal(component_type, g_official_entity_rtti) ||
                (rtti_valid(g_official_entity_rtti) && g_mina->ComponentIsa(component, g_official_entity_rtti));
            is_game_anim = rtti_equal(component_type, g_game_anim_rtti) ||
                (rtti_valid(g_game_anim_rtti) && g_mina->ComponentIsa(component, g_game_anim_rtti));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            is_entity = false;
            is_game_anim = false;
        }
        if (is_entity) {
            trace_player_hammer_anim_entity(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                nodes,
                max_nodes,
                now_ms);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }
        char seq[64]{};
        char seq_full[96]{};
        uint32_t frame_idx = 0;
        __try {
            copy_mm_string_to_cstr(seq, sizeof(seq), g_mina->GameAnimGetSeqNameNoDir(component));
            if (g_mina->GameAnimGetSeqName) {
                copy_mm_string_to_cstr(seq_full, sizeof(seq_full), g_mina->GameAnimGetSeqName(component));
            }
            if (g_mina->GameAnimGetSeqFrameIdx) {
                frame_idx = g_mina->GameAnimGetSeqFrameIdx(component);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            seq[0] = 0;
            seq_full[0] = 0;
            frame_idx = 0;
        }
        if (!player_hammer_anim_trace_sequence(seq, seq_full)) {
            continue;
        }
        const uintptr_t component_address = reinterpret_cast<uintptr_t>(component);
        PlayerHammerAnimTraceSlot *slot = player_hammer_anim_trace_slot(component_address);
        if (!slot) {
            continue;
        }
        const bool changed = slot->component != component_address ||
            slot->frame_idx != frame_idx ||
            std::strcmp(slot->seq, seq) != 0 ||
            std::strcmp(slot->seq_full, seq_full) != 0;
        if (!changed) {
            continue;
        }
        slot->component = component_address;
        slot->frame_idx = frame_idx;
        std::snprintf(slot->seq, sizeof(slot->seq), "%s", seq);
        std::snprintf(slot->seq_full, sizeof(slot->seq_full), "%s", seq_full);
        write_player_hammer_anim_trace(
            now_ms,
            depth,
            component_address,
            seq,
            seq_full,
            frame_idx);
    }
    g_mina->Free(children);
}

void maybe_trace_player_hammer_anims(unsigned long long now_ms) {
    if (!env_bool("MINA_XMARK_PLAYER_HAMMER_ANIM_TRACE_ALLOWED", true)) {
        g_player_hammer_anim_trace_enabled = false;
        g_player_hammer_anim_trace_was_enabled = false;
        return;
    }
    init_player_hammer_anim_trace_paths();
    if (!g_player_hammer_anim_trace_flag_path[0]) {
        return;
    }
    if (!g_player_hammer_anim_trace_last_flag_poll_ms ||
        now_ms >= g_player_hammer_anim_trace_last_flag_poll_ms + 250u) {
        g_player_hammer_anim_trace_last_flag_poll_ms = now_ms;
        g_player_hammer_anim_trace_enabled =
            GetFileAttributesA(g_player_hammer_anim_trace_flag_path) != INVALID_FILE_ATTRIBUTES;
    }
    if (!g_player_hammer_anim_trace_enabled) {
        g_player_hammer_anim_trace_was_enabled = false;
        return;
    }
    if (!xmark_game_anim_api_available() ||
        !official_enemy_init_rtti() ||
        !xmark_game_anim_init_rtti()) {
        return;
    }
    if (!g_player_hammer_anim_trace_was_enabled) {
        g_player_hammer_anim_trace_was_enabled = true;
        std::memset(
            g_player_hammer_anim_trace_slots,
            0,
            sizeof(g_player_hammer_anim_trace_slots));
        FILE *file = nullptr;
        if (fopen_s(&file, g_player_hammer_anim_trace_output_path, "wb") == 0 && file) {
            std::fprintf(file, "tick_ms\tdepth\tcomponent\tseq_no_dir\tseq_full\tseq_frame_idx\tplayer_x\tplayer_y\n");
            std::fclose(file);
        }
        g_mina->Log("XMarkBurn player Hammer animation trace enabled.\n");
    }
    ycComponent *player_component = nullptr;
    ycEntity *player_entity = nullptr;
    __try {
        player_component = g_mina->PlayerGetComponent();
        player_entity = player_component ? g_mina->ComponentGetParent(player_component) : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        player_component = nullptr;
        player_entity = nullptr;
    }
    if (!player_entity) {
        return;
    }
    unsigned int nodes = 0;
    trace_player_hammer_anim_entity(player_entity, 0, 8, &nodes, 128, now_ms);
}

void init_boom_charge_anim_trace_path() {
    if (g_boom_charge_anim_trace_output_path[0]) {
        return;
    }
    char exe_path[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameA(nullptr, exe_path, static_cast<DWORD>(sizeof(exe_path)));
    if (!length || length >= sizeof(exe_path)) {
        return;
    }
    char *slash = std::strrchr(exe_path, '\\');
    if (!slash) {
        return;
    }
    *slash = 0;
    std::snprintf(
        g_boom_charge_anim_trace_output_path,
        sizeof(g_boom_charge_anim_trace_output_path),
        "%s\\mod_workspace\\graphics_probe\\boom_charge_gameanim_trace.tsv",
        exe_path);
}

BoomChargeAnimTraceSlot *boom_charge_anim_trace_slot(uintptr_t component) {
    BoomChargeAnimTraceSlot *free_slot = nullptr;
    for (BoomChargeAnimTraceSlot &slot : g_boom_charge_anim_trace_slots) {
        if (slot.component == component) {
            return &slot;
        }
        if (!slot.component && !free_slot) {
            free_slot = &slot;
        }
    }
    return free_slot;
}

void arm_boom_charge_anim_trace(unsigned long long now_ms, unsigned long long held_ms, int direction) {
    if (!env_bool("MINA_XMARK_BOOM_RUNTIME_TRACE", false)) {
        return;
    }
    const unsigned int minimum_hold_ms =
        std::max(1u, env_uint("MINA_XMARK_BOOM_RUNTIME_TRACE_MIN_HOLD_MS", 1000));
    if (held_ms < minimum_hold_ms) {
        return;
    }
    init_boom_charge_anim_trace_path();
    if (!g_boom_charge_anim_trace_output_path[0]) {
        return;
    }

    g_boom_charge_anim_trace_active = true;
    g_boom_charge_anim_trace_started_ms = now_ms;
    g_boom_charge_anim_trace_until_ms = now_ms +
        std::max(500u, env_uint("MINA_XMARK_BOOM_RUNTIME_TRACE_WINDOW_MS", 1600));
    g_boom_charge_anim_trace_next_scan_ms = now_ms;
    g_boom_charge_anim_trace_direction = direction;
    ++g_boom_charge_anim_trace_sequence;
    std::memset(g_boom_charge_anim_trace_slots, 0, sizeof(g_boom_charge_anim_trace_slots));
    g_boom_charge_anim_trace_origin = {};
    __try {
        g_mina->PlayerGetPos(
            &g_boom_charge_anim_trace_origin.x,
            &g_boom_charge_anim_trace_origin.y);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_boom_charge_anim_trace_origin = {};
    }

    FILE *file = nullptr;
    if (fopen_s(&file, g_boom_charge_anim_trace_output_path, "wb") == 0 && file) {
        std::fprintf(
            file,
            "sequence\telapsed_ms\tdepth\tnew_component\tcomponent\tseq_no_dir\tseq_full\tframe\tframe_count\tframe_time\tplay_rate\tvisible\tpaused\tdone\tnew_frame\tworld_x\tworld_y\tworld_z\tbound_center_x\tbound_center_y\tbound_half_x\tbound_half_y\tpalette\n");
        std::fclose(file);
    }
    if (g_mina) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn Boom runtime trace armed sequence=%u dir=%s heldMs=%llu origin=(%.3f,%.3f).\n",
            g_boom_charge_anim_trace_sequence,
            direction_name(direction),
            held_ms,
            static_cast<double>(g_boom_charge_anim_trace_origin.x),
            static_cast<double>(g_boom_charge_anim_trace_origin.y));
        g_mina->Log(message);
    }
}

void trace_boom_charge_anim_entity(
    ycEntity *entity,
    unsigned int depth,
    unsigned int max_depth,
    unsigned int *nodes,
    unsigned int max_nodes,
    unsigned long long now_ms) {
    if (!entity || !nodes || depth > max_depth || *nodes >= max_nodes) {
        return;
    }
    ++*nodes;
    size_t child_count = 0;
    __try {
        child_count = g_mina->EntityGetChildren(entity, nullptr, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        child_count = 0;
    }
    const size_t child_cap = std::min<size_t>(child_count, 96u);
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

    const float radius = std::max(2.0f, env_float("MINA_XMARK_BOOM_RUNTIME_TRACE_RADIUS", 8.0f));
    const float radius_sq = radius * radius;
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
            trace_boom_charge_anim_entity(
                reinterpret_cast<ycEntity *>(component),
                depth + 1u,
                max_depth,
                nodes,
                max_nodes,
                now_ms);
            continue;
        }
        if (!is_game_anim) {
            continue;
        }

        char seq[64]{};
        char seq_full[96]{};
        uint32_t frame_idx = 0;
        uint32_t frame_count = 0;
        float frame_time = 0.0f;
        float play_rate = 1.0f;
        bool visible = true;
        bool paused = false;
        bool done = false;
        bool new_frame = false;
        MM_Transform world{};
        MM_AABB bound{};
        ycPaletteTexture *palette = nullptr;
        bool read = false;
        __try {
            copy_mm_string_to_cstr(seq, sizeof(seq), g_mina->GameAnimGetSeqNameNoDir(component));
            if (g_mina->GameAnimGetSeqName) {
                copy_mm_string_to_cstr(seq_full, sizeof(seq_full), g_mina->GameAnimGetSeqName(component));
            }
            frame_idx = g_mina->GameAnimGetSeqFrameIdx(component);
            frame_count = g_mina->GameAnimGetNumSeqFrames(component);
            frame_time = g_mina->GameAnimGetCurrentFrameTime(component);
            play_rate = g_mina->GameAnimGetPlayRate(component);
            visible = g_mina->GameAnimIsVisible(component);
            paused = g_mina->GameAnimIsPaused(component);
            done = g_mina->GameAnimIsDone(component);
            new_frame = g_mina->GameAnimIsNewFrame(component);
            g_mina->GameAnimGetWorldTransform(component, &world);
            g_mina->GameAnimGetCurrentFrameBound(component, &bound);
            palette = g_mina->GameAnimGetPalette(component);
            read = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            read = false;
        }
        if (!read) {
            continue;
        }
        const float dx = world.t.x - g_boom_charge_anim_trace_origin.x;
        const float dy = world.t.y - g_boom_charge_anim_trace_origin.y;
        if (dx * dx + dy * dy > radius_sq) {
            continue;
        }

        const uintptr_t component_address = reinterpret_cast<uintptr_t>(component);
        BoomChargeAnimTraceSlot *slot = boom_charge_anim_trace_slot(component_address);
        if (!slot) {
            continue;
        }
        const bool first_seen = slot->component != component_address;
        const bool changed = first_seen ||
            slot->frame_idx != frame_idx ||
            slot->visible != visible ||
            std::strcmp(slot->seq, seq) != 0 ||
            std::strcmp(slot->seq_full, seq_full) != 0;
        if (!changed) {
            continue;
        }
        slot->component = component_address;
        slot->frame_idx = frame_idx;
        slot->visible = visible;
        std::snprintf(slot->seq, sizeof(slot->seq), "%s", seq);
        std::snprintf(slot->seq_full, sizeof(slot->seq_full), "%s", seq_full);

        FILE *file = nullptr;
        if (fopen_s(&file, g_boom_charge_anim_trace_output_path, "ab") == 0 && file) {
            std::fprintf(
                file,
                "%u\t%llu\t%u\t%u\t0x%p\t%s\t%s\t%u\t%u\t%.5f\t%.3f\t%u\t%u\t%u\t%u\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t0x%p\n",
                g_boom_charge_anim_trace_sequence,
                now_ms - g_boom_charge_anim_trace_started_ms,
                depth,
                first_seen ? 1u : 0u,
                reinterpret_cast<void *>(component),
                seq[0] ? seq : "-",
                seq_full[0] ? seq_full : "-",
                frame_idx,
                frame_count,
                static_cast<double>(frame_time),
                static_cast<double>(play_rate),
                visible ? 1u : 0u,
                paused ? 1u : 0u,
                done ? 1u : 0u,
                new_frame ? 1u : 0u,
                static_cast<double>(world.t.x),
                static_cast<double>(world.t.y),
                static_cast<double>(world.t.z),
                static_cast<double>(bound.center.x),
                static_cast<double>(bound.center.y),
                static_cast<double>(bound.extents.x),
                static_cast<double>(bound.extents.y),
                reinterpret_cast<void *>(palette));
            std::fclose(file);
        }
    }
    g_mina->Free(children);
}

void update_boom_charge_anim_trace(unsigned long long now_ms) {
    if (!g_boom_charge_anim_trace_active) {
        return;
    }
    if (now_ms > g_boom_charge_anim_trace_until_ms) {
        g_boom_charge_anim_trace_active = false;
        if (g_mina) {
            g_mina->Log("XMarkBurn Boom runtime trace complete.\n");
        }
        return;
    }
    if (now_ms < g_boom_charge_anim_trace_next_scan_ms) {
        return;
    }
    g_boom_charge_anim_trace_next_scan_ms = now_ms +
        std::max(8u, env_uint("MINA_XMARK_BOOM_RUNTIME_TRACE_SCAN_MS", 16));
    if (!g_mina || !api_function_field_ready(offsetof(MinaModAPI, PlayerGetWorld)) ||
        !api_function_field_ready(offsetof(MinaModAPI, WorldGetGameRootEntity)) ||
        !official_enemy_init_rtti() || !xmark_game_anim_init_rtti()) {
        return;
    }
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
    trace_boom_charge_anim_entity(
        root,
        0,
        env_uint("MINA_XMARK_BOOM_RUNTIME_TRACE_MAX_DEPTH", 10),
        &nodes,
        env_uint("MINA_XMARK_BOOM_RUNTIME_TRACE_MAX_NODES", 1024),
        now_ms);
}

uintptr_t game_singleton() {
    const uintptr_t global = game_singleton_global_address();
    uintptr_t game = 0;
    safe_read_ptr(global ? global : exe_base() + kGameSingletonPtrRva, &game);
    return game;
}

uintptr_t rip_relative_target(uintptr_t instruction, unsigned int displacement_offset, unsigned int instruction_length) {
    int32_t displacement = 0;
    if (!safe_read_i32(instruction + displacement_offset, &displacement)) {
        return 0;
    }
    return instruction + instruction_length + static_cast<intptr_t>(displacement);
}

uintptr_t scan_api_function_for_rip_target(const void *fn, const unsigned char *pattern, unsigned int pattern_len, unsigned int displacement_offset, unsigned int instruction_length) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(fn);
    if (!start || !pattern || !pattern_len) {
        return 0;
    }
    __try {
        const unsigned char *bytes = reinterpret_cast<const unsigned char *>(start);
        for (unsigned int i = 0; i < 96; ++i) {
            bool matches = true;
            for (unsigned int j = 0; j < pattern_len; ++j) {
                if (bytes[i + j] != pattern[j]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return rip_relative_target(start + i, displacement_offset, instruction_length);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return 0;
}

uintptr_t game_singleton_global_address() {
    static uintptr_t cached = 0;
    if (cached) {
        return cached;
    }
    static const unsigned char mov_rax_rip[] = {0x48, 0x8B, 0x05};
    if (g_mina && g_mina->PlayerGetPos) {
        cached = scan_api_function_for_rip_target(
            reinterpret_cast<const void *>(g_mina->PlayerGetPos),
            mov_rax_rip,
            static_cast<unsigned int>(sizeof(mov_rax_rip)),
            3,
            7);
    }
    return cached ? cached : exe_base() + kGameSingletonPtrRva;
}

uintptr_t world_scale_address() {
    static uintptr_t cached = 0;
    if (cached) {
        return cached;
    }
    static const unsigned char movss_rip[] = {0xF3, 0x0F, 0x10, 0x0D};
    if (g_mina && g_mina->SpawnEntity) {
        cached = scan_api_function_for_rip_target(
            reinterpret_cast<const void *>(g_mina->SpawnEntity),
            movss_rip,
            static_cast<unsigned int>(sizeof(movss_rip)),
            4,
            8);
    }
    return cached ? cached : exe_base() + kWorldScaleFloatRva;
}

uintptr_t player_entity() {
    const uintptr_t game = game_singleton();
    uintptr_t player = 0;
    if (game) {
        safe_read_ptr(game + 0x10, &player);
    }
    return player;
}

uintptr_t entity_manager() {
    const uintptr_t player = player_entity();
    uintptr_t owner = 0;
    uintptr_t manager = 0;
    if (player && safe_read_ptr(player + 0x50, &owner) && owner) {
        safe_read_ptr(owner + 0x1828, &manager);
    }
    return manager;
}

