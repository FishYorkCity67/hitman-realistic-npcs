// Realistic NPCs – Lebenszyklus, Einstellungen, Hooks und das Bild-fuer-Bild-Update.

#include "RealisticNPCs.h"

#include "Safe.h"

#include <Logging.h>

#include <Glacier/SGameUpdateEvent.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZGameTime.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZSpatialEntity.h>

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <ctime>
#include <filesystem>
#include <format>

namespace {
    constexpr const char* c_Version = "1.1.0";

    // Verzeichnis dieser DLL (…\Retail\mods).
    std::wstring OwnDllDirectory() {
        HMODULE s_Module = nullptr;
        GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&OwnDllDirectory), &s_Module
        );

        wchar_t s_Path[MAX_PATH] = {};
        GetModuleFileNameW(s_Module, s_Path, MAX_PATH);
        return std::filesystem::path(s_Path).parent_path().wstring();
    }
}

RealisticNPCs::RealisticNPCs() :
    m_Rng(static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) | 1ull) {}

RealisticNPCs::~RealisticNPCs() {
    if (m_EngineReady) {
        const ZMemberDelegate<RealisticNPCs, void(const SGameUpdateEvent&)> s_Delegate(this, &RealisticNPCs::OnFrameUpdate);
        Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);
    }

    if (m_TimeScaleApplied && Globals::GameTimeManager) {
        Globals::GameTimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
    }

    if (m_MenuOpen || m_SettingsFocusRequested) {
        SDK()->ReleaseUIFocus();
    }

    std::scoped_lock s_Lock(m_LogMutex);

    if (m_LogFile) {
        std::fclose(m_LogFile);
        m_LogFile = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Protokoll: mods\RealisticNPCs.log (wird beim Start neu angelegt, wenn es zu gross wird)
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::OpenLog() {
    std::scoped_lock s_Lock(m_LogMutex);

    if (m_LogFile) {
        return;
    }

    const std::filesystem::path s_Path = std::filesystem::path(OwnDllDirectory()) / L"RealisticNPCs.log";
    std::error_code s_Error;
    const bool s_TooBig = std::filesystem::exists(s_Path, s_Error) && std::filesystem::file_size(s_Path, s_Error) > 2 * 1024 * 1024;
    m_LogFile = _wfopen(s_Path.c_str(), s_TooBig ? L"w" : L"a");
}

void RealisticNPCs::Log(const std::string& p_Line) {
    Logger::Info("[RealisticNPCs] {}", p_Line);

    std::scoped_lock s_Lock(m_LogMutex);

    if (!m_LogFile) {
        return;
    }

    const std::time_t s_Now = std::time(nullptr);
    std::tm s_Local {};
    localtime_s(&s_Local, &s_Now);
    char s_Stamp[32];
    std::strftime(s_Stamp, sizeof(s_Stamp), "%Y-%m-%d %H:%M:%S", &s_Local);
    std::fprintf(m_LogFile, "[%s] %s\n", s_Stamp, p_Line.c_str());
    std::fflush(m_LogFile);
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------------------------------------------------

rn::SettingsStore RealisticNPCs::MakeStore() {
    rn::SettingsStore s_Store;

    s_Store.m_Get = [this](const std::string& p_Section, const std::string& p_Key, const std::string& p_Default) {
        const ZString s_Value = GetSetting(ZString(p_Section), ZString(p_Key), ZString(p_Default));
        std::string s_Result(s_Value.c_str(), s_Value.size());
        m_SavedValues[p_Section + "." + p_Key] = s_Result;
        return s_Result;
    };

    s_Store.m_Set = [this](const std::string& p_Section, const std::string& p_Key, const std::string& p_Value) {
        // Das SDK schreibt bei jedem SetSetting die ganze Datei – also nur Geaendertes schreiben.
        auto& s_Saved = m_SavedValues[p_Section + "." + p_Key];

        if (s_Saved != p_Value) {
            s_Saved = p_Value;
            SetSetting(ZString(p_Section), ZString(p_Key), ZString(p_Value));
        }
    };

    return s_Store;
}

rn::Settings RealisticNPCs::CurrentSettings() const {
    std::scoped_lock s_Lock(m_SettingsMutex);
    return m_Settings;
}

void RealisticNPCs::CommitSettings(const rn::Settings& p_Settings) {
    {
        std::scoped_lock s_Lock(m_SettingsMutex);
        m_Settings = p_Settings;
    }

    rn::SaveSettings(p_Settings, MakeStore());
    ApplySettings(p_Settings);
}

void RealisticNPCs::ApplySettings(const rn::Settings& p_Settings) {
    rn::StandoffConfig s_Config;
    s_Config.m_Enabled = p_Settings.m_Enabled && p_Settings.m_WarnBeforeFiring;
    s_Config.m_WarnSeconds = p_Settings.m_WarnSeconds;
    s_Config.m_ComplySeconds = p_Settings.m_ComplySeconds;
    s_Config.m_MaxSeconds = p_Settings.m_MaxStandoffSeconds;
    s_Config.m_StandDownSeconds = p_Settings.m_StandDownSeconds;
    s_Config.m_AggressionMemorySeconds = p_Settings.m_AggressionMemorySeconds;
    m_Standoffs.Configure(s_Config);
}

// ---------------------------------------------------------------------------------------------------------------------
// Lebenszyklus
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::Init() {
    OpenLog();

    // Erster Start? Dann alle Werte in die INI schreiben, damit man sie dort findet und aendern kann.
    const ZString s_Marker = GetSetting(ZString("general"), ZString("enabled"), ZString(""));
    const bool s_FirstRun = s_Marker.size() == 0;

    rn::Settings s_Settings;
    rn::LoadSettings(s_Settings, MakeStore());

    if (s_FirstRun) {
        m_SavedValues.clear();
    }

    // Ungueltige Taste (nur A–Z erlaubt) zuruecksetzen.
    if (s_Settings.m_NegotiateKey < 'A' || s_Settings.m_NegotiateKey > 'Z') {
        s_Settings.m_NegotiateKey = 'T';
    }

    {
        std::scoped_lock s_Lock(m_SettingsMutex);
        m_Settings = s_Settings;
    }

    rn::SaveSettings(s_Settings, MakeStore());
    ApplySettings(s_Settings);

    if (s_Settings.m_SelfTest) {
        Log("Selbsttest ist eingeschaltet und läuft im nächsten Level.");
    }

    Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &RealisticNPCs::OnClearScene);
    Hooks::ZHM5ItemWeapon_FireProjectiles->AddDetour(this, &RealisticNPCs::ZHM5ItemWeapon_FireProjectiles);
    Hooks::ZActor_YouGotHit->AddDetour(this, &RealisticNPCs::ZActor_YouGotHit);

    Log(std::format("Realistic NPCs {} geladen (Warnen: {}, Verwundungen: {}, Blut: {}).", c_Version,
        s_Settings.m_WarnBeforeFiring ? "an" : "aus", s_Settings.m_Wounds ? "an" : "aus", s_Settings.m_Blood ? "an" : "aus"));
}

void RealisticNPCs::OnEngineInitialized() {
    const ZMemberDelegate<RealisticNPCs, void(const SGameUpdateEvent&)> s_Delegate(this, &RealisticNPCs::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);
    m_EngineReady = true;
    Log("Engine bereit, Bild-Update registriert.");
}

double RealisticNPCs::GameNow() {
    return Globals::GameTimeManager ? Globals::GameTimeManager->m_tGameTime.ToSeconds() : 0.0;
}

double RealisticNPCs::RealNow() {
    static const LARGE_INTEGER s_Frequency = [] {
        LARGE_INTEGER s_Value;
        QueryPerformanceFrequency(&s_Value);
        return s_Value;
    }();

    LARGE_INTEGER s_Counter;
    QueryPerformanceCounter(&s_Counter);
    return static_cast<double>(s_Counter.QuadPart) / static_cast<double>(s_Frequency.QuadPart);
}

void RealisticNPCs::ResetScene(const char* p_Reason) {
    m_Standoffs.Reset();

    {
        std::scoped_lock s_Lock(m_EventMutex);
        m_HitEvents.clear();
        m_NewStandoffs.clear();
        m_DyingKeys.clear();
    }

    m_Dying.clear();
    m_Disarmed.clear();
    m_LastWarned.clear();
    m_PreviousAlive.clear();
    m_CountedKills.clear();
    m_ArmedKills = 0;
    m_Addressee = 0;
    m_Effects.clear();
    m_TemplateAvailable.clear();
    m_LastPlayerShot = -1e9;
    m_LastPlayerAggression = -1e9;
    m_LastShotChecked = -1e9;

    // Selbsttest: in einem neuen Level ggf. von vorn (falls er noch eingeschaltet ist).
    if (m_SelfTestStep > 0) {
        m_SelfTestStep = 0;
    }

    m_SelfTestStart = -1.0;

    if (m_TimeScaleApplied && Globals::GameTimeManager) {
        Globals::GameTimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
        m_TimeScaleApplied = false;
    }

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        m_World = {};
    }

    {
        std::scoped_lock s_Lock(m_UiMutex);
        m_StandoffViews.clear();
        m_Negotiation = {};
        m_Subtitles.clear();
    }

    m_RequestCloseMenu = true;
    Log(std::format("Zustand zurückgesetzt ({}).", p_Reason));
}

// ---------------------------------------------------------------------------------------------------------------------
// Bild-fuer-Bild (Spiel-Thread)
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {
    const rn::Settings s_Settings = CurrentSettings();
    const double s_Now = GameNow();

    WorldState s_World;
    const bool s_InLevel = ReadWorld(s_World);

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        m_World = s_World;
    }

    if (!s_InLevel) {
        return;
    }

    if (!s_Settings.m_Enabled) {
        m_RequestCloseMenu = true;
        UpdateTimeScale(s_Settings);
        return;
    }

    ProcessHits(s_Settings, s_Now);
    CheckMercyShots(s_Now);
    UpdateDying(s_Settings, s_Now);
    UpdateReputation(s_Now);
    UpdateStandoffs(s_Settings, s_Now);
    UpdateTimeScale(s_Settings);
    UpdateBloodEffects(s_Now);
    RunSelfTest(s_Settings, s_Now);
}

// ---------------------------------------------------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------------------------------------------------

DEFINE_PLUGIN_DETOUR(RealisticNPCs, void, OnClearScene, ZEntitySceneContext* th, bool p_FullyUnloadScene) {
    // Eigene Effekte entfernen, solange die Szene noch existiert.
    DeleteAllEffects();
    ResetScene("Szene entladen");
    return { HookAction::Continue() };
}

DEFINE_PLUGIN_DETOUR(RealisticNPCs, bool, ZHM5ItemWeapon_FireProjectiles, ZHM5ItemWeapon* th, bool bMayStartSound) {
    if (!th || !m_EngineReady) {
        return { HookAction::Continue() };
    }

    const rn::Settings s_Settings = CurrentSettings();

    if (!s_Settings.m_Enabled) {
        return { HookAction::Continue() };
    }

    const double s_Now = GameNow();
    const auto s_Player = SDK()->GetLocalPlayer();

    // 47 schiesst. Waehrend einer Konfrontation heisst das: Feuer frei.
    if (s_Player && th->m_pOwner == s_Player.m_entityRef) {
        m_LastPlayerShot = s_Now;

        if (!m_Standoffs.Warnings(s_Now).empty()) {
            m_LastPlayerAggression = s_Now;
            m_Standoffs.OnPlayerAggression(s_Now);
        }

        return { HookAction::Continue() };
    }

    // Welcher NPC haelt die Waffe? (Besitzer selbst oder sein logischer Elternteil.)
    uint64_t s_Key = 0;

    {
        std::scoped_lock s_Lock(m_WorldMutex);

        if (!m_World.m_InLevel) {
            return { HookAction::Continue() };
        }

        const ZEntityRef s_Candidates[] = { th->m_pOwner, th->m_pOwner.GetLogicalParent(), th->m_pOwner.GetOwningEntity() };

        for (const ZEntityRef& s_Candidate : s_Candidates) {
            const auto s_Raw = reinterpret_cast<uint64_t>(s_Candidate.m_pObj);

            if (s_Raw != 0 && m_World.Find(s_Raw)) {
                s_Key = s_Raw;
                break;
            }
        }
    }

    if (s_Key == 0) {
        ++m_UnknownShooters;
        return { HookAction::Continue() };
    }

    // Wer im Todeskampf am Boden liegt, schiesst nicht mehr.
    {
        std::scoped_lock s_Lock(m_EventMutex);

        if (m_DyingKeys.contains(s_Key)) {
            ++m_BlockedShots;
            return { HookAction::Return(), false };
        }
    }

    const rn::ShotDecision s_Decision = m_Standoffs.OnShotAttempt(s_Key, s_Now);

    if (s_Decision.m_NewStandoff) {
        std::scoped_lock s_Lock(m_EventMutex);
        m_NewStandoffs.push_back(s_Key);
    }

    if (s_Decision.m_Block) {
        ++m_BlockedShots;
        return { HookAction::Return(), false };
    }

    ++m_AllowedShots;
    return { HookAction::Continue() };
}

DEFINE_PLUGIN_DETOUR(RealisticNPCs, bool, ZActor_YouGotHit, IBaseCharacter* th, const SHitInfo& hitInfo) {
    if (!th || !m_EngineReady) {
        return { HookAction::Continue() };
    }

    const rn::Settings s_Settings = CurrentSettings();

    if (!s_Settings.m_Enabled) {
        return { HookAction::Continue() };
    }

    auto* s_Actor = static_cast<ZActor*>(th);
    ZEntityRef s_Ref;
    s_Actor->GetID(s_Ref);
    const auto s_Key = reinterpret_cast<uint64_t>(s_Ref.m_pObj);
    const double s_Now = GameNow();

    // Das Spiel verraet die Waffe nicht: kurz vorher geschossen = Schuss, sonst direkt daneben = Nahkampf.
    const bool s_Shot = s_Now - m_LastPlayerShot.load() < 1.5;
    NpcInfo s_Npc;
    bool s_Found = false;
    rn::Vec3 s_CameraPos;
    rn::Vec3 s_CameraForward;
    bool s_PlayerArmed = false;

    {
        std::scoped_lock s_Lock(m_WorldMutex);

        if (const NpcInfo* s_Info = m_World.Find(s_Key)) {
            s_Npc = *s_Info;
            s_Found = true;
        }

        s_CameraPos = m_World.m_CameraPos;
        s_CameraForward = m_World.m_CameraForward;
        s_PlayerArmed = m_World.m_PlayerArmed;
    }

    // Nur bekannte NPCs; Treffer von anderen NPCs bleiben unangetastet.
    if (!s_Found) {
        return { HookAction::Continue() };
    }

    const bool s_Melee = !s_Shot && s_Npc.m_Distance < 2.5f;

    if (!s_Shot && !s_Melee) {
        return { HookAction::Continue() };
    }

    bool s_InAgony = false;

    {
        std::scoped_lock s_Lock(m_EventMutex);
        s_InAgony = m_DyingKeys.contains(s_Key);
    }

    // Leichen: nur ein Treffer auf jemanden im Todeskampf zaehlt (Gnadenschuss). Den Treffer selbst macht das Spiel.
    if (!s_Npc.m_Alive) {
        if (s_InAgony) {
            HitEvent s_Event;
            s_Event.m_Key = s_Key;
            s_Event.m_Actor = s_Actor;
            s_Event.m_Pos = s_Npc.m_Pos;
            s_Event.m_Distance = s_Npc.m_Distance;
            s_Event.m_OnBody = true;
            s_Event.m_Kind = s_Shot ? rn::HitKind::Shot : rn::HitKind::Stab;
            s_Event.m_Decision.m_FinishSoon = true;
            s_Event.m_Decision.m_BloodBursts = s_Settings.m_Blood ? 1 : 0;

            std::scoped_lock s_Lock(m_EventMutex);
            m_HitEvents.push_back(s_Event);
        }

        return { HookAction::Continue() };
    }

    ++m_HitsSeen;
    m_LastPlayerAggression = s_Now;
    m_Standoffs.OnPlayerAggression(s_Now);

    HitEvent s_Event;
    s_Event.m_Key = s_Key;
    s_Event.m_Actor = s_Actor;
    s_Event.m_Pos = s_Npc.m_Pos;
    s_Event.m_Distance = s_Npc.m_Distance;
    s_Event.m_Kind = s_Shot ? rn::HitKind::Shot : (s_PlayerArmed ? rn::HitKind::Stab : rn::HitKind::Melee);
    s_Event.m_Zone = s_Shot ? rn::EstimateHitZone(s_CameraPos, s_CameraForward, s_Npc.m_Pos) : rn::BodyZone::Torso;

    {
        std::scoped_lock s_Lock(m_HookRngMutex);
        s_Event.m_Decision = rn::DecideHit(s_Event.m_Zone, s_Event.m_Kind, s_InAgony, s_Settings, m_HookRng);
    }

    bool s_Result = false;
    bool s_Handled = false;
    const float s_HitPoints = s_Actor->m_fCurrentHitPoints;

    if (s_Event.m_Decision.m_Fatal && s_HitPoints > 0.f) {
        // Toedlich – aber ueber das Spiel selbst: Mit fast keinen Lebenspunkten stirbt er an genau diesem Treffer, mit
        // der passenden Sterbe-Animation und dem Ragdoll des Spiels. (Lebende NPCs selbst umzuwerfen endet in der T-Pose.)
        constexpr float c_Tiny = 0.001f;
        s_Actor->m_fCurrentHitPoints = c_Tiny;
        s_Result = p_Hook->CallOriginal(th, hitInfo);
        s_Handled = true;

        if (s_Actor->m_fCurrentHitPoints >= c_Tiny) {
            // Das Spiel hat keinen Schaden angerechnet (z. B. per Skript unverwundbar): alles wie vorher.
            s_Actor->m_fCurrentHitPoints = s_HitPoints;
            s_Event.m_Decision.m_Fatal = false;
            s_Event.m_Decision.m_Agony = false;
        }
        else if (s_Actor->IsAlive()) {
            // Sollte jetzt tot sein – falls das Spiel es nicht selbst erledigt, im naechsten Bild nachhelfen.
            s_Event.m_NeedsKill = true;
        }
    }
    // Nicht toedlich (Arm, Bein ohne Todeskampf): Das Spiel rechnet den Schaden selbst aus (mit grossem Puffer, damit
    // dabei niemand stirbt), danach ziehen wir nur einen Teil ab. So laufen Treffer-Reaktion und Effekte ganz normal.
    else if (!s_Event.m_Decision.m_Vanilla && !s_Event.m_Decision.m_Fatal && s_HitPoints > 0.f) {
        constexpr float c_Buffer = 100000.f;
        const float s_Before = s_Actor->m_fCurrentHitPoints;

        s_Actor->m_fCurrentHitPoints = s_Before + c_Buffer;
        s_Result = p_Hook->CallOriginal(th, hitInfo);
        const float s_After = s_Actor->m_fCurrentHitPoints;
        s_Handled = true;

        // Hat das Spiel den Wert ganz anders gesetzt (unerwartet), lassen wir sein Ergebnis stehen.
        if (s_After > c_Buffer * 0.5f) {
            const float s_Damage = std::max(0.f, s_Before + c_Buffer - s_After);
            const float s_NewHp = s_Before - s_Damage * s_Event.m_Decision.m_DamageFactor;

            if (s_NewHp > 0.f) {
                s_Actor->m_fCurrentHitPoints = s_NewHp;
            }
            else {
                // Auch abgeschwaecht toedlich: im naechsten Bild sauber ueber das Spiel sterben lassen.
                s_Actor->m_fCurrentHitPoints = 0.01f;
                s_Event.m_Lethal = true;
            }
        }
    }

    {
        std::scoped_lock s_Lock(m_EventMutex);
        m_HitEvents.push_back(s_Event);
    }

    if (s_Handled) {
        return { HookAction::Return(), s_Result };
    }

    return { HookAction::Continue() };
}

DEFINE_ZHM_PLUGIN(RealisticNPCs);
