#ifndef SharedResourceManager_h
#define SharedResourceManager_h

#include <stdint.h>
#include <mutex>
#include <vector>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <CircularBuffer.hpp>
#include <StateMachineRTOS.h>
#include <ToolBox.h>

#define MAX_AIR_LOGS 300
#define MAX_AIR_LOGS_RECENT 10
#define MAX_DEVICE_LOGS 200



/// @brief Manages shared resources (variables, arrays) with mutex-protected accessors 
/// to prevent data races in concurrent processes.
class SharedResourceManager : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        Sharing
	};

    // Enum for predefined codes representing various states for Light, Heatpad, or other devices
    enum LogCode
    {
        INIT = 0,            // Initialization state: Indicates that the device has been initialized.
        ON = 1,              // ON state: Indicates that the device (light/heatpad) is currently turned on.
        OFF = 2,             // OFF state: Indicates that the device is currently turned off.

        // Error states: Indicate specific error conditions for monitoring and debugging
        ERROR_0 = 100,                   // Error state, case 0
        ERROR_1 = 101,                   // Error state, case 1
        ERROR_2 = 102,                   // Error state, case 2
        ERROR_3 = 103,                   // Error state, case 3

        // Debug states: For displaying info about device state, and provide more information in "info" field
        DIAGNOSTICS_0 = 200,
        DIAGNOSTICS_1 = 201,
        DIAGNOSTICS_2 = 202,
        DIAGNOSTICS_3 = 203,
    };

    struct AirLogEntry
    {
        std::time_t unixTime;
        float temperature;
        float humidity;
        float co2;

        AirLogEntry() : unixTime(0), temperature(0.0f), humidity(0.0f), co2(0.0f) {}
        AirLogEntry(std::time_t t, float temp, float hum, float c) : unixTime(t), temperature(temp), humidity(hum), co2(c) {}
    };

    struct LightLogEntry
    {
        std::time_t unixTime;
        LogCode code;
        std::string info;

        LightLogEntry() 
            : unixTime(0), code(LogCode::INIT), info("") {}

        LightLogEntry(std::time_t t, LogCode c, const std::string& i = "") 
            : unixTime(t), code(c), info(i) {}
    };

    struct HeatpadLogEntry
    {
        std::time_t unixTime;
        LogCode code;
        std::string info;

        HeatpadLogEntry() 
            : unixTime(0), code(LogCode::INIT), info("") {}

        HeatpadLogEntry(std::time_t t, LogCode c, const std::string& i = "") 
            : unixTime(t), code(c), info(i) {}
    };

    struct FanLogEntry
    {
        std::time_t unixTime;
        LogCode code;
        std::string info;

        FanLogEntry() 
            : unixTime(0), code(LogCode::INIT), info("") {}

        FanLogEntry(std::time_t t, LogCode c, const std::string& i = "") 
            : unixTime(t), code(c), info(i) {}
    };

    struct HumidifierLogEntry
    {
        std::time_t unixTime;
        LogCode code;
        std::string info;

        HumidifierLogEntry() 
            : unixTime(0), code(LogCode::INIT), info("") {}

        HumidifierLogEntry(std::time_t t, LogCode c, const std::string& i = "") 
            : unixTime(t), code(c), info(i) {}
    };

    struct FCSettingHour
    {
        int hour;
        float heatpadTemp_ON;                   // The lower threshold for temperature regulation; when the temperature drops below this value, the heatpad turns ON.
        float heatpadTemp_OFF;                   // The upper threshold for temperature regulation; when the temperature reaches or exceeds this value, the heatpad turns OFF.
        int fanMinutes_ON;                              // The duration (in minutes) that the fan will stay ON 
        int fanMinutes_OFF;                             // The duration (in minutes) that the fan will stay OFF
        int humidifierMinutes_ON;                           // The duration (in minutes) that the humidifier will stay ON
        int humidifierMinutes_OFF;                          // The duration (in minutes) that the humidifier will stay OFF
        int targetHumidity;                                     // target humidity [%]

        void Default()
        {
            hour = 0;
            heatpadTemp_ON = 22.0;
            heatpadTemp_OFF = 26.0;
            fanMinutes_ON = 1;
            fanMinutes_OFF = 60;
            humidifierMinutes_ON = 2;
            humidifierMinutes_OFF = 60;
            targetHumidity = 90;
        }
    };

    struct FCSettingDay
    {
        int lightHour_ON;                 // The hour (in 24-hour format) when the light will turn ON, used for the light cycling procedure.
        int lightHour_OFF;                // The hour (in 24-hour format) when the light will turn OFF, used for the light cycling procedure.   
        std::vector<FCSettingHour> hours;

        void Default()
        {
            lightHour_ON = 6;
            lightHour_OFF = 20;

            hours.clear();
            FCSettingHour hour;
            hour.Default();
            hours.push_back(hour);
        }
    };

    struct FCSettingCycle
    {
        static constexpr const char* PREF_NAMESPACE = "pzg";
        static constexpr const char* PREF_KEY = "cycle";
        std::vector<FCSettingDay> days;
        std::time_t cycleStartTime;                 // When the cycle started, so it can be resumed after boot



        void Default()
        {
            days.clear();
            for (int i = 0; i < 1; ++i)
            {
                FCSettingDay day;
                day.Default();
                days.push_back(day);
            }

            std::tm startTm = {};
            startTm.tm_year = 2025 - 1900; // Year since 1900
            startTm.tm_mon = 0;            // January (0-based)
            startTm.tm_mday = 1;           // 1st
            startTm.tm_hour = 0;
            startTm.tm_min = 0;
            startTm.tm_sec = 0;
            cycleStartTime = std::mktime(&startTm);
        }

        bool Validate()
        {
            // At least 1 day, at most 32
            if (days.empty() || days.size() > 32)
                return false;

            // cycleStartTime must be reasonable (not zero unless intended)
            if (cycleStartTime <= 0)
                return false;

            for (const auto& day : days)
            {
                // ---- Validate light hours ----
                if (day.lightHour_ON < 0 || day.lightHour_ON > 23) return false;
                if (day.lightHour_OFF < 0 || day.lightHour_OFF > 23) return false;
                if (day.lightHour_ON >= day.lightHour_OFF) return false;

                // ---- Validate hours list ----
                if (day.hours.empty() || day.hours.size() > 4)
                    return false;

                if (day.hours[0].hour != 0)
                    return false;

                // Strictly increasing hour order
                for (size_t i = 1; i < day.hours.size(); ++i)
                {
                    if (day.hours[i].hour <= day.hours[i - 1].hour)
                        return false;
                }

                // Validate each FCSettingHour
                for (const auto& hour : day.hours)
                {
                    if (hour.hour < 0 || hour.hour > 23)
                        return false;

                    if (hour.heatpadTemp_ON < 1 || hour.heatpadTemp_ON > 60)
                        return false;
                    if (hour.heatpadTemp_OFF < 1 || hour.heatpadTemp_OFF > 60)
                        return false;
                    if (hour.heatpadTemp_ON >= hour.heatpadTemp_OFF)
                        return false;

                    if (hour.fanMinutes_ON < 1 || hour.fanMinutes_ON > 60)
                        return false;
                    if (hour.fanMinutes_OFF < 30 || hour.fanMinutes_OFF > 1200)
                        return false;

                    if (hour.humidifierMinutes_ON < 1 || hour.humidifierMinutes_ON > 60)
                        return false;
                    if (hour.humidifierMinutes_OFF < 30 || hour.humidifierMinutes_OFF > 1200)
                        return false;

                    if (hour.targetHumidity < 40 || hour.targetHumidity > 100)
                        return false;
                }
            }

            return true;
        }

        String ToJson()
        {
            StaticJsonDocument<3072> doc;
            JsonArray daysArray = doc.createNestedArray("k");

            for (const auto& day : days)
            {
                JsonObject dayObj = daysArray.createNestedObject();
                dayObj["h"] = day.lightHour_ON;
                dayObj["i"] = day.lightHour_OFF;

                JsonArray hoursArray = dayObj.createNestedArray("j");
                for (const auto& hour : day.hours)
                {
                    JsonObject hourObj = hoursArray.createNestedObject();
                    hourObj["a"] = hour.hour;
                    hourObj["b"] = hour.heatpadTemp_ON;
                    hourObj["c"] = hour.heatpadTemp_OFF;
                    hourObj["d"] = hour.fanMinutes_ON;
                    hourObj["e"] = hour.fanMinutes_OFF;
                    hourObj["f"] = hour.humidifierMinutes_ON;
                    hourObj["g"] = hour.humidifierMinutes_OFF;
                    hourObj["m"] = hour.targetHumidity;
                }
            }

            doc["l"] = static_cast<int64_t>(cycleStartTime);

            String jsonStr;
            serializeJson(doc, jsonStr);
            return jsonStr;
        }

        bool FromJson(const String& jsonStr)
        {
            if (jsonStr.isEmpty()) return false;

            StaticJsonDocument<3072> doc;
            DeserializationError error = deserializeJson(doc, jsonStr);
            if (error) return false;

            days.clear();
            for (JsonObject dayObj : doc["k"].as<JsonArray>())
            {
                FCSettingDay day;
                day.lightHour_ON = dayObj["h"] | 0;
                day.lightHour_OFF = dayObj["i"] | 0;

                for (JsonObject hourObj : dayObj["j"].as<JsonArray>())
                {
                    FCSettingHour hour;
                    hour.hour = hourObj["a"] | 0;
                    hour.heatpadTemp_ON = hourObj["b"] | 0.0;
                    hour.heatpadTemp_OFF = hourObj["c"] | 0.0;
                    hour.fanMinutes_ON = hourObj["d"] | 0;
                    hour.fanMinutes_OFF = hourObj["e"] | 0;
                    hour.humidifierMinutes_ON = hourObj["f"] | 0;
                    hour.humidifierMinutes_OFF = hourObj["g"] | 0;
                    hour.targetHumidity = hourObj["m"] | 0;
                    day.hours.push_back(hour);
                }

                days.push_back(day);
            }

            if (doc.containsKey("l"))
                cycleStartTime = static_cast<std::time_t>(doc["l"]);

            return true;
        }

        bool Serialize()
        {
            Preferences preferences;
            if (!preferences.begin(PREF_NAMESPACE, false)) return false;

            String jsonStr = ToJson();
            bool success = preferences.putString(PREF_KEY, jsonStr) > 0;
            preferences.end();
            return success;
        }

        bool Deserialize()
        {
            Preferences preferences;
            if (!preferences.begin(PREF_NAMESPACE, true)) return false;

            String jsonStr = preferences.getString(PREF_KEY, "");
            preferences.end();
            if (jsonStr.isEmpty()) return false;

            return FromJson(jsonStr);
        }

        bool AddDay()
        {
            if (days.size() >= 32) return false;

            FCSettingDay newDay;
            newDay.Default();
            days.push_back(newDay);
            return true;
        }

        bool CloneDay(int sourceIndex)
        {
            // Ensure the source index is valid
            if (sourceIndex < 0 || sourceIndex >= static_cast<int>(days.size()))
                return false;

            // Optional limit to prevent unbounded growth
            if (days.size() >= 32) // Adjust limit as needed
                return false;

            // Deep copy the source day
            FCSettingDay clone = days[sourceIndex];
            days.push_back(clone);

            return true;
        }

        // Removes a day at the given index from the cycle.
        // Prevents removal of the first day (index 0) to ensure at least one day remains.
        bool RemoveDay(int dayIndex)
        {
            if (dayIndex <= 0 || dayIndex >= static_cast<int>(days.size()))
                return false; // Invalid index or attempt to remove the first day

            days.erase(days.begin() + dayIndex);
            return true;
        }

        // Sets light ON/OFF hours for a specific day.
        // Validates the 24-hour format and ensures ON is earlier than OFF.
        bool SetLightHours(int dayIndex, int onHour, int offHour)
        {
            if (dayIndex < 0 || dayIndex >= static_cast<int>(days.size()))
                return false;

            if (onHour < 0 || onHour > 23 || offHour < 0 || offHour > 23)
                return false;

            if (onHour >= offHour)
                return false;

            days[dayIndex].lightHour_ON = onHour;
            days[dayIndex].lightHour_OFF = offHour;
            return true;
        }

        // Adds a default hour configuration to the end of the specified day's hour list.
        // Prevents adding more than 4 hours.
        bool AddHour(int dayIndex)
        {
            if (dayIndex < 0 || dayIndex >= static_cast<int>(days.size()))
                return false;

            auto& hourList = days[dayIndex].hours;

            if (hourList.size() >= 4)
                return false; // Maximum of 4 hours per day allowed

            int lastHour = hourList.back().hour;
            if (lastHour >= 23)
                return false; // No valid hour after 23

            FCSettingHour newHour;
            newHour.Default();
            newHour.hour = lastHour + 1; // Ensure strictly increasing

            hourList.push_back(newHour);
            return true;
        }

        // Removes an hour configuration at the given index from a day's hour list.
        // Ensures at least one hour remains and prevents removal of the first hour (index 0).
        bool RemoveHour(int dayIndex, int hourIndex)
        {
            if (dayIndex < 0 || dayIndex >= static_cast<int>(days.size()))
                return false;

            auto& hourList = days[dayIndex].hours;

            if (hourList.size() <= 1)
                return false; // At least one hour must remain

            if (hourIndex <= 0 || hourIndex >= static_cast<int>(hourList.size()))
                return false; // Invalid index or attempt to remove first hour

            hourList.erase(hourList.begin() + hourIndex);
            return true;
        }

        // Updates a specific hour configuration in a day using new validated values.
        // Performs bounds checks and validation for logical correctness.
        bool UpdateHour(int dayIndex, int hourIndex, const FCSettingHour& newHour)
        {
            if (dayIndex < 0 || dayIndex >= static_cast<int>(days.size()))
                return false;

            auto& hourList = days[dayIndex].hours;
            if (hourIndex < 0 || hourIndex >= static_cast<int>(hourList.size()))
                return false;

            if (newHour.hour < 0 || newHour.hour > 23)
                return false;

            if (newHour.heatpadTemp_ON < 1                        ||
                newHour.heatpadTemp_ON > 60                       ||
                newHour.heatpadTemp_OFF < 1                       ||
                newHour.heatpadTemp_OFF > 60                      ||      
                newHour.heatpadTemp_ON >= newHour.heatpadTemp_OFF ||
                newHour.fanMinutes_ON < 1                         ||
                newHour.fanMinutes_ON > 60                        ||
                newHour.fanMinutes_OFF < 30                       ||
                newHour.fanMinutes_OFF > 1200                     ||
                newHour.humidifierMinutes_ON < 1                  ||
                newHour.humidifierMinutes_ON > 60                 ||
                newHour.humidifierMinutes_OFF < 30                ||
                newHour.humidifierMinutes_OFF > 1200              ||
                newHour.targetHumidity < 40                       ||
                newHour.targetHumidity > 100)
            {
                return false;
            }

            // ⏱ Hour ordering constraint:
            // Check previous hour (if exists)
            if (hourIndex > 0) {
                const FCSettingHour& previous = hourList[hourIndex - 1];
                if (newHour.hour <= previous.hour)
                    return false; // must be strictly later
            }

            // Check next hour (if exists)
            if (hourIndex < static_cast<int>(hourList.size()) - 1) {
                const FCSettingHour& next = hourList[hourIndex + 1];
                if (newHour.hour >= next.hour)
                    return false; // must be strictly earlier
            }

            // 🚫 Prevent changing the hour on index 0
            FCSettingHour updated = newHour;
            if (hourIndex == 0)
                updated.hour = hourList[hourIndex].hour;

            hourList[hourIndex] = updated;
            return true;
        }
    };



    virtual ~SharedResourceManager() {};
    void begin();
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

    // Air logs methods
    void AirLogs_Add(std::time_t unixTime, float temperature, float humidity, float co2);
    std::vector<AirLogEntry> AirLogs_Get(size_t count);
    void AirLogsRecent_Add(std::time_t unixTime, float temperature, float humidity, float co2);
    std::vector<AirLogEntry> AirLogsRecent_Get(size_t count);
    long AirLogs_DurationMinutes();

    // Air logs helper methods to get average readings
    bool IsMaskedByFAE(std::time_t t, int cooldownSec = 60);
    float GetAverage(int hours, std::function<float(const AirLogEntry&)> selector, bool useFAEMask = false);
    float GetAverageTemperature(int hours, bool useFAEMask = false);
    float GetAverageHumidity(int hours, bool useFAEMask = false);
    float GetAverageCO2(int hours, bool useFAEMask = false);
    float GetTrend(int hours, std::function<float(const AirLogEntry&)> selector, bool useFAEMask = false);
    float GetTrendHumidity(int hours, bool useFAEMask = false);

    // Light logs methods
    void LightLogs_Add(std::time_t unixTime, LogCode code, const std::string& info = "");
    std::vector<LightLogEntry> LightLogs_Get(size_t count);

    // Heatpad logs methods
    void HeatpadLogs_Add(std::time_t unixTime, LogCode code, const std::string& info = "");
    std::vector<HeatpadLogEntry> HeatpadLogs_Get(size_t count);

    // Fan logs methods
    void FanLogs_Add(std::time_t unixTime, LogCode code, const std::string& info = "");
    std::vector<FanLogEntry> FanLogs_Get(size_t count);

    // Humidifier logs methods
    void HumidifierLogs_Add(std::time_t unixTime, LogCode code, const std::string& info = "");
    std::vector<HumidifierLogEntry> HumidifierLogs_Get(size_t count);

    // Accessor methods for turnOffLight
    void TurnOffLight_Set(bool value);
    bool TurnOffLight_Get();

    // Accessor methods for adaptiveHumidification
    void AdaptiveHumidification_Set(bool value);
    bool AdaptiveHumidification_Get();

    // Accessor methods for incubation
    void Incubation_Set(bool value);
    bool Incubation_Get();

    // Accessor methods for unitName
    void UnitName_Set(const std::string& value);
    std::string UnitName_Get();

    // Accessor methods for network
    void NetworkSSID_Set(const std::string& value);
    std::string NetworkSSID_Get();
    void NetworkPassword_Set(const std::string& value);
    std::string NetworkPassword_Get();

    // Accessor methods for account
    void AccountLogin_Set(const std::string& value);
    std::string AccountLogin_Get();
    void AccountPassword_Set(const std::string& value);
    std::string AccountPassword_Get();

    // Accessor methods for time & timezone
    void TimeAutomatic_Set(bool enabled);
    bool TimeAutomatic_Get();
    void TimezoneLocation_Set(const std::string& value);
    std::string TimezoneLocation_Get();

    // Accessor methods for FCSettingCycle
    void Cycle_Set(const FCSettingCycle& cycle);
    FCSettingCycle Cycle_Get();



private:
    static constexpr const char* PREF_NAMESPACE = "pzg";

    std::recursive_mutex mutex;                                             // Mutex for synchronizing access to shared resources.
    CircularBuffer<AirLogEntry, MAX_AIR_LOGS> airLogs;                                          // Logs for temperature, humidity, and CO2 (long time span, low frequency).
    CircularBuffer<AirLogEntry, MAX_AIR_LOGS_RECENT> airLogsRecent;                             // Logs for temperature, humidity, and CO2 (short time span, high frequency)
    CircularBuffer<LightLogEntry, MAX_DEVICE_LOGS> lightLogs;                                       // Logs for light status or events.
    CircularBuffer<HeatpadLogEntry, MAX_DEVICE_LOGS> heatpadLogs;                                   // Logs for heatpad status or events.
    CircularBuffer<FanLogEntry, MAX_DEVICE_LOGS> fanLogs;                                           // Logs for fan status or events.
    CircularBuffer<HumidifierLogEntry, MAX_DEVICE_LOGS> humidifierLogs;                             // Logs for humidifier status or events.
    bool turnOffLight;                                                      // If true, the LightController will stop cycling the light and keep it always OFF.
    bool adaptiveHumidification;                                            // true = regulate towards target humidity, false = use fixed ON/OFF timing
    bool incubation;
    std::string unitName;

    std::string networkSSID;
    std::string networkPassword;
    std::string accountLogin;
    std::string accountPassword;

    bool timeAutomatic;                 // true = automatyczne wykrywanie czasu przez NTP
    std::string timezoneLocation;           // np. "Europe/Warsaw"
    
    FCSettingCycle cycle;

    void FillWithDummyLogs();
    void TestSerialization();
    void TestAirLogsAverageAndTrend();

    void DeleteStateInit(uint8_t, void*);
    void BootProcedure();
	void SharingProcedure();
};

#endif