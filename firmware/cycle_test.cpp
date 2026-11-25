#include "cycle.hpp"
#include "fan_warmup.hpp"
#include "heat_control.hpp"
#include "humidity_sensor.hpp"
#include "storage.hpp"
#include "service.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>

using namespace h1;
using namespace h1::cold;

namespace {
constexpr Inputs ready{true, 220, true, false, true, 0};
constexpr std::uint32_t erased = 0xffffffffu;
using namespace journalFlash;
std::array<std::uint32_t, 2 * pageWords> flash;
int writesBeforeFailure = -1;

void testPresets() {
    constexpr int expected[]{55, 65, 70, 70, 80, 80, 80, 80, 80, 80, 80, 45};
    static_assert(sizeof expected / sizeof expected[0] == presetCount);
    Cycle cycle;
    for (unsigned i = 0; i < presetCount; ++i) {
        cycle.selectProfile(i);
        assert(presets[cycle.settings.profile].celsius == expected[i]);
        assert(cycle.seconds() == 28800);
    }
    cycle.restoreDefaults();
    assert(cycle.settings.profile == 0 && cycle.settings.lidDetection && cycle.settings.rotation);
}

void testManualFan() {
    Cycle cycle;
    assert(cycle.manualFanAvailable() && !cycle.outputs(0).fan);
    cycle.setManualFan(65, false);
    assert(cycle.outputs(0).manualDuty == 65 && cycle.outputs(0).fan && !cycle.outputs(0).motor);
    assert(!Cycle(cycle.snapshot()).outputs(0).fan);
    cycle.setManualFan(0, false);
    assert(!cycle.outputs(0).fan);

    cycle.setManualFan(65, false);
    auto open = ready;
    open.lidOpen = true;
    cycle.update(10, open);
    cycle.setManualFan(65, true);
    assert(!cycle.outputs(10).fan);
    cycle.update(100, ready);
    assert(!cycle.outputs(100).fan);

    cycle.setManualFan(65, false);
    assert(cycle.start(110, ready) == StartResult::started);
    cycle.setManualFan(65, false);
    assert(!cycle.manualFanAvailable() && !cycle.manualFanDuty() && cycle.outputs(110).fan);
    cycle.pause(120);
    assert(cycle.manualFanAvailable() && !cycle.outputs(120).fan);
    cycle.setManualFan(65, false);
    assert(cycle.outputs(120).fan && !cycle.outputs(120).motor);
    assert(cycle.start(130, ready) == StartResult::started && !cycle.manualFanDuty());
    cycle.pause(140);
    cycle.setManualFan(65, false);
    cycle.stop();
    assert(!cycle.outputs(140).fan);
}

void testTiming() {
    Cycle cycle;
    assert(cycle.start(20, {}) == StartResult::waitingForSensors);
    assert(cycle.start(20, ready) == StartResult::started);
    assert(cycle.outputs(20).vents == VentTarget::closed && cycle.outputs(20).fan);
    assert(!cycle.outputs(20).motor);

    cycle.update(120, ready);
    assert(cycle.outputs(120).motor);
    cycle.update(60020, ready);
    assert(!cycle.outputs(60020).motor);
    cycle.update(300020, ready);
    assert(cycle.outputs(300020).motor);
    assert(cycle.seconds() == presetSeconds - 300);

    auto atTarget = ready;
    atTarget.filteredChamberTenths = 550;
    cycle.update(300120, atTarget);
    assert(cycle.outputs(300120).vents == VentTarget::open);
    cycle.update(300220, ready);
    assert(cycle.outputs(300220).vents == VentTarget::open);

    cycle.pause(300520);
    assert(!cycle.outputs(300520).fan && !cycle.outputs(300520).motor);
    assert(cycle.seconds() == presetSeconds - 300);
    cycle.update(305519, ready);
    assert(cycle.outputs(305519).vents == VentTarget::open);
    cycle.update(305520, ready);
    assert(cycle.outputs(305520).vents == VentTarget::closed);

    assert(cycle.start(310020, ready) == StartResult::started);
    cycle.update(310520, ready);
    assert(cycle.seconds() == presetSeconds - 301 && cycle.outputs(310520).motor);
    cycle.stop();
    assert(cycle.phase() == Phase::idle && cycle.seconds() == presetSeconds);
}

void testDryingAt85() {
    Cycle cycle;
    cycle.selectCustom(85, 120);
    auto sample = ready;
    sample.filteredChamberTenths = 503;
    sample.thermistorTenths = 800;
    constexpr std::uint32_t start = 0xfffff000u;
    assert(cycle.start(start, sample) == StartResult::started);

    sample.thermistorTenths = 810;
    cycle.update(start + 100, sample);
    sample.thermistorTenths = 809;
    cycle.update(start + 7900, sample);
    sample.thermistorTenths = 850;
    cycle.update(start + 8100, sample);
    cycle.update(start + 16099, sample);
    assert(cycle.outputs(start + 16099).vents == VentTarget::closed);
    cycle.update(start + 16100, sample);
    assert(cycle.outputs(start + 16100).vents == VentTarget::open);

    sample.thermistorTenths = 805;
    cycle.update(start + 3600001, sample);
    assert(cycle.running() && cycle.outputs(start + 3600001).vents == VentTarget::open);
    cycle.pause(start + 3600100);
    cycle.update(start + 3605100, sample);
    assert(cycle.outputs(start + 3605100).vents == VentTarget::closed);
    sample.thermistorTenths = 850;
    assert(cycle.start(start + 3606000, sample) == StartResult::started);
    assert(cycle.phase() == Phase::warmStart && cycle.outputs(start + 3606000).vents == VentTarget::open);
    sample.thermistorTenths = 840;
    cycle.update(start + 3607000, sample);
    assert(cycle.phase() == Phase::drying);
    sample.ventFaults = 1;
    cycle.update(start + 3607100, sample);
    assert(cycle.notice() == Notice::exhaust && cycle.outputs(start + 3607100).vents == VentTarget::closed);

    // Passing the vent threshold alone does not clear the one-hour heat timeout.
    Cycle stalled;
    stalled.selectCustom(85, 120);
    sample.ventFaults = 0;
    sample.thermistorTenths = 799;
    assert(stalled.start(0, sample) == StartResult::started);
    sample.filteredChamberTenths = 850;
    stalled.update(1, sample);
    assert(stalled.outputs(1).vents == VentTarget::open);
    stalled.update(3600001, sample);
    assert(stalled.notice() == Notice::heatTimeout);

    HeatControl heater;
    assert(heater.demand(0, 85, 70, 5039, true));
    assert(!heater.demand(1, 85, 70, 8500, true));
    assert(!heater.demand(2, 85, 70, 8301, true));
    assert(heater.demand(3, 85, 70, 8300, true));
}

void testWarmAndWrap() {
    constexpr std::uint32_t start = 0xfffffff0u;
    auto warm = ready;
    warm.filteredChamberTenths = 550;
    Cycle cycle;
    assert(cycle.start(start, warm) == StartResult::started);
    assert(cycle.phase() == Phase::warmStart && cycle.outputs(start).vents == VentTarget::open);
    cycle.update(start + 1000, warm);
    assert(cycle.outputs(start + 1000).fan && !cycle.outputs(start + 1000).motor);
    assert(cycle.seconds() == presetSeconds - 1);
    cycle.update(start + 1100, ready);
    assert(cycle.phase() == Phase::drying && cycle.outputs(start + 1100).motor);

    Cycle hotCycle;
    assert(hotCycle.start(0, warm) == StartResult::started);
    hotCycle.update(presetSeconds * 1000, warm);
    assert(hotCycle.phase() == Phase::finished && hotCycle.seconds() == 0);
    assert(!hotCycle.outputs(0).fan && hotCycle.outputs(0).vents == VentTarget::closed);
    hotCycle.acknowledge();
    assert(hotCycle.seconds() == presetSeconds);
}

void testInterlocks() {
    Cycle cycle;
    auto open = ready;
    open.lidOpen = true;
    assert(cycle.start(0, open) == StartResult::closeLid);
    assert(cycle.start(0, ready) == StartResult::started);
    cycle.update(1000, open);
    assert(cycle.notice() == Notice::lid && cycle.phase() == Phase::paused);
    cycle.update(5000, ready);
    assert(cycle.seconds() == presetSeconds - 1 && cycle.phase() == Phase::paused);

    cycle.settings.lidDetection = false;
    assert(cycle.start(5000, open) == StartResult::started);
    cycle.update(6000, open);
    const auto outputs = cycle.outputs(6000);
    assert(cycle.seconds() == presetSeconds - 2 && outputs.fan && outputs.motor && !outputs.lidDetection);

    auto fault = ready;
    fault.ventFaults = 2;
    cycle.update(7000, fault);
    assert(cycle.phase() == Phase::paused && cycle.notice() == Notice::inlet &&
           !cycle.outputs(7000).motor && cycle.outputs(7000).vents == VentTarget::closed);
    cycle.acknowledge();
    assert(cycle.start(8000, ready) == StartResult::started);
    fault.ventFaults = 1;
    cycle.update(9000, fault);
    assert(cycle.notice() == Notice::exhaust);

    assert(cycle.start(10000, ready) == StartResult::started);
    auto noPulse = ready;
    noPulse.fanPulse = false;
    cycle.update(11499, noPulse);
    assert(cycle.running() && !cycle.outputs(11499).motor);
    cycle.update(11500, noPulse);
    assert(cycle.notice() == Notice::fan && cycle.phase() == Phase::paused);

    assert(cycle.start(12000, ready) == StartResult::started);
    fault = ready;
    fault.chamberValid = false;
    cycle.update(12001, fault);
    assert(cycle.notice() == Notice::humidity);

    assert(cycle.start(13000, ready) == StartResult::started);
    fault = ready;
    fault.thermistorValid = false;
    cycle.update(13001, fault);
    assert(cycle.notice() == Notice::thermistor);
}

void testPagePreparation() {
    using namespace journalFlash;
    flash.fill(erased);
    Journal busy;
    busy.load();
    // Force both pages to be used and test rollover with a full configuration write.
    for (unsigned i = 1; i <= 300; ++i) assert(busy.save({{}, i}));
    assert(busy.load().seconds == 300);
    Settings changed;
    changed.displayTimeout = 2;
    const auto baseline = flash;
    for (int cut : {0, 1, 19, 74, 76}) {
        flash = baseline;
        assert(busy.load().seconds == 300);
        writesBeforeFailure = cut;
        assert(!busy.save({changed, 301}));
        writesBeforeFailure = -1;
        assert(busy.load().seconds == 300);
    }
    assert(busy.save({changed, 301}));
    assert(busy.load().seconds == 301);
    assert(busy.load().settings.displayTimeout == 2);
    for (unsigned i = 302; i <= 500 && !busy.needsPreparation(); ++i) assert(busy.save({changed, i}));
    assert(busy.needsPreparation());
    assert(busy.prepareNextPage() && !busy.needsPreparation() && busy.canSaveWithoutErase());
}

void testCustom() {
    flash.fill(erased);
    Journal journal;
    assert(journal.load().seconds == 0);
    Cycle cycle;

    cycle.selectCustom(105, 720);
    assert(cycle.settings.profile == 0);
    cycle.selectCustom(65, 45);
    assert(!cycle.settings.custom());
    cycle.selectCustom(100, 720);
    assert(!cycle.settings.custom());
    cycle.settings.setHighTemperature(true);
    cycle.selectCustom(100, 720);
    assert(cycle.settings.custom() && cycle.settings.targetCelsius() == 100 && cycle.seconds() == 43200);
    assert(cycle.start(0, ready) == StartResult::started);
    cycle.update(3000, ready);
    assert(cycle.seconds() == 43197);
    cycle.selectCustom(45, 30);
    assert(cycle.settings.durationSeconds() == 43200);
    assert(journal.save(cycle.snapshot()));

    Cycle recovered(journal.load());
    assert(recovered.phase() == Phase::recovery && recovered.seconds() == 43197);
    assert(recovered.settings.targetCelsius() == 100 && recovered.settings.customMinutes == 720);
    recovered.stop();
    recovered.selectProfile(0);
    assert(recovered.seconds() == presetSeconds && recovered.settings.customMinutes == 720);
    recovered.selectCustom(45, 30);
    assert(recovered.start(100, ready) == StartResult::started);
    recovered.update(1800100, ready);
    assert(recovered.phase() == Phase::finished);
    recovered.restoreDefaults();
    assert(recovered.settings.customTemperature == customTemperatureDefault &&
           recovered.settings.customMinutes == customTimeDefaultMinutes);
}

void testExtendedCustomStorage() {
    flash.fill(erased);
    // A Beta 0.9 record for a Custom cycle at 85 °C with 43,197 seconds left.
    flash[0] = 0x48435431;
    flash[1] = 41;
    flash[2] = 0xd8a8bdec;
    flash[3] = 0x3ce259d0;
    Journal journal;
    auto saved = journal.load();
    assert(saved.settings.targetCelsius() == 85 && saved.seconds == 43197);

    saved.settings.customTemperature = 95;
    saved.settings.customMinutes = 450;
    saved.seconds = 26000;
    saved.totalSeconds = 27000;
    assert(saved.settings.targetCelsius() == 85);
    saved.settings.setHighTemperature(true);
    assert(journal.save(saved));
    saved.settings.customMinutes = 480;
    saved.totalSeconds = 28800;
    assert(journal.save(saved));
    Journal reloaded;
    const auto restored = reloaded.load();
    assert(restored.settings.targetCelsius() == 95 && restored.settings.customMinutes == 480);
    assert(restored.settings.highTemperature && restored.seconds == 26000);

    flash.fill(erased);
    constexpr std::uint32_t legacyHigh = 0x090000ecu; // 100 °C, 30 min, idle
    flash[0] = 0x48435432;
    flash[1] = 1;
    flash[2] = legacyHigh;
    // CRC-32 over sequence, payload and extended signature.
    std::uint32_t crc = 0xffffffffu;
    for (auto word : {flash[1], flash[2], flash[0]}) {
        crc ^= word;
        for (unsigned i = 0; i < 32; ++i) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    flash[3] = ~crc;
    const auto migrated = reloaded.load();
    assert(migrated.settings.custom() && migrated.settings.targetCelsius() == 85);
    assert(!migrated.settings.highTemperature);
}

void testFanWarmup() {
    FanWarmup fan;
    auto run = [&](unsigned first, unsigned last, unsigned pulseStep, bool airflow = true) {
        for (unsigned now = first; now <= last; now += 10) {
            fan.update(now, true, true, now % pulseStep == 0, airflow);
        }
    };
    run(0, 1240, 50);
    assert(fan.duty() == FanWarmup::normalDuty && fan.allowsHeat());
    run(1250, 1250, 50);
    assert(fan.duty() == FanWarmup::warmupDuty && !fan.allowsHeat());
    run(1260, 1750, 50);
    assert(fan.duty() == FanWarmup::warmupDuty && fan.allowsHeat());
    // Deteriorating feedback at the lower duty causes a return to the normal duty.
    run(1760, 2250, 140);
    assert(fan.duty() == FanWarmup::normalDuty && !fan.allowsHeat());
    run(2260, 2500, 50);
    assert(fan.duty() == FanWarmup::normalDuty && fan.allowsHeat());
    run(2510, 4000, 50);
    assert(fan.duty() == FanWarmup::normalDuty);

    fan.update(4001, false, false, false, false);
    run(4010, 5260, 50);
    assert(fan.duty() == FanWarmup::warmupDuty && !fan.allowsHeat());
    fan.update(5270, true, true, false, false);
    assert(fan.duty() == FanWarmup::normalDuty && !fan.allowsHeat());
}

class ChamberBus final : public h1::HumidityBus {
public:
    unsigned triggers = 0;

    bool write(std::uint8_t address, const std::uint8_t* data, std::size_t size) override {
        assert(address == h1::humiditySensorAddress && size == h1::humidityMeasure.size());
        assert(std::equal(h1::humidityMeasure.begin(), h1::humidityMeasure.end(), data));
        ++triggers;
        return true;
    }

    bool read(std::uint8_t address, std::uint8_t* data, std::size_t size) override {
        constexpr std::array<std::uint8_t, 7> frame{0x18, 0x80, 0, 6, 0, 0, 0x23};
        assert(address == h1::humiditySensorAddress);
        if (size == 1) {
            *data = frame.front();
        } else {
            assert(size == frame.size());
            std::copy(frame.begin(), frame.end(), data);
        }
        return true;
    }
};

void testChamberCadence() {
    using h1::HumidityPollResult;
    ChamberBus bus;
    h1::HumiditySensor sensor(bus, 0);
    h1::ChamberReading reading{};
    assert(sensor.poll(40, reading) == HumidityPollResult::waiting && bus.triggers == 1);
    assert(sensor.poll(119, reading) == HumidityPollResult::waiting);
    assert(sensor.poll(120, reading) == HumidityPollResult::sample);
    assert(reading.status == h1::HumidityStatus::ready && reading.temperatureCelsius == 25.0f);
    assert(sensor.poll(239, reading) == HumidityPollResult::waiting && bus.triggers == 1);
    assert(sensor.poll(240, reading) == HumidityPollResult::waiting && bus.triggers == 2);
    assert(sensor.poll(319, reading) == HumidityPollResult::waiting);
    assert(sensor.poll(320, reading) == HumidityPollResult::sample);
}

void testHeat() {
    HeatControl regulator;
    assert(!regulator.demand(0, 55, 22.0f, 2200, false));
    assert(regulator.demand(0, 55, 22.0f, 2200, true));
    assert(!regulator.demand(1000, 55, 51.0f, 2200, true));
    assert(!regulator.demand(8999, 55, 52.0f, 2200, true));
    assert(regulator.demand(9000, 55, 52.0f, 2200, true));
    assert(!regulator.demand(10000, 55, 55.0f, 2200, true));
    assert(!regulator.demand(11000, 55, 54.0f, 2200, true));
    assert(regulator.demand(12000, 55, 53.0f, 2200, true));
    assert(!regulator.demand(13000, 55, 100.0f, 2200, true));
    assert(!regulator.demand(13001, 55, 25.0f, 2200, false));
    assert(regulator.demand(13002, 100, 90.0f, 2200, true));
    assert(!regulator.demand(13003, 100, 100.0f, 2200, true));

    Cycle cycle;
    auto hot = ready;
    hot.overTemperature = true;
    assert(cycle.start(0, hot) == StartResult::tooHot);
    assert(cycle.start(0, ready) == StartResult::started);
    cycle.update(3599999, ready);
    assert(cycle.running());
    cycle.update(3600000, ready);
    assert(cycle.notice() == Notice::heatTimeout && cycle.phase() == Phase::paused);
    assert(cycle.start(3600500, ready) == StartResult::started);
    cycle.update(3600501, hot);
    assert(cycle.notice() == Notice::overTemperature && !cycle.outputs(3600501).fan);
}

void testRecoveryAndJournal() {
    flash.fill(erased);
    Journal initial;
    assert(initial.load().seconds == 0);
    assert(initial.save({{11, true, false, false, false}, 700}));

    Journal reloaded;
    auto saved = reloaded.load();
    assert(saved.settings.profile == 11 && saved.settings.fahrenheit && !saved.settings.soundMask);
    Cycle recovery(saved);
    assert(recovery.phase() == Phase::recovery && !recovery.outputs(0).fan);
    recovery.update(180000, ready);
    assert(recovery.seconds() == 700);
    assert(recovery.start(180000, ready) == StartResult::started);
    recovery.update(183000, ready);
    assert(recovery.seconds() == 697 && !recovery.outputs(183000).motor);

    // Cut power after every possible partial record, then retry in the same page.
    const auto baseline = flash;
    for (int n = 0; n < 4; ++n) {
        flash = baseline;
        Journal writer;
        writer.load();
        writesBeforeFailure = n;
        assert(!writer.save(recovery.snapshot()));
        writesBeforeFailure = -1;
        Journal afterCut;
        assert(afterCut.load().seconds == 700);
        assert(afterCut.save(recovery.snapshot()) && afterCut.load().seconds == 697);
    }

    for (unsigned i = 0; i < 2 * pageWords / 4; ++i) {
        assert(reloaded.save({{11, true, false, false, false}, i + 1}));
    }
    Journal afterRoll;
    assert(afterRoll.load().seconds == 2 * pageWords / 4);
    recovery.restoreDefaults();
    assert(afterRoll.save(recovery.snapshot()));
    saved = afterRoll.load();
    assert(saved.seconds == 0 && saved.settings.profile == 0 && !saved.settings.fahrenheit &&
           saved.settings.lidDetection && saved.settings.soundMask == allSounds);
}
void testHumidityAndLegacyTiming() {
    Cycle cycle;
    cycle.selectCustom(55, 30, 20);
    auto in = ready;
    in.thermistorTenths = 500;
    in.humidityTenths = 190;
    assert(cycle.start(0, in) == StartResult::started);
    cycle.update(310000, in);
    assert(cycle.running());
    in.thermistorTenths = 550;
    cycle.update(320000, in);
    in.humidityTenths = 201;
    cycle.update(500000, in);
    in.humidityTenths = 200;
    cycle.update(501000, in);
    in.humidityTenths = -1;
    cycle.update(790000, in);
    in.humidityTenths = 190;
    cycle.update(800000, in);
    cycle.update(1099999, in);
    assert(cycle.running());
    cycle.update(1100000, in);
    assert(cycle.phase() == Phase::finished && cycle.completion() == Completion::humidity);

    cycle.stop();
    in.humidityTenths = 700;
    assert(cycle.start(0, in) == StartResult::started);
    cycle.update(1800000, in);
    assert(cycle.completion() == Completion::humidityTimeLimit);

    cycle.stop();
    cycle.selectCustom(55, 720);
    // Beta 0.14 could save a cycle extended beyond the currently selectable preset duration.
    const SavedCycle legacy{cycle.settings, 44500, 45000};
    Cycle recovered(legacy);
    assert(recovered.seconds() == 44500 && recovered.totalSeconds() == 45000);
    assert(recovered.start(0, ready) == StartResult::started);
    recovered.update(1000, ready);
    assert(recovered.seconds() == 44499);
    flash.fill(erased);
    Journal journal;
    assert(journal.save(recovered.snapshot()));
    const auto saved = journal.load();
    assert(saved.seconds == 44499 && saved.totalSeconds == 45000);
}

void testSaveCurrentProfile() {
    flash.fill(erased);
    Journal journal;
    Cycle cycle;
    cycle.selectCustom(65, 360, 20);
    const Filament unnamed{{}, cycle.settings.targetCelsius(), cycle.settings.durationSeconds() / 60,
                           cycle.settings.humidityTarget()};
    assert(!cycle.saveProfile(0, unnamed) && cycle.settings.custom());
    auto named = unnamed;
    named.name[0] = 'A';
    assert(!cycle.saveProfile(savedProfileCount, named));
    assert(cycle.saveProfile(0, named) && cycle.settings.profile == firstSavedProfile);
    assert(journal.save(cycle.snapshot()));
    Cycle restored(journal.load());
    assert(restored.settings.profile == firstSavedProfile && restored.settings.filament().name[0] == 'A');
    assert(restored.settings.targetCelsius() == 65 && restored.settings.durationSeconds() == 21600 &&
           restored.settings.humidityTarget() == 20);
    assert(restored.start(0, ready) == StartResult::started);
    assert(!restored.saveProfile(1, named));
}

void testProfilesAndFaultHistory() {
    flash.fill(erased);
    Journal journal;
    journal.load();
    Cycle cycle;
    for (unsigned i = 0; i < savedProfileCount; ++i) {
        auto& profile = cycle.settings.filaments[i];
        std::snprintf(profile.name, sizeof profile.name, "Filament %u", i + 1);
        profile.temperature = 85;
        profile.minutes = 90 + i * 30;
        profile.humidity = i ? 20 + i : 0;
    }
    cycle.selectProfile(firstSavedProfile + 7);
    cycle.settings.setHighTemperature(true);
    cycle.settings.filaments[7].temperature = 95;
    cycle.settings.volume = 40;
    cycle.settings.soundMask = soundError;
    cycle.settings.displayTimeout = 4;
    FaultHistory faults;
    for (int i = 0; i < 34; ++i) faults.add(Notice::thermostat, 880 - i);
    assert(faults.size() == 30 && faults.heaterTenths(29) == 876);
    assert(journal.save(cycle.snapshot(), faults));
    const auto saved = journal.load();
    assert(saved.settings.profile == firstSavedProfile + 7 && saved.settings.targetCelsius() == 95);
    assert(saved.settings.humidityTarget() == 27 && saved.settings.durationSeconds() == 18000);
    assert(saved.settings.volume == 40 && saved.settings.soundMask == soundError && saved.settings.displayTimeout == 4);
    assert(journal.faults().size() == 30 && journal.faults().code(0) == Notice::thermostat &&
           journal.faults().heaterTenths(0) == 847);
    cycle.settings.setHighTemperature(false);
    assert(cycle.settings.targetCelsius() == 85 && cycle.settings.filaments[7].temperature == 95);
    assert(cycle.start(0, ready) == StartResult::started);
    cycle.thermostatFault(1);
    assert(cycle.notice() == Notice::thermostat && !cycle.outputs(1).fan);
    assert(cycle.snapshot().seconds == 0 && cycle.start(2, ready) == StartResult::unavailable);
}

void testHomeTemperature() {
    constexpr int bands[][2]{
        {4399, 4399}, {4400, 5500}, {4401, 5300},
        {4799, 5300}, {4800, 5300}, {4801, 5400},
        {5099, 5400}, {5100, 5400}, {5101, 5500},
        {5899, 5500}, {5900, 5500}, {5901, 5600},
        {6199, 5600}, {6200, 5600}, {6201, 5700},
        {6599, 5700}, {6600, 5700}, {6601, 6601},
    };
    for (const auto& band : bands) {
        HomeTemperature home;
        assert(home.update(0, 0, band[0], 55, true, true) == band[1]);
    }

    HomeTemperature swing;
    for (std::uint32_t now = 0; now <= 4000; now += 200) {
        assert(swing.update(now, now, now % 400 ? 5900 : 5300, 55, true, true) == 5500);
    }

    HomeTemperature average;
    assert(average.update(0, 0, 3000, 55, false, true) == 3000);
    for (std::uint32_t now = 200; now < 400; now += 20) {
        assert(average.update(now, 200, 3300, 55, false, true) == 3000);
    }
    for (std::uint32_t now = 400; now <= 800; now += 200) {
        assert(average.update(now, now, 3000, 55, false, true) == 3000);
    }
    assert(average.update(1000, 800, 3000, 55, false, true) == 3060);
    assert(average.update(1200, 1200, 3000, 55, false, true) == 3060);
    assert(average.update(2000, 2000, 3000, 55, false, true) == 3000);

    HomeTemperature fractionalEdge;
    assert(fractionalEdge.update(0, 0, 5900, 55, true, true) == 5500);
    assert(fractionalEdge.update(200, 200, 5901, 55, true, true) == 5500);
    for (std::uint32_t now = 400; now <= 1000; now += 200) {
        const int expected = now == 1000 ? 5600 : 5500;
        assert(fractionalEdge.update(now, now, 5900, 55, true, true) == expected);
    }

    HomeTemperature slew;
    assert(slew.update(0, 0, 5500, 55, false, true) == 5500);
    for (std::uint32_t now = 50; now <= 4000; now += 50) {
        const auto sampleAt = now / 200 * 200;
        const int measured = sampleAt == 0 ? 5500 : (sampleAt <= 2000 ? 7500 : 3500);
        const int seconds = static_cast<int>(now / 1000);
        const int expected = seconds <= 2 ? 5500 + seconds * 100 : 5900 - seconds * 100;
        assert(slew.update(now, sampleAt, measured, 55, false, true) == expected);
    }
    assert(slew.update(7000, 7000, 3500, 55, false, true) == 5200);
    assert(slew.update(7001, 7001, -1500, 55, false, false) == 0);
    assert(slew.update(7201, 7201, 3000, 55, false, true) == 3000);

    HomeTemperature wrap;
    constexpr std::uint32_t start = UINT32_MAX - 499u;
    assert(wrap.update(start, start, 5500, 55, false, true) == 5500);
    assert(wrap.update(start + 999u, start + 800u, 7500, 55, false, true) == 5500);
    assert(wrap.update(start + 1000u, start + 1000u, 7500, 55, false, true) == 5600);
}

void testService() {
    HeatMonitor monitor;
    assert(!monitor.update(0, 200, true, true));
    assert(!monitor.update(60000, 810, true, true));
    assert(!monitor.update(90000, 745, true, true));
    for (unsigned now = 90100; now < 210000; now += 100) {
        assert(!monitor.update(now, now < 110000 ? 745 : 710, now != 150000, true));
    }
    assert(monitor.update(210000, 710, true, true));
    assert(!monitor.update(210001, 700, false, false));
    assert(!monitor.update(500000, 400, true, true));
    assert(!monitor.update(630000, 400, true, true));

    ScreenIdle screen;
    assert(!screen.update(59999, 1, false) && screen.update(60000, 1, false));
    assert(!screen.touch(60010, true) && !screen.off());
    assert(!screen.touch(60020, false)); // A single released sample could be contact bounce.
    assert(!screen.touch(60030, true));
    assert(!screen.touch(60040, false) && !screen.touch(60060, false));
    assert(screen.touch(60080, true));
    screen.touch(60100, false);
    assert(screen.update(120080, 1, false));
    assert(!screen.update(120090, 1, true));
    assert(!screen.update(300000, 1, true));
    assert(!screen.update(300100, 1, false));

    History history;
    for (unsigned i = 0; i <= 96; ++i) history.add(i * 30000, {static_cast<std::int16_t>(i), 10, 20});
    assert(history.size() == 49 && history.intervalSeconds() == 60);
    assert(history[0].chamber == 0 && history[48].chamber == 96);

    SelfTest test;
    auto inputs = ready;
    inputs.lidOpen = true;
    inputs.thermistorTenths = 300;
    test.start(TestItem::count, 0);
    assert(test.outputs().ventMask == 1 && test.outputs().vents == VentTarget::open);
    test.update(1100, inputs, 1, false, true);
    assert(test.outputs().vents == VentTarget::closed);
    test.update(2200, inputs, 1, false, true);
    assert(test.outputs().ventMask == 2 && test.outputs().vents == VentTarget::open);
    test.update(3300, inputs, 2, false, true);
    test.update(4400, inputs, 2, false, true);
    assert(test.item() == TestItem::fan && test.outputs().fan);
    test.update(4600, inputs, 0, true, true);
    test.update(4605, inputs, 0, true, true);
    assert(test.heatRequested(inputs) && test.outputs().diagnosticHeat && !test.outputs().lidDetection);
    inputs.thermistorTenths = 330;
    test.update(9000, inputs, 0, true, true);
    assert(test.result(TestItem::heater) == TestResult::passed && !test.heatRequested(inputs));
    test.update(11000, inputs, 0, true, true);
    assert(test.result(TestItem::rotation) == TestResult::manual);
    test.confirm(11020, true);
    assert(test.takeBuzzer());
    test.update(14500, inputs, 0, false, true);
    test.confirm(14520, true);
    inputs.humidityTenths = 250;
    test.update(14540, inputs, 0, false, true);
    assert(!test.active() && test.result(TestItem::sensors) == TestResult::passed);
    test.start(TestItem::heater, 0);
    inputs.thermistorValid = false;
    test.update(5, inputs, 0, false, true);
    assert(!test.heatRequested(inputs) && test.result(TestItem::heater) == TestResult::failed);
    inputs.thermistorValid = true;
    inputs.thermistorTenths = 401;
    test.start(TestItem::heater, 0);
    test.update(5, inputs, 0, true, true);
    assert(!test.heatRequested(inputs) && test.result(TestItem::heater) == TestResult::failed);
}

}

namespace h1::cold::journalFlash {
std::uint32_t read(unsigned word) {
    return flash.at(word);
}
bool program(unsigned word, std::uint32_t value) {
    if (writesBeforeFailure == 0) return false;
    if (writesBeforeFailure > 0) --writesBeforeFailure;
    assert(flash.at(word) == erased);
    flash.at(word) = value;
    return true;
}
bool erase(unsigned page) {
    for (unsigned i = page * pageWords; i < (page + 1) * pageWords; ++i) flash.at(i) = erased;
    return true;
}
}

int main() {
    testPresets();
    testManualFan();
    testTiming();
    testDryingAt85();
    testWarmAndWrap();
    testInterlocks();
    testCustom();
    testExtendedCustomStorage();
    testFanWarmup();
    testChamberCadence();
    testHeat();
    testRecoveryAndJournal();
    testPagePreparation();
    testHumidityAndLegacyTiming();
    testSaveCurrentProfile();
    testProfilesAndFaultHistory();
    testHomeTemperature();
    testService();
    std::puts("Cycle, chamber sampling, interlocks, power loss and two-page journal passed.");
}

