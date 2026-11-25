CROSS ?= arm-none-eabi-
HOST_CC ?= cc
HOST_CXX ?= c++
BUILD ?= build
TARGET := $(BUILD)/h1ht-beta-0.17
PACKAGE ?= $(BUILD)/ELEGOO_H1_HT_Beta_0.17.zip
LVGL_DIR ?= ../lvgl
NEWLIB_SYSROOT ?=
REFERENCE := drivers
APP := app.cpp beta_ui.cpp cold_outputs.cpp cycle.cpp service.cpp fan_warmup.cpp storage.cpp journal_flash.cpp
HAL := board.cpp i2c.cpp sensors.cpp humidity_sensor.cpp vent_control.cpp heat_control.cpp $(patsubst $(REFERENCE)/%,%,$(wildcard $(REFERENCE)/hal/*.cpp))
LVGL_C := $(shell cd "$(LVGL_DIR)" 2>/dev/null && find src -name '*.c' | LC_ALL=C sort)
LV_CONF := -I. -isystem $(LVGL_DIR) -DLV_CONF_INCLUDE_SIMPLE
COMMON := -Os -ffunction-sections -fdata-sections
CPU := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
# Keep conditional register writes outside multi-instruction IT blocks for the MMIO tests.
ARM_C := $(CPU) $(COMMON) -DH1_ARM_FIRMWARE -ffreestanding -fno-if-conversion -fno-if-conversion2
ARM_CPP = $(ARM_C) -std=c++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -fno-unwind-tables -fno-asynchronous-unwind-tables \
           -Wall -Wextra -Wconversion -Werror
ifneq ($(strip $(NEWLIB_SYSROOT)),)
ARM_C += -isystem $(NEWLIB_SYSROOT)/usr/include/newlib
ARM_LIB := -L$(NEWLIB_SYSROOT)/usr/lib/arm-none-eabi/newlib/$(shell $(CROSS)gcc $(CPU) -print-multi-directory)
endif
ARM_OBJECTS := $(addprefix $(BUILD)/app/,$(APP:.cpp=.o)) \
               $(addprefix $(BUILD)/hal/,$(HAL:.cpp=.o)) \
               $(addprefix $(BUILD)/lvgl/,$(LVGL_C:.c=.o))
HOST_LVGL := $(addprefix $(BUILD)/host/,$(LVGL_C:.c=.o))

.PHONY: all check-lvgl package test preview
all: $(TARGET).bin

check-lvgl:
	@test -f "$(LVGL_DIR)/lvgl.h" || { echo "Set LVGL_DIR to LVGL v8.3.11 source"; exit 1; }
	@awk '/^#define LVGL_VERSION_MAJOR / {a=$$3} /^#define LVGL_VERSION_MINOR / {b=$$3} /^#define LVGL_VERSION_PATCH / {c=$$3} END {exit !(a==8 && b==3 && c==11)}' "$(LVGL_DIR)/lvgl.h" || { echo "LVGL v8.3.11 required"; exit 1; }

$(BUILD)/app/%.o: %.cpp | check-lvgl
	@mkdir -p $(@D)
	$(CROSS)g++ $(ARM_CPP) $(LV_CONF) -I$(REFERENCE) -MMD -MP -c $< -o $@

$(BUILD)/hal/%.o: $(REFERENCE)/%.cpp
	@mkdir -p $(@D)
	$(CROSS)g++ $(ARM_CPP) -MMD -MP -c $< -o $@

$(BUILD)/lvgl/%.o: $(LVGL_DIR)/%.c | check-lvgl
	@mkdir -p $(@D)
	$(CROSS)gcc $(ARM_C) $(if $(filter src/draw/%,$*),-O2) $(LV_CONF) -MMD -MP -c $< -o $@

$(BUILD)/host/%.o: $(LVGL_DIR)/%.c | check-lvgl
	@mkdir -p $(@D)
	$(HOST_CC) $(COMMON) $(LV_CONF) -MMD -MP -c $< -o $@

$(TARGET).elf: $(ARM_OBJECTS) gd32f303.ld
	$(CROSS)g++ $(CPU) -nostdlib -T gd32f303.ld -Wl,--gc-sections,-Map,$(TARGET).map \
		$(ARM_OBJECTS) $(ARM_LIB) -Wl,--start-group -lc_nano -lgcc -Wl,--end-group -o $@
	$(CROSS)size $@

$(TARGET).bin: $(TARGET).elf
	$(CROSS)objcopy -O binary $< $@

package: $(TARGET).bin
	python3 package.py "$<" "$(PACKAGE)"

HOST_CYCLE_SOURCES := cycle_test.cpp cycle.cpp service.cpp fan_warmup.cpp storage.cpp \
                      $(addprefix $(REFERENCE)/,heat_control.cpp humidity_sensor.cpp sensors.cpp)
HOST_CYCLE_HEADERS := cycle.hpp fan_warmup.hpp storage.hpp service.hpp \
                      $(addprefix $(REFERENCE)/,heat_control.hpp humidity_sensor.hpp sensors.hpp vent_control.hpp)

$(BUILD)/cycle_test: $(HOST_CYCLE_SOURCES) $(HOST_CYCLE_HEADERS)
	@mkdir -p $(@D)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Wconversion -Werror -fsanitize=address,undefined -fno-pie -no-pie \
		-I$(REFERENCE) $(HOST_CYCLE_SOURCES) -o $@

test: $(BUILD)/cycle_test
	"$<"

$(BUILD)/ui_test: ui_test.cpp beta_ui.cpp cycle.cpp service.cpp storage.cpp $(HOST_LVGL) beta_ui.hpp cycle.hpp
	$(HOST_CXX) -std=c++17 $(COMMON) $(LV_CONF) -I$(REFERENCE) \
		ui_test.cpp beta_ui.cpp cycle.cpp service.cpp storage.cpp $(HOST_LVGL) -Wl,--gc-sections -o $@

preview: $(BUILD)/ui_test
	"$<" "$(BUILD)"

-include $(ARM_OBJECTS:.o=.d) $(HOST_LVGL:.o=.d)

