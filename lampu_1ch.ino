#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "Tenda";
const char* password = "password_tenda_lu";

WebServer server(80);

#define RELAY 26
bool statusLampu = false;

String halamanWeb() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif;text-align:center;background:#111;color:white;padding-top:50px;}";
  html += ".box{background:#222;padding:30px;margin:20px;border-radius:20px;}";
  html += "button{font-size:28px;padding:20px 40px;border:none;border-radius:15px;color:white;margin:10px;}";
  html += ".on{background:green;} .off{background:red;}</style></head><body>";
  html += "<h1>Lampu Rumah</h1>";
  html += "<div class='box'><h2>Lampu Teras</h2><h1>";
  html += statusLampu ? "NYALA" : "MATI";
  html += "</h1>";
  html += "<a href='/on'><button class='on'>NYALAIN</button></a><br>";
  html += "<a href='/off'><button class='off'>MATIIN</button></a>";
  html += "</div></body></html>";
  return html;
}

void setup() {
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, HIGH);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); }
  Serial.println(WiFi.localIP());
  server.on("/", []() { server.send(200, "text/html", halamanWeb()); });
  server.on("/on", []() { digitalWrite(RELAY, LOW); statusLampu = true; server.sendHeader("Location", "/"); server.send(303); });
  server.on("/off", []() { digitalWrite(RELAY, HIGH); statusLampu = false; server.sendHeader("Location", "/"); server.send(303); });
  server.begin();
}
void loop() { server.handleClient(); }
