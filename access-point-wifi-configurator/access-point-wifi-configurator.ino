#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <HTTPClient.h> 
#include <ArduinoJson.h>

#define CONFIG_BUTTON D1
#define LONG_PRESS_MS 6000
#define LED D0
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x3D,16,2);
String payload;

WebServer server(80);
Preferences prefs;

bool startConfigPortal = false;
long configLedTime= 0;
long apiFetchTime= 0;
int hour= 0;
int minute= 0;
int second= 0;
String rawtime;
bool firstPass= true;
unsigned long btnPressStart = 0;
bool btnWasPressed = false;
bool portalRunning = false;
unsigned long nextFetchTime = 0;
const char* networkName= "Clock";
long noWiFiTime= 0;
bool justDisconnected= true;

String makePage(String options) { // This is where the HTML + CSS of the access point is created
  return R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>Clock Wi-Fi setup</title>

    <style>

    body {
      font-family: Arial, sans-serif;
      background: #f2f4f8;
      margin: 0;
      padding: 0;
    }

    .container {
      max-width: 380px;
      margin: 40px auto;
      background: white;
      padding: 24px;
      border-radius: 12px;
      box-shadow: 0 4px 14px rgba(0,0,0,0.12);
    }

    h2 {
      text-align: center;
      margin-bottom: 20px;
      color: #333;
    }

    label {
      font-weight: bold;
      font-size: 14px;
    }

    select, input[type=password] {
      width: 100%;
      padding: 12px;
      margin-top: 6px;
      margin-bottom: 16px;
      border-radius: 8px;
      border: 1px solid #ccc;
      font-size: 16px;
    }

    button {
      width: 100%;
      padding: 14px;
      border: none;
      border-radius: 8px;
      background: #2e86de;
      color: white;
      font-size: 16px;
      font-weight: bold;
    }

    button:active {
      background: #1b4f72;
    }

    .footer {
      text-align: center;
      margin-top: 14px;
      font-size: 12px;
      color: #777;
    }

    </style>
    </head>

    <body>

    <div class="container">

    <h2>Configure WiFi</h2>

    <form action="/connect" method="POST">

    <label>Network</label>
    <select name="ssid">
    )rawliteral" + options + R"rawliteral(
    </select>

    <label>Password</label>
    <input type="password" name="pass" placeholder="Enter WiFi password">

    <button type="submit">Connect</button>

    </form>

    <div class="footer">
    ESP32 Setup Portal
    </div>

    </div>

    </body>
    </html>
)rawliteral";

}

String buildSSIDOptions() { // create the SSID (networks available) list
  int n = WiFi.scanNetworks();
  String opts;

  for (int i = 0; i < n; i++) {
    opts += "<option value='" + WiFi.SSID(i) + "'>";
    opts += WiFi.SSID(i);
    opts += " (";
    opts += WiFi.RSSI(i);
    opts += " dBm)";
    opts += "</option>";
  }

  return opts;
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);
  Wire.begin(6, 7);   // For my Xiao ESP32-C3, SDA = GPIO6, SCL = GPIO7 
  Serial.begin(115200);
  
  lcd.init();    
  lcd.backlight();
  lcd.noBlink();
  lcd.noCursor();
  lcd.clear();
  if (!connectSavedWiFi()) { // Only starts access point if no previous Wi-Fi credentials are saved
  startConfigPortal = true;
  }

}


void loop() {
  checkButtonRuntime(); // keep looking into whether the push button is pressed long enough

  // If no Wi-Fi credentials were found or push button was pressed long enough, create access point
  if (startConfigPortal && !portalRunning) { 
    startAPMode();
    portalRunning = true;
  }

  if (startConfigPortal) { // Handles the web page and everything that happens before AP connection
    server.handleClient();
    
    // This shows the user the name of the Wi-Fi network (SSID) in which to connect
    if(millis() - noWiFiTime > 60000 || justDisconnected == true){ 
      noWiFiTime= millis();
      justDisconnected= false;
      lcd.clear();
      lcd.setCursor(2,0);
      lcd.printf("No Wi-Fi");
      lcd.setCursor(0,1);
      lcd.printf("AP: ");
      lcd.printf(networkName);
    }
    // Just blink an LED while there is no Wi-Fi connection
    if (millis() - configLedTime > 200) {
      configLedTime += 200;
      digitalWrite(LED, !digitalRead(LED));
    }
    return;
  }

  // ===== Execution of your code of interest =====
  
  // This is what  this project does after Wi-Fi connecion, a clock on a 16x2 i2c LCD display
  if ((firstPass || millis() >= nextFetchTime) && !startConfigPortal) {

    updateTimeFromAPI(); // gets time (hour, minute, second)

    Serial.printf("%02d:%02d\n", hour, minute);
    lcd.clear();
    lcd.setCursor(2,0);
    lcd.printf("%02d:%02d", hour, minute);

    // Ensuring that the minute is updated as close as possible to second zero.
    // So effectively the time API is not read every 60 hard seconds, but
    // following a calculation that intends to get time at second zero.
    int delayToNextMinute = (60 - second) * 1000;
    nextFetchTime = millis() + delayToNextMinute;

    firstPass = false;
  }

  
}
// Keep an eye on the long button press for access point build
bool checkLongPress() {
  
  if (digitalRead(CONFIG_BUTTON) == LOW) {
    unsigned long start = millis();
    while (digitalRead(CONFIG_BUTTON) == LOW) {
      if (millis() - start > LONG_PRESS_MS) {
        return true;
      }
      delay(10);
    }
  }
  return false;
}
// Next three functions handle the Wi-Fi connection logic
void handleRoot() {
  String options = buildSSIDOptions();
  server.send(200, "text/html", makePage(options));
}
void handleConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  server.send(200, "text/html", "Saved. Rebooting...");
  delay(1500);
  ESP.restart();
}
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(networkName); // This is how it shows the network you are going to connect to

  server.on("/", handleRoot);
  server.on("/connect", HTTP_POST, handleConnect);
  server.begin();

  Serial.println("Config portal started");
}
bool connectSavedWiFi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.print("Connecting");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.println(WiFi.localIP());
    return true;
  }

  return false;
}
// Gets time from TimeZoneDB API
void updateTimeFromAPI() {

  HTTPClient http;

  // Please register an account in TimeZoneDB and get you API key and region (mine is America/Sao_Paulo)
  http.begin("https://api.timezonedb.com/v2.1/get-time-zone?key=YourAPIkey&format=json&by=zone&zone=America/Sao_Paulo");

  if (http.GET() == HTTP_CODE_OK) {
    payload = http.getString();

    JsonDocument remotedata;
    deserializeJson(remotedata, payload);

    // Time comes from TimeZoneDB as a long string, we have to extract information from it
    String timeStr = remotedata["formatted"];
    hour = timeStr.substring(11, 13).toInt(); // These numbers are positions where information is at
    minute = timeStr.substring(14, 16).toInt();
    second = timeStr.substring(17, 19).toInt();
  }

  http.end();
}
// Decides that the button was pressed long enough
void checkButtonRuntime() {
  if (digitalRead(CONFIG_BUTTON) == LOW) {

    if (!btnWasPressed) {
      btnWasPressed = true;
      btnPressStart = millis();
    }

    if (millis() - btnPressStart > LONG_PRESS_MS) {
      Serial.println("Long press detected → start config portal");
      startConfigPortalNow();
      btnWasPressed = false;
    }

  } else {
    btnWasPressed = false;
  }
}
// Disconnects Wi-Fi to start access point
void startConfigPortalNow() {
  WiFi.disconnect(true);
  delay(200);

  startConfigPortal = true;
  justDisconnected= true;
  
}

