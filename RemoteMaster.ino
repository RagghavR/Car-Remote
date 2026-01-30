#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <LiquidCrystal.h>
#include <Keypad.h>

// MAC address of the slave
uint8_t slaveAddress[] = { 0x78, 0x42, 0x1C, 0x2D, 0x15, 0x50 };

// Pin of LED
#define LED_PIN 25
#define MAX_STEPS 20

struct Instruction {
  int8_t direction;   // 1–4 (tiny, not int)
  int16_t value;      // distance (cm) or angle (deg)
};

struct RoutePacket {
  uint8_t count;
  Instruction steps[MAX_STEPS];
};

RoutePacket routePacket;

// State enum
enum InputState { WAIT_DIRECTION, WAIT_VALUE };
InputState state = WAIT_DIRECTION;
bool stateJustChanged = true;

// Direction
int currentDirection = -1;

// Route
int route[20][2];
int routeIndex = 0;

// ================= KEYPAD GLOBALS =================
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

byte rowPins[ROWS] = { 32, 33, 16, 26 };  // R1, R2, R3, R4
byte colPins[COLS] = { 27, 14, 13 };      // C1, C2, C3

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= LCD GLOBALS =================
String inputBuffer = "";
int cursorPos = 0;
bool firstKeyPress = true;

LiquidCrystal lcd(23, 22, 21, 19, 18, 17);  // Update with your pins

byte upArrow[8] = {
  B00100, B01110, B10101, B00100,
  B00100, B00100, B00100, B00100
};

byte downArrow[8] = {
  B00100, B00100, B00100, B00100,
  B00100, B10101, B01110, B00100
};

byte leftArrow[8] = {
  B00000, B00100, B01000, B11111,
  B01001, B00101, B00001, B00001
};

byte rightArrow[8] = {
  B00000, B00100, B00010, B11111,
  B10010, B10100, B10000, B10000
};

// ================= KEYPAD NUMBER INPUT =================
int getNumberFromKeypad() {
  char key = keypad.getKey();
  if (!key) return -1;

  // BACKSPACE
  if (key == '*') {
    if (inputBuffer.length() > 0) {
      inputBuffer.remove(inputBuffer.length() - 1);
      cursorPos--;
      lcd.setCursor(cursorPos, 1);
      lcd.print(" ");
      lcd.setCursor(cursorPos, 1);
    }
    return -1;
  }

  // ENTER
  if (key == '#') {
    if (inputBuffer.length() == 0) return -1;
    int number = inputBuffer.toInt();
    inputBuffer = "";
    cursorPos = 0;
    return number;
  }

  // DIGITS
  if (key >= '0' && key <= '9') {
    if (cursorPos >= 16) return -1;
    lcd.setCursor(cursorPos, 1);
    lcd.print(key);
    inputBuffer += key;
    cursorPos++;
  }

  return -1;
}

// ================= DIRECTION SELECT =================
int selectDirection() {
  static int selected = -1;
  char key = keypad.getKey();
  if (!key) return -1;

  switch (key) {
    case '2':
      lcd.clear();
      lcd.setCursor(7, 0);
      lcd.write(byte(0));
      selected = 1;
      break;

    case '6':
      lcd.clear();
      lcd.setCursor(9, 0);
      lcd.write(byte(1));
      selected = 2;
      break;

    case '8':
      lcd.clear();
      lcd.setCursor(7, 1);
      lcd.write(byte(2));
      selected = 3;
      break;

    case '4':
      lcd.clear();
      lcd.setCursor(5, 0);
      lcd.write(byte(3));
      selected = 4;
      break;

    case '5':
      lcd.clear();
      lcd.setCursor(6, 0);
      lcd.write("END");
      selected = 0;
      break;

    case '#':
      if (selected != -1) {
        int result = selected;
        selected = -1;
        lcd.clear();
        return result;
      }
      break;
  }

  return -1;
}

// ================= PRINT ROUTE =================
void printRoute() {
  for (int i = 0; i < routeIndex; i++) {
    Serial.print("Step ");
    Serial.print(i);
    Serial.print("\t Dir: ");

    if (route[i][0] == 1) Serial.print("Forwards\t");
    else if (route[i][0] == 2) Serial.print("Right\t");
    else if (route[i][0] == 3) Serial.print("Backwards\t");
    else if (route[i][0] == 4) Serial.print("Left\t");

    Serial.print(" Dist: ");
    Serial.println(route[i][1]);
  }
}

// ================= SEND ROUTE =================
void sendRoute() {
  routePacket.count = routeIndex;

  for (int i = 0; i < routeIndex; i++) {
    routePacket.steps[i].direction = route[i][0];
    routePacket.steps[i].value = route[i][1];
  }

  esp_err_t result = esp_now_send(
    slaveAddress,
    (uint8_t*)&routePacket,
    sizeof(RoutePacket)
  );

  if (result == ESP_OK) Serial.println("Route sent successfully");
  else Serial.println("Error sending route");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);


  Serial.print("Remote MAC: ");
  Serial.println(WiFi.macAddress());

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, slaveAddress, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  lcd.createChar(0, upArrow);
  lcd.createChar(1, rightArrow);
  lcd.createChar(2, downArrow);
  lcd.createChar(3, leftArrow);
}

// ================= LOOP =================
void loop() {

  if (routeIndex >= MAX_STEPS) return;

  // ================= DIRECTION STATE =================
  if (state == WAIT_DIRECTION) {

    if (stateJustChanged) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Direction:");
      stateJustChanged = false;
    }

    int dir = selectDirection();

    if (dir != -1) {

      if (dir == 0) {  // END
        Serial.println("ROUTE COMPLETE:");
        printRoute();
        sendRoute();
        while (1);  // stop program
      }

      currentDirection = dir;
      route[routeIndex][0] = dir;

      state = WAIT_VALUE;
      stateJustChanged = true;
    }
  }

  // ================= VALUE STATE =================
  else if (state == WAIT_VALUE) {

    if (stateJustChanged) {
      lcd.clear();
      lcd.setCursor(0, 0);

      if (currentDirection == 1 || currentDirection == 3)
        lcd.print("DISTANCE:");
      else
        lcd.print("ANGLE:");

      lcd.setCursor(0, 1);
      inputBuffer = "";
      cursorPos = 0;
      stateJustChanged = false;
    }

    int value = getNumberFromKeypad();

    if (value != -1 && inputBuffer.length() == 0) {  // confirmed with #
      route[routeIndex][1] = value;
      routeIndex++;

      state = WAIT_DIRECTION;
      stateJustChanged = true;
    }
  }
}
