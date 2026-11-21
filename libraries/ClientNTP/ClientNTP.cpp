#include "Arduino.h"
#include "ClientNTP.h"

#define GuardMutex std::lock_guard<std::recursive_mutex> guardMutex (mutex)



void ClientNTP::begin(SharedResourceManager* sharedResourceManager)
{   
    this->sharedResourceManager = sharedResourceManager;

    std::function<void()>* stateProcedures = new std::function<void()>[4];
    stateProcedures[0] = std::bind(&ClientNTP::StartProcedure, this);
    stateProcedures[1] = std::bind(&ClientNTP::NotSyncedProcedure, this);
    stateProcedures[2] = std::bind(&ClientNTP::SyncedProcedure, this);
    stateProcedures[3] = std::bind(&ClientNTP::SyncAttemptProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Start, "ClientNTP", 4096);
}

void ClientNTP::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void ClientNTP::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

std::time_t ClientNTP::GetUnixTime()
{
    GuardMutex;

    unsigned long currentMillis = millis();
    unsigned long elapsedMillis = currentMillis - syncMillis;       // Elapsed time in milliseconds
    std::time_t currentTime = unixTime + (elapsedMillis / 1000);        // Convert milliseconds to seconds and add to unixTime
    return currentTime;
}

void ClientNTP::SetUnixTime(std::time_t newUnixTime)
{
    GuardMutex;

    unixTime = newUnixTime;
    syncMillis = millis();
}

/**
 * @brief Zwraca lokalny czas jako strukturę std::tm.
 *
 * UWAGA: Funkcja zakłada, że wcześniej została ustawiona strefa czasowa
 *        przy pomocy POSIX TZ stringa, np.:
 *
 *        setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
 *        tzset();
 *
 *        Bez ustawionej strefy zwrócony czas będzie odpowiadał domyślnej
 *        konfiguracji środowiska (często UTC).
 *
 * @return std::tm - struktura lokalnego czasu (uwzględnia strefę i DST).
 */
std::tm ClientNTP::GetLocalTime()
{
    std::time_t now = GetUnixTime();
    std::tm localTime;
    localtime_r(&now, &localTime);                  // Konwersja na lokalny czas wg aktualnego TZ

    return localTime;
}

/// @brief Oblicza aktualny offset UTC w sekundach dla ustawionej strefy.
/// @return liczba sekund różnicy względem UTC (np. 3600 dla CET, 7200 dla CEST).
int32_t ClientNTP::GetUtcOffsetSeconds()
{
    std::time_t now = GetUnixTime();

    std::tm gmt;
    gmtime_r(&now, &gmt);

    std::tm local;
    localtime_r(&now, &local);

    // policz offset w sekundach
    int offset = (local.tm_hour - gmt.tm_hour) * 3600
               + (local.tm_min  - gmt.tm_min)  * 60
               + (local.tm_sec  - gmt.tm_sec);

    // korekta przy przejściu doby
    if (local.tm_yday > gmt.tm_yday) offset += 86400;
    else if (local.tm_yday < gmt.tm_yday) offset -= 86400;

    return offset;
}

// Format: 2025-10-03 21:48:05
String ClientNTP::GetUtcTimeString()
{
    std::time_t now = GetUnixTime();
    std::tm tmUtc{};
    std::tm* utcPtr = gmtime(&now);
    if (utcPtr) tmUtc = *utcPtr;

    char buffer[40];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmUtc);
    return String(buffer);
}

// Format: 2025-10-03 23:48:05
String ClientNTP::GetLocalTimeString()
{
    std::tm tmLocal = GetLocalTime();
    char buffer[40];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmLocal);
    return String(buffer);
}

void ClientNTP::DeleteStateInit(uint8_t state, void* stateInit)
{
    // ClientNTP does not have any states with initialization parameters to delete
}

void ClientNTP::StartProcedure()
{
    while (!ToolBox::IsStationConnected())
        delay(1000);

    SetState(State::SyncAttempt);
    WaitForNextState(portMAX_DELAY);
}

void ClientNTP::NotSyncedProcedure()
{
    delay(5000);

    while (!ToolBox::IsStationConnected())
        delay(1000);

    SetState(State::SyncAttempt);
    WaitForNextState(portMAX_DELAY);
}

void ClientNTP::SyncedProcedure()
{
    delay(10 * 60 * 60 * 1000);  // 10 hours

    if (!ToolBox::IsStationConnected())
        SetState(State::NotSynced);
    else
        SetState(State::SyncAttempt);

    WaitForNextState(portMAX_DELAY);
}

void ClientNTP::SyncAttemptProcedure()
{
    if (!ToolBox::IsStationConnected() || !sharedResourceManager->TimeAutomatic_Get())
    {
        SetState(State::NotSynced);
    }
    else
    {
        // List of NTP servers to try
        const char* ntpServers[] =
        {
            "time.google.com",      // global
            "time.cloudflare.com",  // global
            "pool.ntp.org",         // global (czasem blokowane w CN)
            "time.nist.gov",        // USA
            "ntp.ubuntu.com",       // global Ubuntu pool

            // --- dodatkowe lokalne ---
            "cn.pool.ntp.org",      // Chiny - kieruje do serwerów w CN
            "ntp1.aliyun.com",      // Chiny (Alibaba, bardzo stabilny)
            "ru.pool.ntp.org",      // Rosja
            "in.pool.ntp.org",      // Indie
            "ir.pool.ntp.org"       // Iran
        };
        const int ntpPort = 123;                 // NTP port
        const int ntpPacketSize = 48;            // NTP packet size (first 48 bytes are for the NTP time)

        // Create a packet buffer for the NTP request
        byte packetBuffer[ntpPacketSize] = {0};
        packetBuffer[0] = 0b11100011;   // LI, Version, Mode
        packetBuffer[1] = 0;            // Stratum, or type of clock
        packetBuffer[2] = 6;            // Polling Interval
        packetBuffer[3] = 0xEC;         // Peer Clock Precision
        // Remaining bytes should be zeroed out by default

        bool syncSuccess = false;

        // Try each NTP server in the list
        for (const char* ntpServer : ntpServers)
        {
            // Initialize UDP for sending NTP requests
            WiFiUDP udp;
            udp.begin(ntpPort);

            // Send the packet to the NTP server
            udp.beginPacket(ntpServer, ntpPort);
            udp.write(packetBuffer, ntpPacketSize);
            udp.endPacket();

            // Wait for a response
            delay(3000);  // Allow time for the server to respond

            if (udp.parsePacket())
            {
                // We received a response, now read it
                udp.read(packetBuffer, ntpPacketSize);

                // Extract the Unix time from the response
                unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
                unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
                // Combine the two words into a long integer
                unsigned long secsSince1900 = highWord << 16 | lowWord;

                const unsigned long seventyYears = 2208988800UL;  // Difference between 1900 and Unix epoch (1970)
                unsigned long unixTimeNow = secsSince1900 - seventyYears;

                SetUnixTime(unixTimeNow);
                syncSuccess = true;
            }

            // Clean up UDP and wait for the next state
            udp.stop();

            if (syncSuccess)
                break;
        }

        if (syncSuccess)
            SetState(State::Synced);
        else
            SetState(State::NotSynced);
    }

    WaitForNextState(portMAX_DELAY);
}