// ESP32 fire detector + automatic sprinkler
// Sensors: DHT22 (temperature), MQ135 (smoke/air), optional flame sensor
// Output : relay (active-LOW) -> sprinkler pump, optional buzzer

#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

const char* WIFI_NAME = "Demo_Name";
const char* WIFI_PASS = "Demo_Password";

// ---------------- Pins ----------------
#define DHT_PIN     4
#define MQ135_PIN   34
#define RELAY_PIN   26      // LOW = sprinkler ON, HIGH = OFF
#define BUZZER_PIN  25      // active buzzer (harmless if not connected)
#define FLAME_PIN   27      // flame sensor digital out (LOW = flame on most modules)
#define USE_FLAME   0       // set to 1 if a flame sensor is connected

// ---------------- Thresholds (tune these!) ----------------
#define SMOKE_LIMIT    1800     // MQ135 raw value; set above your clean-air reading
#define SMOKE_CONFIRM  3        // consecutive 1-second readings above limit before it counts
#define TEMP_LIMIT     55.0     // deg C; a normal hot day should never reach this

// ---------------- Sprinkler timing ----------------
#define MIN_ON_MS   20000UL     // keep running at least this long after last detection
#define MAX_ON_MS   180000UL    // hard stop (protects the water tank / pump)
#define TEST_MS     10000UL     // length of a manual test run

DHT dht(DHT_PIN, DHT22);        // change to DHT11 if that is your sensor
WebServer server(80);

// ---------------- State ----------------
float temp = NAN, hum = NAN;
int smoke = 0;
bool flame = false;
int smokeCount = 0;
bool fire = false;
bool alarmLatched = false;      // stays on until you press RESET
bool sprinklerOn = false;
bool lockout = false;           // set when MAX_ON_MS reached
unsigned long onSince = 0, lastFire = 0, testUntil = 0;
bool mdnsStarted = false;
unsigned long lastWifiTry = 0;

int readAir() {
  long s = 0;
  for (int i = 0; i < 20; i++) { s += analogRead(MQ135_PIN); delay(5); }
  return s / 20;
}

void setSprinkler(bool on) {
  digitalWrite(RELAY_PIN, on ? LOW : HIGH);
  if (on && !sprinklerOn) onSince = millis();
  sprinklerOn = on;
}

void updateSensors() {
  static unsigned long lastRead = 0, lastDht = 0;
  unsigned long now = millis();

  if (now - lastRead >= 1000) {
    lastRead = now;
    smoke = readAir();
    smokeCount = (smoke > SMOKE_LIMIT) ? min(smokeCount + 1, SMOKE_CONFIRM) : 0;
    flame = USE_FLAME ? (digitalRead(FLAME_PIN) == LOW) : false;
  }
  if (now - lastDht >= 2000) {
    lastDht = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    temp = t;               // NAN if the sensor is missing/faulty
    hum = h;
  }
}

void runLogic() {
  unsigned long now = millis();

  bool tempHigh = !isnan(temp) && temp > TEMP_LIMIT;
  fire = flame || tempHigh || (smokeCount >= SMOKE_CONFIRM);

  if (!fire) lockout = false;
  if (fire) { lastFire = now; alarmLatched = true; }

  bool testing = (testUntil != 0 && now < testUntil);
  if (!testing) testUntil = 0;

  bool wantOn = testing;
  if (!lockout) {
    if (fire) wantOn = true;
    if (sprinklerOn && (now - lastFire < MIN_ON_MS)) wantOn = true;
  }

  // Hard stop so the pump never runs forever
  if (wantOn && !testing && sprinklerOn && (now - onSince > MAX_ON_MS)) {
    wantOn = false;
    lockout = true;
  }

  setSprinkler(wantOn);
  digitalWrite(BUZZER_PIN, alarmLatched ? HIGH : LOW);
}

void handleRoot() {
  String p = "<html><head><meta http-equiv='refresh' content='3'>"
             "<meta name='viewport' content='width=device-width'></head>"
             "<body style='font-family:sans-serif;font-size:24px'><h2>Fire monitor</h2>";

  if (fire)               p += "<div style='color:white;background:red;padding:8px'><b>FIRE DETECTED</b></div>";
  else if (alarmLatched)  p += "<div style='color:white;background:orange;padding:8px'><b>ALARM (cleared, press RESET)</b></div>";
  else                    p += "<div style='color:white;background:green;padding:8px'><b>All clear</b></div>";

  if (isnan(temp) || isnan(hum)) p += "<br>DHT22 error<br>";
  else p += "<br>Temp: " + String(temp, 1) + " C<br>Humidity: " + String(hum, 1) + " %<br>";

  p += "Smoke/air value: " + String(smoke) + " (limit " + String(SMOKE_LIMIT) + ")<br>";
  if (USE_FLAME) p += String("Flame: ") + (flame ? "<b>YES</b>" : "no") + "<br>";

  p += "<br>Sprinkler: ";
  p += sprinklerOn ? "<b>ON</b>" : "<b>OFF</b>";
  if (lockout) p += "<br><b style='color:red'>Max run time reached - check tank</b>";

  p += "<br><br><a href='/test' style='font-size:30px'>TEST SPRINKLER (10 s)</a>";
  p += "<br><br><a href='/reset' style='font-size:30px'>STOP &amp; RESET ALARM</a>";
  p += "</body></html>";
  server.send(200, "text/html", p);
}

void startTest() {
  testUntil = millis() + TEST_MS;
  server.sendHeader("Location", "/");
  server.send(303);
}

void resetAlarm() {
  alarmLatched = false;
  testUntil = 0;
  lockout = false;
  setSprinkler(false);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsStarted) {
      Serial.println();
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      if (MDNS.begin("firemonitor")) Serial.println("Also open http://firemonitor.local");
      mdnsStarted = true;
    }
  } else {
    mdnsStarted = false;
    if (millis() - lastWifiTry > 10000) {     // retry every 10 s, never blocks the alarm
      lastWifiTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_NAME, WIFI_PASS);
      Serial.print(".");
    }
  }
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);          // sprinkler OFF at boot
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  if (USE_FLAME) pinMode(FLAME_PIN, INPUT);

  Serial.begin(115200);
  dht.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NAME, WIFI_PASS);

  server.on("/", handleRoot);
  server.on("/test", startTest);
  server.on("/reset", resetAlarm);
  server.begin();
}

void loop() {
  updateSensors();     // detection and sprinkler work even if WiFi is down
  runLogic();
  handleWifi();
  server.handleClient();
}
