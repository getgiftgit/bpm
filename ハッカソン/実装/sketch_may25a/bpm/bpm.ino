#include <WiFiS3.h>
#include <WiFiUdp.h>

// --- Wi-Fiと通信の設定 ---
char ssid[] = "YOUR_ROUTER_SSID"; // 使用するルータのSSIDを入力
char pass[] = "YOUR_ROUTER_PASS"; // 使用するルータのパスワードを入力
int status = WL_IDLE_STATUS;

WiFiUDP Udp;
unsigned int localPort = 2390; // 通信に使うポート番号
// サブネットが192.168.1.Xの場合のブロードキャスト（全員宛て）アドレス
IPAddress broadcastIP(192, 168, 1, 255); 

// --- 使うピンの番号 ---
const int PIN_BPM_UP = 2;
const int PIN_BPM_DOWN = 3;
const int PIN_START = 4;

// --- 状態を覚えておくための変数 ---
float currentBPM = 120.0;
bool lastUpState = HIGH;
bool lastDownState = HIGH;
bool lastStartState = HIGH;

unsigned long previousTime = 0;
int currentBeat = 1;
int currentBar = 1;
bool isPlaying = false;

void setup() {
  Serial.begin(9600);
  
  pinMode(PIN_BPM_UP, INPUT_PULLUP);
  pinMode(PIN_BPM_DOWN, INPUT_PULLUP);
  pinMode(PIN_START, INPUT_PULLUP);

  // --- Wi-Fiへの接続処理 ---
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
    delay(1000); // 接続できるまで1秒ごとにリトライ
  }
  Serial.println("Connected to Wi-Fi");
  
  // UDP通信の開始
  Udp.begin(localPort);
}

void loop() {
  checkButtons();
  countBeats();
}

// ==========================================
// 処理関数
// ==========================================

void checkButtons() {
  bool upState = digitalRead(PIN_BPM_UP);
  bool downState = digitalRead(PIN_BPM_DOWN);
  bool startState = digitalRead(PIN_START);

  bool isBpmChanged = false;

  // BPMアップ
  if (lastUpState == HIGH && upState == LOW) {
    currentBPM += 5.0;
    isBpmChanged = true;
  }

  // BPMダウン
  if (lastDownState == HIGH && downState == LOW) {
    currentBPM -= 5.0;
    if (currentBPM < 30.0) currentBPM = 30.0;
    isBpmChanged = true;
  }

  // ★BPMが変わったら、楽器側ArduinoへWi-Fiで一斉送信する★
  if (isBpmChanged) {
    // 自分のPC(Processing)への通知
    Serial.print("BPM:");
    Serial.println(currentBPM); 
    
    // 楽器側ArduinoへのWi-Fi通知
    broadcastTempoData(); 
    
    delay(50);
  }

  // スタート/ストップ
  if (lastStartState == HIGH && startState == LOW) {
    if (isPlaying == false) {
      isPlaying = true;
      currentBeat = 1;
      currentBar = 1;
      previousTime = millis();
      Serial.println("START");
    } else {
      isPlaying = false; 
      Serial.println("STOP");
    }
    delay(50);
  }

  lastUpState = upState;
  lastDownState = downState;
  lastStartState = startState;
}

// 楽器側ArduinoへUDPブロードキャストでBPMを送信する関数 (設計書 3.3.8.1)
void broadcastTempoData() {
  // broadcastIP宛てに送ることで、ネットワーク内の全Arduinoが同時に受信できる
  Udp.beginPacket(broadcastIP, localPort);
  
  // 送るデータを作成 (例: "BPM:125.0")
  Udp.print("BPM:");
  Udp.print(currentBPM);
  
  Udp.endPacket();
  
  Serial.println("BPM Broadcasted via Wi-Fi");
}

void countBeats() {
  if (isPlaying == false) return;

  float beatTime = 60000.0 / currentBPM;
  unsigned long currentTime = millis();

  if (currentTime - previousTime >= (unsigned long)beatTime) {
    previousTime += (unsigned long)beatTime; 

    // 自PC(スピーカー役)へ鳴らす合図
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