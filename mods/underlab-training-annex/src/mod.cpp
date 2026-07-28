#include "MinaModAPI.h"
#include "MinaModEnums.h"

#include <stddef.h>
#include <stdio.h>
#include <chrono>
#include <math.h>

namespace
{
MinaModAPI *Mina = nullptr;

// UnderLab runs in the checkpoint-room state.
constexpr int32_t kHubState = GAMESTATE_CHECKPOINTROOM;
constexpr int32_t kTrainingState = GAMESTATE_GYM_WORLDLOADTEST1;
constexpr uint32_t kUnderLabRoom = 14;
constexpr float kAnnexEntryX = 297.604187f;
constexpr float kAnnexEntryY = -254.157748f;

float kEntranceMinX = 0.0f;
float kEntranceMaxX = 0.0f;
float kEntranceMinY = 0.0f;
float kEntranceMaxY = 0.0f;
MM_Vec3 kUnderLabReturnPos{};
MM_Vec3 kTrainingStartPos{};
MM_Vec3 kTunnelPos{};
MM_Vec3 kRugPos{};
float kRawScaleX = 0.1f;
float kRawScaleY = 0.1f;
struct DummyTier
{
    MM_Vec3 position;
    float health;
    float barWidth;
    const char *name;
};
DummyTier kDummyTiers[] = {
    {{}, 50.0f, 0.0f, "50 HP"},
    {{}, 100.0f, 0.0f, "100 HP"},
    {{}, 100.0f, 0.0f, "Flying 100 HP"},
};
constexpr size_t kDummyCount = sizeof(kDummyTiers) / sizeof(kDummyTiers[0]);
constexpr bool kCustomDummiesEnabled = true;
constexpr uint32_t kTrainingDummyUpgradeFlag =
    uint32_t(1) << (kItemType_Upgrade_TrainingDummy - kItemType_Upgrade_CandleVision);
constexpr float kRetailTrainingLockX = 284.35f;
constexpr float kRetailTrainingMinY = -237.2f;
constexpr float kRetailTrainingMaxY = -223.6f;

struct DummyRuntime
{
    SpawnPoint *spawnPoint;
    GameComponent *configuredComponent;
    bool resetRequested;
};

bool ChamberActive = false;
int32_t ReturnGameState = kHubState;
bool SaveWriteWasEnabled = true;
bool SaveWriteCaptured = false;
bool TemporaryTrainingUpgradeActive = false;
uint32_t SavedUpgradeFlags = 0;
bool F8WasDown = false;
World *InitializedTrainingWorld = nullptr;
World *InitializedReturnWorld = nullptr;
MM_Rtti CombatCoreRtti{};
MM_Rtti EntityRtti{};
MM_Rtti GameAnimRtti{};
DummyRuntime DummyRuntimes[kDummyCount]{};

bool rtti_valid(const MM_Rtti rtti)
{
    return rtti.typeId != 0;
}

bool rtti_equal(const MM_Rtti lhs, const MM_Rtti rhs)
{
    return lhs.typeId == rhs.typeId;
}

MM_Rtti rtti_from_hash(const uint64_t hash)
{
    return MM_Rtti{hash};
}
int32_t LastObservedState = -9999;
uint32_t LastObservedRoom = 0xffffffffu;
bool WasInAnnex = false;
float PreviousPlayerX = 272.0f;
float LastTeleportRoomTime = -10.0f;
bool StabilizeAnnexEntry = false;
bool StabilizeCheckpointReturn = false;
bool AnnexRuntimeProbeLogged = false;
bool AnnexDummySpawnWindowComplete = false;
bool RetailTrainingBoundaryLogged = false;
bool PlayerArtHiddenForTeleport = false;
ycEntity *HiddenPlayerRoot = nullptr;
MM_Vec3 HiddenPlayerRootScale{1.0f, 1.0f, 1.0f};
struct HiddenPlayerArt
{
    ycEntity *entity;
    MM_Transform transform;
};
HiddenPlayerArt HiddenPlayerArtEntities[64]{};
size_t HiddenPlayerArtCount = 0;
enum TeleportPhase
{
    kTeleportIdle,
    kTeleportDeparting,
    kTeleportArriving,
};
TeleportPhase CurrentTeleportPhase = kTeleportIdle;
bool TeleportToAnnex = false;
bool TeleportNeedsPadExit = false;
bool TeleportArrivalVfxStarted = false;
MM_Vec3 TeleportPinnedPosition{};
uint64_t TeleportPhaseStartedMs = 0;
ycTileLevel2Entity *TunnelArtEntity = nullptr;
ycTileLevel2Entity *TeleportSparkleEntity = nullptr;
ycTileLevel2Entity *TeleportRingEntity = nullptr;
World *TunnelArtWorld = nullptr;
SpawnPoint *TunnelArtSpawnPoints[2]{};
enum ActiveTeleportVfxKind
{
    kTeleportVfxRing,
    kTeleportVfxSparkle,
};
struct ActiveTeleportVfx
{
    ycEntity *root;
    ActiveTeleportVfxKind kind;
    float angle;
};
ActiveTeleportVfx ActiveTeleportVfxRoots[16]{};
size_t ActiveTeleportVfxRootCount = 0;
MM_Vec3 ActiveTeleportVfxCenter{};
uint64_t ActiveTeleportVfxStartedMs = 0;
uint64_t ActiveTeleportVfxDurationMs = 260;
constexpr uint64_t kTeleportArrivalRevealDelayMs = 650;
int ActiveTeleportSparkleWave = -1;
struct BlinkPlayerArt
{
    ycEntity *entity;
    MM_Transform transform;
};
BlinkPlayerArt BlinkPlayerArtEntities[32]{};
size_t BlinkPlayerArtCount = 0;
bool PostTeleportBlinkActive = false;
bool PostTeleportBlinkHidden = false;
uint64_t PostTeleportBlinkStartedMs = 0;

constexpr uint64_t kDummyNameLevelHashes[kDummyCount] = {
    15191824161433712700ULL,
    15191824161433712701ULL,
    15191824161433712702ULL,
};

struct BossStatueDef
{
    uint8_t flagBit;
    uint64_t nameLevelHash;
};

BossStatueDef kBossStatues[] = {
    {27, 15191824161433712400ULL},
    {0, 15191824161433712401ULL},
    {1, 15191824161433712402ULL},
    {2, 15191824161433712403ULL},
    {3, 15191824161433712404ULL},
    {4, 15191824161433712405ULL},
    {5, 15191824161433712406ULL},
    {6, 15191824161433712407ULL},
    {7, 15191824161433712408ULL},
    {8, 15191824161433712409ULL},
    {9, 15191824161433712410ULL},
    {10, 15191824161433712411ULL},
};
constexpr size_t kBossStatueCount = sizeof(kBossStatues) / sizeof(kBossStatues[0]);
SpawnPoint *BossStatueSpawnPoints[kBossStatueCount]{};
World *BossStatueWorld = nullptr;
uint64_t AppliedBossStatueFlags = 0;

const char *kTunnelArtEntityDesc =
    "ycTileLevel2Entity { "
    "m_props: [ "
    "ycTileLevel2Property { m_nameHash: 1410415984, "
    "m_value: \"levels/tilesets/hub/animTiles/doorway.anb.yc\", }, "
    "ycTileLevel2Property { m_nameHash: 2401088400, m_value: \"true\", }, "
    "ycTileLevel2Property { m_nameHash: 3026379260, m_value: \"hub\", }, "
    "ycTileLevel2Property { m_nameHash: 4271143480, m_value: \"10\", } ], "
    "m_scale: ycVec2 { x: -1, y: 1 }, "
    "m_layerNameHash: 3792921557, "
    "m_entityType: 48, "
    "m_tileLayerIndex: 70, "
    "m_spawnType: kTileLevel2EntitySpawnType_Disable "
    "};";

const char *kTeleportSparkleEntityDesc =
    "ycTileLevel2Entity { "
    "m_props: [ "
    "ycTileLevel2Property { m_nameHash: 1410415984, "
    "m_value: \"levels/tilesets/underlabTrainingAnnex/animTiles/teleportSparkle.anb.yc\", }, "
    "ycTileLevel2Property { m_nameHash: 2401088400, m_value: \"true\", }, "
    "ycTileLevel2Property { m_nameHash: 1162648605, "
    "m_value: \"palettes/global.pal.yc\", }, "
    "ycTileLevel2Property { m_nameHash: 3026379260, m_value: \"idle\", }, "
    "ycTileLevel2Property { m_nameHash: 4271143480, m_value: \"12\", } ], "
    "m_scale: ycVec2 { x: 1, y: 1 }, "
    "m_layerNameHash: 3792921557, "
    "m_entityType: 48, "
    "m_tileLayerIndex: 77, "
    "m_spawnType: kTileLevel2EntitySpawnType_Disable "
    "};";

const char *kTeleportRingEntityDesc =
    "ycTileLevel2Entity { "
    "m_props: [ "
    "ycTileLevel2Property { m_nameHash: 1410415984, "
    "m_value: \"levels/tilesets/underlabTrainingAnnex/animTiles/teleportRing.anb.yc\", }, "
    "ycTileLevel2Property { m_nameHash: 2401088400, m_value: \"true\", }, "
    "ycTileLevel2Property { m_nameHash: 1162648605, "
    "m_value: \"palettes/global.pal.yc\", }, "
    "ycTileLevel2Property { m_nameHash: 3026379260, m_value: \"idle\", }, "
    "ycTileLevel2Property { m_nameHash: 4271143480, m_value: \"11\", } ], "
    "m_scale: ycVec2 { x: 1, y: 1 }, "
    "m_layerNameHash: 3792921557, "
    "m_entityType: 48, "
    "m_tileLayerIndex: 76, "
    "m_spawnType: kTileLevel2EntitySpawnType_Disable "
    "};";

bool key_pressed(uint32_t key, bool &wasDown)
{
    const bool down = Mina->IsKeyDown(key) || Mina->IsKeyHeld(key);
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

bool jump_held()
{
    return Mina->IsActionHeld(kGameAct_Jump) || Mina->IsActionDown(kGameAct_Jump) ||
           Mina->IsKeyHeld(YC_KEY_SPACE) || Mina->IsKeyDown(YC_KEY_SPACE);
}

bool right_held()
{
    return Mina->IsActionHeld(kGameAct_DpadRight) ||
           Mina->IsActionDown(kGameAct_DpadRight) ||
           Mina->IsKeyHeld(YC_KEY_RIGHT) ||
           Mina->IsKeyDown(YC_KEY_RIGHT) ||
           Mina->IsKeyHeld(YC_KEY_D) ||
           Mina->IsKeyDown(YC_KEY_D);
}

bool at_underlab_entrance()
{
    float x = 0.0f;
    float y = 0.0f;
    Mina->PlayerGetPos(&x, &y);
    return x >= kEntranceMinX && x <= kEntranceMaxX &&
           y >= kEntranceMinY && y <= kEntranceMaxY;
}

bool at_training_exit()
{
    if (Mina->GetCurrentGameState() != kTrainingState)
        return false;
    float x = 0.0f;
    float y = 0.0f;
    Mina->PlayerGetPos(&x, &y);
    return x >= kEntranceMinX && x <= kEntranceMaxX &&
           y >= kEntranceMinY && y <= kEntranceMaxY;
}

MM_Vec3 raw_position(float x, float y)
{
    const MM_Vec2 transformed = Mina->SpawnPointSpawnPointTransformPos(MM_Vec2{x, y});
    return MM_Vec3{transformed.x, transformed.y, 0.0f};
}

void initialize_layout_from_level_coordinates()
{
    const MM_Vec3 origin = raw_position(0.0f, 0.0f);
    const MM_Vec3 unitX = raw_position(8.0f, 0.0f);
    const MM_Vec3 unitY = raw_position(0.0f, 8.0f);
    kRawScaleX = unitX.x >= origin.x ? (unitX.x - origin.x) / 8.0f : (origin.x - unitX.x) / 8.0f;
    kRawScaleY = unitY.y >= origin.y ? (unitY.y - origin.y) / 8.0f : (origin.y - unitY.y) / 8.0f;

    kEntranceMinX = 284.8f;
    kEntranceMaxX = 310.4f;
    kEntranceMinY = -256.0f;
    kEntranceMaxY = -243.2f;

    kUnderLabReturnPos = MM_Vec3{271.991272f, -232.044022f, 0.0f};
    kTrainingStartPos = MM_Vec3{290.0f, -251.2f, 0.0f};
    kTunnelPos = MM_Vec3{278.0f, -229.6f, 0.0f};
    kRugPos = MM_Vec3{297.6f, -249.2f, 0.0f};
    kDummyTiers[0].position = MM_Vec3{292.8f, -248.6f, 0.0f};
    kDummyTiers[1].position = MM_Vec3{297.6f, -248.6f, 0.0f};
    kDummyTiers[2].position = MM_Vec3{302.4f, -248.6f, 0.0f};
    kDummyTiers[0].barWidth = 3.0f;
    kDummyTiers[1].barWidth = 4.5f;
    kDummyTiers[2].barWidth = 4.5f;

    Mina->Log(
        "UnderLab Training Annex: native Hub room bounds x=[%.3f,%.3f] y=[%.3f,%.3f].\n",
        kEntranceMinX,
        kEntranceMaxX,
        kEntranceMinY,
        kEntranceMaxY);
}

void suspend_save_writes()
{
    if (!SaveWriteCaptured)
    {
        SaveWriteWasEnabled = Mina->IsSaveWriteEnabled();
        SaveWriteCaptured = true;
    }
    Mina->SetSaveWriteEnabled(false);
}

void restore_save_writes()
{
    if (SaveWriteCaptured)
        Mina->SetSaveWriteEnabled(SaveWriteWasEnabled);
    SaveWriteCaptured = false;
}

void enable_temporary_training_upgrade()
{
    if (TemporaryTrainingUpgradeActive || !Mina->PlayerGetUpgrades ||
        !Mina->PlayerSetUpgrades)
        return;
    const uint32_t current = Mina->PlayerGetUpgrades();
    if ((current & kTrainingDummyUpgradeFlag) != 0)
        return;
    SavedUpgradeFlags = current;
    suspend_save_writes();
    Mina->PlayerSetUpgrades(current | kTrainingDummyUpgradeFlag);
    TemporaryTrainingUpgradeActive = true;
    Mina->Log(
        "UnderLab Training Annex: temporarily enabled retail Training Dummy behavior; save writes suspended.\n");
}

void disable_temporary_training_upgrade()
{
    if (!TemporaryTrainingUpgradeActive)
        return;
    if (Mina->PlayerSetUpgrades)
        Mina->PlayerSetUpgrades(SavedUpgradeFlags);
    TemporaryTrainingUpgradeActive = false;
    restore_save_writes();
    Mina->Log(
        "UnderLab Training Annex: restored original upgrade flags and save-write state.\n");
}

void enforce_locked_retail_training_room_boundary(int32_t state)
{
    if (state != kHubState || !Mina->PlayerGetUpgrades)
    {
        RetailTrainingBoundaryLogged = false;
        return;
    }
    const uint32_t upgrades = Mina->PlayerGetUpgrades();
    if ((upgrades & kTrainingDummyUpgradeFlag) != 0)
    {
        RetailTrainingBoundaryLogged = false;
        return;
    }
    const uint32_t room = Mina->GetRoomIndex();
    if (room > 1)
        return;

    float x = 0.0f;
    float y = 0.0f;
    Mina->PlayerGetPos(&x, &y);
    if (y < kRetailTrainingMinY || y > kRetailTrainingMaxY || x <= kRetailTrainingLockX)
    {
        RetailTrainingBoundaryLogged = false;
        return;
    }

    Mina->PlayerSetPos(kRetailTrainingLockX, y);
    if (!RetailTrainingBoundaryLogged)
    {
        Mina->Log(
            "UnderLab Training Annex: blocked locked retail training-room doorway; "
            "the real upgrade is absent.\n");
        RetailTrainingBoundaryLogged = true;
    }
}

void enter_training_annex()
{
    if (ChamberActive)
        return;
    ChamberActive = true;
    ReturnGameState = Mina->GetCurrentGameState();
    InitializedTrainingWorld = nullptr;
    InitializedReturnWorld = nullptr;
    suspend_save_writes();
    Mina->Log("UnderLab Training Annex: entering through the right-side dive tunnel.\n");
    Mina->TransitionToGameState(kTrainingState);
}

void return_to_underlab()
{
    if (!ChamberActive)
        return;
    InitializedReturnWorld = nullptr;
    Mina->Log("UnderLab Training Annex: returning to UnderLab.\n");
    Mina->TransitionToGameState(ReturnGameState);
}

ycComponent *find_combat_core(GameComponent *component)
{
    if (!component || !rtti_valid(CombatCoreRtti))
        return nullptr;
    ycEntity *entity = Mina->ComponentGetParent(reinterpret_cast<ycComponent *>(component));
    if (!entity)
        return nullptr;
    const size_t childCount = Mina->EntityGetChildren(entity, nullptr, 0);
    if (!childCount)
        return nullptr;
    ycComponent **children = static_cast<ycComponent **>(Mina->Alloc(sizeof(ycComponent *) * childCount));
    if (!children)
        return nullptr;
    const size_t readCount = Mina->EntityGetChildren(entity, children, childCount);
    ycComponent *combatCore = nullptr;
    for (size_t i = 0; i < readCount && i < childCount; ++i)
    {
        ycComponent *candidate = children[i];
        if (candidate &&
            (rtti_equal(Mina->ComponentGetType(candidate), CombatCoreRtti) ||
             Mina->ComponentIsa(candidate, CombatCoreRtti)))
        {
            combatCore = candidate;
            break;
        }
    }
    Mina->Free(children);
    return combatCore;
}

bool configure_dummy_health(GameComponent *dummy, const DummyTier &tier)
{
    ycComponent *combatCore = find_combat_core(dummy);
    if (!combatCore)
        return false;
    Mina->CombatCoreSetHealthMax(combatCore, tier.health);
    Mina->CombatCoreSetHealth(combatCore, tier.health);
    Mina->Log("UnderLab Training Annex: %s dummy health set to %.0f.\n", tier.name, tier.health);
    return true;
}

void spawn_dummy(World *world, WorldRegion *region, size_t index)
{
    const DummyTier &tier = kDummyTiers[index];
    SpawnManager *spawnManager = Mina->WorldRegionGetSpawnManager(region);
    SpawnPoint *spawnPoint = Mina->SpawnManagerCreateSpawnPoint(
        spawnManager,
        region,
        tier.position,
        MM_Vec3{1.0f, 1.0f, 1.0f},
        0.0f,
        ENTITYTYPE_TEST_TRAINING_DUMMY,
        kTileLevel2EntitySpawnType_RoomInfinite,
        255,
        1);
    GameComponent *dummy = Mina->SpawnEntity2(
        Mina->WorldGetGameRootEntity(world),
        ENTITYTYPE_TEST_TRAINING_DUMMY,
        tier.position,
        MM_Vec3{1.0f, 1.0f, 1.0f},
        0.0f,
        1,
        spawnPoint,
        nullptr,
        nullptr);
    if (!dummy)
    {
        Mina->Log("UnderLab Training Annex: a dummy failed to spawn.\n");
        return;
    }

    DummyRuntimes[index].spawnPoint = spawnPoint;
    if (!configure_dummy_health(dummy, tier))
    {
        Mina->Log("UnderLab Training Annex: %s dummy spawned, but its health core was not found.\n", tier.name);
        return;
    }
    DummyRuntimes[index].configuredComponent = dummy;
}

void spawn_runtime_tunnel_art(World *world, WorldRegion *region)
{
    if (!TunnelArtEntity || !world || !region || TunnelArtWorld == world)
        return;
    SpawnManager *spawnManager = Mina->WorldRegionGetSpawnManager(region);
    if (!spawnManager)
        return;
    const MM_Vec3 positions[] = {
        {278.0f, -229.6f, 0.0f},
    };
    for (size_t i = 0; i < 1; ++i)
    {
        SpawnPoint *spawnPoint =
            Mina->SpawnManagerCreateSpawnPoint2(spawnManager, region, TunnelArtEntity);
        if (!spawnPoint)
            continue;
        Mina->SpawnPointSetPos(spawnPoint, positions[i]);
        Mina->SpawnPointSetScale(spawnPoint, MM_Vec3{-1.0f, 1.0f, 1.0f});
        GameComponent *art = Mina->SpawnEntity2(
            Mina->WorldGetGameRootEntity(world),
            ENTITYTYPE_ANIM_TILE,
            positions[i],
            MM_Vec3{-1.0f, 1.0f, 1.0f},
            0.0f,
            1,
            spawnPoint,
            nullptr,
            nullptr);
        if (art)
            TunnelArtSpawnPoints[i] = spawnPoint;
    }
    TunnelArtWorld = world;
    Mina->Log(
        "UnderLab Training Annex: runtime tunnel-art fallback spawned at (278.0,-229.6).\n");
}

void maintain_dummy_health_tiers()
{
    for (size_t i = 0; i < kDummyCount; ++i)
    {
        DummyRuntime &runtime = DummyRuntimes[i];
        if (!runtime.spawnPoint || !Mina->SpawnPointIsEntitySpawned(runtime.spawnPoint))
            continue;
        GameComponent *current = Mina->SpawnPointGetSpawnedEntity(runtime.spawnPoint);
        if (!current || current == runtime.configuredComponent)
            continue;
        if (configure_dummy_health(current, kDummyTiers[i]))
            runtime.configuredComponent = current;
    }
}

void bind_and_configure_level_dummies(World *world, WorldRegion *region)
{
    if (!world || !region)
        return;
    SpawnManager *spawnManager = Mina->WorldRegionGetSpawnManager(region);
    if (!spawnManager)
        return;
    for (size_t i = 0; i < kDummyCount; ++i)
    {
        SpawnPoint *spawnPoint = Mina->SpawnManagerGetSpawnPointByNameLevelHash(
            spawnManager, kDummyNameLevelHashes[i]);
        if (!spawnPoint)
            continue;
        DummyRuntimes[i].spawnPoint = spawnPoint;
        Mina->SpawnPointSetPos(spawnPoint, kDummyTiers[i].position);
        if (!Mina->SpawnPointIsEntitySpawned(spawnPoint))
        {
            if (!DummyRuntimes[i].resetRequested)
            {
                Mina->SpawnPointReset(spawnPoint);
                DummyRuntimes[i].resetRequested = true;
                Mina->Log(
                    "UnderLab Training Annex: armed one retail %s dummy spawn at (%.1f, %.1f).\n",
                    kDummyTiers[i].name,
                    kDummyTiers[i].position.x,
                    kDummyTiers[i].position.y);
            }
            continue;
        }
        DummyRuntimes[i].resetRequested = false;
        GameComponent *dummy = Mina->SpawnPointGetSpawnedEntity(spawnPoint);
        if (!dummy || dummy == DummyRuntimes[i].configuredComponent)
            continue;
        if (configure_dummy_health(dummy, kDummyTiers[i]))
            DummyRuntimes[i].configuredComponent = dummy;
    }
}

void activate_defeated_boss_statues(World *world, WorldRegion *region)
{
    if (!world || !region)
        return;
    if (BossStatueWorld != world)
    {
        BossStatueWorld = world;
        AppliedBossStatueFlags = 0;
        for (size_t i = 0; i < kBossStatueCount; ++i)
            BossStatueSpawnPoints[i] = nullptr;
    }
    SpawnManager *spawnManager = Mina->WorldRegionGetSpawnManager(region);
    if (!spawnManager)
        return;
    const uint64_t defeated = Mina->PlayerGetBossesDefeated();
    size_t defeatedCount = 0;
    for (size_t i = 0; i < kBossStatueCount; ++i)
    {
        const uint64_t flag = uint64_t(1) << kBossStatues[i].flagBit;
        if ((defeated & flag) != 0)
            ++defeatedCount;
    }
    size_t defeatedOrdinal = 0;
    for (size_t i = 0; i < kBossStatueCount; ++i)
    {
        const uint64_t flag = uint64_t(1) << kBossStatues[i].flagBit;
        if ((defeated & flag) == 0)
            continue;
        SpawnPoint *spawnPoint = Mina->SpawnManagerGetSpawnPointByNameLevelHash(
            spawnManager, kBossStatues[i].nameLevelHash);
        if (!spawnPoint)
        {
            ++defeatedOrdinal;
            continue;
        }
        const float centeredRawX = 2976.0f +
            (static_cast<float>(defeatedOrdinal) -
             static_cast<float>(defeatedCount - 1) * 0.5f) * 24.0f;
        Mina->SpawnPointSetPos(spawnPoint, raw_position(centeredRawX, 2464.0f));
        ++defeatedOrdinal;
        if ((AppliedBossStatueFlags & flag) != 0)
            continue;
        Mina->SpawnPointSetSpawnType(
            spawnPoint, kTileLevel2EntitySpawnType_RoomViewInfinite);
        Mina->SpawnPointReset(spawnPoint);
        BossStatueSpawnPoints[i] = spawnPoint;
        AppliedBossStatueFlags |= flag;
    }
}

void draw_tunnel(ycDrawUtil *draw)
{
    const float outerWidth = 3.8f;
    const float outerHeight = 5.2f;
    Mina->DebugDrawRectSolid(
        draw, kTunnelPos, outerWidth, outerHeight, MM_Color{104, 62, 45, 255}, true);
    Mina->DebugDrawRectSolid(
        draw,
        kTunnelPos,
        2.6f,
        4.0f,
        MM_Color{10, 8, 14, 255},
        true);
    MM_Vec3 lipTop = kTunnelPos;
    MM_Vec3 lipBottom = kTunnelPos;
    lipTop.y += 2.1f;
    lipBottom.y -= 2.1f;
    Mina->DebugDrawRectSolid(
        draw, lipTop, outerWidth, 0.5f, MM_Color{163, 97, 56, 255}, true);
    Mina->DebugDrawRectSolid(
        draw, lipBottom, outerWidth, 0.5f, MM_Color{67, 43, 42, 255}, true);
}

void draw_defeated_boss_statues(ycDrawUtil *draw)
{
    const uint64_t defeated = Mina->PlayerGetBossesDefeated();
    int displayed = 0;
    for (int bit = 0; bit < 64 && displayed < 18; ++bit)
    {
        if ((defeated & (uint64_t(1) << bit)) == 0)
            continue;
        const int row = displayed / 9;
        const int column = displayed % 9;
        const MM_Vec3 pos{
            291.0f + static_cast<float>(column) * 1.45f,
            -226.0f - static_cast<float>(row) * 1.6f,
            0.0f};
        const int variant = bit & 7;
        const MM_Color stone = variant & 1 ? MM_Color{165, 158, 139, 255}
                                           : MM_Color{139, 142, 140, 255};
        Mina->DebugDrawRectSolid(
            draw, pos, 0.8f, 1.2f, stone, true);
        MM_Vec3 head = pos;
        head.y += 0.7f;
        Mina->DebugDrawRectSolid(
            draw, head, variant & 2 ? 1.0f : 0.7f, 0.7f, stone, true);
        MM_Vec3 base = pos;
        base.y -= 0.7f;
        Mina->DebugDrawRectSolid(
            draw, base, 1.2f, 0.3f, MM_Color{71, 69, 68, 255}, true);
        ++displayed;
    }
}

void draw_dummy_health_bars(ycDrawUtil *draw)
{
    for (size_t i = 0; i < kDummyCount; ++i)
    {
        DummyRuntime &runtime = DummyRuntimes[i];
        if (!runtime.spawnPoint || !Mina->SpawnPointIsEntitySpawned(runtime.spawnPoint))
            continue;
        GameComponent *dummy = Mina->SpawnPointGetSpawnedEntity(runtime.spawnPoint);
        ycComponent *combatCore = find_combat_core(dummy);
        if (!combatCore)
            continue;
        const DummyTier &tier = kDummyTiers[i];
        const float maxHealth = Mina->CombatCoreGetHealthMax(combatCore);
        const float health = Mina->CombatCoreGetHealth(combatCore);
        float healthPercent = maxHealth > 0.0f ? health / maxHealth : 0.0f;
        if (healthPercent < 0.0f)
            healthPercent = 0.0f;
        if (healthPercent > 1.0f)
            healthPercent = 1.0f;

        MM_Vec3 center{tier.position.x, tier.position.y + 21.5f * kRawScaleY, 0.0f};
        Mina->DebugDrawRectSolid(
            draw,
            center,
            tier.barWidth + 2.2f * kRawScaleX,
            4.2f * kRawScaleY,
            MM_Color{20, 15, 19, 245},
            true);
        const float fillWidth = tier.barWidth * healthPercent;
        MM_Vec3 fillCenter = center;
        fillCenter.x -= (tier.barWidth - fillWidth) * 0.5f;
        Mina->DebugDrawRectSolid(
            draw, fillCenter, fillWidth, 2.4f * kRawScaleY, MM_Color{196, 48, 58, 255}, true);
        MM_Vec3 labelPos{tier.position.x - tier.barWidth * 0.24f,
                         tier.position.y + 25.5f * kRawScaleY,
                         0.0f};
        Mina->DebugDrawText(
            draw, tier.name, MM_Color{246, 228, 185, 255}, labelPos, 4.2f * kRawScaleX);
    }
}

void draw_locked_retail_training_room_blackout()
{
    if (!Mina || Mina->GetCurrentGameState() != kHubState)
        return;
    const uint32_t upgrades = Mina->PlayerGetUpgrades ? Mina->PlayerGetUpgrades() : 0;
    if ((upgrades & kTrainingDummyUpgradeFlag) != 0)
        return;
    ycDrawUtil *draw = Mina->GetDebugDraw("World");
    if (!draw)
        return;
    const MM_Vec3 center{297.6f, -230.4f, 50.0f};
    Mina->DebugDrawRectSolid(
        draw, center, 25.8f, 13.0f, MM_Color{0, 0, 0, 255}, true);
}

void draw_training_room_decor()
{
    ycDrawUtil *draw = Mina->GetDebugDraw("World");
    if (!draw)
        return;
    const MM_Vec3 poolPos{291.6f, -234.5f, 0.0f};
    Mina->DebugDrawRectSolid(
        draw, poolPos, 8.5f, 6.0f, MM_Color{67, 34, 91, 255}, true);
    Mina->DebugDrawRect(
        draw, poolPos, 8.5f, 6.0f, MM_Color{139, 106, 173, 255}, true);
    Mina->DebugDrawRectSolid(
        draw, poolPos, 7.0f, 4.5f, MM_Color{126, 112, 179, 215}, true);
    const float rugWidth = 118.0f * kRawScaleX;
    const float rugHeight = 64.0f * kRawScaleY;
    Mina->DebugDrawRectSolid(
        draw, kRugPos, rugWidth, rugHeight, MM_Color{112, 53, 126, 205}, true);
    Mina->DebugDrawRect(
        draw, kRugPos, rugWidth, rugHeight, MM_Color{222, 167, 75, 255}, true);
    for (int stripe = -2; stripe <= 2; ++stripe)
    {
        MM_Vec3 stripePos = kRugPos;
        stripePos.x += static_cast<float>(stripe) * 24.0f * kRawScaleX;
        Mina->DebugDrawRectSolid(
            draw,
            stripePos,
            5.0f * kRawScaleX,
            rugHeight - 8.0f * kRawScaleY,
            MM_Color{139, 69, 145, 115},
            true);
    }
    draw_defeated_boss_statues(draw);
}

void draw_underlab_tunnel()
{
    ycDrawUtil *draw = Mina->GetDebugDraw("World");
    if (draw)
        draw_tunnel(draw);
}

void draw_teleport_pad(ycDrawUtil *draw, const MM_Vec3 &position)
{
    Mina->DebugDrawRectSolid(
        draw, position, 1.6f, 1.6f, MM_Color{45, 35, 52, 255}, true);
    Mina->DebugDrawRect(
        draw, position, 1.6f, 1.6f, MM_Color{245, 190, 62, 255}, true);
    Mina->DebugDrawRectSolid(
        draw, position, 1.0f, 1.0f, MM_Color{55, 201, 199, 235}, true);
    Mina->DebugDrawRect(
        draw, position, 0.5f, 0.5f, MM_Color{249, 240, 180, 255}, true);
}

bool standing_on_pad(float playerX, float playerY, const MM_Vec3 &pad)
{
    return playerX >= pad.x - 0.9f && playerX <= pad.x + 0.9f &&
           playerY >= pad.y - 0.9f && playerY <= pad.y + 0.9f;
}

uint64_t monotonic_milliseconds()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void play_teleport_sound()
{
    if (Mina->SoundPlay)
        Mina->SoundPlay("portal_teleport");
}

void remember_teleport_vfx_root(
    GameComponent *component,
    ActiveTeleportVfxKind kind,
    float angle)
{
    if (!component || ActiveTeleportVfxRootCount >= 16)
        return;
    ycEntity *root = Mina->ComponentGetParent(reinterpret_cast<ycComponent *>(component));
    if (root)
        ActiveTeleportVfxRoots[ActiveTeleportVfxRootCount++] =
            ActiveTeleportVfx{root, kind, angle};
}

void spawn_native_teleport_tile(
    World *world,
    WorldRegion *region,
    const ycTileLevel2Entity *tileEntity,
    const MM_Vec3 &position,
    ActiveTeleportVfxKind kind,
    float angle)
{
    if (!world || !region || !tileEntity)
        return;
    SpawnManager *spawnManager = Mina->WorldRegionGetSpawnManager(region);
    if (!spawnManager)
        return;
    SpawnPoint *spawnPoint =
        Mina->SpawnManagerCreateSpawnPoint2(spawnManager, region, tileEntity);
    if (!spawnPoint)
        return;
    Mina->SpawnPointSetPos(spawnPoint, position);
    Mina->SpawnPointSetScale(spawnPoint, MM_Vec3{1.0f, 1.0f, 1.0f});
    GameComponent *effect = Mina->SpawnEntity2(
        Mina->WorldGetGameRootEntity(world),
        ENTITYTYPE_ANIM_TILE,
        position,
        MM_Vec3{1.0f, 1.0f, 1.0f},
        0.0f,
        1,
        spawnPoint,
        nullptr,
        nullptr);
    remember_teleport_vfx_root(effect, kind, angle);
}

void hide_active_teleport_vfx()
{
    for (size_t i = 0; i < ActiveTeleportVfxRootCount; ++i)
    {
        ycEntity *root = ActiveTeleportVfxRoots[i].root;
        if (!root)
            continue;
        MM_Transform transform = Mina->EntityGetLocalTransform(root);
        transform.s = MM_Vec3{0.0f, 0.0f, 0.0f};
        Mina->EntitySetLocalTransform(root, &transform);
        ActiveTeleportVfxRoots[i].root = nullptr;
    }
    ActiveTeleportVfxRootCount = 0;
}

void hide_active_sparkles_keep_ring()
{
    size_t kept = 0;
    for (size_t i = 0; i < ActiveTeleportVfxRootCount; ++i)
    {
        ActiveTeleportVfx effect = ActiveTeleportVfxRoots[i];
        if (!effect.root)
            continue;
        if (effect.kind == kTeleportVfxRing)
        {
            ActiveTeleportVfxRoots[kept++] = effect;
            continue;
        }
        MM_Transform transform = Mina->EntityGetLocalTransform(effect.root);
        transform.s = MM_Vec3{0.0f, 0.0f, 0.0f};
        Mina->EntitySetLocalTransform(effect.root, &transform);
    }
    ActiveTeleportVfxRootCount = kept;
}

void spawn_teleport_sparkle_wave(int wave)
{
    World *world = Mina->PlayerGetWorld();
    WorldRegion *region = Mina->PlayerGetWorldRegion ? Mina->PlayerGetWorldRegion() : nullptr;
    if (!world || !region)
        return;
    hide_active_sparkles_keep_ring();
    constexpr float tau = 6.28318530718f;
    const float radius = static_cast<float>(wave) * 0.45f;
    const float rotation = static_cast<float>(wave) * 0.18f;
    for (int i = 0; i < 8; ++i)
    {
        const float angle = tau * static_cast<float>(i) / 8.0f + rotation;
        MM_Vec3 sparklePos = ActiveTeleportVfxCenter;
        sparklePos.x += cosf(angle) * radius;
        sparklePos.y += sinf(angle) * radius * 0.62f;
        spawn_native_teleport_tile(
            world,
            region,
            TeleportSparkleEntity,
            sparklePos,
            kTeleportVfxSparkle,
            angle);
    }
    ActiveTeleportSparkleWave = wave;
}

void update_active_teleport_vfx()
{
    if (!ActiveTeleportVfxRootCount)
        return;
    const uint64_t elapsedMs = monotonic_milliseconds() - ActiveTeleportVfxStartedMs;
    const float progress = elapsedMs >= ActiveTeleportVfxDurationMs
        ? 1.0f
        : static_cast<float>(elapsedMs) / static_cast<float>(ActiveTeleportVfxDurationMs);
    int desiredWave = static_cast<int>(progress * 5.0f);
    if (desiredWave > 4)
        desiredWave = 4;
    if (desiredWave > ActiveTeleportSparkleWave)
        spawn_teleport_sparkle_wave(desiredWave);
}

void spawn_native_teleport_vfx(const MM_Vec3 &center)
{
    World *world = Mina->PlayerGetWorld();
    WorldRegion *region = Mina->PlayerGetWorldRegion ? Mina->PlayerGetWorldRegion() : nullptr;
    if (!world || !region)
    {
        Mina->Log("UnderLab Training Annex: native teleport VFX skipped because the player region was unavailable.\n");
        return;
    }

    hide_active_teleport_vfx();
    ActiveTeleportVfxCenter = center;
    ActiveTeleportVfxStartedMs = monotonic_milliseconds();
    ActiveTeleportVfxDurationMs =
        CurrentTeleportPhase == kTeleportDeparting ? 160 : 260;
    ActiveTeleportSparkleWave = -1;
    MM_Vec3 ringPos = center;
    spawn_native_teleport_tile(
        world, region, TeleportRingEntity, ringPos, kTeleportVfxRing, 0.0f);
    spawn_teleport_sparkle_wave(0);
    Mina->Log(
        "UnderLab Training Annex: spawned compact native-blue stars above a pad-centered concentric ring at (%.3f, %.3f).\n",
        center.x,
        center.y);
}

void draw_teleport_wisps(ycDrawUtil *draw, const MM_Vec3 &center, float progress, bool arriving)
{
    if (!draw)
        return;
    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;
    const float motion = arriving ? (1.0f - progress) : progress;
    const float radius = 0.45f + motion * 2.45f;
    constexpr float tau = 6.28318530718f;
    const MM_Color colors[3] = {
        MM_Color{37, 226, 205, 255},
        MM_Color{248, 176, 48, 255},
        MM_Color{226, 215, 181, 255},
    };
    for (int i = 0; i < 12; ++i)
    {
        const float phase = static_cast<float>(i) / 12.0f;
        const float angle = tau * (phase + progress * (arriving ? -1.4f : 1.4f));
        MM_Vec3 wisp = center;
        wisp.x += cosf(angle) * radius;
        wisp.y += sinf(angle) * radius * 0.62f - (0.35f + phase * 0.8f) * motion;
        const float size = 0.16f + 0.12f * sinf((phase + progress) * tau) *
            sinf((phase + progress) * tau);
        Mina->DebugDrawRectSolid(draw, wisp, size, size * 1.8f, colors[i % 3], true);
        MM_Vec3 tail = wisp;
        tail.y += 0.35f + 0.35f * motion;
        Mina->DebugDrawLine(draw, wisp, tail, colors[i % 3], true);
    }
    Mina->DebugDrawRect(
        draw, center, 1.4f + 1.8f * motion, 0.7f + 1.0f * motion,
        MM_Color{37, 226, 205, 220}, true);
}

void hide_player_art_in_entity(ycEntity *entity, ycEntity *playerRoot, size_t depth)
{
    if (!entity || depth > 8 || HiddenPlayerArtCount >= 64)
        return;
    const size_t childCount = Mina->EntityGetChildren(entity, nullptr, 0);
    if (!childCount)
        return;
    ycComponent **children = static_cast<ycComponent **>(
        Mina->Alloc(sizeof(ycComponent *) * childCount));
    if (!children)
        return;
    const size_t readCount = Mina->EntityGetChildren(entity, children, childCount);
    for (size_t i = 0; i < readCount && HiddenPlayerArtCount < 64; ++i)
    {
        ycComponent *component = children[i];
        if (!component)
            continue;
        const MM_Rtti type = Mina->ComponentGetType(component);
        if (rtti_equal(type, EntityRtti) ||
            (rtti_valid(EntityRtti) && Mina->ComponentIsa(component, EntityRtti)))
        {
            ycEntity *childEntity = reinterpret_cast<ycEntity *>(component);
            hide_player_art_in_entity(
                childEntity, playerRoot, depth + 1);
            continue;
        }
        if (!rtti_equal(type, GameAnimRtti) &&
            (!rtti_valid(GameAnimRtti) || !Mina->ComponentIsa(component, GameAnimRtti)))
            continue;
        ycEntity *artEntity = Mina->ComponentGetParent(component);
        if (!artEntity || artEntity == playerRoot)
            continue;
        bool alreadyHidden = false;
        for (size_t j = 0; j < HiddenPlayerArtCount; ++j)
            alreadyHidden = alreadyHidden || HiddenPlayerArtEntities[j].entity == artEntity;
        if (alreadyHidden)
            continue;
        HiddenPlayerArt &hidden = HiddenPlayerArtEntities[HiddenPlayerArtCount++];
        hidden.entity = artEntity;
        hidden.transform = Mina->EntityGetLocalTransform(artEntity);
        MM_Transform invisible = hidden.transform;
        invisible.s = MM_Vec3{0.0f, 0.0f, 0.0f};
        Mina->EntitySetLocalTransform(artEntity, &invisible);
    }
    Mina->Free(children);
}

void hide_player_art_for_teleport()
{
    if (PlayerArtHiddenForTeleport)
        return;
    HiddenPlayerArtCount = 0;
    ycComponent *playerComponent = Mina->PlayerGetComponent();
    ycEntity *playerRoot = playerComponent ? Mina->ComponentGetParent(playerComponent) : nullptr;
    HiddenPlayerRoot = playerRoot;
    if (playerRoot)
        hide_player_art_in_entity(playerRoot, playerRoot, 0);
    PlayerArtHiddenForTeleport = HiddenPlayerArtCount != 0;
    Mina->Log(
        "UnderLab Training Annex: concealed %zu Mina animation entities for teleport; parent/root and room furniture untouched.\n",
        HiddenPlayerArtCount);
}

void enforce_player_art_hidden_for_teleport()
{
    if (!PlayerArtHiddenForTeleport)
        return;

    ycComponent *playerComponent = Mina->PlayerGetComponent();
    ycEntity *playerRoot = playerComponent ? Mina->ComponentGetParent(playerComponent) : nullptr;
    if (!playerRoot)
        return;

    if (playerRoot != HiddenPlayerRoot)
    {
        HiddenPlayerRoot = playerRoot;
        for (size_t i = 0; i < HiddenPlayerArtCount; ++i)
            HiddenPlayerArtEntities[i].entity = nullptr;
        HiddenPlayerArtCount = 0;
        hide_player_art_in_entity(playerRoot, playerRoot, 0);
        Mina->Log(
            "UnderLab Training Annex: adopted rebuilt Mina hierarchy and concealed %zu animation entities only.\n",
            HiddenPlayerArtCount);
    }
    hide_player_art_in_entity(playerRoot, playerRoot, 0);
    for (size_t i = 0; i < HiddenPlayerArtCount; ++i)
    {
        HiddenPlayerArt &hidden = HiddenPlayerArtEntities[i];
        if (!hidden.entity)
            continue;
        MM_Transform invisible = hidden.transform;
        invisible.s = MM_Vec3{0.0f, 0.0f, 0.0f};
        Mina->EntitySetLocalTransform(hidden.entity, &invisible);
    }
}

void restore_player_art_after_teleport()
{
    if (!PlayerArtHiddenForTeleport)
        return;
    for (size_t i = 0; i < HiddenPlayerArtCount; ++i)
    {
        HiddenPlayerArt &hidden = HiddenPlayerArtEntities[i];
        if (hidden.entity)
            Mina->EntitySetLocalTransform(hidden.entity, &hidden.transform);
        hidden.entity = nullptr;
    }
    HiddenPlayerRoot = nullptr;
    HiddenPlayerArtCount = 0;
    PlayerArtHiddenForTeleport = false;
    Mina->Log("UnderLab Training Annex: restored player art for teleport arrival.\n");
}

void collect_blink_player_art(ycEntity *entity, size_t depth)
{
    if (!entity || depth > 8 || BlinkPlayerArtCount >= 32)
        return;
    const size_t childCount = Mina->EntityGetChildren(entity, nullptr, 0);
    if (!childCount)
        return;
    ycComponent **children = static_cast<ycComponent **>(
        Mina->Alloc(sizeof(ycComponent *) * childCount));
    if (!children)
        return;
    const size_t readCount = Mina->EntityGetChildren(entity, children, childCount);
    for (size_t i = 0; i < readCount && BlinkPlayerArtCount < 32; ++i)
    {
        ycComponent *child = children[i];
        if (!child)
            continue;
        if (Mina->ComponentIsa(child, EntityRtti))
        {
            collect_blink_player_art(reinterpret_cast<ycEntity *>(child), depth + 1);
            continue;
        }
        if (!Mina->ComponentIsa(child, GameAnimRtti))
            continue;
        ycEntity *artEntity = Mina->ComponentGetParent(child);
        if (!artEntity)
            continue;
        bool alreadyCollected = false;
        for (size_t j = 0; j < BlinkPlayerArtCount; ++j)
            alreadyCollected = alreadyCollected || BlinkPlayerArtEntities[j].entity == artEntity;
        if (alreadyCollected)
            continue;
        BlinkPlayerArt &art = BlinkPlayerArtEntities[BlinkPlayerArtCount++];
        art.entity = artEntity;
        art.transform = Mina->EntityGetLocalTransform(artEntity);
    }
    Mina->Free(children);
}

void set_post_teleport_blink_hidden(bool hidden)
{
    for (size_t i = 0; i < BlinkPlayerArtCount; ++i)
    {
        BlinkPlayerArt &art = BlinkPlayerArtEntities[i];
        if (!art.entity)
            continue;
        MM_Transform transform = art.transform;
        if (hidden)
            transform.s = MM_Vec3{0.0f, 0.0f, 0.0f};
        Mina->EntitySetLocalTransform(art.entity, &transform);
    }
    PostTeleportBlinkHidden = hidden;
}

void finish_post_teleport_blink()
{
    if (BlinkPlayerArtCount)
        set_post_teleport_blink_hidden(false);
    for (size_t i = 0; i < BlinkPlayerArtCount; ++i)
        BlinkPlayerArtEntities[i].entity = nullptr;
    BlinkPlayerArtCount = 0;
    PostTeleportBlinkActive = false;
    PostTeleportBlinkHidden = false;
}

void start_post_teleport_blink()
{
    finish_post_teleport_blink();
    ycComponent *playerComponent = Mina->PlayerGetComponent();
    ycEntity *playerRoot = playerComponent ? Mina->ComponentGetParent(playerComponent) : nullptr;
    if (playerRoot)
        collect_blink_player_art(playerRoot, 0);
    PostTeleportBlinkStartedMs = monotonic_milliseconds();
    PostTeleportBlinkActive = BlinkPlayerArtCount != 0;
    PostTeleportBlinkHidden = false;
    Mina->Log(
        "UnderLab Training Annex: post-teleport materialization blink started on %zu Mina animation entities; room art is untouched and control is enabled.\n",
        BlinkPlayerArtCount);
}

void update_post_teleport_blink()
{
    if (!PostTeleportBlinkActive)
        return;
    constexpr uint64_t kBlinkDurationMs = 360;
    constexpr uint64_t kBlinkIntervalMs = 60;
    const uint64_t elapsed = monotonic_milliseconds() - PostTeleportBlinkStartedMs;
    if (elapsed >= kBlinkDurationMs)
    {
        finish_post_teleport_blink();
        Mina->Log("UnderLab Training Annex: post-teleport materialization blink complete.\n");
        return;
    }
    const bool hidden = ((elapsed / kBlinkIntervalMs) & 1u) != 0;
    set_post_teleport_blink_hidden(hidden);
}

void begin_pad_teleport(bool toAnnex, const MM_Vec3 &source)
{
    finish_post_teleport_blink();
    if (!toAnnex)
        disable_temporary_training_upgrade();
    TeleportToAnnex = toAnnex;
    TeleportArrivalVfxStarted = false;
    TeleportPinnedPosition = source;
    TeleportPhaseStartedMs = monotonic_milliseconds();
    CurrentTeleportPhase = kTeleportDeparting;
    TeleportNeedsPadExit = true;
    hide_player_art_for_teleport();
    spawn_native_teleport_vfx(source);
    play_teleport_sound();
    Mina->Log(
        "UnderLab Training Annex: native departure sparkle/ring effect started toward %s.\n",
        toAnnex ? "the Annex" : "the checkpoint");
}

void FixedUpdate(void *)
{
    if (!Mina)
        return;

    const bool f8Pressed = key_pressed(YC_KEY_F8, F8WasDown);
    const int32_t state = Mina->GetCurrentGameState();
    enforce_locked_retail_training_room_boundary(state);

    if (!ChamberActive && state != kTrainingState)
    {
        const bool diveEntrance = at_underlab_entrance() && right_held() && jump_held();
        if (diveEntrance || (f8Pressed && at_underlab_entrance()))
            enter_training_annex();
        return;
    }

    if (ChamberActive && state == kTrainingState && f8Pressed)
        return_to_underlab();

    if (ChamberActive && state == kTrainingState && Mina->GetRoomTime() > 0.75f &&
        at_training_exit() && right_held() && jump_held())
        return_to_underlab();
}

struct WorldUpdateCtx
{
    World *world;
};

void WorldUpdate(void *rawCtx)
{
    if (!Mina || !rawCtx)
        return;
    WorldUpdateCtx *ctx = static_cast<WorldUpdateCtx *>(rawCtx);
    World *playerWorld = Mina->PlayerGetWorld();
    if (!ctx->world || ctx->world != playerWorld)
        return;

    const int32_t state = Mina->GetCurrentGameState();
    const uint32_t room = Mina->GetRoomIndex();
    float playerX = 0.0f;
    float playerY = 0.0f;
    Mina->PlayerGetPos(&playerX, &playerY);

    const MM_Vec3 checkpointPad{274.567993f, -234.394998f, 0.0f};
    const MM_Vec3 annexPad{291.2f, -253.6f, 0.0f};
    const float roomTime = Mina->GetRoomTime();
    if (state != kHubState || room != 2)
        disable_temporary_training_upgrade();
    draw_locked_retail_training_room_blackout();
    const bool onCheckpointPad = standing_on_pad(playerX, playerY, checkpointPad);
    const bool onAnnexPad = standing_on_pad(playerX, playerY, annexPad);
    if (CurrentTeleportPhase == kTeleportIdle)
    {
        if (TeleportNeedsPadExit)
        {
            if (!onCheckpointPad && !onAnnexPad)
                TeleportNeedsPadExit = false;
        }
        else if (state == kHubState && onCheckpointPad)
        {
            begin_pad_teleport(true, checkpointPad);
        }
        else if (state == kHubState && onAnnexPad)
        {
            begin_pad_teleport(false, annexPad);
        }
    }

    if (CurrentTeleportPhase != kTeleportIdle)
    {
        enforce_player_art_hidden_for_teleport();
        Mina->PlayerSetPos(TeleportPinnedPosition.x, TeleportPinnedPosition.y);
        playerX = TeleportPinnedPosition.x;
        playerY = TeleportPinnedPosition.y;
        uint64_t elapsed = monotonic_milliseconds() - TeleportPhaseStartedMs;
        update_active_teleport_vfx();

        const bool destinationRoomActive =
            (!TeleportToAnnex && room == 0) || (TeleportToAnnex && room == 2);
        if (CurrentTeleportPhase == kTeleportArriving &&
            !TeleportArrivalVfxStarted && destinationRoomActive &&
            roomTime >= 0.95f)
        {
            TeleportArrivalVfxStarted = true;
            TeleportPhaseStartedMs = monotonic_milliseconds();
            elapsed = 0;
            spawn_native_teleport_vfx(TeleportPinnedPosition);
            play_teleport_sound();
            Mina->Log(
                "UnderLab Training Annex: native arrival sparkle/ring effect and sound started in %s.\n",
                TeleportToAnnex ? "the Annex" : "the checkpoint");
        }

        if (CurrentTeleportPhase == kTeleportDeparting && elapsed >= 160)
        {
            hide_active_teleport_vfx();
            if (TeleportToAnnex)
            {
                TeleportPinnedPosition = MM_Vec3{kAnnexEntryX, kAnnexEntryY, 0.0f};
                StabilizeAnnexEntry = true;
                StabilizeCheckpointReturn = false;
            }
            else
            {
                TeleportPinnedPosition = kUnderLabReturnPos;
                StabilizeAnnexEntry = false;
                StabilizeCheckpointReturn = true;
            }
            playerX = TeleportPinnedPosition.x;
            playerY = TeleportPinnedPosition.y;
            Mina->PlayerSetPos(playerX, playerY);
            CurrentTeleportPhase = kTeleportArriving;
            TeleportPhaseStartedMs = monotonic_milliseconds();
        }
        else if (CurrentTeleportPhase == kTeleportArriving && TeleportArrivalVfxStarted &&
                 elapsed >= kTeleportArrivalRevealDelayMs &&
                 roomTime >= 0.95f &&
                 destinationRoomActive)
        {
            hide_active_teleport_vfx();
            restore_player_art_after_teleport();
            CurrentTeleportPhase = kTeleportIdle;
            LastTeleportRoomTime = roomTime;
            start_post_teleport_blink();
            Mina->Log(
                "UnderLab Training Annex: Mina reappeared in %s.\n",
                TeleportToAnnex ? "the Annex" : "the checkpoint");
        }
    }
    update_post_teleport_blink();
    if (StabilizeAnnexEntry && state == kHubState && room == 2)
    {
        playerX = kAnnexEntryX;
        playerY = kAnnexEntryY;
        Mina->PlayerSetPos(playerX, playerY);
        if (roomTime >= 0.6f)
        {
            StabilizeAnnexEntry = false;
            Mina->Log("UnderLab Training Annex: room-2 arrival placement stabilized.\n");
        }
    }
    if (StabilizeCheckpointReturn && state == kHubState && room == 0)
    {
        playerX = kUnderLabReturnPos.x;
        playerY = kUnderLabReturnPos.y;
        Mina->PlayerSetPos(playerX, playerY);
        if (roomTime >= 0.6f)
        {
            StabilizeCheckpointReturn = false;
            Mina->Log("UnderLab Training Annex: checkpoint return placement stabilized.\n");
        }
    }
    const bool inAnnex = state == kHubState &&
                         playerX >= kEntranceMinX && playerX <= kEntranceMaxX &&
                         playerY >= kEntranceMinY && playerY <= kEntranceMaxY;
    if (state != LastObservedState || room != LastObservedRoom)
    {
        const MM_Vec2 rawPlayer =
            Mina->SpawnPointSpawnPointInvTransformPos(MM_Vec2{playerX, playerY});
        Mina->Log(
            "UnderLab Training Annex observation: state=%d room=%u player=(%.3f, %.3f) "
            "inverseRaw=(%.3f, %.3f).\n",
            state,
            room,
            playerX,
            playerY,
            rawPlayer.x,
            rawPlayer.y);
        LastObservedState = state;
        LastObservedRoom = room;
    }
    if (inAnnex != WasInAnnex)
    {
        const bool enteringAnnex = inAnnex;
        Mina->Log(
            "UnderLab Training Annex: %s native annex at player=(%.3f, %.3f).\n",
            inAnnex ? "entered" : "left",
            playerX,
            playerY);
        WasInAnnex = inAnnex;
        if (enteringAnnex)
        {
            AnnexDummySpawnWindowComplete = false;
            for (size_t i = 0; i < kDummyCount; ++i)
                DummyRuntimes[i] = DummyRuntime{};
        }
    }
    if (!inAnnex)
    {
        PreviousPlayerX = playerX;
        return;
    }

    if (InitializedTrainingWorld != ctx->world)
    {
        InitializedTrainingWorld = ctx->world;
        AnnexRuntimeProbeLogged = false;
        AnnexDummySpawnWindowComplete = false;
        for (size_t i = 0; i < kDummyCount; ++i)
            DummyRuntimes[i] = DummyRuntime{};
        Mina->Log(
            "UnderLab Training Annex: binding Annex-only native targets at "
            "50/100/100 HP; retail room-1 dummies remain unmodified.\n");
    }
    if (!AnnexRuntimeProbeLogged)
        Mina->Log("UnderLab Training Annex probe: before PlayerGetWorldRegion.\n");
    WorldRegion *region = Mina->PlayerGetWorldRegion ? Mina->PlayerGetWorldRegion() : nullptr;
    if (!AnnexRuntimeProbeLogged)
        Mina->Log("UnderLab Training Annex probe: region=%p before dummy binding.\n", region);
    if (kCustomDummiesEnabled && !AnnexDummySpawnWindowComplete)
        enable_temporary_training_upgrade();
    if (kCustomDummiesEnabled)
        bind_and_configure_level_dummies(ctx->world, region);
    if (!AnnexRuntimeProbeLogged)
        Mina->Log("UnderLab Training Annex probe: dummy binding complete.\n");
    if (kCustomDummiesEnabled)
        maintain_dummy_health_tiers();
    if (kCustomDummiesEnabled && !AnnexDummySpawnWindowComplete)
    {
        bool allReady = true;
        for (size_t i = 0; i < kDummyCount; ++i)
        {
            const DummyRuntime &runtime = DummyRuntimes[i];
            allReady = allReady && runtime.spawnPoint &&
                Mina->SpawnPointIsEntitySpawned(runtime.spawnPoint) &&
                runtime.configuredComponent;
        }
        if (allReady)
        {
            AnnexDummySpawnWindowComplete = true;
            disable_temporary_training_upgrade();
            Mina->Log(
                "UnderLab Training Annex: all three native dummies configured; retail upgrade window closed before return.\n");
        }
    }
    if (!AnnexRuntimeProbeLogged)
        Mina->Log("UnderLab Training Annex probe: health maintenance complete.\n");
    activate_defeated_boss_statues(ctx->world, region);
    if (!AnnexRuntimeProbeLogged)
    {
        Mina->Log("UnderLab Training Annex probe: statue population complete.\n");
        AnnexRuntimeProbeLogged = true;
    }
    PreviousPlayerX = playerX;
}

void GameShutdown(void *)
{
    finish_post_teleport_blink();
    hide_active_teleport_vfx();
    restore_player_art_after_teleport();
    if (Mina)
    {
        disable_temporary_training_upgrade();
        restore_save_writes();
    }
}

void GameInit(void *)
{
    Mina->Log(
        "UnderLab Training Annex v68: sound-synchronized camera-settled teleport reveal and repeat-visit dummies; "
        "annex arrival=(%.3f, %.3f).\n",
        kAnnexEntryX,
        kAnnexEntryY);
}
} // namespace

extern "C" __declspec(dllexport) void MinaMod_Init(MinaModAPI *api)
{
    Mina = api;
    initialize_layout_from_level_coordinates();
    CombatCoreRtti = rtti_from_hash(Mina->Hash64("CombatCore", 10));
    EntityRtti = rtti_from_hash(Mina->Hash64("ycEntity", 8));
    GameAnimRtti = rtti_from_hash(Mina->Hash64("GameAnim", 8));
    TeleportSparkleEntity = Mina->CreateTileLevelEntity(kTeleportSparkleEntityDesc);
    TeleportRingEntity = Mina->CreateTileLevelEntity(kTeleportRingEntityDesc);
    if (!TeleportSparkleEntity || !TeleportRingEntity)
        Mina->Log("UnderLab Training Annex: failed to create one or more native teleport VFX descriptors.\n");
    Mina->Log(
        "UnderLab Training Annex string hashes: area_name_test_2=%u UnderLab=%u TrainingAnnexArea=%u.\n",
        Mina->Hash32("area_name_test_2", 16),
        Mina->Hash32("UnderLab", 8),
        Mina->Hash32("Training Annex Area", 19));
    Mina->InstallHook("GameInit", 0, GameInit);
    Mina->InstallHook("GameShutdown", 0, GameShutdown);
    Mina->InstallHook("WorldUpdate", 0, WorldUpdate);
    Mina->Log(
        "UnderLab Training Annex loaded in checkpoint state 29. Step onto the paired "
        "teleport pads to enter or leave the training screen.\n");
}
