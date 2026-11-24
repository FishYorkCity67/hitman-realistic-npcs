// Liest pro Bild den fuer die Mod relevanten Zustand aus dem Spiel (nur auf dem Spiel-Thread aufrufen).

#include "RealisticNPCs.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZAIGameState.h>
#include <Glacier/ZCameraEntity.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZOutfit.h>
#include <Glacier/ZSpatialEntity.h>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
    constexpr float c_WorldRadius = 70.f; // weiter entfernte NPCs interessieren nicht
    constexpr int c_MaxActors = 1000;

    rn::Vec3 ToVec(const float4& p_Value) {
        return { p_Value.x, p_Value.y, p_Value.z };
    }

    bool IsValidUtf8(const std::string& p_Text) {
        size_t i = 0;

        while (i < p_Text.size()) {
            const auto c = static_cast<unsigned char>(p_Text[i]);
            const size_t s_Length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;

            if (s_Length == 0 || i + s_Length > p_Text.size()) {
                return false;
            }

            for (size_t k = 1; k < s_Length; ++k) {
                if ((static_cast<unsigned char>(p_Text[i + k]) & 0xC0) != 0x80) {
                    return false;
                }
            }

            i += s_Length;
        }

        return true;
    }

    // Namen im Spiel sind teils Windows-1252 ("André"), die Oberflaeche braucht UTF-8.
    std::string ToStd(const ZString& p_Value) {
        std::string s_Text(p_Value.c_str(), p_Value.size());

        if (IsValidUtf8(s_Text)) {
            return s_Text;
        }

        const int s_Wide = MultiByteToWideChar(1252, 0, s_Text.data(), static_cast<int>(s_Text.size()), nullptr, 0);
        std::wstring s_WideText(static_cast<size_t>(std::max(0, s_Wide)), L'\0');
        MultiByteToWideChar(1252, 0, s_Text.data(), static_cast<int>(s_Text.size()), s_WideText.data(), s_Wide);

        const int s_Utf8 = WideCharToMultiByte(CP_UTF8, 0, s_WideText.data(), s_Wide, nullptr, 0, nullptr, nullptr);
        std::string s_Result(static_cast<size_t>(std::max(0, s_Utf8)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s_WideText.data(), s_Wide, s_Result.data(), s_Utf8, nullptr, nullptr);
        return s_Result;
    }

    std::string ToLower(std::string p_Text) {
        std::transform(p_Text.begin(), p_Text.end(), p_Text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return p_Text;
    }

    // Echte Namen ("Robert Knox") statt interner Bezeichnungen ("Guard_Kitchen_02").
    bool LooksLikePersonName(const std::string& p_Name) {
        if (p_Name.size() < 3 || p_Name.find(' ') == std::string::npos) {
            return false;
        }

        for (const char c : p_Name) {
            if (c == '_' || (c >= '0' && c <= '9') || c == '[' || c == ':' || c == '/') {
                return false;
            }
        }

        const std::string s_Lower = ToLower(p_Name);

        for (const char* s_Generic : { "guard", "npc", "civ", "crowd", "actor", "staff", "worker", "security", "waiter", "bodyguard" }) {
            if (s_Lower.find(s_Generic) != std::string::npos) {
                return false;
            }
        }

        return true;
    }
}

const RealisticNPCs::NpcInfo* RealisticNPCs::WorldState::Find(uint64_t p_Key) const {
    for (const auto& s_Npc : m_Npcs) {
        if (s_Npc.m_Key == p_Key) {
            return &s_Npc;
        }
    }

    return nullptr;
}

bool RealisticNPCs::IsKnownActor(ZActor* p_Actor) const {
    if (!p_Actor || !Globals::ActorManager || !Globals::NextActorId) {
        return false;
    }

    const int s_Count = std::min(static_cast<int>(*Globals::NextActorId), c_MaxActors);

    for (int i = 0; i < s_Count; ++i) {
        if (Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef == p_Actor) {
            return true;
        }
    }

    return false;
}

bool RealisticNPCs::IsLiveActor(ZActor* p_Actor) const {
    return IsKnownActor(p_Actor) && p_Actor->IsAlive();
}

uint8_t RealisticNPCs::BodyFlags(ZActor* p_Actor) {
    if (!p_Actor) {
        return 0;
    }

    return static_cast<uint8_t>((p_Actor->m_bIsBeingDragged ? 1 : 0) | (p_Actor->m_bIsBeingDumped ? 2 : 0) | (p_Actor->m_bBodyHidden ? 4 : 0));
}

bool RealisticNPCs::ReadWorld(WorldState& p_World) const {
    p_World = {};
    p_World.m_Time = GameNow();

    if (!Globals::ActorManager || !Globals::NextActorId || !Globals::Hitman5Module) {
        return false;
    }

    const auto* s_SceneContext = Globals::Hitman5Module->m_pEntitySceneContext;

    if (!s_SceneContext || !s_SceneContext->m_pScene) {
        return false;
    }

    // Im Hauptmenue gibt es auch einen "Spieler" – dort nichts tun.
    if (ToLower(ToStd(s_SceneContext->m_SceneInitParameters.m_SceneResource)).find("/frontend/") != std::string::npos) {
        return false;
    }

    const auto s_Player = SDK()->GetLocalPlayer();

    if (!s_Player || !s_Player.m_pInterfaceRef) {
        return false;
    }

    auto* s_PlayerSpatial = s_Player.m_entityRef.QueryInterface<ZSpatialEntity>();

    if (!s_PlayerSpatial) {
        return false;
    }

    const SMatrix s_PlayerMatrix = s_PlayerSpatial->GetObjectToWorldMatrix();
    p_World.m_PlayerPos = ToVec(s_PlayerMatrix.Trans);

    if (const auto* s_Kit = s_Player.m_pInterfaceRef->m_rOutfitKit.m_pInterfaceRef) {
        p_World.m_PlayerDisguised = !s_Kit->m_bIsHitmanSuit;
    }

    // Kamera: Glacier-Kameras schauen entlang ihrer -Z-Achse.
    if (auto* s_Camera = Functions::GetCurrentCamera->Call()) {
        const SMatrix s_CameraMatrix = s_Camera->GetObjectToWorldMatrix();
        p_World.m_CameraPos = ToVec(s_CameraMatrix.Trans);
        p_World.m_CameraForward = { -s_CameraMatrix.ZAxis.x, -s_CameraMatrix.ZAxis.y, -s_CameraMatrix.ZAxis.z };
    }
    else {
        p_World.m_CameraPos = p_World.m_PlayerPos;
        p_World.m_CameraForward = { -s_PlayerMatrix.YAxis.x, -s_PlayerMatrix.YAxis.y, 0.f };
    }

    if (Globals::GameStatsManager) {
        const ZAIGameState& s_State = Globals::GameStatsManager->m_gameState;
        p_World.m_PlayerArmed = s_State.m_bHoldingIllegalWeapon01 || s_State.m_bHoldingIllegalWeapon02;
    }

    // --- NPCs ---------------------------------------------------------------------------------------------------------
    const int s_Count = std::min(static_cast<int>(*Globals::NextActorId), c_MaxActors);
    p_World.m_Npcs.reserve(64);

    for (int i = 0; i < s_Count; ++i) {
        ZActor* s_Actor = Globals::ActorManager->m_activatedActors[i].m_pInterfaceRef;

        if (!s_Actor) {
            continue;
        }

        ZEntityRef s_Ref;
        s_Actor->GetID(s_Ref);

        if (!s_Ref) {
            continue;
        }

        auto* s_Spatial = s_Ref.QueryInterface<ZSpatialEntity>();

        if (!s_Spatial) {
            continue;
        }

        const rn::Vec3 s_Pos = ToVec(s_Spatial->GetObjectToWorldMatrix().Trans);
        const float s_Distance = rn::Distance(s_Pos, p_World.m_PlayerPos);

        if (s_Distance > c_WorldRadius) {
            continue;
        }

        NpcInfo s_Npc;
        s_Npc.m_Key = reinterpret_cast<uint64_t>(s_Ref.m_pObj);
        s_Npc.m_Actor = s_Actor;
        s_Npc.m_Pos = s_Pos;
        s_Npc.m_Distance = s_Distance;
        s_Npc.m_Alive = s_Actor->IsAlive();
        s_Npc.m_Pacified = s_Actor->IsPacified();
        s_Npc.m_WeaponOut = s_Actor->m_bWeaponUnholstered;

        const std::string s_ActorName = ToStd(s_Actor->m_sActorName);
        std::string s_Role;

        if (const auto* s_Kit = s_Actor->m_rOutfit.m_pInterfaceRef) {
            s_Npc.m_Armed = s_Kit->m_eActorType == EActorType::eAT_Guard || s_Kit->m_bWeaponsAllowed;
            s_Npc.m_Elite = s_Kit->m_eActorRank == EActorRank::AR_Elite || s_Kit->m_bAuthorityFigure;
            s_Role = ToStd(s_Kit->m_sCommonName);
        }

        if (LooksLikePersonName(s_ActorName)) {
            s_Npc.m_Name = s_ActorName;
        }
        else if (!s_Role.empty() && s_Role.find('_') == std::string::npos) {
            s_Npc.m_Name = s_Role;
        }
        else {
            s_Npc.m_Name = s_Npc.m_Armed ? "Wache" : "Passant";
        }

        p_World.m_Npcs.push_back(std::move(s_Npc));
    }

    std::sort(p_World.m_Npcs.begin(), p_World.m_Npcs.end(), [](const NpcInfo& a, const NpcInfo& b) {
        return a.m_Distance < b.m_Distance;
    });

    p_World.m_InLevel = true;
    return true;
}

rn::PleaContext RealisticNPCs::MakePleaContext(const NpcInfo& p_Npc, int p_Guards) const {
    rn::PleaContext s_Context;

    {
        std::scoped_lock s_Lock(m_WorldMutex);
        s_Context.m_PlayerArmed = m_World.m_PlayerArmed;
        s_Context.m_PlayerDisguised = m_World.m_PlayerDisguised;
    }

    s_Context.m_ArmedKills = m_ArmedKills.load();
    s_Context.m_GuardsInvolved = std::max(1, p_Guards);
    s_Context.m_Attempts = m_Standoffs.Attempts(p_Npc.m_Key);
    s_Context.m_Elite = p_Npc.m_Elite;
    return s_Context;
}
