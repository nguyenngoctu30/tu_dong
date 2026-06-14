#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <time.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUDP.h>

// ===================== THƯ VIỆN TFT =====================
// Cài đặt qua Library Manager:
//   - "Adafruit GFX Library"
//   - "Adafruit ILI9341"
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>

// ===================== CHÂN TFT 2.8" SPI =====================
// Kết nối vật lý:
//   TFT VCC  -> 3.3V
//   TFT GND  -> GND
//   TFT CS   -> GPIO 5   (TFT_CS)
//   TFT RESET-> GPIO 4   (TFT_RST)
//   TFT DC   -> GPIO 2   (TFT_DC)
//   TFT MOSI -> GPIO 23  (SPI MOSI mặc định)
//   TFT SCK  -> GPIO 18  (SPI SCK mặc định)
//   TFT LED  -> 3.3V (hoặc qua điện trở 100Ω)
//   TFT MISO -> GPIO 19  (không bắt buộc nếu chỉ ghi)
#define TFT_CS   5
#define TFT_RST  4
#define TFT_DC   2

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Màu sắc tùy chỉnh
#define COLOR_BG        ILI9341_BLACK
#define COLOR_TITLE_BG  0x1082       // Xanh đậm
#define COLOR_WHITE     ILI9341_WHITE
#define COLOR_CYAN      ILI9341_CYAN
#define COLOR_GREEN     ILI9341_GREEN
#define COLOR_RED       ILI9341_RED
#define COLOR_YELLOW    ILI9341_YELLOW
#define COLOR_ORANGE    0xFD20       // Cam
#define COLOR_GRAY      0x8410       // Xám

// ===================== CẤU HÌNH SIM / GPS =====================
#define SIM_RX 16
#define SIM_TX 17
#define SIM_BAUDRATE 115200
HardwareSerial simSerial(2);

// ===================== PREFERENCES =====================
Preferences prefs;
#define PREF_NAMESPACE "freezer"
#define PREF_KEY_THRESHOLD "threshold"
#define PREF_KEY_PHONE "phone"

// ===================== CHÂN CẢM BIẾN =====================
#define DHTPIN 27
#define DHTTYPE DHT22
#define MQ2_PIN 34
#define FAN_PIN 26

// ===================== WiFi =====================
const char* ssid = "677 5G";
const char* password = "10101010";

// ===================== MQTT =====================
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_client_id = "TUIOT_ESP32_Client";
const char* topic_temp      = "tuiot/freezer/temperature";
const char* topic_hum       = "tuiot/freezer/humidity";
const char* topic_threshold = "tuiot/freezer/threshold";
const char* topic_gas       = "tuiot/freezer/gas";
const char* topic_gps       = "tuiot/freezer/gps";
const char* topic_phone     = "tuiot/freezer/phone";
const char* topic_alert     = "tuiot/freezer/alert";
const char* topic_test_call = "tuiot/freezer/test_call";
const char* topic_fan_mode  = "tuiot/freezer/fan_mode";
const char* topic_fan_ctrl  = "tuiot/freezer/fan_ctrl";
const char* topic_fan_status= "tuiot/freezer/fan_status";

// ===================== THINGSPEAK =====================
const char* thingSpeakServer = "api.thingspeak.com";
const String writeApiKey = "VA6IIC1OBJVA7FE3";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7 * 3600, 60000);

// ===================== BIẾN TOÀN CỤC =====================
bool fanState   = false;
bool isFanAuto  = true;
unsigned long lastUpdate    = 0;
const unsigned long updateInterval = 15000;
unsigned long lastGPSRequest = 0;
const unsigned long gpsInterval = 5000;
float temperatureThreshold  = 20.0;
bool gasDetected    = false;
bool callMade       = false;
float lastLatitude  = -999;
float lastLongitude = -999;
bool gpsValid       = false;
String phoneNumber  = "+84938718925";
bool lastTempAlertState = false;
bool lastGasAlertState  = false;

// Biến cache màn hình (tránh vẽ lại khi không đổi)
float  prev_temperature = -999;
float  prev_humidity    = -999;
float  prev_threshold   = -999;
bool   prev_gasDetected = false;
bool   prev_fanState    = false;
bool   prev_wifiOK      = false;
bool   prev_mqttOK      = false;
bool   prev_fanAuto     = false;
bool   tftInitialized   = false;

// ===================== HÀM HIỂN THỊ TFT =====================

void drawStaticLayout() {
    tft.fillScreen(COLOR_BG);

    // === TIÊU ĐỀ ===
    tft.fillRect(0, 0, 240, 36, COLOR_TITLE_BG);
    tft.setTextColor(COLOR_CYAN);
    tft.setTextSize(2);
    tft.setCursor(22, 10);
    tft.print("FREEZER MONITOR");

    // === ĐƯỜNG KẺ PHÂN CÁCH ===
    tft.drawLine(0, 36, 240, 36, COLOR_GRAY);

    // --- Nhãn trạng thái ---
    tft.setTextSize(1);
    tft.setTextColor(COLOR_GRAY);
    tft.setCursor(4, 44);   tft.print("WiFi:");
    tft.setCursor(84, 44);  tft.print("MQTT:");
    tft.setCursor(164, 44); tft.print("Mode:");

    tft.drawLine(0, 58, 240, 58, COLOR_GRAY);

    // --- Nhãn cảm biến ---
    tft.setTextSize(1);
    tft.setTextColor(COLOR_GRAY);

    tft.setCursor(4, 68);   tft.print("Nhiet do (C):");
    tft.setCursor(4, 100);  tft.print("Do am (%):");
    tft.setCursor(4, 132);  tft.print("Nguong canh bao (C):");
    tft.setCursor(4, 164);  tft.print("Trang thai Gas:");
    tft.setCursor(4, 196);  tft.print("Quat:");

    // === ĐƯỜNG KẺ ĐÁY ===
    tft.drawLine(0, 216, 240, 216, COLOR_GRAY);

    // === FOOTER ===
    tft.setTextSize(1);
    tft.setTextColor(COLOR_GRAY);
    tft.setCursor(4, 222);
    tft.print("IoT System v1.0");
}

// Xóa vùng giá trị và ghi lại
void clearValueArea(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COLOR_BG);
}

// (XÓA bỏ hàm clearValueArea vì không còn cần thiết nữa)
// ===================== HÀM HIỂN THỊ TFT TỐI ƯU =====================

// LƯU Ý: Đã xóa hàm clearValueArea() để tránh chớp nháy (flicker)

void updateTFT(float temperature, float humidity, bool wifi, bool mqtt) {
    bool needFullRedraw = !tftInitialized;
    if (needFullRedraw) {
        drawStaticLayout();
        tftInitialized = true;
    }

    // === WiFi STATUS ===
    if (needFullRedraw || prev_wifiOK != wifi) {
        tft.setTextSize(1);
        // Kết hợp màu chữ và màu nền để tự động chùi chữ cũ
        tft.setTextColor(wifi ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        tft.setCursor(38, 44);
        tft.print(wifi ? "OK   " : "FAIL "); 
        prev_wifiOK = wifi;
    }

    // === MQTT STATUS ===
    if (needFullRedraw || prev_mqttOK != mqtt) {
        tft.setTextSize(1);
        tft.setTextColor(mqtt ? COLOR_GREEN : COLOR_RED, COLOR_BG);
        tft.setCursor(118, 44);
        tft.print(mqtt ? "OK   " : "FAIL ");
        prev_mqttOK = mqtt;
    }

    // === CHẾ ĐỘ QUẠT (AUTO/MANUAL) ===
    if (needFullRedraw || prev_fanAuto != isFanAuto) {
        tft.setTextSize(1);
        tft.setTextColor(isFanAuto ? COLOR_CYAN : COLOR_YELLOW, COLOR_BG);
        tft.setCursor(196, 44);
        // Bù thêm khoảng trắng để chiều dài chuỗi bằng nhau
        tft.print(isFanAuto ? "AUTO  " : "MANUAL"); 
        prev_fanAuto = isFanAuto;
    }

    // === NHIỆT ĐỘ ===
    if (needFullRedraw || prev_temperature != temperature) {
        tft.setTextSize(2);
        char buf[10];
        bool tempHigh = (temperature > temperatureThreshold);

        if (isnan(temperature)) {
            sprintf(buf, "--.- ");
            tft.setTextColor(COLOR_GRAY, COLOR_BG);
        } else {
            // Định dạng chuỗi ép cứng 5 ký tự để không bị thụt lùi (Ví dụ: " 9.5 ")
            sprintf(buf, "%-5.1f", temperature); 
            tft.setTextColor(tempHigh ? COLOR_RED : COLOR_WHITE, COLOR_BG);
        }
        tft.setCursor(4, 79);
        tft.print(buf);

        // --- Hiển thị cảnh báo CAO nằm riêng biệt ---
        tft.setTextSize(1);
        tft.setCursor(80, 82); // Tọa độ X=80 an toàn, không bị nhiệt độ đè lên
        if (!isnan(temperature) && tempHigh) {
            tft.setTextColor(COLOR_RED, COLOR_BG);
            tft.print("[!] CAO"); 
        } else {
            // In 7 khoảng trắng đè lên để chùi sạch chữ "[!] CAO"
            tft.setTextColor(COLOR_WHITE, COLOR_BG);
            tft.print("       "); 
        }
        prev_temperature = temperature;
    }

    // === ĐỘ ẨM ===
    if (needFullRedraw || prev_humidity != humidity) {
        tft.setTextSize(2);
        char buf[10];
        if (isnan(humidity)) {
            sprintf(buf, "--.- ");
            tft.setTextColor(COLOR_GRAY, COLOR_BG);
        } else {
            sprintf(buf, "%-5.1f", humidity);
            tft.setTextColor(COLOR_CYAN, COLOR_BG);
        }
        tft.setCursor(4, 111);
        tft.print(buf);
        prev_humidity = humidity;
    }

    // === NGƯỠNG CẢNH BÁO ===
    if (needFullRedraw || prev_threshold != temperatureThreshold) {
        tft.setTextSize(2);
        tft.setTextColor(COLOR_ORANGE, COLOR_BG);
        tft.setCursor(4, 143);
        char buf[10];
        sprintf(buf, "%-5.1f", temperatureThreshold);
        tft.print(buf);
        prev_threshold = temperatureThreshold;
    }

    // === TRẠNG THÁI GAS (FIX LỖI CHE CHỮ QUẠT) ===
    if (needFullRedraw || prev_gasDetected != gasDetected) {
        tft.setTextSize(2);
        tft.setCursor(4, 175);
        if (gasDetected) {
            tft.setTextColor(COLOR_RED, COLOR_BG);
            // Giới hạn đúng 20 ký tự (Chiều ngang tối đa của màn 240px ở TextSize 2)
            tft.print("[!] GAS - NGUY HIEM "); 
        } else {
            tft.setTextColor(COLOR_GREEN, COLOR_BG);
            // Đúng 20 ký tự để chùi toàn bộ dòng báo động đỏ ở trên
            tft.print("Binh thuong         "); 
        }
        prev_gasDetected = gasDetected;
    }

    // === TRẠNG THÁI QUẠT ===
    if (needFullRedraw || prev_fanState != fanState) {
        tft.setTextSize(2);
        tft.setCursor(50, 195);
        if (fanState) {
            tft.setTextColor(COLOR_GREEN, COLOR_BG);
            tft.print("BAT    ");
        } else {
            tft.setTextColor(COLOR_GRAY, COLOR_BG);
            tft.print("TAT    ");
        }
        prev_fanState = fanState;
    }
}
// ===================== CÁC HÀM GỐC =====================

bool isValidPhoneNumber(String phone) {
    if (!phone.startsWith("+")) return false;
    for (int i = 1; i < phone.length(); i++) {
        if (!isDigit(phone[i])) return false;
    }
    return phone.length() >= 10 && phone.length() <= 15;
}

void savePhoneToEEPROM(String phone) {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putString(PREF_KEY_PHONE, phone);
    prefs.end();
}

void saveThresholdToEEPROM(float value) {
    prefs.begin(PREF_NAMESPACE, false);
    prefs.putFloat(PREF_KEY_THRESHOLD, value);
    prefs.end();
}

void makeCall(String phone) {
    if (!isValidPhoneNumber(phone)) return;
    simSerial.println("ATD" + phone + ";");
    Serial.println("Calling: " + phone);
    unsigned long callStartTime = millis();
    while (millis() - callStartTime < 30000) {
        if (client.connected()) client.loop();
        delay(10);
    }
    simSerial.println("ATH");
    Serial.println("Call ended");
}

bool checkGPSStatus() {
    simSerial.println("AT+QGPS?");
    delay(200);
    String response = "";
    while (simSerial.available()) response += simSerial.readStringUntil('\n');
    return response.indexOf("+QGPS: 1") != -1;
}

float nmeaToDecimal(float nmea) {
    int degrees = int(nmea / 100);
    float minutes = nmea - (degrees * 100);
    return degrees + (minutes / 60.0);
}

void processGPSLocation() {
    simSerial.println("AT+QGPSLOC?");
    delay(200);
    String response = "";
    while (simSerial.available()) response += simSerial.readStringUntil('\n');

    if (response.indexOf("+QGPSLOC:") == -1) { gpsValid = false; return; }

    int startIdx = response.indexOf("+QGPSLOC:") + 9;
    int endIdx = response.indexOf('\n', startIdx);
    String data = response.substring(startIdx, endIdx);
    String fields[20];
    int fieldIndex = 0, fromIdx = 0;
    while (fieldIndex < 20) {
        int commaIdx = data.indexOf(',', fromIdx);
        if (commaIdx == -1) { fields[fieldIndex++] = data.substring(fromIdx); break; }
        fields[fieldIndex++] = data.substring(fromIdx, commaIdx);
        fromIdx = commaIdx + 1;
    }

    if (fieldIndex < 3) { gpsValid = false; return; }

    String latStr = fields[1], lonStr = fields[2];
    String lat_dir = latStr.substring(latStr.length() - 1);
    String lon_dir = lonStr.substring(lonStr.length() - 1);
    latStr = latStr.substring(0, latStr.length() - 1);
    lonStr = lonStr.substring(0, lonStr.length() - 1);

    float lat_nmea = latStr.toFloat(), lon_nmea = lonStr.toFloat();
    if (lat_nmea == 0 || lon_nmea == 0) { gpsValid = false; return; }

    float lat = nmeaToDecimal(lat_nmea), lon = nmeaToDecimal(lon_nmea);
    if (lat_dir == "S") lat = -lat;
    if (lon_dir == "W") lon = -lon;

    gpsValid = true; lastLatitude = lat; lastLongitude = lon;
    char gpsStr[32]; snprintf(gpsStr, sizeof(gpsStr), "%.6f,%.6f", lat, lon);
    client.publish(topic_gps, gpsStr);
}

void sendAlert(String type, String timestamp) {
    String alertMsg = type + "|" + timestamp;
    client.publish(topic_alert, alertMsg.c_str());
}

void reconnect() {
    while (!client.connected() && WiFi.status() == WL_CONNECTED) {
        Serial.print("Connecting to MQTT...");
        if (client.connect(mqtt_client_id)) {
            Serial.println("connected");
            client.subscribe(topic_threshold);
            client.subscribe(topic_phone);
            client.subscribe(topic_test_call);
            client.subscribe(topic_fan_mode);
            client.subscribe(topic_fan_ctrl);
        } else {
            Serial.print("failed, rc="); Serial.print(client.state());
            delay(3000);
        }
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
    Serial.println("MQTT Rx [" + String(topic) + "]: " + message);

    bool needTftUpdate = false; // Cờ báo hiệu cần vẽ lại màn hình ngay

    if (String(topic) == topic_threshold) {
        float newThreshold = message.toFloat();
        if (newThreshold >= -30.0 && newThreshold <= 50.0) {
            temperatureThreshold = newThreshold;
            saveThresholdToEEPROM(temperatureThreshold);
            needTftUpdate = true; // Yêu cầu cập nhật TFT
        }
    } else if (String(topic) == topic_phone) {
        if (isValidPhoneNumber(message)) {
            phoneNumber = message;
            savePhoneToEEPROM(phoneNumber);
        }
    } else if (String(topic) == topic_test_call) {
        if (message == "CALL") makeCall(phoneNumber);
    } else if (String(topic) == topic_fan_mode) {
        isFanAuto = (message == "AUTO");
        needTftUpdate = true; // Yêu cầu cập nhật TFT
    } else if (String(topic) == topic_fan_ctrl) {
        if (!isFanAuto) {
            fanState = (message == "ON");
            digitalWrite(FAN_PIN, fanState ? HIGH : LOW);
            client.publish(topic_fan_status, fanState ? "ON" : "OFF");
            needTftUpdate = true; // Yêu cầu cập nhật TFT
        }
    }

    // Nếu có sự thay đổi thông số từ MQTT, vẽ lại màn hình ngay lập tức!
    // Ta truyền vào prev_temperature và prev_humidity để màn hình giữ nguyên số đo cảm biến cũ
    if (needTftUpdate) {
        updateTFT(prev_temperature, prev_humidity, WiFi.status() == WL_CONNECTED, client.connected());
    }
}
void sendToThingSpeak(float temperature, float humidity, bool gas, float lat, float lon) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure tsClient; tsClient.setInsecure();
        if (tsClient.connect(thingSpeakServer, 443)) {
            String postData = "api_key=" + writeApiKey
                + "&field1=" + String(temperature, 2)
                + "&field2=" + String(humidity, 2)
                + "&field3=" + String(gas ? 1 : 0);
            if (gpsValid) postData += "&field4=" + String(lat, 6) + "&field5=" + String(lon, 6);
            tsClient.println("POST /update HTTP/1.1");
            tsClient.println("Host: api.thingspeak.com");
            tsClient.println("Connection: close");
            tsClient.println("Content-Type: application/x-www-form-urlencoded");
            tsClient.println("Content-Length: " + String(postData.length()));
            tsClient.println(); tsClient.println(postData); tsClient.stop();
        }
    }
}

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);

    // Khởi tạo TFT trước tiên để hiển thị trạng thái boot
    tft.begin();
    tft.setRotation(2);         // Dọc 240x320; đổi sang 1 hoặc 3 nếu muốn ngang
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_CYAN);
    tft.setTextSize(2);
    tft.setCursor(20, 130);
    tft.print("Khoi dong...");
    tft.setTextSize(1);
    tft.setTextColor(COLOR_GRAY);
    tft.setCursor(30, 158);
    tft.print("Freezer Monitor");

    pinMode(FAN_PIN, OUTPUT); digitalWrite(FAN_PIN, LOW);
    pinMode(MQ2_PIN, INPUT);

    // SIM / GPS
    simSerial.begin(SIM_BAUDRATE, SERIAL_8N1, SIM_RX, SIM_TX);
    delay(10000);
    simSerial.println("AT+QGPS=1"); delay(2000);
    simSerial.println("AT+QGPSCFG=\"outport\",1"); delay(2000);

    // Đọc Preferences
    prefs.begin(PREF_NAMESPACE, true);
    phoneNumber = prefs.getString(PREF_KEY_PHONE, "+84938718925");
    temperatureThreshold = prefs.getFloat(PREF_KEY_THRESHOLD, 20.0);
    prefs.end();

    // WiFi
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(20, 130);
    tft.print("Ket noi WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println("\nWiFi connected");

    // MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(90);

    timeClient.begin();
    dht.begin();

    // Vẽ giao diện chính ngay khi xong boot
    tftInitialized = false; // Bắt buộc vẽ lại toàn bộ lần đầu
}

// ===================== LOOP =====================
void loop() {
    // WiFi reconnect
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect(); WiFi.reconnect();
        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) delay(500);
    }
    if (!client.connected() && WiFi.status() == WL_CONNECTED) reconnect();
    client.loop();
    timeClient.update();

    unsigned long currentMillis = millis();

    // GPS
    if (currentMillis - lastGPSRequest >= gpsInterval) {
        if (checkGPSStatus()) processGPSLocation();
        else simSerial.println("AT+QGPS=1");
        lastGPSRequest = currentMillis;
    }

    static bool callMadeForTemp = false;

    if (currentMillis - lastUpdate >= updateInterval) {
        float temperature = dht.readTemperature();
        float humidity    = dht.readHumidity();
        gasDetected = !digitalRead(MQ2_PIN);

        String timestamp = timeClient.getFormattedTime();

        // LOGIC QUẠT TỰ ĐỘNG
        if (isFanAuto && !isnan(temperature)) {
            bool newFanState = (temperature > temperatureThreshold);
            if (newFanState != fanState) {
                fanState = newFanState;
                digitalWrite(FAN_PIN, fanState ? HIGH : LOW);
                client.publish(topic_fan_status, fanState ? "ON" : "OFF");
            }
        }

        // Cảnh báo nhiệt độ
        bool tempAlert = (temperature > temperatureThreshold);
        if (tempAlert && !lastTempAlertState) {
            sendAlert("Temperature High", timestamp);
            if (!callMadeForTemp) { makeCall(phoneNumber); callMadeForTemp = true; }
        } else if (!tempAlert && callMadeForTemp) {
            callMadeForTemp = false;
        }
        lastTempAlertState = tempAlert;

        // Cảnh báo gas
        if (gasDetected && !lastGasAlertState) {
            sendAlert("Gas Detected", timestamp);
            if (!callMade) { makeCall(phoneNumber); callMade = true; }
        } else if (!gasDetected && callMade) { callMade = false; }
        lastGasAlertState = gasDetected;

        // Publish MQTT & ThingSpeak
        if (!isnan(temperature) && !isnan(humidity)) {
            char tempStr[8], humStr[8];
            dtostrf(temperature, 6, 2, tempStr);
            dtostrf(humidity, 6, 2, humStr);
            client.publish(topic_temp, tempStr);
            client.publish(topic_hum, humStr);
            client.publish(topic_gas, gasDetected ? "1" : "0");
            client.publish(topic_fan_status, fanState ? "ON" : "OFF");
            sendToThingSpeak(temperature, humidity, gasDetected, lastLatitude, lastLongitude);
        }

        // === CẬP NHẬT MÀN HÌNH TFT ===
        bool wifiOK = (WiFi.status() == WL_CONNECTED);
        bool mqttOK = client.connected();
        updateTFT(temperature, humidity, wifiOK, mqttOK);

        lastUpdate = currentMillis;
    }
}
