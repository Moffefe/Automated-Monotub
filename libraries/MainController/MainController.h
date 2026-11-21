#ifndef MainController_h
#define MainController_h

#include <stdint.h>
#include <vector>
#include <StateMachineRTOS.h>
#include <Eventfun.h>
#include <Wire.h>
#include <nvs_flash.h>
#include <SharedResourceManager.h>
#include <ClientNTP.h>
#include <CycleTimeBind.h>
#include <WiFiConnector.h>
#include <SensorReader.h>
#include <ServerController.h>
#include <ClusterManager.h>
#include <LightController.h>
#include <HeatpadController.h>
#include <FanController.h>
#include <HumidifierController.h>
#include <ToolBox.h>



class MainController : StateMachineRTOS
{
public:
    enum State
	{
        factoryReset,
        boot
	};



    virtual ~MainController() {};
    void begin();
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    SharedResourceManager* sharedResourceManager;
    ClientNTP* clientNTP;
    CycleTimeBind* cycleTimeBind;
    WiFiConnector* wifiConnector;
    SensorReader* sensorReader;
    ServerController* serverController;
    ClusterManager* clusterManager;
    LightController* lightController;
    HeatpadController* heatpadController;
    FanController* fanController;
    HumidifierController* humidifierController;

    void DeleteStateInit(uint8_t, void*);
    void FactoryResetProcedure();
	void BootProcedure();
};

#endif