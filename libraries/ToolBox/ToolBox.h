#ifndef ToolBox_h
#define ToolBox_h

#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <stdlib.h>
#include <vector>
#include <string>
#include <AppConfig.h>          // Must be before TimezoneStrings
#include <TimezoneStrings.h>



class ToolBox
{
public:
    /// @brief Linearly interpolates between values a and b by factor t.
    /// @param a Start value
    /// @param b End value
    /// @param t Interpolation factor [0..1]
    /// @return Interpolated value
    static float Lerp(float a, float b, float t)
    {
        return a * (1.0f - t) + (b * t);
    }

    /// @brief Checks if value x is within the closed interval [a, b].
    /// @param x Value to check
    /// @param a Lower bound
    /// @param b Upper bound
    /// @return True if x is within [a, b], otherwise false
    static bool IsConstrained(float x, float a, float b)
    {
        return x >= a && x <= b;
    }

    /// @brief Scans the I2C bus for connected devices.
    /// @note Call Wire.begin(SDA, SCL) before using this function.
    /// @note If running in a multi-threaded context (e.g. FreeRTOS on ESP32), lock a mutex around I2C access to prevent race conditions.
    /// @return A vector containing the 7-bit addresses of all detected I2C devices
    static std::vector<uint8_t> ScanI2C()
    {
        std::vector<uint8_t> foundDevices;

        for (uint8_t address = 1; address < 127; address++)
        {
            Wire.beginTransmission(address);
            uint8_t error = Wire.endTransmission();

            if (error == 0)
                foundDevices.push_back(address);
        }

        return foundDevices;
    }

    /// @brief Checks if an I2C device responds at the given address.
    /// @param address 7-bit I2C address to check.
    /// @return True if the device acknowledges, otherwise false.
    static bool CheckI2CDevice(uint8_t address)
    {
        Wire.beginTransmission(address);
        return (Wire.endTransmission() == 0);
    }

    /// @brief Checks if ESP32 is connected to WiFi in Station mode.
    static bool IsStationConnected()
    {
        return (WiFi.getMode() & WIFI_STA) && (WiFi.status() == WL_CONNECTED);
    }

    /// @brief Checks if Access Point (AP) mode is enabled.
    static bool IsAccessPointMode()
    {
        return WiFi.getMode() & WIFI_AP;
    }

    /// @brief Checks if any network interface is active (STA connected or AP enabled).
    static bool IsNetworkAvailable()
    {
        return IsStationConnected() || IsAccessPointMode();
    }

    /// @brief Ustawia strefę czasową przy pomocy POSIX TZ stringa.
    /// @param tzString np. "CET-1CEST,M3.5.0/2,M10.5.0/3"
    static void ApplyTimezone(const char* tzString)
    {
        setenv("TZ", tzString, 1);
        tzset();
    }

    /// @brief Zwraca listę nazw lokalizacji dostępnych w systemie (np. do dropdown).
    static std::vector<std::string> GetTimezoneList()
    {
        std::vector<std::string> list;
        list.reserve(timezoneStringsCount);

        for (int i = 0; i < timezoneStringsCount; i++)
        {
            // odczytanie elementu z PROGMEM
            TimezoneEntry entry;
            memcpy_P(&entry, &timezoneStrings[i], sizeof(TimezoneEntry));

            list.push_back(std::string(entry.name));
        }

        return list;
    }

    /// @brief Zwraca indeks lokalizacji w tablicy timezoneStrings.
    /// @param locationName Nazwa lokalizacji np. "Europe/Warsaw".
    /// @return Indeks w tablicy lub -1 jeśli nie znaleziono.
    static int FindTimezoneIndexByName(const char* locationName)
    {
        for (int i = 0; i < timezoneStringsCount; i++)
        {
            TimezoneEntry entry;
            memcpy_P(&entry, &timezoneStrings[i], sizeof(TimezoneEntry));

            if (strcmp(entry.name, locationName) == 0)
                return i;
        }
        return -1;
    }

    /// @brief Zwraca POSIX TZ string dla podanej nazwy lokalizacji.
    /// @param locationName np. "Europe/Warsaw".
    /// @return std::string z TZ (np. "CET-1CEST,M3.5.0/2,M10.5.0/3") lub pusty string, jeśli nie znaleziono.
    static std::string GetTimezoneStringByName(const char* locationName)
    {
        for (int i = 0; i < timezoneStringsCount; i++)
        {
            TimezoneEntry entry;
            memcpy_P(&entry, &timezoneStrings[i], sizeof(TimezoneEntry));

            if (strcmp(entry.name, locationName) == 0)
            {
                return std::string(entry.tz);
            }
        }
        return "";
    }

    /// @brief Zaokrągla wartość float do najbliższego wielokrotności kroku (np. 0.5).
    /// @param value Wartość wejściowa
    /// @param step Rozdzielczość (np. 0.5, 0.1, 5.0)
    /// @return Wartość zaokrąglona do kroku
    static float RoundToStep(float value, float step)
    {
        if (step <= 0.0f) return value;
        return roundf(value / step) * step;
    }
};

#endif