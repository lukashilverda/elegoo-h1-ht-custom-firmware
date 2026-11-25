#pragma once

#include "storage.hpp"

namespace h1::cold {

class HeatMonitor {
public:
    bool update(std::uint32_t now, int heaterTenths, bool energized, bool cyclePermitted);
    void reset() { *this = {}; }

private:
    bool observed_ = false;
    bool tracking_ = false;
    int initial_ = 0;
    int peak_ = 0;
    int recoveryBaseline_ = 0;
    std::uint32_t recoveryAt_ = 0;
    std::uint32_t lastEnergized_ = 0;
};

class HomeTemperature {
public:
    int update(std::uint32_t now, std::uint32_t sampleAt, int heaterHundredths,
               unsigned targetCelsius, bool drying, bool valid);

private:
    struct Sample {
        int hundredths = 0;
        std::uint32_t at = 0;
    };

    // The heater sensor is sampled every 200 ms.
    std::array<Sample, 5> samples_{};
    unsigned sampleCount_ = 0;
    int sum_ = 0;
    int displayed_ = 0;
    std::uint32_t displayedAt_ = 0;
};

class ScreenIdle {
public:
    bool update(std::uint32_t now, unsigned timeoutMinutes, bool keepAwake);
    bool touch(std::uint32_t now, bool down);
    bool off() const { return off_; }

private:
    std::uint32_t lastUse_ = 0;
    bool off_ = false;
    bool swallowing_ = false;
    bool held_ = false;
    unsigned releases_ = 0;
};

struct ChartSample {
    static constexpr std::int16_t missing = INT16_MIN;
    std::int16_t chamber = missing;
    std::int16_t heater = missing;
    std::int16_t humidity = missing;
};

class History {
public:
    static constexpr unsigned capacity = 96;
    void clear() { count_ = 0; interval_ = 30000; }
    void add(std::uint32_t now, ChartSample sample);
    unsigned size() const { return count_; }
    unsigned intervalSeconds() const { return interval_ / 1000; }
    const ChartSample& operator[](unsigned index) const { return samples_[index]; }

private:
    std::array<ChartSample, capacity> samples_{};
    unsigned count_ = 0;
    std::uint32_t interval_ = 30000;
    std::uint32_t at_ = 0;
};

enum class TestItem : unsigned { exhaust, inlet, fan, heater, rotation, buzzer, sensors, count };
enum class TestResult : unsigned { pending, running, passed, failed, manual, skipped };

class SelfTest {
public:
    static constexpr unsigned count = static_cast<unsigned>(TestItem::count);
    void start(TestItem item, std::uint32_t now);
    void update(std::uint32_t now, const Inputs& inputs, unsigned ventsReached, bool airflow, bool soundDone);
    void confirm(std::uint32_t now, bool passed);
    void cancel();
    Outputs outputs() const;
    bool heatRequested(const Inputs& inputs) const;
    bool takeBuzzer();
    TestResult result(TestItem item) const { return results_[static_cast<unsigned>(item)]; }
    TestItem item() const { return item_; }
    bool active() const { return item_ != TestItem::count; }
    bool full() const { return full_; }
    const char* detail() const { return detail_; }

private:
    void next(std::uint32_t now, TestResult result, const char* detail);
    std::array<TestResult, count> results_{};
    TestItem item_ = TestItem::count;
    std::uint32_t since_ = 0;
    const char* detail_ = "";
    int baseline_ = 0;
    bool full_ = false;
    bool closing_ = false;
    bool baselineSet_ = false;
    bool playBuzzer_ = false;
};

}  // namespace h1::cold
