#pragma once

// Table des paramètres et conversions moteur <-> hôte.
// Volontairement sans dépendance à iPlug2 : l'aller-retour de chaque paramètre
// est vérifié par tests/dsp_smoke_test.cpp.

#include "MicroContact_DSP.h"
#include <cmath>
#include <algorithm>
#include <type_traits>

enum EParams
{
  // Macro-contrôles
  kParamSnap = 0, kParamBody, kParamErosion, kParamSpace,
  // Moteur d'excitation
  kParamExciter, kParamExcLevel, kParamVelSens,
  // Corps tonal
  kParamOsc1Shape, kParamOsc1Octave, kParamOsc1Level,
  kParamOsc2Shape, kParamOsc2Semi, kParamOsc2Detune, kParamOsc2Level,
  kParamPhaseReset, kParamPitchDrop, kParamDropTime,
  // Matrice de friction
  kParamMaterial, kParamCoupling, kParamResonance, kParamMatLevel,
  // Enveloppe d'amplitude
  kParamAttack, kParamDecay, kParamSustain, kParamRelease,
  // Sortie
  kParamGain,
  kNumParams
};

// Les presets de l'hôte sont exactement les cas d'usage du moteur
constexpr int kNumPresets = 3;
static_assert(kNumPresets == std::tuple_size<std::remove_cv_t<std::remove_reference_t<decltype(mc::UseCases())>>>::value,
              "kNumPresets doit correspondre à mc::UseCases()");

// Valeur affichée par l'hôte (%, ms, dB, demi-tons, index d'énumération)
inline double ToPluginValue(int idx, const mc::Params& p)
{
  switch (idx)
  {
    case kParamSnap:       return p.snap * 100.0;
    case kParamBody:       return p.body * 100.0;
    case kParamErosion:    return p.erosion * 100.0;
    case kParamSpace:      return p.space * 100.0;
    case kParamExciter:    return p.exciter;
    case kParamExcLevel:   return p.excLevel * 100.0;
    case kParamVelSens:    return p.velSens * 100.0;
    case kParamOsc1Shape:  return p.osc1Shape;
    case kParamOsc1Octave: return p.osc1Octave;
    case kParamOsc1Level:  return p.osc1Level * 100.0;
    case kParamOsc2Shape:  return p.osc2Shape;
    case kParamOsc2Semi:   return p.osc2Semi;
    case kParamOsc2Detune: return p.osc2Detune;
    case kParamOsc2Level:  return p.osc2Level * 100.0;
    case kParamPhaseReset: return p.phaseReset ? 1.0 : 0.0;
    case kParamPitchDrop:  return p.pitchDrop;
    case kParamDropTime:   return p.dropTimeMs;
    case kParamMaterial:   return p.material;
    case kParamCoupling:   return p.coupling * 100.0;
    case kParamResonance:  return p.resonance * 100.0;
    case kParamMatLevel:   return p.matLevel * 100.0;
    case kParamAttack:     return p.attackMs;
    case kParamDecay:      return p.decayMs;
    case kParamSustain:    return p.sustain * 100.0;
    case kParamRelease:    return p.releaseMs;
    case kParamGain:       return 20.0 * std::log10(std::max(1e-6, (double) p.gainLin));
    default:               return 0.0;
  }
}

inline void FromPluginValue(int idx, double v, mc::Params& p)
{
  const float f = static_cast<float>(v);
  const int i = static_cast<int>(std::lround(v));
  switch (idx)
  {
    case kParamSnap:       p.snap = f * 0.01f; break;
    case kParamBody:       p.body = f * 0.01f; break;
    case kParamErosion:    p.erosion = f * 0.01f; break;
    case kParamSpace:      p.space = f * 0.01f; break;
    case kParamExciter:    p.exciter = std::clamp(i, 0, mc::kNumExciters - 1); break;
    case kParamExcLevel:   p.excLevel = f * 0.01f; break;
    case kParamVelSens:    p.velSens = f * 0.01f; break;
    case kParamOsc1Shape:  p.osc1Shape = std::clamp(i, 0, mc::kNumShapes - 1); break;
    case kParamOsc1Octave: p.osc1Octave = i; break;
    case kParamOsc1Level:  p.osc1Level = f * 0.01f; break;
    case kParamOsc2Shape:  p.osc2Shape = std::clamp(i, 0, mc::kNumShapes - 1); break;
    case kParamOsc2Semi:   p.osc2Semi = i; break;
    case kParamOsc2Detune: p.osc2Detune = f; break;
    case kParamOsc2Level:  p.osc2Level = f * 0.01f; break;
    case kParamPhaseReset: p.phaseReset = v >= 0.5; break;
    case kParamPitchDrop:  p.pitchDrop = f; break;
    case kParamDropTime:   p.dropTimeMs = f; break;
    case kParamMaterial:   p.material = std::clamp(i, 0, mc::kNumMaterials - 1); break;
    case kParamCoupling:   p.coupling = f * 0.01f; break;
    case kParamResonance:  p.resonance = f * 0.01f; break;
    case kParamMatLevel:   p.matLevel = f * 0.01f; break;
    case kParamAttack:     p.attackMs = f; break;
    case kParamDecay:      p.decayMs = f; break;
    case kParamSustain:    p.sustain = f * 0.01f; break;
    case kParamRelease:    p.releaseMs = f; break;
    case kParamGain:       p.gainLin = static_cast<float>(std::pow(10.0, v / 20.0)); break;
    default: break;
  }
}
