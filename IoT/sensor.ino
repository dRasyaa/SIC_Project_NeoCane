#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGPS++.h>

// LCD
LiquidCrystal_I2C lcd(0x27, 20, 4);

// Pin Ultrasonik
#define TRIG_DEPAN 14
#define ECHO_DEPAN 27
#define TRIG_KIRI  12
#define ECHO_KIRI  13
#define TRIG_KANAN 33
#define ECHO_KANAN 32

// WiFi dan Server
const char* ssid = "Balai Diklat 2025";
const char* password = "denivorasya";
const char* serverURL = "http://172.16.1.71:5500/distance"; // Server Flask URL
const char* openCageApiKey = "ISI_API_KEY_OPENCAGE_KAMU"; // <-- Ganti API key kamu
String lastLocationName = "Unknown";

// ESP-NOW
uint8_t receiverAddress[] = {0xF8, 0xB3, 0xB7, 0x7B, 0xDD, 0xD8};

// Struktur Data untuk ESP-NOW
typedef struct struct_message {
  float depan;
  float kiri;
  float kanan;
  bool bahaya; 
} struct_message;

struct_message dataSensor;

// Control Flag
bool sensorAktif = true;

// Waktu
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;
unsigned long lastControlCheck = 0;
const unsigned long controlCheckInterval = 2000;
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 1000;

// Jarak Sensor
float depan = -1, kiri = -1, kanan = -1;

// Untuk Tracking Waktu Jalan
bool sedangBerjalan = false;
unsigned long mulaiJalanMillis = 0;
unsigned long waktuBerjalan = 0; // dalam ms
unsigned long waktuBerhentiMillis = 0;
double lastLat = 0.0;
double lastLon = 0.0;
bool timerPaused = false;

// GPS
TinyGPSPlus gps;
HardwareSerial SerialGPS(1); // Gunakan UART1 (GPIO16 RX, GPIO17 TX)
unsigned long lastGPSCheck = 0;
bool gpsConnected = false;

// -------------------------
// Fungsi Membaca Jarak
float readDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 10000);
  if (duration == 0) {
    Serial.println("pulseIn timeout di echo pin: " + String(echoPin));
    return -1;
  }

  float distance = duration * 0.034 / 2;
  if (distance < 2 || distance > 400) {
    return -1;
  }
  return distance;
}

// -------------------------
// Fungsi Cek Control dari Ubidots
void checkSensorControl() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "http://172.16.1.71:5500/sensor_control"; // Flask control endpoint

    Serial.println("[Control] Request to: " + url);

    http.setTimeout(2000);
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("[Control] Payload: " + payload);
      float kontrol = payload.toFloat();
      sensorAktif = kontrol == 1.0;
      Serial.println("[Control] sensorAktif: " + String(sensorAktif));
    } else {
      Serial.println("[Control] Error HTTP: " + String(httpCode));
    }

    http.end();
  } else {
    Serial.println("[Control] WiFi NOT connected");
  }
}

// -------------------------
// Fungsi Kirim ke Server Lokal (Flask)
void sendToServer(float depan, float kiri, float kanan, double latitude, double longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(2000);
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    // Mengirim data sensor + GPS
    String jsonPayload = "{";
    jsonPayload += "\"front\": " + String(depan, 2) + ",";
    jsonPayload += "\"left\": " + String(kiri, 2) + ",";
    jsonPayload += "\"right\": " + String(kanan, 2) + ",";
    jsonPayload += "\"latitude\": " + String(latitude, 6) + ",";
    jsonPayload += "\"longitude\": " + String(longitude, 6);
    jsonPayload += "}";

    int httpResponseCode = http.POST(jsonPayload);
    if (httpResponseCode > 0) {
      Serial.println("Data sent to server: " + jsonPayload);
    } else {
      Serial.println("Send failed. Error: " + String(httpResponseCode));
    }
    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

// -------------------------
// Callback ESP-NOW
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Terkirim" : "Gagal");
}

// -------------------------
// Setup
void setup() {
  Serial.begin(115200);

  // Pin Setup
  pinMode(TRIG_DEPAN, OUTPUT); pinMode(ECHO_DEPAN, INPUT);
  pinMode(TRIG_KIRI, OUTPUT);  pinMode(ECHO_KIRI, INPUT);
  pinMode(TRIG_KANAN, OUTPUT); pinMode(ECHO_KANAN, INPUT);

  // LCD Setup
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("NeoCane 3 Sensor");

  // WiFi Setup
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 1);
  lcd.print("WiFi: Connecting...");
  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.setCursor(0, 1);
  lcd.print("WiFi: Connected   ");
  Serial.println("\nWiFi connected.");
  delay(2000);
  lcd.clear();

  // ESP-NOW Setup
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init gagal");
    return;
  }

  esp_now_register_send_cb(onSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Gagal tambah peer ESP-NOW");
    return;
  }

  // GPS Setup
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17); // RX=16, TX=17
  Serial.println("Menunggu koneksi GPS...");
}

void updateLocationName(double latitude, double longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.opencagedata.com/geocode/v1/json?q=" 
                 + String(latitude, 6) + "+" + String(longitude, 6) 
                 + "&key=" + openCageApiKey + "&no_annotations=1&language=id";

    Serial.println("[OpenCage] Request: " + url);

    http.setTimeout(5000);
    http.begin(url);

    int httpCode = http.GET();
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.println("[OpenCage] Response: " + payload);

      int formattedStart = payload.indexOf("\"formatted\":");
      if (formattedStart != -1) {
        int quoteStart = payload.indexOf("\"", formattedStart + 12);
        int quoteEnd = payload.indexOf("\"", quoteStart + 1);
        if (quoteStart != -1 && quoteEnd != -1) {
          lastLocationName = payload.substring(quoteStart + 1, quoteEnd);
          Serial.println("[OpenCage] Lokasi: " + lastLocationName);
        }
      } else {
        Serial.println("[OpenCage] Tidak menemukan lokasi.");
      }
    } else {
      Serial.println("[OpenCage] Error HTTP: " + String(httpCode));
    }
    http.end();
  } else {
    Serial.println("[OpenCage] WiFi belum nyambung");
  }
}

double haversine(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000; // Radius bumi dalam meter
  double dLat = radians(lat2 - lat1);
  double dLon = radians(lon2 - lon1);
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(radians(lat1)) * cos(radians(lat2)) *
             sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c;
}

// -------------------------
// Loop
void loop() {
  unsigned long now = millis();

  // Baca GPS terus
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }

  // Cek data GPS setiap 5 detik
  if (gps.satellites.isValid() && gps.satellites.value() > 0) {
    if (!gpsConnected) {
      Serial.println("GPS terkoneksi! 🚀");
      gpsConnected = true;
    }

    Serial.println("=== Data GPS ===");
    Serial.print("Latitude: "); Serial.println(gps.location.lat(), 6);
    Serial.print("Longitude: "); Serial.println(gps.location.lng(), 6);
    Serial.print("Altitude: "); Serial.print(gps.altitude.meters()); Serial.println(" meter");
    Serial.print("Jumlah Satelit: "); Serial.println(gps.satellites.value());
    Serial.println("================");

    updateLocationName(gps.location.lat(), gps.location.lng());

    // Kirim data sensor + GPS ke server
    sendToServer(depan, kiri, kanan, gps.location.lat(), gps.location.lng());

  } else {
    if (gpsConnected) {
      Serial.println("GPS kehilangan sinyal...");
      gpsConnected = false;
    } else {
      Serial.println("Mencari sinyal GPS...");
    }
  }

  // Cek kontrol sensor dari Flask
  if (now - lastControlCheck >= controlCheckInterval) {
    checkSensorControl();
    lastControlCheck = now;
  }

  // Jika sensor aktif
  if (sensorAktif) {
    if (now - lastSensorRead >= sensorInterval) {
      depan = readDistance(TRIG_DEPAN, ECHO_DEPAN);
      delay(30);
      kiri = readDistance(TRIG_KIRI, ECHO_KIRI);
      delay(30);
      kanan = readDistance(TRIG_KANAN, ECHO_KANAN);

      Serial.print("Depan: "); Serial.print(depan);
      Serial.print(" | Kiri: "); Serial.print(kiri);
      Serial.print(" | Kanan: "); Serial.println(kanan);

      dataSensor.bahaya = ( (depan <= 100 && depan > 0) || (kiri <= 100 && kiri > 0) || (kanan <= 100 && kanan > 0) );

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("D:");
      lcd.print(depan > 0 ? String(depan, 1) + "cm" : "Err");
      lcd.print(" K:");
      lcd.print(kiri > 0 ? String(kiri, 1) + "cm" : "Err");

      lcd.setCursor(0, 1);
      lcd.print("Ka:");
      lcd.print(kanan > 0 ? String(kanan, 1) + "cm" : "Err");

      dataSensor.depan = depan;
      dataSensor.kiri = kiri;
      dataSensor.kanan = kanan;

      esp_now_send(receiverAddress, (uint8_t *)&dataSensor, sizeof(dataSensor));

      sendToServer(depan, kiri, kanan);

      lastSensorRead = now;
    }
  }

  // Jika terlalu lama diam, pause timer
  if (sedangBerjalan && !timerPaused) {
    if (now - mulaiJalanMillis >= 5000) {
      timerPaused = true;
      waktuBerjalan = now - mulaiJalanMillis;
      Serial.print("Timer jalan: "); Serial.println(waktuBerjalan);
    }
  }
}
