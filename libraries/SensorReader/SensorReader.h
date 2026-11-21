#ifndef SensorReader_h
#define SensorReader_h

#include <stdint.h>
#include <vector>
#include <StateMachineRTOS.h>
#include <SensirionI2cScd4x.h>
#include <SensirionI2cSht3x.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <ToolBox.h>



// Handles multiple types of sensors. Suitable models for high-humidity environments include:
// - SCD4x
// - SHT31
// - SHT35
// - AHT30
class SensorReader : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        Measure
	};



    virtual ~SensorReader() {};
    void begin(std::shared_ptr<std::recursive_mutex>, ClientNTP*, SharedResourceManager*);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

private:
    std::shared_ptr<std::recursive_mutex> mutexI2C;
    SensirionI2cScd4x* scd4x;
    SensirionI2cSht3x* sht3x;
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;

    void DeleteStateInit(uint8_t, void*);

    void InitSensors();
    bool ReadSensors(float&, float&, float&);
    void SoftResetSensors();

	void BootProcedure();
    void MeasureProcedure();
};

#endif