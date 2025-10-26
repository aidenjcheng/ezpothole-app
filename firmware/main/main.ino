#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

String serverName = "awesomesauce10-pothole.hf.space";
String serverPath = "/upload";
String authToken = HF_TOKEN;
String sessionId = "CAR001";

const int serverPort = 443;

WiFiClientSecure client;

#define SERVICE_UUID        "00000001-710e-4a5b-8d75-3e5b444b3c3f"
#define CHAR_NOTIFY_UUID    "00000002-710e-4a5b-8d75-3e5b444b3c3f"
#define CHAR_WRITE_UUID     "00000003-710e-4a5b-8d75-3e5b444b3c3f"
#define CHAR_CONTROL_UUID   "00000004-710e-4a5b-8d75-3e5b444b3c3f"

BLEServer* pServer = NULL;
BLECharacteristic* pCharNotify = NULL;
BLECharacteristic* pCharWrite = NULL;
BLECharacteristic* pCharControl = NULL;
bool deviceConnected = false;

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39

#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

const int timerInterval = 10000;
unsigned long previousMillis = 0;

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("🔗 iOS device connected!");
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("❌ iOS device disconnected!");
        BLEDevice::startAdvertising();
        Serial.println("🔄 Restarted advertising for reconnection");
    }
};

class MyCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue();

        if (value.length() > 0) {
            Serial.println("📨 RECEIVED MESSAGE FROM iOS APP:");
            Serial.print("   Raw bytes: ");
            for (int i = 0; i < value.length(); i++) {
                Serial.printf("%02X ", (uint8_t)value[i]);
            }
            Serial.println();

            Serial.print("   As text: '");
            for (int i = 0; i < value.length(); i++) {
                if (isPrintable(value[i])) {
                    Serial.print(value[i]);
                } else {
                    Serial.printf("\\x%02X", (uint8_t)value[i]);
                }
            }
            Serial.println("'");

            String response = "ACK: " + value;
            pCharNotify->setValue(response.c_str());
            pCharNotify->notify();

            Serial.println("📤 Sent acknowledgment back to iOS");
            Serial.println("=====================================");
        }
    }
};

class ControlCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        String value = pCharacteristic->getValue();

        if (value.length() > 0) {
            Serial.println("🎛️ CONTROL COMMAND RECEIVED:");
            Serial.print("   Command: '");
            Serial.print(value);
            Serial.println("'");

            if (value == "sleep") {
                Serial.println("💤 DEEP SLEEP COMMAND RECEIVED!");
                Serial.println("   Preparing for deep sleep...");

                pCharNotify->setValue("Going to sleep...");
                pCharNotify->notify();

                delay(100);

                Serial.println("   Entering deep sleep in 2 seconds...");
                delay(2000);

                esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);

                Serial.println("   🌙 Good night! ESP32 entering deep sleep...");
                Serial.println("   =================================");
                delay(100);

                esp_deep_sleep_start();
            } else {
                Serial.println("   Unknown command, ignoring");
                pCharNotify->setValue("Unknown command");
                pCharNotify->notify();
            }
        }
    }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.print("XIAO ESP32-S3 IP Address: ");
  Serial.println(WiFi.localIP());

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s == NULL) {
    Serial.println("Failed to get camera sensor");
    delay(1000);
    ESP.restart();
  }

  s->set_framesize(s, FRAMESIZE_SVGA);

  Serial.println("Camera initialized successfully");
  Serial.printf("Camera sensor ID: 0x%x\n", s->id.PID);

  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s, 0);
  s->set_ae_level(s, 0);
  s->set_aec_value(s, 300);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)0);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);

  delay(1000);

  Serial.println("Starting image transmission...");

  sendPhoto();

  Serial.println("🚀 Initializing BLE Pothole Detector Server");
  Serial.println("============================================");

  BLEDevice::init("ezpotholecamera");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pCharNotify = pService->createCharacteristic(
    CHAR_NOTIFY_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharWrite = pService->createCharacteristic(
    CHAR_WRITE_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );

  pCharControl = pService->createCharacteristic(
    CHAR_CONTROL_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
  );

  pCharWrite->setCallbacks(new MyCallbacks());
  pCharControl->setCallbacks(new ControlCallbacks());

  pCharNotify->setValue("ESP32 Ready - Pothole Detection Active!");
  pCharWrite->setValue("Hello from ESP32 Camera!");

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);

  BLEDevice::startAdvertising();
  Serial.println("✅ BLE Server started and advertising!");
  Serial.println("🔍 iOS app should find device: 'ezpotholecamera'");
  Serial.println("📱 Waiting for iOS connection...");
  Serial.println("============================================");
}

void loop() {
  static unsigned long lastBLEUpdate = 0;
  static int messageCount = 0;

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= timerInterval) {
    sendPhoto();
    previousMillis = currentMillis;
  }

  if (millis() - lastBLEUpdate > 5000) {
    if (deviceConnected) {
      messageCount++;
      String status = "Pothole Detector #" + String(messageCount) + ": Monitoring active";

      pCharNotify->setValue(status.c_str());
      pCharNotify->notify();

      Serial.printf("📊 Sent status update #%d to iOS\n", messageCount);
    } else {
      Serial.println("⏳ Waiting for iOS connection...");
    }
    lastBLEUpdate = millis();
  }

  delay(100);
}

String sendPhoto() {
  String getAll;
  String getBody;

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();

  if(!fb) {
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }

  Serial.printf("Image captured: %dx%d, %d bytes\n", fb->width, fb->height, fb->len);

  Serial.println("Connecting to server: " + serverName);

  client.setInsecure();

  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connection successful!");

    unsigned long timestamp = millis() / 1000 + 1697358000;

    String boundary = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    String head = "";
    String tail = "\r\n--" + boundary + "--\r\n";

    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"session_id\"\r\n\r\n";
    head += sessionId + "\r\n";

    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"type\"\r\n\r\n";
    head += "image\r\n";

    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"timestamp\"\r\n\r\n";
    head += String(timestamp) + "\r\n";

    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"image\"; filename=\"xiao_esp32s3.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    if (authToken.length() > 0) {
      client.println("Authorization: Bearer " + authToken);
    }
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=" + boundary);
    client.println();
    client.print(head);

    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    size_t chunkSize = 1024;

    for (size_t n = 0; n < fbLen; n += chunkSize) {
      if (n + chunkSize < fbLen) {
        client.write(fbBuf, chunkSize);
        fbBuf += chunkSize;
      } else {
        size_t remainder = fbLen - n;
        client.write(fbBuf, remainder);
      }
    }

    client.print(tail);

    esp_camera_fb_return(fb);

    Serial.println("Image sent, waiting for response...");

    int timeoutTimer = 10000;
    long startTimer = millis();
    boolean state = false;

    while ((startTimer + timeoutTimer) > millis()) {
      delay(100);
      while (client.available()) {
        char c = client.read();
        if (c == '\n') {
          if (getAll.length() == 0) {
            state = true;
          }
          getAll = "";
        }
        else if (c != '\r') {
          getAll += String(c);
        }
        if (state == true) {
          getBody += String(c);
        }
        startTimer = millis();
      }
      if (getBody.length() > 0) {
        break;
      }
    }

    Serial.println();
    client.stop();
    Serial.println("Response: " + getBody);
  }
  else {
    getBody = "Connection to " + serverName + " failed.";
    Serial.println(getBody);
    esp_camera_fb_return(fb);
  }

  return getBody;
}