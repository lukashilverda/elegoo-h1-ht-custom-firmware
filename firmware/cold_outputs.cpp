#include "cold_outputs.hpp"
#include "board.hpp"
#include "fan_warmup.hpp"
#include "hal/exhaust_vent.h"
#include "hal/fan.h"
#include "hal/heater.h"
#include "hal/inlet_vent.h"
#include "hal/lid_sensor.h"
#include "hal/stepper.h"
#include "hal/thermistor.h"
#include "hal/vent_bridge.h"

namespace h1::cold::hardware {
namespace {
constexpr std::uint32_t outputLeaseMs = 300;
FanWarmup fanWarmup;
volatile bool ready;
volatile bool restartMovement;
volatile bool requestedFan;
volatile unsigned manualDuty;
volatile bool lowerWhileWarming;
volatile bool requestedMotor;
volatile bool detectLid;
volatile bool diagnosticHeat;
volatile VentTarget requestedVents;
volatile unsigned requestedVentMask;
volatile unsigned reached;
volatile bool heating;
volatile std::uint32_t refreshedAt;
volatile unsigned faults;
constexpr std::uint32_t airflowTimeoutMs = 350;
constexpr unsigned requiredFanPulses = 3;
volatile std::uint32_t heatForMs;
volatile bool fanPulsePending;
volatile unsigned fanPulses;
volatile bool lidOpened;
volatile std::uint32_t firstFanPulse;
volatile std::uint32_t lastFanPulse;

std::uint32_t maskInterrupts() {
    std::uint32_t primask;
    asm volatile("mrs %0, primask\ncpsid i" : "=r"(primask) :: "memory");
    return primask;
}

void restoreInterrupts(std::uint32_t primask) {
    asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}
VentDrive exhaust;
VentDrive inlet;

hal::VentDirection direction(VentTarget target) {
    if (target == VentTarget::open) return hal::VentDirection::towardOpen;
    if (target == VentTarget::closed) return hal::VentDirection::towardClosed;
    return hal::VentDirection::stop;
}

void applyOutputs(std::uint32_t now, bool rawLidOpen) {
    if (rawLidOpen) {
        lidOpened = true;
        if (!diagnosticHeat) heatForMs = 0;
    }
    const bool fresh = now - refreshedAt < outputLeaseMs;
    const bool allowed = fresh && (!detectLid || !lidOpened);
    const bool fan = requestedFan && allowed;
    const bool airflow = fan && fanPulses >= requiredFanPulses &&
                         now - lastFanPulse < airflowTimeoutMs;
    const bool thermalSamplesFresh = now - refreshedAt < heatForMs;
    heating = thermalSamplesFresh && airflow && (!lidOpened || diagnosticHeat) &&
              faults == 0 && fanWarmup.allowsHeat();
    hal::heater::setEnabled(heating);
    const unsigned duty = manualDuty ? manualDuty : fanWarmup.duty();
    hal::fan::setDutyPercent(fan ? duty : 0);
    hal::stepper::setRotating(requestedMotor && airflow);
}

void tickVents(std::uint32_t now, VentTarget request) {
    const bool bridgeAvailable = hal::ventBridge::statusHigh();
    if (restartMovement) {
        exhaust.update(VentTarget::hold, false, now);
        inlet.update(VentTarget::hold, false, now);
        restartMovement = false;
    }
    if (!bridgeAvailable) {
        faults = request == VentTarget::hold ? 0u : requestedVentMask;
        request = VentTarget::hold;
    }
    const auto exhaustRequest = requestedVentMask & 1u ? request : VentTarget::hold;
    const auto inletRequest = requestedVentMask & 2u ? request : VentTarget::hold;
    const auto exhaustDrive = exhaust.update(exhaustRequest, hal::exhaustVent::feedbackHigh(), now);
    const auto inletDrive = inlet.update(inletRequest, hal::inletVent::feedbackHigh(), now);
    reached = (exhaust.reached() ? 1u : 0u) | (inlet.reached() ? 2u : 0u);
    if (bridgeAvailable) faults = (exhaust.fault() ? 1u : 0u) | (inlet.fault() ? 2u : 0u);
    hal::exhaustVent::drive(direction(exhaustDrive));
    hal::inletVent::drive(direction(inletDrive));
}
}

void initialize() {
    hal::lidSensor::initialize();
    hal::stepper::initialize();
    hal::ventBridge::initialize();
    hal::exhaustVent::initialize();
    hal::inletVent::initialize();
    hal::ventBridge::setEnabled(true);
    hal::fan::initialize();
    hal::thermistor::initialize();
    ready = true;
}

void publish(Outputs outputs, std::uint32_t now, bool restart, std::uint32_t heatWindowMs,
             bool useLowerDuty) {
    const auto primask = maskInterrupts();
    if (restart || !outputs.fan || !requestedFan) {
        fanPulses = 0;
        fanPulsePending = false;
        fanWarmup.update(now, false, false, false, false);
        hal::fan::takePulse();
    }
    refreshedAt = now;
    requestedFan = outputs.fan;
    manualDuty = outputs.manualDuty;
    lowerWhileWarming = useLowerDuty;
    requestedMotor = outputs.motor;
    requestedVents = outputs.vents;
    requestedVentMask = outputs.ventMask;
    detectLid = outputs.lidDetection;
    // Only the bounded heater self-test may request heating with an open lid.
    diagnosticHeat = outputs.diagnosticHeat && !detectLid;
    heatForMs = lidOpened && !diagnosticHeat ? 0 : heatWindowMs;
    if (restart) {
        // SysTick owns the drive state. Clear the old report before the next sample.
        faults = 0;
        reached = 0;
        restartMovement = true;
    }
    applyOutputs(now, hal::lidSensor::levelHigh());
    restoreInterrupts(primask);
}

bool takeFanPulse() {
    const auto primask = maskInterrupts();
    const bool pulse = fanPulsePending;
    fanPulsePending = false;
    restoreInterrupts(primask);
    return pulse;
}

bool takeLidOpen() {
    const auto primask = maskInterrupts();
    const bool rawOpen = hal::lidSensor::levelHigh();
    const bool event = rawOpen || lidOpened;
    if (event) {
        if (!diagnosticHeat) heatForMs = 0;
        // Keep outputs stopped until the application has handled the opening.
        if (detectLid) {
            requestedFan = requestedMotor = false;
            if (requestedVents == VentTarget::open) requestedVents = VentTarget::hold;
        }
        applyOutputs(board::milliseconds(), rawOpen);
    }
    lidOpened = rawOpen;
    restoreInterrupts(primask);
    return event;
}

bool airflowReady() {
    return fanPulses >= requiredFanPulses && fanWarmup.allowsHeat();
}

bool heaterActive() { return heating; }

unsigned ventsReached() { return reached; }

void inhibitHeat() {
    const auto primask = maskInterrupts();
    heatForMs = 0;
    heating = false;
    hal::heater::setEnabled(false);
    restoreInterrupts(primask);
}

unsigned ventFaults() {
    return faults;
}

void tick(std::uint32_t now) {
    if (!ready) return;
    const bool fresh = now - refreshedAt < outputLeaseMs;
    const bool rawLidOpen = hal::lidSensor::levelHigh();
    const bool lidInterlock = detectLid && (rawLidOpen || lidOpened);
    const bool pulse = hal::fan::takePulse();
    const bool qualifying = fanPulses < requiredFanPulses;
    const bool expired = now - lastFanPulse >= airflowTimeoutMs ||
                         (qualifying && now - firstFanPulse >= airflowTimeoutMs);
    if (!fresh || lidInterlock || !requestedFan || expired) {
        fanPulses = 0;
        fanPulsePending = false;
    }
    if (pulse && fresh && !lidInterlock && requestedFan) {
        if (fanPulses == 0) firstFanPulse = now;
        lastFanPulse = now;
        if (fanPulses < requiredFanPulses) fanPulses = fanPulses + 1;
        fanPulsePending = fanPulses >= requiredFanPulses;
    }
    fanWarmup.update(now, fresh && requestedFan && !lidInterlock,
                     lowerWhileWarming && !rawLidOpen && !lidOpened, pulse,
                     fanPulses >= requiredFanPulses);
    const auto vents = requestedVents;
    const bool canMoveVents = fresh && (!lidInterlock || vents == VentTarget::closed);
    tickVents(now, canMoveVents ? vents : VentTarget::hold);
    applyOutputs(now, rawLidOpen);
}

void emergencyOff() {
    heating = false;
    hal::heater::setEnabled(false);
    if (!ready) return;
    ready = false;
    hal::exhaustVent::drive(hal::VentDirection::stop);
    hal::inletVent::drive(hal::VentDirection::stop);
    hal::ventBridge::setEnabled(false);
    hal::fan::setDutyPercent(0);
    hal::stepper::emergencyStop();
}

}  // namespace h1::cold::hardware

