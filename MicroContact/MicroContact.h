#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "ISender.h"
#include "MicroContact_Params.h" // EParams, kNumPresets, conversions (C++ pur)

enum ECtrlTags
{
  kCtrlTagSignal = 0,
  kCtrlTagScope,
  kCtrlTagModes,
  kCtrlTagMeter,
  kCtrlTagKeyboard,
  kNumCtrlTags
};

// Télémétrie moteur -> interface : [0] transitoire, [1..8] modes, [9..10] sortie G/D, [11] corps
constexpr int kTelemetryChannels = 12;

using namespace iplug;

class MicroContact final : public iplug::Plugin
{
public:
  MicroContact(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnIdle() override;

private:
  void HandleMidi(const IMidiMsg& msg);
  mc::Params ReadParams();
  void PushTelemetry();

  mc::Engine mEngine;
  IMidiQueue mMidiQueue;
  ISender<kTelemetryChannels, 128> mSender;
#endif
};
