#include "Arduino.h"
#include "ClusterManager.h"
#include "ServerController.h"

#define GuardMutex std::lock_guard<std::recursive_mutex> guardMutex (mutex)



void ClusterManager::begin(SharedResourceManager* sharedResourceManager, ServerController* serverController)
{
    this->sharedResourceManager = sharedResourceManager;
    this->serverController      = serverController;
    this->scanIntervalMs        = 10000;
    this->scanImmediate         = false;
    this->pendingCommand.type   = CommandType::NoCommand;
    this->lastCommandResult     = CommandResult::NoResult;

    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&ClusterManager::BootProcedure, this);
    stateProcedures[1] = std::bind(&ClusterManager::ScanAndCommandProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "ClusterManager", 8192);
}

void ClusterManager::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void ClusterManager::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

std::vector<ClusterManager::ClusterNode> ClusterManager::GetClusterNodes()
{
    GuardMutex;
    return nodes;
}

void ClusterManager::ScanNow()
{
    scanImmediate = true;
}

ClusterManager::CommandResult ClusterManager::GetLastCommandResult()
{
    GuardMutex;
    CommandResult result = (CommandResult)lastCommandResult;
    lastCommandResult = CommandResult::NoResult;
    return result;
}

/// @brief Send command to all nodes (except local one) to clone all setting from local node
void ClusterManager::SendCommandToAll_Settings()
{
    GuardMutex;
    pendingCommand.type = CommandType::ToAll_Settings;
    scanImmediate = true;
}

/// @brief Send command to all nodes (except local one) to perform software reset
void ClusterManager::SendCommandToAll_Restart()
{
    GuardMutex;
    pendingCommand.type = CommandType::ToAll_Restart;
    scanImmediate = true;
}

void ClusterManager::ScanNodes()
{
    std::vector<ClusterNode> newNodes;
    int n = MDNS.queryService("http", "tcp");

    for (int i = 0; i < n; i++)
    {
        ClusterNode node;
        node.hostName = MDNS.hostname(i);
        node.url = "http://" + node.hostName + ".local";
        node.ip = MDNS.address(i);
        node.online = true;
        node.lastSeen = millis();
        newNodes.push_back(node);
    }

    {
        GuardMutex;
        nodes = std::move(newNodes);
    }
}

bool ClusterManager::SendHttpPostToNode(const IPAddress& ip, const String& endpoint, const String& body)
{
    String fullUrl = "http://" + ip.toString() + endpoint;

    String login    = sharedResourceManager->AccountLogin_Get().c_str();
    String password = sharedResourceManager->AccountPassword_Get().c_str();
    String auth     = base64::encode(login + ":" + password);

    HTTPClient http;
    http.setTimeout(5000);
    http.begin(fullUrl);
    http.addHeader("Authorization", "Basic " + auth);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    String response = http.getString();
    http.end();

    #ifdef DEBUG_CLUSTER_MANAGER
    String dbg;
    dbg += "[CLUSTER] HTTP POST\n";
    dbg += " URL: " + fullUrl + "\n";
    dbg += " BODY: ";
    dbg += (body.length() > 200 ? body.substring(0, 200) + "..." : body);
    dbg += "\n";
    dbg += " RESPONSE CODE: " + String(code) + "\n";
    dbg += " RESPONSE BODY: ";
    dbg += (response.length() > 200 ? response.substring(0, 200) + "..." : response) + "\n";
    SERIAL_PRINT(dbg);
    #endif

    return (code >= 200 && code < 300);
}

void ClusterManager::ExecuteCommand()
{
    // Pobierz komendę
    Command localCommand;
    {
        GuardMutex;
        localCommand = pendingCommand;
        pendingCommand.type = CommandType::NoCommand;
    }

    if (localCommand.type == CommandType::NoCommand)
        return;

    // Pobierz nody
    std::vector<ClusterNode> localNodes;
    {
        GuardMutex;
        localNodes = nodes;
    }

    bool allOk = true;

    for (auto& node : localNodes)
    {
        // Pomijamy lokalny node
        if (node.ip == WiFi.localIP())
            continue;

        switch (localCommand.type)
        {
            case CommandType::ToAll_Settings:
            {
                // === ŚWIATŁO ===
                bool turnOffLight = sharedResourceManager->TurnOffLight_Get();
                allOk &= SendHttpPostToNode(node.ip, "/setLight", turnOffLight ? "1" : "0");

                // === ADAPTIVE HUMIDIFICATION ===
                bool adaptiveHumidification = sharedResourceManager->AdaptiveHumidification_Get();
                allOk &= SendHttpPostToNode(node.ip, "/setHumid", adaptiveHumidification ? "1" : "0");

                // === INCUBATION MODE ===
                bool incubation = sharedResourceManager->Incubation_Get();
                allOk &= SendHttpPostToNode(node.ip, "/setIncubation", incubation ? "1" : "0");

                // === CYCLE ===
                SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
                allOk &= SendHttpPostToNode(node.ip, "/importCycle", cycle.ToJson());

                // === Write settings to persistent memory
                //allOk &= SendHttpPostToNode(node.ip, "/updateCycle", "{}");

                break;
            }

            case CommandType::ToAll_Restart:
            {
                allOk &= SendHttpPostToNode(node.ip, "/restart", "{}");
                break;
            }
        }
    }

    {
        GuardMutex;
        lastCommandResult = allOk ? CommandResult::Success : CommandResult::Error;
    }

    #ifdef DEBUG_CLUSTER_MANAGER
    String dbg;
    dbg += "[CLUSTER] ExecuteCommand()\n";
    dbg += "Command: " + String((int)localCommand.type) + "\n";
    dbg += "Nodes: " + String(localNodes.size()) + "\n";
    dbg += "FINAL RESULT: ";
    dbg += allOk ? "SUCCESS\n" : "ERROR\n";
    SERIAL_PRINT(dbg);
    #endif
}

void ClusterManager::DeleteStateInit(uint8_t state, void* stateInit)
{
    // Brak dodatkowych danych inicjalizacyjnych do kasowania
}

void ClusterManager::BootProcedure()
{
    while (!ToolBox::IsStationConnected())
        delay(1000);

    SetState(State::ScanAndCommand);
    WaitForNextState(portMAX_DELAY);
}

void ClusterManager::ScanAndCommandProcedure()
{
    while (true)
    {
        // oczekiwanie na timeout lub flage natychmiastowego skanu
        unsigned long waitStart = millis();
        while (!scanImmediate && millis() - waitStart < scanIntervalMs)
            delay(200);
        scanImmediate = false;

        // czekaj na połączenie i aktywnego użytkownika
        while (!ToolBox::IsStationConnected() || !serverController->IsUserActive())
            delay(200);

        ScanNodes();
        ExecuteCommand();
    }

    WaitForNextState(portMAX_DELAY);
}