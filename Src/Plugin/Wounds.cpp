// Verwundungen im Spiel: Treffer auswerten, Entwaffnen und der Todeskampf am Boden (Zucken, Kraempfe, Zittern,
// Stoehnen, Blut). Alles laeuft auf dem Spiel-Thread; die Treffer selbst kommen als Ereignisse aus dem Hook.
//
// Lebende NPCs werden hier nie per Ragdoll umgeworfen: Das bringt die Animationen des Spiels durcheinander und
// endet in der T-Pose. Ein toedlich getroffener NPC stirbt ueber das Spiel (Sterbe-Animation und Ragdoll des Spiels),
// der Todeskampf spielt sich danach am Koerper am Boden ab.

#include "RealisticNPCs.h"

#include "RnLines.h"
#include "Safe.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZHM5BaseCharacter.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZSetpieceEntity.h>
#include <Glacier/ZSpatialEntity.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace {
    // Hoehe ueber den Fuessen, an der ein Treffer in der jeweiligen Zone sitzt.
    float ZoneHeight(rn::BodyZone p_Zone) {
        switch (p_Zone) {
            case rn::BodyZone::Head: return 1.65f;
            case rn::BodyZone::Arm: return 1.3f;
            case rn::BodyZone::Leg: return 0.6f;
            default: return 1.2f;
        }
    }

    const char* KindName(rn::HitKind p_Kind) {
        switch (p_Kind) {
            case rn::HitKind::Shot: return "Schuss";
            case rn::HitKind::Stab: return "Stich";
            default: return "Schlag";
        }
    }

    // Was aus dem Treffer wird, fuer das Protokoll.
    std::string Outcome(const rn::HitDecision& p_Decision) {
        if (p_Decision.m_Fatal && p_Decision.m_Agony) {
            return std::format("bricht zusammen, Todeskampf {:.0f} s", p_Decision.m_AgonySeconds);
        }

        if (p_Decision.m_Agony) {
            return std::format("Todeskampf {:.0f} s, falls tödlich", p_Decision.m_AgonySeconds);
        }

        if (p_Decision.m_TryDisarm) {
            return "Waffe fällt aus der Hand";
        }

        if (!p_Decision.m_Vanilla && p_Decision.m_DamageFactor < 1.f) {
            return std::format("nur {:.0f} % Schaden", p_Decision.m_DamageFactor * 100.f);
        }

        return "wie im Spiel";
    }

    // Ein Gnadenschuss trifft den Koerper am Boden, wenn der Zielstrahl so nah an ihm vorbeigeht.
    constexpr float c_MercyShotRadius = 0.75f;
    // So lange darf das Spiel brauchen, bis ein toedlich Getroffener als tot gilt.
    constexpr double c_DeathWaitSeconds = 4.0;
}

// ---------------------------------------------------------------------------------------------------------------------
// Grundfunktionen
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::KillActor(ZActor* p_Actor, bool p_ImpactAnimation) {
    if (!IsLiveActor(p_Actor)) {
        return;
    }

    const bool s_Ok = rn::SafeCall([&] {
        TEntityRef<IItem> s_Item;
        TEntityRef<ZSetpieceEntity> s_Setpiece;
        Functions::ZActor_KillActor->Call(
            p_Actor, s_Item, s_Setpiece, EDamageEvent::eDE_Shoot,
            p_ImpactAnimation ? EDeathBehavior::eDB_IMPACT_ANIM : EDeathBehavior::eDB_NO_IMPACT_ANIM
        );
    });

    if (!s_Ok) {
        Log("Fehler beim Töten eines NPCs abgefangen.");
    }
}

void RealisticNPCs::ApplyImpulse(ZActor* p_Actor, const rn::Vec3& p_Impulse, uint32_t p_Bone) {
    if (m_ImpulsesBroken || !p_Actor || !p_Actor->m_pRagdollHandler) {
        return;
    }

    // Knochen, die einmal Probleme gemacht haben, durch das Becken ersetzen (das ist an NPCs erprobt).
    if ((p_Bone == rn::c_BoneHead && m_HeadBoneBroken) ||
        (p_Bone != rn::c_BonePelvis && p_Bone != rn::c_BoneHead && m_LimbBonesBroken)) {
        p_Bone = rn::c_BonePelvis;
    }

    const auto s_Apply = [&](uint32_t p_Index) {
        return rn::SafeCall([&] {
            Functions::ZRagdollHandler_ApplyImpulseOnRagdoll->Call(
                p_Actor->m_pRagdollHandler, float4 {}, float4 { p_Impulse.x, p_Impulse.y, p_Impulse.z, 0.f }, p_Index, false
            );
        });
    };

    if (s_Apply(p_Bone)) {
        return;
    }

    if (p_Bone == rn::c_BonePelvis) {
        m_ImpulsesBroken = true;
        Log("Impuls auf das Becken fehlgeschlagen – Zucken für diese Sitzung abgeschaltet.");
        return;
    }

    if (p_Bone == rn::c_BoneHead) {
        m_HeadBoneBroken = true;
        Log("Impuls auf den Kopf fehlgeschlagen – ab jetzt zuckt nur noch der Körper.");
    }
    else {
        m_LimbBonesBroken = true;
        Log(std::format("Impuls auf Knochen {} fehlgeschlagen – Arme zucken ab jetzt nicht mehr.", p_Bone));
    }

    // Diesmal eben am Becken (gedaempft, der Kopf-/Arm-Impuls war fuer leichtere Koerperteile gedacht).
    if (!rn::SafeCall([&] {
            Functions::ZRagdollHandler_ApplyImpulseOnRagdoll->Call(
                p_Actor->m_pRagdollHandler, float4 {}, float4 { p_Impulse.x * 1.5f, p_Impulse.y * 1.5f, p_Impulse.z * 1.5f, 0.f },
                rn::c_BonePelvis, false
            );
        })) {
        m_ImpulsesBroken = true;
    }
}

void RealisticNPCs::DisarmActor(ZActor* p_Actor, uint64_t p_Key, const rn::Settings& p_Settings, double p_Now, bool p_Surrendered) {
    if (!IsLiveActor(p_Actor)) {
        return;
    }

    ZActorInventoryHandler* s_Inventory = p_Actor->m_pInventoryHandler;

    if (!s_Inventory || !s_Inventory->m_rMainWeapon) {
        return;
    }

    // Wie die Editor-Mod des SDK, aber "in die Physikwelt": die Waffe faellt sichtbar zu Boden.
    const bool s_Ok = rn::SafeCall([&] {
        const TEntityRef<IItem> s_Weapon(s_Inventory->m_rMainWeapon.m_entityRef);
        p_Actor->ReleaseItem(s_Weapon, true);
        s_Inventory->m_rMainWeapon = {};
        p_Actor->m_bWeaponUnholstered = false;
    });

    if (!s_Ok) {
        Log("Fehler beim Entwaffnen abgefangen.");
        return;
    }

    m_Disarmed.insert(p_Key);

    // Ohne Waffe schiesst er nicht mehr – auch wenn die Spiel-KI ihn noch zielen laesst.
    m_Standoffs.Apply(p_Key, rn::PleaResult::GuardSurrenders, p_Now);

    if (p_Surrendered) {
        return;
    }

    if (const NpcInfo* s_Npc = m_World.Find(p_Key)) {
        Say(s_Npc->m_Name, rn::lines::Disarmed(p_Key ^ static_cast<uint64_t>(p_Now * 7.0)));
        Log(std::format("{} wurde die Waffe aus der Hand geschossen.", s_Npc->m_Name));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Treffer aus dem Hook verarbeiten
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::ProcessHits(const rn::Settings& p_Settings, double p_Now) {
    std::vector<HitEvent> s_Events;

    {
        std::scoped_lock s_Lock(m_EventMutex);
        s_Events.swap(m_HitEvents);
    }

    for (const HitEvent& s_Event : s_Events) {
        const NpcInfo* s_Found = m_World.Find(s_Event.m_Key);

        if (!s_Found) {
            continue;
        }

        const NpcInfo s_Npc = *s_Found;
        const rn::HitDecision& s_Decision = s_Event.m_Decision;

        // Blut an der Trefferstelle (auch bei Kopfschuessen, die das Spiel selbst erledigt).
        if (p_Settings.m_Blood && s_Decision.m_BloodBursts > 0) {
            const float s_Height = s_Event.m_OnBody ? 0.3f : ZoneHeight(s_Event.m_Zone);
            const rn::Vec3 s_HitPos { s_Npc.m_Pos.x, s_Npc.m_Pos.y, s_Npc.m_Pos.z + s_Height };
            const BloodKind s_Kind = s_Event.m_Zone == rn::BodyZone::Head && !s_Event.m_OnBody ? BloodKind::Headshot : BloodKind::Spray;

            for (int i = 0; i < s_Decision.m_BloodBursts; ++i) {
                SpawnBlood(i == 0 ? s_Kind : BloodKind::Spray, s_HitPos);
            }

            if (!s_Event.m_OnBody) {
                SpawnBlood(BloodKind::Ground, s_Npc.m_Pos);
            }
        }

        // Treffer auf jemanden im Todeskampf: Gnadenschuss.
        if (s_Event.m_OnBody || s_Decision.m_FinishSoon) {
            if (auto s_It = m_Dying.find(s_Event.m_Key); s_It != m_Dying.end() && s_It->second.m_Started && !s_It->second.m_Agony.Finishing()) {
                s_It->second.m_Agony.FinishSoon(p_Now, m_Rng);
                Log(std::format("{}: Gnadenschuss – der Todeskampf ist gleich vorbei.", s_Npc.m_Name));
            }

            continue;
        }

        Log(std::format("Treffer: {} – {} ({}, {:.1f} m) – {}.", s_Npc.m_Name, rn::ZoneName(s_Event.m_Zone), KindName(s_Event.m_Kind),
            s_Event.m_Distance, Outcome(s_Decision)));

        if (s_Event.m_Lethal && IsLiveActor(s_Event.m_Actor)) {
            Log(std::format("{}: Treffer trotz Abschwächung tödlich.", s_Npc.m_Name));
            KillActor(s_Event.m_Actor, true);
            continue;
        }

        if (s_Decision.m_TryDisarm) {
            DisarmActor(s_Event.m_Actor, s_Event.m_Key, p_Settings, p_Now, false);
        }

        // Sollte am Treffer gestorben sein, lebt aber noch: das Spiel sterben lassen (mit Sterbe-Animation).
        if (s_Event.m_NeedsKill && IsLiveActor(s_Event.m_Actor)) {
            KillActor(s_Event.m_Actor, true);
        }

        if (s_Decision.m_Agony) {
            StartAgony(s_Npc, s_Decision.m_AgonySeconds, p_Now);
        }
    }
}

// Gnadenschuss auch dann erkennen, wenn das Spiel fuer Treffer auf Leichen keinen Treffer meldet: 47 hat geschossen
// und der Zielstrahl geht dicht am Koerper im Todeskampf vorbei.
void RealisticNPCs::CheckMercyShots(double p_Now) {
    const double s_Shot = m_LastPlayerShot.load();

    if (s_Shot <= m_LastShotChecked) {
        return;
    }

    m_LastShotChecked = s_Shot;

    for (auto& [s_Key, s_Dying] : m_Dying) {
        if (!s_Dying.m_Started || s_Dying.m_Agony.Finishing() || s_Dying.m_Agony.Done()) {
            continue;
        }

        const NpcInfo* s_Npc = m_World.Find(s_Key);

        if (!s_Npc) {
            continue;
        }

        const rn::Vec3 s_Body { s_Npc->m_Pos.x, s_Npc->m_Pos.y, s_Npc->m_Pos.z + 0.3f };

        if (rn::DistanceToRay(m_World.m_CameraPos, m_World.m_CameraForward, s_Body, 80.f) < c_MercyShotRadius) {
            s_Dying.m_Agony.FinishSoon(p_Now, m_Rng);
            Log(std::format("{}: Gnadenschuss – der Todeskampf ist gleich vorbei.", s_Dying.m_Name));
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Todeskampf
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::StartAgony(const NpcInfo& p_Npc, double p_Seconds, double p_Now) {
    if (m_Dying.contains(p_Npc.m_Key) || !IsKnownActor(p_Npc.m_Actor)) {
        return;
    }

    Dying s_Dying;
    s_Dying.m_Actor = p_Npc.m_Actor;
    s_Dying.m_Name = p_Npc.m_Name;
    s_Dying.m_HitTime = p_Now;
    s_Dying.m_WaitUntil = p_Now + c_DeathWaitSeconds;
    s_Dying.m_Seconds = p_Seconds;
    s_Dying.m_Seed = p_Npc.m_Key ^ static_cast<uint64_t>(p_Now * 1000.0);

    m_Dying.emplace(p_Npc.m_Key, std::move(s_Dying));
    m_Standoffs.Forget(p_Npc.m_Key);

    {
        std::scoped_lock s_Lock(m_EventMutex);
        m_DyingKeys.insert(p_Npc.m_Key);
    }
}

void RealisticNPCs::UpdateDying(const rn::Settings& p_Settings, double p_Now) {
    if (m_Dying.empty()) {
        if (p_Settings.m_DebugOverlay) {
            std::scoped_lock s_Lock(m_UiMutex);
            m_DyingViews.clear();
        }

        return;
    }

    const rn::AgonyConfig s_Config = rn::MakeAgonyConfig(p_Settings);
    std::vector<rn::AgonyAction> s_Actions;
    std::vector<uint64_t> s_Finished;

    for (auto& [s_Key, s_Dying] : m_Dying) {
        const NpcInfo* s_Npc = m_World.Find(s_Key);

        // Verschwunden (entladen oder ausser Reichweite) oder Todeskampf abgeschaltet: vergessen.
        if (!s_Npc || !IsKnownActor(s_Dying.m_Actor) || !p_Settings.m_Wounds) {
            s_Finished.push_back(s_Key);
            continue;
        }

        ZActor* s_Actor = s_Dying.m_Actor;

        // --- Erst muss das Spiel ihn haben sterben lassen ----------------------------------------------------------
        if (!s_Dying.m_Started) {
            if (s_Npc->m_Alive) {
                if (p_Now > s_Dying.m_WaitUntil) {
                    Log(std::format("{} hat den Treffer überlebt – kein Todeskampf.", s_Dying.m_Name));
                    s_Finished.push_back(s_Key);
                }

                continue;
            }

            // Die Zeitrechnung beginnt mit dem Treffer: so laeuft erst die Sterbe-Animation, dann der Todeskampf.
            s_Dying.m_Started = true;
            s_Dying.m_BodyFlags = BodyFlags(s_Actor);
            s_Dying.m_Agony.Begin(s_Dying.m_HitTime, s_Dying.m_Seconds, m_Rng);
            ++m_WoundsStarted;
            Log(std::format("{} bricht zusammen – Todeskampf {:.0f} s (Ragdoll {}).", s_Dying.m_Name, s_Dying.m_Seconds,
                s_Actor->m_pRagdollHandler ? "ja" : "nein"));
        }

        // --- Wird der Koerper gezogen, abgelegt oder versteckt, ist es vorbei ---------------------------------------
        if ((BodyFlags(s_Actor) & ~s_Dying.m_BodyFlags) != 0) {
            Log(std::format("{} wird weggebracht – Todeskampf beendet.", s_Dying.m_Name));
            s_Finished.push_back(s_Key);
            continue;
        }

        s_Actions.clear();
        s_Dying.m_Agony.Update(p_Now, s_Config, m_Rng, s_Actions);

        for (const rn::AgonyAction& s_Action : s_Actions) {
            switch (s_Action.m_Type) {
                case rn::AgonyActionType::Twitch:
                    if (!m_LimbBonesBroken || s_Action.m_Bone == rn::c_BonePelvis || s_Action.m_Bone == rn::c_BoneHead) {
                        ApplyImpulse(s_Actor, s_Action.m_Impulse, s_Action.m_Bone);
                    }

                    break;

                case rn::AgonyActionType::Moan:
                    // Nur, wenn 47 es hoeren kann.
                    if (s_Npc->m_Distance < 25.f) {
                        Say(s_Dying.m_Name, rn::lines::Moan(s_Action.m_Strength, s_Dying.m_Seed ^ static_cast<uint64_t>(p_Now * 13.0)), false, 4.f);
                    }

                    break;

                case rn::AgonyActionType::Blood:
                    if (p_Settings.m_Blood) {
                        // Erst die Blutlache unter dem Koerper, danach ab und zu frisches Blut.
                        if (!s_Dying.m_PoolSpawned) {
                            s_Dying.m_PoolSpawned = SpawnBlood(BloodKind::Pool, s_Npc->m_Pos);
                        }
                        else if (m_Rng.Chance(0.3)) {
                            SpawnBlood(BloodKind::Ground, s_Npc->m_Pos);
                        }
                    }

                    break;

                case rn::AgonyActionType::Die:
                    Log(std::format("{} ist tot – der Todeskampf dauerte {:.0f} s.", s_Dying.m_Name, p_Now - s_Dying.m_HitTime));
                    ++m_DeathsCaused;
                    break;
            }
        }

        if (s_Dying.m_Agony.Done()) {
            s_Finished.push_back(s_Key);
        }
    }

    if (!s_Finished.empty()) {
        std::scoped_lock s_Lock(m_EventMutex);

        for (const uint64_t s_Key : s_Finished) {
            m_DyingKeys.erase(s_Key);
            m_Dying.erase(s_Key);
        }
    }

    // Fuer die Diagnose-Anzeige.
    if (p_Settings.m_DebugOverlay) {
        std::vector<std::string> s_Views;

        for (const auto& [s_Key, s_Dying] : m_Dying) {
            if (!s_Dying.m_Started) {
                s_Views.push_back(std::format("{}: stirbt gerade", s_Dying.m_Name));
                continue;
            }

            s_Views.push_back(std::format("{}: noch {:.0f} s ({:.0f} %)", s_Dying.m_Name,
                std::max(0.0, s_Dying.m_Agony.DeathTime() - p_Now), s_Dying.m_Agony.Progress(p_Now) * 100.f));
        }

        std::scoped_lock s_Lock(m_UiMutex);
        m_DyingViews = std::move(s_Views);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Ruf: jeder getoetete Bewaffnete macht 47 gefuerchteter (wirkt auf "Drohen")
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::UpdateReputation(double p_Now) {
    const bool s_PlayerViolent = p_Now - m_LastPlayerAggression.load() < 10.0;

    for (const NpcInfo& s_Npc : m_World.m_Npcs) {
        if (!s_Npc.m_Armed) {
            continue;
        }

        auto [s_It, s_Inserted] = m_PreviousAlive.try_emplace(s_Npc.m_Key, s_Npc.m_Alive);

        if (!s_Inserted) {
            const bool s_WasAlive = s_It->second;
            s_It->second = s_Npc.m_Alive;

            if (s_WasAlive && !s_Npc.m_Alive && s_PlayerViolent && m_CountedKills.insert(s_Npc.m_Key).second) {
                ++m_ArmedKills;
                Log(std::format("Ruf: {} getötet – bisher {} Bewaffnete.", s_Npc.m_Name, m_ArmedKills.load()));
            }
        }
    }
}
