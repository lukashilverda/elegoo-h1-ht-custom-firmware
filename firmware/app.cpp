#include "beta_ui.hpp"
#include "board.hpp"
#include "cold_outputs.hpp"
#include "hal/buzzer.h"
#include "hal/display.h"
#include "heat_control.hpp"
#include "hal/thermistor.h"
#include "hal/touchscreen.h"
#include "humidity_sensor.hpp"
#include "storage.hpp"

#include <cstring>

namespace h1::cold {
namespace {

constexpr unsigned drawRows = 24;
constexpr std::uint32_t thermistorPeriodMs = 200;
lv_color_t pixels[board::screenWidth * drawRows];
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t displayDriver;
lv_indev_drv_t pointerDriver;
ScreenIdle screenIdle;
bool backlightOn = true;

void applyBacklight() {
    const bool enabled = !screenIdle.off();
    if (backlightOn != enabled) {
        backlightOn = enabled;
        board::setBacklight(enabled);
    }
}
static_assert(LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0 && sizeof(lv_color_t) == 2);

void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* data) {
    const auto width = static_cast<unsigned>(area->x2 - area->x1 + 1);
    const auto height = static_cast<unsigned>(area->y2 - area->y1 + 1);
    board::writePixels(static_cast<unsigned>(area->x1), static_cast<unsigned>(area->y1), width, height, data);
    lv_disp_flush_ready(driver);
}

void touch(lv_indev_drv_t* driver, lv_indev_data_t* data) {
    auto& touchscreen = *static_cast<board::Touchscreen*>(driver->user_data);
    board::TouchPoint point{};
    const bool down = screenIdle.touch(board::milliseconds(), touchscreen.read(point));
    applyBacklight();
    if (down) {
        data->point.x = static_cast<lv_coord_t>(point.x);
        data->point.y = static_cast<lv_coord_t>(point.y);
    }
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void initializeLvgl(board::Touchscreen& touchscreen) {
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, pixels, nullptr, board::screenWidth * drawRows);
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = board::screenWidth;
    displayDriver.ver_res = board::screenHeight;
    displayDriver.draw_buf = &drawBuffer;
    displayDriver.flush_cb = flush;
    displayDriver.full_refresh = 0;
    lv_disp_drv_register(&displayDriver);

    lv_indev_drv_init(&pointerDriver);
    pointerDriver.type = LV_INDEV_TYPE_POINTER;
    pointerDriver.user_data = &touchscreen;
    pointerDriver.read_cb = touch;
    lv_indev_drv_register(&pointerDriver);
}

int temperatureHundredths(float celsius) {
    const auto scaled = celsius * 100.0f;
    return static_cast<int>(scaled + (scaled < 0.0f ? -0.5f : 0.5f));
}

class Sensors {
public:
    explicit Sensors(HumiditySensor& chamber) : chamber_(chamber) {}

    Inputs poll(std::uint32_t now) {
        ChamberReading sample{};
        const auto result = chamber_.poll(now, sample);
        if (result == HumidityPollResult::sample && sample.status == HumidityStatus::ready) {
            chamberTenths_ = static_cast<int>(sample.temperatureCelsius * 10.0f);
            reading_.chamberHundredths = temperatureHundredths(sample.temperatureCelsius);
            reading_.humidityTenths = static_cast<int>(sample.relativeHumidityPercent * 10.0f);
            if (reading_.chamberValid) {
                // Preserve the roughly five-second smoothing with 200 ms samples.
                filtered_ = (26 * filtered_ + chamberTenths_ * 256) / 27;
            } else {
                filtered_ = chamberTenths_ * 256;
            }
            chamberFailures_ = 0;
            chamberAt_ = now;
            reading_.chamberValid = true;
        } else if (result != HumidityPollResult::waiting && chamberFailures_ < 2) {
            ++chamberFailures_;
            if (chamberFailures_ == 2) reading_.chamberValid = false;
        }
        if (firstPoll_ || now - reading_.thermistorAt >= thermistorPeriodMs) {
            const auto th = hal::thermistor::read();
            thermistorTenths_ = static_cast<int>(th.temperatureCelsius * 10.0f);
            reading_.thermistorHundredths = temperatureHundredths(th.temperatureCelsius);
            const bool valid = th.withinCalibrationRange && thermistorTenths_ > -150;
            reading_.thermistorValid = valid;
            reading_.thermistorAt = now;
        }
        if (hardware::takeLidOpen()) {
            lastLidOpen_ = now;
            sawOpenLid_ = true;
        } else if (sawOpenLid_ && now - lastLidOpen_ >= 80) {
            sawOpenLid_ = false;
        }
        firstPoll_ = false;
        constexpr int ceiling = static_cast<int>(thermalSafetyCeiling) * 10;
        return {reading_.chamberValid, filtered_ / 256, reading_.thermistorValid, sawOpenLid_,
                hardware::takeFanPulse(), hardware::ventFaults(),
                (reading_.thermistorValid && thermistorTenths_ > ceiling) ||
                (reading_.chamberValid && chamberTenths_ > ceiling), thermistorTenths_,
                reading_.chamberValid && chamberFailures_ == 0 && now - chamberAt_ < 2000 ?
                reading_.humidityTenths : -1};
    }

    std::uint32_t heatBudget(std::uint32_t now, bool diagnostic) const {
        constexpr std::uint32_t chamberValidMs = 2000;
        constexpr std::uint32_t thermistorValidMs = 330;
        const auto chamberAge = now - chamberAt_;
        const auto thermistorAge = now - reading_.thermistorAt;
        if (!reading_.chamberValid || chamberFailures_ != 0 || !reading_.thermistorValid ||
            (sawOpenLid_ && !diagnostic) || chamberAge >= chamberValidMs || thermistorAge >= thermistorValidMs ||
            chamberTenths_ >= static_cast<int>(thermalSafetyCeiling) * 10 ||
            thermistorTenths_ >= static_cast<int>(thermalSafetyCeiling) * 10) {
            return 0;
        }
        const auto chamberRemaining = chamberValidMs - chamberAge;
        const auto thermistorRemaining = thermistorValidMs - thermistorAge;
        return chamberRemaining < thermistorRemaining ? chamberRemaining : thermistorRemaining;
    }

    const Readings& values() const { return reading_; }
    int thermistorTenths() const { return thermistorTenths_; }

private:
    HumiditySensor& chamber_;
    Readings reading_{};
    unsigned chamberFailures_ = 0;
    std::uint32_t chamberAt_ = 0;
    std::uint32_t lastLidOpen_ = 0;
    int filtered_ = 0;
    int chamberTenths_ = 0;
    int thermistorTenths_ = 0;
    bool sawOpenLid_ = false;
    bool firstPoll_ = true;
};

enum class Sound { none, boot, start, finished, fault, test };
struct Note { unsigned frequency; unsigned duration; };
struct Melody { const Note* notes; unsigned count; };
constexpr unsigned steadyBuzzer = 1;

unsigned soundCategory(Sound sound) {
    switch (sound) {
    case Sound::boot: return soundBoot;
    case Sound::start: return soundStart;
    case Sound::finished: return soundStop;
    case Sound::fault: return soundError;
    default: return 0;
    }
}

template<unsigned Count>
constexpr Melody melodyOf(const Note (&notes)[Count]) {
    return {notes, Count};
}

Melody melodyFor(Sound sound) {
    constexpr static Note boot[]{
        {960, 90}, {0, 65}, {960, 90}
    };
    constexpr static Note start[]{
        {988, 90}, {0, 18}, {1319, 110}, {0, 18}, {1568, 170},
        {0, 30}, {steadyBuzzer, 35}, {0, 30}, {steadyBuzzer, 60}
    };
    constexpr static Note finished[]{
        {1568, 110}, {0, 25}, {1319, 150}, {0, 35}, {steadyBuzzer, 65}
    };
    constexpr static Note fault[]{
        {880, 135}, {0, 45}, {1320, 135}, {0, 45}, {1760, 230}, {0, 250},
        {880, 135}, {0, 45}, {1320, 135}, {0, 45}, {1760, 230}
    };
    constexpr static Note test[]{
        {600, 180}, {0, 90}, {1000, 180}, {0, 90}, {1500, 180}
    };
    switch (sound) {
    case Sound::boot: return melodyOf(boot);
    case Sound::start: return melodyOf(start);
    case Sound::finished: return melodyOf(finished);
    case Sound::fault: return melodyOf(fault);
    case Sound::test: return melodyOf(test);
    case Sound::none: return {};
    }
    return {};
}

class Application {
public:
    Application(Journal& journal, Sensors& sensors, bool watchdogReady) :
        journal_(journal), sensors_(sensors), cycle_(journal.load()), faults_(journal.faults()), watchdogReady_(watchdogReady) {
        go(Page::boot);
    }

    void update() {
        const auto now = board::milliseconds();
        const auto inputs = sensors_.poll(now);
        auto action = ui_.takeAction();
        const auto before = cycle_.phase();
        const bool testing = test_.active();
        const auto heatBudget = sensors_.heatBudget(now, testing);
        cycle_.update(now, inputs);
        if (testing) {
            test_.update(now, inputs, hardware::ventsReached(), hardware::airflowReady(), playing_ != Sound::test);
            if (test_.takeBuzzer()) play(Sound::test, now);
            if (!test_.active()) cycle_.stop();
            hardware::publish(test_.active() ? test_.outputs() : cycle_.outputs(now), now, false,
                              test_.heatRequested(inputs) && watchdogReady_ ? heatBudget : 0);
        } else {
            const bool heatingPermitted = watchdogReady_ && cycle_.phase() == Phase::drying &&
                                          heatBudget > 0 && hardware::airflowReady();
            const auto target = cycle_.settings.targetCelsius();
            const auto thermistor = sensors_.thermistorTenths();
            const auto th = static_cast<float>(thermistor) / 10.0f;
            const bool heat = heater_.demand(now, target, th, sensors_.values().chamberHundredths, heatingPermitted);
            const auto targetTenths = static_cast<int>(target) * 10;
            const bool warmup = cycle_.phase() == Phase::drying && heatBudget > 0 &&
                                inputs.filteredChamberTenths <= targetTenths - 50 && thermistor <= targetTenths - 60;
            const bool monitorPermitted = heatingPermitted && !inputs.ventFaults;
            if (heatMonitor_.update(now, thermistor, heat && hardware::heaterActive(), monitorPermitted)) {
                cycle_.thermostatFault(now);
                hardware::inhibitHeat();
            }
            hardware::publish(cycle_.outputs(now), now, false, cycle_.running() && heat ? heatBudget : 0, warmup);
        }

        if (cycle_.running()) sampleChart(inputs);
        if (cycle_.phase() != before) {
            checkpoint_ = true;
            if (cycle_.phase() == Phase::finished) {
                finishHistory();
                action = Action::none;
                go(Page::finished);
                play(Sound::finished, now);
            } else if (cycle_.notice() != Notice::none) {
                action = Action::none;
                if (cycle_.notice() == Notice::lid) go(Page::lid);
                else {
                    recordFault(cycle_.notice(), now);
                    go(Page::fault);
                }
            }
        }
        if (page_ == Page::boot) {
            if (!bootStarted_) {
                bootAt_ = now;
                bootStarted_ = true;
                play(Sound::boot, now);
            }
            if (now - bootAt_ >= 450) {
                go(cycle_.phase() == Phase::recovery ? Page::recovery : Page::home);
            }
        }

        dispatch(action, now, inputs);
        if (deferredSave_ && page_ != Page::sound && page_ != Page::display) checkpoint_ = true;
        const bool rotating = testing || cycle_.outputs(now).motor;
        if (!rotating && journal_.needsPreparation() && now - prepareAt_ >= 5000) {
            prepareAt_ = now;
            hardware::inhibitHeat();
            journal_.prepareNextPage();
        }
        const bool saveDue = checkpoint_ || (deferredSave_ && now - settingsChangedAt_ >= 750) || (retrySave_ ? now - saveAt_ >= 5000 :
                              (cycle_.running() && now - saveAt_ >= 30000));
        if (saveDue && (!rotating || journal_.canSaveWithoutErase())) {
            checkpoint_ = false;
            deferredSave_ = false;
            saveAt_ = now;
            const bool saved = save();
            if (!saved && !retrySave_ && !faultPage(page_) && page_ != Page::lid && page_ != Page::finished) {
                retrySave_ = true;
                alert(Page::storageError, page_);
            }
            retrySave_ = !saved;
        }

        updateSound(board::milliseconds());
        ui_.update(cycle_, sensors_.values());
        if (page_ == Page::selfTest) ui_.updateTest(test_);
        if (page_ == Page::graph) ui_.updateChart(selectedHistory(), cycle_.settings.fahrenheit);
        const bool keepAwake = page_ == Page::boot || test_.active() || faultPage(page_) ||
                               (page_ == Page::faultLog && faultPage(logReturn_));
        screenIdle.update(now, sleepMinutes[cycle_.settings.displayTimeout], keepAwake);
        applyBacklight();
    }

private:
    const History& selectedHistory() const {
        return previousGraph_ ? lastHistory_ : currentHistory_;
    }

    void sampleChart(const Inputs& inputs) {
        const auto& values = sensors_.values();
        const auto elapsedSeconds = cycle_.totalSeconds() - cycle_.seconds();
        if (!chartClockSet_) {
            chartClockSet_ = true;
            chartOrigin_ = elapsedSeconds;
        }
        currentHistory_.add((elapsedSeconds - chartOrigin_) * 1000, {
            values.chamberValid ? static_cast<std::int16_t>(values.chamberHundredths / 10) : ChartSample::missing,
            values.thermistorValid ? static_cast<std::int16_t>(values.thermistorHundredths / 10) : ChartSample::missing,
            inputs.humidityTenths < 0 ? ChartSample::missing : static_cast<std::int16_t>(inputs.humidityTenths)
        });
    }

    void finishHistory() {
        if (currentHistory_.size()) lastHistory_ = currentHistory_;
    }

    static bool faultPage(Page page) {
        return page == Page::fault || page == Page::storageError || page == Page::tooHot ||
               page == Page::controlUnavailable;
    }

    void recordFault(Notice notice, std::uint32_t now) {
        const auto& values = sensors_.values();
        faults_.add(notice, values.thermistorValid ? values.thermistorHundredths / 10 : FaultHistory::noTemperature);
        if (!retrySave_) checkpoint_ = true;
        play(Sound::fault, now);
    }

    void go(Page page) {
        page_ = page;
        ui_.show(page_, cycle_, {&draft_, &faults_, &test_, &selectedHistory(), previousGraph_,
                                 editProfile_ < savedProfileCount && cycle_.settings.filaments[editProfile_].exists(),
                                 logOffset_, namedProfiles_});
    }

    void alert(Page alertPage, Page returnPage) {
        alertReturn_ = returnPage;
        go(alertPage);
        Notice code = Notice::none;
        switch (alertPage) {
        case Page::storageError: code = Notice::storage; break;
        case Page::controlUnavailable: code = Notice::controller; break;
        case Page::tooHot: code = Notice::overTemperature; break;
        default: break;
        }
        if (code != Notice::none) recordFault(code, board::milliseconds());
    }

    void setNote() {
        const auto frequency = melody_.notes[noteIndex_].frequency;
        const auto volume = playing_ == Sound::test && !cycle_.settings.volume ? 50u : cycle_.settings.volume;
        hal::buzzer::setVolume(volume);
        if (frequency == 0) hal::buzzer::setEnabled(false);
        else if (frequency == steadyBuzzer && volume == 100) hal::buzzer::setEnabled(true);
        else hal::buzzer::setTone(frequency == steadyBuzzer ? 1000 : frequency);
    }

    void play(Sound sound, std::uint32_t now) {
        const auto category = soundCategory(sound);
        if (category && (!(cycle_.settings.soundMask & category) || !cycle_.settings.volume)) return;
        if (playing_ == Sound::fault && sound != Sound::fault && sound != Sound::test) return;
        playing_ = sound;
        melody_ = melodyFor(sound);
        noteIndex_ = 0;
        noteAt_ = now;
        setNote();
    }

    void silence() {
        playing_ = Sound::none;
        hal::buzzer::setEnabled(false);
    }

    void updateSound(std::uint32_t now) {
        if (playing_ == Sound::none) return;
        const auto previous = noteIndex_;
        while (now - noteAt_ >= melody_.notes[noteIndex_].duration) {
            noteAt_ += melody_.notes[noteIndex_].duration;
            if (++noteIndex_ == melody_.count) {
                silence();
                return;
            }
        }
        if (noteIndex_ != previous) setNote();
    }

    bool save() {
        // Internal flash operations can stall instruction fetch and interrupts.
        hardware::inhibitHeat();
        return journal_.save(cycle_.snapshot(), faults_);
    }

    void start(std::uint32_t now, const Inputs& inputs) {
        if (test_.active()) return;
        const auto previous = cycle_;
        const auto recovery = cycle_.phase() == Phase::recovery;
        const auto returnPage = recovery ? Page::recovery : Page::home;
        if (!watchdogReady_) {
            alert(Page::controlUnavailable, returnPage);
            return;
        }
        switch (cycle_.start(now, inputs)) {
        case StartResult::unavailable: return;
        case StartResult::closeLid: alert(Page::lid, returnPage); return;
        case StartResult::tooHot: alert(Page::tooHot, returnPage); return;
        case StartResult::waitingForSensors: alert(Page::waitForSensors, returnPage); return;
        case StartResult::started: break;
        }
        if (!save()) {
            cycle_ = previous;
            alert(Page::storageError, returnPage);
            return;
        }
        saveAt_ = now;
        retrySave_ = false;
        const auto outputNow = board::milliseconds();
        hardware::publish(cycle_.outputs(outputNow), outputNow, true);
        go(Page::home);
        if (previous.phase() == Phase::idle) {
            currentHistory_.clear();
            chartClockSet_ = false;
            play(Sound::start, outputNow);
        }
        heatMonitor_.reset();
    }

    void stop(std::uint32_t now, bool chime = true) {
        finishHistory();
        const bool active = cycle_.cycleExists();
        cycle_.stop();
        hardware::publish(cycle_.outputs(now), now);
        checkpoint_ = true;
        go(Page::home);
        if (active && chime) play(Sound::finished, now);
    }

    void confirm(std::uint32_t now, const Inputs& inputs) {
        switch (page_) {
        case Page::start:
        case Page::recovery: start(now, inputs); break;
        case Page::stop: stop(now); break;
        case Page::temperature:
        case Page::custom:
            applyCustom();
            break;
        case Page::profileProgram:
            storeNamed(now, Page::presets);
            break;
        case Page::profileLimit:
            go(Page::presets);
            break;
        case Page::deleteProfile:
            if (editProfile_ < savedProfileCount) {
                if (cycle_.settings.profile == firstSavedProfile + editProfile_) cycle_.selectProfile(0);
                cycle_.settings.filaments[editProfile_] = {};
                checkpoint_ = true;
            }
            go(Page::presets);
            break;
        case Page::unlock:
            if (!cycle_.cycleExists()) {
                cycle_.settings.setHighTemperature(true);
                checkpoint_ = true;
            }
            go(Page::advanced);
            break;
        case Page::reset:
            cycle_.restoreDefaults();
            faults_ = {};
            currentHistory_.clear();
            lastHistory_.clear();
            if (playing_ != Sound::none) silence();
            hardware::publish(cycle_.outputs(now), now, true);
            checkpoint_ = true;
            go(Page::settings);
            break;
        case Page::lid:
            if (cycle_.notice() == Notice::lid) {
                cycle_.acknowledge();
                go(Page::home);
            } else {
                go(alertReturn_);
            }
            break;
        case Page::fault:
            if (cycle_.notice() == Notice::thermostat) {
                stop(now, false);
                break;
            }
            [[fallthrough]];
        case Page::finished:
            cycle_.acknowledge();
            go(Page::home);
            break;
        case Page::storageError:
        case Page::controlUnavailable:
        case Page::tooHot:
        case Page::waitForSensors:
        case Page::testUnavailable: go(alertReturn_); break;
        default: break;
        }
    }

    void back(std::uint32_t now) {
        switch (page_) {
        case Page::presets: go(presetReturn_); break;
        case Page::temperature:
        case Page::custom: go(customReturn_); break;
        case Page::profileName:
        case Page::profileLimit: go(Page::presets); break;
        case Page::profileProgram:
        case Page::deleteProfile: go(Page::profileName); break;
        case Page::unlock: go(Page::advanced); break;
        case Page::graph: go(graphReturn_); break;
        case Page::faultLog: go(logReturn_); break;
        case Page::selfTest:
            cancelTest(now);
            go(Page::diagnostics);
            break;
        case Page::sound:
        case Page::advanced:
        case Page::display:
        case Page::about:
        case Page::reset:
        case Page::units:
        case Page::diagnostics: go(Page::settings); break;
        case Page::recovery: stop(now); break;
        default: go(Page::home); break;
        }
    }

    Filament selectedProgram() const {
        return {{}, cycle_.settings.targetCelsius(), cycle_.settings.durationSeconds() / 60,
                cycle_.settings.humidityTarget()};
    }

    void restrictDraft() {
        const auto limit = cycle_.settings.temperatureLimit();
        if (draft_.temperature > limit) draft_.temperature = limit;
    }

    void applyCustom() {
        const auto selected = selectedProgram();
        if (selected.temperature != draft_.temperature || selected.minutes != draft_.minutes ||
            selected.humidity != draft_.humidity) {
            cycle_.selectCustom(draft_.temperature, draft_.minutes, draft_.humidity);
            checkpoint_ = true;
        }
        go(customReturn_);
    }

    void editNamed(unsigned index) {
        if (cycle_.phase() != Phase::idle || !cycle_.settings.filaments[index].exists()) return;
        editProfile_ = index;
        draft_ = cycle_.settings.filaments[index];
        humidityDraft_ = draft_.humidity ? draft_.humidity : 20;
        restrictDraft();
        go(Page::profileName);
    }

    void saveCurrentProfile() {
        if (page_ != Page::presets || cycle_.phase() != Phase::idle) return;
        for (unsigned i = 0; i < savedProfileCount; ++i) {
            if (cycle_.settings.filaments[i].exists()) continue;
            editProfile_ = i;
            draft_ = selectedProgram();
            go(Page::profileName);
            return;
        }
        namedProfiles_ = true;
        go(Page::profileLimit);
    }

    void storeNamed(std::uint32_t now, Page next) {
        const auto previous = cycle_.settings;
        if (!cycle_.saveProfile(editProfile_, draft_)) return;
        if (!save()) {
            cycle_.settings = previous;
            alert(Page::storageError, page_);
            return;
        }
        checkpoint_ = false;
        retrySave_ = false;
        deferredSave_ = false;
        saveAt_ = now;
        namedProfiles_ = true;
        go(next);
    }

    void continueProfile(std::uint32_t now) {
        if (page_ != Page::profileName || editProfile_ >= savedProfileCount) return;
        const char* name = ui_.profileNameDraft();
        while (*name == ' ') ++name;
        auto length = std::strlen(name);
        while (length && name[length - 1] == ' ') --length;
        if (!length) return;
        std::memset(draft_.name, 0, sizeof draft_.name);
        std::memcpy(draft_.name, name, length <= profileNameLength ? length : profileNameLength);
        if (cycle_.settings.filaments[editProfile_].exists()) go(Page::profileProgram);
        else storeNamed(now, presetReturn_);
    }

    void startTest(TestItem item, std::uint32_t now) {
        if (test_.active()) return;
        if (cycle_.cycleExists()) {
            alert(Page::testUnavailable, page_);
            return;
        }
        if (!watchdogReady_ && (item == TestItem::heater || item == TestItem::count)) {
            alert(Page::controlUnavailable, page_);
            return;
        }
        cycle_.setManualFan(0, false);
        test_.start(item, now);
        hardware::publish(test_.outputs(), now, true);
        if (test_.takeBuzzer()) play(Sound::test, now);
        go(Page::selfTest);
    }

    void cancelTest(std::uint32_t now) {
        if (!test_.active()) return;
        test_.cancel();
        if (playing_ == Sound::test) silence();
        cycle_.stop();
        hardware::publish(cycle_.outputs(now), now);
    }

    void dispatch(Action action, std::uint32_t now, const Inputs& inputs) {
        const auto index = static_cast<unsigned>(action);
        const auto first = static_cast<unsigned>(Action::presetBase);
        const auto edit = static_cast<unsigned>(Action::profileEditBase);
        const auto test = static_cast<unsigned>(Action::testBase);
        if (index >= first && index - first < firstSavedProfile + savedProfileCount) {
            if (page_ != Page::presets) return;
            const auto previous = cycle_.settings.profile;
            cycle_.selectProfile(index - first);
            if (cycle_.settings.profile != previous) {
                checkpoint_ = true;
                go(Page::presets);
            }
            return;
        }
        if (index >= edit && index - edit < savedProfileCount) {
            if (page_ == Page::presets) editNamed(index - edit);
            return;
        }
        if (index >= test && index - test <= SelfTest::count) {
            if (index - test == SelfTest::count) go(Page::selfTest);
            else startTest(static_cast<TestItem>(index - test), now);
            return;
        }
        switch (action) {
        case Action::home: go(Page::home); break;
        case Action::options: go(Page::options); break;
        case Action::settings: go(Page::settings); break;
        case Action::presets:
            presetReturn_ = page_;
            namedProfiles_ = cycle_.settings.named();
            go(Page::presets);
            break;
        case Action::builtInProfiles:
        case Action::myProfiles:
            if (page_ == Page::presets) {
                namedProfiles_ = action == Action::myProfiles;
                go(Page::presets);
            }
            break;
        case Action::editTemperature:
        case Action::editCustom:
            if (cycle_.phase() == Phase::idle) {
                customReturn_ = page_;
                draft_ = selectedProgram();
                humidityDraft_ = draft_.humidity ? draft_.humidity : 20;
                go(action == Action::editTemperature ? Page::temperature : Page::custom);
            }
            break;
        case Action::saveCurrentProfile: saveCurrentProfile(); break;
        case Action::continueProfile: continueProfile(now); break;
        case Action::deleteProfile: go(Page::deleteProfile); break;
        case Action::setTemperature:
            draft_.temperature = customTemperatureMin + ui_.sliderValue() * customTemperatureStep;
            ui_.updateCustom(draft_, cycle_.settings.fahrenheit);
            break;
        case Action::setTime:
            draft_.minutes = ui_.sliderValue() * customTimeStepMinutes;
            ui_.updateCustom(draft_, cycle_.settings.fahrenheit);
            break;
        case Action::setHumidity:
            draft_.humidity = humidityDraft_ = ui_.sliderValue();
            ui_.updateCustom(draft_, cycle_.settings.fahrenheit);
            break;
        case Action::timedMode:
        case Action::humidityMode: {
            const auto humidity = action == Action::timedMode ? 0u : humidityDraft_;
            if (humidity != draft_.humidity) {
                draft_.humidity = humidity;
                go(page_);
            }
            break;
        }
        case Action::back: back(now); break;
        case Action::start: go(Page::start); break;
        case Action::pause:
            cycle_.pause(now);
            hardware::publish(cycle_.outputs(now), now);
            checkpoint_ = true;
            go(cycle_.phase() == Phase::finished ? Page::finished : Page::home);
            break;
        case Action::resume: start(now, inputs); break;
        case Action::stop: go(Page::stop); break;
        case Action::confirm: confirm(now, inputs); break;
        case Action::units: go(Page::units); break;
        case Action::celsius:
        case Action::fahrenheit:
            cycle_.settings.fahrenheit = action == Action::fahrenheit;
            checkpoint_ = true;
            go(Page::settings);
            break;
        case Action::rotate:
            cycle_.settings.rotation = !cycle_.settings.rotation;
            checkpoint_ = true;
            break;
        case Action::lid:
            cycle_.settings.lidDetection = !cycle_.settings.lidDetection;
            checkpoint_ = true;
            break;
        case Action::manualFan: cycle_.setManualFan(ui_.sliderValue(), inputs.lidOpen); break;
        case Action::sound: go(Page::sound); break;
        case Action::soundBoot:
        case Action::soundStart:
        case Action::soundStop:
        case Action::soundError:
            cycle_.settings.soundMask ^= 1u << (index - static_cast<unsigned>(Action::soundBoot));
            if (playing_ != Sound::none && !(cycle_.settings.soundMask & soundCategory(playing_))) silence();
            checkpoint_ = true;
            go(Page::sound);
            break;
        case Action::volume:
            cycle_.settings.volume = ui_.sliderValue() * 10;
            if (!cycle_.settings.volume && playing_ != Sound::none) silence();
            settingsChangedAt_ = now;
            deferredSave_ = true;
            ui_.updateVolume(cycle_.settings.volume);
            break;
        case Action::display: go(Page::display); break;
        case Action::displayTimeout:
            cycle_.settings.displayTimeout = ui_.sliderValue();
            settingsChangedAt_ = now;
            deferredSave_ = true;
            break;
        case Action::advanced: go(Page::advanced); break;
        case Action::changeLimit:
            if (!cycle_.cycleExists() && cycle_.settings.highTemperature) {
                cycle_.settings.setHighTemperature(false);
                checkpoint_ = true;
                go(Page::advanced);
            } else if (!cycle_.cycleExists()) {
                go(Page::unlock);
            }
            break;
        case Action::allTests: startTest(TestItem::count, now); break;
        case Action::testPassed:
        case Action::testFailed:
            test_.confirm(now, action == Action::testPassed);
            if (!test_.active()) {
                cycle_.stop();
                hardware::publish(cycle_.outputs(now), now);
            }
            break;
        case Action::faultLog:
            logReturn_ = page_;
            logOffset_ = 0;
            go(Page::faultLog);
            break;
        case Action::nextLog:
            if (logOffset_ + faultRowsPerPage < faults_.size()) logOffset_ += faultRowsPerPage;
            go(Page::faultLog);
            break;
        case Action::previousLog:
            if (logOffset_ >= faultRowsPerPage) logOffset_ -= faultRowsPerPage;
            go(Page::faultLog);
            break;
        case Action::coolFan:
            if (cycle_.notice() == Notice::thermostat) {
                stop(now, false);
                go(Page::options);
            }
            break;
        case Action::graph:
            graphReturn_ = page_;
            previousGraph_ = !cycle_.cycleExists();
            go(Page::graph);
            break;
        case Action::previousGraph:
            previousGraph_ = !previousGraph_;
            go(Page::graph);
            break;
        case Action::about: go(Page::about); break;
        case Action::reset: go(Page::reset); break;
        case Action::diagnostics: go(Page::diagnostics); break;
        case Action::chamber: go(Page::chamber); break;
        default: break;
        }
    }

    Journal& journal_;
    Sensors& sensors_;
    Cycle cycle_;
    FaultHistory faults_;
    const bool watchdogReady_;
    HeatMonitor heatMonitor_;
    SelfTest test_;
    History currentHistory_;
    History lastHistory_;
    HeatControl heater_;
    Ui ui_;
    Page page_ = Page::boot;
    Page presetReturn_ = Page::home;
    Page alertReturn_ = Page::home;
    Page customReturn_ = Page::home;
    Page logReturn_ = Page::settings;
    Page graphReturn_ = Page::diagnostics;
    Filament draft_{};
    unsigned editProfile_ = savedProfileCount;
    unsigned humidityDraft_ = 20;
    bool namedProfiles_ = false;
    unsigned logOffset_ = 0;
    unsigned chartOrigin_ = 0;
    bool previousGraph_ = false;
    bool chartClockSet_ = false;
    Sound playing_ = Sound::none;
    Melody melody_{};
    unsigned noteIndex_ = 0;
    std::uint32_t bootAt_ = 0;
    std::uint32_t noteAt_ = 0;
    std::uint32_t saveAt_ = 0;
    std::uint32_t prepareAt_ = 0;
    std::uint32_t settingsChangedAt_ = 0;
    bool deferredSave_ = false;
    bool bootStarted_ = false;
    bool checkpoint_ = false;
    bool retrySave_ = false;
};

}  // namespace
}  // namespace h1::cold

extern "C" void firmwareTick(std::uint32_t now) {
    h1::cold::hardware::tick(now);
}

extern "C" void firmwareFault() {
    h1::cold::hardware::emergencyOff();
    h1::hal::buzzer::setEnabled(false);
}

extern "C" void runFirmware() {
    using namespace h1;
    using namespace h1::cold;

    hardware::initialize();
    hal::buzzer::initialize();
    board::initializeDisplay();
    board::I2cBus touchBus(6, 7);
    board::Touchscreen touchscreen(touchBus);
    board::I2cBus chamberBus(10, 11);
    HumiditySensor chamber(chamberBus, board::milliseconds());
    Journal journal;
    Sensors sensors(chamber);

    initializeLvgl(touchscreen);
    const bool watchdogReady = board::startWatchdog();
    Application app(journal, sensors, watchdogReady);
    for (;;) {
        app.update();
        lv_timer_handler();
        if (watchdogReady) board::feedWatchdog();
        board::delay(5);
    }
}


extern "C" [[noreturn]] void Default_Handler();

extern "C" std::uint32_t firmwareUiMilliseconds() {
    return h1::board::milliseconds();
}

extern "C" void firmwareUiFault() {
    Default_Handler();
}
