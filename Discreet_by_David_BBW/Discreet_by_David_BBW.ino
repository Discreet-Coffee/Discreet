#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <dimmable_light.h>
#include <Wire.h>
#include "max6675.h"
#include <PID_v1.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <NimBLEDevice.h>
#include <WebSocketsServer.h> // Fixed: Network debugging streaming framework

// ============================================================================
// SYSTEM PINS & CONSTANTS
// ============================================================================
const uint8_t SD_CS = 32;
const uint8_t SD_MOSI = 25;
const uint8_t SD_MISO = 26;
const uint8_t SD_SCK = 33;
const uint8_t SSR_PIN = 13;
const uint8_t syncPin = 19;
const uint8_t thyristorPin = 18;
const uint8_t thermoCS = 22;
const uint8_t thermoCLK = 23;
const uint8_t thermoDO = 21;
const uint8_t pressurepin = 34;
const uint8_t BUZZER_PIN = 4;
const uint8_t SOLENOID_PIN = 12; // Fixed: New pin allocated to control 3-way exhaust valve

// Bookoo Scale BLE Settings
constexpr char TARGET_MAC_ADDRESS[] = "ee:ee:bd:ed:94:5b";
constexpr char BOOKOO_SERVICE_UUID[] = "0ffe";
constexpr char BOOKOO_CHAR_UUID_DATA[] = "ff11";
constexpr char BOOKOO_CHAR_UUID_CMD[] = "ff12";

const uint8_t PACKET_TARE[] = {0x03, 0x0A, 0x01, 0x00, 0x00, 0x08};
const uint8_t PACKET_RESET[] = {0x03, 0x0A, 0x06, 0x00, 0x00, 0x0F};

// ============================================================================
// GLOBAL STATE ENGINE VARIABLES & THREAD SAFETIES
// ============================================================================
String ssid;
String password;
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // Debug logs streamed over Port 81
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

// FreeRTOS Task and Data Mutex Handles
TaskHandle_t BleTaskHandle = NULL;
SemaphoreHandle_t scaleDataMutex;

// Temperature Noise Filter Arrays
const size_t numReadings = 10;
float readings[numReadings];
size_t readIndex = 0;
float total = 0;
float averageTemp = 0;

// PID Variables
double setpoint, input, output, setpointBoot;
double Kp = 48.0, Ki = 0.2, Kd = 85.0;
bool highTuningsApplied = false;
PID myPID(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);

// Pressure & Pump Control Variables
int pressuresetpoint = 9;
int PrePressureSetpoint = 3;
int PressureTarget = pressuresetpoint;
int pumppower = 0;
int maxPressure = 12;
double currentPressure = 0;

// Timing Loop Controls
const int PRESS_INTERVAL = 50;
const int PID_INTERVAL = 250;
unsigned long lastPIDTime = 0;
unsigned long DimlastUpdate = 0;
unsigned long LastPressCall = 0;
unsigned long lastPrintTime = 0;
unsigned long acDetectedTime = 0;
unsigned long elapsedTime = 0;
int actime = 0;
unsigned long purgeStartTime = 0;  //added 08-28-2026

// Steam & Phase Variables
String brewTemp;
bool steaming = false;
double steamSetpoint;
int offset = 9;
int preinftime = 8;
bool acDetected = false;
bool PIDonly = false;
bool shotStarted = false;
bool pumpPowerSetPreinf = false;
bool pumpPowerSetExtraction = false;
int bloomtime = 0;
int offcount = 0;

DimmableLight light(thyristorPin);

// Smart Scale Threaded Registers (Accessed by multi-cores)
static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pNotifyChr = nullptr;
static NimBLERemoteCharacteristic* pWriteChr = nullptr;
volatile bool scaleConnected = false;
volatile float scaleWeight = 0.00f;
volatile float scaleFlowRate = 0.00f;
volatile int bookooTimerSec = 0;
volatile uint8_t scaleBattery = 0;
volatile unsigned long lastScaleHeartbeatTime = 0;

// Brew-By-Weight Dynamic Configs
float targetWeight = 36.0f;
bool autoTareTriggered = false;
bool weightLimitReached = false;

// Forward Declarations
void runPID();
void GetPressure();
void SetPump();
void steam();
int basePumpPowerForSetpoint(double Pumpsetpoint);
void loadSDConfig();
void startSD();
void startWiFi();
void setupServerRoutes();
void handleUpload();
void handleApplyTheme();
void getCurrentTheme();
void handleFileRequest();
void handleListFiles();
void handleDelete();
void handleTemp();
void handlePressure();
void handleGetValues();
void handleAdjust();
void beepBuzzer(int times, int beepDuration, int pauseDuration);
String getContentType(String filename);
void sendScalePacket(const uint8_t* packet, size_t length);
bool connectToScale();
void handleRouteUploadResponse();
void handleRouteRootIndex();
void handleRouteGetConfig();
void handleRouteSaveConfig();

// Custom Log Broadcaster for OTA Wireless Verification
void debugLog(String message) {
  Serial.println(message);
  webSocket.broadcastTXT(message); 
}

// ============================================================================
// SCALE BLE TELEMETRY PACKET CALLBACK HANDLER
// ============================================================================
void scaleNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (pData == nullptr || length < 20) return;

  uint8_t calculatedXOR = 0;
  for (size_t i = 0; i < length - 1; i++) {
    calculatedXOR ^= pData[i];
  }
  if (calculatedXOR != pData[length - 1]) return;

  // Protect variables from shared-memory thread corruption
  if (xSemaphoreTake(scaleDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    lastScaleHeartbeatTime = millis();
    scaleConnected = true;

    uint32_t elapsedMillis = ((uint32_t)pData[3] << 16) | ((uint32_t)pData[4] << 8) | pData[5];
    bookooTimerSec = elapsedMillis / 1000;

    bool isWeightNegative = (pData[6] == 0x01);
    uint32_t weightRaw = ((uint32_t)pData[7] << 16) | ((uint32_t)pData[8] << 8) | pData[9];
    float tempWeight = (float)weightRaw / 100.0f;
    scaleWeight = isWeightNegative ? (tempWeight * -1.0f) : tempWeight;

    bool isFlowNegative = (pData[10] == 0x01);
    uint16_t flowRaw = ((uint16_t)pData[11] << 8) | pData[12];
    float tempFlow = (float)flowRaw / 100.0f;
    scaleFlowRate = isFlowNegative ? (tempFlow * -1.0f) : tempFlow;

    scaleBattery = pData[13];

    // Fixed: Stabilized predictive fluid-stop formula with rolling latency offset
  if (shotStarted && !weightLimitReached) {
    float currentFlow = (scaleFlowRate > 0.05f) ? scaleFlowRate : 0.0f; 
    float systemLatencyFactor = 1.85f; // Calibrated system response lag
    float predictedFinalWeight = scaleWeight + (currentFlow * systemLatencyFactor);

  if (predictedFinalWeight >= targetWeight) {
    weightLimitReached = true; // This safely hands control directly over to our Page 12 timer block
    debugLog("[PREDICTIVE STOP] Target triggered. Commencing puck purge...");
    beepBuzzer(2, 80, 80);
	}
}
    xSemaphoreGive(scaleDataMutex);
  }
}

class ScaleClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* pClient) {
    scaleConnected = true;
    debugLog("[SCALE] BLE Connection Established!");
  }
  void onDisconnect(NimBLEClient* pClient) {
    scaleConnected = false;
    pNotifyChr = nullptr;
    pWriteChr = nullptr;
    debugLog("[SCALE] Warning - Connection dropped!");
  }
};

void sendScalePacket(const uint8_t* packet, size_t length) {
  if (!scaleConnected || pWriteChr == nullptr) return;
  pWriteChr->writeValue(packet, length, false);
}

bool connectToScale() {
  debugLog("[SCALE] Locating peripheral node...");
  NimBLEAddress scaleAddress(std::string(TARGET_MAC_ADDRESS), BLE_ADDR_RANDOM);
  
  if (pClient != nullptr) {
    NimBLEDevice::deleteClient(pClient);
  }
  pClient = NimBLEDevice::createClient();
  if (!pClient) return false;
  
  pClient->setClientCallbacks(new ScaleClientCallbacks());
  if (!pClient->connect(scaleAddress)) return false;

  NimBLERemoteService* pService = pClient->getService(BOOKOO_SERVICE_UUID);
  if (!pService) {
    pClient->disconnect();
    return false;
  }
  pNotifyChr = pService->getCharacteristic(BOOKOO_CHAR_UUID_DATA);
  pWriteChr = pService->getCharacteristic(BOOKOO_CHAR_UUID_CMD);
  if (!pNotifyChr || !pWriteChr) {
    pClient->disconnect();
    return false;
  }
  if (pNotifyChr->subscribe(true, scaleNotifyCallback, true)) {
    debugLog("[SCALE] Streaming channels locked successfully.");
    return true;
  }
  pClient->disconnect();
  return false;
}

// Dedicated background loop for network connections running on Core 0
void BleWatchdogTask(void * parameter) {
  unsigned long lastScaleCheck = 0;
  for(;;) {
    if (scaleConnected && (millis() - lastScaleHeartbeatTime > 2500)) {
      if (xSemaphoreTake(scaleDataMutex, portMAX_DELAY) == pdTRUE) {
        scaleConnected = false;
        scaleWeight = 0.00f;
        scaleFlowRate = 0.00f;
        bookooTimerSec = 0;
        xSemaphoreGive(scaleDataMutex);
      }
      debugLog("[SCALE] Watchdog timeout! Scale went offline safely.");
    }

    // Attempt reconnection inside Core 0 without blocking Core 1 operations
    if (!scaleConnected && (millis() - lastScaleCheck > 7000)) {
      lastScaleCheck = millis();
      connectToScale();
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Small sleep window to prevent Core starvation
  }
}

// ============================================================================
// SYSTEM UTILITY FUNCTIONS & ROUTE ENDPOINTS
// ============================================================================
void handleApplyTheme() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "No theme specified"); return; }
  String themeName = server.arg("name");
  if (!SD.exists("/" + themeName)) { server.send(404, "text/plain", "Not found"); return; }
  SD.remove("/global.css");
  File src = SD.open("/" + themeName, FILE_READ);
  File dst = SD.open("/global.css", FILE_WRITE);
  while (src.available()) { dst.write(src.read()); }
  src.close(); dst.close();
  File t = SD.open("/currentTheme.txt", FILE_WRITE);
  if (t) { t.print(themeName); t.close(); }
  server.send(200, "text/plain", "OK");
}

void handleFileRequest() {
  String path = server.uri();
  if (path.endsWith("/")) path += "index.html";
File f = SD.open(path);
if (!f || f.isDirectory()) {
	server.send(404, "text/plain", "404");
	return;
}
server.streamFile(f, getContentType(path));
f.close();
}

void handleListFiles() {
	File r = SD.open("/");
	String j = "[";
	File f = r.openNextFile();
	bool first = true;
	while (f) {
		if (!first) j += ",";
		j += R"({"name":")" + String(f.name()) + R"("})";
		first = false;
		f = r.openNextFile();
	}
	j += "]";
	server.send(200, "application/json", j);
}

void handleDelete() {
	String fn = server.arg("name");
	if (!fn.startsWith("/")) fn = "/" + fn;
	if (SD.exists(fn) && SD.remove(fn)) server.send(200, "text/plain", "Deleted");
	else server.send(404, "text/plain", "Error");
}

void handleUpload() {
	static File uploadFile;
	HTTPUpload& upload = server.upload();
	if (upload.status == UPLOAD_FILE_START) {
		uploadFile = SD.open("/" + upload.filename, FILE_WRITE);
	} else if (upload.status == UPLOAD_FILE_WRITE && uploadFile) {
		uploadFile.write(upload.buf, upload.currentSize);
	} else if (upload.status == UPLOAD_FILE_END && uploadFile) {
		uploadFile.close();
	}
}

void handleAdjust() {
	String var = server.arg("var");
	int val = server.arg("val").toInt();
	if (var == "preinftime") {
		preinftime = constrain(preinftime + val, 0, 20);
		pumppower = basePumpPowerForSetpoint(PrePressureSetpoint);
	}
	else if (var == "bloomtime") {
		bloomtime = constrain(bloomtime + val, 0, 20);
	}
	else if (var == "Pause") {
		light.setBrightness(0);
	}
	else if (var == "Resume") {
		light.setBrightness(pumppower);
	}
	else if (var == "setpoint") {
		setpoint = constrain(setpoint + val, 10 + offset, 96 + offset);
		setpointBoot = setpoint;
	}
	else if (var == "steam") {
		setpoint = steamSetpoint;
	}
	else if (var == "stopsteam") {
		setpoint = setpointBoot;
	}
	else if (var == "pressuresetpoint") {
		pressuresetpoint = constrain(pressuresetpoint + val, 3, 13);
		pumppower = basePumpPowerForSetpoint(pressuresetpoint);
	}
	else if (var == "targetweight") {
		targetWeight = constrain(targetWeight + (val * 1.0f), 1.0f, 250.0f);
	}
	else if (var == "manualtare") {
		sendScalePacket(PACKET_TARE, sizeof(PACKET_TARE));
	}
	server.send(200, "text/plain", "OK");
}

void handleTemp() {
	StaticJsonDocument<128> doc;
	doc["temp"] = input - offset;
	doc["setpoint"] = setpoint - offset;
	doc["pressure"] = currentPressure;
	String r;
	serializeJson(doc, r);
	server.send(200, "application/json", r);
}

void handleGetValues() {
	StaticJsonDocument<1024> doc;
	doc["setpoint"] = int(setpoint - offset);
	doc["preinftime"] = preinftime;
	doc["bloomtime"] = bloomtime;
	doc["pressure"] = currentPressure;
	doc["pumppower"] = pumppower;
	doc["pressuresetpoint"] = int(pressuresetpoint);
	doc["actime"] = actime;
	doc["temp"] = input - offset;
	doc["Kp"] = Kp;
	doc["Ki"] = Ki;
	doc["Kd"] = Kd;
	doc["brewTemp"] = brewTemp;
	doc["steamSetpoint"] = steamSetpoint;
	if (xSemaphoreTake(scaleDataMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
		doc["scaleConnected"] = scaleConnected ? 1 : 0;
		doc["scaleWeight"] = scaleWeight;
		doc["scaleFlow"] = scaleFlowRate;
		doc["scaleTimer"] = bookooTimerSec;
		doc["scaleBattery"] = scaleBattery;
		xSemaphoreGive(scaleDataMutex);
	}
	doc["targetWeight"] = targetWeight;
	String response;
	serializeJson(doc, response);
	server.send(200, "application/json", response);
}

void handlePressure() {
	server.send(200, "text/plain", String(currentPressure, 2));
}

void handleRouteUploadResponse() {
	server.sendHeader("Location", "/upload.html?success=1");
	server.send(303);
}

void handleRouteRootIndex() {
	File f = SD.open("/index.html");
	server.streamFile(f, "text/html");
	f.close();
}

void handleRouteGetConfig() {
	File f = SD.open("/config.json");
	server.streamFile(f, "application/json");
	f.close();
}

void handleRouteSaveConfig() {
	StaticJsonDocument<256> doc;
	deserializeJson(doc, server.arg("plain"));
	SD.remove("/config.json");
	// Old_Code File f = SD.open("/config.json", FILE_WRITE);
	// Old_CodeserializeJson(doc, f);
	File f = SD.open("/config.json", FILE_WRITE);
	serializeJsonPretty(doc, f); // Forces neat human-readable block indentations
	f.close();
	Kp = doc["Kp"];
	Ki = doc["Ki"];
	Kd = doc["Kd"];
	myPID.SetTunings(Kp, Ki, Kd);
	server.send(200, "text/plain", "Saved");
}

void beepBuzzer(int times, int beepDuration, int pauseDuration) {
	for (int i = 0; i < times; i++) {
		digitalWrite(BUZZER_PIN, HIGH);
		delay(beepDuration);
		digitalWrite(BUZZER_PIN, LOW);
		delay(pauseDuration);
	}
}

void getCurrentTheme() {
	if (!SD.exists("/currentTheme.txt")) {
		server.send(200, "text/plain", "None");
		return;
	}
	File f = SD.open("/currentTheme.txt");
	String t = f.readString();
	f.close();
	server.send(200, "text/plain", t);
}

void runPID() {
	unsigned long PIDnow = millis();
	if ((PIDnow - lastPIDTime >= PID_INTERVAL)) {
		lastPIDTime = PIDnow;
		float rawTemp = thermocouple.readCelsius();
		if (isnan(rawTemp) || rawTemp < 0 || rawTemp > 160) {
			digitalWrite(SSR_PIN, LOW);
			return;
		}
		total = total - readings[readIndex];
		readings[readIndex] = rawTemp;
		total = total + readings[readIndex];
		readIndex = (readIndex + 1) % numReadings;
		averageTemp = total / numReadings;
		input = averageTemp;
		myPID.Compute();
	}
	static unsigned long windowStartTime = 0;
	unsigned long currentMillis = millis();
	if (currentMillis - windowStartTime >= 500) {
		windowStartTime = currentMillis;
	}
	unsigned long ssrOnTime = ((unsigned long)output * 500) / 255;
	if ((currentMillis - windowStartTime) < ssrOnTime) {
		digitalWrite(SSR_PIN, HIGH);
	} else {
		digitalWrite(SSR_PIN, LOW);
	}
}

void GetPressure() {
	if (!acDetected || elapsedTime < 500) {
		currentPressure = 0;
		return;
	}
	unsigned long Pnow = millis();
	if (Pnow - LastPressCall >= PRESS_INTERVAL) {
		LastPressCall = Pnow;
		int raw = analogRead(pressurepin);
		currentPressure = (raw * (maxPressure / 4095.0));
		currentPressure = round(currentPressure * 100) / 100.0;
	}
}

String getContentType(String filename) {
	if (filename.endsWith(".html")) return "text/html";
	if (filename.endsWith(".css")) return "text/css";
	if (filename.endsWith(".js")) return "application/javascript";
	if (filename.endsWith(".png")) return "image/png";
	if (filename.endsWith(".json")) return "application/json";
	return "text/plain";
}

int basePumpPowerForSetpoint(double Pumpsetpoint) {
	if (Pumpsetpoint <= 3) return 140;
	if (Pumpsetpoint <= 4) return 143;
	if (Pumpsetpoint <= 5) return 147;
	if (Pumpsetpoint <= 6) return 149;
	if (Pumpsetpoint <= 7) return 153;
	if (Pumpsetpoint <= 8) return 155;
	if (Pumpsetpoint <= 9) return 160;
	if (Pumpsetpoint <= 10) return 170;
	return 190;
}

void SetPump() {
	if (millis() - DimlastUpdate > PRESS_INTERVAL) {
		DimlastUpdate = millis();
		static int callCount = 0;
		callCount++;
		if (callCount >= 4) {
			callCount = 0;
			if (currentPressure < PressureTarget - 0.2) {
				pumppower = constrain(pumppower + 1, 120, 255);
			}
			else if (currentPressure > PressureTarget + 0.2) {
				pumppower = constrain(pumppower - 2, 120, 255);
			}
		}
		if (currentPressure > PressureTarget + 0.3) {
			light.setBrightness(0);
		}
		else {
			light.setBrightness(pumppower);
		}
	}
}

void setupServerRoutes() {
  server.on("/upload", HTTP_POST, handleRouteUploadResponse, handleUpload);
  server.on("/", HTTP_GET, handleRouteRootIndex);
  server.on("/getConfig", HTTP_GET, handleRouteGetConfig);
  server.on("/saveConfig", HTTP_POST, handleRouteSaveConfig);
  server.on("/applyTheme", HTTP_GET, handleApplyTheme);
  server.on("/getCurrentTheme", HTTP_GET, getCurrentTheme);
  server.on("/adjust", handleAdjust);
  server.on("/temp", handleTemp);
  server.on("/getValues", handleGetValues);
  server.on("/pressure", handlePressure);
  server.on("/listFiles", HTTP_GET, handleListFiles);
  server.on("/delete", HTTP_GET, handleDelete);
  
  // FIXED: Wrapped using a raw string literal to prevent escape and parsing errors
  server.on("/debug", HTTP_GET, []() {
    String html = R"(
      <html>
      <body style='background:#111;color:#0f0;font-family:monospace;padding:20px;'>
        <h4>Live OTA Monitor Trunk</h4>
        <div id='log'></div>
        <script>
          var ws = new WebSocket('ws://' + location.hostname + ':81/');
          ws.onmessage = function(e) {
            var logDiv = document.getElementById('log');
            logDiv.innerHTML += e.data + '<br>';
            window.scrollTo(0, document.body.scrollHeight);
          };
        </script>
      </body>
      </html>
    )";
    server.send(200, "text/html", html);
  });

  server.onNotFound(handleFileRequest);
}

void startSD() {
	SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
	if (!SD.begin(SD_CS)) {
		Serial.println("SD initialization failed!");
		beepBuzzer(3,1000,200);
	} else {
		Serial.println("SD Card Initialized.");
		beepBuzzer(1,100,100);
	}
}

void loadSDConfig() {
	startSD();
	File configFile = SD.open("/config.json");
	if (configFile) {
		StaticJsonDocument<256> doc;
		deserializeJson(doc, configFile);
		configFile.close();
		ssid = doc["ssid"] | "";
		password = doc["password"] | "";
		Kp = doc["Kp"] | 48.0;
		Ki = doc["Ki"] | 0.2;
		Kd = doc["Dynamic"] | 85.0;
		double rawSetpoint = doc["setpoint"] | 93.0;
		offset = doc["offset"] | 9;
		double rawSteamSetpoint = doc["steamSetpoint"] | 140.0;
		PIDonly = doc["PIDonly"] | false;
		setpoint = rawSetpoint + offset;
		setpointBoot = setpoint;
		steamSetpoint = rawSteamSetpoint + offset;
	} else {
		Kp = 48.0;
		Ki = 0.2;
		Kd = 85.0;
		offset = 9;
		setpoint = 93.0 + offset;
		setpointBoot = setpoint;
		steamSetpoint = 140.0 + offset;
		PIDonly = false;
		Serial.println("[WARN] Config not found on SD card. Defaults loaded safely.");
	}
	myPID.SetTunings(Kp, Ki, Kd);
	SD.end();
	SPI.end();
	delay(200);
}

void startWiFi() {
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid.c_str(), password.c_str());
	unsigned long t0 = millis();
	Serial.println("Connecting to WiFi");
	while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
		delay(500);
		Serial.print(".");
	}
	if (WiFi.status() != WL_CONNECTED) {
		beepBuzzer(5,1000,200);
		Serial.println("WiFi FAILED");
	} else {
		beepBuzzer(1,500,100);
		Serial.println("WiFi Connected!");
		// ADD THIS LINE BELOW TO ENABLE REAL TIME FILE STAMPS: Added 8-26-2026
    // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer)
    // Example for US Pacific Time (-8 hours offset = -28800, 3600 for daylight savings)
    configTime(-28800, 3600, "pool.ntp.org");
	}
}

void steam() {
	if (input > steamSetpoint - 5 && !steaming) {
		beepBuzzer(3,100,100);
		steaming = true;
	}
	if (input < steamSetpoint - 20 && steaming) {
		steaming = false;
	}
}
// ============================================================================
// ARDUINO SETUP ENGINE INITIALIZATION
// ============================================================================
void setup() {
	Serial.begin(115200);
	delay(1000);
	pinMode(BUZZER_PIN, OUTPUT);
	pinMode(syncPin, INPUT);
	pinMode(SSR_PIN, OUTPUT);
	pinMode(SOLENOID_PIN, OUTPUT); // Fixed: Init physical pin for exhaust valve handling
	digitalWrite(SSR_PIN, LOW);
	digitalWrite(SOLENOID_PIN, LOW); // Retain pressure safety on power-up
	loadSDConfig();
	startWiFi();
	webSocket.begin(); // Fixed: Init the remote logging network broadcast server
	startSD();
	scaleDataMutex = xSemaphoreCreateMutex();
	NimBLEDevice::init("");
	pinMode(SSR_PIN, OUTPUT);
	ArduinoOTA.setHostname("Discreet");
	ArduinoOTA.begin();
	setupServerRoutes();
	server.begin();
	DimmableLight::setSyncPin(syncPin);
	DimmableLight::begin();
	myPID.SetSampleTime(250);
	myPID.SetOutputLimits(0, 255);
	myPID.SetMode(AUTOMATIC);
	if (MDNS.begin("discreet")) {
		Serial.println("mDNS Active -> http://discreet.local");
	}
	// Fixed: Allocating the blocking BLE processes exclusively to background Core 0
	  // Ensure this exact structure closes out the bottom of your setup() function
  xTaskCreatePinnedToCore(
    BleWatchdogTask,   /* Task function handles */
    "BleWatchdog",     /* Name identifier string */
    4096,              /* Stack depth allocations */
    NULL,              /* Input parameters */
    1,                 /* Execution scheduling priority */
    &BleTaskHandle,    /* Handle memory references */
    0                  /* Pin task safely to Core 0 */
  );                   // <--- Make sure this semicolon is here and there are no stray characters after it
}

// ============================================================================
// ARDUINO MAIN CONTROLLER EXECUTION LOOP (Runs exclusively on Core 1)
// ============================================================================
void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  webSocket.loop(); // Keep remote network logging trunk updated
  GetPressure();
  runPID();
  steam();

  // --- AUTOMATIC EXTRACTION STATE CONTROLLER ---
if (digitalRead(syncPin) == LOW && !acDetected && !PIDonly) {
    acDetectedTime = millis();
    acDetected = true;
    autoTareTriggered = false;
    weightLimitReached = false; // Reset threshold flag on a brand new cycle
    offcount = 0;
}

if (acDetected) {
    if (!highTuningsApplied) {
        myPID.SetTunings(25.0, 0.05, 50.0);
        highTuningsApplied = true;
    }
    if (!shotStarted) {
        shotStarted = true;
        weightLimitReached = false;
        preinftime = (bloomtime > 0 && preinftime < 5) ? 8 : preinftime;
        // Note: If you share a TRIAC line, this pin declaration handles your logic registers
        digitalWrite(SOLENOID_PIN, HIGH); 
    }

    if (scaleConnected && !autoTareTriggered && (millis() - acDetectedTime >= 500)) {
        sendScalePacket(PACKET_TARE, sizeof(PACKET_TARE));
        autoTareTriggered = true;
        debugLog("[DECOUPLED] Scale tare fired safely.");
    }

    // --- BREW-BY-WEIGHT ENFORCED CUTOFF ---
    if (weightLimitReached) {
        if (purgeStartTime == 0) {
            purgeStartTime = millis(); // Lock the exact start time of our water purge
        }

        // Run a high-intensity 350ms blast to clear standing surface water
        if (millis() - purgeStartTime < 350) {
            pumppower = 230;                 // High power to push standing water through paper
            light.setBrightness(pumppower);   // Keep pump energized
            digitalWrite(SOLENOID_PIN, HIGH); // KEEP SOLENOID ACTIVE (Boiler path OPEN)
        } 
        // Purge window complete: Shut down safely
        else {
            pumppower = 0;
            light.setBrightness(0);
            digitalWrite(SOLENOID_PIN, LOW); // Snap solenoid shut to exhaust remaining steam
        }
    } 
    // Only calculate flow profiles if the target weight hasn't been hit yet
    else {
        elapsedTime = millis() - acDetectedTime;
        actime = elapsedTime / 1000;

        // --- PHASE A: PRE-INFUSION ---
        if (preinftime > 0 && actime < preinftime) {
            if (currentPressure <= PrePressureSetpoint - 1) {
                pumppower = 255;
                light.setBrightness(pumppower);
                pumpPowerSetPreinf = false;
            } else if (!pumpPowerSetPreinf) {
                PressureTarget = PrePressureSetpoint;
                pumppower = basePumpPowerForSetpoint(PressureTarget);
                light.setBrightness(pumppower);
                pumpPowerSetPreinf = true;
                SetPump();
            } else {
                SetPump();
            }
        }
        // --- PHASE B: BLOOM ---
        else if (bloomtime > 0 && actime < preinftime + bloomtime) {
            pumppower = 0;
            light.setBrightness(pumppower);
        }
        // --- PHASE C: EXTRACTION ---
        else {
            if (currentPressure < pressuresetpoint - 2) {
                pumppower = 255;
                light.setBrightness(pumppower);
                pumpPowerSetExtraction = false;
            } else if (!pumpPowerSetExtraction) {
                PressureTarget = pressuresetpoint;
                pumppower = basePumpPowerForSetpoint(PressureTarget);
                light.setBrightness(pumppower);
                pumpPowerSetExtraction = true;
                SetPump();
            } else {
                SetPump();
            }
        }
    }

    // Handle mechanical switch debounce/turn-off detection
    if (digitalRead(syncPin) == HIGH) {
        if (millis() - acDetectedTime > 1500) { offcount++; }
    } else { 
        offcount = 0; 
    }

    // If manual brew switch is turned off OR target weight is maintained after cutoff
    if (offcount >= 100) {
        acDetected = false;
        shotStarted = false;
        weightLimitReached = false;
        pumpPowerSetPreinf = false;
        pumpPowerSetExtraction = false;
        autoTareTriggered = false;
        purgeStartTime = 0; // Reset our custom purge timer for the next shot
        offcount = 0;

        pumppower = 0;
        light.setBrightness(0);
        digitalWrite(SOLENOID_PIN, LOW); 
        debugLog("[SHOT FINISHED] State configurations reset clean.");
    }
	} // <--- Closes out the if (acDetected) block
} // <--- Closes out the main void loop() function
