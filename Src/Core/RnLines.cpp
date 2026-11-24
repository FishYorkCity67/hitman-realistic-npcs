#include "RnLines.h"

#include <iterator>

namespace rn::lines {
    namespace {
        template <size_t N>
        const char* Pick(const char* const (&p_Lines)[N], uint64_t p_Seed) {
            // Seed durchmischen, damit benachbarte Zahlen nicht immer dieselbe Variante treffen.
            p_Seed ^= p_Seed >> 33;
            p_Seed *= 0xff51afd7ed558ccdull;
            p_Seed ^= p_Seed >> 33;
            return p_Lines[p_Seed % N];
        }
    }

    const char* FirstWarning(uint64_t p_Seed) {
        static const char* const c_Lines[] = {
            "Keine Bewegung! Hände hoch, sofort!",
            "Stehen bleiben! Waffe runter!",
            "Halt! Eine falsche Bewegung und ich schieße!",
            "Hände hoch! Ganz langsam!",
            "Stopp! Ich will deine Hände sehen!",
            "Keinen Schritt weiter! Hände, wo ich sie sehen kann!",
        };
        return Pick(c_Lines, p_Seed);
    }

    const char* LastWarning(uint64_t p_Seed) {
        static const char* const c_Lines[] = {
            "Letzte Warnung!",
            "Ich zähle bis drei! Eins…",
            "Ich mein's ernst – letzte Chance!",
            "Noch ein Wort und du bist tot!",
        };
        return Pick(c_Lines, p_Seed);
    }

    const char* OpenFire(uint64_t p_Seed) {
        static const char* const c_Lines[] = {
            "Feuer!",
            "Du hattest deine Chance!",
            "Schießt ihn nieder!",
            "Das war's!",
        };
        return Pick(c_Lines, p_Seed);
    }

    const char* Calmed(uint64_t p_Seed) {
        static const char* const c_Lines[] = {
            "Du schon wieder?! Hände hoch!",
            "Ich hab dir gesagt, du sollst verschwinden!",
            "Jetzt reicht's! Keine Bewegung!",
        };
        return Pick(c_Lines, p_Seed);
    }

    const char* PleaLabel(Plea p_Plea) {
        switch (p_Plea) {
            case Plea::Surrender: return "Ergeben – Hände hoch";
            case Plea::Talk: return "Reden – Missverständnis erklären";
            case Plea::Bribe: return "Bestechen";
            case Plea::Threaten: return "Drohen";
            default: return "";
        }
    }

    const char* PleaSpoken(Plea p_Plea, uint64_t p_Seed) {
        switch (p_Plea) {
            case Plea::Surrender: {
                static const char* const c_Lines[] = {
                    "Okay, okay. Ich gebe auf. Keine Waffe, sehen Sie?",
                    "Ganz ruhig. Meine Hände sind oben.",
                    "Nicht schießen. Ich mache keinen Ärger.",
                };
                return Pick(c_Lines, p_Seed);
            }
            case Plea::Talk: {
                static const char* const c_Lines[] = {
                    "Ganz ruhig. Das ist ein Missverständnis – ich arbeite hier.",
                    "Immer mit der Ruhe. Ich habe mich nur verlaufen.",
                    "Hören Sie, ich wurde hergeschickt. Fragen Sie Ihren Vorgesetzten.",
                };
                return Pick(c_Lines, p_Seed);
            }
            case Plea::Bribe: {
                static const char* const c_Lines[] = {
                    "Wie viel kostet es, dass Sie mich nie gesehen haben?",
                    "Zehntausend in bar. Und Sie haben heute nichts bemerkt.",
                    "Sie sehen aus wie jemand, der unterbezahlt ist. Lassen Sie uns das regeln.",
                };
                return Pick(c_Lines, p_Seed);
            }
            case Plea::Threaten: {
                static const char* const c_Lines[] = {
                    "Waffe runter. Oder Sie sind der Nächste.",
                    "Sie haben gesehen, was mit den anderen passiert ist. Legen Sie sie hin.",
                    "Ich treffe, bevor Sie abdrücken. Wollen Sie es ausprobieren?",
                };
                return Pick(c_Lines, p_Seed);
            }
            default:
                return "";
        }
    }

    const char* Reply(Plea p_Plea, PleaResult p_Result, uint64_t p_Seed) {
        switch (p_Result) {
            case PleaResult::HolsterFirst: {
                static const char* const c_Lines[] = {
                    "Erst die Waffe weg! Sofort!",
                    "Mit der Waffe in der Hand? Leg sie weg!",
                };
                return Pick(c_Lines, p_Seed);
            }
            case PleaResult::GuardSurrenders: {
                static const char* const c_Lines[] = {
                    "Okay! Okay… ich lege sie hin. Bitte nicht schießen!",
                    "Schon gut, schon gut! Ich bin raus!",
                    "Ich hab Familie, Mann… hier, nimm sie!",
                };
                return Pick(c_Lines, p_Seed);
            }
            case PleaResult::OpenFire: {
                static const char* const c_Lines[] = {
                    "Netter Versuch. Feuer!",
                    "Du drohst MIR? Schießt!",
                    "Falsche Antwort!",
                };
                return Pick(c_Lines, p_Seed);
            }
            case PleaResult::StandDown:
                switch (p_Plea) {
                    case Plea::Surrender: {
                        static const char* const c_Lines[] = {
                            "Gut so. Und jetzt verschwinde – ich will dich hier nie wieder sehen.",
                            "Okay… ganz langsam. Raus hier, bevor ich es mir anders überlege.",
                        };
                        return Pick(c_Lines, p_Seed);
                    }
                    case Plea::Talk: {
                        static const char* const c_Lines[] = {
                            "Hm. Na gut. Aber ich behalte dich im Auge.",
                            "Verlaufen, ja? Dann sieh zu, dass du weiterkommst.",
                            "Schon gut. Ich hab nichts gesehen.",
                        };
                        return Pick(c_Lines, p_Seed);
                    }
                    case Plea::Bribe: {
                        static const char* const c_Lines[] = {
                            "…Ich hab dich nie gesehen. Und jetzt geh.",
                            "Das bleibt unter uns. Verschwinde.",
                            "Für das Geld hab ich Mittagspause. Geh schon.",
                        };
                        return Pick(c_Lines, p_Seed);
                    }
                    default:
                        return "Na gut. Verschwinde.";
                }
            case PleaResult::LastWarning:
            default:
                switch (p_Plea) {
                    case Plea::Bribe: {
                        static const char* const c_Lines[] = {
                            "Willst du mich bestechen?! Letzte Warnung!",
                            "Steck dein Geld weg! Hände hoch!",
                        };
                        return Pick(c_Lines, p_Seed);
                    }
                    case Plea::Talk: {
                        static const char* const c_Lines[] = {
                            "Erzähl das jemand anderem! Hände hoch!",
                            "Spar dir die Geschichten! Letzte Warnung!",
                            "Ich kenne hier jeden – und dich nicht!",
                        };
                        return Pick(c_Lines, p_Seed);
                    }
                    default:
                        return "Keine Bewegung, hab ich gesagt!";
                }
        }
    }

    const char* Disarmed(uint64_t p_Seed) {
        static const char* const c_Lines[] = {
            "Aaah! Meine Hand!",
            "Nicht schießen! Ich bin unbewaffnet!",
            "Bitte… ich ergebe mich!",
            "Okay, okay! Ich hab nichts mehr!",
        };
        return Pick(c_Lines, p_Seed);
    }

    const char* Moan(float p_Progress, uint64_t p_Seed) {
        if (p_Progress < 0.35f) {
            static const char* const c_Lines[] = {
                "Aaah! Ich bin getroffen!",
                "Hilfe! Ich blute!",
                "Nein… nein… nicht schießen!",
                "Holt einen Arzt! Schnell!",
                "Oh Gott… das ganze Blut…",
            };
            return Pick(c_Lines, p_Seed);
        }

        if (p_Progress < 0.75f) {
            static const char* const c_Lines[] = {
                "Bitte… ich will nicht sterben…",
                "Ich… krieg keine Luft…",
                "Mir ist so kalt…",
                "Hilfe… bitte…",
                "Ich hab doch Kinder…",
            };
            return Pick(c_Lines, p_Seed);
        }

        static const char* const c_Lines[] = {
            "…",
            "Mama…",
            "…so… kalt…",
            "*röchelt*",
        };
        return Pick(c_Lines, p_Seed);
    }
}
