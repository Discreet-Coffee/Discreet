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


// Wi-Fi Variables
String ssid;
String password;

WebServer server(80);

// SD card pins
#define SD_CS     32
#define SD_MOSI  25
#define SD_MISO  26
#define SD_SCK   33

//SSR Pins
#define SSR_PIN 13  

// Dimmer Pins
#define syncPin        19   //19  // Zero-cross sync input       new 19
#define thyristorPin   18   //18  // Thyristor (dimmer PWM output)

// Thermocouple SPI (HSPI)
#define thermoCS  22 //22 // old 18
#define thermoCLK 23 //23  // old 19
#define thermoDO  21 // 21 // old 21

//Pressure sensor pin
#define pressurepin 34

// Buzzer pin definition
#define BUZZER_PIN 4  // Change this to the actual GPIO you're using

// Initialize Thermo COMMENTED OUT UNTIL DEFINED
MAX6675 thermocouple(thermoCLK, thermoCS, thermoDO);

// PID Setup
double setpoint, input, output;
double Kp = 48.0, Ki = 8, Kd = 50.0;


PID myPID(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);

//Pressure Variables
int pressuresetpoint = 9;
int PrePressureSetpoint = 3;
int PressureTarget = pressuresetpoint;
int pumppower = 0; 
int maxPressure = 12;
double currentPressure = 0;

// Timeing Intervals
const int PRESS_INTERVAL = 50;  // X ms
const int PID_INTERVAL = 250;  // X ms

// Timer Variables
unsigned long lastPIDTime = 0;
unsigned long DimlastUpdate = 0;
unsigned long LastPressCall = 0;
unsigned long lastPrintTime = 0;
unsigned long acDetectedTime = 0;
unsigned long elapsedTime = 0; // Shot time in milliseconds
int actime = 0;  // Shot time in seconds

//Other Variables
int offset = 9; // Due to probe location. If you ask for 100 you will get 91, tune this variable.
int preinftime = 8;

bool acDetected = false;
bool shotStarted = false;
bool pumpPowerSetPreinf = false;
bool pumpPowerSetExtraction = false;

int bloomtime = 0;
int offcount = 0;

DimmableLight light(thyristorPin);

//Functions
void handleApplyTheme() {

    if (!server.hasArg("name")) {
        server.send(400, "text/plain", "No theme specified");
        return;
    }

    String themeName = server.arg("name");

    // Safety: block invalid names
    if (!themeName.endsWith(".css")) {
        server.send(400, "text/plain", "Invalid theme file");
        return;
    }

    String sourcePath = "/" + themeName;
    String targetPath = "/global.css";

    if (!SD.exists(sourcePath)) {
        server.send(404, "text/plain", "Theme not found");
        return;
    }

    // Open source file
    File sourceFile = SD.open(sourcePath, FILE_READ);

    if (!sourceFile) {
        server.send(500, "text/plain", "Failed to open theme file");
        return;
    }

    // Remove old global.css
    if (SD.exists(targetPath)) {
        SD.remove(targetPath);
    }

    // Create new global.css
    File targetFile = SD.open(targetPath, FILE_WRITE);

    if (!targetFile) {
        sourceFile.close();
        server.send(500, "text/plain", "Failed to create global.css");
        return;
    }

    // Copy file
    while (sourceFile.available()) {
        targetFile.write(sourceFile.read());
    }

    sourceFile.close();
    targetFile.close();

    server.send(200, "text/plain", "OK");
    
    File themeFile = SD.open("/currentTheme.txt", FILE_WRITE);

    if (themeFile) {
      themeFile.print(themeName);
      themeFile.close();
    }

}

void handleFileRequest() {
  String path = server.uri();
  if (path.endsWith("/")) path += "index.html";

  String contentType = getContentType(path);

  File file = SD.open(path);
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "File Not Found");
    return;
  }

  server.streamFile(file, contentType);
  file.close();
}

void handleListFiles() {
  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    server.send(500, "application/json", "[]");
    return;
  }

  String json = "[";
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    if (!first) json += ",";
    json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
    first = false;
    file = root.openNextFile();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDelete() {
  if (!server.hasArg("name")) {
    server.send(400, "text/plain", "Missing 'name' argument");
    return;
  }

  String filename = server.arg("name");
  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  if (SD.exists(filename)) {
    if (SD.remove(filename)) {
      server.send(200, "text/plain", "File deleted successfully");
    } else {
      server.send(500, "text/plain", "Failed to delete file");
    }
  } else {
    server.send(404, "text/plain", "File Not Found");
  }
}

void handleUpload() {
  static File uploadFile;
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    uploadFile = SD.open(filename, FILE_WRITE);
    if (!uploadFile) {
      Serial.println("Failed to open file for writing");
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      Serial.println("Upload finished");
    }
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
  }
  else if (var == "steam") {
    setpoint = 150;
  }
    else if (var == "stopsteam") {
    setpoint = 102;
  }
  else if (var == "pressuresetpoint") {
    pressuresetpoint = constrain(pressuresetpoint + val, 3, 11);
    pumppower = basePumpPowerForSetpoint(pressuresetpoint);
  }
  server.send(200, "text/plain", "OK");
}

void handleTemp() {
  // Create JSON document
  StaticJsonDocument<128> doc;
  doc["temp"] = input - offset;
  doc["setpoint"] = setpoint - offset;
  doc["pressure"] = currentPressure;

  // Use a buffer to generate the JSON, then send it
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleGetValues() {
  StaticJsonDocument<256> doc;
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

  // Use a buffer to generate the JSON, then send it
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handlePressure() {
  server.send(200, "text/plain", String(currentPressure, 2));
}

void beepBuzzer(int times, int beepDuration, int pauseDuration) {

  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);  // Turn buzzer on
    delay(beepDuration);
    digitalWrite(BUZZER_PIN, LOW);   // Turn buzzer off
    delay(pauseDuration);
  }
}

void getCurrentTheme() {

    if (!SD.exists("/currentTheme.txt")) {
        server.send(200, "text/plain", "None");
        return;
    }

    File file = SD.open("/currentTheme.txt");

    if (!file) {
        server.send(500, "text/plain", "Error");
        return;
    }

    String theme = file.readString();

    file.close();

    server.send(200, "text/plain", theme);
}

void runPID() {

  unsigned long PIDnow = millis();

  if ((PIDnow - lastPIDTime >= PID_INTERVAL)) { 
    lastPIDTime = PIDnow;
    
    input = thermocouple.readCelsius();
  
    if (isnan(input) || input < 0 || input > 160) {
      digitalWrite(SSR_PIN, LOW); // SSR Off
      return; // Exit function, SSR stays off
    }

    myPID.Compute();

    digitalWrite(SSR_PIN, output >= 127 ? HIGH : LOW);

  }
}

void GetPressure(){

  if (!acDetected) { //Dont read pressure when not pulling a shot. Pressure sensor is before the solenoid so will show pressure build in boiler.
    currentPressure = 0;
    return;
  }

  if (elapsedTime < 500) { //Dont read pressure for the first .5 seconds to let pressure build up escape
    currentPressure = 0;
    return;
  }

  unsigned long Pnow = millis();

  if (Pnow - LastPressCall >= PRESS_INTERVAL) {
    LastPressCall = Pnow; 
    int raw = analogRead(pressurepin);
    currentPressure = (raw * (maxPressure / 4095.0));
    currentPressure = round(currentPressure * 100) / 100.0;  // Limit to 2 decimals
  }

}

String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css"))  return "text/css";
  if (filename.endsWith(".js"))   return "application/javascript";
  if (filename.endsWith(".png"))  return "image/png";
  if (filename.endsWith(".jpg"))  return "image/jpeg";
  if (filename.endsWith(".ico"))  return "image/x-icon";
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

  // Only update after boost every xxx ms
  if (millis() - DimlastUpdate > PRESS_INTERVAL) {
    DimlastUpdate = millis();

    static int callCount = 0;
    callCount++;
    // Slow - adjust pump power every 50ms (per count)
    if (callCount >= 4) {
      callCount = 0;
      if (currentPressure < PressureTarget - 0.2) { pumppower = constrain(pumppower + 1, 120, 255); }
      else if (currentPressure > PressureTarget + 0.2) { pumppower = constrain(pumppower - 2, 120, 255); }
    }

    // Fast - cut pump if overpressure every 50ms
    if (currentPressure > PressureTarget + 0.3) {light.setBrightness(0);}
    else {light.setBrightness(pumppower);}

  }

}

void setupServerRoutes() {

  server.on("/upload", HTTP_POST, []() {
    // This runs *after* handleUpload finishes
    server.sendHeader("Location", "/upload.html?success=1");
    server.send(303);
  }, handleUpload);

  server.on("/", HTTP_GET, []() {
    File file = SD.open("/index.html");
    if (!file) {
      server.send(500, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });

  server.on("/getConfig", HTTP_GET, []() {

    File file = SD.open("/config.json", FILE_READ);

    if (!file) {
      Serial.println("Failed to open config.json");
      server.send(500, "text/plain", "Config not found");
      return;
    }

    server.streamFile(file, "application/json");

    file.close();

  });

  server.on("/saveConfig", HTTP_POST, []() {
  
    StaticJsonDocument<256> doc;
  
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }
  
    // Remove old config
    SD.remove("/config.json");
  
    File file = SD.open("/config.json", FILE_WRITE);
  
    if (!file) {
      server.send(500, "text/plain", "Write failed");
      return;
    }
  
    serializeJson(doc, file);
  
    file.close();
  
    Serial.println("Config saved");
  
    server.send(200, "text/plain", "Saved");
  
  });

  server.on("/applyTheme", HTTP_GET, handleApplyTheme);
  server.on("/getCurrentTheme", HTTP_GET, getCurrentTheme);
  server.on("/adjust", handleAdjust);
  server.on("/temp", handleTemp);
  server.on("/getValues", handleGetValues);
  server.on("/pressure", handlePressure);
  server.on("/listFiles", HTTP_GET, handleListFiles);
  server.on("/delete", HTTP_GET, handleDelete);
  server.onNotFound(handleFileRequest);

}

void startSD(){
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
    beepBuzzer(3,1000,200);
  } else {
    Serial.println("SD card initialized.");
    beepBuzzer(1,100,100);
  }
}

void loadSDConfig() {
  startSD();

  // Read config
  File configFile = SD.open("/config.json");
  if (configFile) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, configFile);
    configFile.close();
    
    ssid = doc["ssid"] | "";
    password = doc["password"] | "";
    Kp = doc["Kp"] | Kp;
    Ki = doc["Ki"] | Ki;
    Kd = doc["Kd"] | Kd;
    Serial.println("SSID: " + ssid);
    Serial.println(Kp);
    Serial.println(Ki);
    Serial.println(Kd);
  
  }
  
  //end SD and SPI
  SD.end();
  SPI.end();  // fully kill SPI
  delay(200);
}

void startWiFi(){

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
    Serial.println("FAILED connecting to WiFi");
  } else {
    beepBuzzer(1,500,100);
    Serial.println("WiFi Connected!");
    WiFi.config(WiFi.localIP(), IPAddress(0,0,0,0), WiFi.subnetMask(), IPAddress(0,0,0,0));
    Serial.println("IP:  " + WiFi.localIP().toString());
    Serial.println("GW:  " + WiFi.gatewayIP().toString());
    Serial.println("DNS: " + WiFi.dnsIP().toString());  
  }

}

void setup() {

  Serial.begin(115200);
  delay(2000);

  pinMode(BUZZER_PIN, OUTPUT);
  
  //load config.json
  loadSDConfig();
  //start WiFi
  startWiFi();

  // Start SD card again after Wifi (having SD running and starting wifi breaks the SD card)
  startSD();
 
  // OTA Setup
  ArduinoOTA.setHostname("Discreet"); // Set a unique hostname
  ArduinoOTA.setPassword("Discreet"); // Optional: Set a password for security
  ArduinoOTA.begin(); // Start OTA service

  // === Web Routes ===
  setupServerRoutes();

  // === Start Server ===
  server.begin();
  Serial.println("HTTP server started.");

    //Dimmer Setup
  pinMode(SSR_PIN, OUTPUT);
  pinMode(syncPin, INPUT);
  DimmableLight::setSyncPin(syncPin);
  DimmableLight::begin();

  // PID setup
  setpoint = 102; //Set Temp + offset
  myPID.SetSampleTime(250); 
  myPID.SetOutputLimits(0, 255);
  myPID.SetMode(AUTOMATIC);
  
  if (MDNS.begin("discreet")) {
    Serial.println("mDNS started - access at http://discreet.local");
  }
  
  delay(2000); //delay before anything starts happening.

}

void loop() {

  ArduinoOTA.handle(); // Handle OTA updates
  server.handleClient();
  GetPressure();
  runPID();

  // AC detection
  if (digitalRead(syncPin) == LOW && !acDetected) {
    acDetectedTime = millis();
    acDetected = true;
  }

  if (acDetected) {

    // Run once at shot start
    if (!shotStarted) {
      shotStarted = true;
      preinftime = (bloomtime > 0 && preinftime < 5) ? 8 : preinftime;
    }

    // Calculate shot time
    elapsedTime = millis() - acDetectedTime;
    actime = elapsedTime / 1000;

    // --- PRE-INFUSION ---
    if (preinftime > 0 && actime < preinftime) {
      if (currentPressure < PrePressureSetpoint - 2) {
        pumppower = 255;
        light.setBrightness(pumppower);      
      }
      else if (!pumpPowerSetPreinf) {
         PressureTarget = PrePressureSetpoint;
         pumppower = basePumpPowerForSetpoint(PressureTarget);
         light.setBrightness(pumppower);
         pumpPowerSetPreinf = true;  // prevents running again
         SetPump();
      }
      else SetPump();
    }

    // --- BLOOM ---
    else if (bloomtime > 0 && actime < preinftime + bloomtime) {
      pumppower = 0;
      light.setBrightness(pumppower);
    }

    // --- EXTRACTION ---
    else {
      if (currentPressure < pressuresetpoint - 2) {
        pumppower = 255;
        light.setBrightness(pumppower);
      }
      else if (!pumpPowerSetExtraction) {
         PressureTarget = pressuresetpoint;
         pumppower = basePumpPowerForSetpoint(PressureTarget); // runs once
         light.setBrightness(pumppower);
         pumpPowerSetExtraction = true;  // prevents it running again
         SetPump();
      }
      else SetPump();
    }

    // AC Turned off
    if (digitalRead(syncPin) == HIGH) offcount++;
    else offcount = 0;

    if (offcount >= 100) {
      acDetected = false;
      shotStarted = false;
      pumpPowerSetPreinf = false;
      pumpPowerSetExtraction = false;
      offcount = 0;
    }
  }
}
