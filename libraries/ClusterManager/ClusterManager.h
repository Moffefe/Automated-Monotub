#ifndef ClusterManager_h
#define ClusterManager_h

#include <stdint.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <base64.h>
#include <StateMachineRTOS.h>
#include <SharedResourceManager.h>
#include <ToolBox.h>



class ServerController;     // forward declaration to resolve cyclic dependency

class ClusterManager : StateMachineRTOS
{
public:
    enum State
    {
        Boot,
        ScanAndCommand
    };

    enum CommandType
    {
        NoCommand,
        ToAll_Settings,
        ToAll_Restart
    };

    enum CommandResult
    {
        NoResult,
        Success,
        Error
    };

    struct Command
    {
        CommandType type = CommandType::NoCommand;
    };

    // Struktura reprezentująca jedną komorę w klastrze
    struct ClusterNode
    {
        String hostName;         // np. "fc-01"
        String url;              // np. "http://fc-01.local"
        IPAddress ip;
        bool online;             // true jeśli ostatni skan się udał
        uint32_t lastSeen;       // millis() kiedy ostatnio widziana
    };

    virtual ~ClusterManager() {};
    void begin(SharedResourceManager*, ServerController*);

    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

    std::vector<ClusterNode> GetClusterNodes();             // pobranie listy aktualnych komór
    void ScanNow();
    CommandResult GetLastCommandResult();
    void SendCommandToAll_Settings();
    void SendCommandToAll_Restart();

private:
    SharedResourceManager* sharedResourceManager;
    ServerController* serverController;
    std::recursive_mutex mutex;                             // ochrona dostępu do nodes

    std::vector<ClusterNode> nodes;                         // aktualnie znane komory
    uint32_t scanIntervalMs;                                // co ile ms wykonywać skan/obsługiwać kolejkę komend
    std::atomic<bool> scanImmediate;                            // Used in ScanNow()
    Command pendingCommand;
    int lastCommandResult;

    void ScanNodes();
    bool SendHttpPostToNode(const IPAddress& ip, const String& endpoint, const String& body);
    void ExecuteCommand();

    void DeleteStateInit(uint8_t, void*);
    void BootProcedure();
    void ScanAndCommandProcedure();
};

#endif