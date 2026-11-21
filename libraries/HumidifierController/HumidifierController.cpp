#include "Arduino.h"
#include "HumidifierController.h"



void HumidifierController::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager, CycleTimeBind* cycleTimeBind, uint8_t humidifierPin)
{
    this->clientNTP                         = clientNTP;
    this->sharedResourceManager             = sharedResourceManager;
    this->cycleTimeBind                     = cycleTimeBind;
    this->humidifierPin                     = humidifierPin;
    this->adaptiveHumidificationDuration_ON = 10;

    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&HumidifierController::BootProcedure, this);
    stateProcedures[1] = std::bind(&HumidifierController::MaintainHumidityProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "HumidifierController", 4096);
}

void HumidifierController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void HumidifierController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void HumidifierController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // HumidifierController does not have any states with initialization parameters to delete
}

void HumidifierController::ToggleHumidifier(bool toggle)
{
    #ifdef DEBUG_HUMIDIFIER_CONTROLLER
    SERIAL_PRINTF("(HumidifierController) ToggleHumidifier(%s) on GPIO %u\n", toggle ? "ON" : "OFF", static_cast<unsigned int>(humidifierPin));
    #endif

    if (toggle)
        digitalWrite(humidifierPin, LOW);
    else
        digitalWrite(humidifierPin, HIGH);
}

void HumidifierController::BootProcedure()
{
    pinMode(humidifierPin, OUTPUT);
    ToggleHumidifier(false);
    delay(30000); // wait 30s
    SetState(State::MaintainHumidity);
    WaitForNextState(portMAX_DELAY);
}

void HumidifierController::MaintainHumidityProcedure()
{
    #ifdef DEBUG_HUMIDIFIER_CONTROLLER
    SERIAL_PRINTLN("(HumidifierController) MaintainHumidity procedure started");
    #endif

    bool humidifierState = false;
    ToggleHumidifier(humidifierState);

    while (true)
    {
        if (sharedResourceManager->AdaptiveHumidification_Get())
        {
            #ifdef DEBUG_HUMIDIFIER_CONTROLLER
            SERIAL_PRINTLN("(HumidifierController) Mode: Adaptive");
            #endif

            MaintainHumidityAdaptive(humidifierState);
        }
        else
        {
            #ifdef DEBUG_HUMIDIFIER_CONTROLLER
            SERIAL_PRINTLN("(HumidifierController) Mode: Fixed");
            #endif

            // Reset adaptive humidification
            adaptiveHumidificationDuration_ON = 10;

            // Get ON/OFF durations from shared resource manager
            int humidifierOnDuration = cycleTimeBind->HumidifierMinutes_ON_Get() * 60 * 1000; // Convert minutes to milliseconds
            int humidifierOffDuration = cycleTimeBind->HumidifierMinutes_OFF_Get() * 60 * 1000; // Convert minutes to milliseconds
            MaintainHumidityTimer(humidifierState, humidifierOnDuration, humidifierOffDuration);
        }
    }
}

void HumidifierController::MaintainHumidityAdaptive(bool &humidifierState)
{
    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    cycleTimeBind->GetCurrentDayAndHour(currentDay, currentHour);

    long durationMinutes     = sharedResourceManager->AirLogs_DurationMinutes();
    long durationMinutesHalf = durationMinutes / 2;
    durationMinutes          = constrain(durationMinutes, 0, 60);
    durationMinutesHalf      = constrain(durationMinutesHalf, 0, 30);
    float targetHumidity     = currentHour.targetHumidity;
    float avg                = sharedResourceManager->GetAverageHumidity(durationMinutesHalf, true);
    float trend              = sharedResourceManager->GetTrendHumidity(durationMinutes, true);
    float distanceToTarget   = abs(targetHumidity - avg);
    float distanceToTarget01 = constrain(distanceToTarget / 5.0f, 0.0f, 1.0f);
    float trendThreshold     = 4.0f;
    int cooldownDuration     = ToolBox::Lerp(10 * 60 * 1000, 5 * 60 * 1000, distanceToTarget01);

    if (durationMinutes >= 30 &&
        avg < targetHumidity  &&
        trend < trendThreshold)
    {
        #ifdef DEBUG_HUMIDIFIER_CONTROLLER
        {
            String info;
            info += "tgt=" + String(targetHumidity, 1);
            info += " avg=" + String(avg, 1);
            info += " tr=" + String(trend, 1);
            info += " dist=" + String(distanceToTarget, 1);
            info += " dist01=" + String(distanceToTarget01, 2);
            info += " thr=" + String(trendThreshold, 1);
            info += " cool=" + String(cooldownDuration / 60000) + "min";
            info += " dur=" + String(adaptiveHumidificationDuration_ON) + "s";
            sharedResourceManager->HumidifierLogs_Add(
                clientNTP->GetUnixTime(),
                SharedResourceManager::LogCode::DIAGNOSTICS_0,
                info.c_str()
            );
            SERIAL_PRINTF("(HumidifierController) Adaptive: tgt=%.1f avg=%.1f tr=%.1f dist=%.1f dist01=%.2f thr=%.1f cool=%dmin dur=%ds\n",
                targetHumidity, avg, trend, distanceToTarget, distanceToTarget01,
                trendThreshold, cooldownDuration / 60000, adaptiveHumidificationDuration_ON);
        }
        #endif

        humidifierState = true;
        ToggleHumidifier(humidifierState);
        sharedResourceManager->HumidifierLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ON);
        delay(adaptiveHumidificationDuration_ON * 1000);

        adaptiveHumidificationDuration_ON += 10;
        adaptiveHumidificationDuration_ON = constrain(adaptiveHumidificationDuration_ON, 10, 60);

        humidifierState = false;
        ToggleHumidifier(humidifierState);
        sharedResourceManager->HumidifierLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
        delay(cooldownDuration);
    }
    else
    {
        if (humidifierState)
        {
            humidifierState = false;
            ToggleHumidifier(humidifierState);
            sharedResourceManager->HumidifierLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);
        }

        adaptiveHumidificationDuration_ON = 10;
        delay(60 * 1000);
    }
}

void HumidifierController::MaintainHumidityTimer(bool &humidifierState, int humidifierOnDuration, int humidifierOffDuration)
{
    if (!humidifierState)
    {
        humidifierState = true;
        ToggleHumidifier(humidifierState);
        sharedResourceManager->HumidifierLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::ON);

        // Keep the humidifier ON for the specified duration
        delay(humidifierOnDuration);
    }
    else
    {
        humidifierState = false;
        ToggleHumidifier(humidifierState);
        sharedResourceManager->HumidifierLogs_Add(clientNTP->GetUnixTime(), SharedResourceManager::LogCode::OFF);

        // Keep the humidifier OFF for the specified duration
        delay(humidifierOffDuration);
    }
}