#include "sounds.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace whopping::sound {
namespace {

constexpr int kRate = 48000;
constexpr float kPi = 3.14159265f;

struct Voice {
  const std::vector<float>* samples = nullptr;
  size_t pos = 0;
  float gain = 1.0f;
};

SDL_AudioDeviceID g_device = 0;
std::array<std::vector<float>, size_t(Sound::kCount)> g_sounds;
std::array<Voice, 8> g_voices;
std::atomic<bool> g_enabled{true};

// A tone gliding from f0 to f1 Hz with a soft attack and an exponential fade,
// a little second harmonic for warmth.
void Tone(std::vector<float>& out, size_t at, float f0, float f1, float seconds, float volume, float decay) {
  const size_t n = size_t(seconds * kRate);
  if (out.size() < at + n) out.resize(at + n, 0.0f);
  float phase = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float t = float(i) / kRate;
    const float f = f0 + (f1 - f0) * (float(i) / n);
    phase += 2.0f * kPi * f / kRate;
    const float attack = std::min(1.0f, t / 0.004f);
    const float env = attack * std::exp(-t * decay);
    out[at + i] += volume * env * (std::sin(phase) + 0.25f * std::sin(2.0f * phase));
  }
}

void Build() {
  auto& move = g_sounds[size_t(Sound::kMove)];
  Tone(move, 0, 1350.0f, 1100.0f, 0.045f, 0.10f, 70.0f);
  auto& select = g_sounds[size_t(Sound::kSelect)];
  Tone(select, 0, 660.0f, 660.0f, 0.09f, 0.13f, 30.0f);
  Tone(select, size_t(0.055f * kRate), 990.0f, 990.0f, 0.14f, 0.13f, 24.0f);
  auto& back = g_sounds[size_t(Sound::kBack)];
  Tone(back, 0, 760.0f, 470.0f, 0.12f, 0.12f, 26.0f);
  auto& toggle = g_sounds[size_t(Sound::kToggle)];
  Tone(toggle, 0, 1050.0f, 1050.0f, 0.05f, 0.11f, 55.0f);
  auto& error = g_sounds[size_t(Sound::kError)];
  Tone(error, 0, 240.0f, 200.0f, 0.18f, 0.16f, 14.0f);
  Tone(error, 0, 360.0f, 300.0f, 0.18f, 0.06f, 14.0f);
  auto& notify = g_sounds[size_t(Sound::kNotify)];  // a rising C major chime
  Tone(notify, 0, 1046.5f, 1046.5f, 0.7f, 0.10f, 6.0f);
  Tone(notify, size_t(0.09f * kRate), 1318.5f, 1318.5f, 0.7f, 0.10f, 6.0f);
  Tone(notify, size_t(0.18f * kRate), 1568.0f, 1568.0f, 0.8f, 0.11f, 5.0f);
  // The games' achievement chime (achievement_toast.cpp, from King Kong Recompiled): a soft low swell, then a
  // rising pair of bell tones and a short echo.
  auto& chime = g_sounds[size_t(Sound::kAchievement)];
  chime.assign(size_t(kRate * 1.6), 0.0f);
  auto bell = [&](double start, double freq, double gain, double decay) {
    const size_t s0 = size_t(start * kRate);
    for (size_t i = s0; i < chime.size(); ++i) {
      const double t = double(i - s0) / kRate;
      const double env = std::min(1.0, t / 0.006) * std::exp(-t / decay);
      const double v = std::sin(2 * kPi * freq * t) + 0.45 * std::sin(2 * kPi * freq * 2.0 * t) * std::exp(-t / 0.15) +
                       0.18 * std::sin(2 * kPi * freq * 3.01 * t) * std::exp(-t / 0.08);
      chime[i] += float(gain * env * v);
    }
  };
  bell(0.00, 392.00, 0.22, 0.30);
  bell(0.02, 783.99, 0.55, 0.40);
  bell(0.13, 1174.66, 0.60, 0.55);
  bell(0.13, 1567.98, 0.18, 0.30);
  const size_t delay = size_t(0.17 * kRate);
  for (size_t i = chime.size(); i-- > delay;) chime[i] += 0.28f * chime[i - delay];
  float peak = 1e-6f;
  for (float v : chime) peak = std::max(peak, std::abs(v));
  for (float& v : chime) v *= 0.70f / peak;

  auto& launch = g_sounds[size_t(Sound::kLaunch)];
  Tone(launch, 0, 420.0f, 1250.0f, 0.28f, 0.12f, 9.0f);
  Tone(launch, size_t(0.12f * kRate), 840.0f, 1680.0f, 0.3f, 0.06f, 9.0f);
}

void Mix(void*, Uint8* stream, int len) {
  float* out = reinterpret_cast<float*>(stream);
  const int frames = len / int(sizeof(float) * 2);
  std::fill(out, out + frames * 2, 0.0f);
  for (Voice& v : g_voices) {
    if (!v.samples) continue;
    for (int i = 0; i < frames && v.pos < v.samples->size(); ++i, ++v.pos) {
      const float s = (*v.samples)[v.pos] * v.gain;
      out[i * 2] += s;
      out[i * 2 + 1] += s;
    }
    if (v.pos >= v.samples->size()) v.samples = nullptr;
  }
  for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace

void Init() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
  Build();
  SDL_AudioSpec want{}, have{};
  want.freq = kRate;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 512;
  want.callback = &Mix;
  g_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (g_device) SDL_PauseAudioDevice(g_device, 0);
}

void Shutdown() {
  if (g_device) SDL_CloseAudioDevice(g_device);
  g_device = 0;
}

void SetEnabled(bool on) { g_enabled = on; }

void Play(Sound s, float gain) {
  if (!g_device || (!g_enabled && s != Sound::kAchievement)) return;  // the chime has its own setting
  SDL_LockAudioDevice(g_device);
  Voice* slot = &g_voices[0];
  for (Voice& v : g_voices) {
    if (!v.samples) {
      slot = &v;
      break;
    }
    if (v.pos > slot->pos) slot = &v;  // all busy: replace the one nearest its end
  }
  slot->samples = &g_sounds[size_t(s)];
  slot->pos = 0;
  slot->gain = gain;
  SDL_UnlockAudioDevice(g_device);
}

}  // namespace whopping::sound
