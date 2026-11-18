#include "Motor/sound_pulse.h"
#include "Driver/tb67h450_base.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>

class DriverProbe : public TB67H450Base
{
public:
    int a = 0, b = 0;
    bool ap = false, am = false, bp = false, bm = false;
    void DacOutputVoltage(uint16_t x, uint16_t y) override { a = x; b = y; }
    void SetInputA(bool p, bool m) override { ap = p; am = m; }
    void SetInputB(bool p, bool m) override { bp = p; bm = m; }
};

int main()
{
    // 校验独立子板波形、定时、限流和取消，不访问硬件。
    SoundPulse pulse;
    assert(pulse.Start(399, 200, 100, 0, 1000) == 1);
    assert(pulse.Start(1000, 501, 100, 0, 1000) == 1);
    assert(pulse.Start(1000, 200, 251, 0, 2000) == 1);
    assert(pulse.Start(1000, 200, 250, 0, 500) == 1);
    assert(pulse.Start(1000, 200, 100, 0, 1000) == 0);
    assert(pulse.Start(1000, 200, 100, 0, 1000) == 2);
    int maximum = 0, peaks = 0, previous = 0, previous2 = 0;
    for (int i = 0; i < 4000; ++i) {
        const int value = pulse.Tick(true, 0);
        assert(value >= 0 && value <= 100);
        if (value > maximum) maximum = value;
        if (i > 100 && i < 3900 && previous > previous2 && previous >= value) ++peaks;
        previous2 = previous;
        previous = value;
    }
    assert(maximum >= 98 && peaks >= 185 && peaks <= 195);
    assert(pulse.Tick(true, 0) == 0 && pulse.state == SoundPulse::FINISHED);
    assert(pulse.Start(1000, 200, 100, 0, 1000) == 3);
    for (int i = 0; i < 20000; ++i) pulse.Tick(false, 0);
    assert(pulse.Start(1000, 200, 100, 0, 1000) == 0);
    pulse.Tick(true, 129);
    assert(pulse.state == SoundPulse::ABORTED);
    SoundPulse cancelled;
    assert(cancelled.Start(1000, 500, 100, 0, 1000) == 0);
    cancelled.Cancel();
    assert(cancelled.Tick(true, 0) == 0 && cancelled.remaining == 0);
    SoundPulse disabled;
    disabled.Start(1000, 200, 100, 0, 1000);
    disabled.Tick(false, 0);
    assert(disabled.state == SoundPulse::ABORTED);
    DriverProbe driver;
    for (unsigned angle = 0; angle < 1024; ++angle) {
        for (int torque : {-1000, -100, 0, 100, 1000}) {
            driver.SetExcitationVector(angle, torque, 250, 1000);
            assert(driver.a <= 1241 && driver.b <= 1241);
            const double a = driver.a * (driver.ap && !driver.am ? 1 : -1) / (5083.0 / 4096.0);
            const double b = driver.b * (driver.bp && !driver.bm ? 1 : -1) / (5083.0 / 4096.0);
            const double sine = sin_pi_m2[angle] / 4096.0;
            const double cosine = sin_pi_m2[(angle + 256) & 1023] / 4096.0;
            assert(std::abs(-a * sine + b * cosine - torque) < 3.0);
        }
    }
    std::puts("波形、时长、冷却、偏移取消及 5120 组驱动矢量检查通过");
}
