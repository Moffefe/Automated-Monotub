#include "Arduino.h"
#include "SharedResourceManager.h"

#define GuardMutex std::lock_guard<std::recursive_mutex> guardMutex (mutex)



void SharedResourceManager::begin()
{
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[Boot] = std::bind(&SharedResourceManager::BootProcedure, this);
    stateProcedures[Sharing] = std::bind(&SharedResourceManager::SharingProcedure, this);

    StateMachineRTOS::begin(stateProcedures, State::Boot, "SharedResourceManager", 8192);
}

void SharedResourceManager::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void SharedResourceManager::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void SharedResourceManager::AirLogs_Add(std::time_t unixTime, float temperature, float humidity, float co2)
{
    GuardMutex;
    airLogs.push(AirLogEntry(unixTime, temperature, humidity, co2));
}

std::vector<SharedResourceManager::AirLogEntry> SharedResourceManager::AirLogs_Get(size_t count)
{
    GuardMutex;

    std::vector<AirLogEntry> result;

    if (airLogs.size() == 0)
        return result;

    size_t totalAvailable = airLogs.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;

    size_t firstIndexToCopy = totalAvailable - totalToCopy;             // Zaczynamy od indeksu najstarszego wpisu, który chcemy zwrócić
    result.reserve(totalToCopy);                                                // Rezerwujemy miejsce w wektorze wynikowym (optymalizacja)

    // Iterujemy od najstarszego logu, który ma się znaleźć w wyniku,
    // aż do najnowszego, dodając wpisy po kolei.
    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(airLogs[i]);

    return result;                  // Zwracamy kopię ostatnich N logów w poprawnej kolejności (od najstarszego do najnowszego)
}

void SharedResourceManager::AirLogsRecent_Add(std::time_t unixTime, float temperature, float humidity, float co2)
{
    GuardMutex;
    airLogsRecent.push(AirLogEntry(unixTime, temperature, humidity, co2));
}

std::vector<SharedResourceManager::AirLogEntry> SharedResourceManager::AirLogsRecent_Get(size_t count)
{
    GuardMutex;

    std::vector<AirLogEntry> result;

    if (airLogsRecent.size() == 0)
        return result;

    size_t totalAvailable = airLogsRecent.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;
    size_t firstIndexToCopy = totalAvailable - totalToCopy;
    result.reserve(totalToCopy);

    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(airLogsRecent[i]);

    return result;
}

/**
 * @brief Zwraca różnicę czasu (w minutach) między najstarszym a najnowszym logiem w buforze airLogs.
 * @return Różnica czasu w minutach, lub 0 jeśli nie ma wystarczających danych.
 */
long SharedResourceManager::AirLogs_DurationMinutes()
{
    GuardMutex;

    if (airLogs.size() < 2) return 0;

    const auto& oldest = airLogs.first();
    const auto& newest = airLogs.last();

    long diffSeconds = static_cast<long>(newest.unixTime - oldest.unixTime);
    if (diffSeconds < 0) diffSeconds = 0; // zabezpieczenie (np. błąd zegara)

    long diffMinutes = diffSeconds / 60;
    return diffMinutes;
}

/**
 * @brief Sprawdza, czy pomiar z danego czasu powinien być pominięty (zmaskowany)
 *        z powodu działania systemu FAE (wentylatora wymiany powietrza).
 *
 * @param t            Czas pomiaru (timestamp logu wilgotności).
 * @param cooldownSec  Czas w sekundach po wyłączeniu FAE, w którym dane nadal są maskowane.
 * @return true jeśli pomiar należy zmaskować, false jeśli dane są wiarygodne.
 */
bool SharedResourceManager::IsMaskedByFAE(std::time_t t, int cooldownSec)
{
    if (fanLogs.size() == 0) return false;

    // Traverse from newest to oldest
    for (int i = static_cast<int>(fanLogs.size()) - 1; i >= 0; --i)
    {
        const FanLogEntry& entry = fanLogs[i];

        // Find the last entry older or equal to t
        if (entry.unixTime <= t)
        {
            // If fan was ON at that time → mask
            if (entry.code == LogCode::ON)
                return true;

            // If fan was OFF but recently before t → mask within cooldown
            if (entry.code == LogCode::OFF && (t - entry.unixTime) <= cooldownSec)
                return true;

            // Otherwise → not masked
            return false;
        }
    }

    return false;       // No entry older than t → assume not masked
}

/**
 * @brief Oblicza średnią wybranej wielkości (np. temperatury, wilgotności, CO₂)
 *        z logów w podanym zakresie minut wstecz.
 *
 * @param minutes  Liczba minut wstecz do analizy (np. 60 oznacza ostatnią godzinę).
 * @param selector Funkcja/lambda wybierająca wartość z obiektu AirLog
 *                 (np. [](const AirLogEntry& l){ return l.temperature; }).
 * @return Średnia z wybranego pola, lub -1.0f jeśli brak wystarczających danych.
 */
float SharedResourceManager::GetAverage(int minutes, std::function<float(const AirLogEntry&)> selector, bool useFAEMask)
{
    GuardMutex;

    if (airLogs.size() == 0) return -1.0f;

    std::time_t latestTime = airLogs.last().unixTime;
    std::time_t threshold  = latestTime - (minutes * 60);

    float total = 0.0f;
    int count = 0;

    for (int i = static_cast<int>(airLogs.size()) - 1; i >= 0; --i)
    {
        if (airLogs[i].unixTime >= threshold)
        {
            // jeśli maskowanie jest aktywne i log jest z okresu FAE → pomiń
            if (useFAEMask && IsMaskedByFAE(airLogs[i].unixTime))
                continue;

            total += selector(airLogs[i]);
            count++;
        }
        else
        {
            break; // starsze niż próg – przerywamy
        }
    }

    return count > 0 ? (total / count) : -1.0f;
}

float SharedResourceManager::GetAverageTemperature(int minutes, bool useFAEMask)
{
    return GetAverage(minutes, [](const AirLogEntry& log){ return log.temperature; }, useFAEMask);
}

float SharedResourceManager::GetAverageHumidity(int minutes, bool useFAEMask)
{
    return GetAverage(minutes, [](const AirLogEntry& log){ return log.humidity; }, useFAEMask);
}

float SharedResourceManager::GetAverageCO2(int minutes, bool useFAEMask)
{
    return GetAverage(minutes, [](const AirLogEntry& log){ return log.co2; }, useFAEMask);
}

/**
 * @brief Oblicza trend wybranej wartości (np. wilgotności, temperatury, CO₂)
 *        w zadanym przedziale minut wstecz.
 *
 * Metoda dzieli wskazany okres (minutes) na dwie połowy:
 * - starszą połowę (old)
 * - nowszą połowę (new)
 *
 * Następnie liczy średnią wartość w każdej połowie i zwraca różnicę:
 *   avgNew - avgOld
 *
 * Interpretacja wyniku:
 *   > 0  → trend rosnący (wartości rosną w czasie)
 *   < 0  → trend malejący (wartości spadają w czasie)
 *   = 0  → brak wyraźnego trendu
 *
 * @param minutes  Łączna liczba minut wstecz do analizy.
 * @param selector Funkcja wybierająca pole z obiektu AirLog (np. [](const AirLog& l){ return l.humidity; }).
 * @return Różnica średnich: dodatnia = rośnie, ujemna = spada, 0.0f przy braku danych.
 */
float SharedResourceManager::GetTrend(int minutes, std::function<float(const AirLogEntry&)> selector, bool useFAEMask)
{
    GuardMutex;

    if (airLogs.size() < 4) return 0.0f;

    std::time_t latestTime = airLogs.last().unixTime;
    std::time_t middleTime = latestTime - (minutes * 60 / 2);  // połowa okresu
    std::time_t oldestTime = latestTime - (minutes * 60);      // pełen okres

    float oldTotal = 0.0f, newTotal = 0.0f;
    int oldCount = 0, newCount = 0;

    for (int i = static_cast<int>(airLogs.size()) - 1; i >= 0; --i)
    {
        std::time_t t = airLogs[i].unixTime;

        // jeśli maskowanie jest aktywne i log jest z okresu FAE → pomiń
        if (useFAEMask && IsMaskedByFAE(t))
            continue;

        if (t >= middleTime && t <= latestTime)
        {
            newTotal += selector(airLogs[i]);
            newCount++;
        }
        else if (t >= oldestTime && t < middleTime)
        {
            oldTotal += selector(airLogs[i]);
            oldCount++;
        }
        else if (t < oldestTime)
        {
            break;
        }
    }

    if (oldCount == 0 || newCount == 0) return 0.0f;

    float avgOld = oldTotal / oldCount;
    float avgNew = newTotal / newCount;

    return avgNew - avgOld; // dodatnie = rośnie, ujemne = spada
}

float SharedResourceManager::GetTrendHumidity(int minutes, bool useFAEMask)
{
    return GetTrend(minutes, [](const AirLogEntry& log){ return log.humidity; }, useFAEMask);
}

void SharedResourceManager::LightLogs_Add(std::time_t unixTime, LogCode code, const std::string& info)
{
    GuardMutex;
    lightLogs.push(LightLogEntry(unixTime, code, info));
}

std::vector<SharedResourceManager::LightLogEntry> SharedResourceManager::LightLogs_Get(size_t count)
{
    GuardMutex;

    std::vector<LightLogEntry> result;

    if (lightLogs.size() == 0)
        return result;

    size_t totalAvailable = lightLogs.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;
    size_t firstIndexToCopy = totalAvailable - totalToCopy;

    result.reserve(totalToCopy);

    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(lightLogs[i]);

    return result;
}


void SharedResourceManager::HeatpadLogs_Add(std::time_t unixTime, LogCode code, const std::string& info)
{
    GuardMutex;
    heatpadLogs.push(HeatpadLogEntry(unixTime, code, info));
}

std::vector<SharedResourceManager::HeatpadLogEntry> SharedResourceManager::HeatpadLogs_Get(size_t count)
{
    GuardMutex;

    std::vector<HeatpadLogEntry> result;

    if (heatpadLogs.size() == 0)
        return result;

    size_t totalAvailable = heatpadLogs.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;
    size_t firstIndexToCopy = totalAvailable - totalToCopy;

    result.reserve(totalToCopy);

    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(heatpadLogs[i]);

    return result;
}

void SharedResourceManager::FanLogs_Add(std::time_t unixTime, LogCode code, const std::string& info)
{
    GuardMutex;
    fanLogs.push(FanLogEntry(unixTime, code, info));
}

std::vector<SharedResourceManager::FanLogEntry> SharedResourceManager::FanLogs_Get(size_t count)
{
    GuardMutex;

    std::vector<FanLogEntry> result;

    if (fanLogs.size() == 0)
        return result;

    size_t totalAvailable = fanLogs.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;
    size_t firstIndexToCopy = totalAvailable - totalToCopy;

    result.reserve(totalToCopy);

    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(fanLogs[i]);

    return result;
}


void SharedResourceManager::HumidifierLogs_Add(std::time_t unixTime, LogCode code, const std::string& info)
{
    GuardMutex;
    humidifierLogs.push(HumidifierLogEntry(unixTime, code, info));
}

std::vector<SharedResourceManager::HumidifierLogEntry> SharedResourceManager::HumidifierLogs_Get(size_t count)
{
    GuardMutex;

    std::vector<HumidifierLogEntry> result;

    if (humidifierLogs.size() == 0)
        return result;

    size_t totalAvailable = humidifierLogs.size();
    size_t totalToCopy = (count < totalAvailable) ? count : totalAvailable;
    size_t firstIndexToCopy = totalAvailable - totalToCopy;

    result.reserve(totalToCopy);

    for (size_t i = firstIndexToCopy; i < totalAvailable; ++i)
        result.push_back(humidifierLogs[i]);

    return result;
}

void SharedResourceManager::TurnOffLight_Set(bool value)
{
    GuardMutex;
    turnOffLight = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putBool("turnOffLight", turnOffLight);
        preferences.end();
    }
}

bool SharedResourceManager::TurnOffLight_Get()
{
    GuardMutex;
    return turnOffLight;
}

void SharedResourceManager::AdaptiveHumidification_Set(bool value)
{
    GuardMutex;
    adaptiveHumidification = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putBool("adaptHumid", adaptiveHumidification);
        preferences.end();
    }
}

bool SharedResourceManager::AdaptiveHumidification_Get()
{
    GuardMutex;
    return adaptiveHumidification;
}

void SharedResourceManager::Incubation_Set(bool value)
{
    GuardMutex;
    incubation = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putBool("incubation", incubation);
        preferences.end();
    }
}

bool SharedResourceManager::Incubation_Get()
{
    GuardMutex;
    return incubation;
}

void SharedResourceManager::UnitName_Set(const std::string& value)
{
    GuardMutex;
    unitName = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("unitName", unitName.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::UnitName_Get()
{
    GuardMutex;
    return unitName;
}

void SharedResourceManager::NetworkSSID_Set(const std::string& value)
{
    GuardMutex;
    networkSSID = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("networkSSID", networkSSID.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::NetworkSSID_Get()
{
    GuardMutex;
    return networkSSID;
}

void SharedResourceManager::NetworkPassword_Set(const std::string& value)
{
    GuardMutex;
    networkPassword = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("networkPassword", networkPassword.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::NetworkPassword_Get()
{
    GuardMutex;
    return networkPassword;
}

void SharedResourceManager::AccountLogin_Set(const std::string& value)
{
    GuardMutex;
    accountLogin = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("accountLogin", accountLogin.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::AccountLogin_Get()
{
    GuardMutex;
    return accountLogin;
}

void SharedResourceManager::AccountPassword_Set(const std::string& value)
{
    GuardMutex;
    accountPassword = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("accountPassword", accountPassword.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::AccountPassword_Get()
{
    GuardMutex;
    return accountPassword;
}

void SharedResourceManager::TimeAutomatic_Set(bool enabled)
{
    GuardMutex;
    timeAutomatic = enabled;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putBool("timeAutomatic", timeAutomatic);
        preferences.end();
    }
}

bool SharedResourceManager::TimeAutomatic_Get()
{
    GuardMutex;
    return timeAutomatic;
}

void SharedResourceManager::TimezoneLocation_Set(const std::string& value)
{
    GuardMutex;
    timezoneLocation = value;

    Preferences preferences;
    if (preferences.begin(PREF_NAMESPACE, false))
    {
        preferences.putString("timezoneLoc", timezoneLocation.c_str());
        preferences.end();
    }
}

std::string SharedResourceManager::TimezoneLocation_Get()
{
    GuardMutex;
    return timezoneLocation;
}

void SharedResourceManager::Cycle_Set(const FCSettingCycle& cycle)
{
    GuardMutex;
    this->cycle = cycle;
}

SharedResourceManager::FCSettingCycle SharedResourceManager::Cycle_Get()
{
    GuardMutex;
    return cycle;
}

void SharedResourceManager::FillWithDummyLogs()
{
    GuardMutex;

    std::time_t startTime = 0;      // Start at "time zero"
    int interval = 300;                 // Interval of 5 minutes between each log entry, in seconds

    // Dummy temperature, humidity, and CO2 values for air logs
    float temperature = 22.0;
    float humidity = 90.0;
    float co2 = 500.0;

    // Fill AirLogs and AirLogsRecent with entries every 5 minutes over 4 hours
    for (std::time_t t = startTime; t <= 4 * 3600; t += interval) // 4 hours * 3600 seconds per hour
    {
        // Vary temperature, humidity, and CO2 slightly for realism
        temperature += (rand() % 10 - 5) * 0.1f;
        humidity += (rand() % 10 - 5) * 0.5f;
        co2 += (rand() % 20 - 10) * 1.0f;

        AirLogs_Add(t, temperature, humidity, co2);
    }

    // Fill LightLogs with entries indicating changes in light status
    for (std::time_t t = startTime; t <= 4 * 3600; t += 1800) // Every 30 minutes
    {
        LogCode code = (t % 3600 == 0) ? LogCode::ON : LogCode::OFF; // Toggle ON/OFF every hour
        LightLogs_Add(t, code);
    }

    // Fill HeatpadLogs with entries indicating heatpad status changes
    for (std::time_t t = startTime; t <= 4 * 3600; t += 1800) // Every 30 minutes
    {
        LogCode code = (t % 3600 == 0) ? LogCode::ON : LogCode::OFF; // Toggle ON/OFF every hour
        HeatpadLogs_Add(t, code);
    }
}

void SharedResourceManager::TestSerialization()
{
    GuardMutex;
    Serial.println(F("=== TestSerialization Start ==="));

    // Initialize cycle with known defaults
    cycle.Default();

    // Serialize to Preferences (writes to NVS)
    bool successWrite = cycle.Serialize();
    Serial.print(F("Serialize() success: "));
    Serial.println(successWrite ? "true" : "false");

    // Overwrite with known wrong values to verify Deserialize works
    cycle.days.clear();  // Empty out to test deserialization correctness

    // Deserialize from Preferences
    bool successRead = cycle.Deserialize();
    Serial.print(F("Deserialize() success: "));
    Serial.println(successRead ? "true" : "false");

    // Output test information
    Serial.print(F("Days in deserialized cycle: "));
    Serial.println(cycle.days.size());

    if (!cycle.days.empty()) {
        const auto& day = cycle.days[0];
        Serial.print(F("First day - Light ON/OFF hours: "));
        Serial.print(day.lightHour_ON);
        Serial.print(F(" / "));
        Serial.println(day.lightHour_OFF);

        if (!day.hours.empty()) {
            const auto& hour = day.hours[0];
            Serial.print(F("First hour - Heatpad ON/OFF: "));
            Serial.print(hour.heatpadTemp_ON);
            Serial.print(F(" / "));
            Serial.println(hour.heatpadTemp_OFF);
        }
    }

    // Reset to known default state
    cycle.Default();
    Serial.println(F("=== TestSerialization End ==="));
}

void SharedResourceManager::TestAirLogsAverageAndTrend()
{
    GuardMutex;
    Serial.println(F("=== TestAirLogsAverageAndTrend Start ==="));

    airLogs.clear();

    std::time_t startTime = 0;
    const int sampleCount = 20;
    const int interval = 1800; // 30 minut = 1800 s → razem 600 minut (10 godzin)

    // ───────────────────────────────
    // 🧩 FAZA 1: TEST TRENDU ROSNĄCEGO
    // ───────────────────────────────
    Serial.println(F("[1] Positive Trend Test"));

    float baseHumidity = 60.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        std::time_t t = startTime + (i * interval);
        float humidity = baseHumidity + i; // rosnąco
        AirLogs_Add(t, 20.0f, humidity, 500.0f);
    }

    // Wartości: 60, 61, 62, ..., 79
    // Zakres = 600 minut (10 godzin)
    // Średnia = (60 + 79) / 2 = 69.5
    float avgPos = GetAverageHumidity(600);  // teraz w minutach
    Serial.print(F("[AverageHumidity +trend] Expected: 69.5, Got: "));
    Serial.println(avgPos, 2);
    if (fabs(avgPos - 69.5f) < 0.01f)
        Serial.println(F("✅ AverageHumidity (positive) OK"));
    else
        Serial.println(F("❌ AverageHumidity (positive) FAIL"));

    // Trend dzieli okres 600 minut na dwie połowy po 300 minut
    // Pierwsza połowa: 60–69 → avg = 64.5
    // Druga połowa: 70–79 → avg = 74.5
    // Trend = +10.0
    float trendPos = GetTrendHumidity(600);
    Serial.print(F("[TrendHumidity +trend] Expected: 10.0, Got: "));
    Serial.println(trendPos, 2);
    if (fabs(trendPos - 10.0f) < 0.01f)
        Serial.println(F("✅ TrendHumidity (positive) OK"));
    else
        Serial.println(F("❌ TrendHumidity (positive) FAIL"));

    // ───────────────────────────────
    // 🧩 FAZA 2: TEST TRENDU MALEJĄCEGO
    // ───────────────────────────────
    Serial.println(F("\n[2] Negative Trend Test"));

    airLogs.clear();

    baseHumidity = 80.0f;
    for (int i = 0; i < sampleCount; ++i)
    {
        std::time_t t = startTime + (i * interval);
        float humidity = baseHumidity - i; // malejąco
        AirLogs_Add(t, 20.0f, humidity, 500.0f);
    }

    // Wartości: 80, 79, ..., 61
    // Średnia = (80 + 61) / 2 = 70.5
    float avgNeg = GetAverageHumidity(600);
    Serial.print(F("[AverageHumidity -trend] Expected: 70.5, Got: "));
    Serial.println(avgNeg, 2);
    if (fabs(avgNeg - 70.5f) < 0.01f)
        Serial.println(F("✅ AverageHumidity (negative) OK"));
    else
        Serial.println(F("❌ AverageHumidity (negative) FAIL"));

    // Trend:
    // Pierwsza połowa: 80–71 → avg = 75.5
    // Druga połowa: 70–61 → avg = 65.5
    // Trend = -10.0
    float trendNeg = GetTrendHumidity(600);
    Serial.print(F("[TrendHumidity -trend] Expected: -10.0, Got: "));
    Serial.println(trendNeg, 2);
    if (fabs(trendNeg + 10.0f) < 0.01f)
        Serial.println(F("✅ TrendHumidity (negative) OK"));
    else
        Serial.println(F("❌ TrendHumidity (negative) FAIL"));

    // ───────────────────────────────
    // ✅ KONIEC TESTU
    // ───────────────────────────────
    Serial.println(F("=== TestAirLogsAverageAndTrend End ===\n"));
}

void SharedResourceManager::DeleteStateInit(uint8_t state, void* stateInit)
{
    // SharedResourceManager does not have any states with initialization parameters to delete
}

void SharedResourceManager::BootProcedure()
{
    //FillWithDummyLogs();            // DEBUG ONLY!!!
    //TestSerialization();                    // DEBUG ONLY!!!
    //TestAirLogsAverageAndTrend();                   // DEBUG ONLY!!!

    {
        GuardMutex;

        Preferences preferences;
        if (preferences.begin(PREF_NAMESPACE, false))
        {
            // --- Wygeneruj unikalną nazwę na podstawie Chip ID ---
            uint64_t chipid = ESP.getEfuseMac();
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "FC%04X%08lX", (uint16_t)(chipid >> 32), (unsigned long)chipid);
            String defaultUnitName = String(buffer);
            defaultUnitName = defaultUnitName.substring(0, defaultUnitName.length() - 6);
            Serial.print("(SharedResourceManager) DefaultUnitName generated: ");
            Serial.println(defaultUnitName);

            // --- Light ---
            turnOffLight = preferences.getBool("turnOffLight", false);
            Serial.print("(SharedResourceManager) turnOffLight: ");
            Serial.println(turnOffLight ? "true" : "false");

            // --- Adaptive Humidification ---
            adaptiveHumidification = preferences.getBool("adaptHumid", true);
            Serial.print("(SharedResourceManager) adaptiveHumidification: ");
            Serial.println(adaptiveHumidification ? "true" : "false");

            // --- Incubation ---
            incubation = preferences.getBool("incubation", false);
            Serial.print("(SharedResourceManager) incubation: ");
            Serial.println(incubation ? "true" : "false");

            // --- UnitName ---
            unitName = preferences.getString("unitName", defaultUnitName).c_str();
            Serial.print("(SharedResourceManager) UnitName loaded: ");
            Serial.println(unitName.c_str());

            // --- Network ---
            networkSSID = preferences.getString("networkSSID", "").c_str();
            networkPassword = preferences.getString("networkPassword", "").c_str();

            // --- Account ---
            accountLogin = preferences.getString("accountLogin", "admin").c_str();
            accountPassword = preferences.getString("accountPassword", "admin").c_str();

            // --- Time & Timezone ---
            timeAutomatic = preferences.getBool("timeAutomatic", false);
            timezoneLocation = preferences.getString("timezoneLoc", "Etc/UTC").c_str();
            Serial.print("(SharedResourceManager) TimeAutomatic: ");
            Serial.println(timeAutomatic ? "true" : "false");
            Serial.print("(SharedResourceManager) Timezone loaded: ");
            Serial.println(timezoneLocation.c_str());

            preferences.end();
        }

        cycle.Default();
        bool successRead = cycle.Deserialize();
        if(successRead)
            Serial.println("(SharedResourceManager) Cycle deserialization successful");
        else
            Serial.println("(SharedResourceManager) Cycle deserialization failed");
    }

    SetState(State::Sharing);
    WaitForNextState(portMAX_DELAY);
}

void SharedResourceManager::SharingProcedure()
{
    WaitForNextState(portMAX_DELAY);
}