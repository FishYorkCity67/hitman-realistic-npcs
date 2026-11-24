#pragma once

#include "IPluginInterface.h"

#include <Glacier/ZEntity.h>

#include "RnMath.h"
#include "RnSettings.h"
#include "RnStandoff.h"
#include "RnWounds.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class ZActor;
class ZHM5ItemWeapon;
class IBaseCharacter;
struct SHitInfo;

// Realistische NPCs:
//   - Bewaffnete warnen, bevor sie schiessen. Mit [T] kann 47 verhandeln: sich ergeben, reden, bestechen oder drohen.
//   - Rumpf-/Beintreffer und toedliche Messerstiche: der NPC bricht mit der Sterbe-Animation des Spiels zusammen
//     und liegt danach noch im Todeskampf (Zucken, Kraempfe, Zittern, Stoehnen, Blutlache). Lebende NPCs werden nie
//     per Ragdoll umgeworfen – das bringt die Animationen des Spiels durcheinander (T-Pose).
//   - Kopfschuesse sind sofort toedlich. Armtreffer schlagen die Waffe aus der Hand.
class RealisticNPCs : public IPluginInterface {
public:
    RealisticNPCs();
    ~RealisticNPCs() override;

    void Init() override;
    void OnEngineInitialized() override;
    void OnDrawUI(bool p_HasFocus) override;
    void OnDrawMenu() override;

private:
    // --- Abbild der Spielwelt (Spiel-Thread schreibt; Hooks und Oberflaeche lesen unter m_WorldMutex) ------------
    struct NpcInfo {
        uint64_t m_Key = 0;          // Entity-Zeiger als Schluessel
        ZActor* m_Actor = nullptr;
        rn::Vec3 m_Pos;
        float m_Distance = 0.f;
        bool m_Alive = false;
        bool m_Pacified = false;
        bool m_Armed = false;        // Wache oder Waffen erlaubt
        bool m_Elite = false;
        bool m_WeaponOut = false;
        std::string m_Name;          // Anzeigename (Rolle)
    };

    struct WorldState {
        bool m_InLevel = false;
        double m_Time = 0.0;
        rn::Vec3 m_PlayerPos;
        rn::Vec3 m_CameraPos;
        rn::Vec3 m_CameraForward;
        bool m_PlayerArmed = false;      // haelt sichtbar eine illegale Waffe
        bool m_PlayerDisguised = false;  // traegt eine Verkleidung statt des Anzugs
        std::vector<NpcInfo> m_Npcs;

        const NpcInfo* Find(uint64_t p_Key) const;
    };

    // --- Ereignisse aus den Hooks (koennen aus anderen Threads kommen) --------------------------------------------
    struct HitEvent {
        uint64_t m_Key = 0;
        ZActor* m_Actor = nullptr;
        rn::BodyZone m_Zone = rn::BodyZone::Unknown;
        rn::HitKind m_Kind = rn::HitKind::Shot;
        rn::HitDecision m_Decision;
        rn::Vec3 m_Pos;
        float m_Distance = 0.f;
        bool m_OnBody = false;       // Treffer auf eine Leiche im Todeskampf
        bool m_NeedsKill = false;    // sollte am Treffer sterben, lebt aber noch: im naechsten Bild toeten
        bool m_Lethal = false;       // abgeschwaechter Schaden war trotzdem toedlich
    };

    // --- Todeskampf (nur Spiel-Thread) ------------------------------------------------------------------------------
    // Ein Eintrag entsteht beim Treffer. Sobald das Spiel den NPC hat sterben lassen, beginnt der Todeskampf am Boden.
    struct Dying {
        ZActor* m_Actor = nullptr;
        rn::Agony m_Agony;
        std::string m_Name;
        bool m_Started = false;      // NPC ist tot, der Todeskampf laeuft
        double m_HitTime = 0.0;
        double m_WaitUntil = 0.0;    // bis dahin muss er gestorben sein, sonst kein Todeskampf
        double m_Seconds = 0.0;      // geplante Dauer (vom Treffer an)
        bool m_PoolSpawned = false;
        uint8_t m_BodyFlags = 0;     // gezogen / abgelegt / versteckt (beim Start), um Aenderungen zu erkennen
        uint64_t m_Seed = 0;
    };

    // --- Blut-Effekte (nur Spiel-Thread) -----------------------------------------------------------------------------
    enum class BloodKind { Spray = 0, Headshot, Ground, Pool, Count };

    struct SpawnedEffect {
        ZEntityRef m_Ref;
        double m_DeleteAt = 0.0; // Spielzeit; < 0 = bleibt bis zum Levelende
        bool m_Pool = false;
    };

    // --- Anzeige ----------------------------------------------------------------------------------------------------
    struct Subtitle {
        std::string m_Speaker;
        std::string m_Text;
        bool m_FromPlayer = false;
        double m_From = 0.0;  // Echtzeit
        double m_Until = 0.0; // Echtzeit
    };

    struct StandoffView {
        uint64_t m_Key = 0;
        std::string m_Name;
        float m_SecondsLeft = 0.f;
        float m_Distance = 0.f;
    };

    struct NegotiationView {
        bool m_Valid = false;
        uint64_t m_Key = 0;
        std::string m_Name;
        float m_SecondsLeft = 0.f;
        int m_Guards = 0;
        int m_Attempts = 0;
        bool m_PlayerArmed = false;
        std::array<float, static_cast<size_t>(rn::Plea::Count)> m_Chances {};
    };

    // --- Einstellungen ------------------------------------------------------------------------------------------------
    rn::SettingsStore MakeStore();
    rn::Settings CurrentSettings() const;
    void CommitSettings(const rn::Settings& p_Settings);
    void ApplySettings(const rn::Settings& p_Settings);

    // --- Spiel-Thread -------------------------------------------------------------------------------------------------
    void OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent);
    static double GameNow();
    static double RealNow();
    bool ReadWorld(WorldState& p_World) const;
    bool IsKnownActor(ZActor* p_Actor) const;   // noch im Level (lebend oder tot)
    bool IsLiveActor(ZActor* p_Actor) const;    // noch im Level und am Leben
    static uint8_t BodyFlags(ZActor* p_Actor);  // wird gezogen / abgelegt / ist versteckt
    void ProcessHits(const rn::Settings& p_Settings, double p_Now);
    void CheckMercyShots(double p_Now);
    void UpdateDying(const rn::Settings& p_Settings, double p_Now);
    void UpdateStandoffs(const rn::Settings& p_Settings, double p_Now);
    void UpdateTimeScale(const rn::Settings& p_Settings);
    void UpdateReputation(double p_Now);
    void ResetScene(const char* p_Reason);
    void RunSelfTest(const rn::Settings& p_Settings, double p_Now);
    void RunImpulseCalibration(const rn::Settings& p_Settings, double p_Now, const std::vector<const NpcInfo*>& p_Candidates);

    void StartAgony(const NpcInfo& p_Npc, double p_Seconds, double p_Now);
    void KillActor(ZActor* p_Actor, bool p_ImpactAnimation);
    void DisarmActor(ZActor* p_Actor, uint64_t p_Key, const rn::Settings& p_Settings, double p_Now, bool p_Surrendered);
    void ApplyImpulse(ZActor* p_Actor, const rn::Vec3& p_Impulse, uint32_t p_Bone);
    bool SpawnBlood(BloodKind p_Kind, const rn::Vec3& p_Pos);
    void UpdateBloodEffects(double p_Now);
    void DeleteAllEffects();
    rn::PleaContext MakePleaContext(const NpcInfo& p_Npc, int p_Guards) const;
    void ResolvePlea(rn::Plea p_Plea, const rn::Settings& p_Settings, double p_Now);

    // --- Anzeige (Render-Thread) --------------------------------------------------------------------------------------
    void Say(const std::string& p_Speaker, const std::string& p_Text, bool p_FromPlayer = false, float p_Seconds = 5.f);
    void HandleInput(const rn::Settings& p_Settings, bool p_HasFocus);
    void DrawHud(const rn::Settings& p_Settings);
    void DrawSubtitles();
    void DrawNegotiationMenu(const rn::Settings& p_Settings);
    void DrawSettingsWindow();
    void DrawDebugOverlay();
    void OpenMenu();
    void CloseMenu();

    // --- Protokoll (mods\RealisticNPCs.log) -----------------------------------------------------------------------
    void Log(const std::string& p_Line);
    void OpenLog();

    // --- Hooks --------------------------------------------------------------------------------------------------------
    DECLARE_PLUGIN_DETOUR(RealisticNPCs, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene);
    DECLARE_PLUGIN_DETOUR(RealisticNPCs, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound);
    DECLARE_PLUGIN_DETOUR(RealisticNPCs, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo);

    // --- Zustand ------------------------------------------------------------------------------------------------------
    mutable std::mutex m_SettingsMutex;
    rn::Settings m_Settings;
    std::unordered_map<std::string, std::string> m_SavedValues;

    mutable std::mutex m_WorldMutex;
    WorldState m_World;

    rn::StandoffTracker m_Standoffs;

    std::mutex m_EventMutex;
    std::vector<HitEvent> m_HitEvents;
    std::vector<uint64_t> m_NewStandoffs;
    std::unordered_set<uint64_t> m_DyingKeys; // fuer die Hooks: liegt im Todeskampf (oder stirbt gerade)?

    std::unordered_map<uint64_t, Dying> m_Dying;          // Spiel-Thread
    std::unordered_set<uint64_t> m_Disarmed;               // Spiel-Thread
    std::unordered_set<uint64_t> m_LastWarned;             // Spiel-Thread: "Letzte Warnung!" schon gerufen
    std::unordered_map<uint64_t, bool> m_PreviousAlive;    // Spiel-Thread (Ruf)
    std::unordered_set<uint64_t> m_CountedKills;           // Spiel-Thread
    std::atomic<int> m_ArmedKills = 0;
    uint64_t m_Addressee = 0;                               // mit wem gerade verhandelt wird (Spiel-Thread)

    std::atomic<double> m_LastPlayerShot = -1e9;
    std::atomic<double> m_LastPlayerAggression = -1e9;
    double m_LastShotChecked = -1e9;                         // Spiel-Thread: Gnadenschuss-Pruefung
    rn::Rng m_Rng;                                           // Spiel-Thread
    std::mutex m_HookRngMutex;
    rn::Rng m_HookRng { 0xC0FFEE1234567ull };

    // Zeitlupe im Verhandlungsmenue (Spiel-Thread).
    bool m_TimeScaleApplied = false;
    float m_PreviousTimeScale = 1.f;

    // Blut (Spiel-Thread). Pro Level wird gemerkt, welche Effekt-Vorlagen geladen sind (-1 = unbekannt, 0 = nein, 1 = ja).
    std::vector<SpawnedEffect> m_Effects;
    std::unordered_map<uint64_t, int> m_TemplateAvailable;
    std::atomic<int> m_BloodTemplatesFound = 0;
    bool m_BloodBroken = false;      // ein Effekt hat einen Fehler ausgeloest -> Blut fuer diese Sitzung aus
    bool m_LimbBonesBroken = false;  // Impuls auf Hand-Knochen fehlgeschlagen -> nur noch Becken/Kopf
    bool m_HeadBoneBroken = false;   // Impuls auf den Kopf fehlgeschlagen -> stattdessen Becken
    bool m_ImpulsesBroken = false;   // Impulse ganz aus (Fehler)

    // Oberflaeche
    mutable std::mutex m_UiMutex;
    std::deque<Subtitle> m_Subtitles;
    std::vector<StandoffView> m_StandoffViews;
    std::vector<std::string> m_DyingViews; // fuer die Diagnose-Anzeige
    NegotiationView m_Negotiation;
    std::string m_Status;
    double m_StatusUntil = 0.0;

    std::atomic<bool> m_MenuOpen = false;
    std::atomic<bool> m_RequestCloseMenu = false;
    std::atomic<int> m_PendingPlea = -1;
    bool m_ShowSettings = false;
    bool m_SettingsFocusRequested = false;
    bool m_KeyWasDown = false;
    double m_MenuOpenedAt = 0.0;
    int* m_CapturingKey = nullptr;

    // Diagnose
    std::atomic<int> m_BlockedShots = 0;
    std::atomic<int> m_AllowedShots = 0;
    std::atomic<int> m_UnknownShooters = 0;
    std::atomic<int> m_HitsSeen = 0;
    std::atomic<int> m_WoundsStarted = 0;  // begonnene Todeskaempfe
    std::atomic<int> m_DeathsCaused = 0;   // zu Ende gegangene Todeskaempfe
    std::atomic<int> m_BloodSpawned = 0;
    std::atomic<int> m_BloodFailed = 0;
    std::atomic<int> m_TotalStandoffs = 0;
    std::atomic<bool> m_EngineReady = false;

    std::mutex m_LogMutex;
    FILE* m_LogFile = nullptr;

    // Selbsttest (Spiel-Thread)
    int m_SelfTestStep = 0;
    double m_SelfTestStart = -1.0;
    double m_SelfTestNext = 0.0;
    uint64_t m_SelfTestDying = 0;
    uint64_t m_SelfTestStab = 0;
};

DECLARE_ZHM_PLUGIN(RealisticNPCs)
