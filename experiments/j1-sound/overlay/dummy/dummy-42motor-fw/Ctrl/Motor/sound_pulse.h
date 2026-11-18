#pragma once

#include <cstdint>
#include "Driver/sin_map.h"

// 仅供短音可行性实验；不改变运动目标，不保存任何参数。
class SoundPulse
{
public:
    enum State : uint8_t { IDLE, PLAYING, FINISHED, CANCELLED, ABORTED };
    State state = IDLE;
    uint32_t remaining = 0;

    static bool Valid(uint32_t frequency, uint32_t duration, uint32_t amplitude)
    {
        return frequency >= 400 && frequency <= 2000 &&
               duration >= 50 && duration <= 500 && amplitude >= 25 && amplitude <= 250;
    }

    uint8_t Start(uint32_t frequency, uint32_t duration, uint32_t amplitude,
                  int32_t position, int32_t ratedCurrent)
    {
        if (!Valid(frequency, duration, amplitude) || ratedCurrent <= 0 ||
            ratedCurrent > 3000 || amplitude > static_cast<uint32_t>(ratedCurrent / 4)) return 1;
        if (state == PLAYING) return 2;
        if (cooldown) return 3;
        frequencyHz = frequency;
        amplitudeMa = amplitude;
        anchor = position;
        remaining = duration * 20;
        elapsed = phase = 0;
        state = PLAYING;
        return 0;
    }

    void Cancel(State reason = CANCELLED)
    {
        if (state != PLAYING) return;
        state = reason;
        remaining = 0;
        cooldown = 20000;
    }

    // 20 kHz 整数查表；5 ms 包络，单次最长 500 ms，结束后冷却 1 秒。
    int32_t Tick(bool permitted, int32_t position)
    {
        if (cooldown) --cooldown;
        if (state != PLAYING) return 0;
        const int64_t drift = static_cast<int64_t>(position) - anchor;
        if (!permitted || drift > 128 || drift < -128) {
            Cancel(ABORTED);
            return 0;
        }
        if (!remaining) {
            Cancel(FINISHED);
            return 0;
        }
        uint32_t ramp = elapsed < 100 ? elapsed : 100;
        if (remaining < ramp) ramp = remaining;
        const uint32_t index = phase * 1024 / 20000;
        const int32_t wave = (amplitudeMa * (4096 + sin_pi_m2[index])) / 8192;
        phase += frequencyHz;
        if (phase >= 20000) phase -= 20000;
        ++elapsed;
        --remaining;
        // 正向磁轴励磁，避免转矩轴往复激励；实际微小位移仍需实测。
        return wave * static_cast<int32_t>(ramp) / 100;
    }

private:
    uint32_t frequencyHz = 0, elapsed = 0, phase = 0, cooldown = 0;
    int32_t amplitudeMa = 0, anchor = 0;
};
