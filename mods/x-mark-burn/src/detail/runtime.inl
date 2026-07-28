bool rtti_valid(const MM_Rtti rtti) {
    return rtti.typeId != 0;
}

bool rtti_equal(const MM_Rtti lhs, const MM_Rtti rhs) {
    return lhs.typeId == rhs.typeId;
}

MM_Rtti rtti_from_hash(const uint64_t hash) {
    return MM_Rtti{hash};
}

MinaModAPI *g_mina = nullptr;
World *g_last_world = nullptr;
char g_state_path[MAX_PATH * 4]{};
char g_hud_state_path[MAX_PATH * 4]{};
char g_hud_layout_state_path[MAX_PATH * 4]{};
char g_trace_path[MAX_PATH * 4]{};
char g_basic_frame_state_path[MAX_PATH * 4]{};
char g_basic_hit_state_path[MAX_PATH * 4]{};
char g_enemy_visual_state_path[MAX_PATH * 4]{};
char g_command_path[MAX_PATH * 4]{};
unsigned int g_last_room = 0xFFFFFFFFu;
unsigned int g_last_official_enemy_room = 0xFFFFFFFFu;
unsigned long long g_last_official_enemy_room_scan_ms = 0;
unsigned long long g_last_write_ms = 0;
unsigned long long g_last_hud_write_ms = 0;
unsigned long long g_last_hud_layout_scan_ms = 0;
uintptr_t g_muriel_regular_dialogue_targets[8]{};
unsigned int g_muriel_regular_dialogue_target_count = 0;
unsigned int g_muriel_regular_dialogue_scan_attempts = 0;
unsigned long long g_last_muriel_regular_dialogue_scan_ms = 0;
bool g_muriel_regular_dialogue_burn_text_active = false;
unsigned long long g_last_command_poll_ms = 0;
unsigned int g_write_interval_ms = 1000;
unsigned int g_hud_write_interval_ms = 80;
bool g_last_hud_state_had_active = false;
unsigned long long g_xmark_timer_last_real_ms = 0;
unsigned int g_xmark_timer_last_room = 0xFFFFFFFFu;
float g_xmark_timer_last_room_time = 0.0f;
bool g_xmark_runtime_overlay_hidden_for_pause = false;
unsigned long long g_xmark_pause_roomtime_still_ms = 0;
World *g_xmark_game_clock_world = nullptr;
unsigned long long g_xmark_game_clock_ms = 0;
double g_xmark_game_clock_fraction_ms = 0.0;
unsigned long long g_xmark_game_clock_last_real_ms = 0;
unsigned long long g_xmark_game_clock_updates = 0;
float g_xmark_game_clock_last_elapsed = 0.0f;
bool g_xmark_game_clock_ready = false;
bool g_xmark_game_clock_activation_logged = false;
bool g_paths_initialized = false;
bool g_clashrend_text_patch_complete = false;
bool g_clashrend_text_patch_final_miss = false;
unsigned int g_clashrend_text_patch_attempts = 0;
unsigned long long g_clashrend_text_patch_next_ms = 0;
bool g_clashrend_text_patch_scan_active = false;
uintptr_t g_clashrend_text_patch_scan_cursor = 0;
uintptr_t g_clashrend_text_patch_scan_max = 0;
uintptr_t g_clashrend_text_patch_region_base = 0;
uintptr_t g_clashrend_text_patch_region_cursor = 0;
uintptr_t g_clashrend_text_patch_region_end = 0;
bool g_clashrend_text_patch_name = false;
bool g_clashrend_text_patch_description = false;
bool g_clashrend_text_patch_upgrade_name = false;
bool g_clashrend_text_patch_upgrade_description = false;
bool g_clashrend_text_patch_upgrade_manual_title = false;
int g_clashrend_transition_previous_game_state = -1;
unsigned int g_clashrend_transition_prewarmed_room = 0xFFFFFFFFu;
unsigned int g_clashrend_transition_prewarm_stage = 0;
MM_WeakPtr *g_weapon_shadow_anim_weak = nullptr;
uintptr_t g_weapon_shadow_anim_component = 0;
unsigned long long g_weapon_shadow_last_scan_ms = 0;
bool g_trace_pseudo_rooms = false;
bool g_room_state_write_enabled = true;
bool g_command_file_enabled = true;
bool g_debug_keys_enabled = false;
bool g_attachment_lab_enabled = false;
bool g_test_runtime_enabled = false;
bool g_force_default_claymore_enabled = false;
bool g_player_world_gate_enabled = true;
bool g_use_world_update_attachment = true;
bool g_use_entity_post_art_queue = true;
bool g_use_entity_hit_update_queue = true;
bool g_hit_queue_owns_basic_health = true;
bool g_use_hud_update_queue = true;
bool g_hud_layout_reporter_enabled = false;
bool g_official_scan_in_world_update = false;
bool g_post_art_refresh_official_snapshot = false;
bool g_marker_debug_draw_warmup = true;
bool g_burn_debug_draw_warmup = true;
bool g_native_spawn_enabled = true;
bool g_native_auto_spawn = false;
bool g_native_auto_spawn_done = false;
unsigned int g_native_spawn_limit = 4096;
unsigned int g_native_spawn_count = 0;
bool g_native_direct_enabled = true;
bool g_integrated_f0029_enabled = false;
bool g_native_crater_enabled = false;
bool g_native_f0029_direct_skip_logged = false;
unsigned int g_integrated_f0029_cooldown_ms = 160;
unsigned int g_native_direct_count = 0;
unsigned int g_native_f0029_count = 0;
unsigned int g_native_crater_count = 0;
unsigned long long g_last_muriel_host_log_ms = 0;
uintptr_t g_last_muriel_host_log_entity = 0;
bool g_test_enemy_harness_enabled = false;
bool g_test_enemy_auto_spawn = false;
bool g_test_enemy_auto_spawn_done = false;
unsigned int g_test_enemy_spawn_count = 0;
unsigned long long g_last_test_enemy_manual_spawn_ms = 0;
uint32_t g_test_enemy_type = ENTITYTYPE_TEST_TRAINING_DUMMY;
uintptr_t g_last_test_enemy_entity = 0;
unsigned long long g_last_test_enemy_resolved_ms = 0;
bool g_test_enemy_resolve_pending = false;
unsigned long long g_test_enemy_resolve_until_ms = 0;
unsigned long long g_last_test_enemy_resolve_scan_ms = 0;
uintptr_t g_test_enemy_pre_spawn_entities[128]{};
unsigned int g_test_enemy_pre_spawn_count = 0;
bool g_runtime_collect_include_non_health_for_test = false;
bool g_win_g_was_down = false;
bool g_win_h_was_down = false;
bool g_win_m_was_down = false;
bool g_win_n_was_down = false;
bool g_win_t_was_down = false;
bool g_attack_j_was_down = false;
bool g_attack_j_latched = false;
bool g_attack_j_pending = false;
bool g_attack_j_basic_release_seen = false;
bool g_attack_crater_pending = false;
bool g_attack_burn_consume_pending = false;
bool g_attack_smear_contact_pending = false;
bool g_input_win_j_down = false;
bool g_input_win_j_press = false;
bool g_input_api_key_j_down = false;
bool g_input_api_key_j_held = false;
bool g_input_api_action_attack_down = false;
bool g_input_api_action_attack_held = false;
bool g_input_api_action_attack_pressed = false;
bool g_input_api_action_attack_was_active = false;
bool g_input_api_action_subweapon_down = false;
bool g_input_api_action_subweapon_held = false;
unsigned long long g_last_subweapon_action_ms = 0;
bool g_input_api_bpad_left_down = false;
bool g_input_api_bpad_left_held = false;
bool g_input_api_bpad_left_pressed = false;
bool g_input_api_bpad_left_was_active = false;
bool g_input_api_bpad_left_was_down = false;
bool g_input_attack_down = false;
int g_input_first_action_down = -1;
int g_input_first_action_held = -1;
int g_input_first_button_down = -1;
int g_input_first_button_held = -1;
unsigned long long g_last_input_probe_log_ms = 0;
unsigned long long g_last_integrated_f0029_ms = 0;
unsigned long long g_f0029_last_consumed_attack_press_ms = 0;
unsigned long long g_f0029_last_consumed_smear_lock_tick = 0;
unsigned long long g_f0029_last_consumed_basic_frame_tick = 0;
unsigned long long g_f0029_last_consumed_basic_frame_draw = 0;
int g_f0029_last_consumed_basic_frame_direction = 1;
unsigned long long g_f0029_side_basic_chain_until_ms = 0;
int g_f0029_side_basic_chain_direction = 1;
unsigned long long g_last_vertical_smear_ms = 0;
unsigned long long g_vertical_basic_f0029_guard_until_ms = 0;
unsigned long long g_vertical_smear_last_source_tick = 0;
unsigned long long g_vertical_smear_last_source_draw = 0;
int g_vertical_smear_last_direction = 4;
unsigned long long g_vertical_smear_last_consumed_attack_press_ms = 0;
unsigned long long g_last_native_crater_ms = 0;
unsigned long long g_attack_j_pending_ms = 0;
unsigned long long g_attack_crater_pending_ms = 0;
unsigned long long g_attack_burn_consume_pending_ms = 0;
unsigned long long g_attack_smear_contact_pending_ms = 0;
unsigned long long g_last_charged_consume_ms = 0;
unsigned int g_last_charged_consume_consumed = 0;
unsigned int g_last_charged_consume_candidates = 0;
int g_last_charged_consume_direction = 1;
float g_last_charged_consume_impact_x = 0.0f;
float g_last_charged_consume_impact_y = 0.0f;
float g_last_charged_consume_radius_x = 0.0f;
float g_last_charged_consume_radius_y = 0.0f;
float g_last_charged_consume_effective_radius_x = 0.0f;
float g_last_charged_consume_effective_radius_y = 0.0f;
unsigned long long g_last_charged_frame_consume_tick = 0;
unsigned long long g_last_charged_frame_consume_draw = 0;
unsigned long long g_attack_j_pressed_ms = 0;
unsigned long long g_attack_j_basic_release_held_ms = 0;
unsigned long long g_last_basic_frame_state_tick = 0;
unsigned long long g_last_basic_frame_hit_trace_tick = 0;
unsigned long long g_last_basic_frame_hit_mark_tick = 0;
unsigned long long g_last_muriel_mark_attack_press_ms = 0;
unsigned long long g_last_muriel_mark_frame_tick = 0;
unsigned long long g_last_muriel_mark_frame_draw = 0;
unsigned long long g_last_basic_frame_hit_log_ms = 0;
unsigned long long g_last_basic_frame_hit_eval_ms = 0;
unsigned long long g_last_f0029_d3d_skip_log_ms = 0;
int g_attack_j_pending_direction = 1;
int g_attack_crater_pending_direction = 1;
int g_attack_burn_consume_pending_direction = 1;
int g_attack_smear_contact_pending_direction = 1;
int g_attack_j_active_direction = 1;
int g_attack_j_start_direction = 1;
int g_last_direction = 1;
int g_last_side_direction = 1;
bool g_attack_j_charge_crater_fired = false;
bool g_attack_j_charge_crater_ready = false;
bool g_attack_j_charge_burn_fired = false;
uintptr_t g_rejected_spawned_effects[256]{};
unsigned int g_rejected_spawned_effect_count = 0;
bool g_attachment_lab_marked = false;
bool g_attachment_lab_auto_start_save_done = false;
bool g_attachment_lab_force_game_state_done = false;
unsigned long long g_attachment_lab_auto_start_save_ms = 0;
bool g_test_save_writes_disabled = false;
uintptr_t g_attachment_lab_target = 0;
unsigned int g_attachment_lab_anchor_offset = 0;
float g_attachment_lab_base_anchor_x = 0.0f;
float g_attachment_lab_base_anchor_y = 0.0f;
float g_attachment_lab_base_anchor_z = 0.0f;
unsigned long long g_attachment_lab_last_log_ms = 0;
unsigned long long g_attachment_lab_first_update_ms = 0;
unsigned long long g_last_force_claymore_log_ms = 0;
bool g_xmark_render_backend_enabled = true;
bool g_xmark_render_backend_required = false;
bool g_xmark_render_backend_available = false;
bool g_xmark_render_backend_ready = false;
bool g_xmark_render_backend_logged_unavailable = false;
bool g_xmark_render_backend_logged_ready = false;
unsigned int g_xmark_render_backend_quads = 0;
unsigned long long g_xmark_render_draw_calls = 0;
unsigned int g_xmark_render_last_draw_quads = 0;
float g_xmark_render_last_quad_x = 0.0f;
float g_xmark_render_last_quad_y = 0.0f;
float g_xmark_render_last_quad_z = 0.0f;
float g_xmark_render_last_quad_half_w = 0.0f;
float g_xmark_render_last_quad_half_h = 0.0f;
unsigned int g_xmark_render_last_quad_frame = 0;
bool g_xmark_render_last_quad_valid = false;
char g_xmark_render_backend_status[96] = "not-checked";
ycTexture *g_xmark_render_texture = nullptr;
ycTexture *g_xmark_marker_debug_textures[6]{};
ycTexture *g_xmark_hud_debug_texture = nullptr;
ycTexture *g_xmark_hud_fire_debug_texture = nullptr;
ycTexture *g_xmark_burn_debug_fire_textures[5]{};
ycTexture *g_xmark_burn_debug_shade_textures[2]{};
ycTexture *g_clashrend_boom_star_debug_textures[5]{};
ycTexture *g_clashrend_boom_ground_debug_textures[2]{};
ycTexture *g_clashrend_boom_native_dome_debug_textures[5]{};
ycTexture *g_clashrend_boom_wave_x_debug_textures[5]{};
ycTexture *g_clashrend_boom_wave_plus_debug_textures[5]{};
bool g_clashrend_boom_debug_draw_ready = false;
bool g_clashrend_boom_debug_draw_logged_ready = false;
ycTexture *g_xmark_hud_render_texture = nullptr;
bool g_xmark_marker_debug_draw_available = false;
bool g_xmark_marker_debug_draw_ready = false;
bool g_xmark_hud_debug_draw_ready = false;
bool g_xmark_burn_debug_draw_ready = false;
unsigned int g_xmark_marker_debug_draw_init_frame = 0;
unsigned int g_xmark_burn_debug_draw_init_frame = 0;
bool g_xmark_hud_render_backend_available = false;
bool g_xmark_hud_render_backend_ready = false;
bool g_xmark_marker_debug_draw_logged_unavailable = false;
bool g_xmark_marker_debug_draw_logged_ready = false;
bool g_xmark_hud_debug_draw_logged_ready = false;
bool g_xmark_burn_debug_draw_logged_ready = false;
bool g_xmark_hud_render_backend_logged_ready = false;
unsigned long long g_xmark_marker_debug_draw_calls = 0;
unsigned long long g_xmark_hud_debug_draw_calls = 0;
unsigned long long g_xmark_burn_debug_draw_calls = 0;
bool g_xmark_burn_debug_draw_phase = false;
unsigned long long g_xmark_burn_debug_draw_last_world_ms = 0;
unsigned long long g_xmark_hud_render_draw_calls = 0;
unsigned int g_xmark_marker_debug_last_frame = 0;
unsigned int g_xmark_hud_render_quads = 0;
unsigned int g_xmark_hud_world_render_quads = 0;
float g_xmark_hud_world_render_last_x = 0.0f;
float g_xmark_hud_world_render_last_y = 0.0f;
char g_xmark_marker_debug_draw_status[96] = "not-checked";
char g_xmark_hud_debug_draw_status[96] = "not-checked";
char g_xmark_burn_debug_draw_status[96] = "not-checked";
char g_xmark_hud_render_backend_status[96] = "not-checked";
constexpr unsigned int kXMarkRenderVertexBufferCount = 8;
ycGpuBuffer *g_xmark_render_vertex_buffers[kXMarkRenderVertexBufferCount]{};
ycGpuBuffer *g_xmark_render_vertex_buffer = nullptr;
unsigned int g_xmark_render_active_vertex_buffer = 0;
unsigned int g_xmark_render_next_vertex_buffer = 1;
ycGpuBuffer *g_xmark_render_index_buffer = nullptr;
MinaModRenderObject *g_xmark_render_object = nullptr;
ycGpuBuffer *g_xmark_hud_render_vertex_buffer = nullptr;
ycGpuBuffer *g_xmark_hud_render_index_buffer = nullptr;
MinaModRenderObject *g_xmark_hud_render_object = nullptr;
bool g_attack_overlay_render_backend_available = false;
bool g_attack_overlay_render_backend_ready = false;
bool g_attack_overlay_render_backend_logged_ready = false;
unsigned int g_attack_overlay_render_quads = 0;
unsigned long long g_attack_overlay_render_draw_calls = 0;
unsigned long long g_attack_overlay_render_last_visible_ms = 0;
unsigned int g_attack_overlay_render_last_frame = 0;
char g_attack_overlay_render_backend_status[96] = "not-checked";
ycTexture *g_attack_overlay_render_texture = nullptr;
ycGpuBuffer *g_attack_overlay_render_vertex_buffer = nullptr;
ycGpuBuffer *g_attack_overlay_render_index_buffer = nullptr;
MinaModRenderObject *g_attack_overlay_render_object = nullptr;

struct Vec3 {
    float x;
    float y;
    float z;
};

float xy_distance_sq(const Vec3 &a, const Vec3 &b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

Vec3 g_last_test_enemy_position{0.0f, 0.0f, 0.0f};

enum FacingDirection {
    FacingRight = 1,
    FacingLeft = 2,
    FacingUp = 3,
    FacingDown = 4,
};

struct ClashrendBoomPatternState {
    struct NativeAnim {
        ycComponent *component = nullptr;
        MM_Vec3 local_position{};
        Vec3 world_position{};
        bool corner = false;
    };

    bool pending = false;
    bool active = false;
    bool first_wave_spawned = false;
    bool second_wave_spawned = false;
    bool third_wave_spawned = false;
    bool third_wave_reasserted = false;
    bool third_wave_native_effects_spawned = false;
    bool debug_draw_logged = false;
    bool native_pattern_ready = false;
    unsigned int native_suppression_scans = 0;
    unsigned int native_suppressed_anims = 0;
    unsigned int native_capture_scans = 0;
    unsigned int native_outer_count = 0;
    unsigned int native_last_scan_nodes = 0;
    unsigned int native_largest_child_count = 0;
    unsigned int native_truncated_entities = 0;
    int direction = FacingRight;
    unsigned long long released_ms = 0;
    unsigned long long started_ms = 0;
    unsigned long long second_wave_started_ms = 0;
    unsigned long long third_wave_started_ms = 0;
    unsigned long long native_next_scan_ms = 0;
    Vec3 impact{};
    NativeAnim native_outer[8]{};
    ycComponent *native_center = nullptr;
    MM_Vec3 native_center_local{};
};

ClashrendBoomPatternState g_clashrend_boom_pattern{};
unsigned long long g_clashrend_boom_last_native_impact_sequence = 0;

struct ClashrendBoomEmber {
    bool active = false;
    Vec3 origin{};
    Vec3 landing{};
    Vec3 position{};
    Vec3 velocity{};
    float arc_height = 0.0f;
    unsigned int flight_ms = 0;
    unsigned long long started_ms = 0;
    unsigned long long expires_ms = 0;
    uintptr_t hit_cores[8]{};
    unsigned int hit_core_count = 0;
};

struct ClashrendBoomGeyserState {
    bool active = false;
    Vec3 center{};
    unsigned long long started_ms = 0;
    unsigned long long expires_ms = 0;
    unsigned int ember_count = 0;
    ClashrendBoomEmber embers[12]{};
};

ClashrendBoomGeyserState g_clashrend_boom_geyser{};

Vec3 clashrend_boom_direction_step(int direction, float distance);
Vec3 clashrend_boom_native_forward_step(int direction);
bool clashrend_boom_matches_native_explosion_anim(ycComponent *anim, const Vec3 &impact);
void clashrend_boom_debug_draw_geyser(unsigned long long now_ms);

struct YcStringView {
    const char *data;
    size_t length;
};

struct XMarkEnemyPlacement {
    const char *level_path;
    const char *level_key;
    const char *room_name;
    unsigned int entity_index;
    const char *local_entry;
    const char *catalog_name;
    float editor_x;
    float editor_y;
};

#include "xmark_enemy_placements.inc"
#include "xmark_known_enemy_types.inc"

struct XMarkDatabaseTarget {
    const XMarkEnemyPlacement *placement;
    Vec3 position;
    float distance_sq;
};

struct XMarkRuntimeTarget {
    uintptr_t entity;
    uintptr_t source_base;
    uintptr_t source_offset;
    unsigned long long visual_key;
    uintptr_t official_combat_core;
    Vec3 anchor;
    Vec3 position;
    float distance_sq;
    float score;
    float forward_delta;
    float lateral_delta;
    float health_value;
    float health_max;
    float render_half_w;
    float render_half_h;
    float contact_half_w;
    float contact_half_h;
    unsigned int anchor_offset;
    unsigned int health_offset;
    unsigned int health_kind;
    unsigned int visual_texture_width;
    unsigned int visual_texture_height;
    bool facing_match;
    bool health_like;
    bool visual_follow;
    bool official_follow;
    bool suppress_hud;
    char visual_entry[96];
    char visual_stem[64];
    char visual_catalog[96];
};

struct XMarkVisualEnemyHost {
    bool active;
    unsigned long long key;
    unsigned long long texture_signature;
    unsigned long long last_seen_ms;
    unsigned int texture_width;
    unsigned int texture_height;
    Vec3 position;
    float half_w;
    float half_h;
    float distance_sq;
    unsigned long long last_hurt_flash_ms;
    bool recent_hurt;
    char entry[96];
    char stem[64];
    char catalog[96];
};

struct XMarkBasicHitCandidate {
    bool active;
    unsigned long long key;
    unsigned long long last_overlap_ms;
    unsigned long long last_hurt_ms;
    unsigned long long last_mark_ms;
    unsigned long long last_frame_tick;
    int direction;
    XMarkVisualEnemyHost host;
};

struct XMarkOfficialEnemyHost {
    bool active;
    uintptr_t entity;
    uintptr_t combat_core;
    uintptr_t game_anim;
    MM_WeakPtr *entity_weak;
    MM_WeakPtr *combat_core_weak;
    MM_WeakPtr *game_anim_weak;
    uintptr_t spawn_point;
    uintptr_t spawned_component;
    MM_WeakPtr *spawned_component_weak;
    uint64_t spawn_name_level_hash;
    uint32_t spawn_name_hash;
    uint32_t spawn_layer_hash;
    uint32_t spawn_entity_type;
    uint32_t registry_generation;
    uint8_t spawn_type;
    uint8_t spawn_tile_layer;
    bool spawn_identity_valid;
    Vec3 entity_position;
    Vec3 body_center_offset;
    Vec3 position;
    float bounds_half_w;
    float bounds_half_h;
    float visual_half_w;
    float visual_half_h;
    float health;
    float health_max;
    unsigned long long last_seen_ms;
    bool bounds_valid;
    bool visual_bounds_valid;
    bool body_center_offset_valid;
    bool muriel_no_damage;
    char component_type[64];
};

XMarkVisualEnemyHost g_visual_enemy_hosts[64]{};
unsigned int g_visual_enemy_host_count = 0;
unsigned long long g_visual_enemy_state_tick = 0;
XMarkBasicHitCandidate g_basic_hit_candidates[32]{};
unsigned long long g_last_visual_enemy_state_read_ms = 0;
bool g_visual_enemy_state_explicit_zero = false;
XMarkOfficialEnemyHost g_official_enemy_hosts[128]{};
unsigned int g_official_enemy_host_count = 0;
unsigned int g_official_enemy_scan_nodes = 0;
unsigned int g_official_enemy_scan_faults = 0;
unsigned long long g_last_official_enemy_scan_ms = 0;
unsigned long long g_last_official_enemy_log_ms = 0;
bool g_official_enemy_snapshot_valid = false;
bool g_official_enemy_api_fault = false;
unsigned int g_official_enemy_registry_generation = 0;
unsigned long long g_last_official_enemy_registry_reconcile_ms = 0;
unsigned long long g_last_official_enemy_lifecycle_batch_ms = 0;
unsigned long long g_official_enemy_registry_rescan_requested_ms = 0;
unsigned int g_official_enemy_attack_discovery_room = 0xFFFFFFFFu;
unsigned int g_official_enemy_attack_discovery_attempts = 0;
unsigned long long g_last_official_enemy_attack_discovery_ms = 0;
unsigned long long g_official_enemy_attack_discovery_event = 0;
unsigned long long g_basic_attack_discovery_event_counter = 0;
unsigned long long g_official_enemy_registry_reconcile_count = 0;
unsigned long long g_official_enemy_registry_targeted_refresh_count = 0;
unsigned long long g_official_enemy_registry_invalid_count = 0;
unsigned int g_official_enemy_lifecycle_cursor = 0;
World *g_xmark_update_queue_world = nullptr;
void *g_xmark_entity_post_art_queue_handle = nullptr;
unsigned long long g_last_xmark_post_art_update_ms = 0;
unsigned long long g_last_xmark_post_art_queue_log_ms = 0;
bool g_xmark_entity_post_art_queue_registered = false;
bool g_xmark_entity_post_art_queue_failed = false;
World *g_xmark_entity_hit_queue_world = nullptr;
void *g_xmark_entity_hit_queue_handle = nullptr;
unsigned long long g_last_xmark_entity_hit_update_ms = 0;
unsigned long long g_last_xmark_entity_hit_probe_update_ms = 0;
unsigned long long g_last_xmark_entity_hit_register_attempt_ms = 0;
unsigned long long g_xmark_entity_hit_queue_callbacks = 0;
unsigned long long g_xmark_entity_hit_queue_probe_updates = 0;
bool g_xmark_entity_hit_queue_registered = false;
bool g_xmark_entity_hit_queue_failed = false;
World *g_xmark_hud_update_queue_world = nullptr;
void *g_xmark_hud_update_queue_handle = nullptr;
unsigned long long g_last_xmark_hud_update_ms = 0;
unsigned long long g_last_xmark_hud_register_attempt_ms = 0;
bool g_xmark_hud_update_queue_registered = false;
bool g_xmark_hud_update_queue_failed = false;
struct XMarkNormalDeathProbeLog {
    uintptr_t entity;
    uintptr_t combat_core;
    unsigned long long logged_ms;
};
XMarkNormalDeathProbeLog g_normal_death_probe_logs[64]{};
struct XMarkNativeFinalHitDeathWatch {
    bool active;
    XMarkOfficialEnemyHost host;
    unsigned long long next_probe_ms;
    unsigned int step;
    unsigned int max_steps;
};
XMarkNativeFinalHitDeathWatch g_native_final_hit_death_watches[8]{};
MM_Rtti g_official_entity_rtti{};
MM_Rtti g_official_combat_core_rtti{};
MM_Rtti g_official_game_component_rtti{};
bool g_official_rtti_initialized = false;
MM_Rtti g_game_anim_rtti{};
bool g_game_anim_rtti_initialized = false;
unsigned long long g_modapi_basic_frame_counter = 0;
unsigned long long g_last_modapi_basic_frame_log_ms = 0;
unsigned int g_modapi_anim_scan_nodes = 0;
unsigned int g_modapi_anim_scan_hits = 0;
char g_modapi_anim_last_seq[64]{};
char g_modapi_anim_last_seq_full[96]{};
uint32_t g_modapi_anim_last_frame_idx = 0;
char g_modapi_anim_seq_list[512]{};
MM_WeakPtr *g_player_attack_anim_weak = nullptr;
uintptr_t g_player_attack_anim_component = 0;
char g_player_attack_anim_observed_seq[96]{};
uint32_t g_player_attack_anim_observed_frame_idx = 0xFFFFFFFFu;
uint32_t g_player_attack_anim_observed_loops = 0xFFFFFFFFu;
unsigned long long g_player_attack_anim_observed_tick = 0;
unsigned long long g_player_attack_anim_observed_draw = 0;
uintptr_t g_anim_property_probed_components[32]{};
unsigned int g_anim_property_probed_component_count = 0;

struct PlayerHammerAnimTraceSlot {
    uintptr_t component;
    uint32_t frame_idx;
    char seq[64];
    char seq_full[96];
};
PlayerHammerAnimTraceSlot g_player_hammer_anim_trace_slots[128]{};
unsigned long long g_player_hammer_anim_trace_last_flag_poll_ms = 0;
bool g_player_hammer_anim_trace_enabled = false;
bool g_player_hammer_anim_trace_was_enabled = false;
char g_player_hammer_anim_trace_flag_path[MAX_PATH * 4]{};
char g_player_hammer_anim_trace_output_path[MAX_PATH * 4]{};

struct BoomChargeAnimTraceSlot {
    uintptr_t component;
    uint32_t frame_idx;
    bool visible;
    char seq[64];
    char seq_full[96];
};
BoomChargeAnimTraceSlot g_boom_charge_anim_trace_slots[256]{};
bool g_boom_charge_anim_trace_active = false;
unsigned long long g_boom_charge_anim_trace_started_ms = 0;
unsigned long long g_boom_charge_anim_trace_until_ms = 0;
unsigned long long g_boom_charge_anim_trace_next_scan_ms = 0;
unsigned int g_boom_charge_anim_trace_sequence = 0;
int g_boom_charge_anim_trace_direction = FacingRight;
Vec3 g_boom_charge_anim_trace_origin{};
char g_boom_charge_anim_trace_output_path[MAX_PATH * 4]{};

constexpr unsigned int kXMarkPositionSlotMax = 8;
constexpr unsigned int kXMarkStaticRenderAnchor = 0xFFFFFFFFu;

struct XMarkAttachment {
    bool active;
    uintptr_t target;
    uintptr_t effect;
    uintptr_t child;
    unsigned long long visual_key;
    uintptr_t target_source_base;
    uintptr_t target_source_offset;
    uintptr_t effect_position_base;
    uintptr_t child_position_base;
    unsigned int effect_position_offset;
    unsigned int child_position_offset;
    unsigned int effect_position_offsets[kXMarkPositionSlotMax];
    unsigned int child_position_offsets[kXMarkPositionSlotMax];
    uintptr_t extra_position_bases[kXMarkPositionSlotMax];
    unsigned int extra_position_offsets[kXMarkPositionSlotMax];
    unsigned int effect_position_count;
    unsigned int child_position_count;
    unsigned int extra_position_count;
    unsigned int target_anchor_offset;
    unsigned int target_health_offset;
    unsigned int target_health_kind;
    uintptr_t official_combat_core;
    unsigned long long expires_ms;
    unsigned long long started_ms;
    unsigned long long last_reacquire_ms;
    unsigned long long last_visual_resolved_ms;
    unsigned long long last_visual_missing_ms;
    unsigned long long last_visual_host_seen_ms;
    unsigned int visual_missing_count;
    Vec3 last_position;
    Vec3 last_visual_host_render_position;
    float visual_velocity_x;
    float visual_velocity_y;
    float render_half_w;
    float render_half_h;
    float contact_half_w;
    float contact_half_h;
    float observed_health;
    float health_before_recent_drop;
    float health_after_recent_drop;
    float last_basic_damage;
    unsigned long long observed_health_ms;
    unsigned long long recent_health_drop_ms;
    unsigned long long last_basic_damage_ms;
    bool has_last_position;
    bool has_observed_health;
    bool has_visual_velocity;
    bool has_runtime_visual_offset;
    bool has_official_body_offset;
    bool render_backend;
    bool marker_debug_draw;
    bool visual_follow;
    bool official_follow;
    bool target_health_like;
    bool suppress_hud;
    bool apply_sfx_pending;
    char visual_entry[96];
    char visual_stem[64];
    char visual_catalog[96];
    Vec3 runtime_visual_offset;
    Vec3 official_body_offset;
    unsigned int status_id;
};

XMarkAttachment g_xmark_attachments[16]{};

struct XMarkChargedConsumeProbe {
    bool active;
    uintptr_t target;
    uintptr_t official_combat_core;
    unsigned long long visual_key;
    unsigned long long started_ms;
    unsigned long long expires_ms;
    float baseline_health;
    Vec3 impact_position;
    int direction;
    char reason[64];
};

XMarkChargedConsumeProbe g_charged_consume_probes[16]{};

struct XMarkBurnEffect {
    bool active;
    uintptr_t target;
    unsigned long long visual_key;
    uintptr_t target_source_base;
    uintptr_t target_source_offset;
    unsigned int target_anchor_offset;
    unsigned int target_health_offset;
    unsigned int target_health_kind;
    uintptr_t official_combat_core;
    MM_WeakPtr *palette_anim_weak;
    ycPaletteTexture *original_palette;
    ycPaletteTexture *burn_palette;
    ycPaletteTexture *burn_palette_hot;
    ycPaletteTexture *burn_palette_bright;
    MM_WeakPtr *palette_tree_anim_weak[32];
    ycPaletteTexture *palette_tree_original[32];
    unsigned int palette_tree_count;
    unsigned long long palette_apply_after_ms;
    unsigned long long palette_next_switch_ms;
    unsigned long long palette_next_anim_resolve_ms;
    unsigned long long palette_restore_until_ms;
    unsigned long long palette_next_restore_ms;
    unsigned int palette_phase;
    unsigned long long started_ms;
    unsigned long long expires_ms;
    unsigned long long next_tick_ms;
    unsigned long long last_tick_ms;
    unsigned long long lethal_written_ms;
    unsigned long long last_visual_resolved_ms;
    unsigned long long last_visual_missing_ms;
    unsigned long long last_visual_host_seen_ms;
    unsigned int visual_missing_count;
    unsigned int tick_count;
    unsigned int tick_attempt_count;
    unsigned int tick_applied_count;
    unsigned int tick_failed_count;
    unsigned int visual_emit_frame_count;
    Vec3 last_position;
    Vec3 lethal_freeze_position;
    Vec3 last_visual_host_render_position;
    Vec3 runtime_visual_offset;
    float visual_velocity_x;
    float visual_velocity_y;
    float render_half_w;
    float render_half_h;
    bool has_last_position;
    bool has_visual_velocity;
    bool has_runtime_visual_offset;
    bool visual_follow;
    bool official_follow;
    bool target_health_like;
    bool suppress_damage;
    bool palette_applied;
    bool palette_restore_pending;
    bool lethal_cleanup_applied;
    bool has_lethal_freeze_position;
    bool lethal_freeze_logged;
    bool death_probe_pre_logged;
    bool death_probe_post_logged;
    bool death_anim_play_attempted;
    bool scripted_boss_guard_logged;
    unsigned long long death_anim_verify_next_ms;
    unsigned int death_anim_verify_step;
    char visual_entry[96];
    char visual_stem[64];
    char visual_catalog[96];
    unsigned int status_id;
};

XMarkBurnEffect g_xmark_burn_effects[16]{};

enum class XMarkEnemyStatusPhase : unsigned int {
    None = 0,
    Marked,
    Burning,
    Dying
};

struct XMarkEnemyStatusRecord {
    bool active;
    bool training_target;
    uintptr_t entity;
    uintptr_t combat_core;
    uintptr_t spawn_point;
    MM_WeakPtr *entity_weak;
    MM_WeakPtr *combat_core_weak;
    unsigned int attachment_index;
    unsigned int burn_index;
    XMarkEnemyStatusPhase phase;
    unsigned long long state_started_ms;
    unsigned long long state_expires_ms;
    unsigned long long last_lifecycle_ms;
    float health;
    float health_max;
    ycPaletteTexture *marked_original_palette;
};

XMarkEnemyStatusRecord g_xmark_enemy_status[32]{};
void capture_xmark_marked_palette(XMarkEnemyStatusRecord &status);
void transfer_xmark_marked_palette_to_burn(
    XMarkEnemyStatusRecord &status,
    XMarkBurnEffect &burn);
unsigned int g_xmark_enemy_lifecycle_cursor = 0;
unsigned long long g_xmark_last_apply_sfx_ms = 0;
unsigned long long g_xmark_burn_last_ignite_sfx_ms = 0;
unsigned long long g_xmark_burn_last_tick_sfx_ms = 0;
bool g_xmark_burn_tick_sfx_owned = false;
struct XMarkApplySfxObservation {
    uintptr_t target;
    uintptr_t official_combat_core;
    unsigned long long visual_key;
    unsigned long long started_ms;
};
XMarkApplySfxObservation g_xmark_apply_sfx_observations[16]{};

struct XMarkBurnDeathBurst {
    bool active;
    Vec3 position;
    unsigned long long started_ms;
    unsigned long long expires_ms;
    uintptr_t seed_target;
    float half_w;
    float half_h;
};

XMarkBurnDeathBurst g_xmark_burn_death_bursts[8]{};

struct XMarkPendingVisualMark {
    bool active;
    XMarkVisualEnemyHost host;
    char reason[96];
    unsigned int attempts;
    unsigned long long next_ms;
    unsigned long long expires_ms;
};

XMarkPendingVisualMark g_pending_visual_marks[16]{};
unsigned int g_pending_visual_mark_spawn_attempts = 0;
unsigned int g_pending_visual_mark_repairs = 0;
unsigned int g_pending_visual_mark_retries = 0;
unsigned int g_pending_visual_mark_refresh_misses = 0;
unsigned int g_pending_visual_mark_expired = 0;
unsigned long long g_last_basic_health_drop_ms = 0;
uintptr_t g_last_basic_health_drop_target = 0;
float g_last_basic_health_drop_amount = 0.0f;
float g_last_basic_health_drop_before = 0.0f;
float g_last_basic_health_drop_after = 0.0f;

struct F0029RenderEffect {
    bool active;
    bool side_basic_bound;
    Vec3 position;
    int direction;
    unsigned long long started_ms;
    unsigned long long soft_expires_ms;
    unsigned long long source_tick;
    unsigned long long source_draw;
    unsigned int duration_ms;
};

F0029RenderEffect g_f0029_render_effects[16]{};

struct VerticalSmearRenderEffect {
    bool active;
    Vec3 position;
    int direction;
    unsigned long long started_ms;
    unsigned long long soft_expires_ms;
    unsigned long long source_tick;
    unsigned long long source_draw;
    unsigned int duration_ms;
};

VerticalSmearRenderEffect g_vertical_smear_render_effects[8]{};

unsigned int active_f0029_render_effect_count(unsigned long long now_ms) {
    unsigned int count = 0;
    for (const F0029RenderEffect &effect : g_f0029_render_effects) {
        const unsigned long long hard_expires_ms = effect.started_ms + effect.duration_ms;
        const unsigned long long expires_ms =
            effect.side_basic_bound && effect.soft_expires_ms
                ? std::min(hard_expires_ms, effect.soft_expires_ms)
                : hard_expires_ms;
        if (effect.active && effect.started_ms && now_ms < expires_ms) {
            ++count;
        }
    }
    return count;
}

unsigned int active_xmark_burn_effect_count(unsigned long long now_ms) {
    unsigned int count = 0;
    for (const XMarkBurnEffect &burn : g_xmark_burn_effects) {
        if (burn.active && burn.expires_ms && now_ms < burn.expires_ms) {
            ++count;
        }
    }
    return count;
}

struct XMarkHudState {
    unsigned int active_count;
    unsigned int mode;
    bool visible;
    bool blinking;
    bool damage_popup_visible;
    uintptr_t first_target;
    float first_health;
    float first_health_max;
    unsigned long long first_expires_ms;
    unsigned long long first_blink_start_ms;
    unsigned long long damage_popup_until_ms;
};

struct XMarkHudMark {
    bool active;
    bool death_linger;
    uintptr_t target;
    unsigned long long expires_ms;
    unsigned long long damage_popup_until_ms;
    unsigned long long death_blink_start_ms;
    unsigned int mode;
    float last_health;
    float last_health_max;
};

struct XMarkHudRuntimeLayout {
    bool valid;
    bool found_left;
    bool found_right;
    float left_x;
    float left_y;
    float left_scale_x;
    float left_scale_y;
    float right_x;
    float right_y;
    float right_scale_x;
    float right_scale_y;
    unsigned int nodes;
    unsigned int components;
    unsigned long long sampled_ms;
    char left_sequence[64];
    char right_sequence[64];
};

XMarkHudRuntimeLayout g_xmark_hud_runtime_layout{};

XMarkHudMark g_xmark_hud_marks[16]{};

constexpr unsigned int kXMarkHudModeNone = 0;
constexpr unsigned int kXMarkHudModeMark = 1;
constexpr unsigned int kXMarkHudModeBurn = 2;

float env_float(const char *name, float fallback);
Vec3 official_spawn_position();
bool gameplay_state_can_spawn_test_enemy(int game_state);
bool test_enemy_uses_api_spawn();
bool test_enemy_spawn_context_ready(int game_state, unsigned int room_index, float room_time);
uintptr_t game_singleton_global_address();
uintptr_t world_scale_address();
Vec3 xmark_attachment_mark_position(const Vec3 &target_position);
Vec3 xmark_attachment_target_position_from_mark(const Vec3 &mark_position);
unsigned int find_position_pair_offsets(
    uintptr_t base,
    const Vec3 &position,
    unsigned int *offsets_out,
    unsigned int max_offsets,
    float tolerance);
bool bind_xmark_attachment_position_slots(XMarkAttachment &attachment, const Vec3 &match_position);
bool xmark_render_backend_ensure_initialized();
void xmark_render_backend_update_vertices(unsigned long long now_ms);
void refresh_visual_follow_attachments_for_render(unsigned long long now_ms);
bool xmark_visual_host_currently_present(unsigned long long key);
bool refresh_active_xmark_attachment_for_visual_host(const XMarkVisualEnemyHost &host, unsigned long long now_ms);
bool xmark_burn_debug_draw_handles_effects();
void xmark_burn_debug_draw_effects(unsigned long long now_ms);
void end_xmark_burn_effect(XMarkBurnEffect &burn, unsigned long long now_ms);
bool official_enemy_host_for_attachment(const XMarkAttachment &attachment, XMarkOfficialEnemyHost *host_out);
bool official_enemy_host_by_combat_core(uintptr_t combat_core, XMarkOfficialEnemyHost *host_out);
bool combat_core_health_read(uintptr_t combat_core, float *health_out);
bool official_enemy_host_by_entity(uintptr_t entity, XMarkOfficialEnemyHost *host_out);
float official_enemy_render_half_from_health(const XMarkOfficialEnemyHost &host, float fallback);
bool official_entity_world_position_read(uintptr_t entity, Vec3 *position_out);
bool read_health_at_offset(uintptr_t entity, unsigned int offset, unsigned int health_kind, float *health_out, float *max_out);
bool xmark_official_damage_gate_enabled();
bool spawn_render_f0029_at(
    const char *reason,
    const Vec3 &position,
    int direction,
    bool side_basic_bound = false,
    unsigned long long source_tick = 0,
    unsigned long long source_draw = 0);
bool runtime_target_from_entity_pointer(
    uintptr_t entity,
    const Vec3 &expected_position,
    int direction,
    XMarkRuntimeTarget *target_out);
bool visual_enemy_host_by_key(unsigned long long key, XMarkVisualEnemyHost *host_out, unsigned long long now_ms);
bool find_nearest_visual_enemy_host(XMarkVisualEnemyHost *host_out, unsigned long long now_ms);
bool runtime_target_from_visual_host(const XMarkVisualEnemyHost &host, XMarkRuntimeTarget *target_out);
Vec3 xmark_burn_visual_host_render_position(
    XMarkBurnEffect &burn,
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms);
void maybe_auto_consume_xmark_for_burn(unsigned long long now_ms);
void update_xmark_charged_consume_health_probes(unsigned long long now_ms);
void update_xmark_burn_effects(unsigned long long now_ms);
void update_xmark_native_final_hit_death_watches(unsigned long long now_ms);
void trace_xmark_burn_death_target(XMarkBurnEffect &burn, unsigned long long now_ms, const char *phase);
bool find_runtime_target_near_visual_host(const XMarkVisualEnemyHost &host, XMarkRuntimeTarget *target_out, unsigned long long now_ms);
void runtime_target_direction_delta_for_direction(
    const XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    int direction,
    float *forward_out,
    float *lateral_out);
bool find_basic_attack_candidate_visual_host_for_health_drop(
    const XMarkRuntimeTarget &target,
    const Vec3 &player_api,
    int direction,
    XMarkVisualEnemyHost *host_out,
    unsigned long long now_ms);
bool spawn_tracked_xmark_for_visual_host(const char *reason, const XMarkVisualEnemyHost &host);
bool maybe_mark_recent_hurt_visual_host_for_basic_attack(
    unsigned long long now_ms,
    const Vec3 &player_api,
    unsigned int max_marks);
XMarkBasicHitCandidate *basic_hit_candidate_for_host(
    const XMarkVisualEnemyHost &host,
    unsigned long long now_ms);

struct XMarkHealthBaseline {
    uintptr_t entity;
    uintptr_t official_combat_core;
    unsigned int health_offset;
    unsigned int anchor_offset;
    unsigned int health_kind;
    Vec3 anchor;
    float health_value;
    float health_max;
    bool official;
};

struct XMarkBasicAttackProbe {
    bool active;
    bool contact_active;
    bool health_check_armed;
    int direction;
    unsigned long long started_ms;
    unsigned long long contact_started_ms;
    unsigned long long expires_ms;
    unsigned long long last_health_poll_ms;
    unsigned long long last_runtime_scan_ms;
    unsigned long long next_health_check_ms;
    unsigned int health_check_count;
    unsigned int baseline_count;
    unsigned int marked_count;
    unsigned int marked_enemy_count;
    XMarkHealthBaseline baselines[128];
    uintptr_t marked[32];
};

XMarkBasicAttackProbe g_basic_attack_probe{};
XMarkHealthBaseline g_runtime_health_history[160]{};
unsigned int g_runtime_health_history_count = 0;
unsigned long long g_last_runtime_health_history_ms = 0;

struct XMarkBasicFrameState {
    unsigned long long tick;
    unsigned long long draw;
    char frame[32];
    int direction;
    bool side_smear;
    bool has_geometry;
    bool has_contact;
    float contact_x;
    float contact_y;
    float min_x;
    float max_x;
    float min_y;
    float max_y;
    uintptr_t anim_component;
    uint32_t seq_frame_idx;
    uint32_t num_seq_frames;
    uint32_t loops_played;
    float current_frame_time;
    float play_rate;
    bool modapi_authoritative;
    bool anim_visible;
};

unsigned long long g_player_attack_anim_last_read_ms = 0;
unsigned long long g_player_attack_anim_last_scan_ms = 0;
bool g_player_attack_anim_last_read_valid = false;
XMarkBasicFrameState g_player_attack_anim_last_read_state{};

XMarkBasicFrameState g_last_muriel_charged_frame_state{};

struct XMarkRecentBasicFrameSample {
    bool active;
    unsigned long long seen_ms;
    XMarkBasicFrameState state;
};

XMarkRecentBasicFrameSample g_recent_basic_frame_samples[16]{};
unsigned int g_recent_basic_frame_sample_cursor = 0;
XMarkBasicFrameState g_last_basic_frame_state_for_hit{};
bool g_last_basic_frame_state_for_hit_active = false;
unsigned long long g_last_basic_frame_state_for_hit_seen_ms = 0;
XMarkBasicFrameState g_cached_basic_frame_file_state{};
bool g_cached_basic_frame_file_state_valid = false;
unsigned long long g_cached_basic_frame_file_read_ms = 0;
FILETIME g_cached_basic_frame_file_write_time{};
DWORD g_cached_basic_frame_file_size_high = 0;
DWORD g_cached_basic_frame_file_size_low = 0;
bool g_cached_basic_frame_file_identity_valid = false;

struct XMarkBasicSmearAnimationLock {
    bool active;
    unsigned long long attack_press_ms;
    unsigned long long source_tick;
    unsigned long long draw;
    unsigned long long started_ms;
    int direction;
    char frame[32];
};

XMarkBasicSmearAnimationLock g_basic_smear_animation_lock{};

bool recent_basic_frame_sample_overlaps_visual_host(
    unsigned long long now_ms,
    int direction,
    const XMarkVisualEnemyHost &host,
    unsigned int max_age_ms,
    XMarkBasicFrameState *sample_out = nullptr,
    unsigned long long *age_out = nullptr);
bool hurt_flash_matches_basic_frame(
    unsigned long long hurt_ms,
    const XMarkBasicFrameState &state,
    unsigned long long now_ms);

constexpr unsigned int kSupportedGameRevision = 148693;
constexpr uintptr_t kGameSingletonPtrRva = 0x10FB8B8;
constexpr uintptr_t kWorldScaleFloatRva = 0xFB4B8C;
constexpr uintptr_t kNativeEntityFactoryRva = 0x95D940;
constexpr uintptr_t kAnimEffectSetupRva = 0x4A15E0;

using NativeEntityFactoryFn = void *(__fastcall *)(
    void *entity_manager,
    uint32_t entity_type,
    const Vec3 *position,
    const Vec3 *scale,
    float rotation,
    int spawn_flag,
    void *arg0,
    void *arg1,
    void *arg2);

using AnimEffectSetupFn = void(__fastcall *)(
    void *anim_effect,
    const YcStringView *anb_path,
    const YcStringView *sequence_name,
    int draw_layer,
    float visual_scale,
    const YcStringView *palette_path);

bool find_position_pair_offset(uintptr_t base, const Vec3 &position, unsigned int *offset_out);
bool write_position_pair(uintptr_t base, unsigned int offset, const Vec3 &position);
float xmark_visual_host_units_per_pixel();

constexpr unsigned int kXMarkEnvironmentCacheSlots = 4096;

struct XMarkEnvironmentCacheSlot {
    const char *name;
    const char *value;
    unsigned int uint_value;
    int int_value;
    float float_value;
    bool uint_valid;
    bool int_valid;
    bool float_valid;
    bool bool_value;
};

INIT_ONCE g_xmark_environment_cache_once = INIT_ONCE_STATIC_INIT;
XMarkEnvironmentCacheSlot g_xmark_environment_cache[kXMarkEnvironmentCacheSlots]{};
char *g_xmark_environment_cache_block = nullptr;
bool g_xmark_environment_cache_active = false;

uint32_t xmark_environment_name_hash(const char *name) {
    uint32_t hash = 2166136261u;
    if (!name) {
        return hash;
    }
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(name); *cursor; ++cursor) {
        unsigned char value = *cursor;
        if (value >= 'a' && value <= 'z') {
            value = static_cast<unsigned char>(value - ('a' - 'A'));
        }
        hash ^= value;
        hash *= 16777619u;
    }
    return hash;
}

void xmark_environment_cache_insert(const char *name, const char *value) {
    if (!name || !name[0] || !value) {
        return;
    }
    const uint32_t hash = xmark_environment_name_hash(name);
    for (unsigned int probe = 0; probe < kXMarkEnvironmentCacheSlots; ++probe) {
        XMarkEnvironmentCacheSlot &slot =
            g_xmark_environment_cache[(hash + probe) % kXMarkEnvironmentCacheSlots];
        if (!slot.name || _stricmp(slot.name, name) == 0) {
            slot.name = name;
            slot.value = value;

            char *end = nullptr;
            const unsigned long parsed_uint = std::strtoul(value, &end, 0);
            slot.uint_valid = end && end != value;
            slot.uint_value = static_cast<unsigned int>(parsed_uint);

            end = nullptr;
            const long parsed_int = std::strtol(value, &end, 0);
            slot.int_valid = end && end != value;
            slot.int_value = static_cast<int>(parsed_int);

            end = nullptr;
            const double parsed_float = std::strtod(value, &end);
            slot.float_valid = end && end != value;
            slot.float_value = static_cast<float>(parsed_float);
            slot.bool_value =
                value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                value[0] == 'y' || value[0] == 'Y';
            return;
        }
    }
}

BOOL CALLBACK xmark_initialize_environment_cache(PINIT_ONCE, PVOID, PVOID *) {
    char enabled[16]{};
    const DWORD enabled_len = GetEnvironmentVariableA(
        "MINA_XMARK_ENV_CACHE_ENABLED",
        enabled,
        static_cast<DWORD>(sizeof(enabled)));
    if (enabled_len > 0 &&
        (enabled_len >= sizeof(enabled) ||
         !(enabled[0] == '1' || enabled[0] == 't' || enabled[0] == 'T' ||
           enabled[0] == 'y' || enabled[0] == 'Y'))) {
        return TRUE;
    }

#define CLASHREND_INSERT_DEFAULT(name, value) xmark_environment_cache_insert(name, value);
    CLASHREND_REGULAR_PROFILE_ENTRIES(CLASHREND_INSERT_DEFAULT)
#undef CLASHREND_INSERT_DEFAULT
    g_xmark_environment_cache_active = true;

    LPCH environment = GetEnvironmentStringsA();
    if (!environment) {
        return TRUE;
    }
    size_t block_size = 1;
    for (const char *cursor = environment; *cursor; cursor += std::strlen(cursor) + 1) {
        block_size += std::strlen(cursor) + 1;
    }
    char *block = static_cast<char *>(HeapAlloc(GetProcessHeap(), 0, block_size));
    if (!block) {
        FreeEnvironmentStringsA(environment);
        return TRUE;
    }
    std::memcpy(block, environment, block_size);
    FreeEnvironmentStringsA(environment);

    for (char *cursor = block; *cursor;) {
        const size_t entry_length = std::strlen(cursor);
        char *next = cursor + entry_length + 1;
        if (cursor[0] != '=') {
            char *separator = std::strchr(cursor, '=');
            if (separator) {
                *separator = 0;
                xmark_environment_cache_insert(cursor, separator + 1);
            }
        }
        cursor = next;
    }
    g_xmark_environment_cache_block = block;
    return TRUE;
}

void xmark_environment_cache_initialize() {
    InitOnceExecuteOnce(
        &g_xmark_environment_cache_once,
        xmark_initialize_environment_cache,
        nullptr,
        nullptr);
}

const XMarkEnvironmentCacheSlot *xmark_environment_cache_find(const char *name) {
    if (!g_xmark_environment_cache_active || !name || !name[0]) {
        return nullptr;
    }
    const uint32_t hash = xmark_environment_name_hash(name);
    for (unsigned int probe = 0; probe < kXMarkEnvironmentCacheSlots; ++probe) {
        const XMarkEnvironmentCacheSlot &slot =
            g_xmark_environment_cache[(hash + probe) % kXMarkEnvironmentCacheSlots];
        if (!slot.name) {
            return nullptr;
        }
        if (_stricmp(slot.name, name) == 0) {
            return &slot;
        }
    }
    return nullptr;
}

bool xmark_read_environment_value(const char *name, char *out, size_t out_size) {
    if (!name || !out || out_size == 0) {
        return false;
    }
    out[0] = 0;
    xmark_environment_cache_initialize();
    if (g_xmark_environment_cache_active) {
        const XMarkEnvironmentCacheSlot *cached = xmark_environment_cache_find(name);
        if (!cached) {
            return false;
        }
        if (std::strlen(cached->value) >= out_size) {
            return false;
        }
        std::snprintf(out, out_size, "%s", cached->value);
        return true;
    }
    const DWORD len = GetEnvironmentVariableA(name, out, static_cast<DWORD>(out_size));
    return len > 0 && len < out_size;
}

void copy_env_or_default(const char *name, const char *fallback_name, char *out, size_t out_size) {
    if (!out || out_size == 0) {
        return;
    }
    out[0] = 0;
    if (xmark_read_environment_value(name, out, out_size)) {
        return;
    }

    char appdata[MAX_PATH * 2]{};
    if (xmark_read_environment_value("APPDATA", appdata, sizeof(appdata))) {
        std::snprintf(
            out,
            out_size,
            "%s\\Yacht Club Games\\Mina the Hollower\\%s",
            appdata,
            fallback_name);
    }
}

unsigned int env_uint(const char *name, unsigned int fallback) {
    xmark_environment_cache_initialize();
    if (g_xmark_environment_cache_active) {
        const XMarkEnvironmentCacheSlot *cached = xmark_environment_cache_find(name);
        return cached && cached->uint_valid ? cached->uint_value : fallback;
    }
    char value[64]{};
    if (!xmark_read_environment_value(name, value, sizeof(value))) {
        return fallback;
    }
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 0);
    if (!end || end == value) {
        return fallback;
    }
    return static_cast<unsigned int>(parsed);
}

int env_int(const char *name, int fallback) {
    xmark_environment_cache_initialize();
    if (g_xmark_environment_cache_active) {
        const XMarkEnvironmentCacheSlot *cached = xmark_environment_cache_find(name);
        return cached && cached->int_valid ? cached->int_value : fallback;
    }
    char value[64]{};
    if (!xmark_read_environment_value(name, value, sizeof(value))) {
        return fallback;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 0);
    if (!end || end == value) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool env_bool(const char *name, bool fallback) {
    xmark_environment_cache_initialize();
    if (g_xmark_environment_cache_active) {
        const XMarkEnvironmentCacheSlot *cached = xmark_environment_cache_find(name);
        return cached ? cached->bool_value : fallback;
    }
    char value[32]{};
    if (!xmark_read_environment_value(name, value, sizeof(value))) {
        return fallback;
    }
    return value[0] == '1' || value[0] == 't' || value[0] == 'T' || value[0] == 'y' || value[0] == 'Y';
}

enum class ModPerfStage : size_t {
    Fixed,
    Snapshot,
    Attachments,
    PostArt,
    BasicHealth,
    HealthHistory,
    CommandPoll,
    Burn,
    Hud,
    Combat,
    StateWrite,
    Count,
};

struct ModPerfCounter {
    std::atomic<unsigned long long> calls{0};
    std::atomic<unsigned long long> ticks{0};
    std::atomic<unsigned long long> max_ticks{0};
};

ModPerfCounter g_mod_perf[static_cast<size_t>(ModPerfStage::Count)]{};
std::atomic<unsigned long long> g_mod_perf_last_report_ms{0};

bool mod_perf_enabled() {
    static const bool enabled = env_bool("MINA_XMARK_ATTACK_PERF_PROBE", false);
    return enabled;
}

unsigned long long mod_perf_ticks() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<unsigned long long>(value.QuadPart);
}

void mod_perf_record(ModPerfStage stage, unsigned long long started) {
    if (!started) {
        return;
    }
    const unsigned long long elapsed = mod_perf_ticks() - started;
    ModPerfCounter &counter = g_mod_perf[static_cast<size_t>(stage)];
    counter.calls.fetch_add(1, std::memory_order_relaxed);
    counter.ticks.fetch_add(elapsed, std::memory_order_relaxed);
    unsigned long long observed = counter.max_ticks.load(std::memory_order_relaxed);
    while (elapsed > observed &&
           !counter.max_ticks.compare_exchange_weak(observed, elapsed, std::memory_order_relaxed)) {
    }
}

class ModPerfScope {
public:
    ModPerfScope(ModPerfStage stage, bool enabled)
        : stage_(stage), started_(enabled ? mod_perf_ticks() : 0) {
    }

    ~ModPerfScope() {
        mod_perf_record(stage_, started_);
    }

private:
    ModPerfStage stage_;
    unsigned long long started_;
};

void mod_perf_maybe_report(unsigned long long now_ms) {
    if (!mod_perf_enabled() || !g_mina) {
        return;
    }
    unsigned long long previous_ms = g_mod_perf_last_report_ms.load(std::memory_order_relaxed);
    const unsigned int report_ms = std::max(
        250u,
        env_uint("MINA_XMARK_ATTACK_PERF_REPORT_MS", 1000));
    if (previous_ms && now_ms < previous_ms + report_ms) {
        return;
    }
    if (!g_mod_perf_last_report_ms.compare_exchange_strong(previous_ms, now_ms, std::memory_order_relaxed)) {
        return;
    }
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double ticks_to_us = frequency.QuadPart > 0 ? 1000000.0 / static_cast<double>(frequency.QuadPart) : 0.0;
    static constexpr const char *names[] = {
        "fixed", "snapshot", "attach", "postArt", "basicHealth", "healthHistory",
        "command", "burn", "hud", "combat", "stateWrite"
    };
    char line[1024]{};
    int used = std::snprintf(
        line,
        sizeof(line),
        "ModPerf intervalMs=%llu",
        previous_ms ? now_ms - previous_ms : 0);
    for (size_t i = 0; i < static_cast<size_t>(ModPerfStage::Count) && used > 0 && used < static_cast<int>(sizeof(line)); ++i) {
        ModPerfCounter &counter = g_mod_perf[i];
        const unsigned long long calls = counter.calls.exchange(0, std::memory_order_relaxed);
        const unsigned long long ticks = counter.ticks.exchange(0, std::memory_order_relaxed);
        const unsigned long long max_ticks = counter.max_ticks.exchange(0, std::memory_order_relaxed);
        used += std::snprintf(
            line + used,
            sizeof(line) - static_cast<size_t>(used),
            " %s=%llu/%llu/%llu",
            names[i],
            calls,
            static_cast<unsigned long long>(static_cast<double>(ticks) * ticks_to_us),
            static_cast<unsigned long long>(static_cast<double>(max_ticks) * ticks_to_us));
    }
    if (used > 0 && used + 2 < static_cast<int>(sizeof(line))) {
        line[used++] = '\n';
        line[used] = '\0';
    }
    g_mina->Log(line);
}

