#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>
#include <time.h>

#define APP_VERSION_BASE "1"
#define UI_LANGUAGE "EN"
constexpr const char* WEBSITE_URL = "https://project.example.com";

// Enable one or more timezone sets to be compiled into the firmware. You can combine multiple regions if needed.
// ⚠️ Note: Including larger sets will increase flash usage and RAM usage in WebUI dropdowns.
// #define TIMEZONE_AFRICA
// #define TIMEZONE_AMERICA
// #define TIMEZONE_ANTARCTICA
// #define TIMEZONE_ARCTIC
// #define TIMEZONE_ASIA
// #define TIMEZONE_ATLANTIC
// #define TIMEZONE_AUSTRALIA
#define TIMEZONE_EUROPE
// #define TIMEZONE_INDIAN
// #define TIMEZONE_PACIFIC

// --------------------------
// Debug configuration
// --------------------------

// Globalny przełącznik: włącza wszystkie debugi naraz
#define DEBUG_ENABLED

#ifdef DEBUG_ENABLED
  //#define DEBUG_WIFI_CONNECTOR
  //#define DEBUG_HUMIDIFIER_CONTROLLER
  #define DEBUG_CLUSTER_MANAGER
#endif

#ifdef DEBUG_ENABLED
  #define SERIAL_PRINT(...)    Serial.print(__VA_ARGS__)
  #define SERIAL_PRINTLN(...)  Serial.println(__VA_ARGS__)
  #define SERIAL_PRINTF(...)   Serial.printf(__VA_ARGS__)
#else
  #define SERIAL_PRINT(...)
  #define SERIAL_PRINTLN(...)
  #define SERIAL_PRINTF(...)
#endif

// --------------------------
// Funkcja: dni od 1 stycznia 2025
// --------------------------
inline int BuildDaysSince2025()
{
    struct tm buildTm = {};
    // __DATE__ jest w formacie "Mmm dd yyyy"
    strptime(__DATE__, "%b %d %Y", &buildTm);

    time_t buildTime = mktime(&buildTm);

    // początek 2025
    struct tm epochTm = {};
    epochTm.tm_year = 2025 - 1900;
    epochTm.tm_mon = 0;    // styczeń
    epochTm.tm_mday = 1;
    time_t epoch = mktime(&epochTm);

    if (buildTime < epoch) return 0;

    // różnica w dniach
    return (buildTime - epoch) / 86400;
}

inline String AppVersion()
{
    int days = BuildDaysSince2025();
    return String(APP_VERSION_BASE) + "." + String(days) + "." + UI_LANGUAGE;
}

#endif // APP_CONFIG_H
