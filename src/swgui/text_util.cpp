#include "text_util.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <system_error>

namespace swgui {

std::string to_utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path from_utf8(const std::string& text) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string file_date(const std::filesystem::path& path) {
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (error) return {};
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(stamp);
    const std::time_t time = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M", &local);
    return buffer;
}

std::string fold(const std::string& text) {
    static const std::map<std::string, char> accents = {
        {"à", 'a'}, {"â", 'a'}, {"ä", 'a'}, {"á", 'a'}, {"é", 'e'}, {"è", 'e'},
        {"ê", 'e'}, {"ë", 'e'}, {"î", 'i'}, {"ï", 'i'}, {"í", 'i'}, {"ô", 'o'},
        {"ö", 'o'}, {"ó", 'o'}, {"ù", 'u'}, {"û", 'u'}, {"ü", 'u'}, {"ú", 'u'},
        {"ç", 'c'}, {"ñ", 'n'}, {"À", 'a'}, {"Â", 'a'}, {"É", 'e'}, {"È", 'e'},
        {"Ê", 'e'}, {"Î", 'i'}, {"Ô", 'o'}, {"Ù", 'u'}, {"Û", 'u'}, {"Ç", 'c'}};
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            result += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            ++i;
            continue;
        }
        const std::size_t length = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        const auto it = accents.find(text.substr(i, length));
        if (it != accents.end()) result += it->second;
        else result += text.substr(i, length);
        i += length;
    }
    return result;
}

std::string group_digits(long long value) {
    const bool negative = value < 0;
    const std::string digits =
        std::to_string(negative ? -static_cast<unsigned long long>(value)
                                : static_cast<unsigned long long>(value));
    std::string result;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) result += ' ';
        result += digits[i];
    }
    return negative ? "-" + result : result;
}

}  // namespace swgui
