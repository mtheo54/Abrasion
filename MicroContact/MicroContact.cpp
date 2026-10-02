#include "MicroContact.h"
#include "IPlug_include_in_plug_src.h"
#include <algorithm>
#include <cmath>

#if IPLUG_EDITOR
#include "IControls.h"
#include "MicroContact_UI.h"
#endif

MicroContact::MicroContact(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  // Les défauts sont ceux du premier cas d'usage (808 & Sub) : une seule source de vérité.
  const mc::Params& d = mc::UseCases()[0].p;
  auto def = [&d](int idx) { return ToPluginValue(idx, d); };
  auto defInt = [&d](int idx) { return static_cast<int>(std::lround(ToPluginValue(idx, d))); };

  // Macro-contrôles
  GetParam(kParamSnap)->InitPercentage("Bite / Snap", def(kParamSnap));
  GetParam(kParamBody)->InitPercentage("Body Weight", def(kParamBody));
  GetParam(kParamErosion)->InitPercentage("Erosion / Wear", def(kParamErosion));
  GetParam(kParamSpace)->InitPercentage("Space Damping", def(kParamSpace));

  // Excitation
  GetParam(kParamExciter)->InitEnum("Excitation", defInt(kParamExciter),
    {"Arc electrique", "Friction roche", "Craquelure verre", "Clic mecanique"});
  GetParam(kParamExcLevel)->InitPercentage("Niveau excitation", def(kParamExcLevel));
  GetParam(kParamVelSens)->InitPercentage("Velocite -> pression", def(kParamVelSens));

  // Corps tonal
  GetParam(kParamOsc1Shape)->InitEnum("OSC1 forme", defInt(kParamOsc1Shape), {"Sinus", "Triangle sature", "Wavetable"});
  GetParam(kParamOsc1Octave)->InitInt("OSC1 octave", defInt(kParamOsc1Octave), -3, 1, "oct");
  GetParam(kParamOsc1Level)->InitPercentage("OSC1 niveau", def(kParamOsc1Level));
  GetParam(kParamOsc2Shape)->InitEnum("OSC2 forme", defInt(kParamOsc2Shape), {"Sinus", "Triangle sature", "Wavetable"});
  GetParam(kParamOsc2Semi)->InitInt("OSC2 demi-tons", defInt(kParamOsc2Semi), -24, 24, "st");
  GetParam(kParamOsc2Detune)->InitDouble("OSC2 desaccord", def(kParamOsc2Detune), -50., 50., 0.1, "ct");
  GetParam(kParamOsc2Level)->InitPercentage("OSC2 niveau", def(kParamOsc2Level));
  GetParam(kParamPhaseReset)->InitBool("Alignement de phase", def(kParamPhaseReset) > 0.5);
  GetParam(kParamPitchDrop)->InitDouble("Drop", def(kParamPitchDrop), 0., 36., 0.1, "st");
  GetParam(kParamDropTime)->InitDouble("Duree du drop", def(kParamDropTime), 5., 500., 0.1, "ms",
                                       0, "", IParam::ShapePowCurve(2.));

  // Matrice de friction
  GetParam(kParamMaterial)->InitEnum("Matiere", defInt(kParamMaterial), {"Metal", "Quartz", "Membrane", "Mercure"});
  GetParam(kParamCoupling)->InitPercentage("Couplage", def(kParamCoupling));
  GetParam(kParamResonance)->InitPercentage("Resonance", def(kParamResonance));
  GetParam(kParamMatLevel)->InitPercentage("Niveau matiere", def(kParamMatLevel));

  // Enveloppe
  GetParam(kParamAttack)->InitDouble("Attaque", def(kParamAttack), 0.5, 2000., 0.1, "ms", 0, "", IParam::ShapePowCurve(3.));
  GetParam(kParamDecay)->InitDouble("Decroissance", def(kParamDecay), 5., 5000., 0.1, "ms", 0, "", IParam::ShapePowCurve(3.));
  GetParam(kParamSustain)->InitPercentage("Maintien", def(kParamSustain));
  GetParam(kParamRelease)->InitDouble("Relachement", def(kParamRelease), 5., 5000., 0.1, "ms", 0, "", IParam::ShapePowCurve(3.));

  GetParam(kParamGain)->InitGain("Volume", def(kParamGain), -60., 6.);

  // Presets usine = cas d'usage. FL Studio les liste dans le menu de presets du plugin.
  for (const auto& uc : mc::UseCases())
  {
    for (int i = 0; i < kNumParams; ++i) GetParam(i)->Set(ToPluginValue(i, uc.p));
    IByteChunk chunk;
    SerializeParams(chunk);
    MakePresetFromChunk(uc.name, chunk);
  }
  for (int i = 0; i < kNumParams; ++i) GetParam(i)->SetToDefault();

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    using namespace mcui;

    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(Bg);
    pGraphics->EnableMouseOver(true);
    pGraphics->EnableMultiTouch(true);
#ifdef ROBOTO_FN
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
#endif

    const IVStyle orange = MakeStyle(Orange);
    const IVStyle cyan = MakeStyle(Cyan);
    const IVStyle neutral = MakeStyle(Muted);

    auto knob = [&](const IRECT& r, int param, const char* label, const IVStyle& style) {
      pGraphics->AttachControl(new IVKnobControl(r, param, label, style, true));
    };
    auto text = [&](const IRECT& r, const char* str, float size, const IColor& color) {
      pGraphics->AttachControl(new ITextControl(r, str, IText(size, color, kFont, EAlign::Near)));
    };

    // En-tête : logo Abrasion + cas d'usage
    pGraphics->AttachControl(new AbrasionLogo(IRECT(24.f, 16.f, 560.f, 58.f)));
    static const char* caseLabels[kNumPresets] = {"808 & Sub", "Percussions", "Plucks & Keys"};
    for (int i = 0; i < kNumPresets; ++i)
    {
      const float x = 602.f + i * 162.f;
      pGraphics->AttachControl(new IVButtonControl(IRECT(x, 20.f, x + 150.f, 54.f),
        [i](IControl* pCaller) {
          SplashClickActionFunc(pCaller);
          mcui::ApplyUseCase(*pCaller->GetDelegate(), i);
        }, caseLabels[i], cyan));
    }

    // Chemin du signal
    pGraphics->AttachControl(new SignalPathControl(IRECT(24.f, 80.f, 1076.f, 136.f)), kCtrlTagSignal);

    // Macro-contrôles
    const IRECT macros(24.f, 148.f, 1076.f, 288.f);
    knob(macros.GetGridCell(0, 0, 1, 4).GetPadded(-30.f, 0.f, -30.f, 0.f), kParamSnap, "Bite / Snap", orange);
    knob(macros.GetGridCell(0, 1, 1, 4).GetPadded(-30.f, 0.f, -30.f, 0.f), kParamBody, "Body Weight", cyan);
    knob(macros.GetGridCell(0, 2, 1, 4).GetPadded(-30.f, 0.f, -30.f, 0.f), kParamErosion, "Erosion / Wear", cyan);
    knob(macros.GetGridCell(0, 3, 1, 4).GetPadded(-30.f, 0.f, -30.f, 0.f), kParamSpace, "Space Damping", neutral);

    // Moteur d'excitation
    pGraphics->AttachControl(new PanelControl(IRECT(24.f, 300.f, 540.f, 586.f), "Moteur d'excitation", Orange, "non-linéaire"));
    pGraphics->AttachControl(new IVTabSwitchControl(IRECT(40.f, 340.f, 524.f, 382.f), kParamExciter,
      {"Arc", "Roche", "Verre", "Clic"}, "", orange));
    knob(IRECT(40.f, 396.f, 140.f, 486.f), kParamExcLevel, "Niveau", orange);
    knob(IRECT(150.f, 396.f, 250.f, 486.f), kParamVelSens, "Vélocité", orange);
    text(IRECT(40.f, 500.f, 250.f, 520.f), "Suivi de hauteur actif", 11.f, Muted);
    text(IRECT(40.f, 520.f, 250.f, 540.f), "Synthèse procédurale, sans échantillons", 11.f, Muted);
    pGraphics->AttachControl(new TransientScope(IRECT(266.f, 396.f, 524.f, 570.f)), kCtrlTagScope);

    // Corps tonal
    pGraphics->AttachControl(new PanelControl(IRECT(560.f, 300.f, 1076.f, 586.f), "Corps tonal", Cyan, "précision"));
    text(IRECT(576.f, 352.f, 630.f, 372.f), "OSC 1", 13.f, Text);
    pGraphics->AttachControl(new IVTabSwitchControl(IRECT(632.f, 344.f, 834.f, 380.f), kParamOsc1Shape,
      {"Sinus", "Tri sat.", "Table"}, "", cyan));
    knob(IRECT(844.f, 334.f, 914.f, 404.f), kParamOsc1Octave, "Octave", cyan);
    knob(IRECT(920.f, 334.f, 990.f, 404.f), kParamOsc1Level, "Niveau", cyan);

    text(IRECT(576.f, 426.f, 630.f, 446.f), "OSC 2", 13.f, Text);
    pGraphics->AttachControl(new IVTabSwitchControl(IRECT(632.f, 418.f, 834.f, 454.f), kParamOsc2Shape,
      {"Sinus", "Tri sat.", "Table"}, "", cyan));
    knob(IRECT(844.f, 408.f, 914.f, 478.f), kParamOsc2Semi, "Demi-tons", cyan);
    knob(IRECT(920.f, 408.f, 990.f, 478.f), kParamOsc2Detune, "Désaccord", cyan);
    knob(IRECT(996.f, 408.f, 1066.f, 478.f), kParamOsc2Level, "Niveau", cyan);

    pGraphics->AttachControl(new IVToggleControl(IRECT(576.f, 500.f, 834.f, 540.f), kParamPhaseReset,
      "", cyan, "Phase libre", "Phase alignée (zero-crossing)"));
    knob(IRECT(844.f, 490.f, 914.f, 570.f), kParamPitchDrop, "Drop", cyan);
    knob(IRECT(920.f, 490.f, 990.f, 570.f), kParamDropTime, "Durée", cyan);

    // Matrice de friction
    pGraphics->AttachControl(new PanelControl(IRECT(24.f, 598.f, 640.f, 728.f), "Matrice de friction", Cyan, "< 100 Hz protégé"));
    pGraphics->AttachControl(new IVTabSwitchControl(IRECT(40.f, 636.f, 400.f, 676.f), kParamMaterial,
      {"Métal", "Quartz", "Membrane", "Mercure"}, "", cyan));
    pGraphics->AttachControl(new ModeBars(IRECT(40.f, 686.f, 400.f, 714.f)), kCtrlTagModes);
    knob(IRECT(412.f, 628.f, 482.f, 716.f), kParamCoupling, "Couplage", cyan);
    knob(IRECT(486.f, 628.f, 556.f, 716.f), kParamResonance, "Résonance", cyan);
    knob(IRECT(560.f, 628.f, 630.f, 716.f), kParamMatLevel, "Niveau", cyan);

    // Enveloppe et sortie
    pGraphics->AttachControl(new PanelControl(IRECT(656.f, 598.f, 1076.f, 728.f), "Enveloppe & sortie", Muted));
    knob(IRECT(668.f, 628.f, 732.f, 716.f), kParamAttack, "A", neutral);
    knob(IRECT(734.f, 628.f, 798.f, 716.f), kParamDecay, "D", neutral);
    knob(IRECT(800.f, 628.f, 864.f, 716.f), kParamSustain, "S", neutral);
    knob(IRECT(866.f, 628.f, 930.f, 716.f), kParamRelease, "R", neutral);
    knob(IRECT(940.f, 628.f, 1010.f, 716.f), kParamGain, "Volume", neutral);
    pGraphics->AttachControl(new OutputMeter(IRECT(1024.f, 634.f, 1060.f, 714.f)), kCtrlTagMeter);

    // Clavier
    pGraphics->AttachControl(new IVKeyboardControl(IRECT(24.f, 740.f, 1076.f, 836.f), 36, 84), kCtrlTagKeyboard);
    pGraphics->SetQwertyMidiKeyHandlerFunc([pGraphics](const IMidiMsg& msg) {
      if (auto* kb = pGraphics->GetControlWithTag(kCtrlTagKeyboard))
        kb->As<IVKeyboardControl>()->SetNoteFromMidi(msg.NoteNumber(), msg.StatusMsg() == IMidiMsg::kNoteOn);
    });
  };
#endif
}

#if IPLUG_DSP
void MicroContact::OnReset()
{
  mEngine.Prepare(GetSampleRate());
  mMidiQueue.Resize(GetBlockSize());
}

mc::Params MicroContact::ReadParams()
{
  // Instantané des paramètres en début de bloc : le moteur ne voit jamais un
  // réglage modifié par l'interface au milieu d'un bloc.
  mc::Params p;
  for (int i = 0; i < kNumParams; ++i) FromPluginValue(i, GetParam(i)->Value(), p);
  return p;
}

void MicroContact::ProcessMidiMsg(const IMidiMsg& msg)
{
  mMidiQueue.Add(msg);
}

void MicroContact::HandleMidi(const IMidiMsg& msg)
{
  switch (msg.StatusMsg())
  {
    case IMidiMsg::kNoteOn:
      if (msg.Velocity() > 0)
      {
        mEngine.NoteOn(msg.NoteNumber(), msg.Velocity() / 127.f);
        break;
      }
      [[fallthrough]];
    case IMidiMsg::kNoteOff:
      mEngine.NoteOff(msg.NoteNumber());
      break;
    case IMidiMsg::kPitchWheel:
      mEngine.SetPitchBend(static_cast<float>(msg.PitchWheel()) * 2.f); // ±2 demi-tons
      break;
    case IMidiMsg::kControlChange:
    {
      const int cc = static_cast<int>(msg.ControlChangeIdx());
      if (cc == IMidiMsg::kModWheel)           // molette : ajoute de l'usure (Erosion)
        mEngine.SetModWheel(static_cast<float>(msg.ControlChange(IMidiMsg::kModWheel)));
      else if (cc == IMidiMsg::kSustainOnOff)
        mEngine.SetSustain(msg.ControlChange(IMidiMsg::kSustainOnOff) >= 0.5);
      else if (cc == 120 || cc == 123)         // All Sound Off / All Notes Off
        mEngine.AllNotesOff();
      break;
    }
    default:
      break;
  }
}

void MicroContact::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  const int nChans = NOutChansConnected();
  if (nChans < 1) { mMidiQueue.Flush(nFrames); return; }

  mEngine.SetParams(ReadParams());

  // Rendu découpé aux positions exactes des événements MIDI
  int pos = 0;
  while (!mMidiQueue.Empty())
  {
    IMidiMsg& msg = mMidiQueue.Peek();
    if (msg.mOffset >= nFrames) break;
    const int offset = std::max(pos, msg.mOffset);
    if (offset > pos) { mEngine.Render(outputs, nChans, pos, offset); pos = offset; }
    HandleMidi(msg);
    mMidiQueue.Remove();
  }
  mEngine.Render(outputs, nChans, pos, nFrames);
  mMidiQueue.Flush(nFrames);

  PushTelemetry();
}

void MicroContact::PushTelemetry()
{
  mc::Telemetry t;
  if (!mEngine.TakeTelemetry(t)) return; // ~100 paquets/s, indépendamment du buffer de l'hôte

  float vals[kTelemetryChannels];
  vals[0] = t.transient;
  for (int i = 0; i < mc::kNumModes; ++i) vals[1 + i] = t.modes[i];
  vals[9] = t.outL;
  vals[10] = t.outR;
  vals[11] = t.body;

  for (int tag : {kCtrlTagSignal, kCtrlTagScope, kCtrlTagModes, kCtrlTagMeter})
  {
    ISenderData<kTelemetryChannels> d(tag, kTelemetryChannels, 0);
    std::copy(vals, vals + kTelemetryChannels, d.vals.begin());
    mSender.PushData(d);
  }
}

void MicroContact::OnIdle()
{
  mSender.TransmitData(*this);
}
#endif
