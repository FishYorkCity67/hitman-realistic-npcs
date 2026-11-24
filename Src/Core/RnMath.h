#pragma once

#include <cstdint>

// Kleine Mathe-Helfer ohne Spiel-Abhaengigkeiten (auch von den Tests genutzt).
namespace rn {
    struct Vec3 {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    float Distance(const Vec3& a, const Vec3& b);
    float Distance2D(const Vec3& a, const Vec3& b);

    enum class BodyZone { Unknown = 0, Head, Torso, Arm, Leg };

    const char* ZoneName(BodyZone p_Zone);

    // Schaetzt, wo ein Schuss getroffen hat: der Punkt des Zielstrahls (Kamera), der der senkrechten Koerperachse
    // des Opfers am naechsten kommt. Funktioniert nur fuer stehende NPCs (Fuesse = Position des NPCs).
    BodyZone EstimateHitZone(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_VictimFeet);

    // Abstand eines Punkts vom Zielstrahl. Liegt der Punkt hinter dem Ursprung oder weiter als p_MaxRange: 1e9.
    float DistanceToRay(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_Point, float p_MaxRange);

    // Kleiner, reproduzierbarer Zufallsgenerator (xorshift64*), damit die Tests deterministisch sind.
    class Rng {
    public:
        explicit Rng(uint64_t p_Seed = 0x9E3779B97F4A7C15ull);

        uint64_t Next();
        double Uniform(double p_Min, double p_Max);
        float UniformF(float p_Min, float p_Max);
        bool Chance(double p_Probability);
        int Int(int p_Min, int p_MaxInclusive);
        float Sign();

    private:
        uint64_t m_State;
    };
}
