// Banc d'essai du moteur Micro-Contact, sans iPlug2.
// Compilation : g++ -std=c++17 -O2 -Wall -Wextra -I.. dsp_smoke_test.cpp -o dsp_smoke_test
// Retourne 0 si tous les tests passent.

#include "../MicroContact_Params.h"
#include <cstdio>
#include <chrono>
#include <vector>
#include <cmath>

using namespace mc;

static int gFailures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++gFailures; std::printf("  ÉCHEC : " __VA_ARGS__); std::printf("\n"); } } while (0)

struct Render
{
  std::vector<float> L, R;
};

// Joue une note et rend `seconds` secondes, note relâchée à `offAt` secondes.
static Render Play(const Params& p, int note, float vel, float seconds, float offAt, float sr = 48000.f)
{
  Engine e;
  e.Prepare(sr);
  e.SetParams(p);
  const int n = (int) (seconds * sr), block = 64, off = (int) (offAt * sr);
  Render r; r.L.assign(n, 0.f); r.R.assign(n, 0.f);
  bool noteOn = false, noteOff = false;
  for (int pos = 0; pos < n; pos += block)
  {
    const int end = std::min(n, pos + block);
    if (!noteOn) { e.NoteOn(note, vel); noteOn = true; }
    if (!noteOff && pos >= off) { e.NoteOff(note); noteOff = true; }
    float* outs[2] = {r.L.data(), r.R.data()};
    e.Render(outs, 2, pos, end);
  }
  return r;
}

static float Peak(const std::vector<float>& x) { float p = 0; for (float v : x) p = std::max(p, std::fabs(v)); return p; }
static float Rms(const std::vector<float>& x, int a, int b)
{
  double s = 0; for (int i = a; i < b; ++i) s += (double) x[i] * x[i];
  return (float) std::sqrt(s / std::max(1, b - a));
}
static bool Finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }

// Énergie spectrale sous `fc` / énergie totale, par DFT directe sur une fenêtre de Hann
static float LowBandRatio(const std::vector<float>& x, int start, int len, float fc, float sr)
{
  double low = 0, total = 0;
  const int maxBin = len / 2;
  for (int k = 1; k < maxBin; k += (k < 64 ? 1 : 4))
  {
    double re = 0, im = 0;
    for (int n = 0; n < len; ++n)
    {
      const double w = 0.5 - 0.5 * std::cos(2 * M_PI * n / (len - 1));
      const double ph = 2 * M_PI * k * n / len;
      re += x[start + n] * w * std::cos(ph);
      im -= x[start + n] * w * std::sin(ph);
    }
    const double e = (re * re + im * im) * (k < 64 ? 1 : 4);
    total += e;
    if (k * sr / len < fc) low += e;
  }
  return (float) (low / std::max(1e-30, total));
}

// Tous les champs de Params, pour comparer deux jeux de réglages
static std::vector<double> Fields(const Params& p)
{
  return {p.snap, p.body, p.erosion, p.space, (double) p.exciter, p.excLevel, p.velSens,
          (double) p.osc1Shape, (double) p.osc1Octave, p.osc1Level, (double) p.osc2Shape, (double) p.osc2Semi,
          p.osc2Detune, p.osc2Level, p.phaseReset ? 1.0 : 0.0, p.pitchDrop, p.dropTimeMs, (double) p.material,
          p.coupling, p.resonance, p.matLevel, p.attackMs, p.decayMs, p.sustain, p.releaseMs, p.gainLin};
}

int main()
{
  const float sr = 48000.f;

  std::printf("0. Table des paramètres : aller-retour moteur <-> hôte sans perte\n");
  {
    Params distinct; // une valeur différente des défauts dans chaque champ
    distinct.snap = .11f; distinct.body = .22f; distinct.erosion = .33f; distinct.space = .44f;
    distinct.exciter = kExClic; distinct.excLevel = .55f + .01f; distinct.velSens = .66f;
    distinct.osc1Shape = kShapeWavetable; distinct.osc1Octave = -3; distinct.osc1Level = .77f;
    distinct.osc2Shape = kShapeSine; distinct.osc2Semi = -19; distinct.osc2Detune = -23.f; distinct.osc2Level = .88f;
    distinct.phaseReset = false; distinct.pitchDrop = 5.5f; distinct.dropTimeMs = 123.f;
    distinct.material = kMatMercure; distinct.coupling = .12f; distinct.resonance = .23f; distinct.matLevel = .34f;
    distinct.attackMs = 12.f; distinct.decayMs = 345.f; distinct.sustain = .45f; distinct.releaseMs = 678.f;
    distinct.gainLin = 0.25f;

    const Params defaults;
    CHECK((int) Fields(defaults).size() == kNumParams, "Fields() ne couvre pas les %d paramètres", kNumParams);
    const auto fd = Fields(defaults), fx = Fields(distinct);
    for (size_t k = 0; k < fd.size(); ++k)
      CHECK(std::fabs(fd[k] - fx[k]) > 1e-6, "le champ %zu de « distinct » égale le défaut : test invalide", k);

    Params q; // part des défauts
    for (int idx = 0; idx < kNumParams; ++idx)
    {
      const auto before = Fields(q);
      FromPluginValue(idx, ToPluginValue(idx, distinct), q);
      CHECK(Fields(q) != before, "le paramètre %d n'a aucun effet (case manquante ?)", idx);
    }
    const auto fq = Fields(q);
    for (size_t k = 0; k < fq.size(); ++k)
      CHECK(std::fabs(fq[k] - fx[k]) < 1e-4, "champ %zu : %.5f au lieu de %.5f après aller-retour", k, fq[k], fx[k]);
    std::printf("  %d paramètres vérifiés\n", kNumParams);
  }

  std::printf("1. Stabilité et niveaux, chaque cas d'usage × matière × excitation\n");
  for (const auto& uc : UseCases())
  {
    for (int mat = 0; mat < kNumMaterials; ++mat)
      for (int ex = 0; ex < kNumExciters; ++ex)
      {
        Params p = uc.p; p.material = mat; p.exciter = ex;
        for (int note : {24, 36, 60, 84, 96})
        {
          Render r = Play(p, note, 1.f, 1.5f, 0.6f, sr);
          CHECK(Finite(r.L) && Finite(r.R), "%s mat %d ex %d note %d : NaN/Inf", uc.name, mat, ex, note);
          CHECK(Peak(r.L) < 1.0f, "%s mat %d ex %d note %d : dépassement", uc.name, mat, ex, note);
        }
      }
    Render r = Play(uc.p, 48, 0.9f, 1.5f, 0.6f, sr);
    std::printf("  %-14s crête %.2f  RMS(0-300 ms) %.3f\n", uc.name, Peak(r.L), Rms(r.L, 0, (int) (0.3f * sr)));
    CHECK(Peak(r.L) > 0.1f, "%s trop faible", uc.name);
  }

  std::printf("2. Paramètres extrêmes (tout à fond, puis tout à zéro)\n");
  {
    Params hot = UseCases()[0].p;
    hot.snap = hot.body = hot.erosion = hot.space = 1.f; hot.excLevel = hot.coupling = hot.resonance = hot.matLevel = 1.f;
    hot.osc1Level = hot.osc2Level = 1.f; hot.pitchDrop = 36.f; hot.gainLin = 2.f; hot.osc2Semi = 24;
    for (int mat = 0; mat < kNumMaterials; ++mat)
    {
      hot.material = mat;
      Render r = Play(hot, 30, 1.f, 3.f, 1.5f, sr);
      CHECK(Finite(r.L) && Finite(r.R), "à fond, matière %d : NaN/Inf", mat);
    }
    Params cold = UseCases()[0].p;
    cold.snap = cold.body = cold.erosion = cold.space = 0.f; cold.excLevel = cold.coupling = cold.resonance = cold.matLevel = 0.f;
    cold.osc1Level = cold.osc2Level = 0.f; cold.attackMs = 2000.f; cold.decayMs = 1.f; cold.releaseMs = 1.f;
    Render r = Play(cold, 60, 0.f, 1.f, 0.5f, sr);
    CHECK(Finite(r.L), "à zéro : NaN/Inf");
  }

  std::printf("3. Bus surface : rien sous 100 Hz (oscillateurs coupés, matière seule)\n");
  for (const auto& uc : UseCases())
  {
    Params p = uc.p; p.osc1Level = 0.f; p.osc2Level = 0.f; p.coupling = 0.f;
    Render r = Play(p, 28, 1.f, 1.0f, 0.5f, sr); // note grave : E1, 41 Hz
    const float ratio = LowBandRatio(r.L, 0, 8192, 100.f, sr);
    std::printf("  %-14s énergie < 100 Hz : %.4f %%\n", uc.name, ratio * 100.f);
    CHECK(ratio < 0.02f, "%s : %.2f %% d'énergie sous 100 Hz dans la matière", uc.name, ratio * 100.f);
  }

  std::printf("3b. Corps tonal seul audible (régression : enveloppe jamais configurée)\n");
  for (const auto& uc : UseCases())
  {
    Params p = uc.p; p.excLevel = 0.f; p.matLevel = 0.f; p.coupling = 0.f; p.space = 0.f;
    Render r = Play(p, 48, 1.f, 0.4f, 0.3f, sr);
    const float rms = Rms(r.L, (int) (0.01f * sr), (int) (0.1f * sr));
    std::printf("  %-14s RMS tonal (10-100 ms) : %.3f\n", uc.name, rms);
    CHECK(rms > 0.05f, "%s : corps tonal muet (RMS %.4f)", uc.name, rms);
  }

  std::printf("3c. Sub 808 : la fondamentale domine bien le bas du spectre\n");
  {
    Params p = UseCases()[0].p; p.pitchDrop = 0.f;
    Render r = Play(p, 36, 1.f, 0.6f, 0.5f, sr); // sub à 32,7 Hz
    const float ratio = LowBandRatio(r.L, (int) (0.05f * sr), 8192, 100.f, sr);
    std::printf("  part d'énergie < 100 Hz du son complet : %.1f %%\n", ratio * 100.f);
    CHECK(ratio > 0.5f, "le sub ne domine pas (%.1f %%)", ratio * 100.f);
  }

  std::printf("4. Alignement de phase : le sub démarre au passage par zéro\n");
  {
    Params p = UseCases()[0].p; p.excLevel = 0.f; p.matLevel = 0.f; p.coupling = 0.f; p.osc2Level = 0.f;
    p.space = 0.f; p.pitchDrop = 0.f; p.attackMs = 0.5f;
    Render r = Play(p, 36, 1.f, 0.05f, 0.04f, sr);
    std::printf("  premier échantillon : %.6f\n", r.L[0]);
    CHECK(std::fabs(r.L[0]) < 1e-3f, "le sub ne démarre pas à zéro (%.5f)", r.L[0]);
  }

  std::printf("5. Relance de la même note et vol de voix : pas de clic\n");
  {
    Params p = UseCases()[0].p; p.excLevel = 0.f; p.matLevel = 0.f; p.coupling = 0.f; p.space = 0.f;
    p.pitchDrop = 0.f; p.decayMs = 3000.f; p.sustain = 1.f;
    Engine e; e.Prepare(sr); e.SetParams(p);
    const int n = (int) (0.5f * sr);
    std::vector<float> L(n), R(n);
    float* outs[2] = {L.data(), R.data()};
    e.NoteOn(36, 1.f);
    e.Render(outs, 2, 0, n / 2);
    e.NoteOn(36, 1.f); // relance pendant que le sub sonne
    e.Render(outs, 2, n / 2, n);
    float maxJump = 0.f;
    for (int i = n / 2 - 10; i < n / 2 + (int) (0.01f * sr); ++i) maxJump = std::max(maxJump, std::fabs(L[i + 1] - L[i]));
    // Pente maximale d'un sinus de 32,7 Hz d'amplitude ~0,9 : ≈ 0,004 par échantillon
    const float level = Rms(L, n / 4, n / 2);
    std::printf("  niveau avant relance (RMS) : %.3f, saut max autour de la relance : %.5f\n", level, maxJump);
    CHECK(level > 0.1f, "test invalide : rien ne sonne avant la relance");
    CHECK(maxJump < 0.02f, "clic à la relance (%.4f)", maxJump);
  }

  std::printf("6. Invariance à la fréquence d'échantillonnage (même son de 44,1 à 192 kHz)\n");
  for (const auto& uc : UseCases())
  {
    Render ref = Play(uc.p, 60, 0.8f, 0.6f, 0.3f, 48000.f);
    const float refRms = Rms(ref.L, 0, (int) ref.L.size());
    std::printf("  %-14s", uc.name);
    for (float fs : {44100.f, 88200.f, 96000.f, 192000.f})
    {
      Render r = Play(uc.p, 60, 0.8f, 0.6f, 0.3f, fs);
      const float db = 20.f * std::log10(Rms(r.L, 0, (int) r.L.size()) / refRms);
      std::printf("  %3.0fk %+.1f dB", fs / 1000.f, db);
      CHECK(Finite(r.L) && Peak(r.L) < 1.f, "%s à %.0f Hz : rendu anormal", uc.name, fs);
      CHECK(std::fabs(db) < 1.f, "%s à %.0f Hz : écart de niveau %+.1f dB", uc.name, fs, db);
    }
    std::printf("\n");
  }

  std::printf("7. Charge CPU : 8 voix tenues, cas le plus lourd (mercure + espace)\n");
  {
    Params p = UseCases()[2].p; p.material = kMatMercure; p.space = 0.6f; p.sustain = 1.f;
    Engine e; e.Prepare(sr); e.SetParams(p);
    for (int k = 0; k < 8; ++k) e.NoteOn(48 + k * 3, 0.8f);
    const int seconds = 10, n = (int) sr * seconds, block = 128;
    std::vector<float> L(block), R(block);
    float* outs[2] = {L.data(), R.data()};
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < n; pos += block) e.Render(outs, 2, 0, block);
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  %d s d'audio en %.3f s  →  %.1f %% d'un cœur\n", seconds, el, 100.0 * el / seconds);
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    std::printf("  (build instrumenté par sanitizer : budget CPU non vérifié)\n");
#else
    CHECK(el / seconds < 0.25, "trop lourd : %.1f %% d'un cœur", 100.0 * el / seconds);
#endif
  }

  std::printf("\n%s (%d échec(s))\n", gFailures ? "ÉCHEC" : "TOUS LES TESTS PASSENT", gFailures);
  return gFailures ? 1 : 0;
}
