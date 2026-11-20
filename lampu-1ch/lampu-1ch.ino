/*
  ================================================================
  NOVIA HOME V1.2.1
  ESP32 DevKit V1 - Android ArduinoDroid friendly single-file sketch
  ================================================================

  HARDWARE:
    Relay 1 -> GPIO 23
    Relay 2 -> GPIO 22
    Relay 3 -> GPIO 21
    Relay 4 -> GPIO 19
    Relay logic: ACTIVE LOW (LOW = ON, HIGH = OFF)

  FEATURES:
    - Blynk IoT control (V0-V3)
    - Wi-Fi credentials stored in ESP32 NVS
    - Automatic AP fallback / Wi-Fi provisioning
    - Local web dashboard and JSON status API
    - Browser-based firmware update (.bin)
    - ArduinoOTA update over local Wi-Fi
    - Relay state starts OFF after reboot
    - Unique ESP32 chip identity
    - Editable device display name stored in NVS
    - Configuration backup download and restore

  BEFORE COMPILING:
    1. Fill in BLYNK_TEMPLATE_ID, BLYNK_TEMPLATE_NAME, BLYNK_AUTH_TOKEN.
    2. Optionally fill DEFAULT_WIFI_SSID / DEFAULT_WIFI_PASS.
    3. Change AP_PASSWORD, WEB_PASSWORD and OTA_PASSWORD.
    4. Install the "Blynk" library in ArduinoDroid.
    5. Select an ESP32 board and a partition scheme with OTA support.
    6. Backup files contain Wi-Fi credentials; keep them private.

  SECURITY:
    The local web dashboard uses HTTP Basic Authentication (not HTTPS).
    Use only on a trusted local network. Never port-forward it to the internet.
*/

#define BLYNK_TEMPLATE_ID   "TMPL6Yk8Hycr-"
#define BLYNK_TEMPLATE_NAME "Novia Home"
#define BLYNK_AUTH_TOKEN    "uUhUdnlB22cqJfKinHQu8yGMXLRbgzf1"

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp32.h>

// ------------------------- USER CONFIG ---------------------------

static const char DEVICE_NAME[] = "NOVIA_HOME";
static const char FW_VERSION[]  = "1.2.1";

static const uint8_t RELAY_PINS[4] = {23, 22, 21, 19};
static const uint8_t RELAY_COUNT = 4;
static const bool RELAY_ACTIVE_HIGH = false;

static const char DEFAULT_WIFI_SSID[] = "";
static const char DEFAULT_WIFI_PASS[] = "";

static const char AP_SSID[] = "NOVIA-HOME-SETUP";
static const char AP_PASSWORD[] = "Noke0710";

static const char WEB_USER[] = "Novia";
static const char WEB_PASSWORD[] = "Nurmala";

static const char OTA_PASSWORD[] = "Noke0710";

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 25000;
static const uint32_t WIFI_RETRY_INTERVAL_MS  = 15000;
static const uint32_t AP_FALLBACK_TIMEOUT_MS  = 25000;
static const uint32_t BLYNK_RETRY_INTERVAL_MS = 5000;

// Blynk datastreams:
// V0 = Relay 1, V1 = Relay 2, V2 = Relay 3, V3 = Relay 4
// V5 = uptime seconds, V6 = Wi-Fi RSSI

// ------------------------- GLOBALS --------------------------------

Preferences preferences;
WebServer server(80);

bool relayState[RELAY_COUNT] = {false, false, false, false};

String savedSSID;
String savedPassword;
String deviceName;
String deviceId;
String networkHostname;

bool apStarted = false;
bool webServerStarted = false;
bool otaStarted = false;
bool blynkConfigured = false;

unsigned long lastWiFiAttempt = 0;
unsigned long disconnectedSince = 0;
unsigned long lastBlynkAttempt = 0;
unsigned long lastTelemetry = 0;
unsigned long bootMillis = 0;

// ------------------------- DECLARATIONS ----------------------------

void storageBegin();
void storageLoadWiFi();
void storageSaveWiFi(const String &ssid, const String &password);
void storageLoadDeviceName();
void storageSaveDeviceName(const String &name);
String makeDeviceId();
bool isValidDeviceName(const String &name);
String htmlEscape(const String &value);
String makeConfigBackup();
bool restoreConfigBackup(const String &backup);

void relayBegin();
void relayWrite(uint8_t index, bool on);
void relayAllOff();

void wifiBegin();
void wifiStartAP();
void wifiHandle();
bool wifiIsConnected();

void webBegin();
void webHandle();
void webRegisterRoutes();
bool webAuthenticate();
void webHandleRoot();
void webHandleWiFiPage();
void webHandleDevicePage();
void webHandleSaveDevice();
void webHandleBackup();
void webHandleRestorePage();
void webHandleRestoreConfig();
void webHandleSaveWiFi();
void webHandleStatus();
void webHandleRelay();
void webHandleReboot();
void webHandleUpdatePage();
void webHandleUpdateUpload();
String htmlHeader(const String &title);
String htmlFooter();

void otaBegin();
void otaHandle();

void blynkBegin();
void blynkHandle();
void blynkSendTelemetry();

String jsonStatus();

// ------------------------- STORAGE MANAGER ------------------------

void storageBegin() {
  preferences.begin("novia-home", false);
}

void storageLoadWiFi() {
  savedSSID = preferences.getString("wifi_ssid", "");
  savedPassword = preferences.getString("wifi_pass", "");

  // Use source-code defaults only if no credentials have been saved yet.
  if (savedSSID.length() == 0 && strlen(DEFAULT_WIFI_SSID) > 0) {
    savedSSID = DEFAULT_WIFI_SSID;
    savedPassword = DEFAULT_WIFI_PASS;
  }
}

void storageSaveWiFi(const String &ssid, const String &password) {
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  savedSSID = ssid;
  savedPassword = password;
}


void storageLoadDeviceName() {
  deviceName = preferences.getString("device_name", "NOVIA HOME");
  if (deviceName.length() == 0) deviceName = "NOVIA HOME";
}

void storageSaveDeviceName(const String &name) {
  preferences.putString("device_name", name);
  deviceName = name;
}

String makeDeviceId() {
  uint64_t chipId = ESP.getEfuseMac();
  char idBuffer[13];
  snprintf(idBuffer, sizeof(idBuffer), "%04X%08X",
           (uint16_t)(chipId >> 32), (uint32_t)chipId);
  return String(idBuffer);
}

bool isValidDeviceName(const String &name) {
  if (name.length() == 0 || name.length() > 24) return false;

  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    bool allowed = (c >= 'A' && c <= 'Z') ||
                   (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9') ||
                   c == ' ' || c == '-' || c == '_';
    if (!allowed) return false;
  }
  return true;
}

String htmlEscape(const String &value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

String makeConfigBackup() {
  String backup;
  backup.reserve(220);
  backup += F("NOVIA_HOME_CONFIG_V1\n");
  backup += F("device_name=");
  backup += deviceName;
  backup += F("\nwifi_ssid=");
  backup += savedSSID;
  backup += F("\nwifi_pass=");
  backup += savedPassword;
  backup += F("\n");
  return backup;
}

bool restoreConfigBackup(const String &backup) {
  if (!backup.startsWith("NOVIA_HOME_CONFIG_V1")) return false;

  String restoredName;
  String restoredSSID;
  String restoredPassword;
  int start = backup.indexOf('\n');
  if (start < 0) return false;
  start++;

  while (start < (int)backup.length()) {
    int end = backup.indexOf('\n', start);
    if (end < 0) end = backup.length();

    String line = backup.substring(start, end);
    if (line.endsWith("\\r")) line.remove(line.length() - 1);
    int separator = line.indexOf('=');
    if (separator > 0) {
      String key = line.substring(0, separator);
      key.trim();
      String value = line.substring(separator + 1);
      if (key == "device_name") restoredName = value;
      else if (key == "wifi_ssid") restoredSSID = value;
      else if (key == "wifi_pass") restoredPassword = value;
    }
    start = end + 1;
  }

  restoredName.trim();
  if (!isValidDeviceName(restoredName) ||
      restoredSSID.length() > 32 || restoredPassword.length() > 64) {
    return false;
  }

  storageSaveDeviceName(restoredName);
  storageSaveWiFi(restoredSSID, restoredPassword);
  return true;
}

// ------------------------- RELAY MANAGER --------------------------

void relayBegin() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    // Set the output latch to OFF before enabling the output driver.
    digitalWrite(RELAY_PINS[i], RELAY_ACTIVE_HIGH ? LOW : HIGH);
    pinMode(RELAY_PINS[i], OUTPUT);
    relayWrite(i, false);
  }
}

void relayWrite(uint8_t index, bool on) {
  if (index >= RELAY_COUNT) return;

  relayState[index] = on;

  uint8_t outputLevel;
  if (RELAY_ACTIVE_HIGH) {
    outputLevel = on ? HIGH : LOW;
  } else {
    outputLevel = on ? LOW : HIGH;
  }

  digitalWrite(RELAY_PINS[index], outputLevel);
}

void relayAllOff() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    relayWrite(i, false);
  }
}

// ------------------------- WIFI MANAGER ---------------------------

void wifiBegin() {
  WiFi.mode(WIFI_STA);
  networkHostname = String("NOVIA-") + deviceId.substring(6);
  WiFi.setHostname(networkHostname.c_str());
  WiFi.setAutoReconnect(true);

  if (savedSSID.length() == 0) {
    wifiStartAP();
    return;
  }

  WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  lastWiFiAttempt = millis();

  Serial.print("Menghubungkan ke Wi-Fi: ");
  Serial.println(savedSSID);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi terhubung. IP: ");
    Serial.println(WiFi.localIP());
    disconnectedSince = 0;
  } else {
    Serial.println("Wi-Fi gagal. Menyalakan AP konfigurasi.");
    disconnectedSince = millis();
    wifiStartAP();
  }
}

void wifiStartAP() {
  if (apStarted) return;

  WiFi.mode(WIFI_AP_STA);
  bool result = WiFi.softAP(AP_SSID, AP_PASSWORD);
  apStarted = result;

  Serial.println();
  if (result) {
    Serial.println("AP konfigurasi NOVIA HOME aktif.");
    Serial.print("SSID: ");
    Serial.println(AP_SSID);
    Serial.print("Password: ");
    Serial.println(AP_PASSWORD);
    Serial.print("Buka: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Gagal memulai AP konfigurasi.");
  }
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void wifiHandle() {
  if (wifiIsConnected()) {
    disconnectedSince = 0;
    return;
  }

  if (disconnectedSince == 0) {
    disconnectedSince = millis();
  }

  if (savedSSID.length() > 0 &&
      millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL_MS) {
    lastWiFiAttempt = millis();
    Serial.println("Mencoba menghubungkan ulang Wi-Fi...");
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  }

  if (!apStarted &&
      millis() - disconnectedSince >= AP_FALLBACK_TIMEOUT_MS) {
    wifiStartAP();
  }
}

// ------------------------- WEB SERVER -----------------------------

String htmlHeader(const String &title) {
  String page;
  page.reserve(4200);
  page += F("<!doctype html><html lang='id'><head>");
  page += F("<meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>");
  page += F("<meta name='theme-color' content='#101828'>");
  page += F("<title>");
  page += htmlEscape(title);
  page += F(" · NOVIA HOME</title>");
  page += F("<style>");
  page += F(":root{--ink:#172033;--muted:#778196;--line:#e8ebf1;--paper:#f5f6f9;--white:#fff;--navy:#101828;--blue:#4263eb;--blue2:#edf1ff;--green:#15966a;--greenbg:#e9f7f1;--red:#c84b58;--shadow:0 12px 34px rgba(18,31,56,.055)}");
  page += F("*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font-family:Inter,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;-webkit-font-smoothing:antialiased}");
  page += F("a{color:inherit;text-decoration:none}.shell{max-width:1040px;margin:auto;padding:0 20px}.topbar{background:var(--navy);color:#fff;padding:22px 0 24px}.brandrow{display:flex;align-items:center;justify-content:space-between;gap:14px}.brand{display:flex;align-items:center;gap:12px}.brandmark{width:40px;height:40px;border-radius:13px;background:linear-gradient(145deg,#7188ff,#4058d9);display:grid;place-items:center;font-weight:800;font-size:17px;box-shadow:0 7px 20px #3348bd55}.brandname{font-size:16px;font-weight:750;letter-spacing:.12em}.brandtag{font-size:11px;color:#aab5ca;margin-top:3px;letter-spacing:.08em;text-transform:uppercase}.version{font-size:11px;color:#c6cde0;border:1px solid #344056;padding:7px 10px;border-radius:30px;white-space:nowrap}.hero{padding:25px 0 0;display:flex;justify-content:space-between;align-items:end;gap:20px}.eyebrow{font-size:11px;text-transform:uppercase;letter-spacing:.15em;color:#9eadd0;font-weight:700}.hero h1{font-size:clamp(25px,5vw,34px);line-height:1.12;margin:8px 0 7px;font-weight:720;letter-spacing:-.035em}.hero p{margin:0;color:#aeb9ce;font-size:13px}.connection{display:flex;align-items:center;gap:8px;padding:9px 12px;border:1px solid #344056;border-radius:30px;color:#dce5f5;font-size:12px;white-space:nowrap}.dot{width:7px;height:7px;border-radius:50%;background:#e0a64b;box-shadow:0 0 0 4px #e0a64b22}.dot.online{background:#35c58a;box-shadow:0 0 0 4px #35c58a22}.main{padding:24px 0 38px}.sectionhead{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:25px 0 13px}.sectionhead h2{font-size:15px;margin:0;font-weight:750;letter-spacing:-.01em}.sectionhint{font-size:11px;color:var(--muted)}.summarygrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.summary{background:var(--white);border:1px solid var(--line);border-radius:16px;padding:15px 16px;box-shadow:var(--shadow);min-width:0}.summarylabel{font-size:11px;color:var(--muted);margin-bottom:8px}.summaryvalue{font-size:16px;font-weight:750;letter-spacing:-.02em;overflow-wrap:anywhere}.summarysub{font-size:10px;color:var(--muted);margin-top:4px}.relaygrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.relaycard{background:var(--white);border:1px solid var(--line);border-radius:18px;padding:17px;box-shadow:var(--shadow);min-width:0;transition:border-color .18s,transform .18s}.relaytop{display:flex;justify-content:space-between;align-items:center;gap:10px}.relayidentity{display:flex;align-items:center;gap:11px}.relayicon{width:38px;height:38px;border-radius:12px;background:#f0f2f7;color:#5d6b85;display:grid;place-items:center;font-size:17px;font-weight:800}.relayname{font-size:14px;font-weight:750}.relaypin{font-size:10px;color:var(--muted);margin-top:3px}.statepill{font-size:10px;font-weight:800;letter-spacing:.06em;padding:6px 9px;border-radius:30px;background:#f0f2f6;color:#7c8798}.statepill.on{background:var(--greenbg);color:var(--green)}.relaybottom{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:17px;padding-top:14px;border-top:1px solid #f0f1f5}.relaycaption{font-size:11px;color:var(--muted)}.relayactions{display:flex;gap:7px}.action{display:inline-flex;justify-content:center;align-items:center;min-width:52px;padding:9px 13px;border-radius:10px;font-size:11px;font-weight:750;border:1px solid var(--line);background:#fff;color:#657086;transition:.15s}.action.onbtn{background:var(--blue);border-color:var(--blue);color:#fff}.action.offbtn{background:#f6f7fa}.action:active{transform:scale(.97)}.panel{background:var(--white);border:1px solid var(--line);border-radius:18px;padding:17px;box-shadow:var(--shadow)}.systemgrid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:13px}.systemitem{min-width:0}.systemlabel{font-size:10px;color:var(--muted);margin-bottom:5px}.systemvalue{font-size:12px;font-weight:700;overflow-wrap:anywhere}.toolsgrid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.tool{background:var(--white);border:1px solid var(--line);border-radius:14px;padding:14px;display:flex;align-items:center;gap:10px;box-shadow:var(--shadow);font-size:12px;font-weight:700;min-width:0}.toolicon{width:32px;height:32px;border-radius:10px;background:var(--blue2);color:var(--blue);display:grid;place-items:center;font-size:14px;flex:0 0 auto}.tool span:last-child{overflow-wrap:anywhere}.restart{margin-top:14px}.button{border:0;border-radius:10px;padding:11px 15px;background:var(--navy);color:white;font-size:12px;font-weight:700;cursor:pointer}.muted{color:var(--muted);font-size:12px}.pagehead{padding:24px 0 4px}.pagehead h1{font-size:25px;margin:0 0 6px;letter-spacing:-.03em}.pagehead p{color:var(--muted);font-size:12px;margin:0}.card{background:var(--white);border:1px solid var(--line);border-radius:18px;padding:20px;margin:14px 0;box-shadow:var(--shadow)}h2{font-size:16px;margin:0 0 14px}.actions{display:flex;gap:8px;flex-wrap:wrap}button,.btn{border:0;border-radius:10px;padding:11px 15px;background:var(--blue);color:white;font-weight:700;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;cursor:pointer;font-size:12px}.off{background:#edf0f5;color:#5f6b80}.green{color:var(--green)}.red{color:var(--red)}input,textarea{box-sizing:border-box;width:100%;padding:12px 13px;border:1px solid #dce1e9;border-radius:10px;margin:7px 0 15px;background:#fff;color:var(--ink);font:inherit;font-size:13px}input:focus,textarea:focus{outline:2px solid #dce3ff;border-color:#8094ff}label{font-size:12px;font-weight:700}.footer{text-align:center;color:#9aa3b2;font-size:10px;padding:20px 0 28px;letter-spacing:.04em}.backlink{display:inline-flex;margin-top:10px;color:var(--blue);font-size:12px;font-weight:700}");
  page += F("@media(max-width:650px){.shell{padding:0 15px}.topbar{padding:18px 0 21px}.hero{align-items:flex-start;flex-direction:column;gap:13px}.summarygrid{grid-template-columns:repeat(2,minmax(0,1fr))}.summary:last-child{grid-column:1/-1}.relaygrid{gap:10px}.relaycard{padding:13px}.relaybottom{align-items:flex-start;flex-direction:column}.relayactions{width:100%}.action{flex:1}.toolsgrid{grid-template-columns:repeat(2,minmax(0,1fr))}.card{padding:16px}.systemgrid{gap:15px}}");
  page += F("@media(max-width:360px){.relaygrid{grid-template-columns:1fr}.summarygrid{grid-template-columns:1fr}.summary:last-child{grid-column:auto}.toolsgrid{grid-template-columns:1fr}.version{font-size:10px;padding:6px 8px}}");
  page += F("</style></head><body>");
  page += F("<header class='topbar'><div class='shell'><div class='brandrow'><div class='brand'><div class='brandmark'>N</div><div><div class='brandname'>NOVIA HOME</div><div class='brandtag'>Smart living system</div></div></div><div class='version'>FIRMWARE ");
  page += FW_VERSION;
  page += F("</div></div>");
  if (title == "Dashboard") {
    page += F("<div class='hero'><div><div class='eyebrow'>Control center</div><h1>");
    page += htmlEscape(deviceName);
    page += F("</h1><p>Kelola perangkat rumah Anda dengan mudah.</p></div><div class='connection'><span id='wifiDot' class='dot");
    page += wifiIsConnected() ? F(" online") : F("");
    page += F("'></span><span id='wifiLabel'>");
    page += wifiIsConnected() ? F("Wi-Fi online") : F("AP / offline");
    page += F("</span></div></div>");
  } else {
    page += F("</div></header><main class='shell'><div class='pagehead'><h1>");
    page += htmlEscape(title);
    page += F("</h1><p>Pengaturan perangkat NOVIA HOME</p></div>");
    return page;
  }
  page += F("</div></header><main class='shell main'>");
  return page;
}

String htmlFooter() {
  String page;
  page += F("<div class='footer'>NOVIA HOME &nbsp;·&nbsp; LOCAL DEVICE CONTROL</div></main></body></html>");
  return page;
}

bool webAuthenticate() {
  if (server.authenticate(WEB_USER, WEB_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

void webBegin() {
  if (webServerStarted) return;
  webRegisterRoutes();
  server.begin();
  webServerStarted = true;
  Serial.println("Web server aktif pada port 80.");
}

void webRegisterRoutes() {
  server.on("/", HTTP_GET, webHandleRoot);
  server.on("/wifi", HTTP_GET, webHandleWiFiPage);
  server.on("/device", HTTP_GET, webHandleDevicePage);
  server.on("/savedevice", HTTP_POST, webHandleSaveDevice);
  server.on("/backup", HTTP_GET, webHandleBackup);
  server.on("/restore", HTTP_GET, webHandleRestorePage);
  server.on("/restoreconfig", HTTP_POST, webHandleRestoreConfig);
  server.on("/savewifi", HTTP_POST, webHandleSaveWiFi);
  server.on("/api/status", HTTP_GET, webHandleStatus);
  server.on("/relay", HTTP_GET, webHandleRelay);
  server.on("/reboot", HTTP_POST, webHandleReboot);
  server.on("/update", HTTP_GET, webHandleUpdatePage);
  server.on("/update", HTTP_POST,
    []() {
      if (!webAuthenticate()) return;
      bool ok = !Update.hasError();
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain",
                  ok ? "Firmware berhasil diunggah. ESP32 akan restart." :
                       "Update gagal. Periksa file BIN dan partisi OTA.");
      if (ok) {
        delay(700);
        ESP.restart();
      }
    },
    webHandleUpdateUpload
  );

  server.onNotFound([]() {
    if (!webAuthenticate()) return;
    server.send(404, "text/plain", "Halaman tidak ditemukan");
  });
}

void webHandleRoot() {
  if (!webAuthenticate()) return;

  String page = htmlHeader("Dashboard");

  // Summary cards
  page += F("<div class='summarygrid'>");
  page += F("<div class='summary'><div class='summarylabel'>STATUS JARINGAN</div><div class='summaryvalue' id='networkValue'>");
  page += wifiIsConnected() ? F("Terhubung") : F("Mode AP");
  page += F("</div><div class='summarysub' id='networkSub'>IP: ");
  page += wifiIsConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  page += F("</div></div>");
  page += F("<div class='summary'><div class='summarylabel'>KONEKSI BLYNK</div><div class='summaryvalue' id='blynkValue'>");
  page += Blynk.connected() ? F("Online") : F("Offline");
  page += F("</div><div class='summarysub'>Cloud connection</div></div>");
  page += F("<div class='summary'><div class='summarylabel'>WAKTU AKTIF</div><div class='summaryvalue' id='uptimeValue'>");
  page += String((millis() - bootMillis) / 1000);
  page += F(" detik</div><div class='summarysub'>Sejak perangkat menyala</div></div>");
  page += F("</div>");

  // Relay controls
  page += F("<div class='sectionhead'><h2>Kontrol perangkat</h2><span class='sectionhint'>4 CHANNEL RELAY</span></div>");
  page += F("<div class='relaygrid'>");
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    page += F("<article class='relaycard'><div class='relaytop'><div class='relayidentity'><div class='relayicon'>");
    page += String(i + 1);
    page += F("</div><div><div class='relayname'>Relay ");
    page += String(i + 1);
    page += F("</div><div class='relaypin'>GPIO ");
    page += String(RELAY_PINS[i]);
    page += F("</div></div></div><span id='relayState");
    page += String(i + 1);
    page += F("' class='statepill");
    page += relayState[i] ? F(" on") : F("");
    page += F("'>");
    page += relayState[i] ? F("ON") : F("OFF");
    page += F("</span></div><div class='relaybottom'><span class='relaycaption'>");
    page += relayState[i] ? F("Perangkat aktif") : F("Perangkat tidak aktif");
    page += F("</span><div class='relayactions'><a class='action onbtn' href='/relay?ch=");
    page += String(i + 1);
    page += F("&state=1'>NYALAKAN</a><a class='action offbtn' href='/relay?ch=");
    page += String(i + 1);
    page += F("&state=0'>MATIKAN</a></div></div></article>");
  }
  page += F("</div>");

  // System details
  page += F("<div class='sectionhead'><h2>Informasi sistem</h2><span class='sectionhint'>DEVICE OVERVIEW</span></div>");
  page += F("<div class='panel'><div class='systemgrid'>");
  page += F("<div class='systemitem'><div class='systemlabel'>NAMA PERANGKAT</div><div class='systemvalue'>");
  page += htmlEscape(deviceName);
  page += F("</div></div><div class='systemitem'><div class='systemlabel'>DEVICE ID</div><div class='systemvalue'>");
  page += deviceId;
  page += F("</div></div><div class='systemitem'><div class='systemlabel'>FIRMWARE</div><div class='systemvalue'>");
  page += FW_VERSION;
  page += F("</div></div><div class='systemitem'><div class='systemlabel'>ALAMAT IP</div><div class='systemvalue' id='ipValue'>");
  page += wifiIsConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  page += F("</div></div><div class='systemitem'><div class='systemlabel'>SINYAL WI-FI</div><div class='systemvalue' id='rssiValue'>");
  page += wifiIsConnected() ? String(WiFi.RSSI()) + " dBm" : String("—");
  page += F("</div></div><div class='systemitem'><div class='systemlabel'>MODE RELAY</div><div class='systemvalue'>ACTIVE LOW</div></div>");
  page += F("</div></div>");

  // Quick settings
  page += F("<div class='sectionhead'><h2>Pengaturan perangkat</h2><span class='sectionhint'>QUICK ACCESS</span></div>");
  page += F("<div class='toolsgrid'><a class='tool' href='/device'><span class='toolicon'>ID</span><span>Identitas</span></a>");
  page += F("<a class='tool' href='/wifi'><span class='toolicon'>Wi</span><span>Wi-Fi</span></a>");
  page += F("<a class='tool' href='/backup'><span class='toolicon'>↓</span><span>Backup</span></a>");
  page += F("<a class='tool' href='/restore'><span class='toolicon'>↑</span><span>Restore</span></a>");
  page += F("<a class='tool' href='/update'><span class='toolicon'>↻</span><span>Firmware</span></a>");
  page += F("<a class='tool' href='/api/status'><span class='toolicon'>{ }</span><span>Status API</span></a></div>");
  page += F("<form class='restart' method='post' action='/reboot' onsubmit=\"return confirm('Restart perangkat NOVIA HOME?')\"><button class='button' type='submit'>Restart perangkat</button></form>");

  // Light polling: update status labels without reloading the control page.
  page += F("<script>");
  page += F("function refreshStatus(){fetch('/api/status',{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('status');return r.json()}).then(d=>{");
  page += F("var w=d.wifi_connected;var dot=document.getElementById('wifiDot');if(dot)dot.className='dot'+(w?' online':'');");
  page += F("var wl=document.getElementById('wifiLabel');if(wl)wl.textContent=w?'Wi-Fi online':'AP / offline';");
  page += F("var nv=document.getElementById('networkValue');if(nv)nv.textContent=w?'Terhubung':'Mode AP';");
  page += F("var ns=document.getElementById('networkSub');if(ns)ns.textContent='IP: '+d.ip;");
  page += F("var bv=document.getElementById('blynkValue');if(bv)bv.textContent=d.blynk_connected?'Online':'Offline';");
  page += F("var uv=document.getElementById('uptimeValue');if(uv)uv.textContent=d.uptime+' detik';");
  page += F("var ip=document.getElementById('ipValue');if(ip)ip.textContent=d.ip;");
  page += F("var rs=document.getElementById('rssiValue');if(rs)rs.textContent=w?d.rssi+' dBm':'—';");
  page += F("if(d.relays){d.relays.forEach((v,i)=>{var e=document.getElementById('relayState'+(i+1));if(e){e.textContent=v?'ON':'OFF';e.className='statepill'+(v?' on':'');}})}");
  page += F("}).catch(()=>{});}setInterval(refreshStatus,5000);");
  page += F("</script>");

  page += htmlFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

void webHandleDevicePage() {
  if (!webAuthenticate()) return;

  String page = htmlHeader("Identitas Perangkat");
  page += F("<div class='card'><h2>Identitas Perangkat</h2>");
  page += F("<p><b>Device ID:</b><br>");
  page += deviceId;
  page += F("</p><p><b>Firmware:</b> ");
  page += FW_VERSION;
  page += F("</p><form method='post' action='/savedevice'>");
  page += F("<label>Nama perangkat (maks. 24 karakter)</label>");
  page += F("<input name='device_name' maxlength='24' required value='");
  page += htmlEscape(deviceName);
  page += F("'>");
  page += F("<button type='submit'>Simpan Nama Perangkat</button></form>");
  page += F("<p class='muted'>Device ID berasal dari identitas chip ESP32 dan tidak berubah saat nama perangkat diganti.</p>");
  page += F("<a class='btn off' href='/'>Kembali</a></div>");
  page += htmlFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

void webHandleSaveDevice() {
  if (!webAuthenticate()) return;

  String newName = server.arg("device_name");
  newName.trim();
  if (!isValidDeviceName(newName)) {
    server.send(400, "text/plain",
      "Nama harus 1-24 karakter dan hanya boleh memakai huruf, angka, spasi, tanda minus, atau garis bawah.");
    return;
  }

  storageSaveDeviceName(newName);
  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><html lang='id'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:Arial;padding:24px'><h2>Nama perangkat tersimpan</h2>"
    "<p>Kembali ke dashboard untuk melihat nama baru.</p><a href='/'>Dashboard</a></body></html>");
}

void webHandleBackup() {
  if (!webAuthenticate()) return;
  server.sendHeader("Content-Disposition", "attachment; filename=NOVIA_HOME_CONFIG.txt");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain; charset=utf-8", makeConfigBackup());
}

void webHandleRestorePage() {
  if (!webAuthenticate()) return;

  String page = htmlHeader("Pulihkan Konfigurasi");
  page += F("<div class='card'><h2>Pulihkan Konfigurasi</h2>");
  page += F("<p class='muted'>Pilih file NOVIA_HOME_CONFIG.txt hasil backup. Isinya akan dimasukkan ke kolom di bawah. Setelah dipulihkan, ESP32 akan restart.</p>");
  page += F("<input type='file' id='backupFile' accept='.txt,text/plain'>");
  page += F("<form method='post' action='/restoreconfig' onsubmit=\"return confirm('Pulihkan konfigurasi dan restart ESP32?')\">");
  page += F("<textarea id='backupText' name='backup' rows='10' required style='width:100%;box-sizing:border-box;padding:10px;border:1px solid #cfd6e2;border-radius:9px'></textarea>");
  page += F("<button type='submit'>Pulihkan Konfigurasi</button></form>");
  page += F("<p class='muted'>File backup memuat password Wi-Fi dalam teks biasa. Jangan bagikan file ini kepada orang lain.</p>");
  page += F("<a class='btn off' href='/'>Kembali</a></div>");
  page += F("<script>document.getElementById('backupFile').addEventListener('change',function(){var f=this.files[0];if(!f)return;var r=new FileReader();r.onload=function(){document.getElementById('backupText').value=r.result;};r.readAsText(f);});</script>");
  page += htmlFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

void webHandleRestoreConfig() {
  if (!webAuthenticate()) return;

  String backup = server.arg("backup");
  if (backup.length() > 512 || !restoreConfigBackup(backup)) {
    server.send(400, "text/plain", "File backup tidak valid atau formatnya tidak dikenali. Tidak ada konfigurasi yang dipulihkan.");
    return;
  }

  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><html lang='id'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:Arial;padding:24px'><h2>Konfigurasi berhasil dipulihkan</h2>"
    "<p>ESP32 akan restart dan mencoba menggunakan konfigurasi Wi-Fi dari backup.</p></body></html>");
  delay(1200);
  ESP.restart();
}

void webHandleWiFiPage() {
  if (!webAuthenticate()) return;

  String page = htmlHeader("Pengaturan Wi-Fi");
  page += F("<div class='card'><h2>Konfigurasi Wi-Fi</h2>");
  page += F("<p class='muted'>Masukkan nama dan password Wi-Fi 2.4 GHz. Setelah disimpan, ESP32 akan restart.</p>");
  page += F("<form method='post' action='/savewifi'>");
  page += F("<label>Nama Wi-Fi (SSID)</label><input name='ssid' maxlength='32' required autocomplete='off'>");
  page += F("<label>Password Wi-Fi</label><input name='pass' type='password' maxlength='64' autocomplete='new-password'>");
  page += F("<button type='submit'>Simpan dan Restart</button></form>");
  page += F("<p class='muted'>Jaringan harus 2.4 GHz. Jangan gunakan tanda kutip dalam nama jaringan.</p>");
  page += F("<a class='btn off' href='/'>Kembali ke Dashboard</a></div>");
  page += htmlFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

void webHandleSaveWiFi() {
  if (!webAuthenticate()) return;

  String ssid = server.arg("ssid");
  String password = server.arg("pass");
  ssid.trim();

  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64) {
    server.send(400, "text/plain", "SSID atau password tidak valid.");
    return;
  }

  storageSaveWiFi(ssid, password);
  server.send(200, "text/html; charset=utf-8",
    "<!doctype html><html lang='id'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<body style='font-family:Arial;padding:24px'><h2>Wi-Fi tersimpan</h2>"
    "<p>ESP32 akan restart dan mencoba terhubung ke jaringan baru.</p></body></html>");
  delay(1000);
  ESP.restart();
}

void webHandleStatus() {
  if (!webAuthenticate()) return;
  server.send(200, "application/json", jsonStatus());
}

String jsonStatus() {
  String json = "{";
  json += "\"device\":\"";
  json += deviceName;
  json += "\",\"device_id\":\"";
  json += deviceId;
  json += "\",";
  json += "\"firmware\":\"";
  json += FW_VERSION;
  json += "\",\"wifi_connected\":";
  json += wifiIsConnected() ? "true" : "false";
  json += ",\"ip\":\"";
  json += wifiIsConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  json += "\",\"rssi\":";
  json += wifiIsConnected() ? String(WiFi.RSSI()) : String(0);
  json += ",\"uptime\":";
  json += String((millis() - bootMillis) / 1000);
  json += ",\"blynk_connected\":";
  json += Blynk.connected() ? "true" : "false";
  json += ",\"relays\":[";
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    if (i > 0) json += ",";
    json += relayState[i] ? "true" : "false";
  }
  json += "]}";
  return json;
}

void webHandleRelay() {
  if (!webAuthenticate()) return;

  if (!server.hasArg("ch") || !server.hasArg("state")) {
    server.send(400, "text/plain", "Parameter ch dan state diperlukan.");
    return;
  }

  int channel = server.arg("ch").toInt();
  int state = server.arg("state").toInt();

  if (channel < 1 || channel > RELAY_COUNT || (state != 0 && state != 1)) {
    server.send(400, "text/plain", "Parameter tidak valid.");
    return;
  }

  relayWrite((uint8_t)(channel - 1), state == 1);
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "OK");
}

void webHandleReboot() {
  if (!webAuthenticate()) return;
  server.send(200, "text/plain", "ESP32 akan restart.");
  delay(500);
  ESP.restart();
}

void webHandleUpdatePage() {
  if (!webAuthenticate()) return;

  String page = htmlHeader("Firmware Update");
  page += F("<div class='card'><h2>Firmware Update</h2>");
  page += F("<p class='muted'>Pilih file firmware .bin yang sudah dikompilasi untuk ESP32 yang sama.</p>");
  page += F("<form method='POST' action='/update' enctype='multipart/form-data'>");
  page += F("<input type='file' name='firmware' accept='.bin' required>");
  page += F("<button type='submit'>Unggah Firmware</button></form>");
  page += F("<p class='muted'>Jangan matikan daya selama proses update. Gunakan hanya file BIN yang benar.</p>");
  page += F("<a class='btn off' href='/'>Kembali</a></div>");
  page += htmlFooter();
  server.send(200, "text/html; charset=utf-8", page);
}

void webHandleUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (!webAuthenticate()) return;
    Serial.printf("Mulai update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) {
      Serial.printf("Update selesai: %u bytes\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("Update dibatalkan.");
  }
}

// ------------------------- OTA MANAGER ----------------------------

void otaBegin() {
  if (otaStarted || !wifiIsConnected()) return;

  ArduinoOTA.setHostname(networkHostname.c_str());
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("ArduinoOTA: proses update dimulai.");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nArduinoOTA: selesai.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA: %u%%\r", (progress * 100) / total);
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error[%u]\n", error);
  });

  ArduinoOTA.begin();
  otaStarted = true;
  Serial.println("ArduinoOTA siap.");
}

void otaHandle() {
  if (otaStarted && wifiIsConnected()) {
    ArduinoOTA.handle();
  }
}

// ------------------------- BLYNK CONNECTOR ------------------------

void blynkBegin() {
  if (!wifiIsConnected()) return;
  Blynk.config(BLYNK_AUTH_TOKEN);
  blynkConfigured = true;
  lastBlynkAttempt = 0;
}

void blynkHandle() {
  if (!wifiIsConnected()) return;

  if (!Blynk.connected() &&
      millis() - lastBlynkAttempt >= BLYNK_RETRY_INTERVAL_MS) {
    lastBlynkAttempt = millis();
    Blynk.connect(1000);
  }

  if (Blynk.connected()) {
    Blynk.run();
  }
}

void blynkSendTelemetry() {
  if (!Blynk.connected()) return;

  Blynk.virtualWrite(V5, (millis() - bootMillis) / 1000);
  Blynk.virtualWrite(V6, WiFi.RSSI());
}

// Blynk virtual pin callbacks
BLYNK_WRITE(V0) { relayWrite(0, param.asInt() == 1); }
BLYNK_WRITE(V1) { relayWrite(1, param.asInt() == 1); }
BLYNK_WRITE(V2) { relayWrite(2, param.asInt() == 1); }
BLYNK_WRITE(V3) { relayWrite(3, param.asInt() == 1); }

// ------------------------- MAIN SETUP / LOOP ----------------------

void setup() {
  Serial.begin(115200);
  delay(300);
  bootMillis = millis();

  Serial.println();
  Serial.println("================================");
  Serial.println(" NOVIA HOME Firmware V1.2.1");
  Serial.println("================================");

  relayBegin();
  storageBegin();
  storageLoadWiFi();
  storageLoadDeviceName();
  deviceId = makeDeviceId();
  Serial.print("Device ID: ");
  Serial.println(deviceId);

  wifiBegin();

  webBegin();

  if (wifiIsConnected()) {
    otaBegin();
    blynkBegin();
  }
}

void loop() {
  wifiHandle();

  // Initialize network services if Wi-Fi connects after AP fallback/retry.
  if (wifiIsConnected() && !otaStarted) otaBegin();
  if (wifiIsConnected() && !blynkConfigured) blynkBegin();

  if (webServerStarted) {
    server.handleClient();
  }

  otaHandle();
  blynkHandle();

  if (Blynk.connected() &&
      millis() - lastTelemetry >= 10000) {
    lastTelemetry = millis();
    blynkSendTelemetry();
  }

  delay(2);
}
