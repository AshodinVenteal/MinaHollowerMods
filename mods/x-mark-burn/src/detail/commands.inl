void trim_command(char *command) {
    if (!command) {
        return;
    }
    char *start = command;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        ++start;
    }
    if (start != command) {
        std::memmove(command, start, std::strlen(start) + 1);
    }
    size_t len = std::strlen(command);
    while (len > 0) {
        const char c = command[len - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        command[--len] = 0;
    }
}

bool command_equals(const char *command, const char *expected) {
    return command && expected && _stricmp(command, expected) == 0;
}

bool command_starts_with(const char *command, const char *prefix) {
    return command && prefix && _strnicmp(command, prefix, std::strlen(prefix)) == 0;
}

void handle_set_env_command(const char *command) {
    const char *cursor = command;
    if (command_starts_with(cursor, "set-env ")) {
        cursor += 8;
    } else if (command_starts_with(cursor, "set ")) {
        cursor += 4;
    } else {
        return;
    }
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }

    char name[128]{};
    char value[192]{};
    unsigned int name_len = 0;
    while (*cursor && *cursor != '=' && *cursor != ' ' && *cursor != '\t' && name_len + 1 < sizeof(name)) {
        name[name_len++] = *cursor++;
    }
    name[name_len] = 0;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '=') {
        ++cursor;
    }
    std::snprintf(value, sizeof(value), "%s", cursor);
    trim_command(value);

    const bool allowed_name =
        name[0] &&
        (std::strncmp(name, "MINA_XMARK_", 11) == 0 ||
         std::strncmp(name, "MINA_D3D12_XMARK_", 17) == 0);
    if (!allowed_name) {
        if (g_mina) {
            char message[256]{};
            std::snprintf(message, sizeof(message), "XMarkBurn command set-env rejected name='%s'\n", name[0] ? name : "<empty>");
            g_mina->Log(message);
        }
        return;
    }

    SetEnvironmentVariableA(name, value);
    if (g_mina) {
        char message[384]{};
        std::snprintf(message, sizeof(message), "XMarkBurn command set-env %s=%s\n", name, value[0] ? value : "<empty>");
        g_mina->Log(message);
    }
}

void maybe_process_command_file(
    unsigned long long now_ms,
    int game_state,
    unsigned int room_index,
    float room_time) {
    if (!env_bool("MINA_XMARK_COMMAND_FILE_ENABLED", true) || !g_command_path[0]) {
        return;
    }
    const unsigned int poll_ms = std::max(16u, env_uint("MINA_XMARK_COMMAND_POLL_MS", 50));
    if (g_last_command_poll_ms && now_ms >= g_last_command_poll_ms && now_ms - g_last_command_poll_ms < poll_ms) {
        return;
    }
    g_last_command_poll_ms = now_ms;

    FILE *file = nullptr;
    if (fopen_s(&file, g_command_path, "rb") != 0 || !file) {
        return;
    }
    char command[160]{};
    const size_t read = std::fread(command, 1, sizeof(command) - 1, file);
    std::fclose(file);
    command[read] = 0;
    DeleteFileA(g_command_path);
    trim_command(command);
    if (!command[0]) {
        return;
    }

    if (g_mina) {
        char message[256]{};
        std::snprintf(message, sizeof(message), "XMarkBurn command file received command='%s'\n", command);
        g_mina->Log(message);
    }

    if (command_starts_with(command, "set ") || command_starts_with(command, "set-env ")) {
        handle_set_env_command(command);
        return;
    }

    if (command_starts_with(command, "play-sfx ")) {
        const char *sound_name = command + std::strlen("play-sfx ");
        while (*sound_name == ' ' || *sound_name == '\t') {
            ++sound_name;
        }
        xmark_play_sound_name(sound_name, "command-file-test");
        return;
    }

    if (command_starts_with(command, "stop-sfx ")) {
        const char *sound_name = command + std::strlen("stop-sfx ");
        while (*sound_name == ' ' || *sound_name == '\t') {
            ++sound_name;
        }
        xmark_stop_sound_name(sound_name, "command-file-test");
        return;
    }

    if (command_starts_with(command, "is-sfx ")) {
        const char *sound_name = command + std::strlen("is-sfx ");
        while (*sound_name == ' ' || *sound_name == '\t') {
            ++sound_name;
        }
        if (g_mina) {
            char message[224]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn SFX status name=%s playing=%u\n",
                sound_name,
                xmark_sound_is_playing_name(sound_name) ? 1u : 0u);
            g_mina->Log(message);
        }
        return;
    }

    if (command_equals(command, "spawn-test-enemy")) {
        if (test_enemy_spawn_context_ready(game_state, room_index, room_time)) {
            spawn_test_enemy_near_mina("command-file-test-enemy");
        } else if (g_mina) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn command spawn-test-enemy skipped contextReady=0 state=%d room=0x%08X roomTime=%.3f\n",
                game_state,
                room_index,
                static_cast<double>(room_time));
            g_mina->Log(message);
        }
        return;
    }

    if (command_equals(command, "mark-nearest-official") || command_equals(command, "mark-nearest-visual")) {
        if (command_equals(command, "mark-nearest-visual") &&
            env_bool("MINA_XMARK_COMMAND_MARK_NEAREST_VISUAL_READ_BRIDGE", false)) {
            read_enemy_visual_state_file(now_ms, true);
        }
        spawn_direct_xmark_enemy_test("command-file-nearest-official");
        return;
    }

    if (command_equals(command, "mark-player-debug")) {
        spawn_debug_xmark_on_player("command-file-player-debug");
        return;
    }

    if (command_equals(command, "mark-hud-debug")) {
        spawn_debug_hud_mark_only("command-file-hud-debug");
        return;
    }

    if (command_equals(command, "consume-mark-burn") ||
        command_equals(command, "consume-nearest-burn") ||
        command_equals(command, "burn-mark-debug")) {
        const unsigned int consumed = consume_first_xmark_attachment_for_burn(now_ms, "command-file-burn-consume");
        if (g_mina && !consumed) {
            g_mina->Log("XMarkBurn command burn consume missed: no active X mark attachment.\n");
        }
        return;
    }

    if (command_equals(command, "test-reburn-idempotence")) {
        const bool first_marked =
            spawn_direct_xmark_enemy_test("command-reburn-first-mark");
        const unsigned int first_consumed = first_marked
            ? consume_first_xmark_attachment_for_burn(now_ms, "command-reburn-first-consume")
            : 0u;
        const bool second_marked = first_consumed
            ? spawn_direct_xmark_enemy_test("command-reburn-second-mark")
            : false;
        const unsigned int second_consumed = second_marked
            ? consume_first_xmark_attachment_for_burn(now_ms, "command-reburn-second-consume")
            : 0u;
        if (g_mina) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn re-burn idempotence test firstMarked=%u firstConsumed=%u secondMarked=%u secondConsumed=%u\n",
                first_marked ? 1u : 0u,
                first_consumed,
                second_marked ? 1u : 0u,
                second_consumed);
            g_mina->Log(message);
        }
        return;
    }

    if (command_equals(command, "mark-last-test-enemy")) {
        mark_last_test_enemy_for_attachment_lab("command-file-last-test-enemy");
        return;
    }

    if (command_equals(command, "spawn-f0029")) {
        spawn_direct_f0029_probe("command-file-f0029");
        return;
    }

    if (command_equals(command, "read-visual")) {
        read_enemy_visual_state_file(now_ms, true);
        return;
    }

    if (command_equals(command, "align-player-left-of-visual") || command_equals(command, "align-left")) {
        XMarkVisualEnemyHost visual_host{};
        if (find_nearest_visual_enemy_host(&visual_host, now_ms) && g_mina && g_mina->PlayerSetPos) {
            const float offset_x = env_float("MINA_XMARK_ALIGN_PLAYER_SIDE_OFFSET_X", 2.25f);
            const float offset_y = env_float("MINA_XMARK_ALIGN_PLAYER_SIDE_OFFSET_Y", 0.0f);
            const float x = env_bool("MINA_XMARK_BASIC_CONTACT_FLIP_X", false)
                ? visual_host.position.x + offset_x
                : visual_host.position.x - offset_x;
            const float y = visual_host.position.y + offset_y;
            __try {
                g_mina->PlayerSetPos(x, y);
                g_last_direction = FacingRight;
                g_last_side_direction = FacingRight;
                g_mina->Log("XMarkBurn command aligned player left of nearest visual host for right-facing basic attack.\n");
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_mina->Log("XMarkBurn command align-left PlayerSetPos raised an exception.\n");
            }
        } else if (g_mina) {
            g_mina->Log("XMarkBurn command align-left missed: no visual host or PlayerSetPos unavailable.\n");
        }
        return;
    }

    if (command_equals(command, "align-player-below-visual") || command_equals(command, "align-below")) {
        XMarkVisualEnemyHost visual_host{};
        if (find_nearest_visual_enemy_host(&visual_host, now_ms) && g_mina && g_mina->PlayerSetPos) {
            const float offset_x = env_float("MINA_XMARK_ALIGN_PLAYER_VERTICAL_OFFSET_X", 0.0f);
            const float offset_y = env_float("MINA_XMARK_ALIGN_PLAYER_VERTICAL_OFFSET_Y", 2.5f);
            const float x = visual_host.position.x + offset_x;
            const float y = visual_host.position.y + offset_y;
            __try {
                g_mina->PlayerSetPos(x, y);
                g_last_direction = FacingUp;
                g_mina->Log("XMarkBurn command aligned player below nearest visual host for up-facing basic attack.\n");
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_mina->Log("XMarkBurn command align-below PlayerSetPos raised an exception.\n");
            }
        } else if (g_mina) {
            g_mina->Log("XMarkBurn command align-below missed: no visual host or PlayerSetPos unavailable.\n");
        }
        return;
    }

    if (g_mina) {
        char message[256]{};
        std::snprintf(message, sizeof(message), "XMarkBurn command file ignored unknown command='%s'\n", command);
        g_mina->Log(message);
    }
}

void update_attack_input_probe(unsigned long long now_ms) {
    const SHORT j_state = GetAsyncKeyState('J');
    g_input_win_j_down = (j_state & 0x8000) != 0;
    g_input_win_j_press = (j_state & 0x0001) != 0;
    g_input_api_key_j_down = false;
    g_input_api_key_j_held = false;
    g_input_api_action_attack_down = false;
    g_input_api_action_attack_held = false;
    g_input_api_action_subweapon_down = false;
    g_input_api_action_subweapon_held = false;
    g_input_api_bpad_left_down = false;
    g_input_api_bpad_left_held = false;
    g_input_first_action_down = -1;
    g_input_first_action_held = -1;
    g_input_first_button_down = -1;
    g_input_first_button_held = -1;

    if (g_mina) {
        g_input_api_key_j_down = safe_input_call(g_mina->IsKeyDown, YC_KEY_J);
        g_input_api_key_j_held = safe_input_call(g_mina->IsKeyHeld, YC_KEY_J);
        g_input_api_action_attack_down = safe_input_call(g_mina->IsActionDown, kGameAct_AttackA);
        g_input_api_action_attack_held = safe_input_call(g_mina->IsActionHeld, kGameAct_AttackA);
        g_input_api_action_subweapon_down = safe_input_call(g_mina->IsActionDown, kGameAct_SubweaponC);
        g_input_api_action_subweapon_held = safe_input_call(g_mina->IsActionHeld, kGameAct_SubweaponC);
        g_input_api_bpad_left_down = safe_input_call(g_mina->IsButtonDown, YC_INPUT_BPAD_LEFT);
        g_input_api_bpad_left_held = safe_input_call(g_mina->IsButtonHeld, YC_INPUT_BPAD_LEFT);

        if (env_bool("MINA_XMARK_INPUT_SCAN_ENABLED", false)) {
            const unsigned int scan_limit = env_uint("MINA_XMARK_INPUT_SCAN_LIMIT", 32);
            for (unsigned int input = 0; input < scan_limit; ++input) {
                if (g_input_first_action_down < 0 && safe_input_call(g_mina->IsActionDown, input)) {
                    g_input_first_action_down = static_cast<int>(input);
                }
                if (g_input_first_action_held < 0 && safe_input_call(g_mina->IsActionHeld, input)) {
                    g_input_first_action_held = static_cast<int>(input);
                }
                if (g_input_first_button_down < 0 && safe_input_call(g_mina->IsButtonDown, input)) {
                    g_input_first_button_down = static_cast<int>(input);
                }
                if (g_input_first_button_held < 0 && safe_input_call(g_mina->IsButtonHeld, input)) {
                    g_input_first_button_held = static_cast<int>(input);
                }
            }
        }
    }

    const bool action_attack_active = g_input_api_action_attack_down || g_input_api_action_attack_held;
    g_input_api_action_attack_pressed = action_attack_active && !g_input_api_action_attack_was_active;
    g_input_api_action_attack_was_active = action_attack_active;

    if (g_input_api_action_subweapon_down || g_input_api_action_subweapon_held) {
        g_last_subweapon_action_ms = now_ms;
        g_basic_attack_probe.active = false;
        g_basic_attack_probe.contact_active = false;
        g_basic_attack_probe.health_check_armed = false;
    }

    const bool bpad_left_active = g_input_api_bpad_left_down || g_input_api_bpad_left_held;
    g_input_api_bpad_left_pressed = g_input_api_bpad_left_down && !g_input_api_bpad_left_was_down;
    g_input_api_bpad_left_was_down = g_input_api_bpad_left_down;
    g_input_api_bpad_left_was_active = bpad_left_active;

    const bool use_bpad_left = env_bool("MINA_XMARK_J_USE_BPAD_LEFT", false);
    g_input_attack_down =
        g_input_win_j_down ||
        g_input_api_key_j_down ||
        g_input_api_key_j_held ||
        action_attack_active ||
        (use_bpad_left && (g_input_api_bpad_left_down || (g_attack_j_latched && g_input_api_bpad_left_held)));

    if (g_input_attack_down &&
        g_mina &&
        env_bool("MINA_XMARK_INPUT_PROBE_LOG", false) &&
        now_ms - g_last_input_probe_log_ms >= env_uint("MINA_XMARK_INPUT_PROBE_LOG_MS", 500)) {
        g_last_input_probe_log_ms = now_ms;
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn input probe attack=1 winJ=%u/%u apiKeyJ=%u/%u actionAttack=%u/%u/%u bpadLeft=%u/%u/%u firstAction=%d/%d firstButton=%d/%d\n",
            g_input_win_j_down ? 1u : 0u,
            g_input_win_j_press ? 1u : 0u,
            g_input_api_key_j_down ? 1u : 0u,
            g_input_api_key_j_held ? 1u : 0u,
            g_input_api_action_attack_down ? 1u : 0u,
            g_input_api_action_attack_held ? 1u : 0u,
            g_input_api_action_attack_pressed ? 1u : 0u,
            g_input_api_bpad_left_down ? 1u : 0u,
            g_input_api_bpad_left_held ? 1u : 0u,
            g_input_api_bpad_left_pressed ? 1u : 0u,
            g_input_first_action_down,
            g_input_first_action_held,
            g_input_first_button_down,
            g_input_first_button_held);
        g_mina->Log(message);
    }
}

void maybe_spawn_f0029_from_attack(
    unsigned long long now_ms,
    int game_state,
    unsigned int room_index,
    float room_time) {
    update_attack_input_probe(now_ms);
    const bool use_bpad_left = env_bool("MINA_XMARK_J_USE_BPAD_LEFT", false);
    const bool physical_key_attack_down =
        g_input_win_j_down ||
        g_input_api_key_j_down ||
        g_input_api_key_j_held;
    const bool action_attack_active = g_input_api_action_attack_down || g_input_api_action_attack_held;
    const bool action_attack_down = action_attack_active && (g_attack_j_latched || g_input_api_action_attack_pressed);
    const bool key_attack_down = physical_key_attack_down || action_attack_down;
    const bool bpad_attack_start = use_bpad_left && g_input_api_bpad_left_pressed;
    bool attack_down = key_attack_down ||
        (use_bpad_left && (g_input_api_bpad_left_down || (g_attack_j_latched && g_input_api_bpad_left_held)));
    bool low_bit_pressed = g_input_win_j_press;
    const bool was_attack_down = g_attack_j_was_down;
    const bool raw_attack_started =
        (physical_key_attack_down && !was_attack_down) ||
        g_input_api_action_attack_pressed ||
        bpad_attack_start ||
        (env_bool("MINA_XMARK_J_USE_LOWBIT_PRESS", false) && low_bit_pressed && !was_attack_down);
    const unsigned int attack_start_debounce_ms = env_uint("MINA_XMARK_ATTACK_START_DEBOUNCE_MS", 320);
    const bool attack_started =
        raw_attack_started &&
        (!g_attack_j_pressed_ms || now_ms - g_attack_j_pressed_ms >= attack_start_debounce_ms);
    const bool attack_released = !attack_down && was_attack_down;
    g_attack_j_was_down = attack_down;
    if (!attack_down) {
        g_attack_j_latched = false;
    }

    const bool burn_charged_release_consume_enabled = env_bool("MINA_XMARK_BURN_CONSUME_ON_CHARGED_RELEASE", false);
    const bool burn_charged_frame_consume_enabled = env_bool("MINA_XMARK_BURN_CONSUME_ON_CHARGED_FRAME", true);
    const bool boom_pattern_enabled = env_bool("MINA_XMARK_BOOM_PATTERN_ENABLED", true);
    if (!g_integrated_f0029_enabled && !g_native_crater_enabled &&
        !burn_charged_release_consume_enabled && !burn_charged_frame_consume_enabled &&
        !boom_pattern_enabled) {
        g_attack_j_pending = false;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;
        g_attack_crater_pending = false;
        g_attack_burn_consume_pending = false;
        g_attack_smear_contact_pending = false;
        g_attack_j_charge_crater_ready = false;
        g_attack_j_charge_burn_fired = false;
        return;
    }
    if (is_pseudo_room(room_index) || game_state <= 0 || room_time < 0.25f) {
        g_attack_j_pending = false;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;
        g_attack_crater_pending = false;
        g_attack_burn_consume_pending = false;
        g_attack_smear_contact_pending = false;
        g_attack_j_charge_crater_ready = false;
        g_attack_j_charge_burn_fired = false;
        return;
    }

    maybe_open_pending_basic_attack_contact(now_ms);

    if (attack_started) {
        const int attack_direction = current_attack_direction_from_input();
        if (!g_clashrend_boom_pattern.active) {
            g_clashrend_boom_pattern = ClashrendBoomPatternState{};
        }
        g_attack_j_latched = true;
        g_attack_j_pressed_ms = now_ms;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;
        g_attack_j_active_direction = attack_direction;
        g_attack_j_start_direction = attack_direction;
        g_attack_j_charge_crater_fired = false;
        g_attack_j_charge_crater_ready = false;
        g_attack_j_charge_burn_fired = false;
        if (!g_f0029_side_basic_chain_until_ms || now_ms >= g_f0029_side_basic_chain_until_ms) {
            g_f0029_side_basic_chain_direction = attack_direction;
        }
        reset_basic_smear_animation_lock();
        if (env_bool("MINA_XMARK_CHARGED_RESERVE_ON_ATTACK_START", true)) {
            const unsigned int reserve_ms =
                std::max(1u, env_uint("MINA_XMARK_CHARGED_RESERVE_MS", 1800));
            for (XMarkAttachment &attachment : g_xmark_attachments) {
                if (!attachment.active || now_ms >= attachment.expires_ms) {
                    continue;
                }
                attachment.expires_ms = std::max(
                    attachment.expires_ms,
                    now_ms + static_cast<unsigned long long>(reserve_ms));
                const uintptr_t hud_target = attachment.target
                    ? attachment.target
                    : static_cast<uintptr_t>(attachment.visual_key);
                if (hud_target) {
                    upsert_xmark_hud_mark_mode(
                        hud_target,
                        attachment.expires_ms,
                        kXMarkHudModeMark);
                }
            }
        }
        if (env_bool("MINA_XMARK_BASIC_HEALTH_START_FROM_INPUT", true)) {
            start_basic_attack_health_probe(now_ms, attack_direction, true, true);
        }
        const bool vertical_attack = attack_direction == FacingUp || attack_direction == FacingDown;
        if (vertical_attack) {
            const unsigned int smear_delay_ms = env_uint("MINA_XMARK_BASIC_VERTICAL_SMEAR_CONTACT_DELAY_MS", 115);
            queue_basic_attack_contact_probe(now_ms, attack_direction, smear_delay_ms);
        }

        const bool side_only = env_bool("MINA_XMARK_F0029_SIDE_ONLY", true);
        const bool f0029_direction_allowed =
            !side_only || attack_direction == FacingLeft || attack_direction == FacingRight;
        if (g_integrated_f0029_enabled &&
            env_bool("MINA_XMARK_F0029_KEYPATH_ENABLED", false) &&
            !env_bool("MINA_XMARK_F0029_REQUIRE_D3D12_BASIC_FRAME", true) &&
            f0029_direction_allowed &&
            now_ms - g_last_integrated_f0029_ms >= g_integrated_f0029_cooldown_ms) {
            g_attack_j_pending = true;
            g_attack_j_pending_direction = attack_direction;
            g_attack_j_pending_ms = now_ms + env_uint("MINA_XMARK_F0029_ATTACK_DELAY_MS", 150);

            if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
                char queued_message[384]{};
                std::snprintf(
                    queued_message,
                    sizeof(queued_message),
                    "XMarkBurn integrated f0029 queued reason=basic-attack-key-J dir=%s delayMs=%u\n",
                    direction_name(g_attack_j_pending_direction),
                    env_uint("MINA_XMARK_F0029_ATTACK_DELAY_MS", 150));
                g_mina->Log(queued_message);
            }
        } else if (g_integrated_f0029_enabled && !f0029_direction_allowed && g_mina &&
                   env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
            char skipped_message[256]{};
            std::snprintf(
                skipped_message,
                sizeof(skipped_message),
                "XMarkBurn integrated f0029 skipped: resolved J direction was %s, side-only is enabled.\n",
                direction_name(attack_direction));
            g_mina->Log(skipped_message);
        }
    }

    if (attack_down) {
        g_attack_j_active_direction = current_attack_direction_from_input();
    }

    if (attack_released && g_attack_j_pending && !g_attack_j_charge_crater_ready) {
        const unsigned long long held_ms = g_attack_j_pressed_ms ? now_ms - g_attack_j_pressed_ms : 0;
        const unsigned int max_basic_hold_ms = env_uint(
            "MINA_XMARK_F0029_MAX_BASIC_HOLD_MS",
            env_uint("MINA_XMARK_F0029_MAX_HOLD_MS", 240));
        if (!max_basic_hold_ms || held_ms <= max_basic_hold_ms) {
            g_attack_j_basic_release_seen = true;
            g_attack_j_basic_release_held_ms = held_ms;
        } else {
            g_attack_j_pending = false;
            g_attack_j_basic_release_seen = false;
            g_attack_j_basic_release_held_ms = 0;
            if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
                char canceled_message[256]{};
                std::snprintf(
                    canceled_message,
                    sizeof(canceled_message),
                    "XMarkBurn integrated f0029 canceled: J release heldMs=%llu exceeded side-basic window %u.\n",
                    held_ms,
                    max_basic_hold_ms);
                g_mina->Log(canceled_message);
            }
        }
    }

    const unsigned int charge_min_ms = env_uint("MINA_XMARK_NATIVE_CRATER_CHARGE_MIN_MS", 650);
    const bool charged_hold_ready =
        attack_down &&
        g_attack_j_pressed_ms &&
        now_ms - g_attack_j_pressed_ms >= charge_min_ms;
    if (charged_hold_ready && env_bool("MINA_XMARK_CANCEL_BASIC_PROBE_ON_CHARGE", true)) {
        g_attack_smear_contact_pending = false;
        if (g_basic_attack_probe.active) {
            g_basic_attack_probe.active = false;
            g_basic_attack_probe.contact_active = false;
            if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_HEALTH_PROBE_LOG", true)) {
                g_mina->Log("XMarkBurn basic health probe canceled: charged attack hold owns next damage drop.\n");
            }
        }
    }

    if (charged_hold_ready && g_native_crater_enabled && !g_attack_j_charge_crater_ready) {
        if (g_attack_j_pressed_ms) {
            g_attack_j_pending = false;
            g_attack_j_basic_release_seen = false;
            g_attack_j_basic_release_held_ms = 0;
            g_attack_smear_contact_pending = false;
            g_attack_j_charge_crater_ready = true;
            if (g_mina) {
                char ready_message[320]{};
                std::snprintf(
                    ready_message,
                    sizeof(ready_message),
                    "XMarkBurn native crater armed reason=charged-attack-key-J dir=%s heldMs=%llu releaseToSpawn=1\n",
                    direction_name(g_attack_j_active_direction),
                    static_cast<unsigned long long>(now_ms - g_attack_j_pressed_ms));
                g_mina->Log(ready_message);
            }
        }
    }

    if (attack_released && !g_attack_j_charge_crater_ready) {
        g_attack_crater_pending = false;
    }
    if (attack_released && g_attack_j_pressed_ms) {
        arm_boom_charge_anim_trace(
            now_ms,
            now_ms - g_attack_j_pressed_ms,
            g_attack_j_active_direction);
        arm_clashrend_boom_pattern(
            now_ms,
            now_ms - g_attack_j_pressed_ms,
            g_attack_j_active_direction);
    }
    if (attack_released &&
        burn_charged_release_consume_enabled &&
        !g_attack_j_charge_burn_fired &&
        g_attack_j_pressed_ms) {
        const unsigned long long held_ms = now_ms - g_attack_j_pressed_ms;
        if (held_ms >= charge_min_ms) {
            g_attack_j_charge_burn_fired = true;
            queue_xmark_burn_charged_consume(now_ms, g_attack_j_active_direction, "charged-attack-release-J");
        }
    }
    if (attack_released && g_native_crater_enabled && g_attack_j_charge_crater_ready && !g_attack_j_charge_crater_fired) {
        g_attack_j_charge_crater_fired = true;
        g_attack_j_charge_crater_ready = false;
        g_attack_j_pending = false;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;

        const unsigned int crater_cooldown_ms = env_uint("MINA_XMARK_NATIVE_CRATER_COOLDOWN_MS", 700);
        if (now_ms - g_last_native_crater_ms >= crater_cooldown_ms) {
            const unsigned int crater_delay_ms = env_uint(
                "MINA_XMARK_NATIVE_CRATER_IMPACT_FRAME_DELAY_MS",
                env_uint("MINA_XMARK_NATIVE_CRATER_RELEASE_DELAY_MS", 190));
            g_attack_crater_pending = true;
            g_attack_crater_pending_direction = g_attack_j_active_direction;
            g_attack_crater_pending_ms = now_ms + crater_delay_ms;
            if (g_mina) {
                char queued_message[384]{};
                std::snprintf(
                    queued_message,
                    sizeof(queued_message),
                    "XMarkBurn native crater queued reason=charged-attack-release-J impactFrames=f0001/f0003/f0005 dir=%s heldMs=%llu delayMs=%u\n",
                    direction_name(g_attack_crater_pending_direction),
                    static_cast<unsigned long long>(now_ms - g_attack_j_pressed_ms),
                    crater_delay_ms);
                g_mina->Log(queued_message);
            }
        } else if (g_mina) {
            g_mina->Log("XMarkBurn native crater skipped: release was inside crater cooldown window.\n");
        }
    }

    if (g_attack_crater_pending && now_ms >= g_attack_crater_pending_ms) {
        const int spawn_direction = g_attack_crater_pending_direction;
        g_attack_crater_pending = false;
        const Vec3 position = crater_position_for_direction(spawn_direction);
        if (g_mina && env_bool("MINA_XMARK_NATIVE_CRATER_EVENT_LOG", false)) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "XMarkBurn native crater target reason=charged-slam-impact-frame impactFrames=f0001/f0003/f0005 dir=%s pos=(%.3f, %.3f, %.3f)\n",
                direction_name(spawn_direction),
                static_cast<double>(position.x),
                static_cast<double>(position.y),
                static_cast<double>(position.z));
            g_mina->Log(message);
        }
        if (spawn_direct_crater_at("charged-attack-release-J", position, spawn_direction)) {
            g_last_native_crater_ms = now_ms;
        }
    }
    maybe_fire_pending_xmark_burn_charged_consume(now_ms);

    if (!g_attack_j_pending || now_ms < g_attack_j_pending_ms) {
        return;
    }

    const int queued_direction = g_attack_j_pending_direction;
    int spawn_direction = queued_direction;
    if (env_bool("MINA_XMARK_F0029_RESOLVE_FACING_AT_SPAWN", false)) {
        spawn_direction = current_side_facing_from_input_or_memory();
    }
    if (env_bool("MINA_XMARK_F0029_SIDE_ONLY", true) &&
        spawn_direction != FacingLeft &&
        spawn_direction != FacingRight) {
        g_attack_j_pending = false;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;
        if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
            char skipped_message[256]{};
            std::snprintf(
                skipped_message,
                sizeof(skipped_message),
                "XMarkBurn integrated f0029 canceled: spawn direction was %s, side-only is enabled.\n",
                direction_name(spawn_direction));
            g_mina->Log(skipped_message);
        }
        return;
    }
    if (env_bool("MINA_XMARK_F0029_CANCEL_LONG_HOLD", false) &&
        !g_attack_j_basic_release_seen &&
        g_attack_j_pressed_ms &&
        now_ms - g_attack_j_pressed_ms > env_uint("MINA_XMARK_F0029_MAX_HOLD_MS", 240)) {
        g_attack_j_pending = false;
        g_attack_j_basic_release_seen = false;
        g_attack_j_basic_release_held_ms = 0;
        if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
            g_mina->Log("XMarkBurn integrated f0029 canceled: J held past side-basic window.\n");
        }
        return;
    }
    if (env_bool("MINA_XMARK_F0029_REQUIRE_BASIC_RELEASE", true) && !g_attack_j_basic_release_seen) {
        const unsigned int max_basic_hold_ms = env_uint(
            "MINA_XMARK_F0029_MAX_BASIC_HOLD_MS",
            env_uint("MINA_XMARK_F0029_MAX_HOLD_MS", 240));
        if (g_attack_j_pressed_ms && max_basic_hold_ms && now_ms - g_attack_j_pressed_ms > max_basic_hold_ms) {
            g_attack_j_pending = false;
            g_attack_j_basic_release_seen = false;
            g_attack_j_basic_release_held_ms = 0;
            if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
                g_mina->Log("XMarkBurn integrated f0029 canceled: no basic release before side-basic window closed.\n");
            }
        }
        return;
    }
    if (env_bool("MINA_XMARK_F0029_REQUIRE_RELEASED_AT_SPAWN", true) && attack_down) {
        return;
    }
    g_attack_j_pending = false;
    g_attack_j_basic_release_seen = false;
    g_attack_j_basic_release_held_ms = 0;
    const Vec3 position = f0029_attack_position_for_direction(spawn_direction);
    if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "XMarkBurn integrated f0029 target reason=basic-attack-key-J dir=%s queuedDir=%s pos=(%.3f, %.3f, %.3f)\n",
            direction_name(spawn_direction),
            direction_name(queued_direction),
            static_cast<double>(position.x),
            static_cast<double>(position.y),
            static_cast<double>(position.z));
        g_mina->Log(message);
    }
    if (spawn_direct_f0029_attack_at("basic-attack-key-J", position, spawn_direction)) {
        g_last_integrated_f0029_ms = now_ms;
        activate_basic_attack_contact_probe(now_ms, spawn_direction, "f0029");
    }
}

void maybe_handle_d3d12_basic_frame_state(
    unsigned long long now_ms,
    int game_state,
    unsigned int room_index,
    float room_time) {
    if (!env_bool("MINA_XMARK_D3D12_BASIC_FRAME_STATE_ENABLED", true)) {
        return;
    }

    XMarkBasicFrameState state{};
    bool has_basic_frame_state = read_preferred_player_attack_frame_state(now_ms, &state);
    const unsigned int max_age_ms = env_uint("MINA_XMARK_D3D12_BASIC_FRAME_MAX_AGE_MS", 180);
    if ((!has_basic_frame_state ||
         (!state.modapi_authoritative && max_age_ms && now_ms > state.tick + max_age_ms)) &&
        read_modapi_player_basic_frame_state(now_ms, &state)) {
        has_basic_frame_state = true;
    }
    if (!has_basic_frame_state) {
        return;
    }
    const bool new_frame_tick = state.tick > g_last_basic_frame_state_tick;
    if (!new_frame_tick) {
        if (!env_bool("MINA_XMARK_D3D12_BASIC_FRAME_HIT_RECHECK_ENABLED", !xmark_official_damage_gate_enabled()) ||
            !g_last_basic_frame_state_for_hit_active ||
            state.tick != g_last_basic_frame_state_for_hit.tick) {
            return;
        }
        const unsigned int recheck_window_ms =
            env_uint("MINA_XMARK_D3D12_BASIC_FRAME_HIT_RECHECK_WINDOW_MS", 700);
        const unsigned int recheck_poll_ms =
            std::max(16u, env_uint("MINA_XMARK_D3D12_BASIC_FRAME_HIT_RECHECK_POLL_MS", 32));
        if (recheck_window_ms &&
            now_ms > g_last_basic_frame_state_for_hit_seen_ms + recheck_window_ms) {
            g_last_basic_frame_state_for_hit_active = false;
            return;
        }
        if (g_last_basic_frame_hit_eval_ms &&
            now_ms >= g_last_basic_frame_hit_eval_ms &&
            now_ms - g_last_basic_frame_hit_eval_ms < recheck_poll_ms) {
            return;
        }
        maybe_trace_basic_frame_enemy_hit(now_ms, g_last_basic_frame_state_for_hit, "d3d12-basic-frame-recheck");
        return;
    }
    g_last_basic_frame_state_tick = state.tick;

    if (max_age_ms && now_ms > state.tick + max_age_ms) {
        g_last_basic_frame_state_for_hit_active = false;
        return;
    }
    if (is_pseudo_room(room_index) || game_state <= 0 || room_time < 0.25f) {
        g_last_basic_frame_state_for_hit_active = false;
        return;
    }
    if (xmark_charged_frame_name_allowed(state.frame)) {
        arm_d3d12_charged_crater(state.direction, state.tick);
        if (g_clashrend_boom_pattern.pending && !g_clashrend_boom_pattern.active) {
            g_clashrend_boom_pattern.direction = state.direction;
        }
        g_last_muriel_charged_frame_state = state;
        g_last_basic_frame_state_for_hit_active = false;
        g_attack_smear_contact_pending = false;
        if (env_bool("MINA_XMARK_CANCEL_BASIC_PROBE_ON_CHARGE", true)) {
            g_basic_attack_probe.active = false;
            g_basic_attack_probe.contact_active = false;
        }
        if (env_bool("MINA_XMARK_BURN_CONSUME_ON_CHARGED_FRAME", true) &&
            (state.tick != g_last_charged_frame_consume_tick ||
             state.draw != g_last_charged_frame_consume_draw)) {
            g_last_charged_frame_consume_tick = state.tick;
            g_last_charged_frame_consume_draw = state.draw;
            if (env_bool("MINA_XMARK_BURN_CHARGED_FRAME_REQUIRE_CONTACT", true) && !state.has_contact) {
                if (g_mina && env_bool("MINA_XMARK_BURN_CHARGED_CONSUME_LOG", true)) {
                    char skipped_message[320]{};
                    std::snprintf(
                        skipped_message,
                        sizeof(skipped_message),
                        "XMarkBurn charged frame consume skipped: frame=%s dir=%s had no contact point.\n",
                        state.frame[0] ? state.frame : "<none>",
                        direction_name(state.direction));
                    g_mina->Log(skipped_message);
                }
                return;
            }
            Vec3 impact_position = state.has_contact
                ? Vec3{state.contact_x, state.contact_y, 0.0f}
                : crater_position_for_direction(state.direction);
            consume_xmark_attachments_near_position_for_burn(
                now_ms,
                impact_position,
                state.direction,
                "charged-slam-frame");
        }
        return;
    }

    const unsigned int charge_min_ms =
        env_uint("MINA_XMARK_NATIVE_CRATER_CHARGE_MIN_MS", 650);
    const bool charged_hold_owns_damage =
        g_attack_j_pressed_ms &&
        ((g_input_attack_down &&
          now_ms >= g_attack_j_pressed_ms + charge_min_ms) ||
         g_attack_j_charge_crater_ready);
    if (charged_hold_owns_damage) {
        g_last_basic_frame_state_for_hit_active = false;
        g_attack_smear_contact_pending = false;
        g_basic_attack_probe.active = false;
        g_basic_attack_probe.contact_active = false;
        g_basic_attack_probe.health_check_armed = false;
        return;
    }
    g_last_basic_frame_state_for_hit = state;
    g_last_basic_frame_state_for_hit_active = xmark_basic_frame_name_allowed(state.frame) &&
        (state.has_geometry || state.has_contact);
    g_last_basic_frame_state_for_hit_seen_ms = now_ms;
    g_last_basic_frame_hit_eval_ms = 0;

    const bool state_side_direction = state.direction == FacingLeft || state.direction == FacingRight;
    int resolved_direction = state.direction;
    if (state_side_direction &&
        env_bool("MINA_XMARK_D3D12_BASIC_FRAME_USE_LAST_SIDE_DIRECTION", false) &&
        (g_last_side_direction == FacingLeft || g_last_side_direction == FacingRight)) {
        resolved_direction = g_last_side_direction;
    }
    if (state_side_direction) {
        resolved_direction = resolve_side_direction_from_basic_frame_geometry(state, resolved_direction);
        if (xmark_direction_is_side(resolved_direction)) {
            g_last_side_direction = resolved_direction;
        }
    }

    const unsigned int attack_press_window_ms = env_uint("MINA_XMARK_F0029_ATTACK_PRESS_WINDOW_MS", 260);
    const bool has_recent_attack_press =
        g_attack_j_pressed_ms &&
        now_ms >= g_attack_j_pressed_ms &&
        (!attack_press_window_ms || now_ms <= g_attack_j_pressed_ms + attack_press_window_ms);
    if (state_side_direction &&
        has_recent_attack_press &&
        env_bool("MINA_XMARK_F0029_D3D_USE_ATTACK_START_DIRECTION", true) &&
        xmark_direction_is_side(g_attack_j_start_direction)) {
        resolved_direction = g_attack_j_start_direction;
        g_last_side_direction = resolved_direction;
    }
    if (env_bool("MINA_XMARK_D3D12_BASIC_FRAME_REQUIRE_RECENT_ATTACK_PRESS", false) &&
        !has_recent_attack_press) {
        return;
    }
    const bool side_only_smear_lock = env_bool("MINA_XMARK_F0029_SIDE_ONLY", true);
    if (state_side_direction && g_basic_smear_animation_lock.active) {
        const unsigned int side_chain_ms =
            std::max(120u, env_uint("MINA_XMARK_F0029_SIDE_BASIC_CHAIN_MS", 260));
        const bool side_chain_expired =
            !g_f0029_side_basic_chain_until_ms ||
            now_ms >= g_f0029_side_basic_chain_until_ms;
        const bool new_rendered_start =
            xmark_side_basic_start_frame_for_f0029(state.frame) &&
            state.tick != g_basic_smear_animation_lock.source_tick &&
            now_ms >= g_basic_smear_animation_lock.started_ms +
                env_uint("MINA_XMARK_F0029_START_FRAME_RELOCK_MS", 80);
        const bool recovered_new_chain =
            side_chain_expired &&
            xmark_side_basic_visible_frame_for_f0029(state.frame) &&
            g_last_integrated_f0029_ms &&
            now_ms >= g_last_integrated_f0029_ms + side_chain_ms;
        if (new_rendered_start || recovered_new_chain) {
            reset_basic_smear_animation_lock();
        }
    }
    XMarkBasicFrameState direction_lock_state = state;
    const bool lock_raw_frame_direction =
        env_bool("MINA_XMARK_BASIC_SMEAR_LOCK_RAW_FRAME_DIRECTION", true);
    if (state_side_direction &&
        (!lock_raw_frame_direction ||
         (has_recent_attack_press &&
          env_bool("MINA_XMARK_F0029_D3D_USE_ATTACK_START_DIRECTION", true))) &&
        xmark_direction_is_side(resolved_direction)) {
        direction_lock_state.direction = resolved_direction;
    }
    const int animation_locked_direction = lock_basic_smear_direction_from_animation(
        now_ms,
        direction_lock_state,
        side_only_smear_lock,
        has_recent_attack_press,
        g_attack_j_pressed_ms);
    const int probe_direction =
        (state_side_direction && xmark_direction_is_side(animation_locked_direction))
            ? animation_locked_direction
            : resolved_direction;

    if (!g_basic_attack_probe.active || g_basic_attack_probe.direction != probe_direction) {
        start_basic_attack_health_probe(now_ms, probe_direction);
    }

    XMarkBasicFrameState mark_probe_state = state;
    if (state_side_direction && xmark_direction_is_side(probe_direction)) {
        mark_probe_state.direction = probe_direction;
    }
    maybe_trace_basic_frame_enemy_hit(now_ms, mark_probe_state, "d3d12-basic-frame");

    const bool vertical_direction = probe_direction == FacingUp || probe_direction == FacingDown;
    if (vertical_direction) {
        const unsigned int guard_ms = std::max(
            120u,
            env_uint("MINA_XMARK_F0029_VERTICAL_ATTACK_GUARD_MS", 260));
        g_vertical_basic_f0029_guard_until_ms = now_ms + guard_ms;
        g_f0029_side_basic_chain_until_ms = 0;
        g_f0029_side_basic_chain_direction = 0;
        for (F0029RenderEffect &effect : g_f0029_render_effects) {
            effect.active = false;
        }
    }
    if (vertical_direction && env_bool("MINA_XMARK_VERTICAL_SMEAR_ENABLED", false)) {
        const bool vertical_start_frame =
            xmark_vertical_basic_start_frame_for_smear(state.frame, probe_direction);
        const bool vertical_visible_frame =
            xmark_vertical_basic_visible_frame_for_smear(state.frame, probe_direction);
        if (vertical_visible_frame) {
            refresh_vertical_basic_smear_lifetime(
                now_ms,
                probe_direction,
                state.tick,
                state.draw);
        }

        bool vertical_smear_active = false;
        for (const VerticalSmearRenderEffect &effect : g_vertical_smear_render_effects) {
            if (effect.active &&
                effect.direction == probe_direction &&
                effect.started_ms &&
                now_ms < effect.started_ms + effect.duration_ms &&
                (!effect.soft_expires_ms || now_ms < effect.soft_expires_ms)) {
                vertical_smear_active = true;
                break;
            }
        }
        const unsigned int vertical_chain_ms =
            std::max(160u, env_uint("MINA_XMARK_VERTICAL_SMEAR_ONE_PER_ATTACK_MS", 260));
        const bool recover_missed_start =
            env_bool("MINA_XMARK_VERTICAL_SMEAR_RECOVER_MISSED_START", true) &&
            vertical_visible_frame &&
            !vertical_start_frame &&
            !xmark_vertical_basic_initial_frame_for_smear(state.frame, probe_direction) &&
            !vertical_smear_active &&
            (!g_last_vertical_smear_ms || now_ms >= g_last_vertical_smear_ms + vertical_chain_ms);
        const bool source_consumed =
            (state.tick && state.tick == g_vertical_smear_last_source_tick) ||
            (state.draw && state.draw == g_vertical_smear_last_source_draw);
        const bool attack_press_consumed =
            g_attack_j_pressed_ms &&
            g_vertical_smear_last_consumed_attack_press_ms == g_attack_j_pressed_ms;
        if ((vertical_start_frame || recover_missed_start) &&
            !source_consumed &&
            !attack_press_consumed &&
            (!g_last_vertical_smear_ms || now_ms >= g_last_vertical_smear_ms + vertical_chain_ms)) {
            Vec3 position = f0029_attack_position_for_direction(probe_direction);
            if (probe_direction == FacingUp) {
                position.y += env_float("MINA_XMARK_VERTICAL_SMEAR_UP_OFFSET_Y", 0.8f);
            } else if (probe_direction == FacingDown) {
                position.y += env_float("MINA_XMARK_VERTICAL_SMEAR_DOWN_OFFSET_Y", -0.3f);
            }
            if (spawn_render_vertical_smear_at(
                    "d3d12-vertical-basic-frame",
                    position,
                    probe_direction,
                    state.tick,
                    state.draw)) {
                g_last_vertical_smear_ms = now_ms;
                g_vertical_smear_last_source_tick = state.tick;
                g_vertical_smear_last_source_draw = state.draw;
                g_vertical_smear_last_direction = probe_direction;
                g_vertical_smear_last_consumed_attack_press_ms = g_attack_j_pressed_ms;
            }
        }
    }

    const bool side_direction = resolved_direction == FacingLeft || resolved_direction == FacingRight;
    const bool vertical_f0029_guard_active =
        g_vertical_basic_f0029_guard_until_ms &&
        now_ms < g_vertical_basic_f0029_guard_until_ms;
    if (state.side_smear &&
        state_side_direction &&
        side_direction &&
        !vertical_f0029_guard_active &&
        g_integrated_f0029_enabled) {
        const bool side_basic_start_frame = xmark_side_basic_start_frame_for_f0029(state.frame);
        const bool side_basic_visible_frame = xmark_side_basic_visible_frame_for_f0029(state.frame);
        const unsigned int side_basic_chain_ms =
            std::max(120u, env_uint("MINA_XMARK_F0029_SIDE_BASIC_CHAIN_MS", 260));
        const bool side_basic_chain_active =
            g_f0029_side_basic_chain_until_ms &&
            now_ms < g_f0029_side_basic_chain_until_ms;
        const bool f0029_render_active = active_f0029_render_effect_count(now_ms) > 0;
        const bool require_recent_attack_press = env_bool("MINA_XMARK_F0029_REQUIRE_RECENT_ATTACK_PRESS", false);
        const bool one_per_attack = env_bool("MINA_XMARK_F0029_D3D_ONE_PER_ATTACK", true);
        const bool fresh_attack_press =
            has_recent_attack_press &&
            g_f0029_last_consumed_attack_press_ms != g_attack_j_pressed_ms;
        const bool attack_press_required = require_recent_attack_press || (one_per_attack && has_recent_attack_press);
        const bool attack_press_available =
            !attack_press_required ||
            fresh_attack_press;
        int f0029_direction = probe_direction;
        if (side_basic_chain_active &&
            !fresh_attack_press &&
            xmark_direction_is_side(g_f0029_side_basic_chain_direction)) {
            f0029_direction = g_f0029_side_basic_chain_direction;
        }
        if (side_basic_visible_frame && side_basic_chain_active) {
            g_f0029_side_basic_chain_until_ms = now_ms + side_basic_chain_ms;
        }
        if (side_basic_visible_frame) {
            refresh_f0029_side_basic_lifetime(now_ms, f0029_direction, state.tick, state.draw);
        }
        const bool recover_missed_start_frame =
            env_bool("MINA_XMARK_F0029_RECOVER_MISSED_START_FRAME", true) &&
            side_basic_visible_frame &&
            !side_basic_start_frame &&
            !f0029_render_active &&
            !side_basic_chain_active;
        const bool d3d_spawn_frame_allowed =
            !env_bool("MINA_XMARK_F0029_D3D_START_FRAME_ONLY", true) ||
            side_basic_start_frame ||
            recover_missed_start_frame;
        const unsigned long long smear_lock_tick =
            g_basic_smear_animation_lock.active && g_basic_smear_animation_lock.source_tick
                ? g_basic_smear_animation_lock.source_tick
                : state.tick;
        const bool smear_lock_consumed =
            one_per_attack &&
            f0029_render_active &&
            smear_lock_tick &&
            g_f0029_last_consumed_smear_lock_tick == smear_lock_tick;
        const bool frame_tick_consumed =
            one_per_attack &&
            f0029_render_active &&
            state.tick &&
            g_f0029_last_consumed_basic_frame_tick == state.tick;
        const bool frame_draw_consumed =
            one_per_attack &&
            f0029_render_active &&
            state.draw &&
            g_f0029_last_consumed_basic_frame_draw == state.draw;
        const bool side_basic_chain_consumed =
            one_per_attack &&
            side_basic_chain_active &&
            !fresh_attack_press;
        if (d3d_spawn_frame_allowed &&
            attack_press_available &&
            !smear_lock_consumed &&
            !frame_tick_consumed &&
            !frame_draw_consumed &&
            !side_basic_chain_consumed &&
            now_ms - g_last_integrated_f0029_ms >= g_integrated_f0029_cooldown_ms) {
            const bool trust_frame_direction = env_bool("MINA_XMARK_F0029_D3D_TRUST_FRAME_DIRECTION", false);
            const int spawn_direction =
                trust_frame_direction && xmark_direction_is_side(state.direction)
                    ? state.direction
                    : (xmark_direction_is_side(f0029_direction) ? f0029_direction : resolved_direction);
            const Vec3 position = f0029_attack_position_for_direction(spawn_direction);
            if (g_mina && env_bool("MINA_XMARK_F0029_EVENT_LOG", false)) {
                char message[448]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "XMarkBurn integrated f0029 target reason=d3d12-basic-frame frame=%s recoveredStart=%u dir=%s resolvedDir=%s animLockDir=%s spawnDir=%s startDir=%s draw=%llu pos=(%.3f, %.3f, %.3f)\n",
                    state.frame[0] ? state.frame : "<unknown>",
                    recover_missed_start_frame ? 1u : 0u,
                    direction_name(state.direction),
                    direction_name(resolved_direction),
                    direction_name(animation_locked_direction),
                    direction_name(spawn_direction),
                    direction_name(g_attack_j_start_direction),
                    state.draw,
                    static_cast<double>(position.x),
                    static_cast<double>(position.y),
                    static_cast<double>(position.z));
                g_mina->Log(message);
            }
            const bool bind_to_side_basic =
                env_bool("MINA_XMARK_F0029_RENDER_BOUND_TO_SIDE_BASIC_FRAMES", true) &&
                side_basic_visible_frame;
            if (spawn_direct_f0029_attack_at(
                    "d3d12-basic-frame",
                    position,
                    spawn_direction,
                    bind_to_side_basic,
                    state.tick,
                    state.draw)) {
                g_last_integrated_f0029_ms = now_ms;
                g_f0029_last_consumed_smear_lock_tick = smear_lock_tick;
                g_f0029_last_consumed_basic_frame_tick = state.tick;
                g_f0029_last_consumed_basic_frame_draw = state.draw;
                g_f0029_last_consumed_basic_frame_direction = spawn_direction;
                g_f0029_side_basic_chain_direction = spawn_direction;
                g_f0029_side_basic_chain_until_ms = now_ms + side_basic_chain_ms;
                if (has_recent_attack_press) {
                    g_f0029_last_consumed_attack_press_ms = g_attack_j_pressed_ms;
                }
            }
        } else if (g_mina &&
            env_bool("MINA_XMARK_F0029_D3D_SKIP_LOG", true) &&
            now_ms >= g_last_f0029_d3d_skip_log_ms + env_uint("MINA_XMARK_F0029_D3D_SKIP_LOG_MS", 300)) {
            g_last_f0029_d3d_skip_log_ms = now_ms;
            char message[448]{};
            std::snprintf(
                message,
                sizeof(message),
                    "XMarkBurn integrated f0029 d3d skip frame=%s dir=%s frameAllowed=%u available=%u recentPress=%u consumed=%u cooldownAgeMs=%llu cooldownMs=%u\n",
                    state.frame[0] ? state.frame : "<unknown>",
                    direction_name(resolved_direction),
                    d3d_spawn_frame_allowed ? 1u : 0u,
                    attack_press_available ? 1u : 0u,
                    has_recent_attack_press ? 1u : 0u,
                    (smear_lock_consumed || frame_tick_consumed || frame_draw_consumed ||
                     side_basic_chain_consumed ||
                     (has_recent_attack_press && g_f0029_last_consumed_attack_press_ms == g_attack_j_pressed_ms)) ? 1u : 0u,
                    static_cast<unsigned long long>(now_ms - g_last_integrated_f0029_ms),
                    g_integrated_f0029_cooldown_ms);
            g_mina->Log(message);
        }
        activate_basic_attack_contact_probe(now_ms, probe_direction, "d3d12-side-basic-frame");
        maybe_mark_muriel_on_current_basic_frame(now_ms);
        return;
    }

    activate_basic_attack_contact_probe(now_ms, probe_direction, "d3d12-basic-frame");
    maybe_mark_muriel_on_current_basic_frame(now_ms);
}

