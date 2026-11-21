#ifndef LightController_h
#define LightController_h

#include <stdint.h>
#include <vector>
#include <memory>
#include <StateMachineRTOS.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <CycleTimeBind.h>
#include <ToolBox.h>



class LightController : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        LightCycling
	};



    virtual ~LightController() {};
    void begin(ClientNTP*, SharedResourceManager*, CycleTimeBind*, uint8_t);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;
    CycleTimeBind* cycleTimeBind;
    uint8_t ledStripPin;

    void DeleteStateInit(uint8_t, void*);
    void ToggleLight(bool);

	void BootProcedure();
    void LightCyclingProcedure();
};

#endif