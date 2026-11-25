#pragma once

#include "service.hpp"
#include "lvgl.h"

namespace h1::cold {

enum class Page {
    boot, home, options, settings, presets, start, units, about, reset, stop,
    finished, fault, lid, diagnostics, chamber, recovery, waitForSensors, storageError, controlUnavailable, tooHot, custom, advanced,
    unlock, sound, display, faultLog, selfTest, testUnavailable, graph, profileName, profileProgram, deleteProfile, temperature, profileLimit
};

enum class Action : unsigned {
    none, home, options, settings, presets, back,
    start, pause, resume, stop, confirm, units, celsius, fahrenheit,
    rotate, lid, sound, about, reset, diagnostics, chamber, editCustom, editTemperature,
    setTemperature, setTime, manualFan, advanced, changeLimit, display, displayTimeout,
    soundBoot, soundStart, soundStop, soundError, volume, faultLog, nextLog, previousLog, coolFan, allTests, testPassed, testFailed,
    graph, previousGraph, saveCurrentProfile, continueProfile, deleteProfile, timedMode, humidityMode, setHumidity,
    builtInProfiles, myProfiles,
    presetBase = 64, profileEditBase = 96, testBase = 112
};

struct Readings {
    int chamberHundredths = 0;
    int humidityTenths = 0;
    int thermistorHundredths = 0;
    bool chamberValid = false;
    bool thermistorValid = false;
    std::uint32_t thermistorAt = 0;
};

inline constexpr unsigned faultRowsPerPage = 3;

struct UiContext {
    const Filament* draft = nullptr;
    const FaultHistory* faults = nullptr;
    const SelfTest* test = nullptr;
    const History* history = nullptr;
    bool previousGraph = false;
    bool editingProfile = false;
    unsigned logOffset = 0;
    bool namedProfiles = false;
};

class Ui {
public:
    void show(Page page, const Cycle& cycle, const UiContext& context = {});
    void update(const Cycle& cycle, const Readings& values);
    void updateCustom(const Filament& draft, bool fahrenheit);
    void updateTest(const SelfTest& test);
    void updateChart(const History& history, bool fahrenheit);
    const char* profileNameDraft() const;
    void updateVolume(unsigned volume);
    Action takeAction();
    unsigned sliderValue() const { return sliderValue_; }

private:
    friend struct ScreenBuilder;
    static void clicked(lv_event_t* event);
    void updateHomeAction(const Cycle& cycle);
    void selectTab(Action action);
    lv_coord_t* savedScroll();

    Action action_ = Action::none;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* tabs_[3]{};
    Action selectedTab_ = Action::none;
    lv_obj_t* homeAction_ = nullptr;
    bool homeHasCycle_ = false;
    bool homeFahrenheit_ = false;
    unsigned homeProfile_ = 0;
    unsigned homeTarget_ = 0;
    bool optionsEditable_ = false;
    bool optionsFanAvailable_ = false;
    bool readingsCached_ = false;
    Readings displayed_{};
    HomeTemperature homeTemperature_;
    int displayedHomeTemperature_ = 0;
    bool displayedFahrenheit_ = false;
    lv_obj_t* chamber_ = nullptr;
    lv_obj_t* humidity_ = nullptr;
    lv_obj_t* thermistor_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* progress_ = nullptr;
    unsigned sliderValue_ = 0;
    lv_obj_t* fanValue_ = nullptr;
    lv_obj_t* temperatureSlider_ = nullptr;
    lv_obj_t* timeSlider_ = nullptr;
    lv_obj_t* rotation_ = nullptr;
    lv_obj_t* lid_ = nullptr;
    lv_obj_t* manualFan_ = nullptr;
    Page page_ = Page::boot;
    lv_obj_t* list_ = nullptr;
    lv_coord_t optionsScroll_ = 0;
    lv_coord_t settingsScroll_ = 0;
    lv_coord_t profileScroll_[2]{};
    bool namedProfiles_ = false;
    std::uint32_t readoutAt_ = 0;
    lv_obj_t* nameInput_ = nullptr;
    lv_obj_t* humiditySlider_ = nullptr;
    lv_obj_t* customHumidity_ = nullptr;
    lv_obj_t* volumeLabel_ = nullptr;
    lv_obj_t* testRows_[SelfTest::count]{};
    lv_obj_t* testPrompt_ = nullptr;
    lv_obj_t* testYes_ = nullptr;
    lv_obj_t* testNo_ = nullptr;
    lv_obj_t* chart_ = nullptr;
    lv_obj_t* chartSpan_ = nullptr;
    lv_chart_series_t* series_[3]{};
    unsigned chartCount_ = 0;
    unsigned chartInterval_ = 0;
    lv_obj_t* customTemperature_ = nullptr;
    lv_obj_t* customTime_ = nullptr;
};

}  // namespace h1::cold

