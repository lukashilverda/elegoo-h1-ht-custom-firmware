#include "beta_ui.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace h1::cold;
namespace {
constexpr unsigned width = 480;
constexpr unsigned height = 320;
std::array<lv_color_t, width * height> framebuffer;
std::array<lv_color_t, width * 10> drawPixels;
lv_disp_draw_buf_t drawBuffer;
lv_disp_drv_t screen;
lv_indev_drv_t pointer;
lv_point_t point{};
bool touched = false;
unsigned pixelsFlushed = 0;
unsigned flushWindows = 0;

void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* pixels) {
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) framebuffer[y * width + x] = *pixels++;
    }
    pixelsFlushed += static_cast<unsigned>((area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1));
    ++flushWindows;
    lv_disp_flush_ready(driver);
}

void read(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->state = touched ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->point = point;
}

void tick() {
    lv_tick_inc(40);
    lv_timer_handler();
}

void tap(Ui& ui, int x, int y, Action action) {
    point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    touched = true;
    tick();
    touched = false;
    tick();
    const auto observed = ui.takeAction();
    if (observed != action) {
        std::fprintf(stderr, "Touch at (%d, %d), wanted action %u, received %u\n",
                     x, y, static_cast<unsigned>(action), static_cast<unsigned>(observed));
        assert(observed == action);
    }
}

void swipe(int x, int y, int dx, int dy) {
    touched = true;
    for (int step = 0; step <= 8; ++step) {
        point = {static_cast<lv_coord_t>(x + dx * step / 8), static_cast<lv_coord_t>(y + dy * step / 8)};
        tick();
    }
    touched = false;
    for (int i = 0; i < 20; ++i) tick();
}

bool hasLabel(lv_obj_t* object, const char* label) {
    if (lv_obj_check_type(object, &lv_label_class) &&
        std::strcmp(lv_label_get_text(object), label) == 0) return true;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(object); ++i) {
        if (hasLabel(lv_obj_get_child(object, static_cast<int>(i)), label)) return true;
    }
    return false;
}

void checkProfileSelection(lv_obj_t* selected, lv_obj_t* other) {
    assert(hasLabel(selected, LV_SYMBOL_OK) && !hasLabel(other, LV_SYMBOL_OK));
    assert(lv_obj_get_style_bg_color(selected, 0).full == lv_obj_get_style_bg_color(other, 0).full);
    auto* selectedTemperature = lv_obj_get_child(selected, 1);
    auto* otherTemperature = lv_obj_get_child(other, 1);
    assert(lv_obj_get_style_text_color(selectedTemperature, 0).full ==
           lv_obj_get_style_text_color(otherTemperature, 0).full);
}

void checkShortScroll(Ui& ui, const char* lastRow, int x, int y) {
    auto* screen = lv_scr_act();
    auto* content = lv_obj_get_child(screen, -1);
    auto* list = lv_obj_has_flag(content, LV_OBJ_FLAG_SCROLLABLE) ? content : lv_obj_get_child(content, -1);
    auto* row = lv_obj_get_child(list, -1);
    assert(hasLabel(row, lastRow));
    assert(row->coords.y2 > list->coords.y2);
    swipe(x, y, 0, -48);
    assert(ui.takeAction() == Action::none);
    assert(row->coords.y2 <= list->coords.y2);
}

void saveFrame(const std::string& directory, const char* name) {
    tick();
    auto* file = std::fopen((directory + "/" + name + ".ppm").c_str(), "wb");
    assert(file);
    std::fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (auto pixel : framebuffer) {
        const unsigned char rgb[]{
            static_cast<unsigned char>(pixel.ch.red * 255 / 31),
            static_cast<unsigned char>(pixel.ch.green * 255 / 63),
            static_cast<unsigned char>(pixel.ch.blue * 255 / 31)
        };
        std::fwrite(rgb, 1, sizeof rgb, file);
    }
    std::fclose(file);
}
}

int main(int argc, char* argv[]) {
    assert(argc == 2);
    lv_init();
    lv_disp_draw_buf_init(&drawBuffer, drawPixels.data(), nullptr, drawPixels.size());
    lv_disp_drv_init(&screen);
    screen.hor_res = width;
    screen.ver_res = height;
    screen.flush_cb = flush;
    screen.draw_buf = &drawBuffer;
    lv_disp_drv_register(&screen);
    lv_indev_drv_init(&pointer);
    pointer.type = LV_INDEV_TYPE_POINTER;
    pointer.read_cb = read;
    lv_indev_drv_register(&pointer);

    Cycle cycle;
    Ui ui;
    const std::string output = argv[1];
    const Readings sample{2340, 187, 2675, true, true};

    ui.show(Page::home, cycle);
    ui.update(cycle, sample);
    assert(hasLabel(lv_scr_act(), "27 °C") && hasLabel(lv_scr_act(), "19 %RH"));
    saveFrame(output, "home");
    pixelsFlushed = flushWindows = 0;
    ui.update(cycle, sample);
    tick();
    assert(pixelsFlushed == 0 && flushWindows == 0);
    auto changed = sample;
    lv_tick_inc(1000);
    changed.thermistorHundredths += 100;
    changed.thermistorAt = lv_tick_get();
    ui.update(cycle, changed);
    tick();
    assert(hasLabel(lv_scr_act(), "28 °C"));
    assert(pixelsFlushed > 0 && pixelsFlushed < width * height / 16 && flushWindows > 0);
    std::printf("Single heater reading update flushed %u of %u pixels.\n", pixelsFlushed, width * height);
    tap(ui, 40, 165, Action::options);
    tap(ui, 315, 269, Action::start);
    tap(ui, 203, 97, Action::chamber);
    tap(ui, 195, 184, Action::editTemperature);
    tap(ui, 390, 184, Action::editCustom);

    pixelsFlushed = flushWindows = 0;
    ui.show(Page::options, cycle);
    ui.update(cycle, sample);
    tick();
    assert(pixelsFlushed > 0 && pixelsFlushed < width * height);
    std::printf("Home to Options flushed %u of %u pixels.\n", pixelsFlushed, width * height);
    pixelsFlushed = flushWindows = 0;
    ui.show(Page::options, cycle);
    ui.update(cycle, sample);
    tick();
    assert(pixelsFlushed == 0 && flushWindows == 0);
    saveFrame(output, "options");
    assert(!hasLabel(lv_scr_act(), "Dry to humidity"));
    tap(ui, 390, 235, Action::lid);
    tap(ui, 390, 160, Action::rotate);
    assert(hasLabel(lv_scr_act(), "Manual fan"));
    tap(ui, 312, 88, Action::manualFan);
    assert(ui.sliderValue() > 40 && ui.sliderValue() < 60);

    swipe(312, 88, 82, 0);
    assert(ui.takeAction() == Action::manualFan && ui.sliderValue() == 100);
    checkShortScroll(ui, "Filament", 210, 273);
    assert(ui.takeAction() == Action::none);
    auto* oldList = lv_obj_get_child(lv_obj_get_child(lv_scr_act(), -1), -1);
    const auto savedScroll = lv_obj_get_scroll_y(oldList);
    assert(savedScroll > 0);
    tap(ui, 215, 280, Action::presets);
    ui.show(Page::presets, cycle);
    tick();
    tap(ui, 59, 34, Action::back);
    ui.show(Page::options, cycle);
    tick();
    auto* restoredList = lv_obj_get_child(lv_obj_get_child(lv_scr_act(), -1), -1);
    assert(lv_obj_get_scroll_y(restoredList) == savedScroll);

    const Inputs ready{true, 234, true, false, true, 0};
    assert(cycle.start(0, ready) == StartResult::started);
    ui.show(Page::home, cycle);
    ui.update(cycle, sample);
    tick();
    saveFrame(output, "running");
    tap(ui, 329, 269, Action::pause);
    tap(ui, 428, 196, Action::none);
    tap(ui, 424, 269, Action::stop);
    cycle.pause(800);
    pixelsFlushed = flushWindows = 0;
    ui.show(Page::home, cycle);
    ui.update(cycle, sample);
    tick();
    assert(!hasLabel(lv_scr_act(), "Paused") && hasLabel(lv_scr_act(), LV_SYMBOL_PLAY));
    assert(pixelsFlushed > 0 && pixelsFlushed < width * height);
    std::printf("Pausing on Home flushed %u of %u pixels.\n", pixelsFlushed, width * height);
    tap(ui, 329, 269, Action::resume);
    ui.show(Page::options, cycle);
    tick();
    tap(ui, 312, 88 - savedScroll, Action::manualFan);
    assert(ui.sliderValue() > 40 && ui.sliderValue() < 60);
    assert(cycle.start(900, ready) == StartResult::started);
    ui.show(Page::options, cycle);
    tick();
    assert(hasLabel(lv_scr_act(), "Auto"));
    tap(ui, 312, 88, Action::none);
    cycle.stop();

    ui.show(Page::start, cycle);
    saveFrame(output, "start");
    tap(ui, 352, 248, Action::confirm);

    Cycle longCycle;
    longCycle.selectCustom(55, 720);
    auto heaterInputs = ready;
    heaterInputs.thermistorTenths = 530;
    assert(longCycle.start(0, heaterInputs) == StartResult::started);
    ui.show(Page::home, longCycle);
    const auto expectHeater = [](const char* value) {
        auto* content = lv_obj_get_child(lv_scr_act(), -1);
        assert(hasLabel(lv_obj_get_child(content, 0), value));
    };
    auto heater = sample;
    const auto updateHeater = [&](std::uint32_t elapsed, int hundredths, bool valid = true) {
        lv_tick_inc(elapsed);
        heater.thermistorHundredths = hundredths;
        heater.thermistorValid = valid;
        heater.thermistorAt = lv_tick_get();
        ui.update(longCycle, heater);
    };
    updateHeater(200, -1500, false);
    expectHeater("--");
    updateHeater(200, 5300);
    expectHeater("55 °C");
    for (unsigned i = 0; i < 10; ++i) {
        updateHeater(200, i % 2 ? 5300 : 5900);
        expectHeater("55 °C");
    }
    updateHeater(1000, 5100);
    expectHeater("54 °C");
    updateHeater(1000, 5101);
    expectHeater("55 °C");
    updateHeater(1000, 5901);
    expectHeater("56 °C");
    longCycle.settings.fahrenheit = true;
    ui.show(Page::home, longCycle);
    ui.update(longCycle, heater);
    expectHeater("133 °F");
    longCycle.settings.fahrenheit = false;
    ui.show(Page::home, longCycle);

    for (const int temp : {7500, 3500}) {
        const bool rising = temp == 7500;
        for (unsigned i = 0; i < 4; ++i) {
            updateHeater(200, temp);
            expectHeater(rising ? "56 °C" : "57 °C");
        }
        lv_tick_inc(199);
        ui.update(longCycle, heater);
        expectHeater(rising ? "56 °C" : "57 °C");
        updateHeater(1, temp);
        expectHeater(rising ? "57 °C" : "56 °C");
    }

    longCycle.pause(1000);
    ui.show(Page::home, longCycle);
    updateHeater(3000, 5900);
    expectHeater("59 °C");
    longCycle.stop();
    ui.show(Page::home, longCycle);
    updateHeater(1000, 5800);
    expectHeater("58 °C");
    updateHeater(200, -1500, false);
    expectHeater("--");
    updateHeater(200, 5150);
    expectHeater("52 °C");

    longCycle.settings.fahrenheit = true;
    ui.show(Page::home, longCycle);
    updateHeater(200, -1500, false);
    expectHeater("--");
    updateHeater(200, 2972); // 85.496 °F rounds to 85 °F.
    expectHeater("85 °F");
    longCycle.settings.fahrenheit = false;
    assert(longCycle.start(2000, heaterInputs) == StartResult::started);
    longCycle.update(61000, heaterInputs);
    ui.show(Page::home, longCycle);
    ui.update(longCycle, heater);
    tick();
    assert(!hasLabel(lv_scr_act(), "+30 min"));

    ui.show(Page::presets, cycle);
    tick();
    assert(hasLabel(lv_scr_act(), "PLA") && hasLabel(lv_scr_act(), "Save current profile"));
    auto* builtInList = lv_obj_get_child(lv_scr_act(), -1);
    auto* firstCard = lv_obj_get_child(builtInList, 0);
    auto* secondCard = lv_obj_get_child(builtInList, 1);
    auto* thirdCard = lv_obj_get_child(builtInList, 2);
    assert(lv_obj_get_width(firstCard) == 218 && lv_obj_get_height(firstCard) == 68);
    checkProfileSelection(firstCard, secondCard);
    assert(lv_obj_get_child_cnt(firstCard) == lv_obj_get_child_cnt(secondCard) + 1);
    assert(firstCard->coords.y1 == secondCard->coords.y1 && firstCard->coords.x2 < secondCard->coords.x1);
    assert(firstCard->coords.x1 == thirdCard->coords.x1 && firstCard->coords.y2 < thirdCard->coords.y1);
    tap(ui, 120, 149, Action::presetBase);
    tap(ui, 344, 149, static_cast<Action>(static_cast<unsigned>(Action::presetBase) + 1));
    swipe(125, 236, 0, -100);
    assert(ui.takeAction() == Action::none);
    const auto builtInScroll = lv_obj_get_scroll_y(builtInList);
    assert(builtInScroll > 0);
    saveFrame(output, "presets");
    tap(ui, 224, 292, Action::saveCurrentProfile);
    tap(ui, 350, 87, Action::myProfiles);
    UiContext named{};
    named.namedProfiles = true;
    ui.show(Page::presets, cycle, named);
    tick();
    assert(!hasLabel(lv_scr_act(), "PLA") && hasLabel(lv_scr_act(), "No saved profiles yet."));
    assert(cycle.saveProfile(0, {"PETG-CF", 65, 360, 20}));
    assert(cycle.saveProfile(2, {"PLA matte", 55, 240, 0}));
    ui.show(Page::presets, cycle, named);
    saveFrame(output, "my-profiles");
    assert(hasLabel(lv_scr_act(), "PETG-CF") && hasLabel(lv_scr_act(), "20% RH") &&
           hasLabel(lv_scr_act(), "Save current profile"));
    tap(ui, 130, 144, static_cast<Action>(static_cast<unsigned>(Action::presetBase) + firstSavedProfile));
    auto* ownList = lv_obj_get_child(lv_scr_act(), -1);
    auto* firstOwn = lv_obj_get_child(ownList, 0);
    auto* secondOwn = lv_obj_get_child(ownList, 1);
    assert(firstOwn->coords.y1 == secondOwn->coords.y1 && firstOwn->coords.x2 < secondOwn->coords.x1);
    checkProfileSelection(secondOwn, firstOwn);
    assert(lv_obj_get_child_cnt(secondOwn) == 4 && lv_obj_get_child_cnt(firstOwn) == 4);
    assert(lv_obj_get_style_bg_color(lv_obj_get_child(firstOwn, -1), 0).full ==
           lv_obj_get_style_bg_color(lv_obj_get_child(secondOwn, -1), 0).full);
    tap(ui, 198, 162, Action::profileEditBase);
    tap(ui, 342, 146, static_cast<Action>(static_cast<unsigned>(Action::presetBase) + firstSavedProfile + 2));
    tap(ui, 424, 162, static_cast<Action>(static_cast<unsigned>(Action::profileEditBase) + 2));
    tap(ui, 224, 292, Action::saveCurrentProfile);
    tap(ui, 123, 87, Action::builtInProfiles);
    ui.show(Page::presets, cycle);
    tick();
    assert(lv_obj_get_scroll_y(lv_obj_get_child(lv_scr_act(), -1)) == builtInScroll);

    UiContext draftSettings{};
    Filament custom{{}, 65, 360, 20};
    draftSettings.draft = &custom;
    ui.show(Page::temperature, cycle, draftSettings);
    saveFrame(output, "temperature");
    assert(hasLabel(lv_scr_act(), "65 °C"));
    tap(ui, 442, 198, Action::setTemperature);
    assert(ui.sliderValue() == (regularTemperatureLimit - customTemperatureMin) / customTemperatureStep);
    tap(ui, 234, 276, Action::confirm);

    ui.show(Page::custom, cycle, draftSettings);
    saveFrame(output, "custom");
    tap(ui, 430, 182, Action::setTemperature);
    assert(ui.sliderValue() == (regularTemperatureLimit - customTemperatureMin) / customTemperatureStep);
    tap(ui, 432, 284, Action::setTime);
    assert(ui.sliderValue() == customTimeMaxMinutes / customTimeStepMinutes);
    custom.temperature = 85;
    custom.minutes = 720;
    ui.updateCustom(custom, false);
    assert(hasLabel(lv_scr_act(), "85 °C") && hasLabel(lv_scr_act(), "12:00"));
    tap(ui, 350, 87, Action::humidityMode);
    tap(ui, 124, 87, Action::timedMode);
    tap(ui, 400, 32, Action::confirm);

    ui.show(Page::settings, cycle);
    tick();
    tap(ui, 243, 162, Action::sound);
    tap(ui, 243, 234, Action::display);
    ui.show(Page::display, cycle);
    tick();
    tap(ui, 434, 159, Action::displayTimeout);
    assert(ui.sliderValue() == 4);
    saveFrame(output, "display");

    ui.show(Page::units, cycle);
    tick();
    tap(ui, 353, 123, Action::fahrenheit);
    cycle.settings.fahrenheit = true;
    ui.show(Page::chamber, cycle);
    ui.update(cycle, sample);
    assert(hasLabel(lv_scr_act(), "74.1 °F") && hasLabel(lv_scr_act(), "80.2 °F"));
    changed = sample;
    changed.chamberHundredths += 150;
    ui.update(cycle, changed);
    assert(hasLabel(lv_scr_act(), "74.1 °F"));
    lv_tick_inc(499);
    ui.update(cycle, changed);
    assert(hasLabel(lv_scr_act(), "74.1 °F"));
    lv_tick_inc(1);
    ui.update(cycle, changed);
    assert(hasLabel(lv_scr_act(), "76.8 °F"));
    saveFrame(output, "chamber");
    ui.show(Page::diagnostics, cycle);
    ui.update(cycle, changed);
    saveFrame(output, "diagnostics");
    tap(ui, 355, 190, Action::allTests);
    tap(ui, 102, 270, static_cast<Action>(static_cast<unsigned>(Action::testBase) + SelfTest::count));

    Cycle recovery({{11, false, true, true, true}, 245});
    ui.show(Page::recovery, recovery);
    saveFrame(output, "recovery");
    tap(ui, 344, 230, Action::confirm);
    tap(ui, 211, 230, Action::back);

    cycle.settings.fahrenheit = false;
    History history;
    for (unsigned i = 0; i < 40; ++i) {
        history.add(i * 30000, {static_cast<std::int16_t>(240 + 9 * i),
                                static_cast<std::int16_t>(300 + 12 * i), static_cast<std::int16_t>(520 - 8 * i)});
    }
    FaultHistory faults;
    for (unsigned i = 0; i < FaultHistory::capacity; ++i) faults.add(Notice::thermostat, 733);
    SelfTest service;
    service.start(TestItem::buzzer, 0);
    assert(service.takeBuzzer());
    service.update(3600, ready, 0, false, true);
    std::strcpy(custom.name, "PETG-CF");
    custom.humidity = 20;
    UiContext context{&custom, &faults, &service, &history};
    ui.show(Page::selfTest, cycle, context);
    saveFrame(output, "self-test");
    assert(hasLabel(lv_scr_act(), "Did you hear three rising tones?"));
    tap(ui, 295, 291, Action::testPassed);
    for (unsigned page = 0; page <= static_cast<unsigned>(Page::profileLimit); ++page) {
        ui.show(static_cast<Page>(page), cycle, context);
        ui.update(cycle, sample);
        tick();
    }
    ui.show(Page::profileName, cycle, context);
    saveFrame(output, "profile-name");
    assert(std::strcmp(ui.profileNameDraft(), "PETG-CF") == 0 && hasLabel(lv_scr_act(), "Save profile"));
    tap(ui, 403, 34, Action::continueProfile);
    ui.show(Page::profileProgram, cycle, context);
    saveFrame(output, "profile-program");
    assert(hasLabel(lv_scr_act(), "20% RH") && hasLabel(lv_scr_act(), "Maximum duration"));
    swipe(270, 260, 0, -120);
    assert(ui.takeAction() == Action::none);
    tap(ui, 431, 284, Action::setHumidity);
    assert(ui.sliderValue() == 99);
    ui.show(Page::faultLog, cycle, context);
    saveFrame(output, "error-log");
    tap(ui, 444, 33, Action::nextLog);
    context.logOffset = 27;
    ui.show(Page::faultLog, cycle, context);
    tick();
    assert(hasLabel(lv_scr_act(), "10 / 10"));
    tap(ui, 326, 33, Action::previousLog);
    ui.show(Page::graph, cycle, context);
    saveFrame(output, "graph");
    ui.show(Page::sound, cycle, context);
    saveFrame(output, "sound");
    ui.show(Page::unlock, cycle, context);
    saveFrame(output, "unlock");
    ui.show(Page::about, cycle);
    saveFrame(output, "about");
    assert(hasLabel(lv_scr_act(), "Made by Lukas Hilverda"));
    lv_mem_monitor_t memory{};
    lv_mem_monitor(&memory);
    assert(memory.max_used < 24 * 1024);
    std::printf("LVGL touch and all pages passed, maximum heap use %u bytes.\n",
                static_cast<unsigned>(memory.max_used));
}


