#pragma once
#include <math.h>

// ESP temperature sensors have overlapping hardware ranges. A failed read must
// never become a valid thermal sample, even if the driver wrote a finite value.
template<class Driver> class TemperatureReader {
public:
    explicit TemperatureReader(Driver& driver) : driver_(driver) {}
    float read() {
        float value = NAN;
        if (active_ && driver_.read(value) && isfinite(value)) return value;
        for (int offset = active_ ? 1 : 0; offset < 4; ++offset) {
            const int range = (range_ + offset) % 4;
            if (driver_.select(Min[range], Max[range]) &&
                driver_.read(value) && isfinite(value)) {
                range_ = range;
                active_ = true;
                return value;
            }
        }
        active_ = false;
        return NAN;
    }
private:
    static constexpr int Min[4] = {20, 50, -10, -40};
    static constexpr int Max[4] = {100, 125, 80, 20};
    Driver& driver_;
    int range_ = 0;
    bool active_ = false;
};
