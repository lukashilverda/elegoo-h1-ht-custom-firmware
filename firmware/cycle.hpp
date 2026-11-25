#pragma once

#include "heat_control.hpp"
#include "vent_control.hpp"

#include <array>

namespace h1::cold {

struct Preset {
    const char* name;
    int celsius;
};

inline constexpr Preset presets[]{
    {"PLA", 55}, {"PETG", 65}, {"TPU", 70}, {"ABS", 70}, {"ASA", 80}, {"PET", 80},
    {"PP", 80}, {"PC", 80}, {"PA", 80}, {"PAHT-CF", 80}, {"PVA (80°C)", 80}, {"PVA (45°C)", 45},
};
inline constexpr unsigned presetCount = sizeof presets / sizeof presets[0];
inline constexpr unsigned presetSeconds = 8 * 60 * 60;
inline constexpr unsigned customProfile = presetCount;
inline constexpr unsigned customTemperatureMin = 45;
inline constexpr unsigned customTemperatureStep = 5;
inline constexpr unsigned customTemperatureDefault = 55;
inline constexpr unsigned customTimeStepMinutes = 30;
inline constexpr unsigned customTimeMaxMinutes = 12 * 60;
inline constexpr unsigned customTimeDefaultMinutes = presetSeconds / 60;

inline constexpr unsigned regularTemperatureLimit = 85;
inline constexpr unsigned savedProfileCount = 8;
inline constexpr unsigned profileNameLength = 16;
inline constexpr unsigned firstSavedProfile = customProfile + 1;
inline constexpr unsigned maxTimedSeconds = UINT32_MAX / 1000 - 1;
inline constexpr std::array<unsigned, 5> sleepMinutes{0, 1, 5, 15, 30};
inline constexpr unsigned soundBoot = 1, soundStart = 2, soundStop = 4, soundError = 8;
inline constexpr unsigned allSounds = soundBoot | soundStart | soundStop | soundError;

struct Filament {
    char name[profileNameLength + 1]{};
    unsigned temperature = customTemperatureDefault;
    unsigned minutes = customTimeDefaultMinutes;
    unsigned humidity = 0;

    bool exists() const { return name[0] != 0; }
    bool valid() const;
};

struct Settings {
    unsigned profile = 0;
    bool fahrenheit = false;
    bool rotation = true;
    bool lidDetection = true;
    unsigned soundMask = allSounds;
    unsigned customTemperature = customTemperatureDefault;
    unsigned customMinutes = customTimeDefaultMinutes;

    unsigned customHumidity = 0;
    unsigned volume = 100;
    unsigned displayTimeout = 0;
    bool highTemperature = false;
    std::array<Filament, savedProfileCount> filaments{};

    bool custom() const { return profile == customProfile; }
    bool named() const { return profile >= firstSavedProfile; }
    const Filament& filament() const { return filaments[profile - firstSavedProfile]; }
    unsigned temperatureLimit() const { return highTemperature ? maxDryingTemperature : regularTemperatureLimit; }
    unsigned targetCelsius() const;
    unsigned durationSeconds() const;
    unsigned humidityTarget() const;
    bool customValid() const;
    bool valid() const;
    void setHighTemperature(bool enabled);

};

struct SavedCycle {
    Settings settings{};
    unsigned seconds = 0;
    unsigned totalSeconds = 0;
};

struct Inputs {
    bool chamberValid = false;
    int filteredChamberTenths = 0;
    bool thermistorValid = false;
    bool lidOpen = false;
    bool fanPulse = false;
    unsigned ventFaults = 0;
    bool overTemperature = false;
    int thermistorTenths = 0;
    int humidityTenths = -1;
};

enum class Phase { idle, warmStart, drying, paused, recovery, finished };
enum class Notice { none, lid, humidity, thermistor, fan, exhaust, inlet, heatTimeout, overTemperature, thermostat, storage, controller };
enum class Completion { time, humidity, humidityTimeLimit };
enum class StartResult { started, waitingForSensors, closeLid, tooHot, unavailable };

struct Outputs {
    VentTarget vents;
    bool fan;
    bool motor;
    bool lidDetection;
    std::uint8_t manualDuty;
    std::uint8_t ventMask = 3;
    bool diagnosticHeat = false;
};

class Cycle {
public:
    explicit Cycle(SavedCycle saved = {});
    void update(std::uint32_t now, const Inputs& inputs);
    StartResult start(std::uint32_t now, const Inputs& inputs);
    void pause(std::uint32_t now);
    void stop();
    void acknowledge();
    void restoreDefaults();
    void setManualFan(unsigned percent, bool lidOpen);
    void selectProfile(unsigned profile);
    bool saveProfile(unsigned slot, const Filament& filament);
    void selectCustom(unsigned temperature, unsigned minutes, unsigned humidity = 0);
    void thermostatFault(std::uint32_t now);
    unsigned totalSeconds() const { return totalSeconds_; }
    Completion completion() const { return completion_; }

    Settings settings{};
    Phase phase() const { return phase_; }
    Notice notice() const { return notice_; }
    bool running() const { return phase_ == Phase::drying || phase_ == Phase::warmStart; }
    bool cycleExists() const { return running() || phase_ == Phase::paused || phase_ == Phase::recovery; }
    bool manualFanAvailable() const {
        return phase_ == Phase::idle || (phase_ == Phase::paused && notice_ == Notice::none);
    }
    unsigned manualFanDuty() const { return manualFan_; }
    unsigned seconds() const;
    Outputs outputs(std::uint32_t now) const;
    SavedCycle snapshot() const;

private:
    void advance(std::uint32_t now);
    void suspend(std::uint32_t now, Notice reason);
    void finish(Completion completion);

    Phase phase_ = Phase::idle;
    Notice notice_ = Notice::none;
    VentTarget vents_ = VentTarget::hold;
    unsigned remainingMs_ = 0;
    unsigned totalSeconds_ = 0;
    Completion completion_ = Completion::time;
    bool heaterAtGoal_ = false;
    bool humidityStable_ = false;
    std::uint32_t humiditySince_ = 0;
    std::uint32_t lastTick_ = 0;
    std::uint32_t fanFeedbackAt_ = 0;
    std::uint32_t rotationAt_ = 0;
    std::uint32_t pausedAt_ = 0;
    std::uint32_t heatStartedAt_ = 0;
    bool reachedTarget_ = false;
    bool timingWarmThermistor_ = false;
    std::uint32_t warmThermistorAt_ = 0;
    bool sawFanPulse_ = false;
    bool pendingVentClose_ = false;
    std::uint8_t manualFan_ = 0;
};

}  // namespace h1::cold

