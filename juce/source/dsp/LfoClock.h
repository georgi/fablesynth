#pragma once
#include <cmath>

namespace fable {
// Preserve phase when the rate changes; a backwards clock is a transport restart.
class LfoClock {
public:
    void reset() { ready_ = false; }
    double update(double position, double rate) {
        if (!ready_ || position < position_) {
            cycles_ = position * rate;
            ready_ = true;
        } else {
            cycles_ += (position - position_) * rate_;
        }
        position_ = position;
        rate_ = rate;
        return cycles_;
    }
    double phase(double position, double rate) {
        const double c = update(position, rate);
        return c - std::floor(c);
    }
private:
    bool ready_ = false;
    double position_ = 0, rate_ = 0, cycles_ = 0;
};
}
