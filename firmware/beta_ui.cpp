#include "beta_ui.hpp"

#include <cstring>


namespace h1::cold {
namespace {

constexpr unsigned background = 0x112033;
constexpr unsigned rail = 0x182b42;
constexpr unsigned card = 0x223952;
constexpr unsigned inactive = 0x314c65;
constexpr unsigned pressed = 0x395a77;
constexpr unsigned accent = 0x72ded7;
constexpr unsigned ink = 0xf3f8ff;
constexpr unsigned muted = 0xb7cbdf;
constexpr unsigned amber = 0xffce83;
constexpr char repositoryUrl[] = "https://github.com/lukashilverda/elegoo-h1-ht-custom-firmware";

struct Rect { int x, y, width, height; };

struct Destination { const char* icon; const char* title; Action action; };
constexpr Destination tabs[]{
    {LV_SYMBOL_HOME, "Home", Action::home},
    {LV_SYMBOL_LIST, "Options", Action::options},
    {LV_SYMBOL_SETTINGS, "Settings", Action::settings},
};
constexpr unsigned railWidth = 86;

Action navigationFor(Page page) {
    switch (page) {
    case Page::home: return Action::home;
    case Page::options: return Action::options;
    case Page::settings: return Action::settings;
    default: return Action::none;
    }
}

lv_obj_t* panel(lv_obj_t* parent, Rect rect, unsigned color, int radius = 0) {
    auto* object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(object, static_cast<lv_coord_t>(rect.x), static_cast<lv_coord_t>(rect.y));
    lv_obj_set_size(object, static_cast<lv_coord_t>(rect.width), static_cast<lv_coord_t>(rect.height));
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, static_cast<lv_coord_t>(radius), 0);
    return object;
}

lv_obj_t* text(lv_obj_t* parent, const char* value, int x, int y,
               const lv_font_t* font = &lv_font_montserrat_16, unsigned color = ink, int width = 0) {
    auto* object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_label_set_text(object, value);
    if (width > 0) lv_obj_set_width(object, static_cast<lv_coord_t>(width));
    lv_obj_set_pos(object, static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y));
    return object;
}


const char* faultMessage(Notice reason) {
    switch (reason) {
    case Notice::humidity: return "H01  Chamber temperature and humidity sensor unavailable.";
    case Notice::thermistor: return "T01  Thermistor reading outside its valid range.";
    case Notice::fan: return "F01  No feedback from the fan.";
    case Notice::exhaust: return "V01  Rear exhaust vent failed to reach its position.";
    case Notice::inlet: return "V02  Lower inlet vent failed to reach its position.";
    case Notice::heatTimeout: return "P01  Heater stayed more than 5 °C below the setpoint for one hour.";
    case Notice::thermostat: return "P03  The heater's bimetal thermostat may have opened. Let the dryer cool. Use Manual fan if needed. After cooling, try again at a lower temperature.";
    case Notice::storage: return "S01  Could not save settings or cycle state.";
    case Notice::controller: return "C01  The watchdog did not start. Restart the dryer.";
    case Notice::overTemperature: return "P02  Temperature limit reached. Let the dryer cool down.";
    default: return "The cycle is paused.";
    }
}

int inChosenUnit(int hundredths, bool fahrenheit) {
    if (!fahrenheit) return hundredths;
    return (hundredths * 9 + (hundredths >= 0 ? 2 : -2)) / 5 + 3200;
}

void temperature(char (&buffer)[24], int hundredths, bool fahrenheit, bool valid) {
    if (!valid) {
        lv_snprintf(buffer, sizeof buffer, "--");
        return;
    }
    const int value = inChosenUnit(hundredths, fahrenheit);
    const int magnitude = value < 0 ? -value : value;
    const int tenths = (magnitude + 5) / 10;
    lv_snprintf(buffer, sizeof buffer, "%s%d.%d °%c", value < 0 && tenths ? "-" : "",
                tenths / 10, tenths % 10, fahrenheit ? 'F' : 'C');
}

const char* profileName(unsigned profile, const Settings& settings) {
    if (profile >= firstSavedProfile) return settings.filaments[profile - firstSavedProfile].name;
    if (profile == customProfile) return "Custom";
    if (settings.fahrenheit && profile == 10) return "PVA (176°F)";
    if (settings.fahrenheit && profile == 11) return "PVA (113°F)";
    return presets[profile].name;
}

void targetTemperature(char (&buffer)[24], unsigned celsius, bool fahrenheit) {
    const auto target = inChosenUnit(static_cast<int>(celsius) * 100, fahrenheit);
    lv_snprintf(buffer, sizeof buffer, "%d °%c", (target + 50) / 100, fahrenheit ? 'F' : 'C');
}

constexpr const char* testNames[]{"Exhaust vent", "Inlet vent", "Fan", "Heater", "Spool drive", "Beeper", "Sensors"};
constexpr const char* testStatus[]{"Waiting", "Testing", "Passed", "Failed", "Check now", "Skipped"};
constexpr const char* sleepNames[]{"Off", "1 min", "5 min", "15 min", "30 min"};

void duration(char (&buffer)[24], unsigned seconds) {
    const unsigned minutes = (seconds + 59) / 60;
    lv_snprintf(buffer, sizeof buffer, "%02u:%02u", minutes / 60, minutes % 60);
}

void replace(lv_obj_t* label, const char* value) {
    if (label && std::strcmp(lv_label_get_text(label), value) != 0) lv_label_set_text(label, value);
}

void updateToggle(lv_obj_t* control, bool on) {
    if (!control) return;
    if (on) lv_obj_add_state(control, LV_STATE_CHECKED);
    else lv_obj_clear_state(control, LV_STATE_CHECKED);
}

void surface(lv_obj_t* object, unsigned color, lv_style_selector_t part) {
    lv_obj_set_style_bg_color(object, lv_color_hex(color), part);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, part);
    lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, part);
}

void scrollList(lv_obj_t* list) {
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    surface(list, inactive, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
}

}

struct ScreenBuilder {
    Ui& ui;
    lv_obj_t* root;
    const Cycle& cycle;
    UiContext context{};

    void bind(lv_obj_t* object, Action action) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(object, lv_color_hex(pressed), LV_STATE_PRESSED);
        lv_obj_add_event_cb(object, Ui::clicked, LV_EVENT_CLICKED, &ui);
        lv_obj_set_user_data(object, reinterpret_cast<void*>(static_cast<std::uintptr_t>(action)));
    }

    lv_obj_t* button(Rect rect, const char* caption, Action action, bool primary = false,
                     lv_obj_t* parent = nullptr) {
        auto* object = panel(parent ? parent : root, rect, primary ? accent : inactive, 8);
        bind(object, action);
        if (primary) lv_obj_set_style_bg_color(object, lv_color_hex(0x90eee9), LV_STATE_PRESSED);
        lv_obj_center(text(object, caption, 0, 0, &lv_font_montserrat_16, primary ? background : ink));
        return object;
    }

    void navigation() {
        auto* bar = panel(root, {0, 0, railWidth, 320}, rail);
        for (unsigned i = 0; i < 3; ++i) {
            const auto& link = tabs[i];
            auto* tab = panel(bar, {9, 23 + static_cast<int>(i) * 98, 68, 76}, rail, 8);
            ui.tabs_[i] = tab;
            bind(tab, link.action);
            lv_obj_align(text(tab, link.icon, 0, 0, &lv_font_montserrat_24, muted),
                         LV_ALIGN_TOP_MID, 0, 12);
            lv_obj_align(text(tab, link.title, 0, 0, &lv_font_montserrat_14, muted),
                         LV_ALIGN_BOTTOM_MID, 0, -10);
        }
    }

    void title(const char* name) {
        text(root, name, 16, 18, &lv_font_montserrat_24);
    }

    lv_obj_t* reading(Rect rect, const char* caption, Action action = Action::none) {
        auto* box = panel(root, rect, card, 9);
        if (action != Action::none) bind(box, action);
        text(box, caption, 12, 10, &lv_font_montserrat_14, muted);
        return text(box, "--", 12, 29, &lv_font_montserrat_24, accent);
    }

    enum class Icon { thermometer, drop, clock, spool };

    void badge(lv_obj_t* parent, int x, int y, Icon icon, unsigned base, unsigned glyph) {
        auto* disc = panel(parent, {x, y, 42, 42}, base, 21);
        switch (icon) {
        case Icon::thermometer:
            panel(disc, {18, 7, 7, 23}, glyph, 4);
            panel(disc, {15, 23, 13, 13}, glyph, 7);
            for (int dy = 0; dy < 12; dy += 5) panel(disc, {26, 10 + dy, 4, 2}, glyph, 1);
            break;
        case Icon::drop:
            lv_obj_center(text(disc, LV_SYMBOL_TINT, 0, 0, &lv_font_montserrat_24, glyph));
            break;
        case Icon::clock:
            panel(disc, {8, 8, 26, 26}, glyph, 13);
            panel(disc, {11, 11, 20, 20}, base, 10);
            panel(disc, {20, 13, 3, 10}, glyph, 1);
            panel(disc, {20, 20, 8, 3}, glyph, 1);
            break;
        case Icon::spool:
            panel(disc, {10, 9, 21, 24}, glyph, 8);
            panel(disc, {21, 9, 12, 24}, muted, 6);
            panel(disc, {25, 18, 4, 6}, base, 2);
            break;
        }
    }

    lv_obj_t* homeTile(int x, int y, Icon icon, const char* name, Action action) {
        auto* box = panel(root, {x, y, 181, 78}, card, 15);
        if (action != Action::none) bind(box, action);
        badge(box, 8, 18, icon, inactive, ink);
        text(box, name, 57, 15, &lv_font_montserrat_14, muted);
        return text(box, "--", 57, 37, &lv_font_montserrat_16);
    }

    void home() {
        ui.homeHasCycle_ = cycle.cycleExists();
        ui.homeProfile_ = cycle.settings.profile;
        ui.homeFahrenheit_ = cycle.settings.fahrenheit;
        ui.homeTarget_ = cycle.settings.targetCelsius();
        auto* climate = panel(root, {10, 10, 374, 124}, card, 16);
        bind(climate, Action::chamber);
        badge(climate, 12, 41, Icon::thermometer, 0x67422f, 0xffba79);
        badge(climate, 201, 41, Icon::drop, 0x28546b, 0x9ce9ff);
        text(climate, "Heater", 62, 34, &lv_font_montserrat_14, muted);
        text(climate, "Humidity", 251, 34, &lv_font_montserrat_14, muted);
        ui.chamber_ = text(climate, "--", 62, 54, &lv_font_montserrat_24);
        ui.humidity_ = text(climate, "--", 251, 54, &lv_font_montserrat_24);
        const bool active = cycle.cycleExists();
        const auto edit = active ? Action::none : Action::editCustom;
        char value[24];
        if (cycle.settings.humidityTarget()) {
            lv_snprintf(value, sizeof value, "Goal %u%% RH", cycle.settings.humidityTarget());
            text(climate, value, 251, 93, &lv_font_montserrat_14, muted);
        }
        targetTemperature(value, cycle.settings.targetCelsius(), cycle.settings.fahrenheit);
        replace(homeTile(10, 145, Icon::thermometer, "Set temp",
                         active ? Action::none : Action::editTemperature), value);
        ui.time_ = homeTile(203, 145, Icon::clock, cycle.settings.humidityTarget() ? "Max time" : (active ? "Remaining time" : "Set time"), edit);
        auto* filament = homeTile(10, 233, Icon::spool, "Filament", active ? Action::none : Action::presets);
        lv_obj_set_width(filament, 117);
        lv_label_set_long_mode(filament, LV_LABEL_LONG_DOT);
        replace(filament, profileName(cycle.settings.profile, cycle.settings));
        auto* play = button({203, 233, active ? 85 : 181, 78}, LV_SYMBOL_PLAY,
                            active ? Action::resume : Action::start);
        lv_obj_set_style_radius(play, 15, 0);
        lv_obj_set_style_border_width(play, 2, 0);
        lv_obj_set_style_border_color(play, lv_color_hex(accent), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(play, 0), lv_color_hex(accent), 0);
        lv_obj_set_style_text_font(lv_obj_get_child(play, 0), &lv_font_montserrat_24, 0);
        if (active) {
            ui.homeAction_ = play;
            ui.updateHomeAction(cycle);
            auto* stop = button({299, 233, 85, 78}, LV_SYMBOL_STOP, Action::stop);
            lv_obj_set_style_radius(stop, 15, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(stop, 0), lv_color_hex(0xff827c), 0);
            lv_obj_set_style_text_font(lv_obj_get_child(stop, 0), &lv_font_montserrat_24, 0);
        }
    }

    lv_obj_t* row(int y, const char* name, Action action, const char* value = nullptr) {
        auto* item = panel(root, {2, y, 366, 65}, card, 12);
        if (action != Action::none) bind(item, action);
        lv_obj_align(text(item, name, 0, 0), LV_ALIGN_LEFT_MID, 14, 0);
        if (value) lv_obj_align(text(item, value, 0, 0, &lv_font_montserrat_16, muted),
                                LV_ALIGN_RIGHT_MID, -15, 0);
        return item;
    }

    lv_obj_t* toggle(int y, const char* name, Action action, bool on) {
        auto* item = row(y, name, action);
        auto* control = lv_switch_create(item);
        lv_obj_remove_style_all(control);
        lv_obj_clear_flag(control, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(control, 55, 30);
        lv_obj_align(control, LV_ALIGN_RIGHT_MID, -14, 0);
        surface(control, inactive, LV_PART_MAIN);
        surface(control, 0x50bf91, LV_PART_INDICATOR | LV_STATE_CHECKED);
        surface(control, ink, LV_PART_KNOB);
        lv_obj_set_style_pad_all(control, -3, LV_PART_KNOB);
        if (action == Action::none) lv_obj_set_style_opa(control, LV_OPA_60, 0);
        updateToggle(control, on);
        return control;
    }

    lv_obj_t* slider(lv_obj_t* parent, Rect r, int min, int max, int value, Action action) {
        auto* control = lv_slider_create(parent);
        lv_obj_remove_style_all(control);
        lv_obj_set_size(control, static_cast<lv_coord_t>(r.width), static_cast<lv_coord_t>(r.height));
        lv_obj_set_pos(control, static_cast<lv_coord_t>(r.x), static_cast<lv_coord_t>(r.y));
        lv_obj_set_ext_click_area(control, 12);
        surface(control, inactive, LV_PART_MAIN);
        surface(control, accent, LV_PART_INDICATOR);
        surface(control, ink, LV_PART_KNOB);
        lv_obj_set_style_pad_all(control, 6, LV_PART_KNOB);
        lv_slider_set_range(control, min, max);
        lv_slider_set_value(control, value, LV_ANIM_OFF);
        lv_obj_set_user_data(control, reinterpret_cast<void*>(static_cast<std::uintptr_t>(action)));
        lv_obj_add_event_cb(control, Ui::clicked, LV_EVENT_VALUE_CHANGED, &ui);
        return control;
    }

    void beginList(Rect viewport) {
        root = panel(root, viewport, background);
        ui.list_ = root;
        scrollList(root);
    }

    void options() {
        title("Drying options");
        ui.optionsEditable_ = !cycle.cycleExists();
        ui.optionsFanAvailable_ = cycle.manualFanAvailable();
        beginList({10, 55, 378, 260});
        auto* fan = row(0, "Manual fan", Action::none, ui.optionsFanAvailable_ ? nullptr : "Auto");
        if (ui.optionsFanAvailable_) {
            ui.manualFan_ = slider(fan, {145, 29, 142, 8}, 0, 100, static_cast<int>(cycle.manualFanDuty()), Action::manualFan);
            ui.fanValue_ = text(fan, "", 302, 22);
        }
        ui.rotation_ = toggle(74, "Rotating drying", ui.optionsEditable_ ? Action::rotate : Action::none, cycle.settings.rotation);
        ui.lid_ = toggle(148, "Lid open detection", ui.optionsEditable_ ? Action::lid : Action::none, cycle.settings.lidDetection);
        row(222, "Filament", ui.optionsEditable_ ? Action::presets : Action::none,
            profileName(cycle.settings.profile, cycle.settings));
        if (!ui.optionsEditable_) text(root, "Stop the cycle to change drying options.", 8, 295, &lv_font_montserrat_14, muted);
    }

    void settings() {
        title("Settings");
        beginList({10, 55, 378, 260});
        row(0, "Temperature unit", Action::units, cycle.settings.fahrenheit ? "°F" : "°C");
        row(74, "Beeper", Action::sound, LV_SYMBOL_RIGHT);
        row(148, "Screen timeout", Action::display, sleepNames[cycle.settings.displayTimeout]);
        row(222, "Diagnostics", Action::diagnostics, LV_SYMBOL_RIGHT);
        row(296, "Error log", Action::faultLog, LV_SYMBOL_RIGHT);
        row(370, "Advanced", Action::advanced, LV_SYMBOL_RIGHT);
        row(444, "Factory reset", Action::reset, LV_SYMBOL_RIGHT);
        row(518, "About", Action::about, LV_SYMBOL_RIGHT);
    }

    void filament() {
        backPanel("Filament");
        button({14, 65, 222, 43}, "Built-in presets", Action::builtInProfiles, !context.namedProfiles);
        button({244, 65, 222, 43}, "My profiles", Action::myProfiles, context.namedProfiles);
        button({14, 270, 452, 44}, "Save current profile", Action::saveCurrentProfile, true);

        beginList({14, 117, 452, 144});
        int visible = 0;
        auto entry = [&](const char* name, unsigned temperature, unsigned humidity, unsigned profile,
                         Action edit = Action::none) {
            const int x = (visible % 2) * 226;
            const int y = (visible / 2) * 76;
            ++visible;
            const bool selected = profile == cycle.settings.profile;
            auto* item = panel(root, {x, y, 218, 68}, card, 12);
            bind(item, static_cast<Action>(static_cast<unsigned>(Action::presetBase) + profile));
            auto* nameLabel = text(item, name, 13, 8, &lv_font_montserrat_16, ink, 171);
            lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
            char value[24];
            targetTemperature(value, temperature, cycle.settings.fahrenheit);
            text(item, value, 13, 31, &lv_font_montserrat_24);
            if (humidity) {
                lv_snprintf(value, sizeof value, "%u%% RH", humidity);
                text(item, value, 97, 38, &lv_font_montserrat_14, muted);
            }
            if (selected) text(item, LV_SYMBOL_OK, 192, 8, &lv_font_montserrat_16, accent);
            if (edit != Action::none) {
                auto* editButton = button({159, 29, 52, 34}, "Edit", edit, false, item);
                lv_obj_set_ext_click_area(editButton, 4);
                lv_obj_set_style_text_font(lv_obj_get_child(editButton, 0), &lv_font_montserrat_14, 0);
            }
        };
        if (!context.namedProfiles) {
            for (unsigned i = 0; i < presetCount; ++i) {
                entry(profileName(i, cycle.settings), static_cast<unsigned>(presets[i].celsius), 0, i);
            }
            return;
        }

        const auto limit = cycle.settings.temperatureLimit();
        for (unsigned i = 0; i < savedProfileCount; ++i) {
            const auto& profile = cycle.settings.filaments[i];
            if (!profile.exists()) continue;
            entry(profile.name, profile.temperature > limit ? limit : profile.temperature, profile.humidity,
                  firstSavedProfile + i, static_cast<Action>(static_cast<unsigned>(Action::profileEditBase) + i));
        }
        if (visible == 0) text(root, "No saved profiles yet.", 14, 22, &lv_font_montserrat_16, muted);
    }

    void backPanel(const char* caption) {
        button({14, 12, 107, 44}, LV_SYMBOL_LEFT "  Back", Action::back);
        text(root, caption, 138, 20, &lv_font_montserrat_24);
    }

    void programField(Rect rect, const char* caption, int min, int max, Action action,
                      lv_obj_t*& label, lv_obj_t*& track) {
        auto* box = panel(root, rect, card, 12);
        text(box, caption, 16, 14);
        label = text(box, "--", 0, 0, &lv_font_montserrat_24, accent);
        lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -16, 12);
        track = slider(box, {24, rect.height - 33, rect.width - 48, 10}, min, max, min, action);
    }

    int maxTemperatureStep() const {
        return static_cast<int>((cycle.settings.temperatureLimit() - customTemperatureMin) / customTemperatureStep);
    }

    void quickTemperature() {
        backPanel("Set temperature");
        programField({14, 89, 452, 138}, "Target temperature", 0, maxTemperatureStep(),
                     Action::setTemperature, ui.customTemperature_, ui.temperatureSlider_);
        button({14, 250, 452, 54}, "Apply", Action::confirm, true);
        const Filament fallback{{}, cycle.settings.targetCelsius()};
        ui.updateCustom(context.draft ? *context.draft : fallback, cycle.settings.fahrenheit);
    }

    void customSettings(bool named) {
        backPanel(named ? "Profile settings" : "Custom drying");
        button({365, 12, 100, 44}, "Save", Action::confirm, true);
        const Filament fallback{{}, cycle.settings.customTemperature, cycle.settings.customMinutes, cycle.settings.customHumidity};
        const auto& draft = context.draft ? *context.draft : fallback;
        button({14, 65, 222, 43}, "Timed drying", Action::timedMode, draft.humidity == 0);
        button({244, 65, 222, 43}, "Humidity drying", Action::humidityMode, draft.humidity != 0);
        beginList({14, 117, 452, 197});
        programField({0, 0, 442, 93}, "Target temperature", 0, maxTemperatureStep(),
                     Action::setTemperature, ui.customTemperature_, ui.temperatureSlider_);
        programField({0, 102, 442, 93}, draft.humidity ? "Maximum duration" : "Drying time",
                     1, customTimeMaxMinutes / customTimeStepMinutes, Action::setTime, ui.customTime_, ui.timeSlider_);
        if (draft.humidity) {
            programField({0, 204, 442, 93}, "Humidity target", 1, 99,
                         Action::setHumidity, ui.customHumidity_, ui.humiditySlider_);
        }
        ui.updateCustom(draft, cycle.settings.fahrenheit);
    }

    void nameProfile() {
        backPanel(context.editingProfile ? "Profile name" : "Save profile");
        button({355, 12, 111, 44}, context.editingProfile ? "Continue" : "Save",
               Action::continueProfile, true);
        ui.nameInput_ = lv_textarea_create(root);
        auto* name = ui.nameInput_;
        lv_obj_set_pos(name, 14, 67);
        lv_obj_set_size(name, context.editingProfile ? 322 : 452, 48);
        lv_obj_set_style_bg_color(name, lv_color_hex(card), 0);
        lv_obj_set_style_bg_opa(name, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(name, 8, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(ink), 0);
        lv_obj_set_style_border_color(name, lv_color_hex(accent), 0);
        lv_obj_set_style_border_width(name, 1, 0);
        lv_obj_set_style_pad_all(name, 12, 0);
        lv_textarea_set_one_line(name, true);
        lv_textarea_set_max_length(name, profileNameLength);
        lv_textarea_set_accepted_chars(name, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 +_-./()");
        lv_textarea_set_placeholder_text(name, "Filament name");
        if (context.draft) lv_textarea_set_text(name, context.draft->name);
        if (context.editingProfile) button({346, 67, 120, 48}, "Delete", Action::deleteProfile);
        else if (context.draft) {
            char temp[24], time[24], summary[72];
            targetTemperature(temp, context.draft->temperature, cycle.settings.fahrenheit);
            duration(time, context.draft->minutes * 60);
            if (context.draft->humidity) {
                lv_snprintf(summary, sizeof summary, "%s   %s max   %u%% RH", temp, time, context.draft->humidity);
            } else {
                lv_snprintf(summary, sizeof summary, "%s   %s", temp, time);
            }
            text(root, summary, 15, 124, &lv_font_montserrat_14, muted);
        }

        auto* keys = lv_keyboard_create(root);
        lv_obj_set_size(keys, 480, context.editingProfile ? 191 : 171);
        lv_obj_align(keys, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(keys, lv_color_hex(rail), 0);
        lv_obj_set_style_pad_all(keys, 4, 0);
        lv_obj_set_style_pad_row(keys, 4, 0);
        lv_obj_set_style_pad_column(keys, 4, 0);
        lv_obj_set_style_bg_color(keys, lv_color_hex(inactive), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(keys, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_radius(keys, 6, LV_PART_ITEMS);
        lv_obj_set_style_text_color(keys, lv_color_hex(ink), LV_PART_ITEMS);
        lv_keyboard_set_textarea(keys, name);
        lv_obj_set_user_data(keys, reinterpret_cast<void*>(static_cast<std::uintptr_t>(Action::continueProfile)));
        lv_obj_add_event_cb(keys, Ui::clicked, LV_EVENT_READY, &ui);
    }

    void readout(bool diagnostic) {
        backPanel(diagnostic ? "Diagnostics" : "Chamber");
        ui.chamber_ = reading({14, 73, 219, 78}, "Chamber temperature");
        ui.humidity_ = reading({247, 73, 219, 78}, "Relative humidity");
        ui.thermistor_ = reading({14, 160, 219, 78}, "Heater temperature");
        if (diagnostic) {
            button({247, 160, 219, 78}, "Full self-test", Action::allTests, true);
            button({14, 248, 219, 58}, "Component tests", static_cast<Action>(static_cast<unsigned>(Action::testBase) + SelfTest::count));
            button({247, 248, 219, 58}, "Drying graph", Action::graph);
        } else {
            ui.time_ = reading({247, 160, 219, 78}, "Time remaining");
            button({14, 248, 452, 46}, "Drying graph", Action::graph);
            panel(root, {14, 306, 452, 7}, card, 3);
            ui.progress_ = panel(root, {14, 306, 0, 7}, accent, 3);
        }
    }

    void tests() {
        backPanel("Component tests");
        for (unsigned i = 0; i < SelfTest::count; ++i) {
            const auto index = static_cast<int>(i);
            auto* entry = panel(root, {14 + (index % 2) * 228, 68 + (index / 2) * 44, 222, 39}, card, 7);
            bind(entry, static_cast<Action>(static_cast<unsigned>(Action::testBase) + i));
            lv_obj_align(text(entry, testNames[i], 0, 0, &lv_font_montserrat_14), LV_ALIGN_LEFT_MID, 8, 0);
            ui.testRows_[i] = text(entry, "", 0, 0, &lv_font_montserrat_14, accent);
            lv_obj_align(ui.testRows_[i], LV_ALIGN_RIGHT_MID, -7, 0);
        }
        ui.testPrompt_ = text(root, "Select a component to test.", 14, 247, &lv_font_montserrat_14, muted, 452);
        ui.testYes_ = button({242, 275, 109, 39}, "Yes", Action::testPassed, true);
        ui.testNo_ = button({357, 275, 109, 39}, "No", Action::testFailed);
        if (context.test) ui.updateTest(*context.test);
    }

    void graph() {
        backPanel(context.previousGraph ? "Previous session" : "Current session");
        button({350, 12, 116, 44}, context.previousGraph ? "Current" : "Previous", Action::previousGraph);
        constexpr unsigned colors[]{0x72ded7, 0xffba79, 0x91b5ff};
        text(root, "Chamber", 39, 67, &lv_font_montserrat_14, colors[0]);
        text(root, "Heater", 200, 67, &lv_font_montserrat_14, colors[1]);
        text(root, "Humidity", 350, 67, &lv_font_montserrat_14, colors[2]);
        text(root, cycle.settings.fahrenheit ? "°F" : "°C", 15, 87, &lv_font_montserrat_14, muted);
        text(root, "%RH", 428, 87, &lv_font_montserrat_14, muted);
        ui.chart_ = lv_chart_create(root);
        auto* chart = ui.chart_;
        lv_obj_set_pos(chart, 54, 113);
        lv_obj_set_size(chart, 366, 153);
        lv_obj_set_style_bg_color(chart, lv_color_hex(card), 0);
        lv_obj_set_style_border_width(chart, 0, 0);
        lv_obj_set_style_line_color(chart, lv_color_hex(inactive), 0);
        lv_obj_set_style_line_width(chart, 1, 0);
        lv_obj_set_style_text_color(chart, lv_color_hex(muted), LV_PART_TICKS);
        lv_obj_set_style_text_font(chart, &lv_font_montserrat_14, LV_PART_TICKS);
        lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_size(chart, 3, LV_PART_INDICATOR);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, cycle.settings.fahrenheit ? 32 : 0,
                           cycle.settings.fahrenheit ? 230 : 110);
        lv_chart_set_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_PRIMARY_Y, 5, 0, 6, 1, true, 45);
        lv_chart_set_axis_tick(chart, LV_CHART_AXIS_SECONDARY_Y, 5, 0, 6, 1, true, 45);
        for (unsigned i = 0; i < 3; ++i) {
            ui.series_[i] = lv_chart_add_series(chart, lv_color_hex(colors[i]),
                                               i == 2 ? LV_CHART_AXIS_SECONDARY_Y : LV_CHART_AXIS_PRIMARY_Y);
        }
        ui.chartSpan_ = text(root, "", 15, 284, &lv_font_montserrat_16, muted);
        if (context.history) ui.updateChart(*context.history, cycle.settings.fahrenheit);
    }

    void errors() {
        backPanel("Error log");
        if (!context.faults || !context.faults->size()) {
            text(root, "No errors recorded.", 26, 86, &lv_font_montserrat_16, muted);
            return;
        }
        const auto& history = *context.faults;
        const auto count = history.size();
        const auto offset = context.logOffset;
        if (count > faultRowsPerPage) {
            char position[16];
            lv_snprintf(position, sizeof position, "%u / %u", offset / faultRowsPerPage + 1, (count + faultRowsPerPage - 1) / faultRowsPerPage);
            if (offset) button({306, 12, 42, 44}, LV_SYMBOL_LEFT, Action::previousLog);
            auto* number = text(root, position, 0, 0, &lv_font_montserrat_16, muted);
            lv_obj_align(number, LV_ALIGN_TOP_RIGHT, -66, 24);
            if (offset + faultRowsPerPage < count) button({424, 12, 42, 44}, LV_SYMBOL_RIGHT, Action::nextLog);
        }
        for (unsigned row = 0; row < faultRowsPerPage && offset + row < count; ++row) {
            const auto index = offset + row;
            auto* entry = panel(root, {14, 65 + static_cast<int>(row) * 82, 452, 76}, card, 9);
            const auto reason = history.code(index);
            const char* message = reason == Notice::thermostat ? "P03  Bimetal thermostat may have opened." : faultMessage(reason);
            text(entry, message, 12, 10, &lv_font_montserrat_14, ink, 424);
            if (history.heaterTenths(index) != FaultHistory::noTemperature) {
                char value[24];
                temperature(value, history.heaterTenths(index) * 10, cycle.settings.fahrenheit, true);
                text(entry, "Heater at error", 12, 51, &lv_font_montserrat_14, muted);
                text(entry, value, 130, 51, &lv_font_montserrat_14, muted);
            }
        }
    }

    void about() {
        backPanel("About");
        panel(root, {14, 67, 452, 237}, card, 12);
        text(root, "ELEGOO H1 HT", 29, 92, &lv_font_montserrat_24);
        text(root, "SPA001-MB V1.2  /  Beta 0.17", 30, 128, &lv_font_montserrat_14, muted);
        text(root, "Made by Lukas Hilverda", 30, 161, &lv_font_montserrat_16);
        text(root, "GitHub repository", 30, 208, &lv_font_montserrat_16, accent);
        text(root, "github.com/lukashilverda/\nelegoo-h1-ht-custom-firmware", 30, 235, &lv_font_montserrat_14, muted);
        auto* quietZone = panel(root, {286, 101, 164, 164}, 0xffffff, 4);
        auto* qr = lv_qrcode_create(quietZone, 134, lv_color_black(), lv_color_white());
        lv_obj_center(qr);
        lv_qrcode_update(qr, repositoryUrl, sizeof repositoryUrl - 1);
    }

    void soundSettings() {
        backPanel("Beeper");
        auto* volume = panel(root, {14, 67, 452, 69}, card, 9);
        text(volume, "Volume", 13, 26);
        slider(volume, {112, 31, 249, 9}, 0, 10, static_cast<int>(cycle.settings.volume / 10), Action::volume);
        ui.volumeLabel_ = text(volume, "", 380, 26);
        ui.updateVolume(cycle.settings.volume);
        constexpr const char* captions[]{"Startup", "Cycle start", "Cycle stop / finish", "Errors"};
        constexpr unsigned masks[]{soundBoot, soundStart, soundStop, soundError};
        for (unsigned i = 0; i < 4; ++i) {
            const int x = 14 + static_cast<int>(i % 2) * 232;
            const int y = 146 + static_cast<int>(i / 2) * 78;
            auto* item = panel(root, {x, y, 220, 67}, card, 9);
            bind(item, static_cast<Action>(static_cast<unsigned>(Action::soundBoot) + i));
            text(item, captions[i], 11, 11, &lv_font_montserrat_16);
            const bool enabled = (cycle.settings.soundMask & masks[i]) != 0;
            text(item, enabled ? "On" : "Off", 11, 38, &lv_font_montserrat_14, enabled ? accent : muted);
        }
    }

    void sleepSettings() {
        backPanel("Screen timeout");
        auto* field = panel(root, {14, 88, 452, 134}, card, 10);
        text(field, "Turn off after inactivity", 14, 14);
        slider(field, {30, 67, 391, 10}, 0, 4, static_cast<int>(cycle.settings.displayTimeout), Action::displayTimeout);
        for (unsigned i = 0; i < sleepMinutes.size(); ++i) {
            auto* label = text(field, sleepNames[i], 0, 95, &lv_font_montserrat_14, muted);
            lv_obj_align(label, LV_ALIGN_TOP_MID, static_cast<lv_coord_t>(-195 + static_cast<int>(i) * 98), 95);
        }
    }

    void error(const char* title, const char* message, bool coolFan = false) {
        panel(root, {38, 33, 404, 254}, rail, 12);
        text(root, title, 58, 54, &lv_font_montserrat_24, amber);
        text(root, message, 58, 99, &lv_font_montserrat_16, ink, 361);
        button({58, 224, 105, 44}, "Error log", Action::faultLog);
        if (coolFan) button({171, 224, 126, 44}, "Manual fan", Action::coolFan);
        button({304, 224, 116, 44}, "OK", Action::confirm, true);
    }

    void modal(const char* title, const char* message, const char* confirm, const char* cancel = nullptr) {
        panel(root, {52, 48, 376, 223}, rail, 12);
        text(root, title, 74, 70, &lv_font_montserrat_24);
        text(root, message, 74, 112, &lv_font_montserrat_16, ink, 331);
        if (cancel) button({161, 209, 115, 45}, cancel, Action::back);
        button({289, 209, 116, 45}, confirm, Action::confirm, true);
    }

    void startWarning() {
        panel(root, {52, 30, 376, 260}, rail, 12);
        text(root, "Before you start", 74, 51, &lv_font_montserrat_24);
        text(root, "Secure the end of the filament to prevent tangling during rotation.",
             74, 104, &lv_font_montserrat_16, ink, 332);
        text(root, "Close the lid before starting.", 74, 174, &lv_font_montserrat_14, amber, 332);
        button({161, 227, 115, 45}, "Cancel", Action::back);
        button({289, 227, 116, 45}, "Start", Action::confirm, true);
    }
};

void Ui::clicked(lv_event_t* event) {
    auto& self = *static_cast<Ui*>(lv_event_get_user_data(event));
    const auto action = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(lv_event_get_target(event)));
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) {
        self.sliderValue_ = static_cast<unsigned>(lv_slider_get_value(lv_event_get_target(event)));
    }
    self.action_ = static_cast<Action>(action);
}

Action Ui::takeAction() {
    const auto action = action_;
    action_ = Action::none;
    return action;
}

void Ui::updateHomeAction(const Cycle& cycle) {
    if (!homeAction_) return;
    const bool paused = cycle.phase() == Phase::paused;
    replace(lv_obj_get_child(homeAction_, 0), paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    lv_obj_set_user_data(homeAction_, reinterpret_cast<void*>(
        static_cast<std::uintptr_t>(paused ? Action::resume : Action::pause)));
}

void Ui::selectTab(Action action) {
    for (unsigned i = 0; i < 3; ++i) {
        const auto destination = tabs[i].action;
        if ((destination == selectedTab_) == (destination == action)) continue;
        auto* tab = tabs_[i];
        const bool selected = destination == action;
        lv_obj_set_style_bg_color(tab, lv_color_hex(selected ? card : rail), 0);
        const auto color = lv_color_hex(selected ? accent : muted);
        lv_obj_set_style_text_color(lv_obj_get_child(tab, 0), color, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(tab, 1), color, 0);
    }
    selectedTab_ = action;
}

lv_coord_t* Ui::savedScroll() {
    switch (page_) {
    case Page::options: return &optionsScroll_;
    case Page::settings: return &settingsScroll_;
    case Page::presets: return &profileScroll_[namedProfiles_ ? 1 : 0];
    default: return nullptr;
    }
}

void Ui::show(Page page, const Cycle& cycle, const UiContext& context) {
    auto* previous = lv_scr_act();
    const auto destination = navigationFor(page);
    const bool keepShell = selectedTab_ != Action::none && destination != Action::none;
    if (keepShell && destination == selectedTab_) {
        if (page == Page::options && optionsEditable_ == !cycle.cycleExists() &&
            optionsFanAvailable_ == cycle.manualFanAvailable()) return;
        if (page == Page::home && homeHasCycle_ == cycle.cycleExists() &&
            homeProfile_ == cycle.settings.profile && homeFahrenheit_ == cycle.settings.fahrenheit &&
            homeTarget_ == cycle.settings.targetCelsius()) {
            updateHomeAction(cycle);
            return;
        }
    }

    if (auto* position = savedScroll(); list_ && position) *position = lv_obj_get_scroll_y(list_);
    list_ = nullptr;
    namedProfiles_ = context.namedProfiles;
    if (keepShell) {
        lv_obj_clean(content_);
    } else {
        // Release the previous page's widgets before allocating the next page on the MCU.
        if (previous) lv_obj_clean(previous);
        screen_ = panel(nullptr, {0, 0, 480, 320}, background);
        content_ = screen_;
        selectedTab_ = Action::none;
        if (destination != Action::none) {
            ScreenBuilder navigation{*this, screen_, cycle};
            navigation.navigation();
            content_ = panel(screen_, {railWidth, 0, 480 - railWidth, 320}, background);
        }
    }
    if (destination != Action::none) selectTab(destination);
    chamber_ = humidity_ = thermistor_ = time_ = progress_ = nullptr;
    temperatureSlider_ = timeSlider_ = fanValue_ = rotation_ = lid_ = manualFan_ = homeAction_ = nullptr;
    customTemperature_ = customTime_ = customHumidity_ = humiditySlider_ = nameInput_ = volumeLabel_ = nullptr;
    testPrompt_ = testYes_ = testNo_ = chart_ = chartSpan_ = nullptr;
    chartCount_ = chartInterval_ = 0;
    readingsCached_ = false;
    page_ = page;
    ScreenBuilder b{*this, content_, cycle, context};
    switch (page) {
    case Page::boot:
        lv_obj_center(text(screen_, "H1 HT  /  Dryer", 0, 0, &lv_font_montserrat_24, accent));
        break;
    case Page::home: b.home(); break;
    case Page::options: b.options(); break;
    case Page::settings: b.settings(); break;
    case Page::presets: b.filament(); break;
    case Page::start: b.startWarning(); break;
    case Page::temperature: b.quickTemperature(); break;
    case Page::custom: b.customSettings(false); break;
    case Page::profileProgram: b.customSettings(true); break;
    case Page::profileName: b.nameProfile(); break;
    case Page::deleteProfile: b.modal("Delete profile?", "Remove this filament profile.", "Delete", "Cancel"); break;
    case Page::profileLimit:
        b.modal("Profile list full", "All eight slots are in use. Delete a profile in My profiles to make room.", "OK");
        break;
    case Page::units:
        b.backPanel("Temperature unit");
        b.button({32, 90, 199, 73}, "Celsius  °C", Action::celsius, !cycle.settings.fahrenheit);
        b.button({249, 90, 199, 73}, "Fahrenheit  °F", Action::fahrenheit, cycle.settings.fahrenheit);
        break;
    case Page::about: b.about(); break;
    case Page::reset:
        b.modal("Factory reset", "Restore default options and clear the saved cycle?", "Reset", "Cancel");
        break;
    case Page::stop:
        b.modal("Stop cycle?", "The remaining time will be cleared.", "Stop", "Cancel");
        break;
    case Page::finished: {
        const auto completion = cycle.completion();
        const char* message = completion == Completion::humidity ? "Humidity stayed at or below the goal for five minutes." :
                              completion == Completion::humidityTimeLimit ? "The maximum drying time was reached." :
                              "The selected drying time has elapsed.";
        b.modal("Cycle finished", message, "Done");
        break;
    }
    case Page::fault:
        b.error("Cycle paused", faultMessage(cycle.notice()), cycle.notice() == Notice::thermostat);
        break;
    case Page::lid:
        b.modal("Lid open", "Close the lid before starting or resuming a cycle.", "OK");
        break;
    case Page::diagnostics: b.readout(true); break;
    case Page::chamber: b.readout(false); break;
    case Page::selfTest: b.tests(); break;
    case Page::testUnavailable:
        b.modal("Test unavailable", "Stop the active cycle before starting a self-test.", "OK");
        break;
    case Page::graph: b.graph(); break;
    case Page::faultLog: b.errors(); break;
    case Page::sound: b.soundSettings(); break;
    case Page::display: b.sleepSettings(); break;
    case Page::advanced:
        b.backPanel("Advanced");
        text(screen_, "Maximum drying temperature", 18, 92, &lv_font_montserrat_16);
        text(screen_, cycle.settings.highTemperature ? "100 °C" : "85 °C", 18, 127, &lv_font_montserrat_24, accent);
        if (!cycle.cycleExists()) {
            b.button({18, 185, 264, 54}, cycle.settings.highTemperature ? "Restore 85 °C limit" : "Unlock up to 100 °C",
                     Action::changeLimit);
        }
        break;
    case Page::unlock:
        panel(screen_, {38, 33, 404, 254}, rail, 12);
        text(screen_, "High temperature warning", 58, 54, &lv_font_montserrat_24, amber);
        text(screen_, "Temperatures above 85 °C can damage filament, spools or dryer parts. Check material and spool ratings before use. This setting stays enabled after restart.",
             58, 99, &lv_font_montserrat_16, ink, 361);
        b.button({171, 224, 126, 44}, "Cancel", Action::back);
        b.button({304, 224, 116, 44}, "Unlock", Action::confirm, true);
        break;
    case Page::recovery: {
        char time[24], message[96];
        duration(time, cycle.seconds());
        lv_snprintf(message, sizeof message, "%s cycle with %s remaining.",
                      profileName(cycle.settings.profile, cycle.settings), time);
        b.modal("Resume saved cycle?", message, "Resume", "Discard");
        break;
    }
    case Page::waitForSensors:
        b.modal("Sensors unavailable", "Wait for a valid chamber and thermistor reading, then try again.", "OK");
        break;
    case Page::storageError:
        b.error("Storage error", faultMessage(Notice::storage));
        break;
    case Page::controlUnavailable:
        b.error("Start unavailable", faultMessage(Notice::controller));
        break;
    case Page::tooHot:
        b.error("Temperature limit", faultMessage(Notice::overTemperature));
        break;
    }
    if (previous != screen_) {
        lv_scr_load(screen_);
        if (previous) lv_obj_del(previous);
    }
    if (const auto* position = savedScroll(); list_ && position && *position) {
        lv_obj_update_layout(screen_);
        lv_obj_scroll_to_y(list_, *position, LV_ANIM_OFF);
    }
}

const char* Ui::profileNameDraft() const {
    return nameInput_ ? lv_textarea_get_text(nameInput_) : "";
}

void Ui::updateCustom(const Filament& draft, bool fahrenheit) {
    char value[24];
    if (temperatureSlider_) {
        lv_slider_set_value(temperatureSlider_, static_cast<int>((draft.temperature - customTemperatureMin) / customTemperatureStep), LV_ANIM_OFF);
        targetTemperature(value, draft.temperature, fahrenheit);
        replace(customTemperature_, value);
    }
    if (timeSlider_) {
        lv_slider_set_value(timeSlider_, static_cast<int>(draft.minutes / customTimeStepMinutes), LV_ANIM_OFF);
        duration(value, draft.minutes * 60);
        replace(customTime_, value);
    }
    if (humiditySlider_) {
        lv_slider_set_value(humiditySlider_, static_cast<int>(draft.humidity), LV_ANIM_OFF);
        lv_snprintf(value, sizeof value, "%u%% RH", draft.humidity);
        replace(customHumidity_, value);
    }
}

void Ui::updateVolume(unsigned volume) {
    char value[8];
    lv_snprintf(value, sizeof value, "%u%%", volume);
    replace(volumeLabel_, value);
}

void Ui::updateTest(const SelfTest& test) {
    if (!testPrompt_) return;
    for (unsigned i = 0; i < SelfTest::count; ++i) {
        replace(testRows_[i], testStatus[static_cast<unsigned>(test.result(static_cast<TestItem>(i)))]);
    }
    const bool manual = test.active() && test.result(test.item()) == TestResult::manual;
    if (manual) {
        lv_obj_clear_flag(testYes_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(testNo_, LV_OBJ_FLAG_HIDDEN);
        replace(testPrompt_, test.item() == TestItem::buzzer ? "Did you hear three rising tones?" : "Is the spool drive turning?");
    } else {
        lv_obj_add_flag(testYes_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(testNo_, LV_OBJ_FLAG_HIDDEN);
        replace(testPrompt_, test.active() ? "Lid detection bypassed during testing. Keep clear." :
                (*test.detail() ? test.detail() : "Select a component to test again."));
    }
}

void Ui::updateChart(const History& history, bool fahrenheit) {
    if (!chart_ || (chartCount_ == history.size() && chartInterval_ == history.intervalSeconds())) return;
    chartCount_ = history.size();
    chartInterval_ = history.intervalSeconds();
    if (!chartCount_) {
        replace(chartSpan_, "No measurements for this session.");
        return;
    }
    lv_chart_set_point_count(chart_, static_cast<std::uint16_t>(chartCount_ > 1 ? chartCount_ : 2));
    for (unsigned i = 0; i < chartCount_; ++i) {
        const auto& sample = history[i];
        const std::int16_t values[]{sample.chamber, sample.heater, sample.humidity};
        for (unsigned j = 0; j < 3; ++j) {
            auto value = values[j] == ChartSample::missing ? LV_CHART_POINT_NONE :
                         (j == 2 ? (values[j] + 5) / 10 :
                          (inChosenUnit(values[j] * 10, fahrenheit) + 50) / 100);
            lv_chart_set_value_by_id(chart_, series_[j], static_cast<std::uint16_t>(i), static_cast<lv_coord_t>(value));
        }
    }
    char time[24], span[58];
    duration(time, (chartCount_ - 1) * chartInterval_);
    lv_snprintf(span, sizeof span, "Elapsed  00:00  to  %s", time);
    replace(chartSpan_, span);
    lv_chart_refresh(chart_);
}

void Ui::update(const Cycle& cycle, const Readings& values) {
    updateToggle(rotation_, cycle.settings.rotation);
    updateToggle(lid_, cycle.settings.lidDetection);
    if (manualFan_) {
        const auto duty = static_cast<int>(cycle.manualFanDuty());
        lv_slider_set_value(manualFan_, duty, LV_ANIM_OFF);
        char percent[8];
        lv_snprintf(percent, sizeof percent, "%d%%", duty);
        replace(fanValue_, percent);
    }
    char buffer[24];
    const bool fahrenheit = cycle.settings.fahrenheit;
    const bool unitChanged = !readingsCached_ || fahrenheit != displayedFahrenheit_;
    const auto now = lv_tick_get();
    const int homeValue = homeTemperature_.update(now, values.thermistorAt, values.thermistorHundredths,
                                                  cycle.settings.targetCelsius(), cycle.running(), values.thermistorValid);
    const bool detailedReadout = page_ == Page::diagnostics || page_ == Page::chamber;
    if (!detailedReadout || unitChanged || now - readoutAt_ >= 500) {
        readoutAt_ = now;
        const bool chamberChanged = !readingsCached_ || displayed_.chamberValid != values.chamberValid;
        const bool home = page_ == Page::home;
        const bool hotChanged = displayed_.thermistorValid != values.thermistorValid ||
                                displayedHomeTemperature_ != homeValue;
        const bool airChanged = chamberChanged || displayed_.chamberHundredths != values.chamberHundredths;
        if (chamber_ && (unitChanged || (home ? hotChanged : airChanged))) {
            if (home) {
                if (values.thermistorValid) {
                    // Convert and round to whole degrees in one step.
                    const int scale = fahrenheit ? 500 : 100;
                    const int t = fahrenheit ? homeValue * 9 + 32 * scale : homeValue;
                    lv_snprintf(buffer, sizeof buffer, "%d °%c", (t + (t >= 0 ? scale / 2 : -scale / 2)) / scale,
                                fahrenheit ? 'F' : 'C');
                } else {
                    lv_snprintf(buffer, sizeof buffer, "--");
                }
            } else {
                temperature(buffer, values.chamberHundredths, fahrenheit, values.chamberValid);
            }
            replace(chamber_, buffer);
        }
        if (humidity_ && (chamberChanged || displayed_.humidityTenths != values.humidityTenths)) {
            if (values.chamberValid) {
                lv_snprintf(buffer, sizeof buffer, "%d.%d %%RH", values.humidityTenths / 10, values.humidityTenths % 10);
                if (page_ == Page::home) lv_snprintf(buffer, sizeof buffer, "%d %%RH", (values.humidityTenths + 5) / 10);
                replace(humidity_, buffer);
            } else {
                replace(humidity_, "--");
            }
        }
        if (thermistor_ && (unitChanged || displayed_.thermistorValid != values.thermistorValid ||
                           displayed_.thermistorHundredths != values.thermistorHundredths)) {
            temperature(buffer, values.thermistorHundredths, fahrenheit, values.thermistorValid);
            replace(thermistor_, buffer);
        }
        displayed_ = values;
        displayedHomeTemperature_ = homeValue;
        displayedFahrenheit_ = fahrenheit;
        readingsCached_ = true;
    }
    const auto seconds = cycle.seconds();
    if (time_) {
        duration(buffer, seconds);
        replace(time_, buffer);
    }
    if (progress_) {
        const auto durationSeconds = cycle.totalSeconds();
        const auto width = durationSeconds >= seconds && durationSeconds ?
                           452u * (durationSeconds - seconds) / durationSeconds : 0u;
        if (lv_obj_get_style_width(progress_, 0) != static_cast<lv_coord_t>(width)) {
            lv_obj_set_width(progress_, static_cast<lv_coord_t>(width));
        }
    }
}

}  // namespace h1::cold

