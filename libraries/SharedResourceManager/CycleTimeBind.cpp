#include "Arduino.h"
#include "CycleTimeBind.h"



void CycleTimeBind::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager)
{
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    
    std::function<void()>* stateProcedures = new std::function<void()>[1];
    stateProcedures[0] = std::bind(&CycleTimeBind::WorkingProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Working, "CycleTimeBind");
}

void CycleTimeBind::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void CycleTimeBind::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void CycleTimeBind::DeleteStateInit(uint8_t state, void* stateInit)
{
    // CycleTimeBind does not have any states with initialization parameters to delete
}

int CycleTimeBind::LightHour_ON_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentDay.lightHour_ON;
}

int CycleTimeBind::LightHour_OFF_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentDay.lightHour_OFF;
}

float CycleTimeBind::HeatpadTemp_ON_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.heatpadTemp_ON;
}

float CycleTimeBind::HeatpadTemp_OFF_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.heatpadTemp_OFF;
}

int CycleTimeBind::FanMinutes_ON_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.fanMinutes_ON;
}

int CycleTimeBind::FanMinutes_OFF_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.fanMinutes_OFF;
}

int CycleTimeBind::HumidifierMinutes_ON_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.humidifierMinutes_ON;
}

int CycleTimeBind::HumidifierMinutes_OFF_Get()
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    GetCurrentDayAndHour(currentDay, currentHour);
    return currentHour.humidifierMinutes_OFF;
}

void CycleTimeBind::GetCurrentDayAndHour(SharedResourceManager::FCSettingDay& currentDay, SharedResourceManager::FCSettingHour& currentHour, int& currentDayIndex, int& currentHourIndex)
{
    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    size_t totalDays = cycle.days.size();
    std::time_t now = clientNTP->GetUnixTime();  // Get current UTC Unix time (monotonic, stable for calculations)

    // Jump into local time
    std::time_t nowLocal = now + clientNTP->GetUtcOffsetSeconds();
    std::time_t cycleStartTimeLocal = cycle.cycleStartTime + clientNTP->GetUtcOffsetSeconds();

    currentDayIndex = 0;
    if (cycleStartTimeLocal > 0 && totalDays > 0)
    {
        // Calculate number of full days elapsed since the cycle started
        std::time_t elapsed = nowLocal - cycleStartTimeLocal;

        if (elapsed >= 0)
        {
            // Divide by number of seconds in a day (86400) to get days passed
            // Use modulo totalDays to wrap around once cycle repeats
            currentDayIndex = (elapsed / 86400) % totalDays;
        }
    }
    currentDay = cycle.days[currentDayIndex];

    struct tm* timeinfo = gmtime(&nowLocal);
    currentHourIndex = 0;
    for (int i = (int)currentDay.hours.size() - 1; i >= 0; --i)
    {
        if(currentDay.hours[i].hour <= timeinfo->tm_hour)
        {
            currentHourIndex = i;
            break;
        }
    }
    currentHour = currentDay.hours[currentHourIndex];
}

void CycleTimeBind::GetCurrentDayAndHour(SharedResourceManager::FCSettingDay& currentDay, SharedResourceManager::FCSettingHour& currentHour)
{
    int unusedDayIdx = 0;
    int unusedHourIdx = 0;
    GetCurrentDayAndHour(currentDay, currentHour, unusedDayIdx, unusedHourIdx);
}

void CycleTimeBind::WorkingProcedure()
{
    WaitForNextState(portMAX_DELAY);
}