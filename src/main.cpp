#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>

#define PIN_LED 8
#define PIN_BUTTON 9

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
bool enableBLE = false;
String bleMacStr;
String bleRawStr;
uint8_t bleMac[6];
uint8_t bleRaw[31] = {0};
bool rawMoreThan31 = false;
uint8_t bleRawExt[31] = {0};
uint8_t bleRawExtLen = 0;
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

// 解析逗号分隔的十六进制字符串为字节数组
// 支持格式: "02,01,06,FF,..." 或 "0x02,0x01,0x06,0xFF,..."
int parseBleRawData(const String &raw, uint8_t *output, int maxLen) {
    String cleaned = raw;
    cleaned.replace(" ", "");
    cleaned.replace("0x", "");
    cleaned.replace("0X", "");

    int count = 0;
    int start = 0;
    while (start < cleaned.length() && count < maxLen) {
        int end = cleaned.indexOf(',', start);
        if (end == -1) end = cleaned.length();
        String hexByte = cleaned.substring(start, end);
        if (hexByte.length() > 0) {
            output[count++] = (uint8_t)strtol(hexByte.c_str(), nullptr, 16);
        }
        start = end + 1;
    }
    return count;
}

// 将 MAC 字符串 "aa:bb:cc:dd:ee:ff" 转换为 6 字节数组 (用于 BLE)
bool parseBleMac(const String &macStr, uint8_t *outMac) {
    if (macStr.length() != 17) return false;
    const char *str = macStr.c_str();
    for (int i = 0; i < 6; i++) {
        unsigned int byteVal;
        if (sscanf(str, "%02x", &byteVal) != 1) return false;
        outMac[i] = (uint8_t)byteVal;
        str += 3;
    }
    return true;
}

// 启动 BLE 广播模拟
bool startBLE() {
    if (!enableBLE || bleMacStr.length() != 17 || bleRawStr.length() < 2) {
        ESP_LOGD(TAG, "BLE: Invalid config, skipping");
        return false;
    }

    if (!parseBleMac(bleMacStr, bleMac)) {
        ESP_LOGD(TAG, "BLE: Invalid MAC format");
        return false;
    }

    // 清空 raw 数组
    memset(bleRaw, 0, sizeof(bleRaw));
    memset(bleRawExt, 0, sizeof(bleRawExt));
    rawMoreThan31 = false;
    bleRawExtLen = 0;

    int totalBytes = parseBleRawData(bleRawStr, bleRaw, 62);
    if (totalBytes <= 0) {
        ESP_LOGD(TAG, "BLE: Failed to parse raw data");
        return false;
    }

    ESP_LOGD(TAG, "BLE: Parsed %d raw bytes", totalBytes);

    // 超过 31 字节的作为扫描响应数据
    if (totalBytes > 31) {
        rawMoreThan31 = true;
        bleRawExtLen = totalBytes - 31;
        memcpy(bleRawExt, bleRaw + 31, bleRawExtLen);
        ESP_LOGD(TAG, "BLE: Scan response data: %d bytes", bleRawExtLen);
    }

    // 计算 base mac (ESP32-C3 为 TWO_UNIVERSAL_MAC_ADDR)
    uint8_t baseMac[6];
    memcpy(baseMac, bleMac, 6);
    if (UNIVERSAL_MAC_ADDR_NUM == FOUR_UNIVERSAL_MAC_ADDR) {
        baseMac[5] -= 2;
    } else if (UNIVERSAL_MAC_ADDR_NUM == TWO_UNIVERSAL_MAC_ADDR) {
        baseMac[5] -= 1;
    }
    ESP_LOGD(TAG, "BLE: Setting base MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             baseMac[0], baseMac[1], baseMac[2],
             baseMac[3], baseMac[4], baseMac[5]);

    esp_err_t macErr = esp_base_mac_addr_set(baseMac);
    if (macErr != ESP_OK) {
        ESP_LOGD(TAG, "BLE: esp_base_mac_addr_set failed: %s", esp_err_to_name(macErr));
    }

    // 初始化 BLE
    BLEDevice::init("");
    ESP_LOGD(TAG, "BLE: Device initialized, target MAC: %s", bleMacStr.c_str());

    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();

    // 清空默认扫描响应数据
    BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
    pAdvertising->setScanResponseData(oScanResponseData);

    // 清空默认广播数据
    BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
    pAdvertising->setAdvertisementData(oAdvertisementData);

    // 直接底层 API 设置抓取到的原始广播数据
    esp_err_t errRc = ::esp_ble_gap_config_adv_data_raw(bleRaw, 31);
    if (errRc != ESP_OK) {
        ESP_LOGD(TAG, "BLE: esp_ble_gap_config_adv_data_raw failed: %d", errRc);
        bleActive = false;
        return false;
    }

    // 超过 31 字节的作为扫描响应数据
    if (rawMoreThan31) {
        errRc = ::esp_ble_gap_config_scan_rsp_data_raw(bleRawExt, bleRawExtLen);
        if (errRc != ESP_OK) {
            ESP_LOGD(TAG, "BLE: esp_ble_gap_config_scan_rsp_data_raw failed: %d", errRc);
        }
    }

    pAdvertising->start();
    bleActive = true;
    ESP_LOGD(TAG, "BLE: Advertising started successfully");
    ESP_LOGD(TAG, "BLE:     MAC: %s", bleMacStr.c_str());
    ESP_LOGD(TAG, "BLE: Raw data: %d bytes", totalBytes);
    return true;
}

// 停止 BLE 广播
void stopBLE() {
    if (bleActive) {
        BLEDevice::getAdvertising()->stop();
        BLEDevice::deinit(true);
        bleActive = false;
        ESP_LOGD(TAG, "BLE: Stopped");
    }
}

// 停止伪装 WiFi AP，回退到默认管理热点
void stopWiFiAP() {
    server.end();
    WiFi.softAPdisconnect();
    delay(300);
    WiFi.softAP("WIFI MANAGER");
    server.begin();
    enableDNS = true;
    digitalWrite(PIN_LED, HIGH);
    ESP_LOGD(TAG, "WiFi: Fallback to WIFI MANAGER");
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
            esp_err_t set_power_err = esp_wifi_set_max_tx_power(34);
            ESP_LOGD(TAG, "esp_wifi_set_max_tx_power: %s", esp_err_to_name(set_power_err));
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
    if (!LittleFS.begin()) {
        ESP_LOGD(TAG, "Failed to initialize LittleFS");
        return;
    }

    server.serveStatic("/", LittleFS, "/");

    server.on("/", HTTP_GET,
              [](AsyncWebServerRequest *request) { request->send(LittleFS, "/index.html", "text/html"); });

    server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("/"); });

    // ========== WiFi + BLE 配置提交 ==========
    server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
        String _wifi_enable_str;
        String _ssid_str;
        String _password_str;
        String _mac_str;
        String _ble_enable_str;
        String _ble_mac_str;
        String _ble_raw_str;

        for (int i = 0; i < request->params(); i++) {
            AsyncWebParameter *p = request->getParam(i);

            if (p->name() == "wifi_enable")      _wifi_enable_str = p->value();
            if (p->name() == "wifi_ssid")        _ssid_str = p->value();
            if (p->name() == "wifi_password")    _password_str = p->value();
            if (p->name() == "wifi_mac")         _mac_str = p->value();
            if (p->name() == "ble_enable")       _ble_enable_str = p->value();
            if (p->name() == "ble_mac")          _ble_mac_str = p->value();
            if (p->name() == "ble_raw")          _ble_raw_str = p->value();
        }

        bool wifiEnableReq = (_wifi_enable_str == "on" || _wifi_enable_str == "1" || _wifi_enable_str == "true");
        bool bleEnableReq  = (_ble_enable_str  == "on" || _ble_enable_str  == "1" || _ble_enable_str  == "true");

        // ---- WiFi 校验 ----
        if (wifiEnableReq) {
            if (_ssid_str.length() < 1 || _ssid_str.length() > 63) {
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

        // ---- BLE 校验 ----
        if (bleEnableReq) {
            if (_ble_mac_str.length() != 17) {
                request->send(200, "text/plain", "蓝牙MAC地址格式错误(aa:bb:cc:dd:ee:ff)"); return;
            }
            if (_ble_raw_str.length() < 2) {
                request->send(200, "text/plain", "蓝牙广播数据不能为空"); return;
            }
            enableBLE = true;
            bleMacStr = _ble_mac_str;
            bleRawStr = _ble_raw_str;
        } else {
            enableBLE = false;
            stopBLE();
        }

        enableWiFi = wifiEnableReq;

        // ---- 持久化到 NVS ----
        preferences.begin("wifi_config", false);
        preferences.putBool("wifi_enable", enableWiFi);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("mac", macAddress);
        preferences.putBool("ble_enable", enableBLE);
        if (enableBLE) {
            preferences.putString("ble_mac", bleMacStr);
            preferences.putString("ble_raw", bleRawStr);
        } else {
            preferences.remove("ble_mac");
            preferences.remove("ble_raw");
        }
        preferences.end();

        // ---- 执行 WiFi ----
        String resultMsg = "";
        if (enableWiFi) {
            if (createAP()) {
                resultMsg += "✅ WiFi伪装已启动";
            } else {
                stopWiFiAP();
                resultMsg += "⚠️ WiFi创建失败,已回退管理热点";
            }
        } else {
            stopWiFiAP();
            resultMsg += "⏸️ WiFi已关闭";
        }

        // ---- 执行 BLE ----
        if (enableBLE) {
            delay(200);
            if (startBLE()) {
                resultMsg += " | ✅ 蓝牙广播已启动";
            } else {
                resultMsg += " | ⚠️ 蓝牙启动失败";
            }
        } else {
            resultMsg += " | ⏸️ 蓝牙已关闭";
        }

        request->send(200, "text/plain", resultMsg);
    });
    server.begin();
}

void setup() {
    pinMode(PIN_LED, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    digitalWrite(PIN_LED, HIGH);

    // ---- 读取 NVS 中保存的配置 ----
    if (!preferences.begin("wifi_config", false)) {
        ESP_LOGD(TAG, "Failed to initialize preferences");
    }

    enableWiFi = preferences.getBool("wifi_enable", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    macAddress = preferences.getString("mac", "");

    enableBLE = preferences.getBool("ble_enable", false);
    bleMacStr = preferences.getString("ble_mac", "");
    bleRawStr = preferences.getString("ble_raw", "");

    preferences.end();

    // ---- 初始化 WiFi ----
    WiFi.mode(WIFI_AP);

    if (enableWiFi && ssid.length() > 0 && macAddress.length() == 17) {
        if (!createAP()) {
            ESP_LOGD(TAG, "WiFi AP creation failed. Fallback to default");
            WiFi.softAP("WIFI MANAGER");
            enableDNS = true;
        }
    } else {
        ESP_LOGD(TAG, "WiFi disabled or no credentials. Creating default AP");
        WiFi.softAP("WIFI MANAGER");
        enableDNS = true;
    }

    // ---- 启动 Web 服务器和 DNS ----
    initWebServer();
    dns.start(53, "*", WiFi.softAPIP());

    // ---- 启动 BLE (在 WiFi 初始化之后) ----
    if (enableBLE && bleMacStr.length() == 17 && bleRawStr.length() > 0) {
        delay(500);  // 等 WiFi 稳定后再启动 BLE
        if (startBLE()) {
            ESP_LOGD(TAG, "BLE started on boot");
        } else {
            ESP_LOGD(TAG, "BLE start failed on boot");
        }
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