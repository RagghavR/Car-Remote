#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <ESP32Servo.h>
#include <ESP32Encoder.h>
#include <math.h>

// ---------------- ESP-NOW ----------------
const uint8_t ESPNOW_CHANNEL = 1;

// CHANGE THIS to your REMOTE ESP32 MAC:
uint8_t REMOTE_MAC[] = { 0xEC, 0xE3, 0x34, 0xD2, 0xB3, 0x74 };
// <-- CHANGE

// ---------------- Motion command format ----------------
enum MoveType : uint8_t
{
  MOVE_NONE = 0,
  MOVE_FWD  = 1,   // qty = 1..9 (10cm steps)
  MOVE_REV  = 2,   // qty = 1..9 (10cm steps)
  TURN_LT90 = 3,   // fixed 90
  TURN_RT90 = 4    // fixed 90
};

typedef struct __attribute__((packed))
{
  uint8_t move;
  uint8_t qty;
} Command;

typedef struct __attribute__((packed))
{
  uint8_t msgType;    // 1 = COMMAND_LIST
  uint8_t count;      // number of commands
  Command cmd[15];
} RemotePacket;

typedef struct __attribute__((packed))
{
  uint8_t msgType;    // 2 = STATUS
  uint8_t state;      // 0=IDLE, 1=RECEIVED, 2=EXECUTING, 3=DONE
  uint8_t index;      // executing index
  uint8_t total;      // total commands
} BotStatus;

// ---------------- Servo ----------------
Servo steeringServo;
int steeringCenter = 90;
int servoPin = 13;

// You MUST tune these two for your chassis:
int steeringLeft   = 55;    // full left (adjust)
int steeringRight  = 110;   // full right (adjust)

// --- NEW: perpendicular axle angle for spin turns (same "feel" as your wall code) ---
int steeringTurn = 0;       // try 0 first; if turns feel wrong, change to 180

// ---------------- Motor Driver Pins (your working set) ----------------
#define enA 33
#define enB 25
#define INa 26
#define INb 27
#define INc 14
#define INd 12

const int freq = 2000;
const int ledChannela = 1;
const int ledChannelb = 2;
const int resolution = 8;

// ---------------- Encoders (your working set) ----------------
ESP32Encoder encoderL;
ESP32Encoder encoderR;

// ---------------- Wheel/encoder constants (your values) ----------------
const float Wheel_circumference = 0.1885f;
const int   Pulse_count         = 24;

// 10cm step requirement:
const float STEP_M = 0.25f;

// TURN calibration:
// This now represents counts needed for a 90° SPIN turn (avg of both encoders).
long TURN90_COUNTS = 15;   // <-- start ~120 for spin, then tune

// ---------------- Storage ----------------
volatile bool gotPacket = false;
RemotePacket rxPacket;

int execIndex = 0;
bool executing = false;

// ---------------- Helpers ----------------
void motors(int leftSpeed, int rightSpeed)
{
  leftSpeed  = constrain(leftSpeed, 0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);
  ledcWrite(enA, leftSpeed);
  ledcWrite(enB, rightSpeed);
}

void forwardDir()
{
  digitalWrite(INa, LOW);
  digitalWrite(INb, HIGH);
  digitalWrite(INc, HIGH);
  digitalWrite(INd, LOW);
}

void reverseDir()
{
  digitalWrite(INa, HIGH);
  digitalWrite(INb, LOW);
  digitalWrite(INc, LOW);
  digitalWrite(INd, HIGH);
}

// --- NEW: per-wheel direction helpers (needed for spin turns) ---
void leftForward()  { digitalWrite(INa, HIGH); digitalWrite(INb, LOW);  }
void leftReverse()  { digitalWrite(INa, LOW);  digitalWrite(INb, HIGH); }

void rightForward() { digitalWrite(INc, LOW);  digitalWrite(INd, HIGH); }
void rightReverse() { digitalWrite(INc, HIGH); digitalWrite(INd, LOW);  }

void stopMotors()
{
  digitalWrite(INa, LOW);
  digitalWrite(INb, LOW);
  digitalWrite(INc, LOW);
  digitalWrite(INd, LOW);
  ledcWrite(enA, 0);
  ledcWrite(enB, 0);
}

void sendStatus(uint8_t state, uint8_t index, uint8_t total)
{
  BotStatus st;
  st.msgType = 2;
  st.state   = state;
  st.index   = index;
  st.total   = total;

  esp_now_send(REMOTE_MAC, (uint8_t*)&st, sizeof(st));
}

// counts for a distance in metres (based on your 10m code)
long countsForDistance(float metres)
{
  float revs = metres / Wheel_circumference;
  float counts = revs * (float)Pulse_count;
  return (long)(counts + 0.5f);
}

void resetEncoders()
{
  encoderL.clearCount();
  encoderR.clearCount();
}

long absL() { return labs((long)encoderL.getCount()); }
long absR() { return labs((long)encoderR.getCount()); }

// --- Execute straight move (encoder-based) ---
void doStraight(float metres, bool forward)
{
  steeringServo.write(steeringCenter);

  resetEncoders();

  if (forward) forwardDir();
  else         reverseDir();

  // speed (tune if needed)
  int leftSpeed  = 150;
  int rightSpeed = 150;

  long target = countsForDistance(metres);

  while (1)
  {
    motors(leftSpeed, rightSpeed);

    long avg = (absL() + absR()) / 2;
    if (avg >= target) break;

    delay(10);
  }

  stopMotors();
  delay(200);
}

// --- Execute 90-degree turn (IMPROVED) ---
// Encoder-based SPIN turn (same style as your wall code):
// left turn  = left wheel reverse + right wheel forward
// right turn = left wheel forward + right wheel reverse
void doTurn90(bool leftTurn)
{
  resetEncoders();

  steeringServo.write(steeringTurn);  // perpendicular axle
  delay(150);

  if (leftTurn)
  {
    leftReverse();
    rightForward();
  }
  else
  {
    leftForward();
    rightReverse();
  }

  const int spinSpeed = 160;  // tune if needed

  while (1)
  {
    motors(spinSpeed, spinSpeed);

    long avg = (absL() + absR()) / 2;
    if (avg >= TURN90_COUNTS) break;

    delay(10);
  }

  stopMotors();
  steeringServo.write(steeringCenter);
  delay(250);
}

// ---------------- ESP-NOW callbacks (ESP32 core v3 safe) ----------------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len == (int)sizeof(RemotePacket))
  {
    memcpy((void*)&rxPacket, data, sizeof(RemotePacket));
    gotPacket = true;
  }
}

#else

void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len)
{
  if (len == (int)sizeof(RemotePacket))
  {
    memcpy((void*)&rxPacket, data, sizeof(RemotePacket));
    gotPacket = true;
  }
}

#endif

void setup()
{
  Serial.begin(115200);
  delay(300);
  Serial.println("EEEBOT MAINBOARD START");

  // Motor PWM setup
  ledcAttachChannel(enA, freq, resolution, ledChannela);
  ledcAttachChannel(enB, freq, resolution, ledChannelb);

  pinMode(INa, OUTPUT);
  pinMode(INb, OUTPUT);
  pinMode(INc, OUTPUT);
  pinMode(INd, OUTPUT);

  stopMotors();

  // Servo setup (same approach as your working code)
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  steeringServo.setPeriodHertz(50);
  steeringServo.attach(servoPin, 500, 2400);
  steeringServo.write(steeringCenter);

  // Encoder setup
  ESP32Encoder::useInternalWeakPullResistors = puType::down;

  encoderL.attachHalfQuad(34, 35);
  encoderR.attachHalfQuad(36, 39);

  resetEncoders();

  // ESP-NOW init
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW init FAILED");
    while (1) { delay(100); }
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Add REMOTE as a peer so we can send status back
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, REMOTE_MAC, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx   = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer add FAILED");
    while (1) { delay(100); }
  }

  sendStatus(0, 0, 0); // IDLE
}

void loop()
{
  // Waiting for command list
  if (gotPacket)
  {
    gotPacket = false;

    if (rxPacket.msgType == 1 && rxPacket.count > 0 && rxPacket.count <= 15)
    {
      Serial.print("Received cmd list. Count=");
      Serial.println(rxPacket.count);

      execIndex = 0;
      executing = true;

      sendStatus(1, 0, rxPacket.count); // RECEIVED
      delay(100);
    }
  }

  // Execute commands sequentially after GO
  if (executing)
  {
    uint8_t total = rxPacket.count;

    sendStatus(2, execIndex, total); // EXECUTING

    Command c = rxPacket.cmd[execIndex];

    if (c.move == MOVE_FWD)
    {
      float metres = (float)c.qty * STEP_M;
      Serial.printf("CMD %d: FWD %d (%.2fm)\n", execIndex+1, c.qty, metres);
      doStraight(metres, true);
    }
    else if (c.move == MOVE_REV)
    {
      float metres = (float)c.qty * STEP_M;
      Serial.printf("CMD %d: REV %d (%.2fm)\n", execIndex+1, c.qty, metres);
      doStraight(metres, false);
    }
    else if (c.move == TURN_LT90)
    {
      Serial.printf("CMD %d: LEFT 90\n", execIndex+1);
      doTurn90(true);
    }
    else if (c.move == TURN_RT90)
    {
      Serial.printf("CMD %d: RIGHT 90\n", execIndex+1);
      doTurn90(false);
    }
    else
    {
      Serial.printf("CMD %d: Unknown\n", execIndex+1);
    }

    execIndex++;

    if (execIndex >= total)
    {
      executing = false;
      stopMotors();
      steeringServo.write(steeringCenter);

      sendStatus(3, total-1, total); // DONE
      delay(500);
      sendStatus(0, 0, 0); // back to IDLE
    }
    else
    {
      delay(150);
    }
  }

  delay(10);
}