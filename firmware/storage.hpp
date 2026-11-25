#pragma once

#include "cycle.hpp"

namespace h1::cold {

namespace journalFlash {
inline constexpr unsigned pageWords = 2048 / sizeof(std::uint32_t);
std::uint32_t read(unsigned word);
bool program(unsigned word, std::uint32_t value);
bool erase(unsigned page);
}

struct FaultHistory {
    static constexpr unsigned capacity = 30;
    static constexpr int noTemperature = -32768;
    std::array<std::uint32_t, capacity> entries{};
    unsigned size() const;
    void add(Notice code, int heaterTenths = noTemperature);
    Notice code(unsigned index) const;
    int heaterTenths(unsigned index) const;
};

class Journal {
public:
    SavedCycle load();
    const FaultHistory& faults() const { return faults_; }
    bool save(SavedCycle cycle, const FaultHistory& faults = {});
    bool canSaveWithoutErase() const;
    bool needsPreparation() const;
    bool prepareNextPage();

private:
    unsigned page_ = 0;
    unsigned fullAt_ = 0;
    unsigned next_ = 0;
    std::uint32_t sequence_ = 0;
    bool found_ = false;
    bool haveConfiguration_ = false;
    bool standbyErased_ = false;
    FaultHistory faults_{};
};

}  // namespace h1::cold
