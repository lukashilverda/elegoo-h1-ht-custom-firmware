#include "storage.hpp"
#include "board.hpp"

extern "C" const std::uint32_t _settings_start[];

namespace h1::cold::journalFlash {
namespace {
constexpr std::uint32_t key = 0x40022004;
constexpr std::uint32_t status = 0x4002200c;
constexpr std::uint32_t control = 0x40022010;
constexpr std::uint32_t address = 0x40022014;
constexpr std::uint32_t busy = 1u;
constexpr std::uint32_t lock = 1u << 7;
constexpr std::uint32_t errors = (1u << 2) | (1u << 4);
constexpr std::uint32_t clearFlags = errors | (1u << 5);

bool waitReady() {
    const auto start = board::reg(0xe0001004);
    const auto timeoutCycles = board::coreClockHz() / 10;
    while (board::reg(status) & busy) {
        if (board::reg(0xe0001004) - start >= timeoutCycles) return false;
    }
    return true;
}


bool unlock() {
    if (!waitReady()) return false;
    if (board::reg(control) & lock) {
        board::reg(key) = 0x45670123;
        board::reg(key) = 0xcdef89ab;
    }
    if (board::reg(control) & lock) return false;
    board::reg(status) = clearFlags;
    return true;
}

bool finish() {
    const bool ok = waitReady() && !(board::reg(status) & errors);
    board::reg(control) = lock;
    board::reg(status) = clearFlags;
    return ok;
}

std::uint32_t wordAddress(unsigned word) {
    return reinterpret_cast<std::uintptr_t>(_settings_start + word);
}
}

std::uint32_t read(unsigned word) {
    return board::reg(wordAddress(word));
}

bool program(unsigned word, std::uint32_t value) {
    if (word >= 2 * pageWords || read(word) != 0xffffffffu) return false;
    if (value == 0xffffffffu) return true;
    if (!unlock()) return false;
    board::reg(control) = 1u;
    board::reg(wordAddress(word)) = value;
    return finish() && read(word) == value;
}

bool erase(unsigned page) {
    if (page >= 2 || !unlock()) return false;
    const auto word = page * pageWords;
    board::reg(control) = 1u << 1;
    board::reg(address) = wordAddress(word);
    board::reg(control) = (1u << 1) | (1u << 6);
    if (!finish()) return false;
    for (unsigned i = 0; i < pageWords; ++i) {
        if (read(word + i) != 0xffffffffu) return false;
    }
    return true;
}

}  // namespace h1::cold::journalFlash

