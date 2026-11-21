#ifndef WiFiConnector_h
#define WiFiConnector_h

#include <stdint.h>
#include <vector>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <StateMachineRTOS.h>
#include <Eventfun.h>
#include <SharedResourceManager.h>
#include <ToolBox.h>



class WiFiConnector : StateMachineRTOS
{
public:
    enum State
	{
        ScanConnection
	};



    virtual ~WiFiConnector() {};
    void begin(SharedResourceManager*);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    SharedResourceManager* sharedResourceManager;
    bool mdnsRunning = false;

    void DeleteStateInit(uint8_t, void*);
	void ScanConnectionProcedure();
};

#endif