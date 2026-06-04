// ═══════════════════════════════════════════════════════════════════════════════
// UnoR4_RCCar.ino  —  RC Rover firmware, reconciled to the RCRoverRTK Android app
//
// Board    : Arduino UNO R4 WiFi  (Renesas RA4M1 + ESP32-S3 radio, WiFiS3 library)
// Protocol : HTTP/1.1 over WiFi SoftAP — the app drives the car with plain GET requests.
//
// NOTE: the Android app's source comments mention an "ESP32-S3", but the actual control
// board here is the UNO R4 WiFi. (The R4 WiFi *does* use an ESP32-S3 as its radio
// co-processor, which is likely where that label came from.) This firmware targets the
// R4 WiFi via the WiFiS3 stack and implements EXACTLY the app's HTTP contract
// (see com.rcdriving.controller.gps.WifiHttpManager / DrivingViewModel):
//
//   SoftAP SSID      "RC_Car_GPS"      (SettingsScreen.kt)
//   SoftAP password  "rccar1234"
//   Gateway IP       192.168.4.1       (WifiHttpManager.CAR_IP — WiFiS3 AP default)
//   HTTP port        80
//
//   GET /                 → 200 "OK"      ping/keepalive, feeds the deadman timer
//   GET /rssi             → "<int>"       RSSI in dBm, as plain text
//   GET /speed?v=<0..100> → 200           set speed percent (applied immediately if moving)
//   GET /forward          → 200           drive forward, steering centered
//   GET /fwd_left         → 200           drive forward, steer left
//   GET /fwd_right        → 200           drive forward, steer right
//   GET /reverse          → 200           drive reverse, steering centered
//   GET /rev_left         → 200           drive reverse, steer left
//   GET /rev_right        → 200           drive reverse, steer right
//   GET /slow             → 200           cap speed to SLOW_PERCENT (used by return-home)
//   GET /stop             → 200           hard stop + recenter steering
//
// FAILSAFE: the app polls GET / at ~1 Hz as a keepalive. If NO request of any kind
// arrives for FAILSAFE_MS (2.5 s — matches the Settings screen description), the motor
// is cut automatically to prevent a runaway when WiFi drops.
//
// Hardware (IBT-2/BTS7960 motor driver + steering servo) and the servo calibration
// angles are carried over unchanged from the original UDP sketch — these are real UNO
// pin numbers, so the wiring is identical; only the network protocol changed.
// ═══════════════════════════════════════════════════════════════════════════════

#include <WiFiS3.h>
#include <Servo.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

// ── Access Point Credentials (must match the app) ──────────────────────────────
const char* AP_SSID = "RC_Car_GPS";
const char* AP_PASS = "rccar1234";

WiFiServer server(80);

// ── IBT-2 (BTS7960) Motor Driver Pins ──────────────────────────────────────────
#define RPWM   5     // forward PWM
#define LPWM   6     // reverse PWM
#define R_EN   7     // right half-bridge enable
#define L_EN   8     // left  half-bridge enable

// ── Servo Steering ──────────────────────────────────────────────────────────────
#define SERVO_PIN     9
#define SERVO_CENTER  135
#define SERVO_LEFT    100
#define SERVO_RIGHT   170
Servo steeringServo;

// ── LED Matrix ───────────────────────────────────────────────────────────────────
ArduinoLEDMatrix matrix;

// ── Failsafe / speed ──────────────────────────────────────────────────────────────
#define FAILSAFE_MS   2500UL
#define SLOW_PERCENT  35
#define PWM_MAX       255

// ── State ───────────────────────────────────────────────────────────────────────
enum Motion { M_STOP, M_FWD, M_REV };
Motion currentMotion = M_STOP;
int    speedPercent  = 0;        // 0..100, set by /speed?v=
unsigned long lastCmdMs = 0;     // last time ANY request arrived (deadman)

// ─────────────────────────────────────────────────────────────────────────────
// LED matrix helper
// ─────────────────────────────────────────────────────────────────────────────
void showLabel(const char* text) {
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.print(text);
  matrix.endText();
  matrix.endDraw();
}

// ─────────────────────────────────────────────────────────────────────────────
// Motor + steering helpers
// ─────────────────────────────────────────────────────────────────────────────
static inline int percentToPwm(int pct) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return (pct * PWM_MAX) / 100;
}

void motorWrite(int rpwm, int lpwm) { analogWrite(RPWM, rpwm); analogWrite(LPWM, lpwm); }

void applyMotion() {
  const int pwm = percentToPwm(speedPercent);
  if      (currentMotion == M_FWD) motorWrite(pwm, 0);
  else if (currentMotion == M_REV) motorWrite(0,   pwm);
  else                             motorWrite(0,   0);
}

void steerCenter() { steeringServo.write(SERVO_CENTER); }
void steerLeft()   { steeringServo.write(SERVO_LEFT);   }
void steerRight()  { steeringServo.write(SERVO_RIGHT);  }

void hardStop() {
  currentMotion = M_STOP;
  speedPercent  = 0;
  motorWrite(0, 0);
  steerCenter();
}

// ─────────────────────────────────────────────────────────────────────────────
// Request routing — `path` is the URL incl. any query string, e.g. "/speed?v=40".
// Returns the plain-text body to send (RSSI route returns a number; others "OK").
// ─────────────────────────────────────────────────────────────────────────────
String route(const String& path) {
  lastCmdMs = millis();   // any request feeds the deadman

  if (path == "/")        { return "OK"; }
  if (path == "/rssi")    { return String(WiFi.RSSI()); }

  if (path.startsWith("/speed")) {
    int i = path.indexOf("v=");
    if (i >= 0) { speedPercent = constrain(path.substring(i + 2).toInt(), 0, 100); }
    applyMotion();                       // take effect now if already moving
    return "OK";
  }

  if      (path == "/forward")   { currentMotion = M_FWD; steerCenter(); applyMotion(); showLabel("F"); }
  else if (path == "/fwd_left")  { currentMotion = M_FWD; steerLeft();   applyMotion(); showLabel("FL"); }
  else if (path == "/fwd_right") { currentMotion = M_FWD; steerRight();  applyMotion(); showLabel("FR"); }
  else if (path == "/reverse")   { currentMotion = M_REV; steerCenter(); applyMotion(); showLabel("R"); }
  else if (path == "/rev_left")  { currentMotion = M_REV; steerLeft();   applyMotion(); showLabel("RL"); }
  else if (path == "/rev_right") { currentMotion = M_REV; steerRight();  applyMotion(); showLabel("RR"); }
  else if (path == "/slow")      { speedPercent = SLOW_PERCENT; applyMotion(); showLabel("SL"); }
  else if (path == "/stop")      { hardStop(); showLabel("ST"); }
  else                           { return ""; }   // 404

  return "OK";
}

// ─────────────────────────────────────────────────────────────────────────────
// Parse the URL token out of the HTTP request line: "GET /path?x=y HTTP/1.1".
// ─────────────────────────────────────────────────────────────────────────────
String parsePath(const String& requestLine) {
  int s = requestLine.indexOf(' ');
  if (s < 0) return "";
  int e = requestLine.indexOf(' ', s + 1);
  if (e < 0) return "";
  return requestLine.substring(s + 1, e);
}

void handleClient(WiFiClient& client) {
  // Read only the request line (first CRLF-terminated line); GET has no body.
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();

  // Drain the remaining request headers so the socket closes cleanly.
  while (client.connected() && client.available()) {
    String h = client.readStringUntil('\n');
    if (h == "\r" || h.length() <= 1) break;
  }

  const String path = parsePath(requestLine);
  const String body = route(path);

  if (body.length() == 0 && path != "/" ) {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  } else {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.print  ("Content-Length: "); client.println(body.length());
    client.println("Connection: close");
    client.println();
    client.print(body);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bring the SoftAP up (also used by the watchdog to recover a dropped AP).
// ─────────────────────────────────────────────────────────────────────────────
void startAP() {
  Serial.println("Starting AP...");
  if (WiFi.beginAP(AP_SSID, AP_PASS) != WL_AP_LISTENING) {
    Serial.println("AP failed!");
    showLabel("ER");
    while (true) { delay(1000); }
  }
  delay(2000);
  Serial.print("AP IP: "); Serial.println(WiFi.localIP());
  server.begin();
  showLabel("AP");
}

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  matrix.begin();
  showLabel("--");

  pinMode(RPWM, OUTPUT); pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT); pinMode(L_EN, OUTPUT);
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  motorWrite(0, 0);

  steeringServo.attach(SERVO_PIN);
  steerCenter();

  startAP();
  lastCmdMs = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main loop
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // AP watchdog — recover if the access point drops.
  int st = WiFi.status();
  if (st != WL_AP_LISTENING && st != WL_AP_CONNECTED) {
    Serial.println("AP lost — restarting...");
    showLabel("ER");
    startAP();
  }

  // Serve one pending HTTP request, if any.
  WiFiClient client = server.available();
  if (client) {
    handleClient(client);
    client.stop();
  }

  // Deadman: if the control link goes quiet, cut the motor.
  if (currentMotion != M_STOP && (millis() - lastCmdMs) > FAILSAFE_MS) {
    Serial.println("FAILSAFE: link lost — stopping motor");
    hardStop();
    showLabel("FS");
  }
}
