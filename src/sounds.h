// The launcher's interface sounds: short tones made in code when the launcher
// starts (no sound files), mixed on SDL's audio thread. Quiet by design.
#pragma once

namespace whopping::sound {

enum class Sound { kMove, kSelect, kBack, kToggle, kError, kNotify, kLaunch, kAchievement, kCount };

void Init();      // after SDL_Init; does nothing when no audio device opens
void Shutdown();
void Play(Sound s, float gain = 1.0f);
void SetEnabled(bool on);

}  // namespace whopping::sound
