#include "Arduino.h"
#include "LightController.h"



void LightController::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager, CycleTimeBind* cycleTimeBind, uint8_t ledStripPin)
{
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    this->cycleTimeBind         = cycleTimeBind;
    this->ledStripPin           = ledStripPin;
    
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&LightController::BootProcedure, this);
    stateProcedures[1] = std::bind(&LightController::LightCyclingProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "LightController", 4096);
}

void LightController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void LightController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void LightController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // LightController does not have any states with initialization parameters to delete
}

void LightController::ToggleLight(bool toggle)
{
    if (toggle)
        digitalWrite(ledStripPin, LOW);
    else
        digitalWrite(ledStripPin, HIGH);
}

void LightController::BootProcedure()
{
    // Wait for NTP client to sync the time
    for (size_t i = 0; i < 5; i++)
    {
        ClientNTP::State ntpState;
        clientNTP->GetState(&ntpState);

        if (ntpState == ClientNTP::State::Synced)
            break;

        delay(2000);
    }

    pinMode(ledStripPin, OUTPUT);
    
    SetState(State::LightCycling);
    WaitForNextState(portMAX_DELAY);
}

void LightController::LightCyclingProcedure()
{
    bool lightState = false;
    ToggleLight(lightState);

    while (true)
    {
        std::tm currentTime = clientNTP->GetLocalTime();
        int lightHour_ON = cycleTimeBind->LightHour_ON_Get();
        int lightHour_OFF = cycleTimeBind->LightHour_OFF_Get();
        bool isDaytime = (currentTime.tm_hour >= lightHour_ON) && (currentTime.tm_hour < lightHour_OFF);

        if (sharedResourceManager->Incubation_Get())
        {
            if (lightState)
            {
                lightState = false;
                ToggleLight(lightState);
                sharedResourceManager->LightLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
            }
        }
        else if (sharedResourceManager->TurnOffLight_Get())
        {
            if (lightState)
            {
                lightState = false;
                ToggleLight(lightState);
                sharedResourceManager->LightLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
            }
        }
        else if (isDaytime)
        {
            if (!lightState)
            {
                lightState = true;
                ToggleLight(lightState);
                sharedResourceManager->LightLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ON);
            }
        }
        else
        {
            if (lightState)
            {
                lightState = false;
                ToggleLight(lightState);
                sharedResourceManager->LightLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
            }
        }
        
        delay(5000);
    }
}