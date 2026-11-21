#include "Arduino.h"
#include "ServerController.h"



void ServerController::begin(ClientNTP* clientNTP, SharedResourceManager* sharedResourceManager, CycleTimeBind* cycleTimeBind, ClusterManager* clusterManager)
{
    this->server                = NULL;
    this->clientNTP             = clientNTP;
    this->sharedResourceManager = sharedResourceManager;
    this->cycleTimeBind         = cycleTimeBind;
    this->clusterManager        = clusterManager;
    this->login                 = sharedResourceManager->AccountLogin_Get().c_str();
    this->password              = sharedResourceManager->AccountPassword_Get().c_str();
    this->lastClientActivity    = 0;
    
    std::function<void()>* stateProcedures = new std::function<void()>[2];
    stateProcedures[0] = std::bind(&ServerController::BootProcedure, this);
    stateProcedures[1] = std::bind(&ServerController::ServingProcedure, this);
    StateMachineRTOS::begin(stateProcedures, State::Boot, "ServerController", 8192);
}

void ServerController::SetState(State newState, void* initializationData)
{
    StateMachineRTOS::SetState((uint8_t)newState, initializationData);
}

void ServerController::GetState(State* currentState, void** currentStateInit)
{
    uint8_t rawState;
    StateMachineRTOS::GetState(&rawState, currentStateInit);
    *currentState = static_cast<State>(rawState);
}

void ServerController::DeleteStateInit(uint8_t state, void* stateInit)
{
    // ServerController does not have any states with initialization parameters to delete
}

bool ServerController::PreRequest()
{
    if (!server->authenticate(login.c_str(), password.c_str()))
    {
        server->requestAuthentication();
        return false;
    }

    UpdateClientActivity();
    return true;
}

String ServerController::ExternalResources()
{
    if (ToolBox::IsStationConnected())
    {
        // Jesteśmy w trybie stacji – zwróć linie CDN
        return R"rawliteral(
        <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
        )rawliteral";
    }

    // W trybie AP – brak internetu → nie dodajemy zewnętrznych zasobów
    return "";
}

String ServerController::BackgroundFogEffect()
{
    if (!ToolBox::IsStationConnected())
    {
        // W trybie AP brak internetu – nie dodajemy efektu
        return "";
    }

    return R"rawliteral(
    <script>
    function loadScript(url, callback) {
        const script = document.createElement('script');
        script.src = url;
        script.onload = callback;
        script.onerror = () => {
            console.warn("Failed to load: " + url);
        };
        document.head.appendChild(script);
    }

    window.addEventListener('DOMContentLoaded', () => {
        loadScript('https://cdnjs.cloudflare.com/ajax/libs/three.js/r121/three.min.js', () => {
            loadScript('https://cdnjs.cloudflare.com/ajax/libs/vanta/0.5.24/vanta.fog.min.js', () => {
                if (typeof VANTA !== 'undefined' && VANTA.FOG) {
                    VANTA.FOG({
                        el: "body",
                        highlightColor: 0x5a73c9,   // jasny niebiesko-fioletowy
                        midtoneColor: 0x3f57a6,     // główny niebieski
                        lowlightColor: 0x1c2547,    // ciemny granat
                        baseColor: 0x2f3c6b,        // bazowy kolor tła
                        blurFactor: 0.6,
                        speed: 1.2,
                        zoom: 1.1
                    });
                } else {
                    console.warn("VANTA.FOG not available.");
                }
            });
        });
    });
    </script>
    )rawliteral";
}

String ServerController::Server_TopBar(const String& title, const String& backUrl)
{
    std::string unitName = sharedResourceManager->UnitName_Get();

    return R"rawliteral(
        <div class="topbar">
            <div class="topbar-content">
                <a class="back-button" href=")rawliteral" + backUrl + R"rawliteral(">
                    <i class="fas fa-arrow-left"></i> Back
                </a>
                <div class="topbar-title">)rawliteral" + title + R"rawliteral(</div>
                <div class="unit-chip">)rawliteral" + String(unitName.c_str()) + R"rawliteral(</div>
            </div>
        </div>

        <style>
            .topbar {
                max-width: calc(100% - 80px);
                margin: 5px auto 30px auto;
                background-color: #25335c;
                padding: 12px 20px;
                border-radius: 12px;
                box-shadow: 0 2px 6px rgba(0,0,0,0.3);
                position: sticky;
                top: 5px;
                z-index: 1000;
            }

            .topbar-content {
                display: flex;
                align-items: center;
                justify-content: space-between;
                position: relative;
            }

            .back-button {
                display: inline-flex;
                align-items: center;
                gap: 8px;
                padding: 8px 14px;
                border-radius: 8px;
                background-color: #3498db;
                font-size: 14px;
                font-weight: bold;
                text-decoration: none;
                color: #fff;
                transition: background-color 0.3s ease, transform 0.2s ease;
            }

            .back-button i {
                font-size: 16px;
            }

            .back-button:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }

            .topbar-title {
                position: absolute;
                left: 50%;
                transform: translateX(-50%);
                font-size: 18px;
                font-weight: bold;
                color: #fff;
            }

            .unit-chip {
                display: inline-block;
                padding: 4px 12px;
                font-size: 14px;
                font-weight: bold;
                color: #fff;
                background-color: #27ae60;
                border-radius: 20px;
                box-shadow: 0 2px 4px rgba(0,0,0,0.2);
            }
        </style>
    )rawliteral";
}

String ServerController::Server_Root_Start()
{
    std::string unitName = sharedResourceManager->UnitName_Get();

    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🍄 FC</title>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
        <script type="text/javascript">
            function openEditModal() {
                document.getElementById('editModal').style.display = 'flex';
            }

            function closeEditModal() {
                document.getElementById('editModal').style.display = 'none';
            }

            function saveUnitName() {
                const newName = document.getElementById('unitNameInput').value;
                if (!newName) {
                    alert("Name cannot be empty");
                    return;
                }

                fetch('/updateUnitName', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name: newName })
                }).then(async response => {
                    const text = await response.text();
                    if (response.ok) {
                        alert(text || "Unit name updated. Device will restart.");
                        closeEditModal();
                        setTimeout(() => location.reload(), 3000);
                    } else {
                        alert("Error: " + text);
                    }
                }).catch(err => {
                    alert("Request failed: " + err);
                });
            }
        </script>

        <style>
            body {
                background-color: #2f3c6b; /* spójne z System/Log */
                color: #f0f0f0;
                font-family: Arial, sans-serif;
                margin: 0;
                padding: 0;
                height: 100vh;
                display: flex;
                flex-direction: column;
                justify-content: center;
                align-items: center;
            }

            .unit-section {
                max-width: 800px;
                padding: 20px 40px;
                background-color: #3a4a82;
                border-radius: 12px;
                box-shadow: 0 4px 10px rgba(0,0,0,0.3);
                text-align: center;
                margin-bottom: 40px;
            }

            .unit-section h1 {
                margin: 0;
                color: #f8f8f8;
                font-size: 28px;
                display: inline-flex;
                align-items: center;
                gap: 10px;
            }

            .edit-btn {
                font-size: 18px;
                color: #3498db;
                background: none;
                border: none;
                cursor: pointer;
                transition: color 0.3s ease;
            }

            .edit-btn:hover {
                color: #2980b9;
            }

            .modal {
                display: none;
                position: fixed;
                z-index: 1000;
                left: 0;
                top: 0;
                width: 100%;
                height: 100%;
                background-color: rgba(0,0,0,0.6);
                display: flex;
                justify-content: center;
                align-items: center;
            }

            .modal-content {
                background-color: #1a1a2e;
                margin: 15% auto;
                padding: 20px;
                border-radius: 12px;
                width: 300px;
                box-shadow: 0 4px 10px rgba(0,0,0,0.4);
                color: #f0f0f0;
                text-align: center;
            }

            .close-btn {
                float: right;
                font-size: 20px;
                font-weight: bold;
                color: #aaa;
                cursor: pointer;
            }

            .close-btn:hover {
                color: #fff;
            }

            .edit-input {
                width: 90%;
                padding: 10px;
                margin-top: 15px;
                border: none;
                border-radius: 8px;
                font-size: 16px;
            }

            .save-button {
                margin-top: 15px;
                padding: 10px 20px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 8px;
                cursor: pointer;
                transition: background-color 0.3s ease;
            }

            .save-button:hover {
                background-color: #2980b9;
            }
        </style>
    </head>
    <body>
        <div class="unit-section">
            <h1>)rawliteral";

    html += String(unitName.c_str());

    html += R"rawliteral(
                <button class="edit-btn" onclick="openEditModal()">✏️</button>
            </h1>
        </div>

        <div id="editModal" class="modal" style="display:none; position:fixed; top:0; left:0; width:100%; height:100%;
            background:rgba(0,0,0,0.6); z-index:1000; align-items:center; justify-content:center;">

            <div style="background:#2c3760; color:#ffffff; padding:25px; border-radius:12px; width:90%; max-width:400px; 
                border:1px solid #5f6eaa; box-shadow:0 4px 15px rgba(0,0,0,0.6); position:relative; text-align:center;">

                <span onclick="closeEditModal()" style="position:absolute; top:10px; right:15px; font-size:24px; 
                    cursor:pointer; color:#ccc;">&times;</span>

                <h2 style="margin-top:0; color:#d0d6ff;">
                    <i class="fas fa-pen"></i> Edit Unit Name
                </h2>

                <p style="font-size:14px; color:#ffd580; margin-bottom:20px;">
                    <i class="fas fa-exclamation-triangle"></i>
                    Confirming will cause the device to restart
                </p>

                <input type="text" id="unitNameInput" placeholder="Enter new name"
                    style="width:90%; padding:10px; border-radius:8px; border:none; font-size:16px; 
                        background:#3b4b7a; color:#fff; border:1px solid #5f6eaa;">

                <button class="save-button" onclick="saveUnitName()" 
                    style="margin-top:20px; padding:10px 20px; background:#27ae60; color:white; border:none;
                        border-radius:8px; cursor:pointer; font-size:16px; transition:background-color 0.3s ease;">
                    <i class="fas fa-check"></i> Confirm
                </button>
            </div>
        </div>
    )rawliteral";

    return html;
}

String ServerController::Server_Root_Buttons()
{
    return R"rawliteral(
        <style>
            .main-button {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                gap: 10px;
                margin: 15px;
                padding: 15px 30px;
                font-size: 18px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 12px;
                cursor: pointer;
                text-decoration: none;
                transition: background-color 0.3s ease, transform 0.2s ease;
                box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            }

            .main-button:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }

            .main-button i {
                font-size: 20px;
            }

            .button-container {
                display: flex;
                justify-content: center;
                flex-wrap: wrap;
            }

            .info-block {
                text-align: center;
                margin-top: 60px;
                font-size: 14px;
                color: #aaa;
            }

            .info-block a {
                color: #bbb;
                text-decoration: underline;
            }

            .info-block a:hover {
                color: #ddd;
            }
        </style>

        <div class="button-container">
            <a href="/sensor" class="main-button"><i class="fas fa-thermometer-half"></i>Sensor</a>
            <a href="/control" class="main-button"><i class="fas fa-sliders-h"></i>Control</a>
            <a href="/log" class="main-button"><i class="fas fa-file-alt"></i>Log</a>
            <a href="/cluster" class="main-button"><i class="fas fa-network-wired"></i>Cluster</a>
            <a href="/system" class="main-button"><i class="fas fa-cog"></i>System</a>
            <a href="/about" class="main-button"><i class="fas fa-info-circle"></i>About</a>
        </div>

        <div class="info-block">
            ℹ️ For instructions and device updates, please visit:
            <a href=")rawliteral" + String(WEBSITE_URL) + R"rawliteral(" target="_blank">)rawliteral" + String(WEBSITE_URL) + R"rawliteral(</a>
        </div>
    )rawliteral";
}

String ServerController::Server_Root_End()
{
    String html;
    html += BackgroundFogEffect();
    html += R"rawliteral(
    </body>
    </html>
    )rawliteral";

    return html;
}

void ServerController::Server_Handle_Root()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_Root_Start());
    server->sendContent(Server_Root_Buttons());
    server->sendContent(Server_Root_End());

    server->sendContent(""); // flush
}

void ServerController::Server_Handle_UpdateUnitName()
{
    if (!PreRequest()) return;

    if (server->hasArg("plain"))
    {
        String body = server->arg("plain");

        // Szukamy wartości w JSON: {"name":"NowaNazwa"}
        int start = body.indexOf(":\"") + 2;
        int end   = body.lastIndexOf("\"");
        if (start > 1 && end > start)
        {
            String newName = body.substring(start, end);

            // --- WALIDACJA ---
            if (newName.length() < 3 || newName.length() > 20) {
                server->send(400, "text/plain", "Name must be 3–20 characters long");
                return;
            }

            if (!isAlpha(newName[0])) {
                server->send(400, "text/plain", "Name must start with a letter");
                return;
            }

            for (int i = 0; i < newName.length(); i++) {
                char c = newName[i];
                if (!isAlphaNumeric(c)) {
                    server->send(400, "text/plain", "Name can only contain letters and digits");
                    return;
                }
            }
            // --- END WALIDACJA ---

            // zapisujemy i restart
            sharedResourceManager->UnitName_Set(newName.c_str());

            server->send(200, "text/plain", "Unit name updated. Restarting...");

            delay(200);   // daj czas na wysłanie odpowiedzi
            ESP.restart();
            return;
        }
    }

    server->send(400, "text/plain", "Invalid request");
}

String ServerController::Server_Sensor_Start()
{
    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🍄 Sensor</title>
        <script src="/chart.js"></script>
        <script type="text/javascript">
            window.onload = function() {
                if (localStorage.getItem('scrollHeight') !== null) {
                    window.scrollTo(0, localStorage.getItem('scrollHeight'));
                }
            };
            window.onbeforeunload = function() {
                localStorage.setItem('scrollHeight', window.scrollY);
            };
        </script>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
        <style>
            body {
                background-color: #2f3c6b; /* brightened deep blue */
                color: #f0f0f0;
                font-family: Arial, sans-serif;
                margin: 0;
                padding: 0;
            }

            h1, h2 {
                text-align: center;
                color: #f8f8f8;
            }
        </style>
    </head>
    <body>
    )rawliteral";

    html += Server_TopBar("Sensor", "/");

    return html;
}

String ServerController::GetAirLogs_JS()
{
    String labels = "";
    String temperatures = "";
    String humidities = "";
    String co2Levels = "";

    auto logs = sharedResourceManager->AirLogs_Get(288);
    for (const auto& log : logs) {
        auto unixTime = log.unixTime;
        char timeBuffer[20];
        struct tm *timeInfo = localtime(&unixTime);
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", timeInfo);

        labels += "\"" + String(timeBuffer) + "\",";
        temperatures += String(log.temperature) + ",";
        humidities += String(log.humidity) + ",";
        co2Levels += String(log.co2) + ",";
    }

    // Remove trailing commas
    labels.remove(labels.length() - 1);
    temperatures.remove(temperatures.length() - 1);
    humidities.remove(humidities.length() - 1);
    co2Levels.remove(co2Levels.length() - 1);

    return "{" + String("labels: [") + labels + String("], temperatureData: [") + temperatures + String("], humidityData: [") + humidities + String("], co2Data: [") + co2Levels + String("]}");
}

String ServerController::Server_Sensor_AirLogs()
{
    String logsData = GetAirLogs_JS();

    return R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-thermometer-half"></i> Temperature 
                <span id="tempWarning" class="warning"></span>
            </h2>
            <canvas id="temperatureChart"></canvas>
        </div>

        <div class="section">
            <h2><i class="fas fa-tint"></i> Humidity 
                <span id="humWarning" class="warning"></span>
            </h2>
            <canvas id="humidityChart"></canvas>
        </div>

        <div class="section">
            <h2><i class="fas fa-wind"></i> CO₂ Levels 
                <span id="co2Warning" class="warning"></span>
            </h2>
            <canvas id="co2Chart"></canvas>
        </div>

        <style>
            .section {
                width: calc(100% - 40px);
                margin: 20px auto;
                padding: 20px;
                background-color: #3a4a82;
                border-radius: 12px;
                box-shadow: 0 4px 10px rgba(0,0,0,0.3);
            }

            .section h2 {
                margin-top: 0;
                text-align: center;
                color: #ffffff;
                font-size: 20px;
                margin-bottom: 15px;
            }

            .warning {
                font-size: 14px;
                color: #f1c40f; /* żółty warning */
                margin-left: 10px;
            }

            canvas {
                width: 100% !important;
                height: 300px !important;
            }
        </style>

        <script>
            const { labels, temperatureData, humidityData, co2Data } = )rawliteral" + logsData + R"rawliteral(;

            const charts = [
                { id: 'temperatureChart', data: temperatureData, yAxisLabel: 'Temperature (°C)', yMin: 15, yMax: 30, borderColor: 'rgba(255, 69, 0, 1)', backgroundColor: 'rgba(255, 69, 0, 0.2)', warningId: 'tempWarning' },
                { id: 'humidityChart', data: humidityData, yAxisLabel: 'Humidity (%)', yMin: 30, yMax: 100, borderColor: 'rgba(173, 255, 47, 1)', backgroundColor: 'rgba(173, 255, 47, 0.2)', warningId: 'humWarning' },
                { id: 'co2Chart', data: co2Data, yAxisLabel: 'CO₂ (ppm)', yMin: 300, yMax: 3000, borderColor: 'rgba(0, 255, 255, 1)', backgroundColor: 'rgba(0, 255, 255, 0.2)', warningId: 'co2Warning' }
            ];

            Chart.defaults.color = '#fff';
            Chart.defaults.borderColor = '#36a3eb91';

            charts.forEach(chart => {
                drawChart(chart.id, chart.data, chart.yAxisLabel, chart.yMin, chart.yMax, chart.borderColor, chart.backgroundColor, chart.warningId);
            });

            function drawChart(canvasId, data, yAxisLabel, yMin, yMax, borderColor, backgroundColor, warningId) {
                const ctx = document.getElementById(canvasId).getContext('2d');

                // tylko ostatnia wartość
                if (data[data.length - 1] === -1) {
                    document.getElementById(warningId).textContent = "⚠ no sensor connected";
                }

                new Chart(ctx, {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [{
                            data: data,
                            borderColor: borderColor,
                            backgroundColor: backgroundColor,
                            borderWidth: 1
                        }]
                    },
                    options: {
                        responsive: true,
                        maintainAspectRatio: false,
                        plugins: {
                            legend: { display: false },
                            title: { display: false }
                        },
                        scales: {
                            y: {
                                beginAtZero: false,
                                min: yMin,
                                max: yMax,
                                title: {
                                    display: true,
                                    text: yAxisLabel
                                }
                            },
                            x: {
                                title: {
                                    display: true,
                                    text: 'Time'
                                }
                            }
                        }
                    }
                });
            }
        </script>
    )rawliteral";
}

String ServerController::Server_Sensor_End()
{
    return R"rawliteral(
    </body>
    </html>
    )rawliteral";
}

void ServerController::Server_Handle_Sensor()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_Sensor_Start());
    server->sendContent(Server_Sensor_AirLogs());
    server->sendContent(Server_Sensor_End());

    server->sendContent(""); // flush
}

String ServerController::Server_Control_Start()
{
    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🍄 Control</title>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
        <style>
            body {
                background-color: #2f3c6b;
                color: #f0f0f0;
                font-family: Arial, sans-serif;
                margin: 0;
                padding: 0;
            }

            h1, h2, h3 {
                text-align: center;
                color: #f8f8f8;
            }

            .status-row, .tab-buttons button, .tab-content, .tab-container {
                color: #fff;
                background-color: #3b4b7a;
            }

            .tab-button.active {
                background-color: #5566aa;
            }

            .tab-button {
                background-color: #5566aa;
                color: white;
            }

            .tab-button:hover {
                background-color: #6577cc;
            }

            .main-button {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                gap: 10px;
                margin: 20px;
                padding: 20px 40px;
                font-size: 18px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 12px;
                cursor: pointer;
                text-decoration: none;
                transition: background-color 0.3s ease, transform 0.2s ease;
                box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            }

            .main-button:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }

            table {
                color: #fff;
                background-color: #3b4b7a;
            }

            th {
                background-color: #2f3c6b;
                color: #f0f0f0;
            }

            td {
                background-color: #405080;
                color: #fff;
            }

            label {
                display: block;
                margin-bottom: 8px;
            }

            input {
                margin-left: 10px;
                margin-bottom: 10px;
                padding: 5px;
                border-radius: 5px;
                border: none;
                background-color: #4e5d8a;
                color: white;
            }

            input[type="number"] {
                width: 80px;
            }

            .status-cards {
                display: flex;
                flex-wrap: wrap;
                gap: 10px;
                margin: 10px 0;
            }

            .status-card {
                flex: 1;
                min-width: 120px;
                background: #4456a5;
                padding: 10px;
                border-radius: 8px;
                text-align: center;
                font-size: 14px;
                color: #fff;
            }

            .action-buttons {
                display: flex;
                justify-content: center;
                gap: 15px;
                margin-top: 15px;
            }

            .action-card {
                min-width: 150px;
                background: #3498db;
                padding: 10px;
                border-radius: 8px;
                text-align: center;
                font-size: 14px;
                color: #fff;
                cursor: pointer;
                border: none;
                transition: background-color 0.3s ease, transform 0.2s ease;
                box-shadow: 0 4px 8px rgba(0,0,0,0.2);
            }

            .action-card:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }

            .action-card.danger {
                background-color: #c0392b;
            }

            .action-card.danger:hover {
                background-color: #e74c3c;
            }

            .cycle-table {
                width: 100%;
                border-collapse: collapse;
                margin-top: 15px;
                background-color: #3a4a82;
                border-radius: 8px;
                overflow: hidden;
                box-shadow: 0 4px 10px rgba(0,0,0,0.3);
            }

            .cycle-table th, .cycle-table td {
                padding: 10px;
                text-align: center;
                border-bottom: 1px solid #5566aa;
                white-space: nowrap;
            }

            .cycle-table th {
                background-color: #2f3c6b;
                color: #f8f8f8;
                font-weight: bold;
            }

            .cycle-table tr:hover {
                background-color: #46588c;
            }
        </style>
    </head>
    <body>
    )rawliteral";

    html += Server_TopBar("Control", "/");

    return html;
}

String ServerController::Server_Control_ControlButtons()
{
    bool isLightOff = sharedResourceManager->TurnOffLight_Get();
    bool isHumidAdaptive = sharedResourceManager->AdaptiveHumidification_Get();
    bool isIncubation = sharedResourceManager->Incubation_Get();

    // Light button
    String lightButtonClass = "action-card " + String(isLightOff ? "light-off" : "light-on");
    String lightButtonText  = isLightOff 
        ? "<i class='fas fa-lightbulb'></i> Light Cycling: OFF"
        : "<i class='fas fa-lightbulb'></i> Light Cycling: ON";

    // Humidifier button (Adaptive / Timer)
    String humidButtonClass = "action-card humid-blue";
    String humidButtonText  = isHumidAdaptive
        ? "<i class='fas fa-tint'></i> Humidification: Adaptive"
        : "<i class='fas fa-tint'></i> Humidification: Timer";
    String humidButtonHint  = isHumidAdaptive
        ? "Automatically adjusts humidifier ON/OFF cycles based on humidity trends"
        : "Runs humidifier with fixed ON/OFF times, without adapting to humidity changes";

    // Incubation / Fruiting button
    String incubButtonClass = String("action-card ") + (isIncubation ? "incub-on" : "incub-off");
    String incubButtonText  = isIncubation
        ? "<i class='fas fa-moon'></i> Mode: Incubation"
        : "<i class='fas fa-seedling'></i> Mode: Fruiting";
    String incubButtonHint = isIncubation
        ? "Incubation mode: lights OFF, fan minimal, temperature 23–24°C"
        : "Fruiting mode: daylight cycle ON, ventilation ON, cycle control ON";

    return R"rawliteral(
        <div class="action-buttons">
            <button class=")rawliteral" + lightButtonClass + R"rawliteral(" 
                    onclick="toggleLight()" 
                    title="Enable or disable automatic light cycle">
                )rawliteral" + lightButtonText + R"rawliteral(
            </button>

            <button class=")rawliteral" + humidButtonClass + R"rawliteral(" 
                    onclick="toggleHumid()" 
                    title=")rawliteral" + humidButtonHint + R"rawliteral(">
                )rawliteral" + humidButtonText + R"rawliteral(
            </button>

            <button class=")rawliteral" + incubButtonClass + R"rawliteral("
                    onclick="toggleIncubation()"
                    title=")rawliteral" + incubButtonHint + R"rawliteral(">
                )rawliteral" + incubButtonText + R"rawliteral(
            </button>
        </div>
        <style>
            /* LIGHT */
            .action-card.light-on {
                background-color: #f1c40f;
            }
            .action-card.light-on:hover {
                background-color: #d4ac0d;
            }
            .action-card.light-off {
                background-color: #7f8c8d;
            }
            .action-card.light-off:hover {
                background-color: #707b7c;
            }

            /* HUMIDIFIER – always blue */
            .action-card.humid-blue {
                background-color: #3498db;
            }
            .action-card.humid-blue:hover {
                background-color: #2e86c1;
            }

            /* INCUBATION */
            .action-card.incub-on {
                background-color: #8e44ad;
            }
            .action-card.incub-on:hover {
                background-color: #732d91;
            }
            .action-card.incub-off {
                background-color: #16a085;
            }
            .action-card.incub-off:hover {
                background-color: #13856e;
            }
        </style>
        <script>
            function toggleLight()
            {
                fetch('/toggleLight', { method: 'POST' })
                    .then(response => {
                        if (response.ok) {
                            location.reload();
                        } else {
                            alert('Failed to toggle light');
                        }
                    });
            }
            function toggleHumid()
            {
                fetch('/toggleHumid', { method: 'POST' })
                    .then(response => {
                        if (response.ok) {
                            location.reload();
                        } else {
                            alert('Failed to toggle humidification');
                        }
                    });
            }
            function toggleIncubation()
            {
                fetch('/toggleIncubation', { method: 'POST' })
                    .then(response => {
                        if (response.ok) {
                            location.reload();
                        } else {
                            alert('Failed to toggle incubation mode');
                        }
                    });
            }
        </script>
    )rawliteral";
}

String ServerController::Server_Control_Cycle()
{
    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    size_t totalDays = cycle.days.size();

    SharedResourceManager::FCSettingDay currentDay;
    SharedResourceManager::FCSettingHour currentHour;
    int currentDayIndex;
    int currentHourIndex;
    cycleTimeBind->GetCurrentDayAndHour(currentDay, currentHour, currentDayIndex, currentHourIndex);

    String utcBuffer   = clientNTP->GetUtcTimeString();
    String localBuffer = clientNTP->GetLocalTimeString();

    String tabsHtml = R"rawliteral(
        <h1>Cycle Configuration</h1>
        <style>
            .tab-container {
                margin-top: 20px;
            }
            .tab-buttons {
                overflow-x: auto;
                white-space: nowrap;
                border-bottom: 2px solid #ccc;
            }
            .tab-button {
                display: inline-block;
                background-color: #f1f1f1;
                border: none;
                outline: none;
                cursor: pointer;
                padding: 10px 20px;
                transition: background-color 0.3s;
                font-size: 16px;
                border-radius: 8px 8px 0 0;
                margin-right: 5px;
            }
            .tab-button:hover {
                background-color: #ddd;
            }
            .tab-button.active {
                background-color: #ccc;
            }
            .tab-content {
                border: 1px solid #ccc;
                padding: 20px;
                border-radius: 0 0 8px 8px;
                margin-top: -1px;
            }
            .status-row {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-top: 25px;
                padding: 10px 20px;
                background-color: #3b4b7a; /* dark bluish background */
                border-radius: 10px;
                border: 1px solid #5f6eaa; /* softer border for dark theme */
                font-size: 16px;
                color: #ffffff; /* white text */
            }
            .status-row button {
                background-color: #c0392b;
                color: white;
                border: none;
                padding: 8px 14px;
                border-radius: 6px;
                cursor: pointer;
                font-size: 14px;
            }
            #editHourForm label {
                color: #cfd7ff;
                font-size: 14px;
            }
            #editHourForm input {
                background: #3b4b7a;
                border: 1px solid #5f6eaa;
                border-radius: 6px;
                color: #ffffff;
                padding: 6px;
                width: 100%;
            }
            #editHourForm button {
                margin-top: 10px;
                padding: 8px 14px;
                border-radius: 6px;
                border: none;
                cursor: pointer;
                font-size: 14px;
            }
            #editHourForm button:first-of-type {
                background-color: #2ecc71; /* zielony Apply */
                color: #fff;
            }
            #editHourForm button:last-of-type {
                background-color: #e74c3c; /* czerwony Cancel */
                color: #fff;
            }
        </style>
    )rawliteral";

    tabsHtml += String(R"rawliteral(
        <div class="status-cards">
            <div class="status-card"><strong>UTC:</strong> )rawliteral") + utcBuffer + R"rawliteral(</div>
            <div class="status-card"><strong>Local:</strong> )rawliteral" + localBuffer + R"rawliteral(</div>
            <div class="status-card">
                <strong>Day Index:</strong> )rawliteral" + String(currentDayIndex) + R"rawliteral(
            </div>
            <div class="status-card">
                <strong>Hour Index:</strong> )rawliteral" + String(currentHourIndex) + R"rawliteral(
            </div>
        </div>
        <div class="action-buttons">
            <button class="action-card" 
                    title="Please remember to click this after editing to save changes" 
                    onclick="submitCycleUpdate()">
                <i class="fas fa-save"></i> Save Cycle
            </button>
            <button class="action-card danger" 
                    title="Resets the cycle back to the beginning, starting from Day 0" 
                    onclick="resetCycle()">
                <i class="fas fa-undo"></i> Reset Cycle
            </button>
        </div>
    )rawliteral";

    tabsHtml += R"rawliteral(
        <div class="tab-container">
            <div class="tab-buttons" id="tabButtons">
    )rawliteral";

    for (size_t i = 0; i < totalDays; ++i)
    {
        tabsHtml += "<button class='tab-button' onclick='showTab(" + String(i) + ")'>Day " + String(i) + "</button>\n";
    }

    tabsHtml += R"rawliteral(
                <button class='tab-button' style="background-color: #dff0d8; color: #3c763d;" onclick='addDay()'>➕ Add Day</button>
            </div>
            <div class="tab-content" id="tabContent">
                <p>Select a day to view/edit its settings.</p>
            </div>
        </div>

        <div id="editModal" style="display:none; position:fixed; top:0; left:0; width:100%; height:100%;
            background:rgba(0,0,0,0.5); z-index:1000; align-items:center; justify-content:center;">
            <div style="background:#2c3760; color:#ffffff; padding:20px; border-radius:12px; width:90%; max-width:500px; 
                position:relative; border:1px solid #5f6eaa; box-shadow:0 4px 15px rgba(0,0,0,0.6);">
                
                <h3 style="margin-top:0; color:#d0d6ff; text-align:center;">
                    <i class="fas fa-clock"></i> Edit Hour <span id="modalHourLabel"></span>
                </h3>

                <form id="editHourForm" style="display:grid; grid-template-columns: 1fr 1fr; gap:15px; margin-top:10px;">
                    <input type="hidden" id="editDayIndex">
                    <input type="hidden" id="editHourIndex">

                    <label style="grid-column:1 / span 2; text-align:center;">
                        Hour:<br>
                        <input type="number" id="editHour" min="0" max="23" style="width:100px; text-align:center;">
                    </label>

                    <label style="grid-column:1 / span 2;">
                        Target Temperature (°C):<br>
                        <input type="number" id="editHeatpadOn" step="0.5" min="1" max="60">
                    </label>

                    <label>Fan ON (min):<br>
                        <input type="number" id="editFanOn" step="1" min="1" max="60">
                    </label>
                    <label>Fan OFF (min):<br>
                        <input type="number" id="editFanOff" step="1" min="30" max="1200">
                    </label>

                    <label>Humidifier ON (min):<br>
                        <input type="number" id="editHumidOn" step="1" min="1" max="60">
                    </label>
                    <label>Humidifier OFF (min):<br>
                        <input type="number" id="editHumidOff" step="1" min="30" max="1200">
                    </label>

                    <label id="labelTargetHumidity" style="display:none; grid-column:1 / span 2;">
                        Target Humidity (%):<br>
                        <input type="number" id="editTargetHumidity" step="1" min="40" max="100">
                    </label>

                    <div style="grid-column: 1 / span 2; display:flex; justify-content:space-between; margin-top:10px;">
                        <button type="button" onclick="applyHourEdit()" 
                                style="flex:1; margin-right:10px; background:#27ae60; color:white; border:none; 
                                    padding:10px; border-radius:8px; cursor:pointer; font-size:14px;">
                            <i class="fas fa-check"></i> Apply
                        </button>
                        <button type="button" onclick="closeModal()" 
                                style="flex:1; margin-left:10px; background:#e74c3c; color:white; border:none; 
                                    padding:10px; border-radius:8px; cursor:pointer; font-size:14px;">
                            <i class="fas fa-times"></i> Cancel
                        </button>
                    </div>
                </form>
            </div>
        </div>

        <div id="editLightModal" style="display:none; position:fixed; top:0; left:0; width:100%; height:100%;
            background:rgba(0,0,0,0.5); z-index:1000; align-items:center; justify-content:center;">
            <div style="background:#2c3760; color:#ffffff; padding:20px; border-radius:12px; width:90%; max-width:400px; 
                position:relative; border:1px solid #5f6eaa; box-shadow:0 4px 15px rgba(0,0,0,0.6);">
                
                <h3 style="margin-top:0; color:#d0d6ff; text-align:center;">
                    <i class="fas fa-lightbulb"></i> Edit Light Settings
                </h3>
                <form id="editLightForm" style="display:flex; flex-direction:column; gap:15px; margin-top:10px;">
                    <input type="hidden" id="editLightDayIndex">
                    
                    <label>
                        Light ON hour:
                        <input type="number" id="editLightOn" min="0" max="23">
                    </label>
                    <label>
                        Light OFF hour:
                        <input type="number" id="editLightOff" min="0" max="23">
                    </label>

                    <div style="display:flex; justify-content:space-between; margin-top:20px;">
                        <button type="button" onclick="applyLightEdit()" 
                                style="flex:1; margin-right:10px; background:#27ae60; color:white; border:none; 
                                    padding:10px; border-radius:8px; cursor:pointer; font-size:14px;">
                            <i class="fas fa-check"></i> Apply
                        </button>
                        <button type="button" onclick="closeLightModal()" 
                                style="flex:1; margin-left:10px; background:#e74c3c; color:white; border:none; 
                                    padding:10px; border-radius:8px; cursor:pointer; font-size:14px;">
                            <i class="fas fa-times"></i> Cancel
                        </button>
                    </div>
                </form>
            </div>
        </div>

        <script>
            function showTab(index) {
                // zapisz otwarty dzień
                localStorage.setItem("openDayIndex", index);

                const buttons = document.getElementsByClassName('tab-button');
                for (let i = 0; i < buttons.length; i++) {
                    buttons[i].classList.remove('active');
                }
                buttons[index].classList.add('active');

                fetch(`/getDayData?dayIndex=${index}`)
                    .then(response => response.json())
                    .then(day => {
                        let html = `
                            <h3>Day ${index}</h3>
                            <div style="margin-bottom: 20px;">
                                <button style="background-color: #e74c3c; color: white; border: none; padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 14px;" onclick="removeDay(${index})">Remove Day</button>
                                <button style="background-color: #3498db; color: white; border: none; padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 14px;" onclick="cloneDay(${index})">Clone Day</button>
                                <button style="background-color:rgba(209, 153, 11, 1); color: white; border: none; padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 14px;" onclick="updateLight(${index})">Edit Light</button>
                                <button style="background-color: #2ecc71; color: white; border: none; padding: 8px 14px; border-radius: 6px; cursor: pointer; font-size: 14px;" onclick="addHour(${index})">Add Hour</button>
                            </div>
                            <p>Light ON: ${day.lightHour_ON}:00 | Light OFF: ${day.lightHour_OFF}:00</p>
                        `;

                        // 🔹 Nagłówki tabeli
                        let humidHeader = day.isHumidAdaptive
                            ? `<th>Target Humidity (%)</th>`
                            : `<th>Humidifier ON (min)</th><th>Humidifier OFF (min)</th>`;

                        html += `<div style="overflow-x:auto;"><table class="cycle-table">
                                    <thead>
                                        <tr>
                                            <th>Hour</th>
                                            <th>Target Temperature (°C)</th>
                                            <th>Fan ON (min)</th>
                                            <th>Fan OFF (min)</th>
                                            ${humidHeader}
                                            <th>Actions</th>
                                        </tr>
                                    </thead>
                                    <tbody>`;

                        day.hours.forEach((hour, hIndex) => {
                            let humidCells = day.isHumidAdaptive
                                ? `<td>${hour.targetHumidity}</td>`
                                : `<td>${hour.humidifierMinutes_ON}</td><td>${hour.humidifierMinutes_OFF}</td>`;

                            html += `
                                <tr>
                                    <td>${hour.hour}:00</td>
                                    <td>${hour.heatpadTemp_ON}</td>
                                    <td>${hour.fanMinutes_ON}</td>
                                    <td>${hour.fanMinutes_OFF}</td>
                                    ${humidCells}
                                    <td style="display:flex; gap:6px; justify-content:center;">
                                        <button style="background-color:#3498db; color:white; border:none; padding:6px 12px; border-radius:6px; cursor:pointer; font-size:14px; display:flex; align-items:center; gap:6px;"
                                                onclick="editHour(${index}, ${hIndex})">
                                            <i class="fas fa-pen"></i> Edit
                                        </button>
                                        <button style="background-color:#e74c3c; color:white; border:none; padding:6px 12px; border-radius:6px; cursor:pointer; font-size:14px; display:flex; align-items:center; gap:6px;"
                                                onclick="removeHour(${index}, ${hIndex})">
                                            <i class="fas fa-trash"></i> Remove
                                        </button>
                                    </td>
                                </tr>`;
                        });

                        html += `</tbody></table></div>`;
                        document.getElementById('tabContent').innerHTML = html;
                    });
            }

            // przywracanie po przeładowaniu
            window.addEventListener("DOMContentLoaded", () => {
                const savedIndex = localStorage.getItem("openDayIndex");
                const buttons = document.getElementsByClassName('tab-button');
                if (savedIndex !== null) {
                    const index = parseInt(savedIndex);
                    if (index >= 0 && index < buttons.length) {
                        showTab(index);
                    }
                }
            });

            function addDay() {
                fetch('/addDay', { method: 'POST' })
                    .then(response => response.ok ? location.reload() : alert('Failed to add day.'));
            }

            function removeDay(index) {
                fetch('/removeDay', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ dayIndex: index })
                }).then(response => response.ok ? location.reload() : alert('Failed to remove day.'));
            }

            function cloneDay(index) {
                fetch('/cloneDay', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ dayIndex: index })
                }).then(response => response.ok ? location.reload() : alert('Failed to clone day.'));
            }

            function updateLight(dayIndex) {
                fetch(`/getDayData?dayIndex=${dayIndex}`)
                    .then(response => response.json())
                    .then(day => {
                        document.getElementById("editLightDayIndex").value = dayIndex;
                        document.getElementById("editLightOn").value = day.lightHour_ON;
                        document.getElementById("editLightOff").value = day.lightHour_OFF;
                        document.getElementById("editLightModal").style.display = "flex";
                    });
            }

            function closeLightModal() {
                document.getElementById("editLightModal").style.display = "none";
            }

            function applyLightEdit() {
                const dayIndex = parseInt(document.getElementById("editLightDayIndex").value);
                const newOn = parseInt(document.getElementById("editLightOn").value);
                const newOff = parseInt(document.getElementById("editLightOff").value);

                const data = { dayIndex: dayIndex, lightHour_ON: newOn, lightHour_OFF: newOff };

                fetch("/updateLight", {
                    method: "POST",
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(data)
                }).then(response => {
                    if (response.ok) {
                        closeLightModal();
                        alert("Light settings updated.");
                        showTab(dayIndex);
                    } else {
                        alert("Failed to update light settings.");
                    }
                });
            }

            function addHour(index) {
                fetch('/addHour', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ dayIndex: index })
                }).then(response => response.ok ? location.reload() : alert('Failed to add hour.'));
            }

            function removeHour(dayIndex, hourIndex) {
                fetch('/removeHour', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ dayIndex: dayIndex, hourIndex: hourIndex })
                }).then(response => response.ok ? location.reload() : alert('Failed to remove hour.'));
            }

            function editHour(dayIndex, hourIndex) {
                fetch(`/getDayData?dayIndex=${dayIndex}`)
                    .then(response => response.json())
                    .then(data => {
                        const hour = data.hours[hourIndex];

                        document.getElementById("editDayIndex").value = dayIndex;
                        document.getElementById("editHourIndex").value = hourIndex;
                        document.getElementById("editHour").value = hour.hour;
                        document.getElementById("editHeatpadOn").value = hour.heatpadTemp_ON;
                        document.getElementById("editFanOn").value = hour.fanMinutes_ON;
                        document.getElementById("editFanOff").value = hour.fanMinutes_OFF;
                        document.getElementById("editHumidOn").value = hour.humidifierMinutes_ON;
                        document.getElementById("editHumidOff").value = hour.humidifierMinutes_OFF;
                        document.getElementById("editTargetHumidity").value = hour.targetHumidity;

                        if (data.isHumidAdaptive) {
                            document.getElementById("labelTargetHumidity").style.display = "block";
                            document.getElementById("editHumidOn").parentElement.style.display = "none";
                            document.getElementById("editHumidOff").parentElement.style.display = "none";
                        } else {
                            document.getElementById("labelTargetHumidity").style.display = "none";
                            document.getElementById("editHumidOn").parentElement.style.display = "block";
                            document.getElementById("editHumidOff").parentElement.style.display = "block";
                        }

                        const hourFormatted = `${hour.hour.toString().padStart(2, '0')}:00`;
                        document.getElementById("modalHourLabel").textContent = hourFormatted;

                        document.getElementById("editModal").style.display = "flex";
                    });
            }

            function closeModal() {
                document.getElementById("editModal").style.display = "none";
            }

            function applyHourEdit() {
                const dayIndex = parseInt(document.getElementById("editDayIndex").value);
                const hourIndex = parseInt(document.getElementById("editHourIndex").value);
                const targetTemp = parseFloat(document.getElementById("editHeatpadOn").value);

                const data = {
                    dayIndex: dayIndex,
                    hourIndex: hourIndex,
                    hour: parseInt(document.getElementById("editHour").value),

                    // Heatpad
                    heatpadTemp_ON: targetTemp,
                    heatpadTemp_OFF: targetTemp + 1,

                    // Fan
                    fanMinutes_ON: parseInt(document.getElementById("editFanOn").value),
                    fanMinutes_OFF: parseInt(document.getElementById("editFanOff").value),

                    // Humidifier (Timer)
                    humidifierMinutes_ON: parseInt(document.getElementById("editHumidOn").value),
                    humidifierMinutes_OFF: parseInt(document.getElementById("editHumidOff").value),

                    // Humidifier (Adaptive)
                    targetHumidity: parseInt(document.getElementById("editTargetHumidity").value)
                };

                fetch("/updateHour", {
                    method: "POST",
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(data)
                })
                .then(response => {
                    if (response.ok) {
                        closeModal();
                        showTab(dayIndex); // Reload current tab
                    } else {
                        alert("Failed to update hour.");
                    }
                });
            }

            function submitCycleUpdate() {
                fetch('/updateCycle', { method: 'POST' })
                    .then(response => {
                        if (response.ok) {
                            alert('Cycle updated successfully.');
                            location.reload();
                        } else {
                            alert('Failed to update cycle.');
                        }
                    });
            }

            function resetCycle() {
                fetch('/resetCycle', { method: 'POST' })
                    .then(response => {
                        if (response.ok) {
                            alert('Cycle reset successfully.');
                            location.reload();
                        } else {
                            alert('Failed to reset cycle.');
                        }
                    });
            }
        </script>
    )rawliteral";

    return tabsHtml;
}

String ServerController::Server_Control_End()
{
    return R"rawliteral(
    </body>
    </html>
    )rawliteral";
}

void ServerController::Server_Handle_Control()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_Control_Start());
    server->sendContent(Server_Control_ControlButtons());
    server->sendContent(Server_Control_Cycle());
    server->sendContent(Server_Control_End());

    server->sendContent(""); // flush
}

void ServerController::Server_Handle_ToggleLight()
{
    if (!PreRequest()) return;

    bool currentState = sharedResourceManager->TurnOffLight_Get();
    sharedResourceManager->TurnOffLight_Set(!currentState);
    server->send(200, "text/plain", "Light state updated");
}

void ServerController::Server_Handle_SetLight()
{
    if (!PreRequest()) return;

    String body = server->arg("plain");
    body.trim();

    if (body != "0" && body != "1")
    {
        server->send(400, "text/plain", "Expected '0' or '1'");
        return;
    }

    bool newValue = (body == "1");
    sharedResourceManager->TurnOffLight_Set(newValue);
    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_ToggleHumid()
{
    if (!PreRequest()) return;

    bool currentState = sharedResourceManager->AdaptiveHumidification_Get();
    sharedResourceManager->AdaptiveHumidification_Set(!currentState);
    server->send(200, "text/plain", "Humidification state updated");
}

void ServerController::Server_Handle_SetHumid()
{
    if (!PreRequest()) return;

    String body = server->arg("plain");
    body.trim();

    if (body != "0" && body != "1")
    {
        server->send(400, "text/plain", "Expected '0' or '1'");
        return;
    }

    bool newValue = (body == "1");
    sharedResourceManager->AdaptiveHumidification_Set(newValue);
    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_ToggleIncubation()
{
    if (!PreRequest()) return;

    bool current = sharedResourceManager->Incubation_Get();
    sharedResourceManager->Incubation_Set(!current);
    server->send(200, "text/plain", "Incubation mode updated");
}

void ServerController::Server_Handle_SetIncubation()
{
    if (!PreRequest()) return;

    String body = server->arg("plain");
    body.trim();

    if (body != "0" && body != "1")
    {
        server->send(400, "text/plain", "Expected '0' or '1'");
        return;
    }

    bool newValue = (body == "1");
    sharedResourceManager->Incubation_Set(newValue);
    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_UpdateCycle()
{
    if (!PreRequest()) return;

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool success = cycle.Serialize();
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
}

void ServerController::Server_Handle_ResetCycle()
{
    if (!PreRequest()) return;

    // Get the current cycle object
    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();

    // Set the cycleStartTime to current Unix time
    std::time_t now = clientNTP->GetUnixTime();
    cycle.cycleStartTime = now;

    // Save updated cycle back to shared resource
    sharedResourceManager->Cycle_Set(cycle);

    // Respond with success
    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_ImportCycle()
{
    if (!PreRequest()) return;

    String jsonStr = server->arg("plain");
    if (jsonStr.length() == 0)
    {
        server->send(400, "text/plain", "Empty body");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool parsed = cycle.FromJson(jsonStr);
    if (!parsed)
    {
        server->send(400, "text/plain", "Invalid JSON format");
        return;
    }

    bool valid = cycle.Validate();
    if (!valid)
    {
        server->send(400, "text/plain", "Validation failed");
        return;
    }

    sharedResourceManager->Cycle_Set(cycle);
    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_AddDay()
{
    if (!PreRequest()) return;

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool success = cycle.AddDay();
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
}

void ServerController::Server_Handle_RemoveDay()
{
    if (!PreRequest()) return;

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain").c_str());
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    int dayIndex = doc["dayIndex"] | -1;
    if (dayIndex < 0) {
        server->send(400, "text/plain", "Missing or invalid dayIndex");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool success = cycle.RemoveDay(dayIndex);
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
}

void ServerController::Server_Handle_CloneDay()
{
    if (!PreRequest()) return;

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain").c_str());
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    int dayIndex = doc["dayIndex"] | -1;
    if (dayIndex < 0) {
        server->send(400, "text/plain", "Missing or invalid dayIndex");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    if (dayIndex >= (int)cycle.days.size()) {
        server->send(400, "text/plain", "dayIndex out of range");
        return;
    }
    bool success = cycle.CloneDay(dayIndex);
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_UpdateLight()
{
    if (!PreRequest()) return;

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain").c_str());
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    int dayIndex = doc["dayIndex"] | -1;
    int lightOn = doc["lightHour_ON"] | -1;
    int lightOff = doc["lightHour_OFF"] | -1;

    if (dayIndex < 0 || lightOn < 0 || lightOn > 23 || lightOff < 0 || lightOff > 23) {
        server->send(400, "text/plain", "Missing or invalid data");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    if (dayIndex >= (int)cycle.days.size()) {
        server->send(400, "text/plain", "dayIndex out of range");
        return;
    }
    bool success = cycle.SetLightHours(dayIndex, lightOn, lightOff);
    if (success)
    {
        sharedResourceManager->Cycle_Set(cycle);
    }
    else
    {
        server->send(400, "text/plain", "Failed to set light hours (invalid range?)");
        return;
    }

    server->send(200, "text/plain", "OK");
}

void ServerController::Server_Handle_GetDayData()
{
    if (!PreRequest()) return;

    // Validate day index parameter
    if (!server->hasArg("dayIndex")) {
        server->send(400, "application/json", "{\"error\":\"Missing dayIndex parameter\"}");
        return;
    }

    int dayIndex = server->arg("dayIndex").toInt();

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();

    if (dayIndex < 0 || dayIndex >= static_cast<int>(cycle.days.size())) {
        server->send(400, "application/json", "{\"error\":\"Invalid day index\"}");
        return;
    }

    const SharedResourceManager::FCSettingDay &day = cycle.days[dayIndex];

    DynamicJsonDocument doc(1024);
    doc["lightHour_ON"] = day.lightHour_ON;
    doc["lightHour_OFF"] = day.lightHour_OFF;
    doc["isHumidAdaptive"] = sharedResourceManager->AdaptiveHumidification_Get();

    JsonArray hoursArray = doc.createNestedArray("hours");

    for (const auto &hour : day.hours)
    {
        JsonObject hourObj = hoursArray.createNestedObject();
        hourObj["hour"] = hour.hour;
        hourObj["heatpadTemp_ON"] = hour.heatpadTemp_ON;
        hourObj["heatpadTemp_OFF"] = hour.heatpadTemp_OFF;
        hourObj["fanMinutes_ON"] = hour.fanMinutes_ON;
        hourObj["fanMinutes_OFF"] = hour.fanMinutes_OFF;
        hourObj["humidifierMinutes_ON"] = hour.humidifierMinutes_ON;
        hourObj["humidifierMinutes_OFF"] = hour.humidifierMinutes_OFF;
        hourObj["targetHumidity"] = hour.targetHumidity;
    }

    String response;
    serializeJson(doc, response);
    server->send(200, "application/json", response);
}

void ServerController::Server_Handle_AddHour()
{
    if (!PreRequest()) return;

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain").c_str());
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    int dayIndex = doc["dayIndex"] | -1;
    if (dayIndex < 0) {
        server->send(400, "text/plain", "Missing or invalid dayIndex");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool success = cycle.AddHour(dayIndex);
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
}

void ServerController::Server_Handle_RemoveHour()
{
    if (!PreRequest()) return;

    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, server->arg("plain").c_str());
    if (error) {
        server->send(400, "text/plain", "Invalid JSON");
        return;
    }

    int dayIndex  = doc["dayIndex"]  | -1;
    int hourIndex = doc["hourIndex"] | -1;
    if (dayIndex < 0 || hourIndex < 0) {
        server->send(400, "text/plain", "Missing or invalid dayIndex/hourIndex");
        return;
    }

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();
    bool success = cycle.RemoveHour(dayIndex, hourIndex);
    if (success)
        sharedResourceManager->Cycle_Set(cycle);

    server->send(success ? 200 : 500, "text/plain", success ? "OK" : "FAIL");
}

void ServerController::Server_Handle_UpdateHour()
{
    if (!PreRequest()) return;

    if (server->method() != HTTP_POST) {
        server->send(405, "application/json", "{\"error\":\"Method Not Allowed\"}");
        return;
    }

    if (!server->hasArg("plain")) {
        server->send(400, "application/json", "{\"error\":\"Missing request body\"}");
        return;
    }

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, server->arg("plain"));
    if (error) {
        server->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    int dayIndex = doc["dayIndex"];
    int hourIndex = doc["hourIndex"];

    SharedResourceManager::FCSettingHour newHour;

    newHour.hour            = doc["hour"];
    newHour.heatpadTemp_ON  = ToolBox::RoundToStep(doc["heatpadTemp_ON"].as<float>(), 0.5f);
    newHour.heatpadTemp_OFF = ToolBox::RoundToStep(doc["heatpadTemp_OFF"].as<float>(), 0.5f);
    newHour.fanMinutes_ON   = doc["fanMinutes_ON"];
    newHour.fanMinutes_OFF  = doc["fanMinutes_OFF"];
    newHour.humidifierMinutes_ON  = doc["humidifierMinutes_ON"];
    newHour.humidifierMinutes_OFF = doc["humidifierMinutes_OFF"];
    newHour.targetHumidity        = doc["targetHumidity"];

    SharedResourceManager::FCSettingCycle cycle = sharedResourceManager->Cycle_Get();

    if (!cycle.UpdateHour(dayIndex, hourIndex, newHour)) {
        server->send(400, "application/json", "{\"error\":\"Invalid hour values or indices\"}");
        return;
    }

    sharedResourceManager->Cycle_Set(cycle);
    server->send(200, "application/json", "{\"status\":\"OK\"}");
}

String ServerController::Server_Log_Start()
{
    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🍄 Logs</title>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
        <style>
            body {
                background-color: #2f3c6b;
                color: #f0f0f0;
                font-family: Arial, sans-serif;
                margin: 0;
                padding: 0;
            }

            h1, h2 {
                text-align: center;
                color: #f8f8f8;
            }

            .section {
                max-width: 90%;
                margin: 30px auto;
                padding: 20px;
                background-color: #3a4a82;
                border-radius: 12px;
                box-shadow: 0 4px 10px rgba(0, 0, 0, 0.3);
            }

            table {
                width: 100%;
                border-collapse: collapse;
                background-color: #3a4a82;
                color: #fff;
                margin-top: 10px;
            }

            table, th, td {
                border: 1px solid #777;
            }

            th, td {
                padding: 10px;
                text-align: center;
                width: 33%;
            }

            th {
                background-color: #4456a5;
            }

            .tablinks {
                background-color: #3498db;
                border: none;
                color: white;
                padding: 10px 20px;
                margin: 5px;
                cursor: pointer;
                font-size: 16px;
                border-radius: 8px;
                transition: background-color 0.3s ease, transform 0.2s ease;
            }

            .tablinks:hover {
                background-color: #2980b9;
                transform: translateY(-1px);
            }

            .tablinks.active {
                background-color: #1f669d;
            }

            .tabcontent {
                display: none;
            }

            .button-container {
                text-align: center;
                margin-top: 20px;
            }

            .main-button {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                gap: 10px;
                margin: 10px;
                padding: 15px 30px;
                font-size: 16px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 10px;
                cursor: pointer;
                text-decoration: none;
                transition: background-color 0.3s ease, transform 0.2s ease;
                box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            }

            .main-button:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }

            .main-button i {
                font-size: 18px;
            }

            .tabs {
                text-align: center;
                margin-bottom: 20px;
            }
        </style>
    </head>
    <body>
    )rawliteral";

    html += Server_TopBar("Logs", "/");

    return html;
}

String ServerController::Server_Log_DeviceLogs()
{
    String html = R"rawliteral(
        <div class="tabs" style="text-align: center; margin-bottom: 20px;">
            <button class="tablinks" onclick="openTab(event, 'Heatpad')"><i class="fas fa-fire"></i> Heatpad</button>
            <button class="tablinks" onclick="openTab(event, 'Light')"><i class="fas fa-lightbulb"></i> Light</button>
            <button class="tablinks" onclick="openTab(event, 'Fan')"><i class="fas fa-fan"></i> Fan</button>
            <button class="tablinks" onclick="openTab(event, 'Humidifier')"><i class="fas fa-water"></i> Humidifier</button>
        </div>
        
        <div id="Heatpad" class="tabcontent">
            <div class="section">
                <h2><i class="fas fa-fire"></i> Heatpad Logs</h2>
                <table>
                    <tr><th>Date</th><th>Code</th><th>Info</th></tr>
    )rawliteral";

    // Funkcja tłumacząca kod logu na tekst
    auto LogCodeToString = [](SharedResourceManager::LogCode code) -> String
    {
        switch (code)
        {
            case SharedResourceManager::LogCode::INIT: return String("Initialized");
            case SharedResourceManager::LogCode::ON: return String("On");
            case SharedResourceManager::LogCode::OFF: return String("Off");
            case SharedResourceManager::LogCode::ERROR_0: return String("Error 0");
            case SharedResourceManager::LogCode::ERROR_1: return String("Error 1");
            case SharedResourceManager::LogCode::ERROR_2: return String("Error 2");
            case SharedResourceManager::LogCode::ERROR_3: return String("Error 3");
            case SharedResourceManager::LogCode::DIAGNOSTICS_0: return String("Diagnostics 0");
            case SharedResourceManager::LogCode::DIAGNOSTICS_1: return String("Diagnostics 1");
            case SharedResourceManager::LogCode::DIAGNOSTICS_2: return String("Diagnostics 2");
            case SharedResourceManager::LogCode::DIAGNOSTICS_3: return String("Diagnostics 3");
            default: return String("Unknown");
        }
    };

    // Heatpad logs
    auto heatpadLogs = sharedResourceManager->HeatpadLogs_Get(9999);
    for (const auto& log : heatpadLogs)
    {
        html += "<tr>";
        std::time_t unixTime = log.unixTime;
        std::tm *tm = std::localtime(&unixTime);
        char timeString[25];
        std::strftime(timeString, sizeof(timeString), "%d.%m.%Y %H:%M:%S", tm);
        html += "<td>" + String(timeString) + "</td>";
        html += "<td>" + LogCodeToString(log.code) + "</td>";
        html += "<td>" + String(log.info.c_str()) + "</td>";
        html += "</tr>";
    }

    html += R"rawliteral(
                </table>
            </div>
        </div>
        
        <div id="Light" class="tabcontent" style="display:none;">
            <div class="section">
                <h2><i class="fas fa-lightbulb"></i> Light Logs</h2>
                <table>
                    <tr><th>Date</th><th>Code</th><th>Info</th></tr>
    )rawliteral";

    auto lightLogs = sharedResourceManager->LightLogs_Get(9999);
    for (const auto& log : lightLogs)
    {
        html += "<tr>";
        std::time_t unixTime = log.unixTime;
        std::tm *tm = std::localtime(&unixTime);
        char timeString[25];
        std::strftime(timeString, sizeof(timeString), "%d.%m.%Y %H:%M:%S", tm);
        html += "<td>" + String(timeString) + "</td>";
        html += "<td>" + LogCodeToString(log.code) + "</td>";
        html += "<td>" + String(log.info.c_str()) + "</td>";
        html += "</tr>";
    }

    html += R"rawliteral(
                </table>
            </div>
        </div>

        <div id="Fan" class="tabcontent" style="display:none;">
            <div class="section">
                <h2><i class="fas fa-fan"></i> Fan Logs</h2>
                <table>
                    <tr><th>Date</th><th>Code</th><th>Info</th></tr>
    )rawliteral";

    auto fanLogs = sharedResourceManager->FanLogs_Get(9999);
    for (const auto& log : fanLogs)
    {
        html += "<tr>";
        std::time_t unixTime = log.unixTime;
        std::tm *tm = std::localtime(&unixTime);
        char timeString[25];
        std::strftime(timeString, sizeof(timeString), "%d.%m.%Y %H:%M:%S", tm);
        html += "<td>" + String(timeString) + "</td>";
        html += "<td>" + LogCodeToString(log.code) + "</td>";
        html += "<td>" + String(log.info.c_str()) + "</td>";
        html += "</tr>";
    }

    html += R"rawliteral(
                </table>
            </div>
        </div>

        <div id="Humidifier" class="tabcontent" style="display:none;">
            <div class="section">
                <h2><i class="fas fa-water"></i> Humidifier Logs</h2>
                <table>
                    <tr><th>Date</th><th>Code</th><th>Info</th></tr>
    )rawliteral";

    auto humidifierLogs = sharedResourceManager->HumidifierLogs_Get(9999);
    for (const auto& log : humidifierLogs)
    {
        html += "<tr>";
        std::time_t unixTime = log.unixTime;
        std::tm *tm = std::localtime(&unixTime);
        char timeString[25];
        std::strftime(timeString, sizeof(timeString), "%d.%m.%Y %H:%M:%S", tm);
        html += "<td>" + String(timeString) + "</td>";
        html += "<td>" + LogCodeToString(log.code) + "</td>";
        html += "<td>" + String(log.info.c_str()) + "</td>";
        html += "</tr>";
    }

    html += R"rawliteral(
                </table>
            </div>
        </div>
    )rawliteral";

    return html;
}

String ServerController::Server_Log_End()
{
    return R"rawliteral(
        <script>
            // Automatically activate first tab on load
            document.addEventListener("DOMContentLoaded", function () {
                document.querySelector(".tablinks").click();
            });

            function openTab(evt, tabName) {
                var i, tabcontent, tablinks;
                tabcontent = document.getElementsByClassName("tabcontent");
                for (i = 0; i < tabcontent.length; i++) {
                    tabcontent[i].style.display = "none";
                }
                tablinks = document.getElementsByClassName("tablinks");
                for (i = 0; i < tablinks.length; i++) {
                    tablinks[i].className = tablinks[i].className.replace(" active", "");
                }
                document.getElementById(tabName).style.display = "block";
                evt.currentTarget.className += " active";
            }
        </script>
    </body>
    </html>
    )rawliteral";
}

void ServerController::Server_Handle_Log()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_Log_Start());
    server->sendContent(Server_Log_DeviceLogs());
    server->sendContent(Server_Log_End());

    server->sendContent(""); // flush
}


String ServerController::Server_Cluster_Start()
{
    std::vector<ClusterManager::ClusterNode> nodes = clusterManager->GetClusterNodes();

    String html = R"rawliteral(
        <html>
        <head>
            <meta charset='UTF-8'>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>🍄 Cluster</title>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
            <style>
                body {
                    background-color: #2f3c6b;
                    color: #f0f0f0;
                    font-family: Arial, sans-serif;
                    margin: 0;
                    padding: 0;
                }

                h1 {
                    text-align: center;
                    padding: 20px;
                    margin: 0;
                    color: #f8f8f8;
                }

                .section {
                    max-width: 800px;
                    margin: 40px auto;
                    padding: 20px;
                    background-color: #3a4a82;
                    border-radius: 12px;
                    box-shadow: 0 4px 10px rgba(0,0,0,0.3);
                }

                .node-entry {
                    padding: 10px;
                    margin-bottom: 8px;
                    background-color: #2d3a6a;
                    border-radius: 8px;
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                }

                .node-entry a {
                    color: #79cfff;
                    text-decoration: none;
                }

                .node-entry span {
                    font-size: 14px;
                    color: #ccc;
                }

                .refresh-container {
                    text-align: center;
                    margin-top: 20px;
                }

                .main-button {
                    display: inline-flex;
                    align-items: center;
                    justify-content: center;
                    gap: 10px;
                    padding: 10px 20px;
                    font-size: 16px;
                    background-color: #3498db;
                    color: white;
                    border: none;
                    border-radius: 10px;
                    cursor: pointer;
                    text-decoration: none;
                    transition: background-color 0.3s ease, transform 0.2s ease;
                    box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
                }

                .main-button:hover {
                    background-color: #2980b9;
                    transform: translateY(-2px);
                }

                .info-block {
                    margin-top: 20px;
                    font-size: 13px;
                    text-align: center;
                    color: #aaa;
                }
            </style>
        </head>
        <body>
    )rawliteral";

    html += Server_TopBar("Cluster", "/");

    html += R"rawliteral(
        <div class="section" style="text-align:center; margin-top:20px;">
            <h2><i class="fas fa-cogs"></i> Cluster Commands</h2>

            <!-- Settings -->
            <button class="main-button" onclick="cmdSettingsAll()">
                <i class="fas fa-sliders-h"></i> Apply Settings to All Nodes
            </button>

            <!-- Restart -->
            <button class="main-button" onclick="cmdRestartAll()">
                <i class="fas fa-redo"></i> Restart All Nodes
            </button>

            <div id="cmdResult" style="margin-top:15px; font-size:14px; color:#ddd;"></div>
        </div>

        <script>
            function cmdRestartAll() {
                const btn = event.target;
                btn.disabled = true;
                btn.style.opacity = "0.6";

                fetch('/sendCommandToAllRestart', { method: 'POST' })
                    .then(r => r.text())
                    .then(t => {
                        document.getElementById("cmdResult").innerText = t;
                    })
                    .catch(() => {
                        document.getElementById("cmdResult").innerText =
                            "❌ Failed to send restart command.";
                    })
                    .finally(() => {
                        btn.disabled = false;
                        btn.style.opacity = "1.0";
                    });
            }

            function cmdSettingsAll() {
                const btn = event.target;
                btn.disabled = true;
                btn.style.opacity = "0.6";

                fetch('/sendCommandToAllSettings', { method: 'POST' })
                    .then(r => r.text())
                    .then(t => {
                        document.getElementById("cmdResult").innerText = t;
                    })
                    .catch(() => {
                        document.getElementById("cmdResult").innerText =
                            "❌ Failed to send settings command.";
                    })
                    .finally(() => {
                        btn.disabled = false;
                        btn.style.opacity = "1.0";
                    });
            }
        </script>
    )rawliteral";

    html += R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-network-wired"></i> Discovered Cluster Nodes</h2>
    )rawliteral";

    if (nodes.empty())
    {
        html += R"rawliteral(<p>No cluster nodes found.</p>)rawliteral";
    }
    else
    {
        for (const auto& node : nodes)
        {
            html += "<div class='node-entry'>";
            html += "<a href='" + node.url + "' target='_blank'>" + node.hostName + "</a>";
            html += "<span>" + node.ip.toString() + " - seen " + String((millis() - node.lastSeen) / 1000) + "s ago</span>";
            html += "</div>";
        }
    }

    html += R"rawliteral(
            <div class="refresh-container">
                <button class="main-button" onclick="refreshCluster()">🔄 Refresh</button>
            </div>
        </div>

        <div class="info-block">
            Devices in the cluster are auto-discovered via mDNS and listed here with clickable access links.
        </div>

        <script>
            function refreshCluster() {
                fetch('/clusterScan', { method: 'POST' })
                    .then(() => location.reload())
                    .catch(() => alert("Failed to trigger cluster scan."));
            }
        </script>
    )rawliteral";

    return html;
}

String ServerController::Server_Cluster_End()
{
    return R"rawliteral(
        </body>
        </html>
    )rawliteral";
}

void ServerController::Server_Handle_Cluster()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_Cluster_Start());
    server->sendContent(Server_Cluster_End());

    server->sendContent(""); // flush
}

void ServerController::Server_Handle_ClusterScan()
{
    if (!PreRequest()) return;

    clusterManager->ScanNow();
    server->send(200, "text/plain", "Scan triggered");
}

void ServerController::Server_Handle_SendCommandToAll_Restart()
{
    if (!PreRequest()) return;

    clusterManager->SendCommandToAll_Restart();
    server->send(200, "text/plain", "Restart command sent to all nodes.");
}

void ServerController::Server_Handle_SendCommandToAll_Settings()
{
    if (!PreRequest()) return;

    clusterManager->SendCommandToAll_Settings();
    server->send(200, "text/plain", "Settings command sent to all nodes.");
}

String ServerController::Server_System_Start()
{
    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>🍄 System</title>
    )rawliteral";

    html += ExternalResources();

    html += R"rawliteral(
        <style>
            /* --- Global --- */
            body {
                background-color: #2f3c6b;
                color: #f0f0f0;
                font-family: Arial, sans-serif;
                margin: 0;
                padding: 0;
            }

            h1 {
                text-align: center;
                padding: 20px;
                margin: 0;
                color: #f8f8f8;
            }

            /* --- Layout blocks --- */
            .section {
                max-width: 800px;
                margin: 40px auto;
                padding: 20px;
                background-color: #3a4a82;
                border-radius: 12px;
                box-shadow: 0 4px 10px rgba(0,0,0,0.3);
            }
            .section h2 {
                margin-top: 0;
                color: #ffffff;
            }

            .info-block {
                text-align: center;
                margin-top: 60px;
                font-size: 14px;
                color: #aaa;
            }
            .info-block a {
                color: #bbb;
                text-decoration: underline;
            }
            .info-block a:hover {
                color: #ddd;
            }

            /* --- Buttons --- */
            .button-container {
                text-align: center;
                margin: 20px 0;
            }
            .main-button {
                display: inline-flex;
                align-items: center;
                justify-content: center;
                gap: 10px;
                margin: 10px;
                padding: 15px 30px;
                font-size: 16px;
                background-color: #3498db;
                color: white;
                border: none;
                border-radius: 10px;
                cursor: pointer;
                text-decoration: none;
                transition: background-color 0.3s ease, transform 0.2s ease;
                box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            }
            .main-button:hover {
                background-color: #2980b9;
                transform: translateY(-2px);
            }
            .main-button i {
                font-size: 18px;
            }

            /* --- Forms --- */
            form {
                display: flex;
                flex-direction: column;
                gap: 12px;
                margin-top: 10px;
            }
            form label {
                font-weight: bold;
            }
            form input[type="text"],
            form input[type="password"],
            form input[type="number"],
            form select {
                width: 100%;
                padding: 8px 10px;
                border-radius: 6px;
                border: 1px solid #888;
                background-color: #2f3c6b;
                color: #fff;
            }

            /* --- Password field with toggle --- */
            .password-wrapper {
                position: relative;
                width: 100%;
            }
            .toggle-password {
                position: absolute;
                right: 10px;
                top: 50%;
                transform: translateY(-50%);
                cursor: pointer;
                font-size: 18px;
                user-select: none;
            }

            /* --- File upload row (Firmware) --- */
            .file-row {
                display: flex;
                align-items: center;
                gap: 10px;
            }
            .file-label {
                background-color: #5566aa;
                color: #fff;
                padding: 10px 15px;
                border-radius: 8px;
                cursor: pointer;
                display: inline-flex;
                align-items: center;
                gap: 8px;
                transition: background-color 0.3s ease;
            }
            .file-label:hover {
                background-color: #445599;
            }
            .file-input {
                display: none;
            }
            .file-name {
                font-size: 14px;
                color: #ddd;
            }
            .firmware-buttons {
                display: flex;
                justify-content: space-between;
                width: 100%;
                gap: 15px;
            }
            .firmware-buttons .main-button {
                flex: 1;
                text-align: center;
            }

            /* --- Inline toggle (Localization) --- */
            .inline-toggle {
                display: flex;
                align-items: center;
                gap: 15px;
            }
            .inline-toggle label {
                font-weight: normal;
            }
            .localization-buttons,
            .auth-buttons,
            .network-buttons {
                display: flex;
                justify-content: flex-end;
                margin-top: 15px;
            }

            form input[type="date"],
            form input[type="time"],
            form input[type="datetime-local"] {
                width: auto;
                min-width: 200px;
                max-width: 250px;
                padding: 8px 10px;
                border-radius: 6px;
                border: 1px solid #888;
                background-color: #2f3c6b;
                color: #fff;
                font-size: 14px;
                flex-shrink: 0;
            }

            /* Kolor placeholdera w dark theme */
            form input[type="date"]::placeholder,
            form input[type="time"]::placeholder {
                color: #bbb;
            }

            /* Wyrównanie ikon w pickerze */
            form input[type="date"]::-webkit-calendar-picker-indicator,
            form input[type="time"]::-webkit-calendar-picker-indicator {
                filter: invert(1);
                cursor: pointer;
            }

            .time-chip {
                display: inline-block;
                margin: 0 auto 20px auto;
                padding: 10px 20px;
                background-color: #25335c;
                border-radius: 12px;
                box-shadow: 0 2px 6px rgba(0,0,0,0.3);
                font-size: 16px;
                text-align: center;
                color: #fff;
                font-weight: bold;
            }
        </style>
    </head>
    <body>
    )rawliteral";

    html += Server_TopBar("System", "/");

    return html;
}

String ServerController::Server_System_Authorization()
{
    return R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-user-shield"></i> Authorization</h2>
            <form id="authForm">
                <label for="adminLogin">Admin Login</label>
                <input type="text" id="adminLogin" name="adminLogin" placeholder="Enter admin login"
                       value=")rawliteral" + login + R"rawliteral(" required>

                <label for="adminPassword">Admin Password</label>
                <div class="password-wrapper">
                    <input type="password" id="adminPassword" name="adminPassword" placeholder="Enter admin password"
                           value=")rawliteral" + password + R"rawliteral(" required>
                    <span class="toggle-password" onclick="toggleAuthPassword()">👁️</span>
                </div>

                <div class="auth-buttons">
                    <button type="submit" class="main-button">
                        <i class="fas fa-save"></i> Save & Restart
                    </button>
                </div>
                <p id="authStatus" style="margin-top:10px; font-size:14px;"></p>
            </form>

            <div class="info-block">
                ℹ️ This device does not send any private data outside your router. 
                Browser warnings about an "insecure connection" simply indicate the absence of HTTPS encryption 
                and <b>do not represent a security risk</b> when used in your local network.
            </div>
        </div>

        <script>
            document.getElementById("authForm").addEventListener("submit", function(e) {
                e.preventDefault();
                const statusMsg = document.getElementById("authStatus");

                const formData = new FormData(this);
                const xhr = new XMLHttpRequest();

                statusMsg.textContent = "Saving admin credentials...";

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        statusMsg.textContent = xhr.responseText;
                        setTimeout(() => {
                            location.reload();
                        }, 8000);
                    } else {
                        statusMsg.textContent = "Error: " + xhr.responseText;
                    }
                };

                xhr.onerror = function() {
                    statusMsg.textContent = "Connection error while saving credentials.";
                };

                xhr.open("POST", "/auth", true);
                xhr.send(formData);
            });

            function toggleAuthPassword() {
                const pwd = document.getElementById("adminPassword");
                pwd.type = pwd.type === "password" ? "text" : "password";
            }
        </script>
    )rawliteral";
}

String ServerController::Server_System_Firmware()
{
    return R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-microchip"></i> Firmware</h2>
            <div style="margin-bottom:15px;">
                <p><b>App Version:</b> )rawliteral" + AppVersion() + R"rawliteral(</p>
                <p><b>Build Date:</b> )rawliteral" + String(__DATE__) + " " + String(__TIME__) + R"rawliteral(</p>
            </div>

            <form id="otaForm" enctype="multipart/form-data">
                <div class="file-row">
                    <label for="firmware" class="file-label">
                        <i class="fas fa-file-upload"></i> Choose firmware (.bin)
                    </label>
                    <input type="file" id="firmware" name="firmware" class="file-input" required>
                    <span id="fileName" class="file-name"></span>
                </div>

                <progress id="progressBar" value="0" max="100" style="width:100%; display:none; margin-top:10px;"></progress>
                <p id="statusMsg" style="margin-top:10px; font-size:14px;"></p>

                <div class="firmware-buttons">
                    <button type="submit" class="main-button">
                        <i class="fas fa-upload"></i> Upload & Update
                    </button>
                    <button type="button" class="main-button" onclick="restartDevice()">
                        <i class="fas fa-sync-alt"></i> Restart
                    </button>
                </div>
            </form>
        </div>

        <script>
            document.getElementById("firmware").addEventListener("change", function() {
                const fileNameSpan = document.getElementById("fileName");
                if (this.files && this.files.length > 0) {
                    fileNameSpan.textContent = this.files[0].name;
                } else {
                    fileNameSpan.textContent = "";
                }
            });

            document.getElementById("otaForm").addEventListener("submit", function(e) {
                e.preventDefault();
                const fileInput = document.getElementById("firmware");
                if (!fileInput.files.length) {
                    alert("Please select a .bin file first.");
                    return;
                }

                const file = fileInput.files[0];
                const formData = new FormData();
                formData.append("firmware", file);

                const xhr = new XMLHttpRequest();
                const progressBar = document.getElementById("progressBar");
                const statusMsg = document.getElementById("statusMsg");

                progressBar.style.display = "block";
                statusMsg.textContent = "Uploading...";

                xhr.upload.onprogress = function(event) {
                    if (event.lengthComputable) {
                        const percent = Math.round((event.loaded / event.total) * 100);
                        progressBar.value = percent;
                        statusMsg.textContent = "Uploading: " + percent + "%";
                    }
                };

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        statusMsg.textContent = "Update successful. Device will reboot...\n\nPlease reload the page after a moment.";
                        setTimeout(() => {
                            location.reload();
                        }, 8000);
                    } else {
                        statusMsg.textContent = "Error: " + xhr.responseText;
                    }
                };

                xhr.onerror = function() {
                    statusMsg.textContent = "Upload failed. Connection error.";
                };

                xhr.open("POST", "/ota", true);
                xhr.send(formData);
            });

            function restartDevice() {
                if (confirm("Are you sure you want to restart the device?")) {
                    fetch('/restart', { method: 'POST' })
                        .then(response => {
                            if (response.ok) {
                                alert("Device is restarting...\n\nPlease reload the page after a moment.");
                            } else {
                                alert("Failed to restart device");
                            }
                        })
                        .catch(() => {
                            alert("Failed to contact device");
                        });
                }
            }
        </script>
    )rawliteral";
}

String ServerController::Server_System_Network()
{
    String mode = (WiFi.getMode() & WIFI_AP) ? "Access Point" : "Station (Client)";
    String ssid = WiFi.SSID();
    if (ssid.length() == 0) ssid = "(not connected)";

    String ip = ToolBox::IsStationConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    String mdnsName = ToolBox::IsStationConnected() ? String(sharedResourceManager->UnitName_Get().c_str()) + ".local" : "(not available)";

    std::string savedSSID = sharedResourceManager->NetworkSSID_Get();
    std::string savedPass = sharedResourceManager->NetworkPassword_Get();

    return R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-wifi"></i> Network</h2>
            <div style="margin-bottom:15px;">
                <p><b>Mode:</b> )rawliteral" + mode + R"rawliteral(</p>
                <p><b>SSID:</b> )rawliteral" + ssid + R"rawliteral(</p>
                <p><b>IP Address:</b> )rawliteral" + ip + R"rawliteral(</p>
                <p><b>mDNS Hostname:</b> )rawliteral" + mdnsName + R"rawliteral(</p>
            </div>

            <form id="networkForm">
                <label for="ssid">WiFi SSID</label>
                <input type="text" id="ssid" name="ssid" placeholder="Network name" 
                       value=")rawliteral" + String(savedSSID.c_str()) + R"rawliteral(" required>

                <label for="password">WiFi Password</label>
                <div class="password-wrapper">
                    <input type="password" id="password" name="password" placeholder="Network password" 
                           value=")rawliteral" + String(savedPass.c_str()) + R"rawliteral(" required>
                    <span class="toggle-password" onclick="togglePasswordVisibility()">👁️</span>
                </div>

                <div class="network-buttons">
                    <button type="submit" class="main-button">
                        <i class="fas fa-save"></i> Save & Restart
                    </button>
                </div>
                <p id="networkStatus" style="margin-top:10px; font-size:14px;"></p>
            </form>
        </div>

        <script>
            document.getElementById("networkForm").addEventListener("submit", function(e) {
                e.preventDefault();
                const statusMsg = document.getElementById("networkStatus");

                const formData = new FormData(this);
                const xhr = new XMLHttpRequest();

                statusMsg.textContent = "Saving WiFi settings...";

                xhr.onload = function() {
                    if (xhr.status === 200) {
                        statusMsg.textContent = xhr.responseText;
                        setTimeout(() => {
                            location.reload();
                        }, 8000);
                    } else {
                        statusMsg.textContent = "Error: " + xhr.responseText;
                    }
                };

                xhr.onerror = function() {
                    statusMsg.textContent = "Connection error while saving WiFi.";
                };

                xhr.open("POST", "/network", true);
                xhr.send(formData);
            });

            function togglePasswordVisibility() {
                const pwd = document.getElementById("password");
                pwd.type = pwd.type === "password" ? "text" : "password";
            }
        </script>
    )rawliteral";
}

String ServerController::Server_System_Localization()
{
    String localBuffer = clientNTP->GetLocalTimeString();
    bool isAuto = sharedResourceManager->TimeAutomatic_Get();

    std::vector<std::string> tzList = ToolBox::GetTimezoneList();
    std::string currentTz = sharedResourceManager->TimezoneLocation_Get();

    String tzOptions;
    for (const auto& tz : tzList)
    {
        bool selected = (tz == currentTz);
        tzOptions += "<option value='" + String(tz.c_str()) + "'";
        if (selected) tzOptions += " selected";
        tzOptions += ">" + String(tz.c_str()) + "</option>";
    }

    return R"rawliteral(
        <div class="section">
            <h2><i class="fas fa-clock"></i> Time & Localization</h2>

            <div class="time-chip">
                <b>Local Time:</b> )rawliteral" + localBuffer + R"rawliteral(
            </div>

            <!-- Time settings -->
            <form id="timeForm" autocomplete="off">
                <div class="inline-toggle">
                    <label><b>Time Mode:</b></label>
                    <label>
                        <input type="radio" name="timeMode" value="auto" onchange="toggleTimeMode()" )rawliteral" + String(isAuto ? "checked" : "") + R"rawliteral(> Automatic (NTP)
                    </label>
                    <label>
                        <input type="radio" name="timeMode" value="manual" onchange="toggleTimeMode()" )rawliteral" + String(!isAuto ? "checked" : "") + R"rawliteral(> Manual
                    </label>
                </div>

                <!-- AUTO mode -->
                <div id="autoTimeSelect" style="margin-top:15px;">
                    <label for="timezone">Select Timezone</label>
                    <select id="timezone" name="timezone">
    )rawliteral" + tzOptions + R"rawliteral(
                    </select>

                    <div class="info-block" style="margin-top:10px; text-align:left; font-size:13px; color:#bbb;">
                        ℹ️ When enabled, the device automatically synchronizes its internal clock with trusted NTP servers at regular intervals. 
                        No personal or sensitive data is ever transmitted during this process.
                    </div>
                </div>

                <!-- MANUAL mode -->
                <div id="manualTimeInput" style="display:none; margin-top:15px;">
                    <label for="manualDateTime">Set Date & Time</label>
                    <div style="display:flex; gap:10px; align-items:center;">
                        <input type="datetime-local" id="manualDateTime" name="manualDateTime" step="1">
                        <button type="button" class="main-button small" onclick="setSystemDateTime()">
                            <i class="fas fa-clock"></i> Set from system
                        </button>
                    </div>
                </div>

                <!-- Buttons -->
                <div id="autoButtons" class="localization-buttons">
                    <button type="submit" class="main-button">
                        <i class="fas fa-save"></i> Save & Restart
                    </button>
                </div>

                <div id="manualButtons" class="localization-buttons" style="display:none;">
                    <button type="submit" class="main-button">
                        <i class="fas fa-save"></i> Save
                    </button>
                </div>

                <p id="tzStatus" style="margin-top:10px; font-size:14px;"></p>
            </form>

            <div class="info-block">
                ℹ️ To change the UI language, please visit:
                <a href=")rawliteral" + String(WEBSITE_URL) + R"rawliteral(" target="_blank">)rawliteral" + String(WEBSITE_URL) + R"rawliteral(</a> and download the update file.
            </div>
        </div>

        <script>
            function toggleTimeMode() {
                const isAuto = document.querySelector('input[name="timeMode"][value="auto"]').checked;
                document.getElementById('autoTimeSelect').style.display = isAuto ? 'block' : 'none';
                document.getElementById('manualTimeInput').style.display = isAuto ? 'none' : 'block';

                document.getElementById('autoButtons').style.display = isAuto ? 'flex' : 'none';
                document.getElementById('manualButtons').style.display = isAuto ? 'none' : 'flex';
            }

            window.onload = function() {
                toggleTimeMode();
            }

            function setSystemDateTime() {
                const now = new Date();
                const yyyy = now.getFullYear();
                const mm = String(now.getMonth() + 1).padStart(2, '0');
                const dd = String(now.getDate()).padStart(2, '0');
                const hh = String(now.getHours()).padStart(2, '0');
                const min = String(now.getMinutes()).padStart(2, '0');
                const ss = String(now.getSeconds()).padStart(2, '0');
                document.getElementById("manualDateTime").value = `${yyyy}-${mm}-${dd}T${hh}:${min}:${ss}`;
            }

            document.getElementById("timeForm").addEventListener("submit", function(e) {
                e.preventDefault();
                const statusMsg = document.getElementById("tzStatus");
                statusMsg.textContent = "Saving time settings...";

                const formData = new FormData(this);
                const xhr = new XMLHttpRequest();
                xhr.onload = function() {
                    statusMsg.textContent = (xhr.status === 200) ? xhr.responseText : "Error: " + xhr.responseText;
                    if (xhr.status === 200) {
                        setTimeout(() => location.reload(), 8000);
                    }
                };
                xhr.onerror = function() {
                    statusMsg.textContent = "Connection error while saving time settings.";
                };
                xhr.open("POST", "/timeAndLoc", true);
                xhr.send(formData);
            });
        </script>
    )rawliteral";
}

String ServerController::Server_System_End()
{
    return R"rawliteral(
    </body>
    </html>
    )rawliteral";
}

void ServerController::Server_Handle_System()
{
    if (!PreRequest()) return;

    server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    server->send(200, "text/html; charset=utf-8", "");

    server->sendContent(Server_System_Start());
    server->sendContent(Server_System_Authorization());
    server->sendContent(Server_System_Firmware());
    server->sendContent(Server_System_Network());
    server->sendContent(Server_System_Localization());
    server->sendContent(Server_System_End());

    server->sendContent(""); // flush
}

void ServerController::Server_Handle_Auth()
{
    if (!PreRequest()) return;

    if (!server->hasArg("adminLogin") || !server->hasArg("adminPassword"))
    {
        server->send(400, "text/plain", "Missing login or password");
        return;
    }

    String newLogin = server->arg("adminLogin");
    String newPass  = server->arg("adminPassword");

    // Walidacja loginu
    if (newLogin.length() < 3)
    {
        server->send(400, "text/plain", "Login must be at least 3 characters long");
        return;
    }
    for (size_t i = 0; i < newLogin.length(); i++)
    {
        char c = newLogin[i];
        if (!(isalnum(c) || c == '_' || c == '-'))
        {
            server->send(400, "text/plain", "Login can contain only letters, digits, '_' and '-'");
            return;
        }
    }

    // Walidacja hasła
    if (newPass.length() < 3)
    {
        server->send(400, "text/plain", "Password must be at least 3 characters long");
        return;
    }

    // zapisz nowe dane
    sharedResourceManager->AccountLogin_Set(newLogin.c_str());
    sharedResourceManager->AccountPassword_Set(newPass.c_str());
    server->send(200, "text/plain", "Admin credentials saved. Device will restart...");
    delay(200);
    ESP.restart();
}

void ServerController::Server_Handle_Restart()
{
    if (!PreRequest()) return;

    server->send(200, "text/plain", "Device restarting...");
    delay(200);                 // daj chwilę na wysłanie odpowiedzi zanim ESP się zrestartuje
    ESP.restart();
}

void ServerController::Server_Handle_OTAUpload()
{
    if (!PreRequest()) return;

    HTTPUpload& upload = server->upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        if (!upload.filename.endsWith(".bin"))
        {
            server->send(400, "text/plain", "Invalid file type. Only .bin allowed.");
            ::Update.abort();
            return;
        }

        if (!::Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            ::Update.printError(Serial);
            ::Update.abort();
        }
    } 
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (::Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            ::Update.printError(Serial);
            ::Update.abort();
        }
    } 
    else if (upload.status == UPLOAD_FILE_END)
    {
        Serial.printf("OTA Upload finished. Size: %u bytes\n", upload.totalSize);
    }
}

void ServerController::Server_Handle_OTAResult()
{
    if (!PreRequest()) return;

    if (::Update.hasError())
    {
        ::Update.abort();
        server->send(500, "text/plain", "OTA update failed (hasError). Aborted.");
        return;
    }

    if (!::Update.end(true))
    {
        String err = ::Update.errorString();
        ::Update.abort();
        server->send(500, "text/plain", "OTA update failed: " + err);
        return;
    }

    if (!::Update.isFinished())
    {
        ::Update.abort();
        server->send(500, "text/plain", "Update not finished. Aborted.");
        return;
    }

    server->send(200, "text/plain", "OTA successful. Rebooting...");
    delay(200);
    ESP.restart();
}

void ServerController::Server_Handle_Network()
{
    if (!PreRequest()) return;

    if (!server->hasArg("ssid") || !server->hasArg("password")) {
        server->send(400, "text/plain", "Missing SSID or password");
        return;
    }

    String ssid = server->arg("ssid");
    String pass = server->arg("password");

    if (ssid.length() == 0) {
        server->send(400, "text/plain", "SSID cannot be empty");
        return;
    }

    // zapisz do SharedResourceManager
    sharedResourceManager->NetworkSSID_Set(ssid.c_str());
    sharedResourceManager->NetworkPassword_Set(pass.c_str());
    server->send(200, "text/plain", "WiFi settings saved. Device will restart...");
    delay(200);
    ESP.restart();
}

void ServerController::Server_Handle_TimeAndLoc()
{
    if (!PreRequest()) return;

    if (!server->hasArg("timeMode")) 
    {
        server->send(400, "text/plain", "Missing timeMode");
        return;
    }

    String mode = server->arg("timeMode");

    if (mode == "auto") 
    {
        if (!server->hasArg("timezone")) 
        {
            server->send(400, "text/plain", "Missing timezone");
            return;
        }

        String tz = server->arg("timezone");

        // ✅ walidacja timezone
        if (ToolBox::FindTimezoneIndexByName(tz.c_str()) < 0) 
        {
            server->send(400, "text/plain", "Invalid timezone");
            return;
        }

        sharedResourceManager->TimeAutomatic_Set(true);
        sharedResourceManager->TimezoneLocation_Set(tz.c_str());

        server->send(200, "text/plain", "Time set to AUTO mode with timezone. Device will restart...");
        delay(200);
        ESP.restart();
    } 
    else if (mode == "manual") 
    {
        if (!server->hasArg("manualDateTime")) 
        {
            server->send(400, "text/plain", "Missing manualDateTime");
            return;
        }

        String dt = server->arg("manualDateTime");

        sharedResourceManager->TimeAutomatic_Set(false);
        sharedResourceManager->TimezoneLocation_Set("Etc/UTC");

        // Parsowanie: "YYYY-MM-DDTHH:MM:SS"
        if (dt.length() >= 19 && dt.charAt(10) == 'T') 
        {
            int yyyy = dt.substring(0,4).toInt();
            int MM   = dt.substring(5,7).toInt();
            int dd   = dt.substring(8,10).toInt();
            int hh   = dt.substring(11,13).toInt();
            int mm   = dt.substring(14,16).toInt();
            int ss   = dt.substring(17,19).toInt();

            struct tm t;
            memset(&t, 0, sizeof(struct tm));
            t.tm_year = yyyy - 1900;
            t.tm_mon  = MM - 1;
            t.tm_mday = dd;
            t.tm_hour = hh;
            t.tm_min  = mm;
            t.tm_sec  = ss;

            time_t epoch = mktime(&t);
            if (epoch == -1) 
            {
                server->send(400, "text/plain", "Failed to convert manualDateTime");
                return;
            }
            clientNTP->SetUnixTime(epoch);

            server->send(200, "text/plain", "Manual time set successfully.");
        } 
        else 
        {
            server->send(400, "text/plain", "Invalid manualDateTime format");
        }
    } 
    else 
    {
        server->send(400, "text/plain", "Invalid timeMode value");
    }
}

void ServerController::Server_Handle_Diagnostics()
{
    if (!PreRequest()) return;

    const UBaseType_t maxTasks = 32;
    TaskStatus_t taskStatus[maxTasks];
    UBaseType_t taskCount = uxTaskGetSystemState(taskStatus, maxTasks, nullptr);

    const char* systemTasks[] = {
        "IDLE0", "IDLE1", "loopTask", "ipc0", "ipc1", "sys_evt",
        "Tmr Svc", "esp_timer", "mdns", "arduino_events", "wifi", "tiT"
    };
    const size_t systemTaskCount = sizeof(systemTasks) / sizeof(systemTasks[0]);

    String userResponse;
    String systemResponse;

    for (UBaseType_t i = 0; i < taskCount; ++i)
    {
        String taskName = taskStatus[i].pcTaskName;
        UBaseType_t freeBytes = taskStatus[i].usStackHighWaterMark * sizeof(StackType_t);

        bool isSystem = false;
        for (size_t j = 0; j < systemTaskCount; ++j)
        {
            if (taskName == systemTasks[j])
            {
                isSystem = true;
                break;
            }
        }

        if (isSystem)
        {
            systemResponse += taskName + ": " + String(freeBytes) + " bytes\n";
        }
        else
        {
            String statusEmoji;
            if (freeBytes < 512)
                statusEmoji = "🟥 ";
            else if (freeBytes < 1024)
                statusEmoji = "🟨 ";
            else
                statusEmoji = "🟩 ";

            userResponse += statusEmoji + taskName + ": " + String(freeBytes) + " bytes\n";
        }
    }

    // Heap diagnostics
    size_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t minFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    size_t internalHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);  // jeśli masz PSRAM

    String diag;
    diag += "\n=== Heap / System Diagnostics ===\n";
    diag += "Free heap (8bit): " + String(freeHeap) + " bytes\n";
    diag += "Min. ever heap: " + String(minFreeHeap) + " bytes\n";
    diag += "Internal heap: " + String(internalHeap) + " bytes\n";
    diag += "PSRAM heap: " + String(psramFree) + " bytes\n";
    diag += "RTOS task count: " + String(taskCount) + "\n";

    // Uptime
    unsigned long uptimeSec = millis() / 1000;
    unsigned long hours = uptimeSec / 3600;
    unsigned long minutes = (uptimeSec % 3600) / 60;
    unsigned long seconds = uptimeSec % 60;
    diag += "\n=== Uptime ===\n";
    diag += "Uptime: " + String(hours) + "h " + String(minutes) + "m " + String(seconds) + "s\n";

    // Reset reason
    esp_reset_reason_t reason = esp_reset_reason();
    String resetReason;
    switch (reason)
    {
        case ESP_RST_POWERON:  resetReason = "Power on"; break;
        case ESP_RST_EXT:      resetReason = "External reset"; break;
        case ESP_RST_SW:       resetReason = "Software reset"; break;
        case ESP_RST_PANIC:    resetReason = "Exception/Panic"; break;
        case ESP_RST_INT_WDT:  resetReason = "Interrupt watchdog"; break;
        case ESP_RST_TASK_WDT: resetReason = "Task watchdog"; break;
        case ESP_RST_DEEPSLEEP:resetReason = "Wake from deep sleep"; break;
        case ESP_RST_BROWNOUT: resetReason = "Brownout"; break;
        case ESP_RST_SDIO:     resetReason = "SDIO"; break;
        default:               resetReason = "Unknown"; break;
    }
    diag += "Last reset reason: " + resetReason + "\n";

    String response;
    response += userResponse;
    response += "\nSystem tasks:\n";
    response += systemResponse;
    response += diag;

    server->send(200, "text/plain; charset=utf-8", response);
}

void ServerController::Server_Handle_Diagnostics_HumidityMasked()
{
    if (!PreRequest()) return;

    // 1. Pobierz dane surowe i maskowane
    auto logs = sharedResourceManager->AirLogs_Get(288);

    String labels;
    String humiditiesRaw;
    String humiditiesMasked;

    for (const auto& log : logs)
    {
        std::time_t t = log.unixTime;

        // Format czasu do labeli (np. 13:05)
        char timeBuffer[20];
        struct tm *timeInfo = localtime(&t);
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M", timeInfo);
        labels += "\"" + String(timeBuffer) + "\",";

        // Dane surowe
        humiditiesRaw += String(log.humidity) + ",";

        // Dane z maskowaniem
        if (sharedResourceManager->IsMaskedByFAE(t))
            humiditiesMasked += "null,";
        else
            humiditiesMasked += String(log.humidity) + ",";
    }

    labels.remove(labels.length() - 1);
    humiditiesRaw.remove(humiditiesRaw.length() - 1);
    humiditiesMasked.remove(humiditiesMasked.length() - 1);

    // 2. Złóż HTML
    String html = R"rawliteral(
    <html>
    <head>
        <meta charset='UTF-8'>
        <title>Humidity Mask Diagnostic</title>
        <script src="/chart.js"></script>
    </head>
    <body>
        <h2>Humidity (raw)</h2>
        <div style="height:300px;">
            <canvas id="chartRaw"></canvas>
        </div>

        <h2>Humidity (masked)</h2>
        <div style="height:300px;">
            <canvas id="chartMasked"></canvas>
        </div>

        <script>
            const labels = [)rawliteral" + labels + R"rawliteral(];
            const dataRaw = [)rawliteral" + humiditiesRaw + R"rawliteral(];
            const dataMasked = [)rawliteral" + humiditiesMasked + R"rawliteral(];

            function drawChart(canvasId, data, color) {
                new Chart(document.getElementById(canvasId).getContext("2d"), {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [{
                            data: data,
                            borderColor: color,
                            backgroundColor: color.replace('1)', '0.2)'),
                            borderWidth: 1,
                            spanGaps: false
                        }]
                    },
                    options: {
                        responsive: true,
                        maintainAspectRatio: false,   // <-- kluczowa linia
                        plugins: {
                            legend: { display: false },
                            title: { display: false }
                        },
                        scales: {
                            y: {
                                title: {
                                    display: true,
                                    text: 'Humidity (%)'
                                },
                                min: 30,
                                max: 100
                            },
                            x: {
                                title: {
                                    display: true,
                                    text: 'Time'
                                }
                            }
                        }
                    }
                });
            }

            drawChart("chartRaw", dataRaw, "rgba(0, 200, 255, 1)");
            drawChart("chartMasked", dataMasked, "rgba(255, 165, 0, 1)");
        </script>
    </body>
    </html>
    )rawliteral";

    server->send(200, "text/html; charset=utf-8", html);
}

void ServerController::Server_Handle_ChartJS()
{
    if (!PreRequest()) return;

    server->sendHeader("Content-Encoding", "gzip");
    server->sendHeader("Cache-Control", "public, max-age=2592000, immutable");       // cache for 30 days
    server->setContentLength(chart_js_gz_len);                                           // musi być podana, bo sendContent_P nie działa z CONTENT_LENGTH_UNKNOWN
    server->send(200, "application/javascript", "");                                         // wysyła nagłówki
    server->sendContent_P((PGM_P)chart_js_gz, chart_js_gz_len);                                  // wysyła cały plik z PROGMEM
}

void ServerController::BootProcedure()
{
    if (server == NULL)
    {
        // Wait for network connection because starting the server before the WiFi is connected can cause exceptions.
        while (!ToolBox::IsNetworkAvailable())
            delay(1000);

        server = new WebServer(80);

        ////////////////////////// Root page //////////////////////////
        server->on("/", std::bind(&ServerController::Server_Handle_Root, this));
        server->on("/updateUnitName", std::bind(&ServerController::Server_Handle_UpdateUnitName, this));

        ////////////////////////// Sensor page //////////////////////////
        server->on("/sensor", std::bind(&ServerController::Server_Handle_Sensor, this));

        ////////////////////////// Control page //////////////////////////
        server->on("/control", std::bind(&ServerController::Server_Handle_Control, this));
        server->on("/toggleLight", std::bind(&ServerController::Server_Handle_ToggleLight, this));
        server->on("/setLight", std::bind(&ServerController::Server_Handle_SetLight, this));
        server->on("/toggleHumid", std::bind(&ServerController::Server_Handle_ToggleHumid, this));
        server->on("/setHumid", std::bind(&ServerController::Server_Handle_SetHumid, this));
        server->on("/toggleIncubation", std::bind(&ServerController::Server_Handle_ToggleIncubation, this));
        server->on("/setIncubation", std::bind(&ServerController::Server_Handle_SetIncubation, this));
        server->on("/updateCycle", std::bind(&ServerController::Server_Handle_UpdateCycle, this));
        server->on("/resetCycle", std::bind(&ServerController::Server_Handle_ResetCycle, this));
        server->on("/importCycle", std::bind(&ServerController::Server_Handle_ImportCycle, this));
        server->on("/addDay", std::bind(&ServerController::Server_Handle_AddDay, this));
        server->on("/removeDay", std::bind(&ServerController::Server_Handle_RemoveDay, this));
        server->on("/cloneDay", std::bind(&ServerController::Server_Handle_CloneDay, this));
        server->on("/updateLight", std::bind(&ServerController::Server_Handle_UpdateLight, this));
        server->on("/getDayData", std::bind(&ServerController::Server_Handle_GetDayData, this));
        server->on("/addHour", std::bind(&ServerController::Server_Handle_AddHour, this));
        server->on("/removeHour", std::bind(&ServerController::Server_Handle_RemoveHour, this));
        server->on("/updateHour", std::bind(&ServerController::Server_Handle_UpdateHour, this));

        ////////////////////////// Log page //////////////////////////
        server->on("/log", std::bind(&ServerController::Server_Handle_Log, this));

        ////////////////////////// Cluster page //////////////////////////
        server->on("/cluster", std::bind(&ServerController::Server_Handle_Cluster, this));
        server->on("/clusterScan", std::bind(&ServerController::Server_Handle_ClusterScan, this));
        server->on("/sendCommandToAllRestart", std::bind(&ServerController::Server_Handle_SendCommandToAll_Restart, this));
        server->on("/sendCommandToAllSettings", std::bind(&ServerController::Server_Handle_SendCommandToAll_Settings, this));

        ////////////////////////// System page //////////////////////////
        server->on("/system", std::bind(&ServerController::Server_Handle_System, this));
        server->on("/auth", std::bind(&ServerController::Server_Handle_Auth, this));
        server->on("/restart", std::bind(&ServerController::Server_Handle_Restart, this));
        server->on("/ota", HTTP_POST, std::bind(&ServerController::Server_Handle_OTAResult, this), std::bind(&ServerController::Server_Handle_OTAUpload, this));
        server->on("/network", std::bind(&ServerController::Server_Handle_Network, this));
        server->on("/timeAndLoc", std::bind(&ServerController::Server_Handle_TimeAndLoc, this));


        ////////////////////////// Other page //////////////////////////
        server->on("/diagnostics", std::bind(&ServerController::Server_Handle_Diagnostics, this));
        server->on("/diagnostics/humidityMasked", std::bind(&ServerController::Server_Handle_Diagnostics_HumidityMasked, this));
        server->on("/chart.js", std::bind(&ServerController::Server_Handle_ChartJS, this));

        server->begin();
        Serial.println("(ServerController) boot complete");
    }

    SetState(State::Serving);
    WaitForNextState(portMAX_DELAY);
}

void ServerController::ServingProcedure()
{
    while (true)
    {
        server->handleClient();
        yield();
    }
}