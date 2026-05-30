#include <WiFiS3.h>
#include <Servo.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

// ── Access Point Credentials ──────────────────────────────────
const char* ap_ssid     = "RC_Car_GPS";
const char* ap_password = "rccar1234";

// ── IBT-2 Motor Driver Pins ───────────────────────────────────
#define RPWM  5
#define LPWM  6
#define R_EN  7
#define L_EN  8

// ── Servo Steering ────────────────────────────────────────────
#define SERVO_PIN  9
Servo steeringServo;

#define SERVO_CENTER  135
#define SERVO_LEFT    165
#define SERVO_RIGHT   90

// ── Web Server ────────────────────────────────────────────────
WiFiServer server(80);

// ── Built-in 12x8 LED Matrix ──────────────────────────────────
ArduinoLEDMatrix matrix;

// ── State ─────────────────────────────────────────────────────
enum Motion { M_STOP, M_FWD, M_REV };
Motion currentMotion = M_STOP;
int currentSpeed = 0;          // 0..255 PWM
int currentSpeedPct = 0;       // 0..100, for display

// ── Display Helper ────────────────────────────────────────────
void showCommand(const char* text) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.print(text);
  matrix.endText();
  matrix.endDraw();
}

// Log command name to Serial and short label to LED matrix.
void announce(const char* name, const char* shortLabel) {
  Serial.print("CMD: ");
  Serial.println(name);
  showCommand(shortLabel);
}

// ── Motor Control Functions ───────────────────────────────────
void motorForward(int speed) { analogWrite(RPWM, speed); analogWrite(LPWM, 0); }
void motorReverse(int speed) { analogWrite(RPWM, 0); analogWrite(LPWM, speed); }
void motorStop()             { analogWrite(RPWM, 0); analogWrite(LPWM, 0); }

void applyMotion() {
  if      (currentMotion == M_FWD) motorForward(currentSpeed);
  else if (currentMotion == M_REV) motorReverse(currentSpeed);
  else                             motorStop();
}

void steerCenter() { steeringServo.write(SERVO_CENTER); }
void steerLeft()   { steeringServo.write(SERVO_LEFT);   }
void steerRight()  { steeringServo.write(SERVO_RIGHT);  }

// ── Combined Drive + Steer ────────────────────────────────────
void driveForwardLeft()     { currentMotion = M_FWD; applyMotion(); steerLeft();   }
void driveForwardRight()    { currentMotion = M_FWD; applyMotion(); steerRight();  }
void driveForwardStraight() { currentMotion = M_FWD; applyMotion(); steerCenter(); }

void driveReverseLeft()     { currentMotion = M_REV; applyMotion(); steerLeft();   }
void driveReverseRight()    { currentMotion = M_REV; applyMotion(); steerRight();  }
void driveReverseStraight() { currentMotion = M_REV; applyMotion(); steerCenter(); }

void fullStop() { currentMotion = M_STOP; applyMotion(); steerCenter(); }

// ── Web Page ──────────────────────────────────────────────────
String buildPage() {
  String html = "";
  html += "<!DOCTYPE html>";
  html += "<html>";
  html += "<head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Arduino Car Control</title>";
  html += "<style>";
  html += "body { font-family: Arial; text-align: center; background: #1a1a2e; color: white; margin: 0; padding: 20px; }";
  html += "h1 { color: #00d4ff; margin-bottom: 5px; }";
  html += ".subtitle { color: #aaa; font-size: 13px; margin-bottom: 20px; }";
  html += ".grid { display: inline-grid; grid-template-columns: repeat(3, 90px); grid-template-rows: repeat(3, 90px); gap: 8px; margin: 10px auto; }";
  html += ".btn { width: 90px; height: 90px; font-size: 13px; font-weight: bold; background: #16213e; border: 2px solid #00d4ff; border-radius: 12px; color: white; cursor: pointer; text-decoration: none; display: flex; align-items: center; justify-content: center; flex-direction: column; line-height: 1.3; }";
  html += ".btn:active { background: #00d4ff; color: #1a1a2e; }";
  html += ".icon { font-size: 22px; }";
  html += ".stop-btn { background: #e94560; border-color: #e94560; }";
  html += ".stop-btn:active { background: #ff6b6b; }";
  html += ".speed-label { color: #aaa; font-size: 14px; margin-top: 20px; }";
  html += "#spd { width: 280px; height: 32px; margin-top: 8px; accent-color: #00d4ff; }";
  html += "#spdval { color: #00d4ff; font-weight: bold; }";
  html += "</style>";
  html += "</head>";
  html += "<body>";
  html += "<h1>&#x1F697; Car Control</h1>";
  html += "<div class='subtitle'>Tap buttons to drive</div>";
  html += "<div class='grid'>";
  html += "<a class='btn' href='/fwd_left'><span class='icon'>&#x2196;</span><span>FWD LEFT</span></a>";
  html += "<a class='btn' href='/forward'><span class='icon'>&#x25B2;</span><span>FORWARD</span></a>";
  html += "<a class='btn' href='/fwd_right'><span class='icon'>&#x2197;</span><span>FWD RIGHT</span></a>";
  html += "<a class='btn' href='/left'><span class='icon'>&#x25C4;</span><span>STEER LEFT</span></a>";
  html += "<a class='btn stop-btn' href='/stop'><span class='icon'>&#x25A0;</span><span>STOP</span></a>";
  html += "<a class='btn' href='/right'><span class='icon'>&#x25BA;</span><span>STEER RIGHT</span></a>";
  html += "<a class='btn' href='/rev_left'><span class='icon'>&#x2199;</span><span>REV LEFT</span></a>";
  html += "<a class='btn' href='/reverse'><span class='icon'>&#x25BC;</span><span>REVERSE</span></a>";
  html += "<a class='btn' href='/rev_right'><span class='icon'>&#x2198;</span><span>REV RIGHT</span></a>";
  html += "</div>";
  html += "<div class='speed-label'>&#x26A1; Speed: <span id='spdval'>";
  html += currentSpeedPct;
  html += "</span>%</div>";
  html += "<input id='spd' type='range' min='0' max='100' step='5' value='";
  html += currentSpeedPct;
  html += "' oninput='document.getElementById(\"spdval\").innerText=this.value'";
  html += " onchange='fetch(\"/speed?v=\"+this.value)'>";
  html += "</body>";
  html += "</html>";
  return html;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // LED matrix
  matrix.begin();
  showCommand("--");

  // IBT-2 pins
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);

  // Servo
  steeringServo.attach(SERVO_PIN);
  steeringServo.write(SERVO_CENTER);

  // Start Access Point
  Serial.println("Starting Access Point...");
  int status = WiFi.beginAP(ap_ssid, ap_password);
  if (status != WL_AP_LISTENING) {
    Serial.println("Failed to start Access Point!");
    showCommand("ER");
    while (true);
  }

  delay(2000);

  Serial.println("Access Point started!");
  Serial.print("Network name: ");
  Serial.println(ap_ssid);
  Serial.print("Password: ");
  Serial.println(ap_password);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Open browser to: http://");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started!");
  showCommand("AP");
}

// ── Show speed percent on matrix and log it to serial
void showSpeed(int pct, const char* source) {
  currentSpeedPct = pct;
  currentSpeed = map(pct, 0, 100, 0, 255);
  applyMotion();

  Serial.print("CMD: SPEED ");
  Serial.print(pct);
  Serial.print("% (via ");
  Serial.print(source);
  Serial.println(")");

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", pct);
  showCommand(buf);
}

// ── Handle a speed query (e.g. "GET /speed?v=45 HTTP/1.1") ────
void handleSpeed(const String& request, int idx) {
  int start = idx + 13;  // length of "GET /speed?v="
  int end = request.indexOf(' ', start);
  if (end < 0) end = request.length();
  int pct = constrain(request.substring(start, end).toInt(), 0, 100);
  showSpeed(pct, "slider");
}

void setSpeedTier(int pct, const char* name) {
  showSpeed(pct, name);
}

// ── Main Loop ─────────────────────────────────────────────────
void loop() {

  if (WiFi.status() != WL_AP_LISTENING && WiFi.status() != WL_AP_CONNECTED) {
    Serial.println("AP lost! Restarting...");
    showCommand("ER");
    WiFi.beginAP(ap_ssid, ap_password);
    delay(2000);
    server.begin();
    showCommand("AP");
  }

  WiFiClient client = server.available();
  if (!client) return;

  String request = "";
  unsigned long start = millis();
  while (client.connected() && millis() - start < 1000) {
    if (client.available()) {
      char c = client.read();
      request += c;
      if (request.endsWith("\r\n\r\n")) break;
    }
  }

  int idxSpeed;
  if      ((idxSpeed = request.indexOf("GET /speed?v=")) >= 0)  handleSpeed(request, idxSpeed);
  else if (request.indexOf("GET /slow")      >= 0) { setSpeedTier(30, "SLOW"); }
  else if (request.indexOf("GET /med")       >= 0) { setSpeedTier(55, "MED"); }
  else if (request.indexOf("GET /fast")      >= 0) { setSpeedTier(80, "FAST"); }
  else if (request.indexOf("GET /fwd_left")   >= 0) { driveForwardLeft();     announce("FWD LEFT",   "FL"); }
  else if (request.indexOf("GET /fwd_right")  >= 0) { driveForwardRight();    announce("FWD RIGHT",  "FR"); }
  else if (request.indexOf("GET /forward")    >= 0) { driveForwardStraight(); announce("FORWARD",    "FS"); }
  else if (request.indexOf("GET /rev_left")   >= 0) { driveReverseLeft();     announce("REV LEFT",   "RL"); }
  else if (request.indexOf("GET /rev_right")  >= 0) { driveReverseRight();    announce("REV RIGHT",  "RR"); }
  else if (request.indexOf("GET /reverse")    >= 0) { driveReverseStraight(); announce("REVERSE",    "RV"); }
  else if (request.indexOf("GET /left")       >= 0) { steerLeft();            announce("STEER LEFT", "L");  }
  else if (request.indexOf("GET /right")      >= 0) { steerRight();           announce("STEER RGT",  "R");  }
  else if (request.indexOf("GET /stop")       >= 0) { fullStop();             announce("STOP",       "ST"); }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  // Speed requests come from fetch() — no need to send full page back, but it's harmless.
  client.println(buildPage());
  client.stop();
}
