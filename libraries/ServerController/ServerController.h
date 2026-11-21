#ifndef ServerController_h
#define ServerController_h

#include <stdint.h>
#include <vector>
#include <atomic>
#include <StateMachineRTOS.h>
#include <Eventfun.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <ClientNTP.h>
#include <SharedResourceManager.h>
#include <CycleTimeBind.h>
#include <ClusterManager.h>
#include <ToolBox.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <chartjs.h>
#include <AppConfig.h>



class ServerController : StateMachineRTOS
{
public:
    enum State
	{
        Boot,
        Serving
	};



    virtual ~ServerController() {};
    void begin(ClientNTP*, SharedResourceManager*, CycleTimeBind*, ClusterManager*);
    void SetState(State,  void*  = NULL);
    void GetState(State*, void** = NULL);

    void UpdateClientActivity() { lastClientActivity = millis(); }
    bool IsUserActive(unsigned long timeoutMs = 30000) { return (millis() - lastClientActivity < timeoutMs); }

private:
    WebServer* server;
    ClientNTP* clientNTP;
    SharedResourceManager* sharedResourceManager;
    CycleTimeBind* cycleTimeBind;
    ClusterManager* clusterManager;
    String login;
    String password;
    std::atomic<unsigned long> lastClientActivity;

    void DeleteStateInit(uint8_t, void*);

    bool PreRequest();
    String ExternalResources();
    String BackgroundFogEffect();
    String Server_TopBar(const String&, const String&);

    ////////////////////////// Root page //////////////////////////
    String Server_Root_Start();
    String Server_Root_Buttons();
    String Server_Root_End();
    void Server_Handle_Root();

    void Server_Handle_UpdateUnitName();



    ////////////////////////// Sensor page //////////////////////////
    String Server_Sensor_Start();
    String GetAirLogs_JS();
    String Server_Sensor_AirLogs();
    String Server_Sensor_End();
    void Server_Handle_Sensor();



    ////////////////////////// Control page //////////////////////////
    String Server_Control_Start();
    String Server_Control_ControlButtons();
    String Server_Control_Cycle();
    String Server_Control_End();
    void Server_Handle_Control();

    void Server_Handle_ToggleLight();
    void Server_Handle_SetLight();
    void Server_Handle_ToggleHumid();
    void Server_Handle_SetHumid();
    void Server_Handle_ToggleIncubation();
    void Server_Handle_SetIncubation();
    void Server_Handle_UpdateCycle();
    void Server_Handle_ResetCycle();
    void Server_Handle_ImportCycle();
    void Server_Handle_AddDay();
    void Server_Handle_RemoveDay();
    void Server_Handle_CloneDay();
    void Server_Handle_UpdateLight();
    void Server_Handle_GetDayData();
    void Server_Handle_AddHour();
    void Server_Handle_RemoveHour();
    void Server_Handle_UpdateHour();

    

    ////////////////////////// Log page //////////////////////////
    String Server_Log_Start();
    String Server_Log_DeviceLogs();
    String Server_Log_End();
    void Server_Handle_Log();



    ////////////////////////// Cluster page //////////////////////////
    String Server_Cluster_Start();
    String Server_Cluster_End();
    void Server_Handle_Cluster();
    void Server_Handle_ClusterScan();
    void Server_Handle_SendCommandToAll_Restart();
    void Server_Handle_SendCommandToAll_Settings();


    ////////////////////////// System page //////////////////////////
    String Server_System_Start();
    String Server_System_Authorization();
    String Server_System_Firmware();
    String Server_System_Network();
    String Server_System_Localization();
    String Server_System_End();
    void Server_Handle_System();

    void Server_Handle_Auth();
    void Server_Handle_Restart();
    void Server_Handle_OTAUpload();
    void Server_Handle_OTAResult();
    void Server_Handle_Network();
    void Server_Handle_TimeAndLoc();

    
    ////////////////////////// Other page //////////////////////////
    void Server_Handle_Diagnostics();
    void Server_Handle_Diagnostics_HumidityMasked();
    void Server_Handle_ChartJS();



	void BootProcedure();
    void ServingProcedure();
};

#endif