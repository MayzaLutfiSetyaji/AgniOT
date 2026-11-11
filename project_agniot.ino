#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Kredensial Wifi dan MQTT
const char* ssid = "AgniOT_wifi";
const char* password = "TOingA223";
const char* mqtt_server = "broker.hivemq.com";

WiFiClient espClient;
PubSubClient client(espClient);

//Pin definition
const int flameSensorPin = D1;

const int ledMerah = D2;
const int ledHijau = D5;
const int ledKuning = D6;

const int buzzer = D7;

// Api harus terus terdeteksi selama 2 detik
const unsigned long waktuApi = 2000;
unsigned long waktuMulaiApi = 0;
bool apiSedangTerdeteksi = false;

//Timer untuk publish MQTT periodik
unsigned long lastMsgTime = 0;
const long intervalPublish = 3000;  //Kirim data setiap 3 detik

//Fungsi koneksi wifi
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.println("Menghubungkan ke ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi Terhubung");
  Serial.println("Alamat IP: ");
  Serial.println(WiFi.localIP());
}

//Fungsi callback (menerima perintah subscribe)
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Pesan diterima pada topik [");
  Serial.print(topic);
  Serial.print("] ");

  String messageTemp = "";
  for (unsigned int i = 0; i < length; i++) {
    messageTemp += (char)payload[i];
  }
  Serial.println(messageTemp);
  
  //Menangani perintah control relay/pompa dari Web Admin 
  if (String(topic) == "agniot/kelompok2/control") {
    if (messageTemp == "PUMP_ON") {
      digitalWrite(ledMerah, HIGH); //Simulasi aktuator pompa on
      Serial.println("Perintah diterima: MENYALAKAN POMPA");
    } else if (messageTemp == "PUMP_OFF") {
      digitalWrite(ledMerah, LOW); //Simulasi aktuator pompa off
      Serial.println("Perintah diterima: MEMATIKAN  POMPA");
    }
  }
}

//Fungsi Reconnect MQTT
void reconnect() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke MQTT Client...");
    //ID client
    String clientId = "AgniOTClient-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("Client berhasil terhubung");
      //Subs ke topic control dari backend web
      client.subscribe("agniot/kelompok2/control");
    } else {
      Serial.print("Client gagal terhubung, rc=");
      Serial.print(client.state());
      Serial.println("Coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(flameSensorPin, INPUT);

  pinMode(ledMerah, OUTPUT);
  pinMode(ledHijau, OUTPUT);
  pinMode(ledKuning, OUTPUT);

  pinMode(buzzer, OUTPUT);

  // Kondisi awal
  digitalWrite(ledHijau, HIGH);
  digitalWrite(ledKuning, LOW);
  digitalWrite(ledMerah, LOW);

  // Buzzer aktif LOW, jadi HIGH = mati
  digitalWrite(buzzer, HIGH);

  //Inisialisasi wifi dan mqtt
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  //Memastikan koneksi mqtt terjaga
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  int flame = digitalRead(flameSensorPin);

  // =================================
  // API TERDETEKSI
  // =================================
  if (flame == LOW) {

    // Baru pertama kali mendeteksi api
    if (!apiSedangTerdeteksi) {

      apiSedangTerdeteksi = true;

      waktuMulaiApi = millis();

      Serial.println("Api mulai terdeteksi!");

      // LED kuning sebagai peringatan
      digitalWrite(ledHijau, LOW);
      digitalWrite(ledKuning, HIGH);
      digitalWrite(ledMerah, LOW);

      // Buzzer aktif LOW
      digitalWrite(buzzer, LOW);
    }

    // Jika api terus terdeteksi selama 2 detik
    if (millis() - waktuMulaiApi >= waktuApi) {

      digitalWrite(ledKuning, LOW);
      digitalWrite(ledMerah, HIGH);

      Serial.println("Api terus terdeteksi! LED MERAH ON");
    }
  }

  // =================================
  // API TIDAK TERDETEKSI
  // =================================
  else {

    apiSedangTerdeteksi = false;

    digitalWrite(ledHijau, HIGH);
    digitalWrite(ledKuning, LOW);
    digitalWrite(ledMerah, LOW);

    // Buzzer aktif LOW → HIGH = mati
    digitalWrite(buzzer, HIGH);

    Serial.println("Tidak ada api.");
  }

  //Publish data telemetri ke HiveMQ
  unsigned long now = millis();
  if (now - lastMsgTime > intervalPublish) {
    lastMsgTime = now;

    //Format payload JSON (belum termasuk data DHT dan MQ-2)
    String aiStatus = (digitalRead(ledMerah) == HIGH) ? "BERBAHAYA" : "NORMAL";
    String payload = "{\"flame\":" + String(flame) + ", \"ai_status\":\"" + aiStatus + "\"}";

    Serial.print("Publish MQTT ->");
    Serial.println(payload);
    client.publish("agniot/kelompok2/telemetry", payload.c_str());
  }

  delay(50);
}