#include "Arduino.h"
#include "MainController.h"



void MainController::begin()
{
    sharedResourceManager = NULL;
    clientNTP             = NULL;
    cycleTimeBind         = NULL;
    wifiConnector         = NULL;
    sensorReader          = NULL;
    serverController      = NULL;
    clusterManager        = NULL;
    lightController       = NULL;
    heatpadController     = NULL;
    fanController         = NULL;
    humidifierController  = NULL;

    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[(uint8_t)State::factoryReset] = std::bind(&MainController::FactoryResetProcedure, this);
    stateProcedures[(uint8_t)State::boot]         = std::bind(&MainController::BootProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::factoryReset, "MainController", 4096);
}

void MainController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void MainController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void MainController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // MainController does not have any states with initialization parameters to delete
}

void MainController::FactoryResetProcedure()
{
    Serial.println("(MainController) Checking factory reset – press BOOT...");

    const int bootButtonPin = 0;   // GPIO0
    pinMode(bootButtonPin, INPUT_PULLUP);
    unsigned long start = millis();
    bool resetTriggered = false;
    while (millis() - start < 7000)
    {
        if (digitalRead(bootButtonPin) == LOW)
        {
            resetTriggered = true;
            break;
        }
        delay(50);
    }

    if (resetTriggered)
    {
        Serial.println("(MainController) Factory reset triggered! Erasing NVS...");
        nvs_flash_erase();
        nvs_flash_init();

        delay(1000);
        Serial.println("(MainController) Restarting device...");
        esp_restart();
    }
    else
    {
        Serial.println("(MainController) Factory reset skipped, continuing to BOOT");
        SetState(State::boot);
    }

    WaitForNextState(portMAX_DELAY);
}

// Remember that the ESP32-S3 Zero has a different pin layout than the regular version.
//  - You need to enable USB CDC in Arduino: https://www.waveshare.com/wiki/ESP32-S3-Zero
//  - You need to use an external UART-to-USB converter to view debug logs
//  - You need to disconnect the TX/RX lines each time you want to flash the program
//  - When using a UART converter, you need to reopen the monitor window in Arduino each time the microcontroller restarts. It may be helpful to add a short delay at the very beginning of the program to give you time to do this
//  - The default I2C (Wire) pins differ on the Zero, change them explicitly (e.g., SDA = GPIO1, SCL = GPIO2).
void MainController::BootProcedure()
{
    if (sharedResourceManager == NULL)
    {
        Serial.println("(MainController) boot start");

        sharedResourceManager = new SharedResourceManager();
        sharedResourceManager->begin();

        // ⏱ Poczekaj aż ResourceManager zakończy Boot i przejdzie do Sharing
        SharedResourceManager::State currentState;
        void* stateInit = nullptr;
        do {
            sharedResourceManager->GetState(&currentState, &stateInit);
            delay(100);
        } while (currentState != SharedResourceManager::Sharing);

        // aktywacja strefy czasowej
        std::string tzName = sharedResourceManager->TimezoneLocation_Get();
        std::string tzString = ToolBox::GetTimezoneStringByName(tzName.c_str());
        if (!tzString.empty())
        {
            ToolBox::ApplyTimezone(tzString.c_str());
            Serial.printf("(MainController) Timezone set: %s -> %s\n", tzName.c_str(), tzString.c_str());
        }

        clientNTP = new ClientNTP();
        clientNTP->begin(sharedResourceManager);

        cycleTimeBind = new CycleTimeBind();
        cycleTimeBind->begin(clientNTP, sharedResourceManager);

        wifiConnector = new WiFiConnector();
        wifiConnector->begin(sharedResourceManager);

        auto mutexI2C = std::make_shared<std::recursive_mutex>();        // Create a mutex for I2C access to share it with all classes that control I2C devices
        Wire.begin(1, 2);                                                     // Initialize I2C here; other classes should not reinitialize it

        sensorReader = new SensorReader();
        sensorReader->begin(mutexI2C, clientNTP, sharedResourceManager);

        serverController = new ServerController();                                                      // those two need references to each other.
        clusterManager   = new ClusterManager();                                                        // Create both objects first, then call begin()
        serverController->begin(clientNTP, sharedResourceManager, cycleTimeBind, clusterManager);       //
        clusterManager->begin(sharedResourceManager, serverController);                                 //

        fanController = new FanController();
        fanController->begin(clientNTP, sharedResourceManager, cycleTimeBind, 4);

        humidifierController = new HumidifierController();
        humidifierController->begin(clientNTP, sharedResourceManager, cycleTimeBind, 5);

        lightController = new LightController();
        lightController->begin(clientNTP, sharedResourceManager, cycleTimeBind, 6);

        heatpadController = new HeatpadController();
        heatpadController->begin(clientNTP, sharedResourceManager, cycleTimeBind, 7);

        Serial.println("(MainController) boot complete");
    }

    WaitForNextState(portMAX_DELAY);
}