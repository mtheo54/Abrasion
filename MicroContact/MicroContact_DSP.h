#pragma once

// Micro-Contact (Abrasion) : moteur Transient-to-Resonance.
//
// En-tête autonome, sans dépendance à iPlug2 : il se compile et se teste seul
// (voir tests/dsp_smoke_test.cpp).
//
// Règles temps réel respectées :
//   - aucune allocation hors Prepare() ;
//   - aucun verrou ;
//   - paramètres copiés une fois par bloc (SetParams), jamais lus pendant que l'UI écrit ;
//   - coefficients recalculés à cadence de contrôle (32 échantillons), incréments
//     d'oscillateurs interpolés entre deux mises à jour pour éviter le zipper.
//
// Chaîne par voix :
//   Excitation procédurale ──┬──────────────────────────► HP dynamique ─► bus surface (stéréo)
//                            └─► Matrice modale ─► HP 24 dB/oct @ ≥100 Hz ─► bus surface
//   OSC1 (sub) + OSC2 ─► enveloppe ─► bus sub (mono, jamais réverbéré)
//                    └─► couplage non linéaire ─► HP 120 Hz ─► Matrice modale
// Bus surface ─► micro-cavité (Space Damping) ; somme finale ─► DC blocker ─► limiteur doux.

#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  #include <xmmintrin.h>
  #define MC_DENORMAL_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
  #define MC_DENORMAL_ARM64 1
#endif

namespace mc {

// Active flush-to-zero / denormals-are-zero pendant le rendu, puis restaure l'état
// du thread de l'hôte. Les queues de filtres qui décroissent vers zéro passent sinon
// en nombres dénormaux : mesuré ici, ils doublaient la charge CPU.
class DenormalGuard
{
public:
  DenormalGuard()
  {
#if defined(MC_DENORMAL_X86)
    mSaved = _mm_getcsr();
    _mm_setcsr(mSaved | 0x8040u); // FTZ (bit 15) + DAZ (bit 6)
#elif defined(MC_DENORMAL_ARM64) && !defined(_MSC_VER)
    asm volatile("mrs %0, fpcr" : "=r"(mSaved));
    const uint64_t fz = mSaved | (1ull << 24);
    asm volatile("msr fpcr, %0" : : "r"(fz));
#endif
  }
  ~DenormalGuard()
  {
#if defined(MC_DENORMAL_X86)
    _mm_setcsr(mSaved);
#elif defined(MC_DENORMAL_ARM64) && !defined(_MSC_VER)
    asm volatile("msr fpcr, %0" : : "r"(mSaved));
#endif
  }
  DenormalGuard(const DenormalGuard&) = delete;
  DenormalGuard& operator=(const DenormalGuard&) = delete;

private:
#if defined(MC_DENORMAL_X86)
  unsigned int mSaved = 0;
#else
  uint64_t mSaved = 0;
#endif
};

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr int kMaxVoices = 8;
constexpr int kNumModes = 8;
constexpr int kControlInterval = 32;
constexpr float kSurfaceFloorHz = 110.f;   // rien du bus « bruit/matière » sous ~100 Hz
constexpr float kModeGain = 0.0035f;       // calibré par tests/dsp_smoke_test.cpp
constexpr float kExciterGain = 0.9f;

enum EExciter  { kExArc = 0, kExRoche, kExVerre, kExClic, kNumExciters };
enum EShape    { kShapeSine = 0, kShapeTriSat, kShapeWavetable, kNumShapes };
enum EMaterial { kMatMetal = 0, kMatQuartz, kMatMembrane, kMatMercure, kNumMaterials };

inline float Clampf(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float SemisToRatio(float s) { return std::exp2(s / 12.f); }

// tanh rationnelle (Padé), bornée : erreur < 2,4 %, dérivée nulle en ±3, ~6× plus rapide.
// Réservée à la coloration ; le limiteur de sortie garde std::tanh.
inline float FastTanh(float x)
{
  x = Clampf(x, -3.f, 3.f);
  const float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

struct Rng
{
  uint32_t s = 0x9E3779B9u;
  void Seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
  inline uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  inline float Uni() { return (Next() >> 8) * (1.f / 16777216.f); } // [0, 1)
  inline float Bi() { return Uni() * 2.f - 1.f; }                     // [-1, 1)
};

// Filtre à variables d'état TPT (Cytomic) : stable même modulé à chaque mise à jour.
struct Svf
{
  float ic1 = 0.f, ic2 = 0.f, a1 = 1.f, a2 = 0.f, a3 = 0.f, k = 1.414f;

  void Set(float fc, float Q, float sr)
  {
    fc = Clampf(fc, 10.f, 0.49f * sr);
    const float g = std::tan(kPi * fc / sr);
    k = 1.f / Q;
    a1 = 1.f / (1.f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
  }
  inline float HighPass(float v0)
  {
    const float v3 = v0 - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    return v0 - k * v1 - v2;
  }
  void Clear() { ic1 = ic2 = 0.f; }
};

// Résonateur modal à deux pôles, normalisé : une impulsion unité donne une
// réponse d'amplitude crête ≈ 1, quelle que soit la fréquence.
struct Mode
{
  float a1 = 0.f, a2 = 0.f, b = 0.f, y1 = 0.f, y2 = 0.f;

  void Set(float f, float t60, float sr)
  {
    f = Clampf(f, 20.f, 0.45f * sr);
    const float w = kTwoPi * f / sr;
    const float r = std::exp(-6.9078f / (std::max(t60, 0.002f) * sr));
    a1 = 2.f * r * std::cos(w);
    a2 = -r * r;
    b = std::sin(w);
  }
  inline float Process(float x)
  {
    const float y = a1 * y1 + a2 * y2 + b * x;
    y2 = y1;
    y1 = y;
    return y;
  }
  void Clear() { y1 = y2 = 0.f; }
};

struct MaterialSpec
{
  float ratio[kNumModes];
  float gain[kNumModes];
  float decay[kNumModes];  // échelle de T60 par mode (les aigus s'éteignent plus vite)
  float t60Scale;
  float wobble;            // ondulation relative des fréquences (mercure)
};

static const MaterialSpec kMaterials[kNumMaterials] = {
  // Métal : barre libre, longs sustains
  {{1.f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.82f, 31.87f},
   {1.f, .75f, .6f, .5f, .4f, .33f, .27f, .22f},
   {1.f, .85f, .72f, .62f, .52f, .45f, .38f, .32f}, 1.f, 0.f},
  // Quartz : spectre clairsemé et brillant, très peu amorti
  {{1.f, 3.14f, 4.69f, 6.28f, 9.42f, 12.07f, 15.71f, 19.32f},
   {.45f, .7f, .85f, .8f, .7f, .6f, .5f, .4f},
   {1.f, 1.f, .95f, .92f, .88f, .84f, .8f, .75f}, 1.4f, 0.f},
  // Membrane : modes de Bessel, amortissement rapide
  {{1.f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f},
   {1.f, .8f, .65f, .6f, .5f, .45f, .4f, .35f},
   {1.f, .7f, .55f, .5f, .45f, .4f, .36f, .32f}, .3f, 0.f},
  // Mercure : modes liquides, faible Q, fréquences qui ondulent
  {{1.f, 1.5f, 2.02f, 2.51f, 3.03f, 3.55f, 4.1f, 4.6f},
   {1.f, .6f, .45f, .36f, .3f, .25f, .21f, .18f},
   {1.f, .8f, .65f, .55f, .48f, .42f, .37f, .33f}, .45f, .018f},
};

// Tables d'ondes à niveaux de détail (mipmaps) limitées en bande : chaque niveau
// divise par deux le nombre d'harmoniques. Toutes les formes valent 0 en phase 0,
// ce qui rend le redémarrage de phase équivalent à un départ au passage par zéro.
class WaveTables
{
public:
  static constexpr int kSize = 2048;
  static constexpr int kMaxHarmonics = 512;
  static constexpr int kLevels = 10; // 512, 256, … , 1 harmoniques

  static const WaveTables& Get()
  {
    static const WaveTables instance; // construit une seule fois, partagé entre instances du plugin
    return instance;
  }

  static int LevelFor(float freq, float sr)
  {
    const float allowed = 0.45f * sr / std::max(freq, 1.f);
    int level = 0, h = kMaxHarmonics;
    while (h > allowed && level < kLevels - 1) { h >>= 1; ++level; }
    return level;
  }

  inline float Read(int shape, int level, float phase) const
  {
    const float* t = &mData[((size_t) shape * kLevels + level) * (kSize + 1)];
    const float idx = phase * kSize;
    const int i0 = (int) idx;
    const float fr = idx - (float) i0;
    return t[i0] + fr * (t[i0 + 1] - t[i0]);
  }

private:
  WaveTables()
  {
    mData.assign((size_t) kNumShapes * kLevels * (kSize + 1), 0.f);

    std::vector<float> cosT(kSize), sinT(kSize);
    for (int j = 0; j < kSize; ++j)
    {
      cosT[j] = std::cos(kTwoPi * j / kSize);
      sinT[j] = std::sin(kTwoPi * j / kSize);
    }

    for (int shape = 0; shape < kNumShapes; ++shape)
    {
      std::vector<float> a(kMaxHarmonics + 1, 0.f), b(kMaxHarmonics + 1, 0.f);

      if (shape == kShapeSine)
      {
        b[1] = 1.f;
      }
      else if (shape == kShapeTriSat)
      {
        // Triangle saturé, analysé par DFT puis resynthétisé sans repliement
        std::vector<float> s(kSize);
        const float drive = 2.5f, norm = std::tanh(drive);
        for (int n = 0; n < kSize; ++n)
        {
          const float t = (float) n / kSize;
          const float tri = t < 0.25f ? 4.f * t : (t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f);
          s[n] = std::tanh(drive * tri) / norm;
        }
        for (int k = 1; k <= kMaxHarmonics; ++k)
        {
          double ak = 0.0, bk = 0.0;
          for (int n = 0; n < kSize; ++n)
          {
            const int j = (k * n) & (kSize - 1);
            ak += s[n] * cosT[j];
            bk += s[n] * sinT[j];
          }
          a[k] = (float) (2.0 * ak / kSize);
          b[k] = (float) (2.0 * bk / kSize);
        }
      }
      else
      {
        // Table harmonique : spectre d'orgue doux, 3e harmoniques atténuées
        for (int k = 1; k <= 32; ++k)
          b[k] = std::pow((float) k, -1.1f) * (k % 3 == 0 ? 0.3f : 1.f) * (k % 2 == 0 ? 0.8f : 1.f);
      }

      float peak0 = 0.f;
      for (int level = 0; level < kLevels; ++level)
      {
        const int H = kMaxHarmonics >> level;
        float* t = &mData[((size_t) shape * kLevels + level) * (kSize + 1)];
        for (int k = 1; k <= H; ++k)
        {
          if (std::fabs(a[k]) < 1e-7f && std::fabs(b[k]) < 1e-7f) continue;
          for (int n = 0; n < kSize; ++n)
          {
            const int j = (k * n) & (kSize - 1);
            t[n] += a[k] * cosT[j] + b[k] * sinT[j];
          }
        }
        t[kSize] = t[0]; // échantillon de garde pour l'interpolation
        if (level == 0)
          for (int n = 0; n < kSize; ++n) peak0 = std::max(peak0, std::fabs(t[n]));
      }

      const float g = peak0 > 0.f ? 1.f / peak0 : 1.f;
      for (int level = 0; level < kLevels; ++level)
      {
        float* t = &mData[((size_t) shape * kLevels + level) * (kSize + 1)];
        for (int n = 0; n <= kSize; ++n) t[n] *= g;
      }
    }
  }

  std::vector<float> mData;
};

// Excitation procédurale : aucun échantillon lu, tout est synthétisé à chaque note,
// piloté par la vélocité et suivant la hauteur.
class Exciter
{
public:
  void Trigger(int type, float freq, float vel, float snap, float sr, uint32_t seed)
  {
    mType = type; mSr = sr; mVel = vel;
    mRng.Seed(seed);

    // Bite / Snap : durée et tranchant du transitoire
    const float tau = Lerp(0.030f, 0.0015f, snap);
    mEnv = 1.f;
    mEnvCoef = std::exp(-1.f / (tau * sr));
    mRemaining = (int) (sr * (tau * 9.f + (type == kExVerre ? 0.18f : 0.012f)));
    mAge = 0;
    mDrive = 1.f + 3.f * vel;

    const float pitchTrack = std::sqrt(Clampf(freq / 261.63f, 0.1f, 16.f));
    // Invariance à fs : un bruit blanc de densité spectrale constante a une variance ∝ fs,
    // une impulsion d'aire constante a une amplitude ∝ fs, les constantes de temps sont en secondes.
    mNoiseScale = std::sqrt(sr / 48000.f);
    mAreaScale = sr / 48000.f;

    mArcRate = (800.f + 4000.f * vel) * pitchTrack;
    mBuzzInc = freq / sr; mBuzzPhase = 0.f; mSpike = 0.f;
    mSpikeCoef = std::exp(-1.f / (0.0000351f * sr)); // ≡ 0,55 par échantillon à 48 kHz

    mLoad = vel * (0.002f + 0.02f * pitchTrack) * (48000.f / sr);
    mStress = 0.f; mThreshold = 0.1f; mLp = 0.f;
    mLpCoef = 1.f - std::exp(-kTwoPi * Lerp(2200.f, 14500.f, snap) / sr);

    for (auto& p : mPing) p.Clear();
    if (type == kExVerre)
    {
      static const float r[4] = {7.1f, 11.3f, 15.8f, 22.4f};
      for (int i = 0; i < 4; ++i)
        mPing[i].Set(Clampf(freq * r[i] * (0.9f + 0.2f * mRng.Uni()), 1500.f, 0.45f * sr),
                     0.04f + 0.08f * mRng.Uni(), sr);
    }
    else if (type == kExClic)
    {
      mPing[0].Set(Clampf(freq * 8.f, 1500.f, 6500.f), 0.004f + 0.012f * (1.f - snap), sr);
    }
    mActive = true;
  }

  bool Active() const { return mActive; }
  void Stop() { mActive = false; }

  inline float Process()
  {
    if (!mActive) return 0.f;
    float out = 0.f;

    switch (mType)
    {
      case kExArc: // décharges stochastiques + bruit haché à la fréquence de la note
      {
        mBuzzPhase += mBuzzInc;
        if (mBuzzPhase >= 1.f) mBuzzPhase -= 1.f;
        const float gate = mBuzzPhase < 0.5f ? 1.f : 0.25f;
        if (mRng.Uni() < mArcRate / mSr)
          mSpike += (mRng.Uni() < 0.5f ? -1.f : 1.f) * (0.6f + 0.8f * mRng.Uni());
        mSpike *= mSpikeCoef;
        out = (mRng.Bi() * gate * 0.6f * mNoiseScale + mSpike) * mEnv;
        break;
      }
      case kExRoche: // stick-slip : la contrainte s'accumule puis cède par à-coups
      {
        mStress += mLoad * (0.5f + mRng.Uni());
        float imp = 0.f;
        if (mStress > mThreshold)
        {
          imp = mStress * (0.7f + 0.6f * mRng.Uni()) * (mRng.Uni() < 0.5f ? -1.f : 1.f) * mAreaScale;
          mStress *= 0.15f * mRng.Uni();
          mThreshold = 0.05f + 0.25f * mRng.Uni();
        }
        mLp += mLpCoef * (imp * 3.f + 0.05f * mRng.Bi() * mNoiseScale - mLp);
        out = mLp * mEnv;
        break;
      }
      case kExVerre: // fracture initiale + micro-fissures qui font sonner 4 éclats inharmoniques
      {
        float in = (mAge == 0) ? 1.f : 0.f;
        if (mRng.Uni() < 1500.f * mEnv / mSr) in += mRng.Bi() * 0.5f;
        float s = 0.f;
        for (auto& p : mPing) s += p.Process(in);
        out = s * 0.3f + in * mEnv * 0.5f;
        break;
      }
      default: // clic mécanique : impulsion + tintement bref + souffle de 0,5 ms
      {
        const float in = (mAge == 0) ? 1.f : 0.f;
        out = mPing[0].Process(in);
        if (mAge < (int) (0.0005f * mSr)) out += mRng.Bi() * 0.5f * mNoiseScale;
        break;
      }
    }

    // Non-linéarité de contact : plus on frappe fort, plus le contact sature
    out = FastTanh(out * mDrive) * mVel;

    mEnv *= mEnvCoef;
    ++mAge;
    if (--mRemaining <= 0) mActive = false;
    return out;
  }

  float Envelope() const { return mActive ? mEnv : 0.f; }

private:
  int mType = kExArc, mAge = 0, mRemaining = 0;
  bool mActive = false;
  float mSr = 48000.f, mVel = 1.f, mEnv = 0.f, mEnvCoef = 0.f, mDrive = 1.f;
  float mArcRate = 0.f, mBuzzInc = 0.f, mBuzzPhase = 0.f, mSpike = 0.f, mSpikeCoef = 0.55f;
  float mNoiseScale = 1.f, mAreaScale = 1.f;
  float mLoad = 0.f, mStress = 0.f, mThreshold = 0.1f, mLp = 0.f, mLpCoef = 0.5f;
  Mode mPing[4];
  Rng mRng;
};

struct Adsr
{
  enum EStage { kIdle = 0, kAttack, kDecay, kSustain, kRelease };
  EStage stage = kIdle;
  float v = 0.f, attInc = 1.f, decCoef = 0.f, relCoef = 0.f, sus = 0.f;

  void Set(float attackMs, float decayMs, float sustain, float releaseMs, float sr)
  {
    attInc = 1.f / std::max(1.f, attackMs * 0.001f * sr);
    decCoef = std::exp(-1.f / std::max(1.f, decayMs * 0.001f * sr / 4.6f));
    relCoef = std::exp(-1.f / std::max(1.f, releaseMs * 0.001f * sr / 4.6f));
    sus = Clampf(sustain, 0.f, 1.f);
  }
  void Gate(bool on) { stage = on ? kAttack : (stage == kIdle ? kIdle : kRelease); }
  void Reset() { stage = kIdle; v = 0.f; }
  bool Active() const { return stage != kIdle; }

  inline float Process()
  {
    switch (stage)
    {
      case kAttack:
        v += attInc;
        if (v >= 1.f) { v = 1.f; stage = kDecay; }
        break;
      case kDecay:
        v = sus + (v - sus) * decCoef;
        if (std::fabs(v - sus) < 1e-4f) { v = sus; stage = sus > 0.f ? kSustain : kIdle; }
        break;
      case kSustain:
        v = sus;
        break;
      case kRelease:
        v *= relCoef;
        if (v < 1e-5f) { v = 0.f; stage = kIdle; }
        break;
      default: break;
    }
    return v;
  }
};

struct Params
{
  // Macros (0..1)
  float snap = 0.7f, body = 0.85f, erosion = 0.1f, space = 0.1f;
  // Excitation
  int exciter = kExArc; float excLevel = 0.55f, velSens = 0.6f;
  // Corps tonal
  int osc1Shape = kShapeSine; int osc1Octave = -1; float osc1Level = 1.f;
  int osc2Shape = kShapeTriSat; int osc2Semi = 12; float osc2Detune = 0.f; float osc2Level = 0.2f;
  bool phaseReset = true; float pitchDrop = 12.f, dropTimeMs = 60.f;
  // Matrice de friction
  int material = kMatMetal; float coupling = 0.25f, resonance = 0.35f, matLevel = 0.35f;
  // Enveloppe d'amplitude
  float attackMs = 0.5f, decayMs = 900.f, sustain = 0.f, releaseMs = 250.f;
  // Sortie
  float gainLin = 0.5f;
};

// Cas d'usage prioritaires : source unique pour les presets de l'hôte,
// les boutons de l'interface et les tests.
struct UseCase { const char* name; Params p; };

inline const std::array<UseCase, 3>& UseCases()
{
  static const std::array<UseCase, 3> cases = [] {
    std::array<UseCase, 3> u {};

    Params sub; // défauts = 808
    u[0] = {"808 & Sub", sub};

    Params snare;
    snare.snap = 0.85f; snare.body = 0.45f; snare.erosion = 0.2f; snare.space = 0.35f;
    snare.exciter = kExRoche; snare.excLevel = 0.9f; snare.velSens = 0.8f;
    snare.osc1Shape = kShapeTriSat; snare.osc1Octave = 0; snare.osc1Level = 0.6f;
    snare.osc2Shape = kShapeSine; snare.osc2Semi = 7; snare.osc2Level = 0.3f;
    snare.pitchDrop = 7.f; snare.dropTimeMs = 30.f;
    snare.material = kMatMembrane; snare.coupling = 0.5f; snare.resonance = 0.3f; snare.matLevel = 0.75f;
    snare.attackMs = 0.5f; snare.decayMs = 180.f; snare.sustain = 0.f; snare.releaseMs = 120.f;
    u[1] = {"Percussions", snare};

    Params pluck;
    pluck.snap = 0.45f; pluck.body = 0.5f; pluck.erosion = 0.35f; pluck.space = 0.3f;
    pluck.exciter = kExVerre; pluck.excLevel = 0.4f; pluck.velSens = 0.7f;
    pluck.osc1Shape = kShapeWavetable; pluck.osc1Octave = 0; pluck.osc1Level = 0.7f;
    pluck.osc2Shape = kShapeWavetable; pluck.osc2Semi = 12; pluck.osc2Detune = 6.f; pluck.osc2Level = 0.35f;
    pluck.phaseReset = false; pluck.pitchDrop = 0.f; pluck.dropTimeMs = 50.f;
    pluck.material = kMatQuartz; pluck.coupling = 0.35f; pluck.resonance = 0.6f; pluck.matLevel = 0.55f;
    pluck.attackMs = 2.f; pluck.decayMs = 600.f; pluck.sustain = 0.35f; pluck.releaseMs = 400.f;
    u[2] = {"Plucks & Keys", pluck};

    return u;
  }();
  return cases;
}

struct Telemetry
{
  float transient = 0.f;
  float modes[kNumModes] = {};
  float outL = 0.f, outR = 0.f;
  float body = 0.f;
};

// Dégradation harmonique (Erosion / Wear) : saturation asymétrique à gain unité
// en petit signal, mélangée selon le degré d'usure. Les constantes ne dépendent
// que du degré d'usure : elles sont calculées à cadence de contrôle, pas par échantillon.
struct Degrader
{
  float amount = 0.f, d = 1.f, bias = 0.f, tb = 0.f, inv = 1.f;

  void Set(float a)
  {
    amount = a;
    d = 1.f + 5.f * a;
    bias = 0.35f * a;
    tb = FastTanh(bias);
    inv = 1.f / (d * (1.f - tb * tb));
  }
  inline float Process(float x) const
  {
    if (amount < 0.001f) return x;
    const float y = (FastTanh(x * d + bias) - tb) * inv;
    return x + (y - x) * amount;
  }
};

class Voice
{
public:
  enum EState { kFree = 0, kPlaying, kKilling, kPendingStart };

  EState state = kFree;
  int note = -1;
  uint64_t order = 0;
  bool gate = false, sustained = false;
  int pendingNote = -1;
  float pendingVel = 0.f;
  bool pendingReleased = false;

  void Prepare(float sr) { mSr = sr; mSrNorm = 48000.f / sr; Reset(); }

  void Reset()
  {
    state = kFree; gate = sustained = pendingReleased = false; note = -1;
    mAdsr.Reset(); mExciter.Stop();
    mPh1 = mPh2 = 0.f; mKill = 1.f; mTail = 0.f;
    mExcHp.Clear(); mCoupleHp.Clear();
    for (auto& f : mResHp) f.Clear();
    for (auto& m : mModes) m.Clear();
  }

  void Start(int n, float vel, const Params& p, uint32_t seed)
  {
    note = n; gate = true; sustained = false; state = kPlaying;
    mVel = vel;
    mBaseFreq = 440.f * std::exp2((n - 69) / 12.f);
    if (p.phaseReset) { mPh1 = 0.f; mPh2 = 0.f; }
    mPitchEnv = p.pitchDrop;
    mSustainTime = 0.f; mDrift = 0.f; mKill = 1.f; mTail = 1.f;
    mRng.Seed(seed);
    for (int i = 0; i < kNumModes; ++i) { mLfo[i] = mRng.Uni(); mModes[i].Clear(); }
    for (auto& f : mResHp) f.Clear();
    mExcHp.Clear(); mCoupleHp.Clear();
    mExciter.Trigger(p.exciter, mBaseFreq, vel, p.snap, mSr, seed * 2654435761u + 1u);
    mAdsr.Gate(true);
    mFirstUpdate = true;
  }

  void Release() { gate = false; sustained = false; mAdsr.Gate(false); }

  void Kill(int n, float vel)
  {
    pendingNote = n; pendingVel = vel; pendingReleased = false;
    state = kKilling;
  }

  float BodyFollower() const { return mBodyFollower; }

  // Cadence de contrôle : coefficients, hauteur, érosion, fin de voix
  void Control(const Params& p, float bendSemis, float modWheel)
  {
    if (state == kFree || state == kPendingStart) return;

    const float dt = (float) kControlInterval / mSr;

    mAdsr.Set(p.attackMs, p.decayMs, p.sustain, p.releaseMs, mSr);

    // Enveloppe de hauteur (808 drop) et dérive brownienne (Erosion / Wear)
    mPitchEnv *= std::exp(-dt / std::max(0.001f, p.dropTimeMs * 0.001f));
    if (mAdsr.stage >= Adsr::kDecay) mSustainTime += dt;
    const float wear = Clampf(p.erosion + 0.5f * modWheel, 0.f, 1.f);
    mEro = wear * (1.f - std::exp(-mSustainTime / 0.8f));
    mDegTonal.Set(mEro);
    mDegSurface.Set(mEro * 0.6f);
    mDrift = Clampf(mDrift * 0.995f + mRng.Bi() * 0.08f, -2.f, 2.f);
    const float driftCents = mDrift * mEro * 30.f;

    const float f = mBaseFreq * SemisToRatio(mPitchEnv + bendSemis + driftCents * 0.01f);
    const float f1 = f * std::exp2((float) p.osc1Octave);
    const float f2 = f * SemisToRatio((float) p.osc2Semi + p.osc2Detune * 0.01f);
    mLevel1 = WaveTables::LevelFor(f1, mSr);
    mLevel2 = WaveTables::LevelFor(f2, mSr);

    const float inc1 = f1 / mSr, inc2 = f2 / mSr;
    if (mFirstUpdate)
    {
      mInc1 = inc1; mInc2 = inc2; mInc1Step = mInc2Step = 0.f;
      mFirstUpdate = false;
    }
    else
    {
      mInc1Step = (inc1 - mInc1) / kControlInterval;
      mInc2Step = (inc2 - mInc2) / kControlInterval;
    }

    // Body Weight : présence du sub + coupe-bas dynamique sur la composante bruit
    mBodyGain = 0.2f + 1.3f * p.body;
    mBodyFollower = Clampf(mAdsr.v * p.osc1Level * mBodyGain / 1.5f, 0.f, 1.f);
    const float snapCut = 80.f + p.snap * 2400.f;
    const float bodyCut = 60.f + p.body * 340.f * mBodyFollower;
    mExcHp.Set(std::max(snapCut, bodyCut), 0.707f, mSr);
    mCoupleHp.Set(120.f, 0.707f, mSr);
    const float resCut = std::max(kSurfaceFloorHz, bodyCut);
    for (auto& h : mResHp) h.Set(resCut, 0.707f, mSr);

    // Matrice de friction : modes accordés sur la note, repliés au-dessus de 110 Hz
    const MaterialSpec& m = kMaterials[std::clamp(p.material, 0, kNumMaterials - 1)];
    const float t60 = 0.08f * std::pow(50.f, p.resonance) * m.t60Scale;
    for (int i = 0; i < kNumModes; ++i)
    {
      float wob = 1.f;
      if (m.wobble > 0.f)
      {
        mLfo[i] += dt * (0.7f + 0.37f * i);
        if (mLfo[i] >= 1.f) mLfo[i] -= 1.f;
        wob += m.wobble * std::sin(kTwoPi * mLfo[i]);
      }
      float fm = f * m.ratio[i] * wob;
      while (fm < kSurfaceFloorHz) fm *= 2.f;
      float g = m.gain[i] * kModeGain;
      if (fm > 0.45f * mSr) g = 0.f;
      mModes[i].Set(fm, t60 * m.decay[i], mSr);
      mModeGain[i] = g;
    }

    // Fin de voix : enveloppe éteinte, excitation terminée, matrice silencieuse
    if (state == kPlaying && !mAdsr.Active() && !mExciter.Active() && mTail < 1e-5f)
      state = kFree;
  }

  inline void Process(const Params& p, const WaveTables& wt,
                      float& sub, float& sl, float& sr, float* energy, float& transient)
  {
    if (state == kFree || state == kPendingStart) return;

    // Corps tonal
    const float t1 = wt.Read(p.osc1Shape, mLevel1, mPh1);
    const float t2 = wt.Read(p.osc2Shape, mLevel2, mPh2);
    mPh1 += mInc1; if (mPh1 >= 1.f) mPh1 -= 1.f;
    mPh2 += mInc2; if (mPh2 >= 1.f) mPh2 -= 1.f;
    mInc1 += mInc1Step; mInc2 += mInc2Step;

    const float env = mAdsr.Process();
    const float o1 = t1 * p.osc1Level * mBodyGain;
    const float o2 = mDegTonal.Process(t2 * p.osc2Level);
    const float tonal = (o1 + o2) * env;

    // Excitation (directe, coupe-bas dynamique)
    const float ex = mExciter.Process() * p.excLevel * kExciterGain;
    const float exOut = mExcHp.HighPass(ex);
    transient = std::max(transient, std::fabs(ex));

    // Couplage non linéaire : l'oscillateur excite la matière, jamais sous 120 Hz
    const float cpl = mCoupleHp.HighPass(FastTanh(tonal * (1.f + 6.f * p.coupling)) * p.coupling * 0.6f);
    // Discrétisation « temps continu » : chaque échantillon d'entrée pèse 1/fs,
    // sinon le gain à la résonance (∝ t60·fs) ferait sonner la matière plus fort à 192 kHz.
    const float resIn = (ex + cpl) * mSrNorm;

    float rl = 0.f, rr = 0.f;
    for (int i = 0; i < kNumModes; ++i)
    {
      const float y = mModes[i].Process(resIn) * mModeGain[i];
      if (i & 1) { rl += y * 0.7f; rr += y; }
      else       { rl += y; rr += y * 0.7f; }
      energy[i] = std::max(energy[i], std::fabs(y));
    }

    // Garantie « rien sous 100 Hz » : passe-haut 24 dB/oct sur la matière
    rl = mResHp[1].HighPass(mResHp[0].HighPass(rl));
    rr = mResHp[3].HighPass(mResHp[2].HighPass(rr));
    rl = mDegSurface.Process(rl) * p.matLevel;
    rr = mDegSurface.Process(rr) * p.matLevel;
    mTail = std::max(std::fabs(rl) + std::fabs(rr), mTail * 0.9995f);

    // Fondu de 3 ms quand la voix est volée ou relancée (pas de clic)
    if (state == kKilling)
    {
      mKill -= 1.f / (0.003f * mSr);
      if (mKill <= 0.f) { mKill = 0.f; state = kPendingStart; }
    }

    sub += tonal * mKill;
    sl += (exOut + rl) * mKill;
    sr += (exOut + rr) * mKill;
  }

private:
  float mSr = 48000.f, mSrNorm = 1.f, mVel = 1.f, mBaseFreq = 440.f;
  float mPh1 = 0.f, mPh2 = 0.f, mInc1 = 0.f, mInc2 = 0.f, mInc1Step = 0.f, mInc2Step = 0.f;
  int mLevel1 = 0, mLevel2 = 0;
  bool mFirstUpdate = true;
  float mPitchEnv = 0.f, mSustainTime = 0.f, mDrift = 0.f, mEro = 0.f;
  float mBodyGain = 1.f, mBodyFollower = 0.f, mKill = 1.f, mTail = 0.f;
  float mLfo[kNumModes] = {};
  Degrader mDegTonal, mDegSurface;
  Adsr mAdsr;
  Exciter mExciter;
  Svf mExcHp, mCoupleHp, mResHp[4];
  Mode mModes[kNumModes];
  float mModeGain[kNumModes] = {};
  Rng mRng;
};

// Space Damping : cavité ultra-courte (FDN 4 lignes, matrice de Householder,
// amortissement dans la boucle). Ne traite que le bus surface : le sub reste sec et mono.
class MicroCavity
{
public:
  void Prepare(float sr)
  {
    mSr = sr;
    mSize = (int) (sr * 0.02f) + 4;
    for (auto& b : mBuf) b.assign(mSize, 0.f);
    mW = 0;
    mLp.fill(0.f);
  }

  void Control(float space)
  {
    static const float baseMs[4] = {1.3f, 2.1f, 3.4f, 5.5f};
    const float scale = 0.6f + 1.6f * space;
    for (int i = 0; i < 4; ++i)
      mLen[i] = std::clamp((int) (baseMs[i] * scale * 0.001f * mSr), 1, mSize - 1);
    mFeedback = 0.35f + 0.5f * space;
    mDamp = 1.f - std::exp(-kTwoPi * Lerp(9000.f, 2500.f, space) / mSr);
    mWet = space * 0.8f;
  }

  inline void Process(float& l, float& r)
  {
    float d[4];
    for (int i = 0; i < 4; ++i)
    {
      int rp = mW - mLen[i]; if (rp < 0) rp += mSize;
      mLp[i] += mDamp * (mBuf[i][rp] - mLp[i]);
      d[i] = mLp[i];
    }
    const float half = 0.5f * (d[0] + d[1] + d[2] + d[3]);
    mBuf[0][mW] = 0.5f * l + mFeedback * (d[0] - half);
    mBuf[1][mW] = 0.5f * r + mFeedback * (d[1] - half);
    mBuf[2][mW] = 0.5f * l + mFeedback * (d[2] - half);
    mBuf[3][mW] = 0.5f * r + mFeedback * (d[3] - half);
    if (++mW >= mSize) mW = 0;
    l += 0.5f * (d[0] + d[2]) * mWet;
    r += 0.5f * (d[1] + d[3]) * mWet;
  }

private:
  std::array<std::vector<float>, 4> mBuf;
  std::array<float, 4> mLp {};
  int mLen[4] = {1, 1, 1, 1};
  int mSize = 4, mW = 0;
  float mSr = 48000.f, mFeedback = 0.f, mDamp = 0.5f, mWet = 0.f;
};

class Engine
{
public:
  Engine() : mTables(WaveTables::Get()) {}

  void Prepare(double sampleRate)
  {
    mSr = (float) sampleRate;
    for (auto& v : mVoices) v.Prepare(mSr);
    mCavity.Prepare(mSr);
    mCtrlCountdown = 0;
    mDcL = mDcR = mDcXL = mDcXR = 0.f;
    mGainSm = mP.gainLin;
    mAcc = Telemetry();
    mAccSamples = 0;
    mTelemetryReady = false;
  }

  void SetParams(const Params& p) { mP = p; }
  void SetPitchBend(float semis) { mBend = semis; }
  void SetModWheel(float v) { mModWheel = Clampf(v, 0.f, 1.f); }

  void SetSustain(bool on)
  {
    mSustain = on;
    if (!on)
      for (auto& v : mVoices)
        if (v.state == Voice::kPlaying && v.sustained) v.Release();
  }

  void AllNotesOff()
  {
    for (auto& v : mVoices)
      if (v.state == Voice::kPlaying && v.gate) v.Release();
  }

  void NoteOn(int note, float velocity)
  {
    const float vel = 1.f - mP.velSens + mP.velSens * Clampf(velocity, 0.f, 1.f);

    // Note déjà en jeu : fondu court puis redémarrage (évite le saut de phase du sub)
    for (auto& v : mVoices)
      if ((v.state == Voice::kPlaying || v.state == Voice::kKilling) && v.note == note && v.gate)
      { v.Kill(note, vel); return; }

    for (auto& v : mVoices)
      if (v.state == Voice::kFree) { StartVoice(v, note, vel); return; }

    // Vol : la plus ancienne voix relâchée, sinon la plus ancienne tout court
    Voice* victim = nullptr;
    for (auto& v : mVoices)
      if (v.state == Voice::kPlaying && !v.gate && (!victim || v.order < victim->order)) victim = &v;
    if (!victim)
      for (auto& v : mVoices)
        if (!victim || v.order < victim->order) victim = &v;
    victim->Kill(note, vel);
  }

  void NoteOff(int note)
  {
    for (auto& v : mVoices)
    {
      if (v.state == Voice::kPlaying && v.gate && v.note == note && !v.sustained)
      {
        if (mSustain) v.sustained = true;
        else v.Release();
      }
      else if ((v.state == Voice::kKilling || v.state == Voice::kPendingStart) && v.pendingNote == note)
      {
        v.pendingReleased = true; // note relâchée pendant le fondu : relâcher dès le départ
      }
    }
  }

  template <typename T>
  void Render(T** out, int nChans, int start, int end)
  {
    const DenormalGuard guard;
    for (int s = start; s < end; ++s)
    {
      if (--mCtrlCountdown <= 0) { ControlTick(); mCtrlCountdown = kControlInterval; }

      float sub = 0.f, sl = 0.f, sr = 0.f;
      for (auto& v : mVoices)
      {
        if (v.state == Voice::kPendingStart)
        {
          const bool released = v.pendingReleased;
          StartVoice(v, v.pendingNote, v.pendingVel);
          if (released && !mSustain) v.Release();
        }
        v.Process(mP, mTables, sub, sl, sr, mAcc.modes, mAcc.transient);
      }

      if (mP.space > 0.001f) mCavity.Process(sl, sr);

      float L = sub + sl, R = sub + sr;

      // Bloqueur de continu (la dégradation asymétrique en crée)
      const float yL = L - mDcXL + 0.9995f * mDcL; mDcXL = L; mDcL = yL;
      const float yR = R - mDcXR + 0.9995f * mDcR; mDcXR = R; mDcR = yR;

      mGainSm += 0.002f * (mP.gainLin - mGainSm);
      const float oL = std::tanh(yL * mGainSm);
      const float oR = std::tanh(yR * mGainSm);
      out[0][s] = (T) oL;
      if (nChans > 1) out[1][s] = (T) oR;

      mAcc.outL = std::max(mAcc.outL, std::fabs(oL));
      mAcc.outR = std::max(mAcc.outR, std::fabs(oR));
      ++mAccSamples;
    }
  }

  // Une mesure toutes les ~10 ms, quelle que soit la taille de buffer de l'hôte
  bool TakeTelemetry(Telemetry& t)
  {
    if (!mTelemetryReady) return false;
    t = mReady;
    mTelemetryReady = false;
    return true;
  }

private:
  void StartVoice(Voice& v, int note, float vel)
  {
    v.Start(note, vel, mP, NextSeed());
    v.order = mCounter++;
    v.Control(mP, mBend, mModWheel);
  }

  void ControlTick()
  {
    float body = 0.f;
    for (auto& v : mVoices)
    {
      v.Control(mP, mBend, mModWheel);
      body = std::max(body, v.BodyFollower());
    }
    mCavity.Control(mP.space);
    mAcc.body = std::max(mAcc.body, body);

    if (mAccSamples >= (int) (mSr * 0.01f))
    {
      mReady = mAcc;
      mTelemetryReady = true;
      mAcc = Telemetry();
      mAccSamples = 0;
    }
  }

  uint32_t NextSeed() { mSeed = mSeed * 1664525u + 1013904223u; return mSeed; }

  const WaveTables& mTables;
  Params mP;
  std::array<Voice, kMaxVoices> mVoices;
  MicroCavity mCavity;
  float mSr = 48000.f, mBend = 0.f, mModWheel = 0.f, mGainSm = 0.5f;
  float mDcL = 0.f, mDcR = 0.f, mDcXL = 0.f, mDcXR = 0.f;
  bool mSustain = false;
  int mCtrlCountdown = 0;
  uint64_t mCounter = 0;
  uint32_t mSeed = 0x2545F491u;

  Telemetry mAcc, mReady;
  int mAccSamples = 0;
  bool mTelemetryReady = false;
};

} // namespace mc
