#ifndef CycleTimeBind_h
#define CycleTimeBind_h

#include <stdint.h>
#include <vector>
#include <StateMachineRTOS.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <ToolBox.h>



class CycleTimeBind : StateMachineRTOS
{
public:
    enum State
	{
        Working
	};



    virtual ~CycleTimeBind() {};
    void begin(ClientNTP*, SharedResourceManager*);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

    int LightHour_ON_Get();
    int LightHour_OFF_Get();
    float HeatpadTemp_ON_Get();
    float HeatpadTemp_OFF_Get();
    int FanMinutes_ON_Get();
    int FanMinutes_OFF_Get();
    int HumidifierMinutes_ON_Get();
    int HumidifierMinutes_OFF_Get();
    void GetCurrentDayAndHour(SharedResourceManager::FCSettingDay& day, SharedResourceManager::FCSettingHour& hour, int& currentDayIndex, int& currentHourIndex);
    void GetCurrentDayAndHour(SharedResourceManager::FCSettingDay& day, SharedResourceManager::FCSettingHour& hour);

private:
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;

    void DeleteStateInit(uint8_t, void*);
    void WorkingProcedure();
};

#endif