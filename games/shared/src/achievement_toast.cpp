// Achievement notifications in the style of the TRG ports (from King Kong
// Recompiled's toast.cpp): a green orb with the achievement's icon grows in,
// unfolds into a bar with "Achievement unlocked" and the name, then folds away.
// A chime made in code plays with it (no sound file).
//
// Settings (the launcher's Achievements section): bk_achievement_toasts,
// bk_achievement_sound, bk_achievement_volume.

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#else
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>

#pragma comment(lib, "winmm.lib")

REXCVAR_DEFINE_BOOL(bk_achievement_toasts, true, "Collection", "Show a notification when an achievement unlocks");
REXCVAR_DEFINE_BOOL(bk_achievement_sound, true, "Collection", "Play the achievement chime");
REXCVAR_DEFINE_INT32(bk_achievement_volume, 80, "Collection", "Achievement chime volume in percent");

namespace bk {
namespace {

// Timeline of one notification, in seconds.
constexpr double kOrbIn = 0.30, kUnfold = 0.40, kHold = 4.6, kFold = 0.35, kOrbOut = 0.25;
constexpr double kTotal = kOrbIn + kUnfold + kHold + kFold + kOrbOut;
constexpr double kPi = 3.14159265358979323846;

float EaseOutBack(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.0f;
  return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}
float EaseInOut(float t) { return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2; }

// The chime: a soft low swell, then a rising pair of bell tones with a short echo (16 bit WAV in memory).
std::vector<uint8_t> MakeChimeWav(float volume) {
  constexpr int kRate = 44100;
  std::vector<float> mix(size_t(kRate * 1.6), 0.0f);
  auto bell = [&](double start, double freq, double gain, double decay) {
    const size_t s0 = size_t(start * kRate);
    for (size_t i = s0; i < mix.size(); ++i) {
      const double t = double(i - s0) / kRate;
      const double env = std::min(1.0, t / 0.006) * std::exp(-t / decay);
      const double v = std::sin(2 * kPi * freq * t) + 0.45 * std::sin(2 * kPi * freq * 2.0 * t) * std::exp(-t / 0.15) +
                       0.18 * std::sin(2 * kPi * freq * 3.01 * t) * std::exp(-t / 0.08);
      mix[i] += float(gain * env * v);
    }
  };
  bell(0.00, 392.00, 0.22, 0.30);
  bell(0.02, 783.99, 0.55, 0.40);
  bell(0.13, 1174.66, 0.60, 0.55);
  bell(0.13, 1567.98, 0.18, 0.30);
  const size_t delay = size_t(0.17 * kRate);
  for (size_t i = mix.size(); i-- > delay;) mix[i] += 0.28f * mix[i - delay];
  float peak = 1e-6f;
  for (float v : mix) peak = std::max(peak, std::abs(v));
  const float scale = 0.70f * std::clamp(volume, 0.0f, 1.0f) / peak;
  const uint32_t data_bytes = uint32_t(mix.size() * 2);
  std::vector<uint8_t> wav(44 + data_bytes);
  auto put32 = [&](size_t o, uint32_t v) { std::memcpy(&wav[o], &v, 4); };
  auto put16 = [&](size_t o, uint16_t v) { std::memcpy(&wav[o], &v, 2); };
  std::memcpy(&wav[0], "RIFF", 4);
  put32(4, 36 + data_bytes);
  std::memcpy(&wav[8], "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);
  put16(22, 1);
  put32(24, kRate);
  put32(28, kRate * 2);
  put16(32, 2);
  put16(34, 16);
  std::memcpy(&wav[36], "data", 4);
  put32(40, data_bytes);
  for (size_t i = 0; i < mix.size(); ++i)
    put16(44 + i * 2, uint16_t(int16_t(std::clamp(mix[i] * scale, -1.0f, 1.0f) * 32767.0f)));
  return wav;
}

void PlayChime() {
  if (!REXCVAR_GET(bk_achievement_sound)) return;
  // PlaySound reads the memory while it plays, so the buffer stays alive until the volume changes.
  static std::vector<uint8_t> wav;
  static int wav_volume = -1;
  const int volume = std::clamp(REXCVAR_GET(bk_achievement_volume), 0, 100);
  if (volume != wav_volume) {
#if defined(_WIN32)
    PlaySoundW(nullptr, nullptr, 0);
#endif
    wav = MakeChimeWav(volume / 100.0f);
    wav_volume = volume;
  }
#if defined(_WIN32)
  PlaySoundW(reinterpret_cast<LPCWSTR>(wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
  // SDL's audio (the game's own audio is SDL's on Linux): the samples after the 44 byte WAV header, on a stream of
  // their own.
  static SDL_AudioStream* stream = [] {
    if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    const SDL_AudioSpec spec{SDL_AUDIO_S16LE, 1, 44100};  // as MakeChimeWav writes it
    return SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  }();
  if (!stream) return;
  SDL_ClearAudioStream(stream);
  SDL_PutAudioStreamData(stream, wav.data() + 44, int(wav.size() - 44));
  SDL_ResumeAudioStreamDevice(stream);
#endif
}

class AchievementToast final : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementToast(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, rex::Runtime* runtime)
      : AchievementNotificationDialog(drawer), icons_(immediate, runtime) {}

  void Push(const rex::system::AchievementEvent& event) override {
    if (!REXCVAR_GET(bk_achievement_toasts)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({event, -1.0});
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    Toast toast;
    const double now = ImGui::GetTime();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (queue_.empty()) return;
      if (queue_.front().started < 0) {
        queue_.front().started = now;
        PlayChime();
      }
      if (now - queue_.front().started > kTotal) {
        queue_.pop_front();
        return;
      }
      toast = queue_.front();
    }
    const float t = float(now - toast.started);
    const float s = std::max(0.75f, io.DisplaySize.y / 900.0f);
    ImFont* font = ImGui::GetFont();

    float orb = 1.0f, unfold = 1.0f;
    if (t < kOrbIn) {
      orb = EaseOutBack(t / float(kOrbIn));
      unfold = 0.0f;
    } else if (t < kOrbIn + kUnfold) {
      unfold = EaseInOut((t - float(kOrbIn)) / float(kUnfold));
    } else if (t > kTotal - kOrbOut) {
      unfold = 0.0f;
      orb = 1.0f - EaseInOut((t - float(kTotal - kOrbOut)) / float(kOrbOut));
    } else if (t > kTotal - kOrbOut - kFold) {
      unfold = 1.0f - EaseInOut((t - float(kTotal - kOrbOut - kFold)) / float(kFold));
    }

    const rex::system::AchievementInfo& a = toast.event.achievement;
    const std::string line1 = "Achievement unlocked";
    const std::string line2 = a.gamerscore ? std::to_string(a.gamerscore) + "G   " + a.label : a.label;
    const float h = 70 * s, r = h * 0.5f;
    const float text_w = std::max(font->CalcTextSizeA(16 * s, FLT_MAX, 0, line1.c_str()).x,
                                  font->CalcTextSizeA(20 * s, FLT_MAX, 0, line2.c_str()).x);
    const float full_w = h + 22 * s + text_w + 34 * s;
    const float w = h + (full_w - h) * unfold;
    const ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.86f);
    const float x0 = center.x - full_w * 0.5f, y0 = center.y - r;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (unfold > 0.0f) {
      const ImVec2 b0(x0, y0), b1(x0 + w, y0 + h);
      dl->AddRectFilled(ImVec2(b0.x + 3 * s, b0.y + 5 * s), ImVec2(b1.x + 3 * s, b1.y + 5 * s), IM_COL32(0, 0, 0, 90),
                        r);
      dl->AddRectFilled(b0, b1, IM_COL32(30, 30, 34, 245), r);
      dl->AddRectFilled(ImVec2(b0.x + 2 * s, b0.y + 2 * s), ImVec2(b1.x - 2 * s, b0.y + h * 0.5f),
                        IM_COL32(255, 255, 255, 16), r - 2 * s, ImDrawFlags_RoundCornersTop);
      dl->AddRect(b0, b1, IM_COL32(255, 255, 255, 40), r, 0, 1.2f * s);
      const float alpha = std::clamp((unfold - 0.6f) / 0.4f, 0.0f, 1.0f);
      if (alpha > 0.0f) {
        const float tx = x0 + h + 18 * s;
        dl->PushClipRect(b0, ImVec2(b1.x - 14 * s, b1.y), true);
        dl->AddText(font, 16 * s, ImVec2(tx, y0 + 12 * s), IM_COL32(200, 205, 210, int(255 * alpha)), line1.c_str());
        dl->AddText(font, 20 * s, ImVec2(tx, y0 + 33 * s), IM_COL32(255, 255, 255, int(255 * alpha)), line2.c_str());
        dl->PopClipRect();
      }
    }
    if (orb > 0.0f) {
      const ImVec2 c(x0 + r, y0 + r);
      const float rr = (r + 4 * s) * orb;
      dl->AddCircleFilled(c, rr + 2 * s, IM_COL32(0, 0, 0, 80), 48);
      dl->AddCircleFilled(c, rr, IM_COL32(124, 194, 66, 255), 48);
      dl->AddCircleFilled(c, rr * 0.86f, IM_COL32(18, 22, 18, 255), 48);
      if (rex::ui::ImmediateTexture* icon = icons_.GetIcon(a)) {
        const float ir = rr * 0.80f;
        dl->AddImageRounded(ImTextureRef(reinterpret_cast<ImTextureID>(icon)), ImVec2(c.x - ir, c.y - ir),
                            ImVec2(c.x + ir, c.y + ir), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ir);
      }
      dl->AddCircle(c, rr, IM_COL32(190, 240, 140, 180), 48, 1.5f * s);
    }
  }

 private:
  struct Toast {
    rex::system::AchievementEvent event;
    double started = -1.0;  // ImGui time when it became visible
  };
  rex::ui::AchievementIconCache icons_;
  std::mutex mutex_;
  std::deque<Toast> queue_;
};

}  // namespace

std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementToast(rex::ui::ImGuiDrawer* drawer,
                                                                               rex::ui::ImmediateDrawer* immediate,
                                                                               rex::Runtime* runtime) {
  return std::make_unique<AchievementToast>(drawer, immediate, runtime);
}

}  // namespace bk
