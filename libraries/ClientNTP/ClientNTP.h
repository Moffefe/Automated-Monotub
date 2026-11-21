#ifndef ClientNTP_h
#define ClientNTP_h

#include <stdint.h>
#include <mutex>
#include <vector>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <StateMachineRTOS.h>
#include <SharedResourceManager.h>
#include <ToolBox.h>



class ClientNTP : StateMachineRTOS
{
public:
    enum State
	{
        Start,                      //After boot
        NotSynced,                      // There was a problem with the last connection or never successfully connected
        Synced,                             // Successfully connected to the NTP server and retrieved time
        SyncAttempt                             // Currently attempting to connect to the NTP server
	};



    virtual ~ClientNTP() {};
    void begin(SharedResourceManager*);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

    std::time_t GetUnixTime();              // Returns the current Unix time (seconds since epoch)
    std::tm GetLocalTime();                     // Returns the local time based on the time zone set
    int32_t GetUtcOffsetSeconds();
    String GetUtcTimeString();
    String GetLocalTimeString();
    void SetUnixTime(std::time_t);

private:
    SharedResourceManager* sharedResourceManager;
    std::recursive_mutex mutex;       // Mutex for protecting shared resources (unixTime, etc.)
    uint64_t unixTime;                  // Stores the current Unix time (synced from the NTP server)
    uint64_t syncMillis;                    // The millis() value at the moment we receive a response from the NTP server

    void DeleteStateInit(uint8_t, void*);
    void StartProcedure();
    void NotSyncedProcedure();
	void SyncedProcedure();
    void SyncAttemptProcedure();
};

#endif