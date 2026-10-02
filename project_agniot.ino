const int flameSensorPin = D1;

const int ledMerah = D2;
const int ledHijau = D5;
const int ledKuning = D6;

const int buzzer = D7;

// Api harus terus terdeteksi selama 2 detik
const unsigned long waktuApi = 2000;

unsigned long waktuMulaiApi = 0;

bool apiSedangTerdeteksi = false;

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
}

void loop() {

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

  delay(50);
}