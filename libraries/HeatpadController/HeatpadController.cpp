#include "Arduino.h"
#include "HeatpadController.h"



void HeatpadController::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager, CycleTimeBind* cycleTimeBind, uint8_t heatpadPin)
{
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    this->cycleTimeBind         = cycleTimeBind;
    this->heatpadPin            = heatpadPin;
    
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&HeatpadController::BootProcedure, this);
    stateProcedures[1] = std::bind(&HeatpadController::KeepTemperatureProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "HeatpadController", 4096);
}

void HeatpadController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void HeatpadController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void HeatpadController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // HeatpadController does not have any states with initialization parameters to delete
}

void HeatpadController::ToggleHeatpad(bool toggle)
{
    if (toggle)
        digitalWrite(heatpadPin, LOW);
    else
        digitalWrite(heatpadPin, HIGH);
}

void HeatpadController::BootProcedure()
{
    pinMode(heatpadPin, OUTPUT);
    ToggleHeatpad(false);
    delay(30000);                                   // wait 30s
    SetState(State::KeepTemperature);
    WaitForNextState(portMAX_DELAY);
}

void HeatpadController::KeepTemperatureProcedure()
{
    bool heatpadState = false;
    ToggleHeatpad(heatpadState);

    while (true)
    {
        float lowTemperatureThreshold = sharedResourceManager->Incubation_Get() ? 23.5f : cycleTimeBind->HeatpadTemp_ON_Get();        // Temperature to start heating
        //float highTemperatureThreshold = cycleTimeBind->HeatpadTemp_OFF_Get();          // Temperature to stop heating
        float highTemperatureThreshold = lowTemperatureThreshold + 0.3f;                  //
        float safeTemperatureThreshold = 5;                                                         // Minimum safe temperature to detect sensor failure

        auto recent = sharedResourceManager->AirLogsRecent_Get(1);
        if (recent.size() > 0)
        {
            if(recent[0].temperature < safeTemperatureThreshold)
            {
                // Temperature is dangerously low, likely due to sensor failure. Turn off heatpad for safety.
                if (heatpadState)
                {
                    heatpadState = false;
                    ToggleHeatpad(heatpadState);
                    sharedResourceManager->HeatpadLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ERROR_1);
                }
            }
            else if (recent[0].temperature < lowTemperatureThreshold)
            {
                if (!heatpadState)
                {
                    heatpadState = true;
                    ToggleHeatpad(heatpadState);
                    sharedResourceManager->HeatpadLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ON);
                }
            }
            else if (recent[0].temperature > highTemperatureThreshold)
            {         
                if (heatpadState)
                {
                    heatpadState = false;
                    ToggleHeatpad(heatpadState);
                    sharedResourceManager->HeatpadLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
                }
            }
        }
        else
        {
            if (heatpadState)
            {
                heatpadState = false;
                ToggleHeatpad(heatpadState);
                sharedResourceManager->HeatpadLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ERROR_0);
            }
        }

        delay(10000);       // wait 10s
    }   
}