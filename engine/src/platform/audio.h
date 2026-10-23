#pragma once

#include <filesystem>
#include <string>

#include "core/types.h"

// Sound output. The SPU runs on emulated time: every vblank the runtime calls
// audio_vblank, which produces exactly the field's share of 44.1 kHz samples
// (44100 * 1001 / 60000, carried as a fraction), clocking the sound driver's root
// counter between them, and hands them to an SDL3 audio stream and/or a WAV file.
// The stream is kept a few fields deep: its playback rate is nudged (by at most
// 0.5%) to hold the queue there, so the host's display clock and audio clock can
// drift apart without the latency growing.
namespace ss::platform {

struct AudioOptions {
  bool device = false;          // open the default playback device
  std::filesystem::path wav;    // dump everything to this WAV file (empty: no dump)
  // Master volume of the device's output, 0-100 (config.ini "volume"); the slider is
  // perceptual (gain = (volume / 100)^2, so 50 is about -12 dB). The WAV dump always
  // gets the SPU's output unchanged.
  int volume = 100;
};

// Returns false (with 'error') if the device or the file can't be opened; the SPU
// still runs and audio_vblank still works then, it just outputs less.
bool audio_open(const AudioOptions& options, std::string* error);
void audio_close();  // finishes the WAV header and closes the device
void audio_vblank();  // Runtime::audio_tick

}  // namespace ss::platform
