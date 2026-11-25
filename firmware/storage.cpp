#include "storage.hpp"

namespace h1::cold {

namespace {
using namespace journalFlash;
constexpr std::uint32_t oldSignature = 0x48435431;
constexpr std::uint32_t extendedSignature = 0x48435432;
constexpr std::uint32_t configSignature = 0x48435433;
constexpr std::uint32_t checkpointSignature = 0x48434333;
constexpr std::uint32_t erased = 0xffffffffu;
constexpr unsigned checkpointWords = 4;
constexpr unsigned fullWords = 80;
constexpr unsigned payloadAt = 5;
constexpr unsigned configWords = 2 + savedProfileCount * 5 + FaultHistory::capacity;
using Payload = std::array<std::uint32_t, configWords>;
static_assert(payloadAt + configWords <= fullWords && fullWords % checkpointWords == 0);

std::uint32_t updateCrc(std::uint32_t crc, std::uint32_t word) {
    crc ^= word;
    for (unsigned i = 0; i < 32; ++i) {
        crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc;
}

std::uint32_t stateCrc(std::uint32_t sequence, unsigned seconds, unsigned total) {
    return updateCrc(updateCrc(updateCrc(erased, sequence), seconds), total);
}

std::uint32_t fullCrc(std::uint32_t sequence, unsigned seconds, unsigned total, const Payload& data) {
    auto crc = stateCrc(sequence, seconds, total);
    for (auto word : data) crc = updateCrc(crc, word);
    return ~crc;
}

Payload pack(const Settings& s, const FaultHistory& faults) {
    Payload data{};
    data[0] = s.profile | (s.fahrenheit ? 1u << 8 : 0u) | (s.rotation ? 1u << 9 : 0u) |
              (s.lidDetection ? 1u << 10 : 0u) | (s.highTemperature ? 1u << 11 : 0u) |
              (s.soundMask << 12) | (s.volume << 16) | (s.displayTimeout << 24);
    data[1] = s.customTemperature | ((s.customMinutes / customTimeStepMinutes) << 8) |
              (s.customHumidity << 16);
    unsigned at = 2;
    for (const auto& item : s.filaments) {
        for (unsigned i = 0; i < profileNameLength; i += 4) {
            std::uint32_t word = 0;
            for (unsigned j = 0; j < 4; ++j) word |= std::uint32_t(std::uint8_t(item.name[i + j])) << (j * 8);
            data[at++] = word;
        }
        data[at++] = item.temperature | ((item.minutes / customTimeStepMinutes) << 8) | (item.humidity << 16);
    }
    for (auto entry : faults.entries) data[at++] = entry;
    return data;
}

Settings unpack(const Payload& data, FaultHistory& faults) {
    Settings s;
    s.profile = data[0] & 0xff;
    s.fahrenheit = data[0] & (1u << 8);
    s.rotation = data[0] & (1u << 9);
    s.lidDetection = data[0] & (1u << 10);
    s.highTemperature = data[0] & (1u << 11);
    s.soundMask = (data[0] >> 12) & 0xf;
    s.volume = (data[0] >> 16) & 0xff;
    s.displayTimeout = data[0] >> 24;
    s.customTemperature = data[1] & 0xff;
    s.customMinutes = ((data[1] >> 8) & 0xff) * customTimeStepMinutes;
    s.customHumidity = (data[1] >> 16) & 0xff;
    unsigned at = 2;
    for (auto& item : s.filaments) {
        for (unsigned i = 0; i < profileNameLength; i += 4) {
            const auto word = data[at++];
            for (unsigned j = 0; j < 4; ++j) item.name[i + j] = static_cast<char>((word >> (j * 8)) & 0xff);
        }
        const auto program = data[at++];
        item.temperature = program & 0xff;
        item.minutes = ((program >> 8) & 0xff) * customTimeStepMinutes;
        item.humidity = (program >> 16) & 0xff;
    }
    for (auto& entry : faults.entries) entry = data[at++];
    return s;
}

bool validTimes(const SavedCycle& cycle) {
    if (!cycle.seconds) return !cycle.totalSeconds;
    const auto base = cycle.settings.durationSeconds();
    return cycle.seconds <= cycle.totalSeconds && cycle.totalSeconds >= base &&
           cycle.totalSeconds <= maxTimedSeconds &&
           (!cycle.settings.humidityTarget() || cycle.totalSeconds == base);
}

bool readLegacy(unsigned word, std::uint32_t header, std::uint32_t sequence, SavedCycle& cycle) {
    const auto payload = read(word + 2);
    auto crc = updateCrc(updateCrc(erased, sequence), payload);
    if (header == extendedSignature) crc = updateCrc(crc, header);
    if (~crc != read(word + 3) || (payload & 0xf) > customProfile) return false;

    auto& s = cycle.settings;
    s.profile = payload & 0xf;
    s.fahrenheit = payload & 0x10;
    s.rotation = payload & 0x20;
    s.lidDetection = payload & 0x40;
    // The previous switch muted startup/session sounds while faults still sounded.
    s.soundMask = payload & 0x80 ? allSounds : soundError;
    const auto custom = (payload >> 24) | (header == extendedSignature ? 0x100u : 0u);
    if (custom) {
        constexpr unsigned timeSlots = customTimeMaxMinutes / customTimeStepMinutes;
        const auto index = custom - 1;
        s.customTemperature = customTemperatureMin + (index / timeSlots) * customTemperatureStep;
        s.customMinutes = (index % timeSlots + 1) * customTimeStepMinutes;
    }
    if (s.customTemperature > maxDryingTemperature) return false;
    cycle.seconds = (payload >> 8) & 0xffffu;
    if (cycle.seconds && s.custom() && s.customTemperature > regularTemperatureLimit) cycle.seconds = 0;
    cycle.totalSeconds = cycle.seconds ? s.durationSeconds() : 0;
    return s.valid() && validTimes(cycle);
}

bool blank(unsigned begin, unsigned end) {
    for (unsigned i = begin; i < end; ++i) {
        if (read(i) != erased) return false;
    }
    return true;
}

unsigned findSpace(unsigned word, unsigned end, unsigned count) {
    while (word + count <= end) {
        if (blank(word, word + count)) return word;
        word += checkpointWords;
    }
    return end;
}
}

unsigned FaultHistory::size() const {
    unsigned n = 0;
    while (n < capacity && entries[n]) ++n;
    return n;
}

void FaultHistory::add(Notice notice, int temperature) {
    for (unsigned i = capacity - 1; i > 0; --i) entries[i] = entries[i - 1];
    entries[0] = (std::uint32_t(std::uint16_t(temperature)) << 8) | static_cast<unsigned>(notice);
}

Notice FaultHistory::code(unsigned index) const {
    return static_cast<Notice>(entries[index] & 0xff);
}

int FaultHistory::heaterTenths(unsigned index) const {
    const auto raw = static_cast<int>((entries[index] >> 8) & 0xffff);
    return raw > 32767 ? raw - 65536 : raw;
}

SavedCycle Journal::load() {
    *this = {};
    SavedCycle result{};
    for (unsigned page = 0; page < 2; ++page) {
        const auto end = (page + 1) * pageWords;
        bool config = false;
        std::uint32_t configSequence = 0;
        unsigned configWord = 0;
        unsigned used = page * pageWords;
        for (unsigned word = used; word < end; word += checkpointWords) {
            if (!blank(word, word + checkpointWords)) used = word + checkpointWords;
            const auto header = read(word);
            if (header == checkpointSignature && config) {
                if (page != page_ || configSequence != sequence_ || configWord != fullAt_) continue;
                auto checkpoint = result;
                checkpoint.seconds = read(word + 1);
                checkpoint.totalSeconds = read(word + 2);
                if (validTimes(checkpoint) &&
                    read(word + 3) == ~stateCrc(configSequence, checkpoint.seconds, checkpoint.totalSeconds)) {
                    result = checkpoint;
                }
                continue;
            }
            if (header != oldSignature && header != extendedSignature && header != configSignature) continue;
            const auto sequence = read(word + 1);
            const auto difference = sequence - sequence_;
            SavedCycle cycle{};
            FaultHistory events{};
            if (header == configSignature) {
                if (word + fullWords > end) continue;
                Payload data{};
                for (unsigned i = 0; i < data.size(); ++i) data[i] = read(word + payloadAt + i);
                cycle.seconds = read(word + 2);
                cycle.totalSeconds = read(word + 3);
                if (read(word + 4) != fullCrc(sequence, cycle.seconds, cycle.totalSeconds, data)) continue;
                cycle.settings = unpack(data, events);
                if (!cycle.settings.valid() || !validTimes(cycle)) continue;
                config = true;
                configSequence = sequence;
                configWord = word;
                used = word + fullWords;
            } else if (!readLegacy(word, header, sequence, cycle)) {
                continue;
            }

            if (!found_ || (difference > 0 && difference < 0x80000000u)) {
                page_ = page;
                fullAt_ = word;
                sequence_ = sequence;
                found_ = true;
                haveConfiguration_ = header == configSignature;
                result = cycle;
                faults_ = events;
            }
            if (header == configSignature) word += fullWords - checkpointWords;
        }
        if (page == page_) next_ = used;
    }
    return result;
}

bool Journal::save(SavedCycle cycle, const FaultHistory& faults) {
    if (!cycle.settings.valid()) return false;
    if (cycle.seconds && !cycle.totalSeconds) cycle.totalSeconds = cycle.settings.durationSeconds();
    if (!validTimes(cycle)) return false;
    const auto data = pack(cycle.settings, faults);
    bool same = haveConfiguration_;
    for (unsigned i = 0; same && i < data.size(); ++i) same = read(fullAt_ + payloadAt + i) == data[i];

    const auto end = (page_ + 1) * pageWords;
    auto count = same ? checkpointWords : fullWords;
    auto word = findSpace(next_, end, count);
    if (word == end) {
        const auto other = 1u - page_;
        word = other * pageWords;
        if (!standbyErased_ && !blank(word, word + pageWords) && !erase(other)) return false;
        standbyErased_ = false;
        count = fullWords;
        same = false;
    }

    bool committed;
    if (same) {
        committed = program(word + 1, cycle.seconds) && program(word + 2, cycle.totalSeconds) &&
                    program(word + 3, ~stateCrc(sequence_, cycle.seconds, cycle.totalSeconds)) &&
                    program(word, checkpointSignature);
    } else {
        const auto sequence = sequence_ + 1;
        for (unsigned i = 0; i < data.size(); ++i) {
            if (!program(word + payloadAt + i, data[i])) return false;
        }
        committed = program(word + 1, sequence) && program(word + 2, cycle.seconds) &&
                    program(word + 3, cycle.totalSeconds) &&
                    program(word + 4, fullCrc(sequence, cycle.seconds, cycle.totalSeconds, data)) &&
                    program(word, configSignature);
        if (committed) {
            fullAt_ = word;
            sequence_ = sequence;
            haveConfiguration_ = true;
            faults_ = faults;
        }
    }
    if (!committed) return false;
    page_ = word / pageWords;
    next_ = word + count;
    found_ = true;
    return true;
}

bool Journal::canSaveWithoutErase() const {
    if (findSpace(next_, (page_ + 1) * pageWords, fullWords) < (page_ + 1) * pageWords) return true;
    const auto standby = (1u - page_) * pageWords;
    return standbyErased_ || blank(standby, standby + pageWords);
}

bool Journal::needsPreparation() const {
    return found_ && !standbyErased_ && (page_ + 1) * pageWords - next_ < fullWords + 8 * checkpointWords;
}

bool Journal::prepareNextPage() {
    if (!found_) return true;
    const auto other = 1u - page_;
    const auto start = other * pageWords;
    standbyErased_ = blank(start, start + pageWords) || erase(other);
    return standbyErased_;
}

}  // namespace h1::cold
