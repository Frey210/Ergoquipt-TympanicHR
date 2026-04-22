# Ergoquipt BLE Health Ecosystem

Repository ini berisi dua firmware embedded berbasis ESP32 untuk ekosistem pemantauan kesehatan Ergoquipt:

- `ergoquipt_hr_band`: wearable HR band berbasis ESP32-S3 + MAX3010x + AMOLED touch display.
- `ergoquipt_tympanic_temp`: node termometer timpani berbasis ESP32-C3 + MLX90614.

Keduanya berjalan sebagai BLE Peripheral/GATT Server dan dirancang untuk dibaca oleh aplikasi mobile sebagai BLE Central.

## Repository Structure

```text
.
|-- README.md
|-- Ergoquipt_BLE_System_Description.txt
|-- ergoquipt_hr_band/
|   |-- platformio.ini
|   |-- lib/
|   |   `-- max3010x_compat/
|   `-- src/
|       |-- main.cpp
|       |-- config.h
|       |-- ble_manager.cpp
|       |-- ble_manager.h
|       |-- sensor_manager.cpp
|       |-- sensor_manager.h
|       |-- ui_manager.cpp
|       `-- ui_manager.h
`-- ergoquipt_tympanic_temp/
    |-- platformio.ini
    `-- src/
        |-- main.cpp
        |-- config.h
        |-- ble_manager.cpp
        |-- ble_manager.h
        |-- sensor_manager.cpp
        `-- sensor_manager.h
```

## System Overview

### 1. HR Band

HR band melakukan tiga fungsi utama secara paralel:

- membaca sinyal optik MAX3010x via I2C,
- menghitung HR, SpO2, RRI, dan HRV,
- menampilkan data ke AMOLED via LVGL dan mengirim snapshot ke BLE.

Arsitektur runtime menggunakan FreeRTOS task:

- `sensorTask`: sampling sensor setiap 10 ms.
- `bleTask`: publish payload BLE setiap 1000 ms.
- `uiTask`: refresh UI setiap 33 ms dengan update konten setiap 1000 ms.

Alur boot:

1. Inisialisasi serial dan mutex I2C.
2. Inisialisasi MAX3010x dan scan I2C.
3. Inisialisasi BLE GATT server.
4. Inisialisasi UI LVGL + AMOLED.
5. Menjalankan task sensor, BLE, dan UI di core ESP32-S3.

### 2. Tympanic Temp

Node tympanic membaca MLX90614 melalui I2C lalu mengirim temperatur objek melalui BLE setiap 1 detik. Firmware ini lebih sederhana dan tidak memakai RTOS task terpisah maupun UI lokal.

## Hardware Summary

| Item | HR Band | Tympanic Temp |
|---|---|---|
| MCU | ESP32-S3 | ESP32-C3 |
| Board target PlatformIO | `esp32-s3-devkitc-1` | `esp32-c3-devkitm-1` |
| Sensor utama | MAX3010x/MAX30102 compatible | MLX90614 |
| Transport sensor | I2C | I2C |
| Display | AMOLED SH8601 + touch FT3168 | Tidak ada |
| BLE role | Peripheral / GATT Server | Peripheral / GATT Server |

## HR Band Firmware Detail

### Sensor Pipeline

Implementasi `ergoquipt_hr_band/src/sensor_manager.cpp` melakukan:

- scan I2C saat startup,
- inisialisasi MAX3010x pada alamat `0x57`,
- pembacaan FIFO sample `IR` dan `Red`,
- smoothing dengan moving average,
- deteksi keberadaan jari berdasarkan ambang `IR`,
- deteksi puncak untuk menghitung `RRI`, `HR`, dan `HRV (RMSSD)`,
- estimasi SpO2 berbasis rasio AC/DC dari kanal merah dan IR,
- pembentukan `status` bitmask untuk UI dan BLE.

Status internal `VitalData` berisi:

- `hr`
- `spo2_x100`
- `rri`
- `hrv`
- `status`

### UI Lokal AMOLED

Implementasi `ergoquipt_hr_band/src/ui_manager.cpp` menampilkan:

- Heart Rate
- SpO2
- R-R Interval
- HRV
- status BLE
- status baterai mock
- status sensor di bagian bawah layar

UI dibangun dengan:

- `Arduino_GFX_Library`
- `lvgl`
- QSPI AMOLED SH8601
- touch controller FT3168

### HR Band Pin Mapping

Pin yang saat ini dipakai firmware:

| Fungsi | Pin |
|---|---:|
| I2C SDA MAX3010x / FT3168 | GPIO15 |
| I2C SCL MAX3010x / FT3168 | GPIO14 |
| Display CS | GPIO12 |
| Display SCK | GPIO11 |
| Display D0 | GPIO4 |
| Display D1 | GPIO5 |
| Display D2 | GPIO6 |
| Display D3 | GPIO7 |
| Touch INT | GPIO21 |

Catatan:

- Firmware menganggap varian hardware AMOLED touch ESP32-S3 yang menggunakan SH8601 + FT3168.
- `board_upload.flash_size` diset ke `16MB`.
- `PSRAM` diaktifkan untuk draw buffer LVGL.

### HR Band BLE Contract

Device name:

- format `Ergoquipt-HR-XXX`
- `XXX` berasal dari 3 digit hex terakhir MAC STA.

UUID:

- Service UUID: `e0020001-7cce-4c2a-9f0b-112233445566`
- Characteristic UUID: `e0020002-7cce-4c2a-9f0b-112233445566`

Properties:

- `READ`
- `NOTIFY`

Security:

- secure bonding (`ESP_LE_AUTH_REQ_SC_BOND`)
- encryption key init/resp diaktifkan

Payload HR band berukuran `12` byte, little-endian:

| Byte | Field | Tipe | Keterangan |
|---:|---|---|---|
| 0..1 | `hr` | `uint16` | bpm |
| 2..3 | `spo2_x100` | `uint16` | SpO2 x100 |
| 4..5 | `rri` | `uint16` | ms |
| 6..7 | `hrv` | `uint16` | ms |
| 8 | `status` | `uint8` | bitmask status |
| 9 | `sequence` | `uint8` | counter publish |
| 10 | reserved | `uint8` | `0x00` |
| 11 | reserved | `uint8` | `0x00` |

HR band status bitmask:

| Bit | Mask | Arti |
|---:|---:|---|
| 0 | `0x01` | vitals valid |
| 1 | `0x02` | sensor error |
| 2 | `0x04` | RRI valid |
| 3 | `0x08` | HRV valid |
| 4 | `0x10` | low battery |

## Tympanic Firmware Detail

Firmware `ergoquipt_tympanic_temp`:

- membaca register object temperature MLX90614 pada alamat `0x5A`,
- memakai pin I2C `SDA=GPIO8` dan `SCL=GPIO9`,
- mengirim payload BLE setiap 1000 ms,
- menyalakan LED koneksi pada `GPIO7` saat connected dan berkedip saat idle.

Device name:

- format `Ergoquipt-TEMP-XXX`

UUID:

- Service UUID: `e0010001-7cce-4c2a-9f0b-112233445566`
- Characteristic UUID: `e0010002-7cce-4c2a-9f0b-112233445566`

Payload tympanic berukuran `4` byte, little-endian:

| Byte | Field | Tipe | Keterangan |
|---:|---|---|---|
| 0..1 | `temperatureX100` | `int16` | suhu Celsius x100 |
| 2 | `status` | `uint8` | bitmask status |
| 3 | `sequence` | `uint8` | counter publish |

Tympanic status bitmask:

| Bit | Mask | Arti |
|---:|---:|---|
| 0 | `0x01` | sensor valid |
| 1 | `0x02` | sensor error |
| 2 | `0x04` | low battery |

## BLE Integration Flow

Untuk aplikasi mobile:

1. Scan perangkat dengan prefix `Ergoquipt-HR-` atau `Ergoquipt-TEMP-`.
2. Connect ke peripheral.
3. Discover service dan characteristic sesuai UUID.
4. Enable notification via CCCD.
5. Lakukan `READ` awal bila perlu.
6. Konsumsi stream data 1 Hz.

## Build

### HR Band

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_hr_band
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" run
```

### Tympanic Temp

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_tympanic_temp
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" run
```

## Upload

### HR Band

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_hr_band
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" run -t upload
```

### Tympanic Temp

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_tympanic_temp
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" run -t upload
```

## Monitor Serial

### HR Band

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_hr_band
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

### Tympanic Temp

```powershell
cd D:\Aerasea\Ergoquipt-HR_Tympanic\ergoquipt_tympanic_temp
& "C:\Users\ASUS TUF\.platformio\penv\Scripts\pio.exe" device monitor -b 115200
```

## Current Notes

- HR band sudah memakai pembacaan sensor nyata, perhitungan HR/SpO2/RRI/HRV, UI AMOLED, dan payload BLE 12 byte.
- Tympanic temp sudah memakai pembacaan MLX90614 nyata dan publish BLE 4 byte.
- Estimasi baterai pada HR band masih berupa mock value.
- Dua byte terakhir payload HR band masih disisakan untuk ekspansi protokol berikutnya.
