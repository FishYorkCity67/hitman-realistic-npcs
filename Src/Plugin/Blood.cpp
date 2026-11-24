// Blut: erzeugt die Blut-Effekte des Spiels selbst an der Trefferstelle bzw. unter Verwundeten (Spiel-Thread).
// Die Vorlagen stammen aus den Spieldateien (Pfade aus der Hash-Liste von glacier-modding). Ist eine Vorlage im
// aktuellen Level nicht geladen, wird die naechste probiert – oder es gibt eben kein Extra-Blut.

#include "RealisticNPCs.h"

#include "Safe.h"

#include <Glacier/CompileReflection.h>
#include <Glacier/EntityFactory.h>
#include <Glacier/SExternalReferences.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZResource.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZSpatialEntity.h>

#include <cmath>
#include <format>
#include <iterator>

namespace {
    struct BloodTemplate {
        ZRuntimeResourceID m_Id;
        const char* m_Name;
        float m_Lifetime; // Sekunden, < 0 = bleibt (Blutlachen)
    };

    // Spritzer beim Treffer (Partikel, verschwinden von selbst).
    const BloodTemplate c_Spray[] = {
        { ResId<"[assembly:/_pro/effects/templates/impacts/fx_impacts_materialbased.template?/fx_pb_spray_blood.entitytemplate].pc_entitytype">, "fx_pb_spray_blood", 6.f },
        { ResId<"[assembly:/_pro/effects/templates/impacts/fx_impacts_closequarterscombatbased.template?/fx_e_impact_blood.entitytemplate].pc_entitytype">, "fx_e_impact_blood", 6.f },
        { ResId<"[assembly:/_pro/effects/templates/misc/fx_bloodsplatter.template?/fx_blood_splatter.entitytemplate].pc_entitytype">, "fx_blood_splatter", 8.f },
    };

    // Kopfschuss.
    const BloodTemplate c_Headshot[] = {
        { ResId<"[assembly:/_pro/effects/templates/misc/fx_bloodsplatter.template?/fx_e_blood_headshot.entitytemplate].pc_entitytype">, "fx_e_blood_headshot", 8.f },
    };

    // Spritzer auf dem Boden.
    const BloodTemplate c_Ground[] = {
        { ResId<"[assembly:/_pro/effects/templates/misc/fx_bloodsplatter.template?/fx_blood_splatter_burst_ground.entitytemplate].pc_entitytype">, "fx_blood_splatter_burst_ground", 30.f },
    };

    // Blutlache unter einem Verwundeten (waechst, bleibt liegen). Reine Effekt-Vorlagen zuerst; die Gameplay-Vorlage
    // (ai2) enthaelt KI-Logik und kommt nur zum Zug, wenn die anderen im Level fehlen.
    const BloodTemplate c_Pool[] = {
        { ResId<"[assembly:/_pro/effects/templates/decals/fx_bloodpool.template?/fx_e_spawnbloodpool.entitytemplate].pc_entitytype">, "fx_e_spawnbloodpool", -1.f },
        { ResId<"[assembly:/_pro/effects/templates/decals/fx_bloodpool.template?/fx_bloodpool.entitytemplate].pc_entitytype">, "fx_bloodpool", -1.f },
        { ResId<"[assembly:/templates/gameplay/ai2/bloodpool.template?/bloodpool.entitytemplate].pc_entitytype">, "bloodpool (ai2)", -1.f },
    };

    constexpr size_t c_MaxPools = 40;
    constexpr size_t c_MaxEffects = 160;

    uint64_t Key(const ZRuntimeResourceID& p_Id) {
        return (static_cast<uint64_t>(p_Id.m_IDHigh) << 32) | p_Id.m_IDLow;
    }
}

bool RealisticNPCs::SpawnBlood(BloodKind p_Kind, const rn::Vec3& p_Pos) {
    if (m_BloodBroken || !Globals::Hitman5Module || !Globals::Hitman5Module->m_pEntitySceneContext) {
        return false;
    }

    const auto s_Scene = Globals::Hitman5Module->m_pEntitySceneContext->m_pScene;

    if (!s_Scene || m_Effects.size() >= c_MaxEffects) {
        return false;
    }

    const BloodTemplate* s_Begin = nullptr;
    size_t s_Count = 0;

    switch (p_Kind) {
        case BloodKind::Headshot: s_Begin = c_Headshot; s_Count = std::size(c_Headshot); break;
        case BloodKind::Ground: s_Begin = c_Ground; s_Count = std::size(c_Ground); break;
        case BloodKind::Pool: s_Begin = c_Pool; s_Count = std::size(c_Pool); break;
        default: s_Begin = c_Spray; s_Count = std::size(c_Spray); break;
    }

    // Nicht in jedem Level sind alle Vorlagen geladen (in Paris z. B. kein Kopfschuss-Effekt):
    // dann eben ein normaler Spritzer an derselben Stelle.
    const auto s_Fallback = [&]() {
        if (p_Kind == BloodKind::Headshot || p_Kind == BloodKind::Ground) {
            return SpawnBlood(BloodKind::Spray, p_Pos);
        }

        ++m_BloodFailed;
        return false;
    };

    for (size_t i = 0; i < s_Count; ++i) {
        const BloodTemplate& s_Template = s_Begin[i];
        int& s_Known = m_TemplateAvailable.try_emplace(Key(s_Template.m_Id), -1).first->second;

        if (s_Known == 0) {
            continue;
        }

        ZEntityRef s_New;
        bool s_Loaded = false;

        const bool s_Ok = rn::SafeCall([&] {
            TResourcePtr<ZTemplateEntityFactory> s_Factory;
            Globals::ResourceManager->GetResourcePtr(s_Factory, s_Template.m_Id, 0);

            if (!s_Factory) {
                return;
            }

            s_Loaded = true;
            SExternalReferences s_ExternalRefs;
            Functions::ZEntityManager_NewEntity->Call(Globals::EntityManager, s_New, "", s_Factory, s_Scene.m_entityRef, s_ExternalRefs, -1);

            if (!s_New) {
                return;
            }

            s_New.SetProperty("m_eRoomBehaviour", ZSpatialEntity::ERoomBehaviour::ROOM_DYNAMIC);

            // Zufaellig um die Hochachse gedreht, damit nicht alles gleich aussieht.
            const float s_Angle = m_Rng.UniformF(0.f, 6.2831853f);
            SMatrix s_Matrix;
            s_Matrix.XAxis = float4 { std::cos(s_Angle), std::sin(s_Angle), 0.f, 0.f };
            s_Matrix.YAxis = float4 { -std::sin(s_Angle), std::cos(s_Angle), 0.f, 0.f };
            s_Matrix.ZAxis = float4 { 0.f, 0.f, 1.f, 0.f };
            s_Matrix.Trans = float4 { p_Pos.x, p_Pos.y, p_Pos.z + (p_Kind == BloodKind::Pool ? 0.02f : 0.f), 1.f };

            if (auto* s_Spatial = s_New.QueryInterface<ZSpatialEntity>()) {
                s_Spatial->SetObjectToWorldMatrixFromEditor(s_Matrix);
            }

            // Effekte starten (welcher Eingang es ist, haengt von der Vorlage ab; unbekannte Eingaenge passieren nichts).
            s_New.SignalInputPin("Start");
            s_New.SignalInputPin("Activate");
            s_New.SignalInputPin("Play");
            s_New.SignalInputPin("Spawn");
        });

        if (!s_Ok) {
            m_BloodBroken = true;
            ++m_BloodFailed;
            Log(std::format("Blut-Effekt {} hat einen Fehler ausgelöst – Extra-Blut für diese Sitzung abgeschaltet.", s_Template.m_Name));
            return false;
        }

        if (!s_Loaded) {
            if (s_Known != 0) {
                Log(std::format("Blut-Vorlage {} ist in diesem Level nicht geladen.", s_Template.m_Name));
            }

            s_Known = 0;
            continue;
        }

        if (!s_New) {
            s_Known = 0;
            ++m_BloodFailed;
            continue;
        }

        if (s_Known != 1) {
            s_Known = 1;
            ++m_BloodTemplatesFound;
            Log(std::format("Blut-Vorlage {} gefunden und erzeugt.", s_Template.m_Name));
        }

        const double s_Now = GameNow();
        SpawnedEffect s_Effect;
        s_Effect.m_Ref = s_New;
        s_Effect.m_Pool = s_Template.m_Lifetime < 0.f;
        s_Effect.m_DeleteAt = s_Effect.m_Pool ? -1.0 : s_Now + s_Template.m_Lifetime;
        m_Effects.push_back(s_Effect);
        ++m_BloodSpawned;
        return true;
    }

    return s_Fallback();
}

void RealisticNPCs::UpdateBloodEffects(double p_Now) {
    if (m_Effects.empty()) {
        return;
    }

    size_t s_Pools = 0;

    for (const SpawnedEffect& s_Effect : m_Effects) {
        s_Pools += s_Effect.m_Pool ? 1 : 0;
    }

    for (auto s_It = m_Effects.begin(); s_It != m_Effects.end();) {
        bool s_Delete = !s_It->m_Pool && p_Now >= s_It->m_DeleteAt;

        // Zu viele Blutlachen: die aeltesten verschwinden.
        if (s_It->m_Pool && s_Pools > c_MaxPools) {
            s_Delete = true;
            --s_Pools;
        }

        if (!s_Delete) {
            ++s_It;
            continue;
        }

        const ZEntityRef s_Ref = s_It->m_Ref;
        rn::SafeCall([&] {
            Functions::ZEntityManager_DeleteEntity->Call(Globals::EntityManager, s_Ref, {});
        });

        s_It = m_Effects.erase(s_It);
    }
}

void RealisticNPCs::DeleteAllEffects() {
    for (const SpawnedEffect& s_Effect : m_Effects) {
        const ZEntityRef s_Ref = s_Effect.m_Ref;
        rn::SafeCall([&] {
            Functions::ZEntityManager_DeleteEntity->Call(Globals::EntityManager, s_Ref, {});
        });
    }

    m_Effects.clear();
}
