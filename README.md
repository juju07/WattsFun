# WattsFun – Virtual Bike Training App

A Windows C++/Qt 6 desktop app that connects to smart bike trainers and heart rate monitors via **ANT+** (preferred) or **Bluetooth LE** (fallback), displays real-time telemetry, and persists device settings between sessions.

---

## Features

| Feature | Details |
|---|---|
| **Protocol auto-detection** | Tries ANT+ USB stick first; falls back to BLE automatically |
| **Trainer modules** | Resistance (FE-C / FTMS), Cadence (CSC), Power (Cycling Power) |
| **Heart Rate Monitor** | ANT+ profile 120 or BLE Heart Rate Service (0x180D) |
| **Device persistence** | Saves chosen devices to Windows Registry via `QSettings` |
| **Real-time dashboard** | Digital readouts + scrolling 2-minute power/HR chart |
| **Dark theme** | Catppuccin Mocha-inspired colour scheme |

---

## Prerequisites

| Requirement | Version |
|---|---|
| Windows | 10 / 11 (64-bit) |
| MSVC | 2022 (or later) |
| CMake | 3.21+ |
| Qt | 6.5+ (Widgets, Charts) |
| Garmin ANT+ PC SDK | *(optional – enables ANT+ support)* |

> **Qt Charts** is part of the `qt6-charts` module. Make sure it is selected in the Qt installer.

---

## Building

### 1. Clone / open the project

```
cd WattsFun
```

### 2. Configure with CMake

**BLE-only (no ANT+ SDK):**
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
```

**With ANT+ SDK:**
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64" `
      -DANTPLUS_SDK_DIR="C:/ANT+_PC_SDK_3.5"
```

> Replace the paths with your actual Qt and SDK installation directories.

### 3. Build

```powershell
cmake --build build --config Release
```

The `windeployqt` step runs automatically as a post-build action to copy required Qt DLLs.

### 4. Run

```powershell
.\build\Release\WattsFun.exe
```

---

## ANT+ Setup

1. Download the [Garmin ANT+ PC SDK](https://www.thisisant.com/developer/resources/downloads/) (requires free registration).
2. Extract to a folder, e.g. `C:\ANT+_PC_SDK_3.5`.
3. Pass `-DANTPLUS_SDK_DIR="C:/ANT+_PC_SDK_3.5"` to CMake.
4. Plug in your ANT+ USB stick **before** launching the app.

Without the SDK, `AntManager::isAvailable()` returns `false` and the app transparently uses BLE.

---

## BLE Setup

No extra SDK required – uses the Windows Runtime (WinRT) APIs built into Windows 10/11.

Supported GATT profiles:
- **Heart Rate Service** `0x180D`
- **Cycling Power Service** `0x1818`
- **Cycling Speed & Cadence** `0x1816`
- **Fitness Machine Service (FTMS)** `0x1826` – resistance control via Control Point `0x2AD9`

---

## Project Structure

```
WattsFun/
├── CMakeLists.txt
├── README.md
├── resources/
│   └── resources.qrc
└── src/
    ├── main.cpp
    ├── mainwindow.{h,cpp,ui}        ← Application shell + menu
    ├── deviceselectiondialog.{h,cpp,ui}  ← Scan & pick devices
    ├── dashboardwidget.{h,cpp}      ← Real-time tiles + chart
    ├── antmanager.{h,cpp}           ← ANT+ PC SDK wrapper
    ├── blemanager.{h,cpp}           ← WinRT BLE wrapper
    ├── iprotocolmanager.h           ← Common interface (ANT/BLE)
    ├── deviceconfig.{h,cpp}         ← QSettings persistence
    └── trainerdata.h                ← Shared data structs
```

---

## Adding Resistance Control

Call `BleManager::setResistance(deviceId, percent)` (0–100 %) at any time after connecting to an FTMS trainer. The app sends FTMS Control Point opcode `0x04` (Set Target Resistance Level).

For ANT+ FE-C, extend `AntManager` with a `setResistance()` method that writes a **Basic Resistance** control page (page 48, opcode `0x30`) via `ANT_SendBroadcastData`.

---

## License

MIT
