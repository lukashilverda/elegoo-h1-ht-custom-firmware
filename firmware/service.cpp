#include "service.hpp"

namespace h1::cold {

bool HeatMonitor::update(std::uint32_t now, int heater, bool energized, bool cyclePermitted) {
    constexpr int clearRise = 80;
    constexpr int drop = 50;
    constexpr int recoveryRise = 20;
    constexpr unsigned recoveryMs = 120000;
    if (!cyclePermitted) {
        reset();
        return false;
    }
    if (!energized) {
        if (now - lastEnergized_ > 500) tracking_ = false;
        return false;
    }
    lastEnergized_ = now;
    if (!observed_) {
        observed_ = true;
        initial_ = peak_ = heater;
    }
    if (heater > peak_) peak_ = heater;
    // A heater that has never warmed is diagnosed by the regular heat timeout.
    if (peak_ - initial_ < clearRise || peak_ < 700 || peak_ - heater < drop) {
        tracking_ = false;
        return false;
    }
    if (!tracking_ || heater >= recoveryBaseline_ + recoveryRise) {
        tracking_ = true;
        recoveryAt_ = now;
        recoveryBaseline_ = heater;
    }
    if (heater < recoveryBaseline_) recoveryBaseline_ = heater;
    return now - recoveryAt_ >= recoveryMs;
}

int HomeTemperature::update(std::uint32_t now, std::uint32_t sampleAt, int heater,
                            unsigned targetCelsius, bool drying, bool valid) {
    constexpr std::uint32_t second = 1000;
    if (!valid) {
        sampleCount_ = 0;
        return 0;
    }

    const bool first = sampleCount_ == 0;
    if (first || sampleAt != samples_[0].at) {
        if (sampleCount_ < samples_.size()) ++sampleCount_;
        for (auto i = sampleCount_ - 1; i > 0; --i) samples_[i] = samples_[i - 1];
        samples_[0] = {heater, sampleAt};
        sum_ = heater;
        unsigned i = 1;
        for (; i < sampleCount_ && sampleAt - samples_[i].at < second; ++i) sum_ += samples_[i].hundredths;
        sampleCount_ = i;
    }

    const auto elapsedSeconds = (now - displayedAt_) / second;
    if (!first && !elapsedSeconds) return displayed_;

    const int count = static_cast<int>(sampleCount_);
    int wanted = (sum_ + (sum_ >= 0 ? count / 2 : -count / 2)) / count;
    if (drying) {
        const int target = static_cast<int>(targetCelsius) * 100;
        // Compare the unrounded mean with the factory band edges.
        const int difference = sum_ - target * count;
        if (difference == -1100 * count) wanted = target;
        else if (difference > -1100 * count && difference <= 1100 * count) {
            if (difference <= -700 * count) wanted = target - 200;
            else if (difference <= -400 * count) wanted = target - 100;
            else if (difference <= 400 * count) wanted = target;
            else if (difference <= 700 * count) wanted = target + 100;
            else wanted = target + 200;
        }
    }

    if (!first) {
        const int limit = static_cast<int>(elapsedSeconds) * 100;
        const int difference = wanted - displayed_;
        if (difference > limit) wanted = displayed_ + limit;
        else if (difference < -limit) wanted = displayed_ - limit;
    }
    displayedAt_ = now;
    displayed_ = wanted;
    return displayed_;
}

bool ScreenIdle::update(std::uint32_t now, unsigned timeoutMinutes, bool keepAwake) {
    if (keepAwake || !timeoutMinutes || held_) lastUse_ = now;
    off_ = timeoutMinutes && !keepAwake && now - lastUse_ >= timeoutMinutes * 60000;
    return off_;
}

bool ScreenIdle::touch(std::uint32_t now, bool down) {
    held_ = down;
    if (!down) {
        if (swallowing_ && ++releases_ >= 2) swallowing_ = false;
        return false;
    }
    releases_ = 0;
    lastUse_ = now;
    if (off_) {
        off_ = false;
        swallowing_ = true;
    }
    return !swallowing_;
}

void History::add(std::uint32_t now, ChartSample sample) {
    if (count_ && now - at_ < interval_) return;
    if (count_ == capacity) {
        for (unsigned i = 0; i < capacity / 2; ++i) samples_[i] = samples_[i * 2];
        count_ /= 2;
        interval_ *= 2;
    }
    samples_[count_++] = sample;
    at_ = now;
}

void SelfTest::start(TestItem item, std::uint32_t now) {
    *this = {};
    full_ = item == TestItem::count;
    item_ = full_ ? TestItem::exhaust : item;
    results_[static_cast<unsigned>(item_)] = TestResult::running;
    since_ = now;
    playBuzzer_ = item_ == TestItem::buzzer;
}

void SelfTest::next(std::uint32_t now, TestResult result, const char* detail) {
    auto index = static_cast<unsigned>(item_);
    results_[index] = result;
    if (*detail) detail_ = detail;
    closing_ = baselineSet_ = false;
    if (!full_ || ++index == count) {
        item_ = TestItem::count;
        return;
    }
    item_ = static_cast<TestItem>(index);
    results_[index] = TestResult::running;
    since_ = now;
    playBuzzer_ = item_ == TestItem::buzzer;
}

void SelfTest::cancel() {
    if (active()) results_[static_cast<unsigned>(item_)] = TestResult::skipped;
    item_ = TestItem::count;
    playBuzzer_ = false;
}

void SelfTest::confirm(std::uint32_t now, bool passed) {
    if (active() && result(item_) == TestResult::manual) {
        next(now, passed ? TestResult::passed : TestResult::failed, "");
    }
}

void SelfTest::update(std::uint32_t now, const Inputs& in, unsigned reached, bool airflow, bool soundDone) {
    if (!active()) return;
    const auto elapsed = now - since_;
    switch (item_) {
    case TestItem::exhaust:
    case TestItem::inlet: {
        const auto mask = item_ == TestItem::exhaust ? 1u : 2u;
        if ((in.ventFaults & mask) || elapsed >= 12000) {
            next(now, TestResult::failed, "Vent could not complete both positions. Check the vent.");
        } else if (elapsed >= 1100 && (reached & mask)) {
            if (closing_) next(now, TestResult::passed, "");
            else {
                closing_ = true;
                since_ = now;
            }
        }
        break;
    }
    case TestItem::fan:
        if (airflow) next(now, TestResult::passed, "");
        else if (elapsed >= 3000) next(now, TestResult::failed, "Fan feedback was not detected.");
        break;
    case TestItem::heater:
        if (in.overTemperature || !in.thermistorValid || !in.chamberValid ||
            in.thermistorTenths >= 500 || (!baselineSet_ && in.thermistorTenths > 400)) {
            next(now, TestResult::failed, "Heater test needs valid sensors and a cool heater (40 C or below).");
        } else if (!airflow && (baselineSet_ || elapsed >= 3000)) {
            next(now, TestResult::failed, "Heater test stopped because fan feedback was missing.");
        } else if (airflow && !baselineSet_) {
            baseline_ = in.thermistorTenths;
            baselineSet_ = true;
            since_ = now;
        } else if (baselineSet_ && in.thermistorTenths >= baseline_ + 30) {
            next(now, TestResult::passed, "");
        } else if (elapsed >= 15000) {
            next(now, TestResult::failed, "The heater did not warm by 3 C during the test.");
        }
        break;
    case TestItem::rotation:
        if (elapsed >= 3000 && !airflow) next(now, TestResult::failed, "Rotation test needs fan feedback.");
        else if (elapsed >= 2000 && airflow) results_[static_cast<unsigned>(item_)] = TestResult::manual;
        if (elapsed >= 15000 && item_ == TestItem::rotation) next(now, TestResult::skipped, "");
        break;
    case TestItem::buzzer:
        if (!playBuzzer_ && soundDone) results_[static_cast<unsigned>(item_)] = TestResult::manual;
        if (elapsed >= 20000) next(now, TestResult::skipped, "");
        break;
    case TestItem::sensors:
        if (in.chamberValid && in.thermistorValid && in.humidityTenths >= 0) next(now, TestResult::passed, "");
        else if (elapsed >= 3000) next(now, TestResult::failed, "Valid chamber, heater and humidity readings were not available.");
        break;
    case TestItem::count: break;
    }
}

Outputs SelfTest::outputs() const {
    const bool vent = item_ == TestItem::exhaust || item_ == TestItem::inlet;
    const bool fan = item_ == TestItem::fan || item_ == TestItem::heater || item_ == TestItem::rotation;
    return {vent ? (closing_ ? VentTarget::closed : VentTarget::open) : VentTarget::hold,
            fan, item_ == TestItem::rotation, false, 0,
            static_cast<std::uint8_t>(item_ == TestItem::exhaust ? 1u : 2u),
            item_ == TestItem::heater && baselineSet_};
}

bool SelfTest::heatRequested(const Inputs& in) const {
    return item_ == TestItem::heater && baselineSet_ && !in.overTemperature &&
           in.thermistorValid && in.chamberValid && in.thermistorTenths < 500;
}

bool SelfTest::takeBuzzer() {
    const bool play = playBuzzer_;
    playBuzzer_ = false;
    return play;
}

}  // namespace h1::cold
