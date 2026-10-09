// MJ7 Master Chain - liste unique des parametres, des modules et des styles d'usine.
#pragma once
#include "dsp/MasterAnalyzer.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace mj7
{
inline juce::String U8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

// X (identifiant, nom, type, min, max, defaut, centre de course (0 = lineaire), unite, choix)
// Les identifiants ne doivent JAMAIS changer : les projets FL Studio et l'automation en dependent.
#define MJ7_PARAMS(X) \
    X (in_gain,       "Gain entrée",        tF, -18, 18, 0, 0, "dB", "") \
    X (intensity,     "Intensité",          tF, 0, 100, 100, 0, "%", "") \
    X (target,        "Cible",              tC, 0, 5, 0, 0, "", "Streaming -14 LUFS|Apple Music -16 LUFS|Radio -12 LUFS|Fort -10 LUFS|Club -8 LUFS|TV et pub -23 LUFS") \
    X (eq_on,         "EQ actif",           tB, 0, 1, 1, 0, "", "") \
    X (eq_lowcut,     "Coupe-sub",          tF, 10, 60, 25, 0, "Hz", "") \
    X (eq_low_f,      "Basses fréq",        tF, 40, 300, 100, 120, "Hz", "") \
    X (eq_low_g,      "Basses gain",        tF, -6, 6, 0, 0, "dB", "") \
    X (eq_mud_f,      "Bas-médium fréq",    tF, 150, 800, 250, 350, "Hz", "") \
    X (eq_mud_g,      "Bas-médium gain",    tF, -6, 3, 0, 0, "dB", "") \
    X (eq_mud_q,      "Bas-médium Q",       tF, 0.5, 4, 1, 0, "", "") \
    X (eq_pres_f,     "Présence fréq",      tF, 1500, 6000, 3000, 0, "Hz", "") \
    X (eq_pres_g,     "Présence gain",      tF, -6, 6, 0, 0, "dB", "") \
    X (eq_air_f,      "Air fréq",           tF, 6000, 16000, 10000, 0, "Hz", "") \
    X (eq_air_g,      "Air gain",           tF, -6, 6, 0, 0, "dB", "") \
    X (mb_on,         "Multibande actif",   tB, 0, 1, 1, 0, "", "") \
    X (mb_xlow,       "Séparation grave",   tF, 60, 400, 150, 150, "Hz", "") \
    X (mb_xhigh,      "Séparation aigu",    tF, 2000, 8000, 4000, 0, "Hz", "") \
    X (mb_low_thr,    "Graves seuil",       tF, -40, 0, -20, 0, "dB", "") \
    X (mb_low_ratio,  "Graves ratio",       tF, 1, 6, 2, 0, ":1", "") \
    X (mb_low_gain,   "Graves gain",        tF, -6, 6, 0, 0, "dB", "") \
    X (mb_mid_thr,    "Médiums seuil",      tF, -40, 0, -20, 0, "dB", "") \
    X (mb_mid_ratio,  "Médiums ratio",      tF, 1, 6, 2, 0, ":1", "") \
    X (mb_mid_gain,   "Médiums gain",       tF, -6, 6, 0, 0, "dB", "") \
    X (mb_high_thr,   "Aigus seuil",        tF, -40, 0, -20, 0, "dB", "") \
    X (mb_high_ratio, "Aigus ratio",        tF, 1, 6, 2, 0, ":1", "") \
    X (mb_high_gain,  "Aigus gain",         tF, -6, 6, 0, 0, "dB", "") \
    X (glue_on,       "Glue actif",         tB, 0, 1, 1, 0, "", "") \
    X (glue_thr,      "Glue seuil",         tF, -40, 0, -18, 0, "dB", "") \
    X (glue_ratio,    "Glue ratio",         tF, 1.5, 10, 2, 4, ":1", "") \
    X (glue_attack,   "Glue attaque",       tF, 1, 50, 10, 0, "ms", "") \
    X (glue_release,  "Glue release",       tF, 50, 800, 150, 200, "ms", "") \
    X (glue_makeup,   "Glue gain",          tF, 0, 12, 0, 0, "dB", "") \
    X (glue_mix,      "Glue mix",           tF, 0, 100, 100, 0, "%", "") \
    X (sat_on,        "Saturation active",  tB, 0, 1, 1, 0, "", "") \
    X (sat_mode,      "Saturation type",    tC, 0, 2, 1, 0, "", "Lampe|Bande|Clipper") \
    X (sat_drive,     "Saturation drive",   tF, 0, 18, 3, 0, "dB", "") \
    X (sat_mix,       "Saturation mix",     tF, 0, 100, 50, 0, "%", "") \
    X (st_on,         "Stéréo actif",       tB, 0, 1, 1, 0, "", "") \
    X (st_width,      "Largeur",            tF, 0, 200, 100, 0, "%", "") \
    X (st_mono,       "Basses mono sous",   tF, 20, 300, 100, 100, "Hz", "") \
    X (lim_on,        "Limiteur actif",     tB, 0, 1, 1, 0, "", "") \
    X (lim_gain,      "Limiteur gain",      tF, -12, 24, 0, 0, "dB", "") \
    X (lim_ceiling,   "Plafond",            tF, -3, 0, -1, 0, "dBTP", "") \
    X (lim_release,   "Limiteur release",   tF, 10, 1000, 120, 150, "ms", "") \
    X (match,         "Bypass à niveau égal", tB, 0, 1, 1, 0, "", "") \
    X (bypass,        "Bypass",             tB, 0, 1, 0, 0, "", "")

enum PID
{
#define X(id, name, type, mn, mx, def, centre, unit, choices) id,
    MJ7_PARAMS (X)
#undef X
    kNumParams
};

enum class PType { tF, tI, tB, tC };
struct ParamDef { const char* id; const char* name; PType type; float min, max, def, centre; const char* unit; const char* choices; };

inline const ParamDef& paramDef (int pid)
{
    static const ParamDef defs[] = {
#define X(id, name, type, mn, mx, def, centre, unit, choices) { #id, name, PType::type, (float) (mn), (float) (mx), (float) (def), (float) (centre), unit, choices },
        MJ7_PARAMS (X)
#undef X
    };
    return defs[pid];
}

inline float targetLufs (int index)
{
    static const float t[6] = { -14.0f, -16.0f, -12.0f, -10.0f, -8.0f, -23.0f };
    return t[std::clamp (index, 0, 5)];
}

// ----------------------------------------------------------------------------------------------
enum Family { Correction = 0, Dynamique, Couleur, Espace, Neutre };

struct ModuleDef { const char* name; const char* title; const char* help; Family family; int bypass; int meter; std::vector<int> params; };

enum Meter { mLow = 0, mMid, mHigh, mGlue, mLimit, kNumMeters };

inline const std::vector<ModuleDef>& modules()
{
    static const std::vector<ModuleDef> m = {
        { "Entrée", "Entrée", "Niveau envoyé dans la chaîne. Visez un mix qui culmine vers -6 dBFS.", Neutre, -1, -1, { in_gain } },
        { "EQ", "Égaliseur de mastering", "Corrections larges et douces : coupe-sub, basses, bas-médium, présence et air.", Correction, eq_on, -1,
          { eq_lowcut, eq_low_f, eq_low_g, eq_mud_f, eq_mud_g, eq_mud_q, eq_pres_f, eq_pres_g, eq_air_f, eq_air_g } },
        { "Multibande", "Compresseur 3 bandes", "Tient séparément les graves, les médiums et les aigus. Visez 1 à 3 dB de réduction par bande.", Dynamique, mb_on, mLow,
          { mb_xlow, mb_xhigh, mb_low_thr, mb_low_ratio, mb_low_gain, mb_mid_thr, mb_mid_ratio, mb_mid_gain, mb_high_thr, mb_high_ratio, mb_high_gain } },
        { "Glue", "Compresseur de bus (glue)", "Soude les éléments du mix entre eux. 1 à 2 dB de réduction suffisent.", Dynamique, glue_on, mGlue,
          { glue_thr, glue_ratio, glue_attack, glue_release, glue_makeup, glue_mix } },
        { "Saturation", "Saturation", "Chaleur et densité. Le Clipper rabote les crêtes avant le limiteur pour gagner du volume.", Couleur, sat_on, -1,
          { sat_mode, sat_drive, sat_mix } },
        { "Stéréo", "Image stéréo", "Largeur de l'image et basses en mono, pour un bas du spectre solide sur tous les systèmes.", Espace, st_on, -1,
          { st_width, st_mono } },
        { "Limiteur", "Limiteur true peak", "Monte le volume jusqu'à la cible sans dépasser le plafond, crêtes inter-échantillons comprises.", Neutre, lim_on, mLimit,
          { lim_gain, lim_ceiling, lim_release } },
    };
    return m;
}

// ----------------------------------------------------------------------------------------------
struct FactoryPreset { const char* name; GenreTarget target; std::vector<std::pair<int, float>> values; };

inline const std::vector<FactoryPreset>& factoryPresets()
{
    //                                       sub   basses bas-med presence  air
    static const std::vector<FactoryPreset> p = {
        { "Afrobeat / Coupé-décalé", { 7.0f, 10.0f, 5.0f, -6.0f, -14.0f },
          { { mb_low_ratio, 2.5f }, { sat_mode, 1 }, { sat_drive, 3 }, { st_width, 110 }, { st_mono, 110 } } },
        { "Rap / Trap", { 12.0f, 10.0f, 4.0f, -7.0f, -14.0f },
          { { mb_low_ratio, 3.0f }, { sat_mode, 2 }, { sat_drive, 2 }, { sat_mix, 40 }, { lim_release, 80 }, { st_mono, 120 } } },
        { "R&B / Pop", { 8.0f, 8.0f, 4.0f, -6.0f, -13.0f },
          { { sat_mode, 0 }, { sat_drive, 2 }, { st_width, 115 }, { glue_ratio, 2 } } },
        { "Gospel / Live", { 6.0f, 8.0f, 5.0f, -6.0f, -14.0f },
          { { glue_release, 250 }, { glue_attack, 20 }, { mb_low_ratio, 1.8f }, { st_width, 110 }, { lim_release, 200 } } },
        { "Cinématique / Pub", { 9.0f, 8.0f, 5.0f, -8.0f, -15.0f },
          { { mb_low_ratio, 1.6f }, { mb_mid_ratio, 1.6f }, { mb_high_ratio, 1.6f }, { glue_ratio, 1.6f }, { sat_drive, 1.5f }, { st_width, 120 }, { lim_release, 250 } } },
        { "Neutre (point de départ)", { 8.0f, 9.0f, 5.0f, -7.0f, -15.0f }, {} },
    };
    return p;
}
} // namespace mj7
