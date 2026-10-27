#pragma once
#include <cstring>
namespace setup_portal {
struct Timezone { const char* label; const char* iana; const char* posix; };
static const Timezone timezones[] = {
    {"New York (US Eastern)", "America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago (US Central)", "America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"Denver (US Mountain)", "America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Phoenix (Arizona)", "America/Phoenix", "MST7"},
    {"Los Angeles (US Pacific)", "America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Anchorage (Alaska)", "America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Honolulu (Hawaii)", "Pacific/Honolulu", "HST10"},
    {"London / Dublin", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Berlin / Paris / Madrid", "Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Athens / Helsinki", "Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow", "Europe/Moscow", "MSK-3"},
    {"Dubai", "Asia/Dubai", "GST-4"},
    {"India (Kolkata)", "Asia/Kolkata", "IST-5:30"},
    {"Bangkok / Jakarta", "Asia/Bangkok", "ICT-7"},
    {"China / Singapore / HK", "Asia/Shanghai", "CST-8"},
    {"Tokyo / Seoul", "Asia/Tokyo", "JST-9"},
    {"Sydney / Melbourne", "Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Brisbane", "Australia/Brisbane", "AEST-10"},
    {"Auckland", "Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Sao Paulo", "America/Sao_Paulo", "BRT3"},
    {"Buenos Aires", "America/Argentina/Buenos_Aires", "ART3"},
    {"Mexico City", "America/Mexico_City", "CST6"},
    {"Johannesburg", "Africa/Johannesburg", "SAST-2"},
};
inline bool knownTimezone(const char* value) {
    for (const auto& zone : timezones) if (!strcmp(zone.posix, value)) return true;
    return false;
}
}
