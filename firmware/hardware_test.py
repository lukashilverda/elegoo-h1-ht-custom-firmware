"""Run the update binary with modeled GD32 peripherals on a Cortex-M4."""

import argparse
import struct
import subprocess
import zlib
from pathlib import Path

from unicorn import (
    UC_ARCH_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
)
from unicorn.arm_const import (
    UC_CPU_ARM_CORTEX_M4, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP,
)

FLASH = 0x08008000
JOURNAL = 0x08047000
RETURN = 0x0807FFF0
PA, PB, PC = 0x40010800, 0x40010C00, 0x40011000
ADC = 0x40012400
RCU = 0x40021000
PMU = 0x40007000
WATCHDOG = 0x40003000
FMC_KEY, FMC_STATUS, FMC_CTL, FMC_ADDR = (0x40022004, 0x4002200C, 0x40022010, 0x40022014)
LCD_CMD, LCD_DATA = 0x60000000, 0x60020000
FAN_FLAGS, FAN_COMPARE = 0x40012C10, 0x40012C34
STEP_TIMER = 0x40000C00
BUZZ_TIMER = 0x40001000
BUZZ_PIN = 1 << 10
BOOT_NOTES = [960, 960]
NORMAL_PWM, WARMUP_PWM = 4320, 3840
LID_OPEN = 1 << 2
MOTOR_DISABLED, MOTOR_STEP = 1 << 1, 1 << 3
VENT_READY, VENT_MASK = 1 << 11, 0x3C0
VENT_CLOSE, VENT_OPEN = (1 << 6) | (1 << 9), (1 << 7) | (1 << 8)


class Board:
    def __init__(self, image, nm, pll=True, crystal=True, high_drive=True):
        self.image = image.read_bytes()
        lines = subprocess.check_output([nm, "-Cn", str(image.with_suffix(".elf"))], text=True)
        self.symbols = {}
        for line in lines.splitlines():
            fields = line.split(maxsplit=2)
            if len(fields) == 3:
                self.symbols[fields[2]] = int(fields[0], 16)
        assert self.symbols["_settings_start"] == JOURNAL
        self.cpu = Uc(UC_ARCH_ARM, UC_MODE_MCLASS | UC_MODE_THUMB)
        self.cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
        for base, size in (
            (FLASH, 0x78000), (0x20000000, 0x10000), (0x40000000, 0x40000),
            (0xE0001000, 0x1000), (0xE000E000, 0x1000), (0xA0000000, 0x1000),
            (LCD_CMD, 0x1000), (LCD_DATA, 0x1000),
        ):
            self.cpu.mem_map(base, size)
        self.cpu.mem_write(FLASH, self.image)
        self.cpu.mem_write(JOURNAL, b"\xff" * 4096)
        self.cpu.reg_write(UC_ARM_REG_SP, 0x2000FFF8)
        self.write(RCU, 3)
        self.write(FMC_CTL, 1 << 7)
        self.pll = pll
        self.crystal = crystal
        self.high_drive = high_drive
        self.key_received = False
        self.heater_clears = 0
        self.heating = False
        self.heat_enabled_by_test = False
        self.watchdog_started = False
        self.flushes = 0
        self.expected_label = None
        self.saw_screen_label = False
        self.booting = False
        self.lcd_commands = []
        self.lcd_pixels = []
        self.ticks = self.symbols["(anonymous namespace)::ticks"]
        self.delay = self.symbols["h1::board::delay(unsigned long)"]
        self.delay_us = self.symbols["h1::board::delayMicroseconds(unsigned long)"]
        self.flush = next(addr for name, addr in self.symbols.items()
                          if name.startswith("h1::cold::(anonymous namespace)::flush("))
        self.set_label = self.symbols["lv_label_set_text"]
        self.has_real_inputs = False
        self.started_heating_at = None
        self.lid_cutoff_verified = False
        self.expect_fault = False
        self.cut_heating_after = 570
        self.buzzer_notes = []
        self.warning_at = None
        self.home_at = None
        self.touch_home_used = False
        self.touch_warning_used = False
        for begin, end in ((PA, PC + 0x14), (FMC_KEY, FMC_ADDR), (WATCHDOG, WATCHDOG),
                           (FLASH, 0x0807ffff)):
            self.cpu.hook_add(UC_HOOK_MEM_WRITE, self.mmio_write, begin=begin, end=end)
        for address in (self.delay, self.delay_us, self.flush, self.set_label, self.symbols["Default_Handler"]):
            self.cpu.hook_add(UC_HOOK_CODE, self.execute_hook, begin=address, end=address)
        for begin, end in ((FMC_STATUS, FMC_STATUS), (ADC, ADC + 8), (RCU, RCU + 0x24), (PMU + 4, PMU + 4)):
            self.cpu.hook_add(UC_HOOK_MEM_READ, self.mmio_read, begin=begin, end=end)

    def read(self, address):
        return struct.unpack("<I", self.cpu.mem_read(address, 4))[0]

    def write(self, address, value):
        self.cpu.mem_write(address, struct.pack("<I", value))

    def mmio_write(self, _cpu, _access, address, size, value, _user):
        if address == PB + 0x10 and value & 2:
            assert self.heat_enabled_by_test, "Unexpected PB1 high"
            self.heating = True
            if self.has_real_inputs and self.started_heating_at is None:
                self.started_heating_at = self.read(self.ticks)
        if address == PB + 0x14 and value & 2:
            self.heater_clears += 1
            self.heating = False
        if address == WATCHDOG and value == 0xcccc:
            self.watchdog_started = True
        if address in (PA + 0x10, PA + 0x14):
            output = self.read(PA + 0x0c)
            self.write(PA + 0x0c, output | value if address == PA + 0x10 else output & ~value)
        if address in (PC + 0x10, PC + 0x14):
            output = self.read(PC + 0x0c)
            level = output | value if address == PC + 0x10 else output & ~value
            assert level & 0xC0 != 0xC0 and level & 0x300 != 0x300
            self.write(PC + 0x0c, level)
        if address == FMC_KEY:
            if self.key_received and value == 0xCDEF89AB:
                self.write(FMC_CTL, self.read(FMC_CTL) & ~(1 << 7))
            self.key_received = value == 0x45670123
        elif address == FMC_STATUS:
            assert value == 0x34
        elif address == FMC_CTL and value & (1 << 6):
            page = self.read(FMC_ADDR)
            assert value == 0x42 and page in (JOURNAL, JOURNAL + 2048)
            self.cpu.mem_write(page, b"\xff" * 2048)
        elif FLASH <= address < 0x08080000:
            assert JOURNAL <= address < JOURNAL + 4096 and size == 4
            assert self.read(FMC_CTL) == 1 and self.read(address) == 0xffffffff

    def execute_hook(self, cpu, address, _size, _user):
        if address == self.delay:
            now = self.read(self.ticks) + cpu.reg_read(UC_ARM_REG_R0)
            self.write(self.ticks, now)
            if self.has_real_inputs and self.watchdog_started:
                if self.started_heating_at is not None and now - self.started_heating_at >= self.cut_heating_after:
                    if self.expect_fault:
                        if self.saw_screen_label and len(self.buzzer_notes) >= 6:
                            assert not self.heating
                            cpu.emu_stop()
                            return
                    else:
                        self.heat_enabled_by_test = False
                        self.write(PA + 8, LID_OPEN)
                        if not self.heating:
                            self.lid_cutoff_verified = True
                            cpu.emu_stop()
                            return
                if self.read(FAN_COMPARE):
                    self.write(FAN_FLAGS, 4)
                cpu.reg_write(UC_ARM_REG_R0, now)
                cpu.reg_write(UC_ARM_REG_PC, self.symbols["firmwareTick"] | 1)
            else:
                cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == self.delay_us:
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))
        elif address == self.set_label:
            pointer = cpu.reg_read(UC_ARM_REG_R1)
            if not pointer:
                return
            if self.expected_label and cpu.mem_read(pointer, len(self.expected_label)) == self.expected_label:
                self.saw_screen_label = True
            if self.has_real_inputs:
                if cpu.mem_read(pointer, 9) == b"Set temp\0":
                    self.home_at = self.read(self.ticks)
                if cpu.mem_read(pointer, 17) == b"Before you start\0":
                    self.warning_at = self.read(self.ticks)
        elif address == self.flush:
            self.flushes += 1
            if self.saw_screen_label and not self.has_real_inputs:
                cpu.emu_stop()
        elif self.booting:
            raise AssertionError("Entered default exception handler during boot")

    def mmio_read(self, _cpu, _access, address, _size, _value, _user):
        if address == FMC_STATUS:
            self.write(FMC_STATUS, 0)
            return
        if address == RCU:
            control = self.read(RCU)
            pll_stable = (1 << 25) if self.pll and control & (1 << 24) else 0
            crystal_stable = (1 << 17) if self.crystal and control & (1 << 16) else 0
            self.write(RCU, (control & ~((1 << 25) | (1 << 17))) | pll_stable | crystal_stable | 2)
            return
        if address == RCU + 4:
            config = self.read(RCU + 4)
            self.write(RCU + 4, (config & ~0x0c) | ((config & 3) << 2))
            return
        if address == RCU + 0x24:
            self.write(address, self.read(address) | 2)
            return
        if address == PMU + 4:
            self.write(address, self.read(PMU) & (3 << 16) if self.high_drive else 0)
            return
        if address not in (ADC, ADC + 8):
            return
        adc_control = self.read(ADC + 8)
        if adc_control & (3 << 2):
            self.write(ADC + 8, adc_control & ~(3 << 2))
        if adc_control & (1 << 22):
            self.write(ADC + 8, adc_control & ~(1 << 22))
            self.write(ADC, 1 << 1)
            self.write(ADC + 0x4C, 3908)

    def call(self, name, *arguments):
        for register, argument in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), arguments):
            self.cpu.reg_write(register, argument)
        for i, argument in enumerate(arguments[4:]):
            self.write(self.cpu.reg_read(UC_ARM_REG_SP) + 4 * i, argument)
        self.cpu.reg_write(UC_ARM_REG_LR, RETURN | 1)
        self.cpu.emu_start(self.symbols[name] | 1, RETURN, timeout=1_000_000)
        assert self.cpu.reg_read(UC_ARM_REG_PC) == RETURN, name
        return self.cpu.reg_read(UC_ARM_REG_R0)

    def hw(self, name, *arguments):
        return self.call("h1::cold::hardware::" + name, *arguments)

    def publish(self, now, vent=0, fan=False, motor=False, lid=True, restart=False, heat=0, warmup=False, vent_mask=3,
                diagnostic_heat=False):
        # AAPCS packs the first four flags in one word and duty and vent mask in the next.
        command = vent | (fan << 8) | (motor << 16) | (lid << 24)
        self.hw("publish(h1::cold::Outputs, unsigned long, bool, unsigned long, bool)",
                command, (vent_mask << 8) | (diagnostic_heat << 16), now, restart, heat, warmup)

    def tick(self, now, fan_pulse=False):
        self.write(self.ticks, now)
        if fan_pulse:
            self.write(FAN_FLAGS, 4)
        self.hw("tick(unsigned long)", now)
        return self.read(PC + 0x0C)

    def step(self, at_ms=0, late_us=0):
        now = (self.read(STEP_TIMER + 0x34) + late_us) & 0xffff
        self.write(self.ticks, at_ms)
        self.write(STEP_TIMER + 0x24, now)
        self.write(STEP_TIMER + 0x10, 2)
        self.call("Timer4_Handler")
        return bool(self.read(PC + 0x0C) & MOTOR_STEP), (self.read(STEP_TIMER + 0x34) - now) & 0xffff

    def boot(self):
        stack, reset = struct.unpack_from("<II", self.image)
        self.cpu.reg_write(UC_ARM_REG_SP, stack)
        self.booting = True
        self.cpu.emu_start(reset, RETURN, timeout=15_000_000)

    def load_saved_cycle(self, payload, header=0x48435431):
        sequence = 1
        data = struct.pack("<II", sequence, payload)
        if header == 0x48435432:
            data += struct.pack("<I", header)
        checksum = zlib.crc32(data)
        self.cpu.mem_write(JOURNAL, struct.pack("<IIII", header, sequence, payload, checksum))

    def capture_buzzer_notes(self):
        def capture_tone(cpu, _address, _size, _user):
            self.buzzer_notes.append(cpu.reg_read(UC_ARM_REG_R0))

        tone = self.symbols["h1::hal::buzzer::setTone(unsigned int)"]
        self.cpu.hook_add(UC_HOOK_CODE, capture_tone, begin=tone, end=tone)

    def start_control_scenario(self, fault=None):
        thermistor_samples = []

        def capture_thermistor_read(_cpu, _address, _size, _user):
            thermistor_samples.append(self.read(self.ticks))

        self.capture_buzzer_notes()
        if fault == "over_temperature":
            self.load_saved_cycle(0x60)  # Default setpoint, notifications off.
            self.expected_label = b"P02"
        elif fault == "sensor":
            self.load_saved_cycle(0x60)  # Idle, lid detection and rotation on, notifications off.
            self.expected_label = b"H01"
        if fault:
            self.expect_fault = True
            self.cut_heating_after = 135
        self.has_real_inputs = True
        self.heat_enabled_by_test = True
        self.write(PC + 8, VENT_READY | 1)
        self.write(PA + 8, 2)

        def sensor_callback(cpu, _address, _size, _user):
            fault_active = fault and self.started_heating_at is not None and (
                self.read(self.ticks) - self.started_heating_at >= self.cut_heating_after
            )
            if fault_active:
                self.heat_enabled_by_test = False
            if fault_active and fault == "sensor":
                cpu.reg_write(UC_ARM_REG_R0, 2)
            else:
                target = cpu.reg_read(UC_ARM_REG_R2)
                temp = 23.0
                if fault == "over_temperature":
                    temp = 101.0 if fault_active else 23.0
                self.cpu.mem_write(target, struct.pack("<Iff", 0, temp, 30.0))
                cpu.reg_write(UC_ARM_REG_R0, 1)
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        def touch_callback(cpu, _address, _size, _user):
            now = self.read(self.ticks)
            is_home = self.home_at is not None and self.warning_at is None and now - self.home_at >= 80
            is_warning = self.warning_at is not None and now - self.warning_at >= 80
            hit = False
            if is_home and not self.touch_home_used:
                hit = now - self.home_at < 135
                self.touch_home_used = not hit
                self.cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1), struct.pack("<II", 340, 270))
            elif is_warning and not self.touch_warning_used:
                hit = now - self.warning_at < 135
                self.touch_warning_used = not hit
                self.cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1), struct.pack("<II", 350, 247))
            cpu.reg_write(UC_ARM_REG_R0, int(hit))
            cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

        poll = self.symbols["h1::HumiditySensor::poll(unsigned long, h1::ChamberReading&)"]
        touch = self.symbols["h1::board::Touchscreen::read(h1::board::TouchPoint&)"]
        thermistor_read = self.symbols["h1::hal::thermistor::read()"]
        self.cpu.hook_add(UC_HOOK_CODE, sensor_callback, begin=poll, end=poll)
        self.cpu.hook_add(UC_HOOK_CODE, touch_callback, begin=touch, end=touch)
        self.cpu.hook_add(UC_HOOK_CODE, capture_thermistor_read, begin=thermistor_read, end=thermistor_read)
        self.boot()
        assert self.watchdog_started and self.started_heating_at is not None, (
            "Start not heated", self.home_at, self.warning_at, self.read(self.ticks)
        )
        assert not self.heating
        intervals = [current - previous for previous, current in zip(thermistor_samples, thermistor_samples[1:])]
        assert len(intervals) >= 3 and all(200 <= interval <= 210 for interval in intervals), intervals
        if fault:
            assert self.saw_screen_label and self.buzzer_notes == [880, 1320, 1760] * 2, self.buzzer_notes
        else:
            assert self.lid_cutoff_verified and self.buzzer_notes == BOOT_NOTES + [988, 1319, 1568], self.buzzer_notes


def test_filament_profile_flow(board):
    board.has_real_inputs = True
    board.write(PC + 8, VENT_READY | 1)
    board.write(PA + 8, 2)
    first_touch = None
    taps = [
        (180, 183),  # Home → Set temp
        (240, 198),  # 65 °C on the quick-temperature slider
        (234, 277),  # Apply
        (177, 270),  # Home → Filament
        (350, 88),   # My profiles
        (230, 291),  # Save current profile
        (65, 170),   # 'q' on the profile-name keyboard
        (407, 35),   # Save, which activates the new profile on Home
    ]

    def touch_callback(cpu, _address, _size, _user):
        nonlocal first_touch
        now = board.read(board.ticks)
        if first_touch is None and board.home_at is not None:
            first_touch = board.home_at + 100
        hit = False
        if first_touch is not None and now >= first_touch:
            elapsed = now - first_touch
            step = elapsed // 195
            if step >= len(taps):
                cpu.emu_stop()
                return
            hit = elapsed % 195 < 55
            if hit:
                cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1), struct.pack("<II", *taps[step]))
        cpu.reg_write(UC_ARM_REG_R0, int(hit))
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    address = board.symbols["h1::board::Touchscreen::read(h1::board::TouchPoint&)"]
    board.cpu.hook_add(UC_HOOK_CODE, touch_callback, begin=address, end=address)
    board.boot()

    # Configuration records have a five-word header followed by settings and profile slots.
    saved = []
    for offset in range(0, 4096 - 80 * 4 + 1, 16):
        record = JOURNAL + offset
        if board.read(record) != 0x48435433:
            continue
        selection = board.read(record + 5 * 4) & 0xff
        if selection == 13:
            name = board.cpu.mem_read(record + 7 * 4, 16).split(b"\x00")[0]
            program = board.read(record + 11 * 4)
            saved.append((bytes(name), program & 0xff, (program >> 8) & 0xff, (program >> 16) & 0xff))
    assert saved == [(b"q", 65, 16, 0)], saved
    assert first_touch is not None and board.home_at >= first_touch + 7 * 195
    assert not board.heating


def test_outputs(board):
    board.call("h1::hal::heater::initialize()")
    board.call("h1::board::initialize()")
    assert board.call("h1::board::coreClockHz()") == 120_000_000
    assert board.read(0xE000E014) == 119999
    assert board.read(RCU + 4) & 0x0837070F == 0x0837040A
    board.hw("initialize()")
    board.write(PC + 8, VENT_READY)
    assert board.heater_clears == 1 and not board.read(FAN_COMPARE)
    assert board.read(PC + 0x0C) & MOTOR_DISABLED
    assert board.read(STEP_TIMER + 0x28) == 119

    board.publish(10, fan=True, motor=True, heat=300)
    for now in (11, 12):
        assert board.tick(now, fan_pulse=True) & MOTOR_DISABLED
        assert not board.heating
    board.heat_enabled_by_test = True
    assert not board.tick(13, fan_pulse=True) & MOTOR_DISABLED
    assert board.read(FAN_COMPARE) == NORMAL_PWM and board.heating

    board.heat_enabled_by_test = False
    board.write(PA + 8, LID_OPEN)
    assert board.tick(14) & MOTOR_DISABLED
    assert not board.read(FAN_COMPARE) and not board.heating
    assert not board.read(PC + 0x0C) & MOTOR_STEP

    # Close the lid before the application reads it. The transient stays latched.
    board.write(PA + 8, 0)
    board.publish(15, fan=True, motor=True, heat=300)
    assert board.tick(16, fan_pulse=True) & MOTOR_DISABLED
    assert not board.heating and not board.read(FAN_COMPARE)
    assert board.hw("takeLidOpen()") == 1 and board.hw("takeLidOpen()") == 0

    # With detection disabled, the fan and rotator stay available with the lid open.
    board.write(PA + 8, LID_OPEN)
    board.publish(17, fan=True, motor=True, lid=False, heat=300)
    for now in (18, 19, 20):
        board.tick(now, fan_pulse=True)
    assert not board.tick(21) & MOTOR_DISABLED
    assert board.read(FAN_COMPARE) == NORMAL_PWM and not board.heating
    assert board.tick(317) & MOTOR_DISABLED and not board.read(FAN_COMPARE)

    board.publish(500, vent=1)
    board.tick(500)
    board.publish(800, vent=1)
    assert board.tick(800) & VENT_MASK == VENT_CLOSE
    board.publish(1100)
    assert board.tick(1100) & VENT_MASK == 0

    board.write(PA + 8, 0)
    assert board.hw("takeLidOpen()") == 1
    board.publish(1300, vent=2, restart=True)
    board.tick(1300)
    board.publish(1600, vent=2)
    assert board.tick(1600) & VENT_MASK == VENT_OPEN
    board.write(PC + 8, 0)
    assert board.tick(1601) & VENT_MASK == 0
    assert board.hw("ventFaults()") == 3

    board.write(PC + 8, VENT_READY)
    board.write(PA + 8, 1 << 1)
    for offset in (0, 300, 800, 1100, 2000, 2300, 3200, 3500, 4400, 4700, 5600, 5900, 6800, 7100, 8000, 8300):
        now = 2000 + offset
        board.publish(now, vent=2, restart=(offset == 0))
        board.tick(now)
    assert board.hw("ventFaults()") == 1 and not board.read(PC + 0x0C) & VENT_MASK

    board.publish(10400, fan=True, motor=True, restart=True, heat=300)
    board.heat_enabled_by_test = True
    for now in (10401, 10402, 10403):
        board.tick(now, fan_pulse=True)
    assert board.heating
    board.publish(10410, fan=True, motor=True, heat=40)
    board.tick(10449, fan_pulse=True)
    assert board.heating
    board.heat_enabled_by_test = False
    board.tick(10450, fan_pulse=True)
    assert not board.heating and board.read(FAN_COMPARE) == NORMAL_PWM

    board.heat_enabled_by_test = True
    board.publish(10600, fan=True, motor=True, heat=300)
    assert board.heating
    board.heat_enabled_by_test = False
    board.tick(10800)
    assert not board.heating and board.read(FAN_COMPARE) == NORMAL_PWM
    board.call("firmwareFault")
    assert not board.heating and not board.read(FAN_COMPARE)
    assert board.tick(10801) & MOTOR_DISABLED
    assert not board.read(PC + 0x0C) & ((1 << 10) | VENT_MASK)


def test_step_timing(board):
    board.call("h1::board::initialize()")
    board.call("h1::hal::stepper::initialize()")
    board.call("h1::hal::stepper::setRotating(bool)", True)
    assert board.read(STEP_TIMER + 0x2c) == 0xffff
    assert board.read(STEP_TIMER + 0x34) == 8000
    assert board.step(at_ms=8) == (True, 40)
    assert board.step(at_ms=8) == (False, 7711)

    assert board.step(at_ms=650, late_us=80) == (True, 40)
    assert board.step(at_ms=650) == (False, 1880)
    assert board.step(at_ms=652, late_us=2100) == (True, 40)
    assert board.step(at_ms=652) == (False, 2000)

    # Keep a 2 ms period across the 16-bit counter wrap.
    wrapped = False
    for now in range(654, 734, 2):
        rise = board.read(STEP_TIMER + 0x34)
        assert board.step(at_ms=now) == (True, 40)
        assert board.step(at_ms=now) == (False, 1960)
        next_rise = board.read(STEP_TIMER + 0x34)
        assert next_rise == (rise + 2000) & 0xffff
        wrapped = wrapped or next_rise < rise
    assert wrapped
    board.call("h1::hal::stepper::setRotating(bool)", False)
    assert board.read(PC + 0x0C) & (MOTOR_DISABLED | MOTOR_STEP) == MOTOR_DISABLED


def test_fan_qualification(board):
    board.call("h1::board::initialize()")
    board.hw("initialize()")
    board.write(PC + 8, VENT_READY)
    for now in (20, 320, 620):
        board.publish(now, fan=True, motor=True, heat=250)
        assert board.tick(now, fan_pulse=True) & MOTOR_DISABLED
        assert not board.hw("takeFanPulse()") and not board.heating
    board.heat_enabled_by_test = True
    board.tick(630, fan_pulse=True)
    assert not board.hw("takeFanPulse()") and not board.heating
    board.tick(640, fan_pulse=True)
    assert board.hw("takeFanPulse()") and board.heating

    board.publish(650, fan=True, motor=True, restart=True, heat=250)
    assert board.read(FAN_COMPARE) == NORMAL_PWM and not board.heating
    for now in (660, 670):
        assert board.tick(now, fan_pulse=True) & MOTOR_DISABLED
        assert not board.hw("takeFanPulse()") and not board.heating
    board.tick(680, fan_pulse=True)
    assert board.hw("takeFanPulse()") and board.heating


def test_manual_fan(board):
    def touch_callback(cpu, _address, _size, _user):
        hit = False
        if board.home_at is not None:
            elapsed = board.read(board.ticks) - board.home_at
            board.write(PA + 8, 2 | (LID_OPEN if 570 <= elapsed < 680 else 0))
            for start, x, y in ((80, 40, 165), (210, 314, 88), (340, 243, 88), (470, 314, 88)):
                if start <= elapsed < start + 55:
                    cpu.mem_write(cpu.reg_read(UC_ARM_REG_R1), struct.pack("<II", x, y))
                    hit = True
                    break
            if 300 <= elapsed < 330 or 550 <= elapsed < 570:
                assert 2300 <= board.read(FAN_COMPARE) <= 2500
            elif 430 <= elapsed < 460 or elapsed >= 620:
                assert not board.read(FAN_COMPARE)
            assert not board.heating and board.read(PC + 0x0C) & MOTOR_DISABLED
            if elapsed >= 800:
                cpu.emu_stop()
                return
        cpu.reg_write(UC_ARM_REG_R0, int(hit))
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    board.has_real_inputs = True
    board.write(PC + 8, VENT_READY | 1)
    board.write(PA + 8, 2)
    touch = board.symbols["h1::board::Touchscreen::read(h1::board::TouchPoint&)"]
    board.cpu.hook_add(UC_HOOK_CODE, touch_callback, begin=touch, end=touch)
    board.boot()
    assert board.home_at is not None and board.read(board.ticks) - board.home_at >= 800


def test_warmup_fan(board):
    board.call("h1::board::initialize()")
    board.hw("initialize()")
    board.write(PC + 8, VENT_READY)
    board.heat_enabled_by_test = True
    for now in range(0, 1751, 50):
        if now % 100 == 0:
            board.publish(now, fan=True, heat=300, warmup=True)
        board.tick(now, fan_pulse=True)
        if 1250 <= now < 1750:
            assert board.read(FAN_COMPARE) == WARMUP_PWM and not board.heating
    assert board.read(FAN_COMPARE) == WARMUP_PWM and board.heating
    for now in range(1800, 2251, 50):
        board.publish(now, fan=True, heat=300, warmup=True)
        board.tick(now, fan_pulse=(now % 150 == 0))
    assert board.read(FAN_COMPARE) == NORMAL_PWM and not board.heating
    for now in range(2300, 2501, 50):
        board.publish(now, fan=True, heat=300, warmup=True)
        board.tick(now, fan_pulse=True)
    assert board.read(FAN_COMPARE) == NORMAL_PWM and board.heating


def test_diagnostic_vent_channels(board):
    board.call("h1::board::initialize()")
    board.hw("initialize()")
    board.write(PC + 8, VENT_READY)
    board.write(PA + 8, LID_OPEN)
    for mask, open_pin in ((1, 7), (2, 8)):
        base = mask * 2000
        board.publish(base, vent=2, restart=True, vent_mask=mask, lid=False)
        board.tick(base)
        board.publish(base + 300, vent=2, vent_mask=mask, lid=False)
        assert board.tick(base + 300) & VENT_MASK == 1 << open_pin
        board.publish(base + 800, vent=2, vent_mask=mask, lid=False)
        assert board.tick(base + 800) & VENT_MASK == 0
        board.publish(base + 1100, vent=2, vent_mask=mask, lid=False)
        board.tick(base + 1100)
        assert board.hw("ventsReached()") == mask


def test_open_lid_heater_diagnostic(board):
    board.call("h1::board::initialize()")
    board.hw("initialize()")
    board.write(PC + 8, VENT_READY)
    board.write(PA + 8, LID_OPEN)
    board.heat_enabled_by_test = True
    board.publish(10, fan=True, lid=False, diagnostic_heat=True, heat=50)
    board.tick(11, fan_pulse=True)
    board.tick(12, fan_pulse=True)
    assert not board.heating
    board.tick(13, fan_pulse=True)
    assert board.heating and board.hw("takeLidOpen()") == 1 and board.heating
    board.tick(59, fan_pulse=True)
    assert board.heating
    board.tick(60, fan_pulse=True)
    assert not board.heating
    board.publish(61, fan=True, lid=False, diagnostic_heat=True, heat=150)
    assert board.heating
    board.publish(62, fan=True, lid=False, heat=150)
    assert not board.heating
    board.publish(63, fan=True, lid=True, diagnostic_heat=True, heat=150)
    assert not board.heating and not board.read(FAN_COMPARE)


def test_buzzer(board):
    board.call("h1::board::initialize()")
    board.call("h1::hal::buzzer::initialize()")
    assert board.read(BUZZ_TIMER + 0x28) == 119
    assert not board.read(PA + 0x0c) & BUZZ_PIN
    board.call("h1::hal::buzzer::setTone(unsigned int)", 988)
    assert board.read(BUZZ_TIMER + 0x2c) == 505
    assert board.read(BUZZ_TIMER) & 1 and board.read(PA + 0x0c) & BUZZ_PIN
    board.write(BUZZ_TIMER + 0x10, 1)
    board.call("Timer5_Handler")
    assert not board.read(PA + 0x0c) & BUZZ_PIN
    board.write(BUZZ_TIMER + 0x10, 1)
    board.call("Timer5_Handler")
    assert board.read(PA + 0x0c) & BUZZ_PIN
    board.call("h1::hal::buzzer::setVolume(unsigned int)", 50)
    board.call("h1::hal::buzzer::setTone(unsigned int)", 988)
    assert board.read(BUZZ_TIMER + 0x2c) == 302
    board.write(BUZZ_TIMER + 0x10, 1)
    board.call("Timer5_Handler")
    assert board.read(BUZZ_TIMER + 0x2c) == 708
    board.call("h1::hal::buzzer::setEnabled(bool)", False)
    assert not board.read(BUZZ_TIMER) & 1 and not board.read(PA + 0x0c) & BUZZ_PIN


def test_display(board):
    def lcd_write(_cpu, _access, address, _size, value, _user):
        (board.lcd_commands if address == LCD_CMD else board.lcd_pixels).append(value)

    for address in (LCD_CMD, LCD_DATA):
        board.cpu.hook_add(UC_HOOK_MEM_WRITE, lcd_write, begin=address, end=address)
    pixels = tuple(range(20))
    board.cpu.mem_write(0x20000080, struct.pack("<20H", *pixels))
    board.call("h1::board::writePixels(unsigned int, unsigned int, unsigned int, unsigned int, void const*)",
               5, 7, 10, 2, 0x20000080)
    assert board.lcd_commands == [0x2a, 0x2b, 0x2c]
    assert board.lcd_pixels == [0, 5, 0, 14, 0, 7, 0, 8] + list(pixels)


def test_flash(board):
    def program(word, value):
        return board.call("h1::cold::journalFlash::program(unsigned int, unsigned long)", word, value)

    def erase(page):
        return board.call("h1::cold::journalFlash::erase(unsigned int)", page)

    assert program(0, 0x1234ABCD) and board.read(JOURNAL) == 0x1234ABCD
    assert not program(0, 0x12345678) and not program(1024, 0)
    assert program(512, 0x23456789) and board.read(JOURNAL + 2048) == 0x23456789
    assert erase(0) and board.read(JOURNAL) == 0xFFFFFFFF
    assert board.read(JOURNAL + 2048) == 0x23456789 and not erase(2)
    assert board.read(FMC_CTL) == 1 << 7


def test_boot(board, recovery=False, muted=False):
    if recovery:
        board.load_saved_cycle((700 << 8) | 0xfb)
        board.expected_label = b"Resume saved cycle?\0"
    else:
        if muted:
            board.load_saved_cycle(0x60)  # Idle with sound off.
        board.expected_label = b"Set temp\0"
    board.capture_buzzer_notes()
    board.boot()
    clock = board.read(0xE000E014)
    exmc = (0x5011, 0x0e03) if clock == 47999 else (0x1011, 0x0201)
    assert (board.read(0xA0000000), board.read(0xA0000004)) == exmc
    assert board.flushes >= 2 and board.saw_screen_label, "Reset did not reach the expected screen"
    assert board.heater_clears and not board.heating and not board.read(FAN_COMPARE)
    assert board.read(PC + 0x0C) & MOTOR_DISABLED
    assert board.watchdog_started
    assert board.buzzer_notes == ([] if muted else BOOT_NOTES), board.buzzer_notes
    assert not board.read(BUZZ_TIMER) & 1 and not board.read(PA + 0x0c) & BUZZ_PIN


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--nm", default="arm-none-eabi-nm")
    args = parser.parse_args()
    outputs = Board(args.image, args.nm)
    test_outputs(outputs)
    test_step_timing(Board(args.image, args.nm))
    test_fan_qualification(Board(args.image, args.nm))
    test_manual_fan(Board(args.image, args.nm))
    test_warmup_fan(Board(args.image, args.nm))
    test_buzzer(Board(args.image, args.nm))
    test_diagnostic_vent_channels(Board(args.image, args.nm))
    test_open_lid_heater_diagnostic(Board(args.image, args.nm))
    test_flash(outputs)
    test_display(outputs)
    test_boot(Board(args.image, args.nm))
    test_boot(Board(args.image, args.nm), recovery=True)
    test_boot(Board(args.image, args.nm), muted=True)
    for fallback, reload in ((Board(args.image, args.nm, crystal=False), 47999),
                             (Board(args.image, args.nm, high_drive=False), 47999),
                             (Board(args.image, args.nm, pll=False), 7999)):
        test_boot(fallback)
        assert fallback.read(0xE000E014) == reload
    test_filament_profile_flow(Board(args.image, args.nm))
    Board(args.image, args.nm).start_control_scenario()
    Board(args.image, args.nm).start_control_scenario(fault="sensor")
    Board(args.image, args.nm).start_control_scenario(fault="over_temperature")
    print("ARM clocks, LCD, STEP, buzzer volume, journal, lid, fan PWM, isolated vent outputs, profile saving and heated touchscreen cycle passed.")


if __name__ == "__main__":
    main()

