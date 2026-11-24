#include "RnMath.h"

#include <algorithm>
#include <cmath>

namespace rn {
    float Distance(const Vec3& a, const Vec3& b) {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        const float z = a.z - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }

    float Distance2D(const Vec3& a, const Vec3& b) {
        const float x = a.x - b.x;
        const float y = a.y - b.y;
        return std::sqrt(x * x + y * y);
    }

    const char* ZoneName(BodyZone p_Zone) {
        switch (p_Zone) {
            case BodyZone::Head: return "Kopf";
            case BodyZone::Torso: return "Oberkörper";
            case BodyZone::Arm: return "Arm";
            case BodyZone::Leg: return "Bein";
            default: return "unbekannt";
        }
    }

    BodyZone EstimateHitZone(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_VictimFeet) {
        const float s_DirXY = p_RayDir.x * p_RayDir.x + p_RayDir.y * p_RayDir.y;

        if (s_DirXY < 1e-6f) {
            return BodyZone::Unknown;
        }

        // Strahlparameter, an dem der Strahl (in der Ebene) der Koerperachse am naechsten ist.
        const float s_T = ((p_VictimFeet.x - p_RayOrigin.x) * p_RayDir.x + (p_VictimFeet.y - p_RayOrigin.y) * p_RayDir.y) / s_DirXY;

        if (s_T < 0.f) {
            return BodyZone::Unknown;
        }

        const float s_X = p_RayOrigin.x + p_RayDir.x * s_T;
        const float s_Y = p_RayOrigin.y + p_RayDir.y * s_T;
        const float s_Z = p_RayOrigin.z + p_RayDir.z * s_T;
        const float s_Height = s_Z - p_VictimFeet.z;
        const float s_Side = std::sqrt((s_X - p_VictimFeet.x) * (s_X - p_VictimFeet.x) + (s_Y - p_VictimFeet.y) * (s_Y - p_VictimFeet.y));

        if (s_Height < -0.2f || s_Height > 2.2f || s_Side > 0.9f) {
            return BodyZone::Unknown;
        }

        if (s_Height >= 1.47f) {
            return BodyZone::Head;
        }

        if (s_Height < 0.95f) {
            return BodyZone::Leg;
        }

        return s_Side > 0.22f ? BodyZone::Arm : BodyZone::Torso;
    }

    float DistanceToRay(const Vec3& p_RayOrigin, const Vec3& p_RayDir, const Vec3& p_Point, float p_MaxRange) {
        const float s_Length = std::sqrt(p_RayDir.x * p_RayDir.x + p_RayDir.y * p_RayDir.y + p_RayDir.z * p_RayDir.z);

        if (s_Length < 1e-4f) {
            return 1e9f;
        }

        const Vec3 s_Dir { p_RayDir.x / s_Length, p_RayDir.y / s_Length, p_RayDir.z / s_Length };
        const Vec3 s_To { p_Point.x - p_RayOrigin.x, p_Point.y - p_RayOrigin.y, p_Point.z - p_RayOrigin.z };
        const float s_T = s_To.x * s_Dir.x + s_To.y * s_Dir.y + s_To.z * s_Dir.z;

        if (s_T < 0.f || s_T > p_MaxRange) {
            return 1e9f;
        }

        const Vec3 s_Closest { p_RayOrigin.x + s_Dir.x * s_T, p_RayOrigin.y + s_Dir.y * s_T, p_RayOrigin.z + s_Dir.z * s_T };
        return Distance(s_Closest, p_Point);
    }

    Rng::Rng(uint64_t p_Seed) : m_State(p_Seed ? p_Seed : 0x9E3779B97F4A7C15ull) {}

    uint64_t Rng::Next() {
        m_State ^= m_State >> 12;
        m_State ^= m_State << 25;
        m_State ^= m_State >> 27;
        return m_State * 0x2545F4914F6CDD1Dull;
    }

    double Rng::Uniform(double p_Min, double p_Max) {
        if (p_Max <= p_Min) {
            return p_Min;
        }

        const double s_Unit = static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0);
        return p_Min + (p_Max - p_Min) * s_Unit;
    }

    float Rng::UniformF(float p_Min, float p_Max) {
        return static_cast<float>(Uniform(p_Min, p_Max));
    }

    bool Rng::Chance(double p_Probability) {
        return Uniform(0.0, 1.0) < p_Probability;
    }

    int Rng::Int(int p_Min, int p_MaxInclusive) {
        if (p_MaxInclusive <= p_Min) {
            return p_Min;
        }

        const auto s_Span = static_cast<uint64_t>(p_MaxInclusive - p_Min) + 1;
        return p_Min + static_cast<int>(Next() % s_Span);
    }

    float Rng::Sign() {
        return (Next() & 1) ? 1.f : -1.f;
    }
}
