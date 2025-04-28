#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include "esp_camera.h"
#include "base64.h"
#include <esp_now.h>
#include <esp_wifi.h> 

// =========================
// WiFi & Server Config
// =========================
const char* ssid = "Balai Diklat 2025";
const char* password = "denivorasya";
const char* serverURL = "http://192.168.234.11:5500/predict";
const char* serverSave = "http://192.168.234.11:5500/save-photo";

// =========================
// ESP-NOW Receiver (Gelang) MAC Address
// =========================
uint8_t receiverMAC[] = {0xF8, 0xB3, 0xB7, 0x7B, 0xDD, 0xD8};

// ==========================
// Kamera AI Thinker
// ==========================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QQVGA;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Kamera gagal inisialisasi: 0x%x\n", err);
  } else {
    Serial.println("✅ Kamera berhasil diinisialisasi!");
  }
}

void initESPNOW() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW init gagal");
    return;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Gagal menambahkan peer");
    return;
  }
  Serial.println("✅ ESP-NOW siap kirim sinyal ke gelang!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Inisialisasi kamera...");
  initCamera();
  delay(300);

  Serial.println("Menghubungkan ke WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n📶 WiFi terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.print("📡 ESP32-CAM pakai WiFi channel: ");
  Serial.println(WiFi.channel());

  int wifi_channel = WiFi.channel();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(wifi_channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  initESPNOW();
}

void loop(){
  if (WiFi.status() == WL_CONNECTED) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("❌ Gagal mengambil gambar");
      delay(1000);
      return;
    }
    Serial.println("✅ Gambar berhasil diambil!");

    String image_base64 = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);

    // === Kirim ke /predict
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");
    String payload = "{\"image\":\"" + image_base64 + "\"}";
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("✅ Response dari /predict: ");
      Serial.println(response);

      // ✅ Kirim "rusak" via ESP-NOW kalau jalan rusak
      if (response.indexOf("jalan rusak") >= 0) {
        Serial.println("🚨 Kirim sinyal 'rusak' ke gelang via ESP-NOW");
        const char *pesan = "rusak";
        esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)pesan, strlen(pesan));
        if (result == ESP_OK) {
          Serial.println("✅ Sinyal 'rusak' berhasil dikirim!");
        } else {
          Serial.println("❌ Gagal kirim sinyal 'rusak'");
        }
      }

    } else {
      Serial.print("❌ HTTP /predict error: ");
      Serial.println(httpResponseCode);
    }
    http.end();

    // ====================
    // Kirim ke /save-photo
    // ====================
    HTTPClient http_photo;
    http_photo.begin(serverSave);
    http_photo.addHeader("Content-Type", "application/json");

    String payload_save = "{\"image\":\"" + image_base64 + "\"}";
    Serial.println("Mengirim foto ke /save-photo...");
    int photoResponseCode = http_photo.POST(payload_save);

    if (photoResponseCode > 0) {
      Serial.println("✅ Foto berhasil dikirim ke /save-photo");
    } else {
      Serial.print("❌ Gagal kirim foto: ");
      Serial.println(photoResponseCode);
    }

    http_photo.end();

    delay(5000);
  }
   
}
