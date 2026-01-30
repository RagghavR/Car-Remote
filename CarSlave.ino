#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Encoder.h>
#include <ESP32Servo.h>

Servo steeringServo;
ESP32Encoder encoder1;
ESP32Encoder encoder2;

int16_t LeftCount = 0;
int16_t RightCount = 0;

int servoPin = 13;

const int freq = 2000;
const int ledChannela = 11;
const int ledChannelb = 12;
const int resolution = 8;

#define enA 33
#define enB 25
#define INa 26
#define INb 27
#define INc 14
#define INd 12
#define Vout 32
#define X 16
#define Y 17
#define Z 4

int displayNumber = -1;

uint8_t masterAddress[] = { 0x08, 0x3A, 0x8D, 0x0D, 0x91, 0x98 };

void runMotors(int leftMotor_speed, int rightMotor_speed) {
  leftMotor_speed = constrain(leftMotor_speed, -255, 255);
  rightMotor_speed = constrain(rightMotor_speed, -255, 255);

  ledcWrite(enA, abs(leftMotor_speed));
  ledcWrite(enB, abs(rightMotor_speed));

  if (leftMotor_speed < 0) {
    digitalWrite(INa, LOW);
    digitalWrite(INb, HIGH);
  } else {
    digitalWrite(INa, HIGH);
    digitalWrite(INb, LOW);
  }

  if (rightMotor_speed < 0) {
    digitalWrite(INc, LOW);
    digitalWrite(INd, HIGH);
  } else {
    digitalWrite(INc, HIGH);
    digitalWrite(INd, LOW);
  }
}

void batteryDisplay() {
  int batteryVoltageADC = analogRead(Vout);
  batteryVoltageADC = constrain(batteryVoltageADC, 0, 3723);

  if (batteryVoltageADC <= 2615) {
    displayNumber = 0;
  } else if (batteryVoltageADC <= 2836) {
    displayNumber = 1;
  } else if (batteryVoltageADC <= 3058) {
    displayNumber = 2;
  } else if (batteryVoltageADC <= 3280) {
    displayNumber = 3;
  } else if (batteryVoltageADC <= 3501) {
    displayNumber = 4;
  } else {
    displayNumber = 5;
  }

  digitalWrite(X, (displayNumber >> 2) & 1);
  digitalWrite(Y, (displayNumber >> 1) & 1);
  digitalWrite(Z, (displayNumber >> 0) & 1);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  ESP32Encoder::useInternalWeakPullResistors = puType::up;

  encoder1.attachHalfQuad(34, 35);
  encoder2.attachHalfQuad(36, 39);

  encoder1.setCount(0);
  encoder2.setCount(0);

  ledcAttachChannel(enA, freq, resolution, ledChannela);
  ledcAttachChannel(enB, freq, resolution, ledChannelb);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  steeringServo.setPeriodHertz(50);
  steeringServo.attach(servoPin, 500, 2400);

  pinMode(INa, OUTPUT);
  pinMode(INb, OUTPUT);
  pinMode(INc, OUTPUT);
  pinMode(INd, OUTPUT);
  pinMode(Vout, INPUT);
  pinMode(X, OUTPUT);
  pinMode(Y, OUTPUT);
  pinMode(Z, OUTPUT);

  steeringServo.write(90);
}

void loop() {
  RightCount = -encoder1.getCount();
  LeftCount = encoder2.getCount();

  batteryDisplay();

  Serial.print("Left Motor: ");
  Serial.print(LeftCount);
  Serial.print("\tRight Motor: ");
  Serial.println(RightCount);
}
