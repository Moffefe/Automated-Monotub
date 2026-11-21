#include "Arduino.h"
#include "FanController.h"



void FanController::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager, CycleTimeBind* cycleTimeBind, uint8_t fanPin)
{
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    this->cycleTimeBind         = cycleTimeBind;
    this->fanPin                = fanPin;
    
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&FanController::BootProcedure, this);
    stateProcedures[1] = std::bind(&FanController::WorkingProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "FanController", 4096);
}

void FanController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void FanController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void FanController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // FanController does not have any states with initialization parameters to delete
}

void FanController::ToggleFan(bool toggle)
{
    if (toggle)
        digitalWrite(fanPin, LOW);
    else
        digitalWrite(fanPin, HIGH);
}

void FanController::BootProcedure()
{
    pinMode(fanPin, OUTPUT);
    ToggleFan(false);
    delay(30000);                                   // wait 30s
    SetState(State::Working);
    WaitForNextState(portMAX_DELAY);
}

void FanController::WorkingProcedure()
{
    bool fanState = false;  // Tracks the current state of the fan (ON/OFF)
    ToggleFan(fanState);    // Ensure the fan is initially OFF

    while (true)
    {
        // Get ON/OFF durations from shared resource manager
        int fanOnDuration = cycleTimeBind->FanMinutes_ON_Get() * 60 * 1000; // Convert minutes to milliseconds
        int fanOffDuration = cycleTimeBind->FanMinutes_OFF_Get() * 60 * 1000; // Convert minutes to milliseconds

        if (sharedResourceManager->Incubation_Get())
        {
            if (fanState)
            {
                fanState = false;
                ToggleFan(fanState);
                sharedResourceManager->FanLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
            }

            delay(10000);       // wait 10s
        }
        else if (!fanState)
        {
            // Fan is OFF; turn it ON and log the event
            fanState = true;
            ToggleFan(fanState);
            sharedResourceManager->FanLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ON);

            // Keep the fan ON for the specified duration
            delay(fanOnDuration);
        }
        else
        {
            // Fan is ON; turn it OFF and log the event
            fanState = false;
            ToggleFan(fanState);
            sharedResourceManager->FanLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);

            // Keep the fan OFF for the specified duration
            delay(fanOffDuration);
        }
    }
}