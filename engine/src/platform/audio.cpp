#include "platform/audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "runtime/libspu.h"

namespace ss::platform {
namespace {

constexpr int kRate = 44100;
// Samples per NTSC field: 44100 / (60000 / 1001).
constexpr u64 kFieldNumerator = 44100ull * 1001;
constexpr u64 kFieldDenominator = 60000;
constexpr int kTargetFrames = 3 * 736;  // the stream's depth we aim for (~50 ms)

struct State {
  SDL_AudioStream* stream = nullptr;
  std::FILE* wav = nullptr;
  u64 wav_frames = 0;
  u64 remainder = 0;  // fraction of a sample carried between fields
  std::vector<s16> buffer;
  bool primed = false;
  // SS_AUDIO_STATS: the stream's depth (frames queued before each push), reported at the end.
  bool stats = false;
  u64 depth_sum = 0, depth_count = 0;
  int depth_min = 1 << 30, depth_max = 0, refills = 0, drops = 0;
  ~State();
};
State g;

void write_le(std::FILE* f, u32 v, int bytes) {
  for (int i = 0; i < bytes; ++i) std::fputc(static_cast<int>((v >> (8 * i)) & 0xFF), f);
}

void write_wav_header(std::FILE* f, u64 frames) {
  const u32 data_bytes = static_cast<u32>(std::min<u64>(frames * 4, 0xFFFFFFF0ull));
  std::fseek(f, 0, SEEK_SET);
  std::fputs("RIFF", f);
  write_le(f, 36 + data_bytes, 4);
  std::fputs("WAVEfmt ", f);
  write_le(f, 16, 4);
  write_le(f, 1, 2);  // PCM
  write_le(f, 2, 2);  // stereo
  write_le(f, kRate, 4);
  write_le(f, kRate * 4, 4);
  write_le(f, 4, 2);
  write_le(f, 16, 2);
  std::fputs("data", f);
  write_le(f, data_bytes, 4);
}

void close_wav() {
  if (!g.wav) return;
  write_wav_header(g.wav, g.wav_frames);
  std::fclose(g.wav);
  g.wav = nullptr;
}

State::~State() { close_wav(); }

}  // namespace

bool audio_open(const AudioOptions& options, std::string* error) {
  bool ok = true;
  if (!options.wav.empty()) {
    g.wav = std::fopen(options.wav.string().c_str(), "wb");
    if (g.wav) {
      write_wav_header(g.wav, 0);
    } else {
      ok = false;
      if (error) *error = "cannot write " + options.wav.string();
    }
  }
  if (options.device) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      if (error) *error = std::string("SDL audio: ") + SDL_GetError();
      return false;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, kRate};
    g.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g.stream) {
      if (error) *error = std::string("SDL audio device: ") + SDL_GetError();
      return false;
    }
    const float level = static_cast<float>(std::clamp(options.volume, 0, 100)) / 100.0f;
    SDL_SetAudioStreamGain(g.stream, level * level);
    SDL_ResumeAudioStreamDevice(g.stream);
    g.stats = std::getenv("SS_AUDIO_STATS") != nullptr;
  }
  return ok;
}

void audio_close() {
  if (g.stats && g.depth_count) {
    std::fprintf(stderr, "[audio] stream depth: mean %.1f ms, min %.1f ms, max %.1f ms over %llu fields; %d restarts, %d drops\n",
                 g.depth_sum * 1000.0 / g.depth_count / kRate, g.depth_min * 1000.0 / kRate, g.depth_max * 1000.0 / kRate,
                 static_cast<unsigned long long>(g.depth_count), g.refills, g.drops);
  }
  if (g.stream) {
    SDL_DestroyAudioStream(g.stream);
    g.stream = nullptr;
  }
  close_wav();
}

void audio_pause(bool paused) {
  if (g.stream == nullptr) return;
  if (paused) SDL_PauseAudioStreamDevice(g.stream);
  else SDL_ResumeAudioStreamDevice(g.stream);
}

void audio_vblank() {
  g.remainder += kFieldNumerator;
  const size_t frames = static_cast<size_t>(g.remainder / kFieldDenominator);
  g.remainder %= kFieldDenominator;
  g.buffer.resize(frames * 2);
  psyq::spu_run(g.buffer.data(), frames);

  if (g.wav) {
    std::fwrite(g.buffer.data(), sizeof(s16), g.buffer.size(), g.wav);
    g.wav_frames += frames;
  }
  if (g.stream) {
    const int queued = SDL_GetAudioStreamQueued(g.stream) / 4;
    if (g.primed) {
      g.depth_sum += static_cast<u64>(queued);
      ++g.depth_count;
      g.depth_min = std::min(g.depth_min, queued);
      g.depth_max = std::max(g.depth_max, queued);
    }
    if (!g.primed || queued == 0) {
      if (g.primed) ++g.refills;
      // Start (or restart after an underrun) a little ahead with silence.
      std::vector<s16> silence(static_cast<size_t>(kTargetFrames) * 2, 0);
      SDL_PutAudioStreamData(g.stream, silence.data(), static_cast<int>(silence.size() * sizeof(s16)));
      g.primed = true;
    } else if (queued > 4 * kTargetFrames) {
      SDL_ClearAudioStream(g.stream);  // far behind (a long stall): drop and resync
      g.primed = false;
      ++g.drops;
    }
    const float error = static_cast<float>(queued - kTargetFrames) / kTargetFrames;
    SDL_SetAudioStreamFrequencyRatio(g.stream, 1.0f + std::clamp(error * 0.005f, -0.005f, 0.005f));
    SDL_PutAudioStreamData(g.stream, g.buffer.data(), static_cast<int>(g.buffer.size() * sizeof(s16)));
  }
}

}  // namespace ss::platform
