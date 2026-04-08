# 🚴 WattsFun – Virtual Bike Training App

> A feature-rich Windows desktop app for indoor cycling — connect your smart trainer, ride GPX routes on a live satellite map, follow structured interval workouts, and upload everything to Strava.

Built with **C++ / Qt 6** • Dark themed (Catppuccin Mocha) • ANT+ & Bluetooth LE

---

## ✨ Highlights

| | Feature | Description |
|---|---|---|
| 🗺️ | **Map Ride Mode** | Load GPX routes and ride them on a live satellite map with heading-up compass rotation, elevation profiles, and automatic grade simulation |
| 📊 | **Interval Training** | Create custom ERG/Grade programs with a visual editor, or use built-in workouts — steps auto-advance with live countdown |
| 🏋️ | **Free Ride** | Manual ERG watt target or grade simulation with +/− controls |
| 📈 | **Advanced Analytics** | Normalized Power, TSS, IF, W/kg, power bests (5s/1m/5m/20m), 7-zone Coggan model, time-in-zone breakdown |
| 🔗 | **Strava Upload** | One-click OAuth upload with auto token refresh — opens your activity page when done |
| 💾 | **TCX Export** | Garmin-compatible TCX files with power extensions — works with Strava, TrainingPeaks, Garmin Connect |
| 📡 | **Dual Protocol** | ANT+ (preferred) with automatic BLE fallback — no config needed |
| 📸 | **Screenshots** | Capture your dashboard mid-ride, auto-matched to workouts during Strava upload |

---

## 🖥️ Three Training Modes

### 🏋️ Free Ride
Manually set your ERG watt target (+/− 10W) or grade simulation (+/− 1%). Great for warmups or unstructured rides.

### ⏱️ Interval Training
Select from your training library or create custom programs with the workout editor. Each step defines a duration, mode (ERG watts or grade %), and target value. The dashboard shows:
- Current step name and countdown timer
- Target value and progress bar
- Color-coded power zone preview (Z1–Z7 based on your FTP)
- Visual workout profile with proportional bar chart

Ships with two built-in programs: **ERG Power Intervals** and **Hill Climb Intervals**.

### 🗺️ Map Ride
Load any GPX file and ride along the route on a live satellite map:
- **Esri satellite imagery** with optional OpenStreetMap toggle
- **Heading-up rotation** (smoothed 120m look-ahead) or north-up mode
- **North arrow** compass indicator, zoom controls, drag-to-pan
- **Green start marker** and **checkered finish flag**
- **Blue trace** for ridden portion, **red bike marker** with glow
- **Full-route elevation profile** with gradient fill and position cursor
- **300m sliding-window elevation zoom** (±150m around rider)
- **Automatic grade simulation** from GPX elevation data with triangular-kernel smoothing
- **Grade Effect slider** (50–100%) to scale trainer resistance
- **Route management**: import GPX files, sort by name/distance/elevation
- **Route looping**: wraps back to start when you exceed total distance

---

## 📊 Real-Time Dashboard

| Metric | Details |
|---|---|
| ⚡ Power | Live watts, avg watts, W/kg, power zone badge |
| ❤️ Heart Rate | Live HR, avg HR |
| 🔄 Cadence | Live RPM, avg RPM |
| 🚀 Speed | Physics-based model (drag, rolling resistance, grade, chain friction) |
| 📏 Distance | Cumulative with elevation gain |
| ⏱️ Duration | Active ride time with pause/resume |

**Two view modes:**
- 📈 **Chart View** — 2-minute scrolling dual-axis line chart (power + HR, auto-scaling)
- 🎯 **Dials View** — Analog gauge dials with color-coded zone arcs and animated needles

---

## 📈 Power Analytics

- **Normalized Power (NP)** — 30-second rolling average, 4th power method (Coggan)
- **Intensity Factor (IF)** — NP / FTP
- **Training Stress Score (TSS)** — computed from NP, IF, and duration
- **Power Bests** — rolling max for 5s, 1min, 5min, 20min windows
- **7-Zone Coggan Model** — Recovery → Endurance → Tempo → Threshold → VO2max → Anaerobic → Neuromuscular
- **Time-in-Zone** — color-coded stacked bar with per-zone minutes

---

## 💾 Workout History

Every ride is auto-saved with per-second sample data. The **Workouts** tab shows:
- Ride list (newest first) with summary
- **16 metric stat cards**: Avg/Max Power, Cadence, HR, Speed, Duration, Distance, Elevation, NP, IF, TSS, Best 5s/1m/5m/20m
- **Time-in-Zone stacked bar** (Z1–Z7 with legend)
- **Post-ride map** (satellite imagery with route trace for GPS workouts)
- **Gauge dials** (avg power/HR for non-GPS workouts)
- **TCX Export** and **Delete** buttons per workout

---

## 🔗 Strava Integration

- Full **OAuth 2.0** browser-based login
- **Automatic token refresh** (persisted across sessions)
- **One-click upload** of TCX files
- Auto-opens your Strava activity page after upload
- Matches **screenshots** taken during the ride window
- Requires `Qt6::NetworkAuth` (conditionally compiled with `STRAVA_ENABLED`)

Configure via **Strava → Connect to Strava** menu → enter your Client ID and Client Secret.

---

## 👤 Cyclist Profile

Set your **weight (kg)** and **FTP (watts)** in the profile settings. These affect:
- W/kg power-to-weight display
- Power zone thresholds (Z1–Z7)
- Physics-based speed calculation
- Interval workout zone coloring
- TSS / IF calculations

---

## 📡 Device Support

### Protocol Auto-Detection
Tries ANT+ USB stick first → falls back to BLE automatically. No configuration needed.

### ANT+ Profiles
| Profile | Use |
|---|---|
| FE-C | Smart trainer control + data |
| Cycling Power | Power meter |
| Speed & Cadence | Cadence sensor |
| Heart Rate (profile 120) | HR monitor |

### BLE GATT Services
| Service | UUID | Use |
|---|---|---|
| Heart Rate | `0x180D` | HR monitor |
| Cycling Power | `0x1818` | Power meter |
| Cycling Speed & Cadence | `0x1816` | Cadence sensor |
| Fitness Machine (FTMS) | `0x1826` | Smart trainer control |

### Resistance Control
- **FTMS**: Control Point opcode `0x04` (Set Target Resistance Level)
- **ANT+ FE-C**: Basic Resistance control page (page 48)

---

## 🏗️ Prerequisites

| Requirement | Version |
|---|---|
| Windows | 10 / 11 (64-bit) |
| MSVC | 2022 (or later) |
| CMake | 3.21+ |
| Qt | 6.5+ (Widgets, Charts, Network) |
| Qt NetworkAuth | *(optional — enables Strava upload)* |
| Garmin ANT+ PC SDK | *(optional — enables ANT+ support)* |

> **Qt Charts** is part of the `qt6-charts` module. **Qt NetworkAuth** is `qt6-networkauth`. Make sure both are selected in the Qt installer.

---

## 🔨 Building

### 1. Clone the repository

```bash
git clone https://github.com/juju07/WattsFun.git
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

## 📁 ANT+ Setup

1. Download the [Garmin ANT+ PC SDK](https://www.thisisant.com/developer/resources/downloads/) (requires free registration).
2. Extract to a folder, e.g. `C:\ANT+_PC_SDK_3.5`.
3. Pass `-DANTPLUS_SDK_DIR="C:/ANT+_PC_SDK_3.5"` to CMake.
4. Plug in your ANT+ USB stick **before** launching the app.

Without the SDK, the app transparently uses BLE only.

---

## 📂 Project Structure

```
WattsFun/
├── CMakeLists.txt                        Build configuration + CPack installer
├── LICENSE                               MIT License
├── README.md
├── html/
│   └── index.html                        Documentation website (Catppuccin themed)
├── resources/
│   ├── resources.qrc                     Qt resource file
│   ├── WattsFun.{ico,png}               App icons
│   ├── arrow-{up,down}.svg              UI icons
│   └── routes/
│       └── sample_route.gpx             Built-in demo route
└── src/
    ├── main.cpp                          Entry point
    ├── mainwindow.{h,cpp,ui}            App shell, menus, workout history
    ├── dashboardwidget.{h,cpp}          Dashboard, map, intervals, analytics
    ├── deviceselectiondialog.{h,cpp,ui} Device scanner & picker
    ├── workouteditordialog.{h,cpp}      Interval program editor
    ├── antmanager.{h,cpp}               ANT+ PC SDK wrapper
    ├── blemanager.{h,cpp}               WinRT BLE wrapper
    ├── iprotocolmanager.h               Common ANT/BLE interface
    ├── deviceconfig.{h,cpp}             QSettings persistence + cyclist profile
    ├── trainerdata.{h,cpp}              Data structs + physics model
    ├── traininglibrary.{h,cpp}          Interval program persistence
    ├── tcxexporter.{h,cpp}              TCX file generation
    └── stravauploader.{h,cpp}           Strava OAuth + upload
```

---

## 📦 Installer

WattsFun includes a **CPack/NSIS** installer configuration for creating a Windows installer with Start Menu shortcuts and bundled Qt DLLs.

```powershell
cmake --build build --config Release --target PACKAGE
```

---

## 📄 License

[MIT](LICENSE)
