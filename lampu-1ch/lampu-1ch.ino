/*
  ================================================================
  NOVIA HOME V1.2.0
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
static const char FW_VERSION[]  = "1.2.0";

static const uint8_t RELAY_PINS[4] = {23, 22, 21, 19};
static const uint8_t RELAY_COUNT = 4;
static const bool RELAY_ACTIVE_HIGH = false;

static const char DEFAULT_WIFI_SSID[] = "";
static const char DEFAULT_WIFI_PASS[] = "";

static const char AP_SSID[] = "NOVIA-HOME-SETUP";
static const char AP_PASSWORD[] = "Noke0710";

static const char WEB_USER[] = "admin";
static const char WEB_PASSWORD[] = "Noke0710";

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
  page.reserve(1200);
  page += F("<!doctype html><html lang='id'><head>");
  page += F("<meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<meta name='theme-color' content='#101d36'>");
  page += F("<title>");
  page += title;
  page += F(" - NOVIA HOME</title>");
  page += F("<style>");
  page += F("body{margin:0;background:#f3f5f9;color:#17233b;font-family:Arial,sans-serif}");
  page += F(".top{background:#101d36;color:white;padding:22px 16px}");
  page += F(".wrap{max-width:760px;margin:auto;padding:16px}");
  page += F(".card{background:white;border-radius:16px;padding:18px;margin:12px 0;box-shadow:0 3px 14px #15213a12}");
  page += F("h1{font-size:22px;margin:0 0 6px}h2{font-size:18px;margin:0 0 12px}");
  page += F(".muted{color:#6b7890;font-size:13px}");
  page += F(".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}");
  page += F(".relay{border:1px solid #e3e8f0;border-radius:12px;padding:14px}");
  page += F(".state{font-weight:bold;margin:8px 0}");
  page += F("button,.btn{border:0;border-radius:10px;padding:11px 15px;background:#174ea6;color:white;font-weight:bold;text-decoration:none;display:inline-block;cursor:pointer}");
  page += F(".off{background:#667085}.green{color:#12804a}.red{color:#c0392b}");
  page += F("input{box-sizing:border-box;width:100%;padding:12px;border:1px solid #cfd6e2;border-radius:9px;margin:6px 0 14px}");
  page += F("label{font-size:14px;font-weight:bold}.actions{display:flex;gap:8px;flex-wrap:wrap}");
  page += F("@media(max-width:430px){.grid{grid-template-columns:1fr}.wrap{padding:12px}}");
  page += F("</style></head><body>");
  page += F("<div class='top'><div class='wrap'><h1>");
  page += htmlEscape(deviceName);
  page += F("</h1>");
  page += F("<div class='muted' style='color:#c9d4e8'>Smart Relay Controller · Firmware ");
  page += FW_VERSION;
  page += F("</div></div></div><main class='wrap'>");
  return page;
}

String htmlFooter() {
  String page;
  page += F("<p class='muted' style='text-align:center;padding:16px'>NOVIA HOME · Local control</p>");
  page += F("</main></body></html>");
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
  page += F("<div class='card'><h2>Dashboard perangkat</h2>");
  page += F("<div class='muted'>Status koneksi: ");
  page += wifiIsConnected() ? F("<b class='green'>Wi-Fi terhubung</b>") :
                              F("<b class='red'>Belum terhubung</b>");
  page += F("</div><p class='muted'>IP perangkat: ");
  page += wifiIsConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  page += F("</p><p class='muted'>Nama perangkat: ");
  page += htmlEscape(deviceName);
  page += F("<br>ID perangkat: ");
  page += deviceId;
  page += F("</p><div class='actions'><a class='btn' href='/device'>Identitas Perangkat</a>");
  page += F("<a class='btn' href='/wifi'>Pengaturan Wi-Fi</a>");
  page += F("<a class='btn' href='/backup'>Backup Konfigurasi</a>");
  page += F("<a class='btn' href='/restore'>Restore</a>");
  page += F("<a class='btn' href='/update'>Firmware Update</a></div></div>");

  page += F("<div class='card'><h2>Kontrol relay</h2><div class='grid'>");
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    page += F("<div class='relay'><b>Relay ");
    page += String(i + 1);
    page += F("</b><div class='state'>");
    page += relayState[i] ? F("<span class='green'>MENYALA</span>") :
                            F("<span class='muted'>MATI</span>");
    page += F("</div><div class='actions'><a class='btn' href='/relay?ch=");
    page += String(i + 1);
    page += F("&state=1'>ON</a><a class='btn off' href='/relay?ch=");
    page += String(i + 1);
    page += F("&state=0'>OFF</a></div></div>");
  }
  page += F("</div></div>");

  page += F("<div class='card'><h2>Informasi sistem</h2><p>Firmware: ");
  page += FW_VERSION;
  page += F("</p><p>Uptime: ");
  page += String((millis() - bootMillis) / 1000);
  page += F(" detik</p><p>Blynk: ");
  page += Blynk.connected() ? F("<span class='green'>Terhubung</span>") :
                              F("<span class='muted'>Tidak terhubung</span>");
  page += F("</p><form method='post' action='/reboot' onsubmit=\"return confirm('Restart perangkat?')\"><button class='off'>Restart ESP32</button></form></div>");
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
  Serial.println(" NOVIA HOME Firmware V1.2.0");
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
