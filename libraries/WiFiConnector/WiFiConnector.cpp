#include "Arduino.h"
#include "WiFiConnector.h"



void WiFiConnector::begin(SharedResourceManager* sharedResourceManager)
{
    this->sharedResourceManager = sharedResourceManager;

    std::function<void()>* stateProcedures = new std::function<void()>[1];
    stateProcedures[0] = std::bind(&WiFiConnector::ScanConnectionProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::ScanConnection, "WiFiConnector", 4096);
}

void WiFiConnector::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void WiFiConnector::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void WiFiConnector::DeleteStateInit(uint8_t state, void* stateInit)
{
    // WiFiConnector does not have any states with initialization parameters to delete
}

void WiFiConnector::ScanConnectionProcedure()
{
    while (true)
    {
        #ifdef DEBUG_WIFI_CONNECTOR
        SERIAL_PRINTLN("(WiFiConnector) Checking connection.");
        #endif

        if (!ToolBox::IsNetworkAvailable())
        {
            SERIAL_PRINTLN("(WiFiConnector) WiFi connection lost. Attempting to reconnect...");

            if (mdnsRunning)
            {
                MDNS.end();
                mdnsRunning = false;
            }

            WiFi.disconnect(true);      // `true` means WiFi should be fully disabled
            delay(3000);                    // Give it some time to fully disconnect

            std::string savedSSID = sharedResourceManager->NetworkSSID_Get();
            std::string savedPass = sharedResourceManager->NetworkPassword_Get();
            if (!savedSSID.empty())
            {
                int retries = 5;
                unsigned long tryDuration = 60000;

                for (int i = 0; i < retries; ++i)
                {
                    if (ToolBox::IsStationConnected()) break;

                    SERIAL_PRINTF("(WiFiConnector) Connecting to SSID: %s\n", savedSSID.c_str());
                    WiFi.disconnect(true);
                    delay(3000);
                    WiFi.mode(WIFI_STA);
                    WiFi.begin(savedSSID.c_str(), savedPass.c_str());

                    unsigned long start = millis();
                    while (millis() - start < tryDuration)
                    {
                        if (ToolBox::IsStationConnected()) break;

                        delay(1000);
                        SERIAL_PRINT(".");
                    }
                    SERIAL_PRINTLN();
                }
            }

            if (!ToolBox::IsNetworkAvailable())
            {
                SERIAL_PRINTLN("(WiFiConnector) Starting AP mode...");
                WiFi.mode(WIFI_AP);
                String apName = String(sharedResourceManager->UnitName_Get().c_str()) + "_AP";
                WiFi.softAP(apName.c_str(), "12345678");
            }

            if (ToolBox::IsStationConnected())
            {
                SERIAL_PRINTLN("(WiFiConnector) Connected in STA mode.");
                SERIAL_PRINT("IP: ");
                SERIAL_PRINTLN(WiFi.localIP());
            }
            else if (ToolBox::IsAccessPointMode())
            {
                SERIAL_PRINTLN("(WiFiConnector) Running in AP mode.");
                SERIAL_PRINT("AP IP: ");
                SERIAL_PRINTLN(WiFi.softAPIP());
            }
            else
            {
                SERIAL_PRINTLN("(WiFiConnector) Reconnection failed.");
            }
        }

        if (ToolBox::IsStationConnected() && !mdnsRunning)
        {
            if (MDNS.begin(sharedResourceManager->UnitName_Get().c_str()))
            {
                SERIAL_PRINTLN("(WiFiConnector) mDNS service started.");
                MDNS.addService("http", "tcp", 80);
                mdnsRunning = true;
            }
            else
            {
                SERIAL_PRINTLN("(WiFiConnector) Failed to start mDNS.");
            }
        }

        delay(1000);
    }
}