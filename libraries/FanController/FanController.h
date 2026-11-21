#ifndef FanController_h
#define FanController_h

#include <stdint.h>
#include <vector>
#include <memory>
#include <StateMachineRTOS.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <CycleTimeBind.h>
#include <ToolBox.h>



class FanController : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        Working
	};



    virtual ~FanController() {};
    void begin(ClientNTP*, SharedResourceManager*, CycleTimeBind*, uint8_t);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;
    CycleTimeBind* cycleTimeBind;
    uint8_t fanPin;

    void DeleteStateInit(uint8_t, void*);
    void ToggleFan(bool);

	void BootProcedure();
    void WorkingProcedure();
};

#endif