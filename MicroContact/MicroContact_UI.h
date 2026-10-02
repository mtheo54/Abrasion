#pragma once

// Interface Micro-Contact (marque Abrasion), dessinée en vectoriel avec IGraphics.
// Aucune image à embarquer : le logo et tous les visuels sont des chemins.

#include "IControl.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstring>

using namespace iplug;
using namespace igraphics;

namespace mcui {

const IColor Bg(255, 28, 30, 31);
const IColor Panel(255, 36, 39, 41);
const IColor Panel2(255, 45, 49, 51);
const IColor Border(255, 59, 64, 66);
const IColor Text(255, 236, 232, 224);
const IColor Muted(255, 155, 160, 160);
const IColor Orange(255, 255, 106, 61);
const IColor Cyan(255, 111, 224, 208);

constexpr const char* kFont = "Roboto-Regular";

inline IColor WithAlpha(const IColor& c, float a)
{
  return IColor(std::clamp((int) (a * 255.f), 0, 255), c.R, c.G, c.B);
}

inline IVStyle MakeStyle(const IColor& accent)
{
  return DEFAULT_STYLE
    .WithColor(kBG, Panel)
    .WithColor(kFG, Panel2)
    .WithColor(kPR, accent)
    .WithColor(kFR, Border)
    .WithColor(kHL, WithAlpha(accent, 0.25f))
    .WithColor(kSH, IColor(0, 0, 0, 0))
    .WithColor(kX1, accent)
    .WithDrawShadows(false)
    .WithFrameThickness(1.f)
    .WithRoundness(0.25f)
    .WithLabelText(IText(12.f, Text, kFont))
    .WithValueText(IText(12.f, Muted, kFont));
}

// Lecture du paquet de télémétrie envoyé par MicroContact::PushTelemetry
inline bool ReadTelemetry(int msgTag, int dataSize, const void* pData, ISenderData<kTelemetryChannels>& d)
{
  if (msgTag != ISender<>::kUpdateMessage) return false;
  IByteStream stream(pData, dataSize);
  stream.Get(&d, 0);
  return true;
}

// Applique un cas d'usage depuis l'interface : l'hôte est prévenu de chaque changement
// (automation, annulation) et les contrôles affichés suivent.
inline void ApplyUseCase(IEditorDelegate& d, int caseIdx)
{
  const mc::Params& p = mc::UseCases()[std::clamp(caseIdx, 0, kNumPresets - 1)].p;
  for (int i = 0; i < kNumParams; ++i)
  {
    const double norm = d.GetParam(i)->ToNormalized(ToPluginValue(i, p));
    d.BeginInformHostOfParamChangeFromUI(i);
    d.SendParameterValueFromUI(i, norm);
    d.EndInformHostOfParamChangeFromUI(i);
    d.SendParameterValueFromDelegate(i, norm, true);
  }
}

// Petite horloge pour les contrôles animés
class Ticker
{
public:
  float Tick()
  {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(0.1f, std::chrono::duration<float>(now - mLast).count());
    mLast = now;
    mTime += dt;
    return dt;
  }
  float Time() const { return mTime; }

private:
  std::chrono::steady_clock::time_point mLast = std::chrono::steady_clock::now();
  float mTime = 0.f;
};

// Logo Abrasion : une fissure traverse le carré et finit en étincelle.
class AbrasionLogo final : public IControl
{
public:
  explicit AbrasionLogo(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }

  static void DrawMark(IGraphics& g, const IRECT& mark)
  {
    const float k = mark.W() / 34.f;
    auto X = [&](float x) { return mark.L + x * k; };
    auto Y = [&](float y) { return mark.T + y * k; };

    g.DrawRoundRect(Text, IRECT(X(2.f), Y(2.f), X(32.f), Y(32.f)), 8.f * k, nullptr, 2.f * k);

    IStrokeOptions opts;
    opts.mCapOption = ELineCap::Round;
    opts.mJoinOption = ELineJoin::Round;
    g.PathClear();
    g.PathMoveTo(X(7.f), Y(25.f));
    g.PathLineTo(X(12.5f), Y(18.5f));
    g.PathLineTo(X(16.f), Y(21.f));
    g.PathLineTo(X(21.5f), Y(12.f));
    g.PathLineTo(X(27.f), Y(8.f));
    g.PathStroke(IPattern(Orange), 2.4f * k, opts);

    g.FillCircle(Cyan, X(27.f), Y(8.f), 2.6f * k);
  }

  void Draw(IGraphics& g) override
  {
    const float s = mRECT.H();
    DrawMark(g, IRECT(mRECT.L, mRECT.T, mRECT.L + s, mRECT.B));

    const float x = mRECT.L + s + 14.f;
    g.DrawText(IText(10.5f, Muted, kFont, EAlign::Near), "A B R A S I O N",
               IRECT(x, mRECT.T + 2.f, mRECT.R, mRECT.T + 16.f));
    g.DrawText(IText(22.f, Text, kFont, EAlign::Near), "Micro-Contact",
               IRECT(x, mRECT.T + 16.f, x + 170.f, mRECT.B));
    g.DrawText(IText(11.5f, Muted, kFont, EAlign::Near), "Transient-to-Resonance Engine",
               IRECT(x + 172.f, mRECT.T + 22.f, mRECT.R, mRECT.B));
  }
};

// Panneau avec pastille de couleur, titre et badge facultatif
class PanelControl final : public IControl
{
public:
  PanelControl(const IRECT& bounds, const char* title, const IColor& accent, const char* badge = nullptr)
  : IControl(bounds), mTitle(title), mBadge(badge), mAccent(accent)
  {
    SetIgnoreMouse(true);
  }

  void Draw(IGraphics& g) override
  {
    g.FillRoundRect(Panel, mRECT, 10.f);
    g.DrawRoundRect(Border, mRECT, 10.f, nullptr, 1.f);
    g.FillCircle(mAccent, mRECT.L + 22.f, mRECT.T + 22.f, 3.5f);
    g.DrawText(IText(14.f, Text, kFont, EAlign::Near), mTitle,
               IRECT(mRECT.L + 32.f, mRECT.T + 10.f, mRECT.R - 140.f, mRECT.T + 34.f));
    if (mBadge)
    {
      const float w = 18.f + 6.2f * (float) std::strlen(mBadge);
      const IRECT b(mRECT.R - 16.f - w, mRECT.T + 12.f, mRECT.R - 16.f, mRECT.T + 32.f);
      g.DrawRoundRect(Border, b, 10.f, nullptr, 1.f);
      g.DrawText(IText(10.5f, Muted, kFont), mBadge, b);
    }
  }

private:
  const char* mTitle;
  const char* mBadge;
  IColor mAccent;
};

// Chemin du signal : 4 étapes qui s'illuminent selon l'activité réelle du moteur
class SignalPathControl final : public IControl
{
public:
  explicit SignalPathControl(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }

  bool IsDirty() override { return true; }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    ISenderData<kTelemetryChannels> d;
    if (!ReadTelemetry(msgTag, dataSize, pData, d)) return;
    float modes = 0.f;
    for (int i = 1; i <= 8; ++i) modes += d.vals[i];
    mTarget[0] = std::max(mTarget[0], std::min(1.f, d.vals[0] * 2.5f));
    mTarget[1] = std::max(mTarget[1], d.vals[11]);
    mTarget[2] = std::max(mTarget[2], std::min(1.f, modes * 4.f));
    mTarget[3] = std::max(mTarget[3], std::min(1.f, std::max(d.vals[9], d.vals[10]) * 1.5f));
  }

  void Draw(IGraphics& g) override
  {
    const float dt = mClock.Tick();
    for (int i = 0; i < 4; ++i)
    {
      mActivity[i] = std::max(mTarget[i], mActivity[i] * std::exp(-dt / 0.25f));
      mTarget[i] = 0.f;
    }

    static const char* titles[4] = {"Excitation", "Corps tonal", "Matrice de friction", "Sortie"};
    static const char* subs[4] = {"impacts microscopiques", "OSC 1 + OSC 2", "résonateurs couplés", "mix stéréo"};
    const IColor accents[4] = {Orange, Cyan, Cyan, Muted};
    const IColor flow[3] = {Orange, Cyan, Orange};

    const float gap = 64.f;
    const float w = (mRECT.W() - 3.f * gap) / 4.f;
    IRECT cards[4];
    for (int i = 0; i < 4; ++i)
    {
      const float x = mRECT.L + i * (w + gap);
      cards[i] = IRECT(x, mRECT.T + 2.f, x + w, mRECT.B - 2.f);
    }

    // Flèches et particules de flux
    for (int i = 0; i < 3; ++i)
    {
      const float x1 = cards[i].R + 4.f, x2 = cards[i + 1].L - 6.f, y = cards[i].MH();
      g.DrawLine(Border, x1, y, x2, y, nullptr, 2.f);
      g.FillTriangle(Border, x2 + 6.f, y, x2 - 2.f, y - 5.f, x2 - 2.f, y + 5.f);

      const float speed = 0.35f + 0.9f * mActivity[i];
      const float ph = std::fmod(mClock.Time() * speed + i * 0.33f, 1.f);
      const float a = std::sin(3.14159f * ph) * (0.3f + 0.7f * mActivity[i]);
      g.FillCircle(WithAlpha(flow[i], a), x1 + (x2 - x1) * ph, y, 3.f);
    }

    // Cartes, avec halo proportionnel à l'activité
    for (int i = 0; i < 4; ++i)
    {
      if (mActivity[i] > 0.02f)
        g.FillRoundRect(WithAlpha(accents[i], 0.28f * mActivity[i]), cards[i].GetPadded(3.f), 10.f);
      g.FillRoundRect(Panel, cards[i], 8.f);
      g.DrawRoundRect(mActivity[i] > 0.3f ? accents[i] : Border, cards[i], 8.f, nullptr, 1.f);
      g.FillCircle(accents[i], cards[i].L + 16.f, cards[i].MH() - 7.f, 3.f);
      g.DrawText(IText(13.f, Text, kFont, EAlign::Near), titles[i],
                 IRECT(cards[i].L + 26.f, cards[i].T + 6.f, cards[i].R - 8.f, cards[i].MH()));
      g.DrawText(IText(11.f, Muted, kFont, EAlign::Near), subs[i],
                 IRECT(cards[i].L + 26.f, cards[i].MH(), cards[i].R - 8.f, cards[i].B - 6.f));
    }
  }

private:
  Ticker mClock;
  float mActivity[4] = {};
  float mTarget[4] = {};
};

// Oscilloscope du transitoire : historique de l'enveloppe d'excitation
class TransientScope final : public IControl
{
public:
  explicit TransientScope(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }

  bool IsDirty() override { return true; }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    ISenderData<kTelemetryChannels> d;
    if (!ReadTelemetry(msgTag, dataSize, pData, d)) return;
    mHistory[mWrite] = std::min(1.f, std::sqrt(d.vals[0] * 2.f));
    mWrite = (mWrite + 1) % kBars;
  }

  void Draw(IGraphics& g) override
  {
    g.DrawText(IText(11.f, Muted, kFont, EAlign::Near), "Scope transitoire",
               IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 16.f));
    const IRECT area(mRECT.L, mRECT.T + 20.f, mRECT.R, mRECT.B);
    g.FillRoundRect(Bg, area, 6.f);
    g.DrawRoundRect(Border, area, 6.f, nullptr, 1.f);

    const IRECT in = area.GetPadded(-5.f);
    const float bw = in.W() / kBars;
    for (int i = 0; i < kBars; ++i)
    {
      const float v = mHistory[(mWrite + i) % kBars];
      if (v < 0.01f) continue;
      const float h = std::max(2.f, v * in.H());
      g.FillRect(Orange, IRECT(in.L + i * bw + 0.5f, in.B - h, in.L + (i + 1) * bw - 0.5f, in.B));
    }
  }

private:
  static constexpr int kBars = 48;
  float mHistory[kBars] = {};
  int mWrite = 0;
};

// Énergie des 8 modes couplés de la matrice
class ModeBars final : public IControl
{
public:
  explicit ModeBars(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }

  bool IsDirty() override { return true; }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    ISenderData<kTelemetryChannels> d;
    if (!ReadTelemetry(msgTag, dataSize, pData, d)) return;
    for (int i = 0; i < 8; ++i) mTarget[i] = std::max(mTarget[i], std::min(1.f, std::sqrt(d.vals[1 + i] * 30.f)));
  }

  void Draw(IGraphics& g) override
  {
    const float dt = mClock.Tick();
    const float bw = mRECT.W() / 8.f;
    for (int i = 0; i < 8; ++i)
    {
      mLevel[i] = std::max(mTarget[i], mLevel[i] * std::exp(-dt / 0.3f));
      mTarget[i] = 0.f;
      const IRECT slot(mRECT.L + i * bw + 2.f, mRECT.T, mRECT.L + (i + 1) * bw - 2.f, mRECT.B);
      g.FillRect(Panel2, slot);
      const float h = std::max(2.f, mLevel[i] * slot.H());
      g.FillRect(Cyan, IRECT(slot.L, slot.B - h, slot.R, slot.B));
    }
  }

private:
  Ticker mClock;
  float mLevel[8] = {};
  float mTarget[8] = {};
};

// Vumètre de sortie stéréo (crête, échelle -60..0 dB)
class OutputMeter final : public IControl
{
public:
  explicit OutputMeter(const IRECT& bounds) : IControl(bounds) { SetIgnoreMouse(true); }

  bool IsDirty() override { return true; }

  void OnMsgFromDelegate(int msgTag, int dataSize, const void* pData) override
  {
    ISenderData<kTelemetryChannels> d;
    if (!ReadTelemetry(msgTag, dataSize, pData, d)) return;
    mTarget[0] = std::max(mTarget[0], d.vals[9]);
    mTarget[1] = std::max(mTarget[1], d.vals[10]);
  }

  void Draw(IGraphics& g) override
  {
    const float dt = mClock.Tick();
    const float bw = (mRECT.W() - 6.f) / 2.f;
    for (int c = 0; c < 2; ++c)
    {
      mLevel[c] = std::max(mTarget[c], mLevel[c] * std::exp(-dt / 0.35f));
      mTarget[c] = 0.f;
      const float db = 20.f * std::log10(std::max(1e-5f, mLevel[c]));
      const float norm = std::clamp((db + 60.f) / 60.f, 0.f, 1.f);
      const IRECT slot(mRECT.L + c * (bw + 6.f), mRECT.T, mRECT.L + c * (bw + 6.f) + bw, mRECT.B);
      g.FillRoundRect(Panel2, slot, 3.f);
      const float h = norm * slot.H();
      if (h > 1.f)
        g.FillRoundRect(db > -3.f ? Orange : Cyan, IRECT(slot.L, slot.B - h, slot.R, slot.B), 3.f);
    }
  }

private:
  Ticker mClock;
  float mLevel[2] = {};
  float mTarget[2] = {};
};

} // namespace mcui
