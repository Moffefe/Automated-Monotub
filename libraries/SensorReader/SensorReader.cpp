#include "Arduino.h"
#include "SensorReader.h"

#define GuardMutex std::lock_guard<std::recursive_mutex> guardMutex (*mutexI2C)



void SensorReader::begin(std::shared_ptr<std::recursive_mutex> mutexI2C, ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager)
{
    this->mutexI2C              = mutexI2C;
    this->scd4x                 = NULL;
    this->sht3x                 = NULL;
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&SensorReader::BootProcedure, this);
    stateProcedures[1] = std::bind(&SensorReader::MeasureProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "SensorReader", 4096);
}

void SensorReader::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void SensorReader::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void SensorReader::DeleteStateInit(uint8_t state, void* stateInit)
{
    // SensorReader does not have any states with initialization parameters to delete
}

void SensorReader::InitSensors()
{
    GuardMutex;

    if (scd4x != NULL) return;

    int retryCount = 3;
    int retryDelay = 500;

    // Create SCD4x sensor
    {
        scd4x = new SensirionI2cScd4x();
        scd4x->begin(Wire, SCD41_I2C_ADDR_62);

        if (ToolBox::CheckI2CDevice(SCD41_I2C_ADDR_62))
        {
            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->wakeUp() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);

            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->stopPeriodicMeasurement() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);

            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->reinit() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);

            // Set altitude in meters for Cieszanow
            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->setSensorAltitude(218) == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100); 

            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->setAutomaticSelfCalibrationEnabled(0) == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);

            for (int i = 0; i < retryCount; ++i)
            {
                if (scd4x->startPeriodicMeasurement() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);
        }
    }

    // Create SHT3x sensor
    {
        sht3x = new SensirionI2cSht3x();
        sht3x->begin(Wire, SHT30_I2C_ADDR_44);

        if (ToolBox::CheckI2CDevice(SHT30_I2C_ADDR_44))
        {
            for (int i = 0; i < retryCount; ++i)
            {
                if (sht3x->stopMeasurement() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);

            for (int i = 0; i < retryCount; ++i)
            {
                if (sht3x->softReset() == 0)
                    break;
                delay(retryDelay); 
            }
            delay(100);
        }
    }
}

bool SensorReader::ReadSensors(float &temperature, float &humidity, float &co2)
{
    GuardMutex;

    bool result = false;
    temperature = -1;
    humidity    = -1;
    co2         = -1;

    // Try to read SCD4x sensor
    if (ToolBox::CheckI2CDevice(SCD41_I2C_ADDR_62))
    {
        bool isDataReady = false;
        uint16_t error = scd4x->getDataReadyStatus(isDataReady);

        if (error == 0 && isDataReady)
        {
            uint16_t co2Integer;
            error = scd4x->readMeasurement(co2Integer, temperature, humidity);
            co2 = static_cast<float>(co2Integer);

            if (error == 0          &&
                co2 > 0             &&
                !isnan(temperature) &&
                !isnan(humidity)    &&
                !isnan(co2))
            {
                result = true;
            }
        }
    }

    // Try to read SHT3x sensor
    if (ToolBox::CheckI2CDevice(SHT30_I2C_ADDR_44))
    {
        temperature = -1;
        humidity    = -1;
        co2         = -1;

        uint16_t error = sht3x->measureSingleShot(REPEATABILITY_MEDIUM, false, temperature, humidity);
        if (error == 0          &&
            !isnan(temperature) &&
            !isnan(humidity))
        {
            result = true;
        }
    }

    if (!result)
    {
        temperature = -1;
        humidity    = -1;
        co2         = -1;
    }
    return result;
}

void SensorReader::SoftResetSensors()
{
    GuardMutex;

    int retryCount = 3;
    int retryDelay = 500;

    // SCD4x sensor
    if (ToolBox::CheckI2CDevice(SCD41_I2C_ADDR_62))
    {

    }

    // SHT3x sensor
    if (ToolBox::CheckI2CDevice(SHT30_I2C_ADDR_44))
    {
        for (int i = 0; i < retryCount; ++i)
        {
            if (sht3x->stopMeasurement() == 0)
                break;
            delay(retryDelay); 
        }
        delay(100);

        for (int i = 0; i < retryCount; ++i)
        {
            if (sht3x->softReset() == 0)
                break;
            delay(retryDelay); 
        }
        delay(100);
    }
}

void SensorReader::BootProcedure()
{
    InitSensors();

    // Wait for NTP client to sync the time
    for (size_t i = 0; i < 5; i++)
    {
        ClientNTP::State ntpState;
        clientNTP->GetState(&ntpState);

        if (ntpState == ClientNTP::State::Synced)
            break;

        delay(2000);
    }
    
    SetState(State::Measure);
    WaitForNextState(portMAX_DELAY);
}

void SensorReader::MeasureProcedure()
{
    const unsigned long delayRecent = 5000;             // 5-second delay
    const unsigned long delayLong = 300000;         // 5-minute delay
    unsigned long lastRecent = millis();
    unsigned long lastLong = lastRecent;
    int retryCount = 10;
    int retryDelay = 1000;

    while (true)
    {
        // Read sensor every 5 seconds
        if (millis() - lastRecent >= delayRecent)
        {
            lastRecent = millis();

            float temperature = -1;
            float humidity    = -1;
            float co2         = -1;
            for (int i = 0; i < retryCount; ++i)
            {
                if (ReadSensors(temperature, humidity, co2))
                    break;
                delay(retryDelay);
            }

            sharedResourceManager->AirLogsRecent_Add(clientNTP->GetUnixTime(), temperature, humidity, co2);

            if (temperature == -1 || humidity == -1)
                SoftResetSensors();
        }

        // Read sensor every 5 minutes
        if (millis() - lastLong >= delayLong)
        {
            lastLong = millis();

            float temperature = -1;
            float humidity    = -1;
            float co2         = -1;
            for (int i = 0; i < retryCount; ++i)
            {
                if (ReadSensors(temperature, humidity, co2))
                    break;
                delay(retryDelay);
            }

            sharedResourceManager->AirLogs_Add(clientNTP->GetUnixTime(), temperature, humidity, co2);

            if (temperature == -1 || humidity == -1)
                SoftResetSensors();
        }

        delay(100);
    }
}