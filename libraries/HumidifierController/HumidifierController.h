#ifndef HumidifierController_h
#define HumidifierController_h

#include <stdint.h>
#include <vector>
#include <memory>
#include <StateMachineRTOS.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <CycleTimeBind.h>
#include <ToolBox.h>

class HumidifierController : StateMachineRTOS
{
public:
    enum State
    {
        Boot,
        MaintainHumidity
    };

    virtual ~HumidifierController() {};
    void begin(ClientNTP*, SharedResourceManager*, CycleTimeBind*, uint8_t);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;
    CycleTimeBind* cycleTimeBind;
    uint8_t humidifierPin;
    int adaptiveHumidificationDuration_ON;

    void DeleteStateInit(uint8_t, void*);
    void ToggleHumidifier(bool);

    void BootProcedure();
    void MaintainHumidityProcedure();

    void MaintainHumidityAdaptive(bool &);
    void MaintainHumidityTimer(bool &, int, int);
};

#endif