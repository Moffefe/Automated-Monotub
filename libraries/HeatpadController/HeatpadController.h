#ifndef HeatpadController_h
#define HeatpadController_h

#include <stdint.h>
#include <vector>
#include <memory>
#include <StateMachineRTOS.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <CycleTimeBind.h>
#include <ToolBox.h>



class HeatpadController : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        KeepTemperature
	};



    virtual ~HeatpadController() {};
    void begin(ClientNTP*, SharedResourceManager*, CycleTimeBind*, uint8_t);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;
    CycleTimeBind* cycleTimeBind;
    uint8_t heatpadPin;

    void DeleteStateInit(uint8_t, void*);
    void ToggleHeatpad(bool);

	void BootProcedure();
    void KeepTemperatureProcedure();
};

#endif