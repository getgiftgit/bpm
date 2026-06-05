// 楽器側（受信側）Arduino専用プログラム
// ※指揮者側とは別のArduino Uno R4 WIFIに書き込んでください

#include <WiFiS3.h>
#include <WiFiUdp.h>

// --- Wi-Fiと通信の設定 ---
char ssid[] = "hackathon010-WPA2";
char pass[] = "hackathon010";
int status = WL_IDLE_STATUS;

WiFiUDP Udp;

// 送信側の destPort と必ず一致させること
unsigned int localPort = 8080; 

// --- レーザ受信(フォトトランジスタ)の設定 ---
const int PIN_LASER = A0;
int laserThreshold = 600; // ※環境に合わせて調整してください

// --- 状態を覚えておくための変数 ---
float currentBPM = 120.0;
bool isBuffering = false;
bool isPlaying   = false;
int bufferCount  = 0;

unsigned long previousTime = 0;
int currentBeat = 1;
int currentBar  = 1;

void setup() {
  Serial.begin(9600);
  pinMode(PIN_LASER, INPUT);

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

  // 受信側は localPort (8080) でバインドして待ち受け
  Udp.begin(localPort);
  Serial.print("Listening on port: ");
  Serial.println(localPort);
}

void loop() {
  receiveBPM();    // 1. 指揮者からWi-FiでBPMを受信する
  checkLaser();    // 2. 指揮者からのレーザ(スタート合図)を監視する
  countBeats();    // 3. 拍のカウントとPCへの送信を行う
}

// ==========================================
// 処理関数
// ==========================================

// 1. Wi-Fi経由でBPMを受信する処理
void receiveBPM() {
  int packetSize = Udp.parsePacket();
  if (packetSize == 0) return; // パケットがなければ即リターン

  char packetBuffer[64] = {0};
  int len = Udp.read(packetBuffer, sizeof(packetBuffer) - 1);
  if (len <= 0) return;
  packetBuffer[len] = '\0';

  // "BPM:XXX" 形式のパース
  if (strncmp(packetBuffer, "BPM:", 4) == 0) {
    int bpmValue = atoi(packetBuffer + 4);
    
    if (bpmValue >= 30 && bpmValue <= 240) {
      currentBPM = (float)bpmValue;
      Serial.print("BPM UPDATED: ");
      Serial.println(currentBPM);

      // --- 【通信遅延計測用】送信側へ「受け取ったよ(ACK)」と即座に返信する ---
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      Udp.print("ACK");
      Udp.endPacket();
      // -------------------------------------------------------------

    } else {
      Serial.print("Invalid BPM value: ");
      Serial.println(bpmValue);
    }
  }
}

// 2. レーザのスタート合図を受信する処理（vi_cla_v4.inoのロジックを統合）
void checkLaser() {
  // ※演奏中の音量変更などは別途追加するため、現在はスタート待機時のみ処理します
  if (isPlaying || isBuffering) return;

  // 光を検知したらビット解析を開始
  if (readLaserState() == 1) {
    unsigned long startTime = millis();
    while (readLaserState() == 1) {
      if (millis() - startTime > 1000) return; // タイムアウト
    }
    unsigned long duration = millis() - startTime;
    
    // 300msの開始合図（250〜350ms）を検出したらビットを読み取る
    if (duration >= 250 && duration <= 350) {
      byte cmd = parseBitStream();
      
      // 読み取ったコマンドが 0b001 (START) ならバッファカウント開始
      if (cmd == 0b001) {
        isBuffering  = true;
        bufferCount  = 0;
        
        // ★レーザの受信が完全に終わった「今」をカウントの基準時間にする
        previousTime = millis();
        Serial.println("LASER DETECTED (START)! Starting 16-beat buffer...");
      }
    }
  }
}

// --- 【追加】vi_cla_v4.ino のレーザ読み取り関数 ---
int readLaserState() {
  int val = analogRead(PIN_LASER);
  return (val > laserThreshold) ? 1 : 0; 
}

// --- 【追加】vi_cla_v4.ino の多数決ビット判定関数 ---
byte parseBitStream() {
  byte command = 0;
  delay(75); // 75ms待機でビット点灯期間の中心をサンプリング

  for (int i = 2; i >= 0; i--) {
    int votes = 0;
    votes += readLaserState(); delay(5);
    votes += readLaserState(); delay(5);
    votes += readLaserState();
    
    int bitValue = (votes >= 2) ? 1 : 0;
    command |= (bitValue << i);
    delay(90);
  }
  return command;
}

// 3. 拍・小節のカウントとPCへの合図送信
void countBeats() {
  if (!isBuffering && !isPlaying) return;
  
  float beatTime = 60000.0 / currentBPM;
  unsigned long currentTime = millis();

  if (currentTime - previousTime >= (unsigned long)beatTime) {
    previousTime += (unsigned long)beatTime;

    // --- 【準備中】16拍のバッファリング ---
    if (isBuffering) {
      bufferCount++;
      Serial.print("Buffering... ");
      Serial.println(bufferCount);
      
      if (bufferCount >= 16) {
        isBuffering = false;
        isPlaying   = true;
        currentBeat = 1;
        currentBar  = 1;
        Serial.println("BUFFER COMPLETE. START PLAYING!");
      }
    } 
    // --- 【本番演奏中】拍カウントの出力 ---
    else if (isPlaying) {
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
}