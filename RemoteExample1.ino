#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <LiquidCrystal.h>
#include <Keypad.h>

// ---------------- LCD pins (your wiring) ----------------
// RS: 27, EN: 14, D4: 32, D5: 33, D6: 25, D7: 26
const int rs = 19, en = 23, d4 = 18, d5 = 17, d6 = 16, d7 = 15;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// ---------------- LED ----------------
const int statusLED = 13;   // OFF = idle, FLASH = busy

// ---------------- Keypad pins (your wiring) ----------------
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] =
{
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Your 7 keypad wires (R1..R4 then C1..C3)
byte rowPins[ROWS] = {27, 26, 25, 33};
byte colPins[COLS] = {14, 13, 4}; 

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ---------------- ESP-NOW ----------------
const uint8_t ESPNOW_CHANNEL = 1;

// CHANGE THIS to your EEEBot mainboard MAC:
uint8_t BOT_MAC[] = { 0x08, 0x3A, 0x8D, 0x0D, 0x83, 0x4C }; // <-- CHANGE

// ---------------- Command format ----------------
enum MoveType : uint8_t
{
  MOVE_NONE = 0,
  MOVE_FWD  = 1,   // qty = 1..9 (10cm steps)
  MOVE_REV  = 2,   // qty = 1..9 (10cm steps)
  TURN_LT90 = 3,   // qty ignored (fixed 90)
  TURN_RT90 = 4    // qty ignored (fixed 90)
};

typedef struct __attribute__((packed))
{
  uint8_t move;
  uint8_t qty;
} Command;

typedef struct __attribute__((packed))
{
  uint8_t msgType;     // 1 = COMMAND_LIST
  uint8_t count;       // number of commands
  Command cmd[15];     // exactly 15 max (per spec)
} RemotePacket;

typedef struct __attribute__((packed))
{
  uint8_t msgType;     // 2 = STATUS
  uint8_t state;       // 0=IDLE, 1=RECEIVED, 2=EXECUTING, 3=DONE
  uint8_t index;       // executing command index (0-based)
  uint8_t total;       // total commands
} BotStatus;

// ---------------- Storage ----------------
Command commandList[15];
int commandCount = 0;

bool waitingQty = false;
MoveType pendingMove = MOVE_NONE;

volatile BotStatus lastStatus;
volatile bool gotStatus = false;

unsigned long lastFlash = 0;
bool ledState = false;

// ---------- LCD helpers ----------
const char* MoveToText(uint8_t m)
{
  if (m == MOVE_FWD)  return "FD";
  if (m == MOVE_REV)  return "RV";
  if (m == TURN_LT90) return "L90";
  if (m == TURN_RT90) return "R90";
  return "--";
}

void LCDShowIdle()
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("2F 8B 4L 6R");
  lcd.setCursor(0,1);
  lcd.print("*Del  #Go");
}

void LCDShowList()
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Cmds ");
  lcd.print(commandCount);
  lcd.print("/15");

  lcd.setCursor(0,1);

  if (commandCount == 0)
  {
    lcd.print("None");
    return;
  }

  Command c = commandList[commandCount - 1];
  lcd.print(MoveToText(c.move));
  lcd.print(" ");
  lcd.print((int)c.qty);

  if (waitingQty)
  {
    lcd.print(" qty?");
  }
}

void LCDShowExecuting(uint8_t idx, uint8_t total)
{
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Executing");
  lcd.setCursor(0,1);
  lcd.print((int)(idx + 1));
  lcd.print("/");
  lcd.print((int)total);
}

void AddCommand(uint8_t mv, uint8_t qty)
{
  if (commandCount >= 15)
  {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Max 15 cmds");
    delay(900);
    LCDShowList();
    return;
  }

  commandList[commandCount].move = mv;
  commandList[commandCount].qty  = qty;
  commandCount++;

  LCDShowList();
}

void DeleteLast()
{
  if (commandCount > 0)
  {
    commandCount--;
  }
  waitingQty = false;
  pendingMove = MOVE_NONE;
  LCDShowList();
}

void ClearAll()
{
  commandCount = 0;
  waitingQty = false;
  pendingMove = MOVE_NONE;
  LCDShowIdle();
}

void SendCommandList()
{
  RemotePacket pkt;
  pkt.msgType = 1;
  pkt.count   = (uint8_t)commandCount;

  for (int i = 0; i < commandCount; i++)
  {
    pkt.cmd[i] = commandList[i];
  }

  esp_err_t r = esp_now_send(BOT_MAC, (uint8_t*)&pkt, sizeof(pkt));

  Serial.print("Send list: ");
  Serial.println(r == ESP_OK ? "OK" : "FAIL");
}

// ---------------- ESP-NOW callbacks (ESP32 core v3 safe) ----------------
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)

void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status)
{
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
  if (len == (int)sizeof(BotStatus))
  {
    memcpy((void*)&lastStatus, data, sizeof(BotStatus));
    gotStatus = true;
  }
}

#else

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  Serial.print("Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void OnDataRecv(const uint8_t *mac, const uint8_t *data, int len)
{
  if (len == (int)sizeof(BotStatus))
  {
    memcpy((void*)&lastStatus, data, sizeof(BotStatus));
    gotStatus = true;
  }
}

#endif

void setup()
{
  Serial.begin(115200);

  pinMode(statusLED, OUTPUT);
  digitalWrite(statusLED, LOW);

  lcd.begin(16, 2);
  LCDShowIdle();

  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK)
  {
    lcd.clear();
    lcd.print("ESP-NOW init");
    lcd.setCursor(0,1);
    lcd.print("FAILED");
    while (1) { delay(100); }
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, BOT_MAC, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx   = WIFI_IF_STA;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    lcd.clear();
    lcd.print("Peer add");
    lcd.setCursor(0,1);
    lcd.print("FAILED");
    while (1) { delay(100); }
  }

  Serial.println("REMOTE READY");
}

void loop()
{
  // --- status from bot ---
  if (gotStatus)
  {
    gotStatus = false;

    if (lastStatus.state == 0) // IDLE
    {
      digitalWrite(statusLED, LOW);
      ledState = false;
    }
    else if (lastStatus.state == 2) // EXECUTING
    {
      unsigned long now = millis();
      if (now - lastFlash > 200)
      {
        lastFlash = now;
        ledState = !ledState;
        digitalWrite(statusLED, ledState);
      }
      LCDShowExecuting(lastStatus.index, lastStatus.total);
    }
    else if (lastStatus.state == 3) // DONE
    {
      digitalWrite(statusLED, LOW);
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("DONE");
      lcd.setCursor(0,1);
      lcd.print("Ready");
      delay(1000);
      ClearAll();
    }
  }

  // --- keypad ---
  char k = keypad.getKey();
  if (!k) return;

  Serial.print("Key: ");
  Serial.println(k);

  // Delete last
  if (k == '*')
  {
    DeleteLast();
    return;
  }

  // GO
  if (k == '#')
  {
    if (commandCount == 0)
    {
      lcd.clear();
      lcd.print("No commands");
      delay(800);
      LCDShowIdle();
      return;
    }

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Sending...");
    lcd.setCursor(0,1);
    lcd.print(commandCount);
    lcd.print(" cmds");

    SendCommandList();
    return;
  }

  // If waiting for qty (only for 2 or 8)
  if (waitingQty)
  {
    if (k >= '1' && k <= '9')
    {
      uint8_t qty = (uint8_t)(k - '0');
      AddCommand((uint8_t)pendingMove, qty);
      waitingQty = false;
      pendingMove = MOVE_NONE;
    }
    return;
  }

  // Movement entry
  if (k == '2') { pendingMove = MOVE_FWD; waitingQty = true;  LCDShowList(); return; }
  if (k == '8') { pendingMove = MOVE_REV; waitingQty = true;  LCDShowList(); return; }

  // LEFT/RIGHT are instant 90 deg commands (no qty needed)
  if (k == '4') { AddCommand((uint8_t)TURN_LT90, 90); return; }
  if (k == '6') { AddCommand((uint8_t)TURN_RT90, 90); return; }

  // ignore other keys
}
