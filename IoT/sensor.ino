#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
 
LiquidCrystal_I2C lcd(0x27, 20, 4);
 
#define TRIG_DEPAN 14
#define ECHO_DEPAN 27
#define TRIG_KIRI  12
#define ECHO_KIRI  13
#define TRIG_KANAN 33
#define ECHO_KANAN 32
 
const char* ssid = "Balai Diklat 2025";
const char* password = "denivorasya";
const char* serverURL = "http://172.16.1.71:5500/distance";
const char* ubidotsToken = "BBUS-dUnnmdDGegd40VNGBKuCOnpvAbO9eJ"; 
const char* ubidotsDeviceLabel = "neocane-dashboard"; 
const char* ubidotsURL = "https://industrial.api.ubidots.com/api/v1.6/devices/"; 
 

uint8_t receiverAddress[] = {0xF8, 0xB3, 0xB7, 0x7B, 0xDD, 0xD8} ;
 
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;
 
typedef struct struct_message {
  float depan;
  float kiri;
  float kanan;
  bool bahaya; 
} struct_message;
 
struct_message dataSensor;
 
bool sensorAktif = true;
unsigned long lastControlCheck = 0;
const unsigned long controlCheckInterval = 3000;
 
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
 
void checkSensorControl() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(ubidotsURL) + ubidotsDeviceLabel + "/sensor_control/lv";
 
    Serial.println("[Control] Request to: " + url);
 
    http.setTimeout(2000);
    http.begin(url);
    http.addHeader("X-Auth-Token", ubidotsToken);
 
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
 
void sendToServer(float depan, float kiri, float kanan) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(2000); 
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");
 
 
    String jsonPayload = "{";
    jsonPayload += "\"front\": " + String(depan, 2) + ",";
    jsonPayload += "\"left\": " + String(kiri, 2) + ",";
    jsonPayload += "\"right\": " + String(kanan, 2);
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
 
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Terkirim" : "Gagal");
}

unsigned long berjalanStartTime = 0;
const unsigned long waktuPengingat = 45UL * 60UL * 1000UL;
bool sedangBerjalan = false;
 
void setup() {
  Serial.begin(115200);
 
  pinMode(TRIG_DEPAN, OUTPUT); pinMode(ECHO_DEPAN, INPUT);
  pinMode(TRIG_KIRI, OUTPUT);  pinMode(ECHO_KIRI, INPUT);
  pinMode(TRIG_KANAN, OUTPUT); pinMode(ECHO_KANAN, INPUT);
 
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("NeoCane 3 Sensor");
 
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
}
 
void sendToUbidots(float depan, float kiri, float kanan) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
 
    String fullURL = String(ubidotsURL) + ubidotsDeviceLabel;
    http.setTimeout(2000);
    http.begin(fullURL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Auth-Token", ubidotsToken);
 
    String jsonPayload = "{";
    jsonPayload += "\"jarak_tengah\": " + String(depan, 2) + ",";
    jsonPayload += "\"jarak_kiri\": " + String(kiri, 2) + ",";
    jsonPayload += "\"jarak_kanan\": " + String(kanan, 2);
    jsonPayload += "}";
 
    Serial.println("[Ubidots] Kirim: " + jsonPayload);
 
    int httpResponseCode = http.POST(jsonPayload);
 
    if (httpResponseCode > 0) {
      Serial.println("[Ubidots] OK: " + String(httpResponseCode));
    } else {
      Serial.println("[Ubidots] Gagal, Error: " + String(httpResponseCode));
    }
 
    http.end();
  } else {
    Serial.println("[Ubidots] WiFi belum nyambung");
  }
}
 
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 500;
 
unsigned long lastUbidotsSend = 0;
unsigned long ubidotsInterval = 5000;
 
float depan = -1, kiri = -1, kanan = -1;
 
void loop() {
  unsigned long now = millis();
 
  if (now - lastControlCheck >= controlCheckInterval) {
    checkSensorControl();
    lastControlCheck = now;
  }
 
  if (sensorAktif) {
    if (now - lastSensorRead >= sensorInterval) {
      depan = readDistance(TRIG_DEPAN, ECHO_DEPAN);
      delay(30);
      kiri  = readDistance(TRIG_KIRI, ECHO_KIRI);
      delay(30);
      kanan = readDistance(TRIG_KANAN, ECHO_KANAN);
 
      Serial.print("Depan: "); Serial.print(depan);
      Serial.print(" | Kiri: "); Serial.print(kiri);
      Serial.print(" | Kanan: "); Serial.println(kanan);
 
      // Pengecekan jarak depan, kiri, dan kanan untuk mengaktifkan bahaya jika < 100 cm
      if ((depan <= 100 && depan > 0) || (kiri <= 100 && kiri > 0) || (kanan <= 100 && kanan > 0)) {
        dataSensor.bahaya = true; 
      } else {
        dataSensor.bahaya = false;
      }
 
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
 
      lastSensorRead = now;
    }
 
    if (now - lastUbidotsSend >= ubidotsInterval) {
      sendToServer(depan, kiri, kanan);
      sendToUbidots(depan, kiri, kanan);
      lastUbidotsSend = now;
    }
 
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor: NON-AKTIF");
  }
}