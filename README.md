# ESP32 Modbus Gateway 🌐⚡

An **ESP32-based Industrial Modbus Gateway** with a **Web UI**, **MQTT integration**, and **RS485 Modbus RTU support**.  
Designed for **energy meters, temperature sensors, and industrial Modbus devices**.

This project allows you to:
- Configure Modbus registers from a browser
- Read multiple Modbus slaves
- Publish data to MQTT in JSON format
- Manage device settings without reflashing firmware

---

## Project Description

This project is a **software-based web replica of a 4G Modbus Gateway**.

It recreates the **user interface and observable operational behavior** of an industrial Modbus 4G gateway for learning and demonstration purposes. The focus is on understanding gateway configuration, Modbus function handling, and cloud communication workflows from a software perspective.

### Reference Materials

To provide context about the **original commercial 4G Modbus Gateway model**, the following reference files are included:

- 📄 **PDF documentation** of the original 4G Modbus Gateway (for overview and understanding)
- ⚙️ **Windows executable (.exe)** provided by the original gateway vendor (used only as a reference/demo of the actual product)

These reference materials are **not created by this project** and are included **solely for comparison and understanding**.

⚠️ This project does **not include physical gateway hardware** and is **not affiliated with the original gateway manufacturer**.

### 📄 Reference Documentation (Original Product)

- [View PDF – Original 4G Modbus Gateway Manual](./reference/4G User Manual.pdf)

### ⚙️ Reference Software (Original Product)

- [Download EXE – Original 4G Modbus Gateway Demo](./reference/MODBUS_GPRS Config-V223.exe)


---

## ✨ Features

- 📡 **Modbus RTU (RS485)**
  - Supports FC01, FC02, FC03, FC04
  - Multiple slaves
  - Configurable polling interval

- 🌐 **Web Configuration UI**
  - Runs directly on ESP32
  - Modbus register mapping
  - MQTT configuration
  - RS485 serial settings
  - Device (AP) configuration

- ☁️ **MQTT Publisher**
  - Publishes Modbus data as JSON
  - Works with Mosquitto, HiveMQ, AWS IoT, etc.
  - Test publish from UI

- 🕒 **RTC / Time Sync**
  - NTP based time sync
  - Timestamped logs

- 📂 **Config Management**
  - Upload / download `config.json`
  - Upload / download `modbus.json`
  - Changes applied **without reflashing**

- 🔒 **Login Protected UI**
  - Simple authentication
  - Factory reset support

---

## 🧱 System Architecture
<img width="247" height="307" alt="image" src="https://github.com/user-attachments/assets/d2618038-9abe-4751-8b44-3ba8c6cde425" />

---

## 🔌 Hardware Connections

### RS485 (MAX485 / Similar)

| RS485 | ESP32 |
|------|-------|
| RO   | RX (GPIO 26) |
| DI   | TX (GPIO 27) |
| DE+RE | DE (GPIO 25) |
| A+   | Modbus A |
| B-   | Modbus B |

⚠️ Use **termination resistors (120Ω)** at the end of the RS485 bus.

---

## 📁 Project Structure

modbusGateway/ <br />
├── main.ino<br />
├── data/ # LittleFS<br />
│ ├── index.html<br />
│ ├── app.js<br />
│ ├── style.css<br />
│ ├── config.json<br />
│ └── modbus.json<br />
├── README.md<br />
└── LICENSE<br />


---

## 🚀 Getting Started

### 1️⃣ Flash ESP32
- Open `.ino` file in Arduino IDE
- Select **ESP32 Dev Module**
- Upload firmware

### 2️⃣ Upload Web Files
Use **ESP32 LittleFS Upload Tool** to upload:
index.html
app.js
style.css


### 3️⃣ Connect to ESP32
- WiFi AP: `Test-AP`
- Password: `12345678`
- Open browser: `http://192.168.4.1`

---

## ⚙️ Configuration Files

### `config.json`
```json
{
  "ap": {
    "ssid": "Test-AP",
    "pass": "12345678"
  },
  "mqtt": {
    "host": "broker.hivemq.com",
    "port": 1883,
    "clientId": "esp32-gateway",
    "user": "",
    "pass": "",
    "topic": "esp32/modbus/data"
  }
}
```
### `modbus.json`
```json
[
  {
    "slave": 1,
    "func": 3,
    "start": 0,
    "count": 2,
    "poll": 2000,
    "registers": [
      { "index": 0, "name": "voltage", "scale": 0.1 },
      { "index": 1, "name": "current", "scale": 0.01 }
    ]
  }
]
```

## 🛠 Supported Function Codes

| Code | Description |
|-----:|-------------|
| FC01 | Read Coils |
| FC02 | Read Discrete Inputs |
| FC03 | Read Holding Registers |
| FC04 | Read Input Registers |


## 🔐 Default Credentials

Username: admin
Password: admin123

## 📜 License

Licensed under the **Apache License 2.0**.

You may:
- Use this project commercially
- Modify and distribute it
- Integrate it into closed-source products

This license also provides **explicit patent protection**, making it suitable
for industrial and commercial gateway products.

See the `LICENSE` file for full terms.

# 👤 Author

Srri Hari T R
🔗 GitHub: https://github.com/srrihari

🔗 LinkedIn: https://www.linkedin.com/in/srrihari/
