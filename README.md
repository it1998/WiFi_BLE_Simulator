# WiFi / 蓝牙设备模拟

基于 **ESP32-C3** 芯片，通过修改 AP 模式 WiFi MAC 地址 + 模拟 BLE 蓝牙广播，实现 WiFi 与蓝牙设备模拟。

> ✅ WiFi 和蓝牙可**独立开关**，灵活组合使用

---

## 硬件要求

| 项目 | 说明 |
|------|------|
| 主控 | ESP32-C3 开发板（推荐 Seeed Studio XIAO ESP32C3） |
| 引脚 | GPIO 8 → LED 指示灯，GPIO 9 → 按键（内部上拉） |
| 天线 | 需连接 WiFi/蓝牙天线到 IPEX 接口 |
| 供电 | USB Type-C 5V |

---

## 烧录方法

### 1. 修改板型（如需要）

编辑 `platformio.ini` 第 13 行：

```ini
; XIAO ESP32C3 开发板
board = seeed_xiao_esp32c3

; 其他 C3 开发板
; board = esp32-c3-devkitm-1
```

### 2. 编译 & 烧录

**方式一：VS Code + PlatformIO 插件**

| 步骤 | 操作 | 图标 |
|------|------|------|
| 编译固件 | 底部蓝色状态栏 → ✅ Build | |
| 烧录固件 | → Upload | 需先 USB 连接开发板 |
| 上传网页文件 | PlatformIO 面板 → **Upload Filesystem Image** | |

**方式二：命令行**

```bash
pio run --target upload      # 烧录固件
pio run --target uploadfs    # 上传网页文件
pio device monitor           # 查看串口日志
```

> 💡 烧录失败时：按住 BOOT 按钮 → 按一下 RST → 松开 BOOT → 再执行 Upload

---

## 初始配置

烧录完成后：

1. 手机搜索并连接热点 **`WIFI MANAGER`**（开放，无密码）
2. 自动弹出配置页面 `192.168.4.1`（Captive Portal）
3. 分别配置 WiFi 和/或 蓝牙参数

---

## 功能配置

### 📶 WiFi 伪装

在配置页打开「WiFi 伪装」开关，填写：

| 字段 | 格式 | 说明 |
|------|------|------|
| WiFi 名称 | 1-32 字符 | 要模拟的 WiFi SSID |
| WiFi 密码 | ≥8 字符或留空 | 开放网络留空 |
| MAC 地址 | `aa:bb:cc:dd:ee:ff` | 要模拟的设备 MAC |

### 📡 蓝牙模拟

1. 手机安装 **nRF Connect** App
2. 到目标蓝牙设备附近，打开 nRF Connect 扫描
3. 找到信号最强的设备，记下 **MAC 地址**
4. 点击设备 → **Raw** 标签 → 复制完整广播数据
5. 在配置页打开「蓝牙模拟」开关，填入：

| 字段 | 格式 | 示例 |
|------|------|------|
| 蓝牙 MAC | `aa:bb:cc:dd:ee:ff` | nRF Connect 中复制的地址 |
| 广播数据 | `02,01,06,FF,...` | nRF Connect → Raw 标签内容 |

> 最多支持 62 字节，前 31 字节为广播数据，超出部分自动作为扫描响应

---

## 独立开关

WiFi 和蓝牙可**单独开启/关闭**，灵活组合：

| WiFi | BLE | 效果 |
|:----:|:---:|------|
| ✅ | ❌ | 仅广播伪装 WiFi |
| ❌ | ✅ | 仅广播蓝牙信号（WiFi 回退到 WIFI MANAGER 供下次配置） |
| ✅ | ✅ | 同时广播 WiFi + 蓝牙 |

> ⚠️ 关闭 WiFi 后会自动创建默认热点 `WIFI MANAGER`，确保配置页面仍可访问

---

## 按键操作

| 操作 | 功能 |
|------|------|
| 长按 >1 秒 | 清除所有配置（WiFi + 蓝牙），重启回默认模式 |

---

## LED 指示灯

| LED | 含义 |
|:---:|------|
| 亮起 | WiFi 伪装热点创建成功 |
| 熄灭 | 默认模式 / WiFi 已关闭 / 创建失败 |

---

## 参考资料

本项目基于 [Starryccc/DingDing_WiFi](https://github.com/Starryccc/DingDing_WiFi) 二次开发，新增了以下功能：

- 🔵 **BLE 蓝牙模拟** — 原始项目仅支持 WiFi 模拟，本项目增加蓝牙广播模拟
- 🔘 **独立开关** — WiFi 和蓝牙可独立开启/关闭，灵活组合
- 🖥️ **UI 重构** — 配置页面增加蓝牙参数区域、开关式交互
- 🐛 **Bug 修复** — 修复原项目 `wiif_password` 拼写错误导致密码无法提交的问题

---

## 技术细节

- **WiFi MAC 伪装**：`esp_wifi_set_mac(WIFI_IF_AP, ...)` 直接修改 AP 接口 MAC
- **BLE MAC 伪装**：`esp_base_mac_addr_set()` + 偏移计算，适配 ESP32-C3 双 MAC 地址架构
- **BLE 广播注入**：`esp_ble_gap_config_adv_data_raw()` 底层 API 直接注入原始广播数据
- **WiFi 功率**：`esp_wifi_set_max_tx_power(34)` 降低至 ~8dBm，减少发热
- **存储**：Preferences (NVS) 持久化，断电不丢失
- **文件系统**：LittleFS 存放网页文件

---

![image](images/ESP32C3%20Xiao.jpg)
![image](images/setup.png)

---

## 鸣谢

- [Starryccc/DingDing_WiFi](https://github.com/Starryccc/DingDing_WiFi) — 原始 WiFi 模拟项目
- [Seeed Studio XIAO ESP32C3 蓝牙使用指南](https://wiki.seeedstudio.com/cn/XIAO_ESP32C3_Bluetooth_Usage/) — 蓝牙开发文档

---

## 许可与免责声明

本项目仅限个人用户在合法范围内自用。源代码以学习研究为目的开放，**禁止任何形式的商业使用**，包括但不限于代安装、代部署、售卖、付费提供下载链接及其他一切营利行为。

对源代码的任何引用（无论数量多少）均须进行署名。基于本项目衍生的作品**必须同样以开源方式发布**。

使用者因使用本项目所产生的一切后果由使用者自行承担，与作者无关。本声明的最终解释权归项目作者所有。