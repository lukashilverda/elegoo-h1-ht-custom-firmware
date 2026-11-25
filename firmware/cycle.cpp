#include "cycle.hpp"

namespace h1::cold {
namespace {
constexpr unsigned fanTimeoutMs = 1500;
constexpr unsigned pauseCloseMs = 5000;
constexpr unsigned rotationMs = 60000;
constexpr unsigned rotationPeriodMs = 300000;
// Use the heater's first soak band and duration to qualify a warm exhaust path.
constexpr int ventWarmupMarginTenths = 40;
constexpr unsigned ventQualificationMs = 8000;
constexpr unsigned heatTimeoutMs = 3600000;
constexpr int heatTimeoutMarginTenths = 50;
constexpr unsigned humidityQualificationMs = 5 * 60 * 1000;

bool validProgram(unsigned temperature, unsigned minutes, unsigned humidity) {
    return temperature >= customTemperatureMin && temperature <= maxDryingTemperature &&
           (temperature - customTemperatureMin) % customTemperatureStep == 0 &&
           minutes >= customTimeStepMinutes && minutes <= customTimeMaxMinutes &&
           minutes % customTimeStepMinutes == 0 && humidity < 100;
}
}

bool Filament::valid() const {
    return name[profileNameLength] == 0 && validProgram(temperature, minutes, humidity);
}

unsigned Settings::targetCelsius() const {
    auto requested = customTemperature;
    if (named()) requested = filament().temperature;
    else if (!custom()) requested = static_cast<unsigned>(presets[profile].celsius);
    return requested > temperatureLimit() ? temperatureLimit() : requested;
}

unsigned Settings::durationSeconds() const {
    if (named()) return filament().minutes * 60;
    return custom() ? customMinutes * 60 : presetSeconds;
}

unsigned Settings::humidityTarget() const {
    if (named()) return filament().humidity;
    return custom() ? customHumidity : 0;
}

bool Settings::customValid() const {
    return validProgram(customTemperature, customMinutes, customHumidity);
}

bool Settings::valid() const {
    if (profile >= firstSavedProfile + filaments.size() ||
        (named() && !filament().exists()) || !customValid() ||
        volume > 100 || displayTimeout >= sleepMinutes.size() || (soundMask & ~allSounds)) return false;
    for (const auto& item : filaments) {
        if (!item.valid()) return false;
    }
    return true;
}

void Settings::setHighTemperature(bool enabled) {
    highTemperature = enabled;
}

Cycle::Cycle(SavedCycle saved) : settings(saved.settings) {
    if (saved.seconds != 0) {
        phase_ = Phase::recovery;
        remainingMs_ = saved.seconds * 1000;
        totalSeconds_ = saved.totalSeconds ? saved.totalSeconds : settings.durationSeconds();
    }
}

unsigned Cycle::seconds() const {
    if (phase_ == Phase::idle) return settings.durationSeconds();
    return (remainingMs_ + 999) / 1000;
}

SavedCycle Cycle::snapshot() const {
    const bool recoverable = cycleExists() && notice_ != Notice::thermostat;
    return {settings, recoverable ? seconds() : 0u, recoverable ? totalSeconds_ : 0u};
}

void Cycle::selectProfile(unsigned profile) {
    if (phase_ != Phase::idle) return;
    if (profile < firstSavedProfile ||
        (profile - firstSavedProfile < settings.filaments.size() &&
         settings.filaments[profile - firstSavedProfile].exists())) settings.profile = profile;
}

bool Cycle::saveProfile(unsigned slot, const Filament& item) {
    if (phase_ != Phase::idle || slot >= settings.filaments.size() || !item.exists() ||
        !item.valid() || item.temperature > settings.temperatureLimit()) return false;
    settings.filaments[slot] = item;
    settings.profile = firstSavedProfile + slot;
    return true;
}

void Cycle::selectCustom(unsigned temperature, unsigned minutes, unsigned humidity) {
    if (phase_ != Phase::idle) return;
    if (!validProgram(temperature, minutes, humidity) || temperature > settings.temperatureLimit()) return;
    settings.profile = customProfile;
    settings.customTemperature = temperature;
    settings.customMinutes = minutes;
    settings.customHumidity = humidity;
}

StartResult Cycle::start(std::uint32_t now, const Inputs& inputs) {
    if (phase_ != Phase::idle && phase_ != Phase::paused && phase_ != Phase::recovery) {
        return StartResult::unavailable;
    }
    if (notice_ == Notice::thermostat) return StartResult::unavailable;
    if (inputs.overTemperature) return StartResult::tooHot;
    if (settings.lidDetection && inputs.lidOpen) return StartResult::closeLid;
    if (!inputs.chamberValid || !inputs.thermistorValid) return StartResult::waitingForSensors;

    const bool freshStart = phase_ == Phase::idle;
    if (freshStart) {
        totalSeconds_ = settings.durationSeconds();
        remainingMs_ = totalSeconds_ * 1000;
    }
    const int target = static_cast<int>(settings.targetCelsius()) * 10;
    const bool warm = inputs.filteredChamberTenths >= target || inputs.thermistorTenths >= target;
    phase_ = warm ? Phase::warmStart : Phase::drying;
    vents_ = warm ? VentTarget::open : VentTarget::closed;
    notice_ = Notice::none;
    lastTick_ = rotationAt_ = fanFeedbackAt_ = heatStartedAt_ = now;
    reachedTarget_ = inputs.thermistorTenths >= target - heatTimeoutMarginTenths;
    heaterAtGoal_ = inputs.thermistorTenths >= target;
    humidityStable_ = false;
    timingWarmThermistor_ = false;
    sawFanPulse_ = pendingVentClose_ = false;
    manualFan_ = 0;
    return StartResult::started;
}

void Cycle::advance(std::uint32_t now) {
    if (!running()) return;
    const auto elapsed = now - lastTick_;
    lastTick_ = now;
    if (elapsed >= remainingMs_) {
        finish(settings.humidityTarget() ? Completion::humidityTimeLimit : Completion::time);
    } else {
        remainingMs_ -= elapsed;
    }
}

void Cycle::finish(Completion completion) {
    remainingMs_ = 0;
    completion_ = completion;
    phase_ = Phase::finished;
    vents_ = VentTarget::closed;
}

void Cycle::thermostatFault(std::uint32_t now) {
    if (running()) suspend(now, Notice::thermostat);
}

void Cycle::suspend(std::uint32_t now, Notice reason) {
    phase_ = Phase::paused;
    notice_ = reason;
    pendingVentClose_ = reason == Notice::none;
    if (!pendingVentClose_) vents_ = VentTarget::closed;
    pausedAt_ = now;
}

void Cycle::pause(std::uint32_t now) {
    advance(now);
    if (running()) suspend(now, Notice::none);
}

void Cycle::stop() {
    remainingMs_ = 0;
    phase_ = Phase::idle;
    notice_ = Notice::none;
    vents_ = VentTarget::closed;
    pendingVentClose_ = false;
    manualFan_ = 0;
}

void Cycle::acknowledge() {
    notice_ = Notice::none;
    if (phase_ == Phase::finished) stop();
}

void Cycle::restoreDefaults() {
    stop();
    settings = {};
}

void Cycle::setManualFan(unsigned percent, bool lidOpen) {
    if (manualFanAvailable() && percent <= 100) {
        manualFan_ = lidOpen && settings.lidDetection ? 0 : static_cast<std::uint8_t>(percent);
    }
}

void Cycle::update(std::uint32_t now, const Inputs& inputs) {
    advance(now);
    const bool lidPause = inputs.lidOpen && settings.lidDetection;
    if (lidPause) manualFan_ = 0;
    if (phase_ == Phase::paused && pendingVentClose_ &&
        (lidPause || now - pausedAt_ >= pauseCloseMs)) {
        vents_ = VentTarget::closed;
        pendingVentClose_ = false;
    }
    if (!running()) return;

    if (inputs.fanPulse) {
        sawFanPulse_ = true;
        fanFeedbackAt_ = now;
    }
    const int target = static_cast<int>(settings.targetCelsius()) * 10;
    if (inputs.lidOpen) heatStartedAt_ = now;
    const bool chamberAtTarget = inputs.filteredChamberTenths >= target;
    if (phase_ == Phase::drying && vents_ != VentTarget::open) {
        if (inputs.thermistorTenths < target - ventWarmupMarginTenths) {
            timingWarmThermistor_ = false;
        } else if (!timingWarmThermistor_) {
            timingWarmThermistor_ = true;
            warmThermistorAt_ = now;
        }
        if (chamberAtTarget || (timingWarmThermistor_ && now - warmThermistorAt_ >= ventQualificationMs)) {
            vents_ = VentTarget::open;
        }
    }
    if (inputs.thermistorValid && !inputs.lidOpen) {
        if (inputs.thermistorTenths >= target - heatTimeoutMarginTenths) reachedTarget_ = true;
        if (inputs.thermistorTenths >= target) heaterAtGoal_ = true;
    }

    Notice reason = Notice::none;
    if (inputs.overTemperature) reason = Notice::overTemperature;
    else if (lidPause) reason = Notice::lid;
    else if (!inputs.thermistorValid) reason = Notice::thermistor;
    else if (!inputs.chamberValid) reason = Notice::humidity;
    else if (inputs.ventFaults & 1u) reason = Notice::exhaust;
    else if (inputs.ventFaults & 2u) reason = Notice::inlet;
    else if (now - fanFeedbackAt_ >= fanTimeoutMs) reason = Notice::fan;
    else if (!reachedTarget_ && now - heatStartedAt_ >= heatTimeoutMs) {
        reason = Notice::heatTimeout;
    }
    if (reason != Notice::none) {
        suspend(now, reason);
        return;
    }

    const auto humidityTarget = settings.humidityTarget();
    if (humidityTarget && heaterAtGoal_ && !inputs.lidOpen && inputs.humidityTenths >= 0 &&
        inputs.humidityTenths <= static_cast<int>(humidityTarget) * 10) {
        if (!humidityStable_) humiditySince_ = now;
        humidityStable_ = true;
        if (now - humiditySince_ >= humidityQualificationMs) {
            finish(Completion::humidity);
            return;
        }
    } else {
        humidityStable_ = false;
    }

    if (phase_ == Phase::warmStart && !chamberAtTarget && inputs.thermistorTenths < target) {
        phase_ = Phase::drying;
        rotationAt_ = now;
        // The vents stay open once driven open during a warm start.
    }
}

Outputs Cycle::outputs(std::uint32_t now) const {
    return {vents_, running() || manualFan_, phase_ == Phase::drying && settings.rotation && sawFanPulse_ &&
             (now - rotationAt_) % rotationPeriodMs < rotationMs, settings.lidDetection, manualFan_};
}

}  // namespace h1::cold

