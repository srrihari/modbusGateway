#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <time.h>
#include "nvs_flash.h"

#define RX 26
#define TX 27
#define DE 25

String ap_ssid = "Sustain-A-Byte";
String ap_pass = "12345678";

ModbusMaster node;
uint8_t currentSlave = 0xFF;

WebServer server(80);
bool loggedIn = false;

struct RS485Config
{
  int baud;
  int databits;
  char parity;
  int stopbits;
};

RS485Config rs485;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// MQTT config variables
String mqtt_host;
int mqtt_port;
String mqtt_clientId;
String mqtt_user;
String mqtt_pass;
String mqtt_topic;

String mqttLogBuffer = "";

#define MAX_MB_ROWS 10

struct RegMap
{
  String name;
  uint8_t index;
  float scale;
};

struct ModbusRow
{
  uint8_t slave;
  uint8_t func;
  uint16_t start;
  uint16_t count;
  uint32_t poll;
  RegMap regs[8];
  uint8_t regCount;
};

ModbusRow mbRows[MAX_MB_ROWS];
int mbRowCount = 0;

unsigned long lastPoll[MAX_MB_ROWS];

uint32_t getSerialCfg(int d, char p, int s)
{
  if (d == 8 && p == 'N' && s == 1)
    return SERIAL_8N1;
  if (d == 8 && p == 'N' && s == 2)
    return SERIAL_8N2;
  if (d == 8 && p == 'E' && s == 1)
    return SERIAL_8E1;
  if (d == 8 && p == 'O' && s == 1)
    return SERIAL_8O1;
  if (d == 7 && p == 'E' && s == 1)
    return SERIAL_7E1;
  if (d == 7 && p == 'O' && s == 1)
    return SERIAL_7O1;
  return SERIAL_8N1;
}

void initFS()
{
  LittleFS.begin(true);
}

void applyRS485()
{
  Serial2.end();
  delay(50);

  uint32_t cfg = getSerialCfg(
      rs485.databits,
      rs485.parity,
      rs485.stopbits);

  Serial2.begin(rs485.baud, cfg, RX, TX);
}

void handleGetModbus()
{
  if (!LittleFS.exists("/modbus.json"))
  {
    server.send(200, "application/json", "[]");
    return;
  }

  File f = LittleFS.open("/modbus.json", "r");
  server.streamFile(f, "application/json");
  f.close();
}

void loadModbusConfig()
{
  File f = LittleFS.open("/modbus.json", "r");
  if (!f)
    return;

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, f))
  {
    f.close();
    return;
  }
  f.close();

  mbRowCount = 0;

  for (JsonObject row : doc.as<JsonArray>())
  {
    if (mbRowCount >= MAX_MB_ROWS)
      break;

    ModbusRow &mb = mbRows[mbRowCount];

    mb.slave = row["slave"];
    mb.func = row["func"];
    mb.start = row["start"];
    mb.count = row["count"];
    mb.poll = row["poll"];

    mb.regCount = 0;

    JsonArray regs = row["registers"];
    for (JsonObject r : regs)
    {
      if (mb.regCount >= 8)
        break; // ✅ REQUIRED

      mb.regs[mb.regCount].name = r["name"].as<String>();
      mb.regs[mb.regCount].index = r["index"];
      mb.regs[mb.regCount].scale = r["scale"];
      mb.regCount++;
    }

    lastPoll[mbRowCount] = 0;
    mbRowCount++;
  }

  Serial.print("Loaded Modbus rows: ");
  Serial.println(mbRowCount);
}

void processModbus()
{
  if (mbRowCount == 0)
    return;

  for (int i = 0; i < mbRowCount; i++)
  {

    if (millis() - lastPoll[i] < mbRows[i].poll)
      continue;
    lastPoll[i] = millis();

    if (currentSlave != mbRows[i].slave)
    {
      node.begin(mbRows[i].slave, Serial2);
      currentSlave = mbRows[i].slave;
    }

    uint8_t result;

    switch (mbRows[i].func)
    {
    case 1:
      result = node.readCoils(
          mbRows[i].start,
          mbRows[i].count);
      break;

    case 2:
      result = node.readDiscreteInputs(
          mbRows[i].start,
          mbRows[i].count);
      break;

    case 3:
      result = node.readHoldingRegisters(
          mbRows[i].start,
          mbRows[i].count);
      break;

    case 4:
      result = node.readInputRegisters(
          mbRows[i].start,
          mbRows[i].count);
      break;

    default:
      Serial.println("Invalid function code");
      return;
    }

    if (result == node.ku8MBSuccess)
    {
      publishModbusJSON(i);
    }
    else
    {
      Serial.print("Modbus error row ");
      Serial.print(i);
      Serial.print(" : ");
      Serial.println(result);
    }
  }
}

void publishModbusJSON(int idx)
{
  StaticJsonDocument<512> doc;

  doc["slave"] = mbRows[idx].slave;
  doc["func"] = mbRows[idx].func;
  doc["start"] = mbRows[idx].start;

  JsonArray arr = doc.createNestedArray("data");
  JsonObject obj = arr.createNestedObject();

  for (int i = 0; i < mbRows[idx].regCount; i++)
  {
    uint16_t raw = node.getResponseBuffer(
        mbRows[idx].regs[i].index);

    obj[mbRows[idx].regs[i].name] =
        raw * mbRows[idx].regs[i].scale;
  }

  char payload[512];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  if (!mqttClient.connected())
  {
    String log =
        "[" + nowTime() + "] MQTT NOT CONNECTED\n";
    Serial.print(log);
    mqttLogBuffer += log;
    return;
  }

  bool ok = mqttClient.publish(
      mqtt_topic.c_str(),
      payload,
      len);

  String log =
      "[" + nowTime() + "] MQTT TX → " + payload + "\n";

  Serial.print(log);
  mqttLogBuffer += log;

  Serial.print("Publish status: ");
  Serial.println(ok ? "OK" : "FAILED");

  if (mqttLogBuffer.length() > 2000)
    mqttLogBuffer.remove(0, 1000);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String msg = "[" + nowTime() + "] MQTT RX → ";
  msg += topic;
  msg += " : ";

  for (unsigned int i = 0; i < length; i++)
  {
    msg += (char)payload[i];
  }
  msg += "\n";

  Serial.print(msg);    // Serial output
  mqttLogBuffer += msg; // UI buffer

  // Prevent overflow
  if (mqttLogBuffer.length() > 2000)
  {
    mqttLogBuffer.remove(0, 1000);
  }
}

void handleSaveModbus()
{
  File f = LittleFS.open("/modbus.json", "w");
  f.print(server.arg("plain"));
  f.close();

  loadModbusConfig(); // reload immediately

  server.send(200, "text/plain", "Saved");
}

bool loadConfig()
{
  File f = LittleFS.open("/config.json", "r");
  if (!f)
    return false;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, f))
  {
    f.close();
    return false;
  }
  f.close();

  mqtt_host = doc["mqtt"]["host"].as<String>();
  mqtt_port = doc["mqtt"]["port"];
  mqtt_clientId = doc["mqtt"]["clientId"].as<String>();
  mqtt_user = doc["mqtt"]["user"].as<String>();
  mqtt_pass = doc["mqtt"]["pass"].as<String>();
  mqtt_topic = doc["mqtt"]["topic"].as<String>();
  ap_ssid = doc["ap"]["ssid"].as<String>();
  ap_pass = doc["ap"]["pass"].as<String>();

  return true;
}

void handleSaveDevice()
{
  StaticJsonDocument<256> in;
  deserializeJson(in, server.arg("plain"));

  File f = LittleFS.open("/config.json", "r");
  StaticJsonDocument<512> cfg;
  deserializeJson(cfg, f);
  f.close();

  cfg["ap"]["ssid"] = in["ap"]["ssid"];
  cfg["ap"]["pass"] = in["ap"]["pass"];

  f = LittleFS.open("/config.json", "w");
  serializeJson(cfg, f);
  f.close();

  server.send(200, "text/plain", "Saved");
  delay(1000);
  ESP.restart();
}

void connectWiFi()
{
  static unsigned long lastAttempt = 0;

  if (WiFi.status() == WL_CONNECTED)
    return;

  if (millis() - lastAttempt < 5000)
    return;

  lastAttempt = millis();

  Serial.println("Trying STA connect...");
  WiFi.begin("TV_Net", "Thana@4384"); //    
}

void handleGetLogs()
{
  server.send(200, "text/plain", mqttLogBuffer);
}

void connectMQTT()
{
  if (mqttClient.connected())
    return;

  mqttClient.setServer(mqtt_host.c_str(), mqtt_port);
  mqttClient.setCallback(mqttCallback);

  Serial.print("Connecting MQTT to ");
  Serial.print(mqtt_host);
  Serial.print(":");
  Serial.println(mqtt_port);

  if (mqttClient.connect(
          mqtt_clientId.c_str(),
          mqtt_user.length() ? mqtt_user.c_str() : NULL,
          mqtt_pass.length() ? mqtt_pass.c_str() : NULL))
  {
    Serial.println("MQTT CONNECTED");
    mqttClient.subscribe(mqtt_topic.c_str());
  }
  else
  {
    Serial.print("MQTT FAILED, rc=");
    Serial.println(mqttClient.state());
  }
}

void handleSaveConfig()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "text/plain", "No data");
    return;
  }

  File f = LittleFS.open("/config.json", "w");
  if (!f)
  {
    server.send(500, "text/plain", "Config open failed");
    return;
  }

  f.print(server.arg("plain"));
  f.close();

  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(1000);
  ESP.restart();
}

/* ---------- HELPER ---------- */
void serveFile(const char *path, const char *type)
{
  if (!LittleFS.exists(path))
  {
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  File file = LittleFS.open(path, "r");
  server.streamFile(file, type);
  file.close();
}

/* ---------- ROUTES ---------- */
void handleGetConfig()
{
  File f = LittleFS.open("/config.json", "r");
  if (!f)
  {
    server.send(404, "text/plain", "Config not found");
    return;
  }
  server.streamFile(f, "application/json");
  f.close();
}

// First page
void handleRoot()
{
  serveFile("/login.html", "text/html");
}

// Login POST
void handleLogin()
{
  String user = server.arg("username");
  String pass = server.arg("password");

  if (user == "admin" && pass == "admin123")
  {
    loggedIn = true;
    server.sendHeader("Location", "/index.html");
    server.send(302);
  }
  else
  {
    server.send(401, "text/html",
                "<h3>Invalid Login</h3><a href='/login.html'>Try again</a>");
  }
}

// test msg
void handleTestMqtt()
{
  if (!server.hasArg("plain"))
  {
    server.send(400, "text/plain", "No message");
    return;
  }

  if (!mqttClient.connected())
  {
    server.send(500, "text/plain", "MQTT not connected");
    return;
  }

  String msg = server.arg("plain");

  bool ok = mqttClient.publish(mqtt_topic.c_str(), msg.c_str());

  if (ok)
  {
    server.send(200, "text/plain", "MQTT test sent");
  }
  else
  {
    server.send(500, "text/plain", "MQTT publish failed");
  }
}

// Dashboard
void handleDashboard()
{
  if (!loggedIn)
  {
    server.sendHeader("Location", "/login.html");
    server.send(302);
    return;
  }
  serveFile("/index.html", "text/html");
}

// Logout
void handleLogout()
{
  loggedIn = false;
  server.sendHeader("Location", "/login.html");
  server.send(302);
}

void preTx()
{
  digitalWrite(DE, HIGH);
  delayMicroseconds(1500);
}

void postTx()
{
  delayMicroseconds(1500);
  digitalWrite(DE, LOW);
}

String nowTime()
{
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);

  char buf[16];
  sprintf(buf, "%02d:%02d:%02d",
          t->tm_hour,
          t->tm_min,
          t->tm_sec);

  return String(buf);
}

void handleClearLogs()
{
  mqttLogBuffer = "";
  server.send(200, "text/plain", "Logs cleared");
}

void setupTime()
{
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");

  Serial.print("Syncing time");
  time_t now;
  while ((now = time(nullptr)) < 100000)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println(" done");
}

void handleFileUpload(const char *path)
{
  HTTPUpload &upload = server.upload();
  static File file;

  if (upload.status == UPLOAD_FILE_START)
  {
    file = LittleFS.open(path, "w");
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (file)
      file.write(upload.buf, upload.currentSize);
  }

  else if (upload.status == UPLOAD_FILE_END)
  {
    if (file)
      file.close();

    if (strcmp(path, "/config.json") == 0)
    {
      loadConfig();
      Serial.println("Config reloaded");

      WiFi.softAPdisconnect(true);
      delay(200);
      WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());

      Serial.print("AP restarted with SSID: ");
      Serial.println(ap_ssid);
    }

    if (strcmp(path, "/modbus.json") == 0)
    {
      mbRowCount = 0;
      for (int i = 0; i < MAX_MB_ROWS; i++)
        lastPoll[i] = 0;
      loadModbusConfig();
      Serial.println("Modbus reloaded");
    }
  }
}

/* ---------- SETUP ---------- */
void setup()
{
  Serial.begin(115200);

  pinMode(DE, OUTPUT);
  digitalWrite(DE, LOW);

  node.preTransmission(preTx);
  node.postTransmission(postTx);

  rs485.baud = 9600;
  rs485.databits = 8;
  rs485.parity = 'N';
  rs485.stopbits = 1;

  applyRS485();

  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS mount failed");
    return;
  }
  Serial.println("LittleFS mounted");

  // List files
  File root = LittleFS.open("/");
  File f = root.openNextFile();
  while (f)
  {
    Serial.print("FILE: ");
    Serial.println(f.name());
    f = root.openNextFile();
  }

  loadConfig();
  loadModbusConfig();

  // Start WiFi ONCE
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());

  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Try STA (non-blocking)
  connectWiFi();

  // Web routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login.html", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/index.html", HTTP_GET, handleDashboard);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/saveConfig", HTTP_POST, handleSaveConfig);
  server.on("/getConfig", HTTP_GET, handleGetConfig);
  server.on("/testMqtt", HTTP_POST, handleTestMqtt);
  server.on("/logs", HTTP_GET, handleGetLogs);
  server.on("/saveModbus", HTTP_POST, handleSaveModbus);
  server.on("/getModbus", HTTP_GET, handleGetModbus);
  server.on("/saveDevice", HTTP_POST, handleSaveDevice);
  server.on("/clearLogs", HTTP_POST, handleClearLogs);
  server.on("/api/system/reset", HTTP_POST, []()
            {
  server.send(200, "text/plain", "Resetting");
  delay(200);
  ESP.restart(); });

  server.on("/api/system/factory-reset", HTTP_POST, []()
            {
  nvs_flash_erase();
  server.send(200, "text/plain", "Factory reset");
  delay(200);
  ESP.restart(); });

  server.on("/api/system/rtc", HTTP_POST, []()
            {
  setupTime();   // your existing function
  server.send(200, "text/plain", "RTC synced"); });
  // Download config.json
  server.on("/api/config/download", HTTP_GET, []()
            {
  if (!LittleFS.exists("/config.json")) {
    server.send(404, "text/plain", "config.json not found");
    return;
  }

  File f = LittleFS.open("/config.json", "r");
  server.streamFile(f, "application/json");
  f.close(); });

  // Download modbus.json
  server.on("/api/modbus/download", HTTP_GET, []()
            {
  if (!LittleFS.exists("/modbus.json")) {
    server.send(404, "text/plain", "modbus.json not found");
    return;
  }

  File f = LittleFS.open("/modbus.json", "r");
  server.streamFile(f, "application/json");
  f.close(); });

  server.on(
      "/api/config/upload",
      HTTP_POST,
      []()
      {
        server.send(200, "text/plain", "Config uploaded");
      },
      []()
      {
        handleFileUpload("/config.json");
      });

  server.on(
      "/api/modbus/upload",
      HTTP_POST,
      []()
      {
        server.send(200, "text/plain", "Modbus uploaded & applied");
      },
      []()
      {
        handleFileUpload("/modbus.json");
      });

  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/app.js", LittleFS, "/app.js");
  server.serveStatic("/logo.png", LittleFS, "/logo.png");

  server.begin();
  Serial.println("Web server started");
}

/* ---------- LOOP ---------- */
void loop()
{
  server.handleClient();

  connectWiFi();

  if (WiFi.status() != WL_CONNECTED)
    return;

  connectMQTT();

  if (!mqttClient.connected())
    return;

  static bool timeReady = false;
  if (!timeReady)
  {
    setupTime();
    timeReady = true;
  }

  mqttClient.loop();
  processModbus();
}
