#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEUtils.h>

#define PIN_LED 8
#define PIN_BUTTON 9
#define FIRMWARE_VER 1  // 改此版本号 = 烧录后自动清 NVS

// ========== BLE 出厂默认值 (可通过配置页修改) ==========
#define DEFAULT_BLE_UUID "3132A97F-FA40-D56B-04E0-8562E0D3AEE6"
#define DEFAULT_BLE_NAME "PunchDevice"

Preferences preferences;
DNSServer dns;
AsyncWebServer server(80);

// ========== WiFi 配置变量 ==========
bool enableWiFi = true;
String ssid;
String password;
String macAddress;
uint8_t macArray[6];

// ========== BLE 配置变量 ==========
bool enableBLE = true;                              // 默认开启
String bleUuidStr = DEFAULT_BLE_UUID;
String bleName    = DEFAULT_BLE_NAME;
bool bleActive = false;

bool enableDNS = true;

bool lastButtonState = HIGH;
bool currentButtonState = HIGH;
uint32_t pressedTime = 0;

const char *TAG = "MJOLNIR";

bool convertMacStringToArray() {
    if (macAddress.length() == 17) {
        const char *macStr = macAddress.c_str();
        for (unsigned char &i: macArray) {
            sscanf(macStr, "%02x", &i);
            macStr += 3;
        }
        ESP_LOGD(TAG, "Conversions successfully, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 macArray[0], macArray[1],
                 macArray[2], macArray[3],
                 macArray[4], macArray[5]);
        return true;
    } else {
        ESP_LOGD(TAG, "Invalid MAC address");
        return false;
    }
}

// ==================== BLE 功能 ====================

// 从 UUID 字符串派生 MAC 地址 (取前 12 hex 字符)
// UUID "3132A97F-FA40-D56B-..." -> MAC "31:32:A9:7F:FA:40"
// MAC   "F4:2A:7D:E0:A2:C3"   -> MAC "F4:2A:7D:E0:A2:C3"
void uuidToMac(const String &input, uint8_t *outMac) {
    String hex = input;
    hex.replace("-", "");
    hex.replace(":", "");
    hex.toUpperCase();
    // 确保至少有 12 个 hex 字符
    while (hex.length() < 12) hex += "0";
    const char *str = hex.c_str();
    for (int i = 0; i < 6; i++) {
        unsigned int byteVal;
        sscanf(str, "%02x", &byteVal);
        outMac[i] = (uint8_t)byteVal;
        str += 2;
    }
}

// 启动 BLE 广播 (MAC 自动从 UUID 派生)
bool startBLE() {
    if (bleUuidStr.length() < 2) bleUuidStr = DEFAULT_BLE_UUID;
    if (bleName.length() < 1)    bleName    = DEFAULT_BLE_NAME;

    String uuid = bleUuidStr;
    uuid.toUpperCase();

    // 从 UUID 自动派生 MAC
    uint8_t mac[6];
    uuidToMac(bleUuidStr, mac);

    ESP_LOGD(TAG, "BLE: UUID: %s", uuid.c_str());
    ESP_LOGD(TAG, "BLE: Derived MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // 计算 base mac (ESP32-C3 为 TWO_UNIVERSAL_MAC_ADDR)
    uint8_t baseMac[6];
    memcpy(baseMac, mac, 6);
    if (UNIVERSAL_MAC_ADDR_NUM == FOUR_UNIVERSAL_MAC_ADDR) {
        baseMac[5] -= 2;
    } else if (UNIVERSAL_MAC_ADDR_NUM == TWO_UNIVERSAL_MAC_ADDR) {
        baseMac[5] -= 1;
    }

    esp_err_t macErr = esp_base_mac_addr_set(baseMac);
    if (macErr != ESP_OK) {
        ESP_LOGW(TAG, "BLE: esp_base_mac_addr_set failed: %s", esp_err_to_name(macErr));
    }

    BLEDevice::init(bleName.c_str());
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (!pAdvertising) {
        ESP_LOGE(TAG, "BLE: getAdvertising() returned null");
        return false;
    }

    BLEUUID bleUuid(uuid.c_str());
    BLEAdvertisementData advData;
    advData.setCompleteServices(bleUuid);
    pAdvertising->setAdvertisementData(advData);

    BLEAdvertisementData scanRsp;
    scanRsp.setName(bleName.c_str());
    pAdvertising->setScanResponseData(scanRsp);

    pAdvertising->start();
    bleActive = true;
    // LED 快闪表示 BLE 已启动
    digitalWrite(PIN_LED, LOW);  delay(80);
    digitalWrite(PIN_LED, HIGH); delay(80);
    digitalWrite(PIN_LED, LOW);  delay(80);
    digitalWrite(PIN_LED, HIGH);
    ESP_LOGI(TAG, "BLE: Advertising started");
    ESP_LOGI(TAG, "BLE:  Name: %s", bleName.c_str());
    ESP_LOGI(TAG, "BLE:  UUID: %s", uuid.c_str());
    return true;
}

void stopBLE() {
    if (bleActive) {
        BLEDevice::getAdvertising()->stop();
        BLEDevice::deinit(true);
        bleActive = false;
        ESP_LOGD(TAG, "BLE: Stopped");
    }
}

bool createAP() {
    if (!convertMacStringToArray()) {
        digitalWrite(PIN_LED, HIGH);
        return false;
    }

    WiFi.enableAP(true);
    esp_err_t set_mac_err = esp_wifi_set_mac(WIFI_IF_AP, macArray);
    ESP_LOGD(TAG, "esp_wifi_set_mac: %s", esp_err_to_name(set_mac_err));
    if (set_mac_err == ESP_OK) {
        server.end();
        WiFi.softAPdisconnect();
        if (WiFi.softAP(ssid, password)) {
            // 降低发射功率 (4 ≈ 1dBm, 2米够用, 几乎不发热)
            esp_err_t set_power_err = esp_wifi_set_max_tx_power(4);
            ESP_LOGD(TAG, "esp_wifi_set_max_tx_power(4): %s", esp_err_to_name(set_power_err));
            // Beacon 间隔从 100ms 拉到 500ms, 减少射频占空比
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            conf.ap.beacon_interval = 500;
            esp_wifi_set_config(WIFI_IF_AP, &conf);
            digitalWrite(PIN_LED, LOW);
            ESP_LOGD(TAG, "AP created successfully");
            ESP_LOGD(TAG, "#     SSID: %s", ssid.c_str());
            ESP_LOGD(TAG, "# Password: %s", password.c_str());
            ESP_LOGD(TAG, "#      MAC: %s", macAddress.c_str());
            server.begin();
            enableDNS = false;
            return true;
        } else {
            digitalWrite(PIN_LED, HIGH);
            return false;
        }
    } else {
        digitalWrite(PIN_LED, HIGH);
        ESP_LOGD(TAG, "Failed to set MAC address");
        return false;
    }
}

void initWebServer() {
    bool fsOk = LittleFS.begin();
    if (!fsOk) {
        ESP_LOGW(TAG, "LittleFS failed, serving minimal page");
    }

    server.on("/", HTTP_GET, [fsOk](AsyncWebServerRequest *request) {
        if (fsOk) {
            request->send(LittleFS, "/index.html", "text/html");
        } else {
            request->send(200, "text/html",
                "<h2>文件系统未烧录</h2><p>请执行: pio run --target uploadfs</p>");
        }
    });

    server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });

    // ========== WiFi + BLE 配置提交 ==========
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
        String _wifi_enable_str;
        String _ssid_str;
        String _password_str;
        String _mac_str;
        String _ble_enable_str;
        String _ble_uuid_str;

        for (int i = 0; i < request->params(); i++) {
            AsyncWebParameter *p = request->getParam(i);

            if (p->name() == "wifi_enable")      _wifi_enable_str = p->value();
            if (p->name() == "wifi_ssid")        _ssid_str = p->value();
            if (p->name() == "wifi_password")    _password_str = p->value();
            if (p->name() == "wifi_mac")         _mac_str = p->value();
            if (p->name() == "ble_enable")       _ble_enable_str = p->value();
            if (p->name() == "ble_uuid")         _ble_uuid_str = p->value();
        }

        bool wifiEnableReq = (_wifi_enable_str == "on" || _wifi_enable_str == "1" || _wifi_enable_str == "true");
        bool bleEnableReq  = (_ble_enable_str  == "on" || _ble_enable_str  == "1" || _ble_enable_str  == "true");

        // ---- WiFi 校验 ----
        if (wifiEnableReq) {
            if (_ssid_str.length() < 1 || _ssid_str.length() > 32) {
                request->send(200, "text/plain", "SSID格式错误(1-32字符)"); return;
            }
            if (_password_str.length() > 0 && _password_str.length() < 8) {
                request->send(200, "text/plain", "密码至少8位或留空"); return;
            }
            if (_mac_str.length() != 17) {
                request->send(200, "text/plain", "MAC地址格式错误(aa:bb:cc:dd:ee:ff)"); return;
            }
            ssid = _ssid_str;
            password = _password_str;
            macAddress = _mac_str;
        }
        enableWiFi = wifiEnableReq;

        // ---- BLE 校验 ----
        if (bleEnableReq) {
            if (_ble_uuid_str.length() < 2) {
                request->send(200, "text/plain", "蓝牙UUID不能为空"); return;
            }
            enableBLE = true;
            bleUuidStr = _ble_uuid_str;
        } else {
            enableBLE = false;
        }

        // ---- 持久化到 NVS ----
        preferences.begin("wifi_config", false);
        preferences.putBool("wifi_enable", enableWiFi);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("mac", macAddress);
        preferences.putBool("ble_enable", enableBLE);
        if (enableBLE) {
            preferences.putString("ble_uuid", bleUuidStr);
        } else {
            preferences.remove("ble_uuid");
        }
        preferences.end();

        // ---- 保存后重启生效 ----
        request->send(200, "text/plain", "配置已保存,设备重启中...");
        delay(500);
        ESP.restart();
    });
    server.begin();
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    digitalWrite(PIN_LED, HIGH);

    // ---- 读取 NVS 中保存的 WiFi + BLE 配置 ----
    if (!preferences.begin("wifi_config", false)) {
        ESP_LOGD(TAG, "Failed to initialize preferences");
    }

    // 版本检测: 固件更新后自动清 NVS, 避免旧配置导致异常
    int savedVer = preferences.getInt("fw_ver", 0);
    if (savedVer != FIRMWARE_VER) {
        ESP_LOGW(TAG, "Firmware updated (v%d -> v%d), clearing NVS", savedVer, FIRMWARE_VER);
        preferences.clear();
        preferences.putInt("fw_ver", FIRMWARE_VER);
        preferences.end();
        preferences.begin("wifi_config", false);
    }

    enableWiFi = preferences.getBool("wifi_enable", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    macAddress = preferences.getString("mac", "");

    enableBLE  = preferences.getBool("ble_enable", true);   // 默认开启
    bleUuidStr = preferences.getString("ble_uuid", DEFAULT_BLE_UUID);

    preferences.end();

    // ---- BLE 必须在 WiFi 之前启动 (ESP32-C3 射频共存) ----
    ESP_LOGD(TAG, "Starting BLE first (enableBLE=%d)...", enableBLE);
    if (enableBLE) {
        if (!startBLE()) {
            ESP_LOGE(TAG, "BLE start failed!");
        }
    }

    // ---- 初始化 WiFi (关闭时完全不发射) ----
    if (enableWiFi) {
        WiFi.mode(WIFI_AP);

        if (ssid.length() > 0 && macAddress.length() == 17) {
            if (!createAP()) {
                ESP_LOGD(TAG, "WiFi AP creation failed. Fallback to default");
                WiFi.softAP("WIFI MANAGER");
                esp_wifi_set_max_tx_power(4);
                wifi_config_t conf;
                esp_wifi_get_config(WIFI_IF_AP, &conf);
                conf.ap.beacon_interval = 500;
                esp_wifi_set_config(WIFI_IF_AP, &conf);
                enableDNS = true;
            }
        } else {
            ESP_LOGD(TAG, "No WiFi credentials. Creating default AP");
            WiFi.softAP("WIFI MANAGER");
            esp_wifi_set_max_tx_power(4);
            wifi_config_t conf;
            esp_wifi_get_config(WIFI_IF_AP, &conf);
            conf.ap.beacon_interval = 500;
            esp_wifi_set_config(WIFI_IF_AP, &conf);
            enableDNS = true;
        }

        // ---- 启动 Web 服务器和 DNS ----
        initWebServer();
        dns.start(53, "*", WiFi.softAPIP());
    } else {
        ESP_LOGD(TAG, "WiFi disabled, not broadcasting");
        WiFi.mode(WIFI_OFF);
    }
}

void loop() {
    currentButtonState = digitalRead(PIN_BUTTON);

    if (lastButtonState == HIGH && currentButtonState == LOW) {
        pressedTime = millis();
    } else if (lastButtonState == LOW && currentButtonState == HIGH) {
        if (millis() - pressedTime > 1000) {
            ESP_LOGD(TAG, "Button long pressed, Clean all credentials");
            stopBLE();
            preferences.begin("wifi_config", false);
            preferences.clear();
            preferences.end();
            delay(1000);
            ESP.restart();
        }
    }
    lastButtonState = currentButtonState;

    if (enableDNS) {
        dns.processNextRequest();
    }
    delay(50);
}