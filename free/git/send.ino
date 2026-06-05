#include <WiFiS3.h>
#include <WiFiUdp.h>

// --- Wi-Fiと通信の設定 ---
char ssid[] = "hackathon010-WPA2";
char pass[] = "hackathon010";
int status = WL_IDLE_STATUS;

WiFiUDP Udp;

// [FIX] 送信側のローカルポートと宛先ポートを分ける
unsigned int localPort = 9090; // 送信側が使うローカルポート（受信側と別にする）
unsigned int destPort  = 8080; // 受信側が listen しているポート

// [FIX] 接続後にIPとサブネットマスクから自動計算する
IPAddress broadcastIP;

// --- 使うピンの番号 ---
const int PIN_BPM_UP   = 2;
const int PIN_BPM_DOWN = 3;
const int PIN_START    = 4;

// --- 【追加】レーザ通信用のピン（conductor_v4.inoより） ---
const int laserPins[] = {A0, A1, A2, A3};
// --------------------------------------------------------

// --- BPMの上下限 ---
const float BPM_MIN = 30.0;
const float BPM_MAX = 240.0;

// --- 状態を覚えておくための変数 ---
float currentBPM = 120.0;
bool lastUpState    = HIGH;
bool lastDownState  = HIGH;
bool lastStartState = HIGH;

unsigned long previousTime = 0;
int currentBeat = 1;
int currentBar  = 1;
bool isPlaying  = false;

void setup() {
  Serial.begin(9600);

  pinMode(PIN_BPM_UP,   INPUT_PULLUP);
  pinMode(PIN_BPM_DOWN, INPUT_PULLUP);
  pinMode(PIN_START,    INPUT_PULLUP);

  // --- 【追加】レーザピンの初期化 ---
  for (int i = 0; i < 4; i++) {
    pinMode(laserPins[i], OUTPUT);
    digitalWrite(laserPins[i], LOW);
  }
  // ------------------------------

  // --- Wi-Fiへの接続処理 ---
  Serial.print("Connecting to: ");
  Serial.println(ssid);

  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
    delay(1000);
  }
  Serial.println("Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // [FIX] 接続後にブロードキャストIPを自動計算
  IPAddress ip     = WiFi.localIP();
  IPAddress subnet = WiFi.subnetMask();

  for (int i = 0; i < 4; i++) {
    broadcastIP[i] = (ip[i] & subnet[i]) | (~subnet[i] & 0xFF);
  }
  Serial.print("Broadcast IP: ");
  Serial.println(broadcastIP);

  // [FIX] 送信側は localPort (9090) でバインド → destPort (8080) に送る
  Udp.begin(localPort);

  // [FIX] 起動時に現在のBPMをブロードキャスト
  broadcastTempoData();
}

void loop() {
  checkButtons();
  countBeats();
}

// ==========================================
// 処理関数
// ==========================================

void checkButtons() {
  bool upState    = digitalRead(PIN_BPM_UP);
  bool downState  = digitalRead(PIN_BPM_DOWN);
  bool startState = digitalRead(PIN_START);

  bool isBpmChanged = false;

  // BPMアップ
  if (lastUpState == HIGH && upState == LOW) {
    currentBPM += 5.0;
    if (currentBPM > BPM_MAX) currentBPM = BPM_MAX;
    isBpmChanged = true;
  }

  // BPMダウン
  if (lastDownState == HIGH && downState == LOW) {
    currentBPM -= 5.0;
    if (currentBPM < BPM_MIN) currentBPM = BPM_MIN;
    isBpmChanged = true;
  }

  if (isBpmChanged) {
    Serial.print("BPM:");
    Serial.println(currentBPM);
    broadcastTempoData();
    delay(50);
  }

  // スタート/ストップ
  if (lastStartState == HIGH && startState == LOW) {
    if (!isPlaying) {
      
      // --- 【変更】チームのコードを参考に、レーザ発射(3ビット送信)を追加 ---
      Serial.println("Sending START laser command...");
      sendLaserCommand(0b001, -1); // 0b001(START)を全レーザに送信
      // -------------------------------------------------------------

      isPlaying = true;
      currentBeat = 1;
      currentBar  = 1;
      // ★レーザの送信が完全に終わった「今」をカウントの基準時間にする
      previousTime = millis();
      broadcastTempoData(); // スタート時にも現在BPMを送信
      Serial.println("START (Count Started)");
    } else {
      isPlaying = false;
      Serial.println("STOP");
    }
    delay(50);
  }

  lastUpState    = upState;
  lastDownState  = downState;
  lastStartState = startState;
}

// 楽器側ArduinoへUDPブロードキャストでBPMを送信
void broadcastTempoData() {
  Udp.beginPacket(broadcastIP, destPort);
  Udp.print("BPM:");
  Udp.print((int)currentBPM);
  Udp.endPacket();

  Serial.print("Broadcasted BPM:");
  Serial.println((int)currentBPM);
}

void countBeats() {
  if (!isPlaying) return;

  float beatTime = 60000.0 / currentBPM;
  unsigned long currentTime = millis();

  if (currentTime - previousTime >= (unsigned long)beatTime) {
    previousTime += (unsigned long)beatTime;

    Serial.print("PLAY:");
    Serial.print(currentBar);
    Serial.print(",");
    Serial.println(currentBeat);

    currentBeat++;
    if (currentBeat > 4) {
      currentBeat = 1;
      currentBar++;
    }
  }
}

// ==========================================
// --- 【追加】conductor_v4.ino のレーザ送信関数 ---
// ==========================================
void sendLaserCommand(byte command, int target) {
  // 開始合図：300ms連続点灯 → 50ms消灯
  setLaserState(target, HIGH);
  delay(300);
  setLaserState(target, LOW);
  delay(50);
  
  // 3ビットをMSBから順に送信（1=点灯50ms, 0=消灯50ms）
  for (int bit = 2; bit >= 0; bit--) {
    int bitValue = (command >> bit) & 0x01;
    if (bitValue == 1) {
      setLaserState(target, HIGH);
    } else {
      setLaserState(target, LOW);
    }
    delay(50);
    setLaserState(target, LOW);
    delay(50);
  }
}

void setLaserState(int target, int state) {
  if (target == -1) {
    for (int i = 0; i < 4; i++) digitalWrite(laserPins[i], state);
  } else {
    digitalWrite(laserPins[target], state);
  }
}