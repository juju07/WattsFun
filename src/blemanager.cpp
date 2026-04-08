// blemanager.cpp
// WinRT BLE implementation using Windows::Devices::Bluetooth LE APIs.
// Requires: /await compiler flag, linking windowsapp.lib + runtimeobject.lib
// ──────────────────────────────────────────────────────────────────────────────

#include "blemanager.h"
#include "trainerdata.h"
#include <QDebug>
#include <QThread>
#include <QEventLoop>
#include <vector>
#include <chrono>

// ── WinRT headers ─────────────────────────────────────────────────────────────
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt;
using namespace Windows::Devices::Bluetooth;
using namespace Windows::Devices::Bluetooth::Advertisement;
using namespace Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace Windows::Storage::Streams;
using namespace Windows::Foundation;

// ── GATT UUIDs ────────────────────────────────────────────────────────────────
namespace Uuid {
    // Services
    static const winrt::guid HeartRate          { 0x0000180D,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid CyclingPower       { 0x00001818,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid CyclingSpeedCad    { 0x00001816,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid FitnessMachine     { 0x00001826,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    // Characteristics
    static const winrt::guid HrMeasurement      { 0x00002A37,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid PowerMeasurement   { 0x00002A63,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid CscMeasurement     { 0x00002A5B,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid IndoorBikeData     { 0x00002AD2,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
    static const winrt::guid FtmsControlPoint   { 0x00002AD9,0,0x1000,{0x80,0x00,0x00,0x80,0x5F,0x9B,0x34,0xFB} };
}

// ──────────────────────────────────────────────────────────────────────────────

struct BleDeviceEntry {
    uint64_t                   address = 0;
    BluetoothLEDevice          device  = nullptr;
    QString                    name;
    bool                       isTrainer = false;
    bool                       isHrm     = false;
    bool                       hasTrainerService = false;  // Track if we've subscribed to a trainer service
    
    // Cadence calculation state
    uint16_t                   lastCumulativeRevs = 0;
    uint16_t                   lastEventTime = 0;
    bool                       hasPreviousCadenceData = false;

    std::vector<GattCharacteristic> subscribedCharacteristics;
    std::vector<winrt::event_token> valueChangedTokens;
};

struct BleManager::Impl {
    BluetoothLEAdvertisementWatcher watcher = nullptr;
    QMap<uint64_t, BleDeviceEntry>  found;   // address → entry (scan results)
    QMap<QString,  BleDeviceEntry>  connected;
    BleManager *q = nullptr;
    double riderWeightKg = 75.0;  // set from CyclistProfile
    double currentGrade  = 0.0;   // current road grade (%) for speed model
    double currentSpeedMps = 0.0; // running speed state for inertial model
    std::chrono::steady_clock::time_point lastSpeedTs = std::chrono::steady_clock::now();

    // ── watcher callback ──────────────────────────────────────────────────────
    void onAdvertisementReceived(BluetoothLEAdvertisementWatcher const&,
                                 BluetoothLEAdvertisementReceivedEventArgs const& args)
    {
        uint64_t address = args.BluetoothAddress();
        if (found.contains(address)) return;   // already reported

        auto advName = args.Advertisement().LocalName();
        QString name = QString::fromStdWString(advName.c_str());

        // Heuristic: check advertised service UUIDs
        bool trainer = false, hrm = false;
        for (const auto &svcGuid : args.Advertisement().ServiceUuids()) {
            if (svcGuid == Uuid::CyclingPower    ||
                svcGuid == Uuid::CyclingSpeedCad ||
                svcGuid == Uuid::FitnessMachine)
                trainer = true;
            if (svcGuid == Uuid::HeartRate)
                hrm = true;
        }
        if (!trainer && !hrm) return;   // not an interesting device

        BleDeviceEntry entry;
        entry.address   = address;
        entry.name      = name.isEmpty() ? QString("BLE 0x%1").arg(address, 0, 16) : name;
        entry.isTrainer = trainer;
        entry.isHrm     = hrm;
        found.insert(address, entry);

        QString id   = QString("BLE-%1").arg(address);
        QString type = (trainer && hrm) ? "Trainer/HRM" : (trainer ? "Trainer" : "HRM");
        DeviceInfo info { id, entry.name, type, DataSource::BLE };
        QMetaObject::invokeMethod(q, [this, info]() { emit q->deviceFound(info); },
                                  Qt::QueuedConnection);
    }

    // ── subscribe to a notify characteristic ─────────────────────────────────
    void subscribeCharacteristic(const QString &deviceId,
                                 GattCharacteristic const& ch,
                                 std::function<void(const uint8_t*, size_t)> handler)
    {
        try {
            qDebug() << "[BLE] Setting up subscription for characteristic...";

            auto desc = ch.GetDescriptorsForUuidAsync(
                            GattDescriptorUuids::ClientCharacteristicConfiguration())
                          .get();
            if (desc.Status() != GattCommunicationStatus::Success) {
                qDebug() << "[BLE] Failed to get CCCD descriptor";
                return;
            }

            auto properties = ch.CharacteristicProperties();
            auto hasNotify = (properties & GattCharacteristicProperties::Notify) != GattCharacteristicProperties::None;
            auto hasIndicate = (properties & GattCharacteristicProperties::Indicate) != GattCharacteristicProperties::None;
            GattClientCharacteristicConfigurationDescriptorValue desiredValue =
                hasNotify ? GattClientCharacteristicConfigurationDescriptorValue::Notify :
                (hasIndicate ? GattClientCharacteristicConfigurationDescriptorValue::Indicate :
                               GattClientCharacteristicConfigurationDescriptorValue::Notify);

            auto result = ch.WriteClientCharacteristicConfigurationDescriptorAsync(desiredValue).get();
            if (result != GattCommunicationStatus::Success) {
                qDebug() << "[BLE] Failed to enable notifications/indications";
                return;
            }

            qDebug() << "[BLE] Notifications/indications enabled, registering ValueChanged handler...";

            auto token = ch.ValueChanged([handler](GattCharacteristic const&,
                                      GattValueChangedEventArgs const& args)
            {
                try {
                    auto reader = DataReader::FromBuffer(args.CharacteristicValue());
                    size_t len  = reader.UnconsumedBufferLength();
                    if (len > 0) {
                        std::vector<uint8_t> buf(len);
                        reader.ReadBytes(buf);
                        qDebug() << "[BLE] Callback value changed" << len << "bytes:" << QString::fromStdString(std::string(reinterpret_cast<char*>(buf.data()), len));
                        handler(buf.data(), len);
                    }
                } catch (const std::exception &e) {
                    qDebug() << "[BLE] Callback error:" << e.what();
                } catch (...) {
                    qDebug() << "[BLE] Callback unknown error";
                }
            });

            qDebug() << "[BLE] ValueChanged handler registered successfully";

            // Keep characteristic object + token alive for this connection.
            auto it = connected.find(deviceId);
            if (it != connected.end()) {
                it.value().subscribedCharacteristics.push_back(ch);
                it.value().valueChangedTokens.push_back(token);
            }

        } catch (const std::exception &e) {
            qDebug() << "[BLE] subscribeCharacteristic exception:" << e.what();
        } catch (...) {
            qDebug() << "[BLE] subscribeCharacteristic unknown error";
        }
    }
};

// ──────────────────────────────────────────────────────────────────────────────

BleManager::BleManager(QObject *parent)
    : IProtocolManager(parent)
    , m_impl(new Impl())
{
    // WinRT initialization is done in the thread context where BLE IO is used.
    m_impl->q = this;
}

BleManager::~BleManager()
{
    stopScan();
    delete m_impl;
}

bool BleManager::isAvailable() const
{
    try {
        winrt::init_apartment();
    } catch (const winrt::hresult_error &e) {
        // If COM is already initialized on this thread, we can continue.
        qDebug() << "[BLE] isAvailable: WinRT thread mode already set:" << QString::fromStdWString(e.message().c_str());
    } catch (...) {
        qDebug() << "[BLE] isAvailable: Unknown WinRT init error";
    }

    // Probe Bluetooth radio availability synchronously
    try {
        qDebug() << "[BLE] Checking availability...";
        auto adapter = BluetoothAdapter::GetDefaultAsync().get();
        if (!adapter) {
            qDebug() << "[BLE] No Bluetooth adapter found";
            return false;
        }
        // Adapter present — check radio is actually ON (Windows can toggle it off)
        auto radio = adapter.GetRadioAsync().get();
        if (!radio) {
            qDebug() << "[BLE] No radio found for adapter";
            return false;
        }
        bool on = (radio.State() == winrt::Windows::Devices::Radios::RadioState::On);
        qDebug() << "[BLE] Available:" << on;
        return on;
    } catch (const winrt::hresult_error &e) {
        qDebug() << "[BLE] WinRT error:" << QString::fromStdWString(e.message().c_str());
        return false;
    } catch (...) {
        qDebug() << "[BLE] Exception during adapter check";
        return false;
    }
}

void BleManager::startScan()
{
    // Disconnect all connected devices – active GATT connections block discovery
    const QStringList ids = m_impl->connected.keys();
    for (const QString &id : ids)
        disconnectDevice(id);

    m_impl->found.clear();
    m_impl->watcher = BluetoothLEAdvertisementWatcher();
    m_impl->watcher.ScanningMode(BluetoothLEScanningMode::Active);

    m_impl->watcher.Received(
        [this](BluetoothLEAdvertisementWatcher const& w,
               BluetoothLEAdvertisementReceivedEventArgs const& args) {
            m_impl->onAdvertisementReceived(w, args);
        });

    m_impl->watcher.Stopped(
        [this](BluetoothLEAdvertisementWatcher const&,
               BluetoothLEAdvertisementWatcherStoppedEventArgs const&) {
            QMetaObject::invokeMethod(this, [this]() { emit scanFinished(); },
                                      Qt::QueuedConnection);
        });

    try {
        m_impl->watcher.Start();
        qDebug() << "[BLE] Scan started";
    } catch (const winrt::hresult_error &e) {
        qDebug() << "[BLE] watcher.Start() failed:" << QString::fromStdWString(e.message().c_str());
        emit errorOccurred(tr("Bluetooth scan failed: radio may be off. (%1)")
                           .arg(e.code().value));
        emit scanFinished();
    } catch (...) {
        qDebug() << "[BLE] watcher.Start() unknown error";
        emit errorOccurred(tr("Bluetooth scan failed unexpectedly."));
        emit scanFinished();
    }
}

void BleManager::stopScan()
{
    if (m_impl->watcher && m_impl->watcher.Status() == BluetoothLEAdvertisementWatcherStatus::Started)
        m_impl->watcher.Stop();
}

void BleManager::connectDevice(const QString &deviceId)
{
    qDebug() << "[BLE] connectDevice called for:" << deviceId;

    bool ok = false;
    uint64_t addr = deviceId.mid(4).toULongLong(&ok);
    if (!ok) {
        qDebug() << "[BLE] Invalid device ID format";
        emit errorOccurred(tr("Invalid BLE device id: %1").arg(deviceId));
        return;
    }

    QThread *thread = QThread::create([this, deviceId, addr]() {
        qDebug() << "[BLE:Thread] Starting connection for:" << deviceId;

        // Check if already connected
        if (m_impl->connected.contains(deviceId)) {
            qDebug() << "[BLE:Thread] Already connected to" << deviceId << ", skipping";
            return;
        }

        bool apartmentInit = false;
        try {
            winrt::init_apartment();
            apartmentInit = true;
        } catch (const winrt::hresult_error &e) {
            qDebug() << "[BLE:Thread] WinRT init already done on this thread:" << QString::fromStdWString(e.message().c_str());
        } catch (...) {
            qDebug() << "[BLE:Thread] Unknown error initializing WinRT in thread";
        }

        struct AtExit { bool doUninit; ~AtExit() { if (doUninit) winrt::uninit_apartment(); } } atExit{apartmentInit};

        try {
            auto device = BluetoothLEDevice::FromBluetoothAddressAsync(addr).get();
            if (!device) {
                qDebug() << "[BLE:Thread] Could not create device";
                QMetaObject::invokeMethod(this, [this, deviceId]() {
                    emit errorOccurred(tr("Could not create BLE device for %1").arg(deviceId));
                }, Qt::QueuedConnection);
                return;
            }

            BleDeviceEntry entry = m_impl->found.value(addr);
            entry.device = device;
            entry.subscribedCharacteristics.clear();
            entry.valueChangedTokens.clear();
            entry.hasTrainerService = false;  // Reset for new connection
            entry.lastCumulativeRevs = 0;
            entry.lastEventTime = 0;
            entry.hasPreviousCadenceData = false;
            m_impl->connected.insert(deviceId, entry);

            auto svcResult = device.GetGattServicesAsync().get();
            qDebug() << "[BLE:Thread] Got" << svcResult.Services().Size() << "services for" << deviceId;
            if (svcResult.Status() != GattCommunicationStatus::Success) {
                qDebug() << "[BLE:Thread] GATT service discovery failed";
                QMetaObject::invokeMethod(this, [this, deviceId, entry]() {
                    emit errorOccurred(tr("GATT service discovery failed for %1").arg(entry.name));
                }, Qt::QueuedConnection);
                return;
            }

            qDebug() << "[BLE:Thread] Found" << svcResult.Services().Size() << "services";
            for (auto svc : svcResult.Services()) {
                auto svcUuid = svc.Uuid();
                auto svcUuidH = winrt::to_hstring(svcUuid);
                QString svcUuidQ = QString::fromStdWString(std::wstring(svcUuidH.c_str(), svcUuidH.size()));
                qDebug() << "[BLE:Thread] Processing service" << svcUuidQ << "hasTrainerService=" << entry.hasTrainerService;

                if (svcUuid == Uuid::HeartRate) {
                    qDebug() << "[BLE:Thread] Subscribing to HeartRate";
                    auto charResult = svc.GetCharacteristicsForUuidAsync(Uuid::HrMeasurement).get();
                    if (charResult.Status() == GattCommunicationStatus::Success &&
                        charResult.Characteristics().Size() > 0) {
                        m_impl->subscribeCharacteristic(
                            deviceId,
                            charResult.Characteristics().GetAt(0),
                            [this](const uint8_t *d, size_t len) {
                                if (len < 2) return;
                                HrmData hr;
                                bool is16bit = (d[0] & 0x01) != 0;
                                hr.heartRateBpm = is16bit
                                    ? static_cast<int>(d[1] | (d[2] << 8))
                                    : static_cast<int>(d[1]);
                                hr.valid = hr.heartRateBpm > 0;
                                QMetaObject::invokeMethod(this, [this, hr]() {
                                    emit hrmDataUpdated(hr);
                                }, Qt::QueuedConnection);
                            });
                    }
                }

                if (svcUuid == Uuid::CyclingPower && !entry.hasTrainerService) {
                    entry.hasTrainerService = true; // Set immediately to prevent lower priority services
                    qDebug() << "[BLE:Thread] Subscribing to CyclingPower (priority over FTMS/CSC)";
                    auto charResult = svc.GetCharacteristicsForUuidAsync(Uuid::PowerMeasurement).get();
                    if (charResult.Status() == GattCommunicationStatus::Success &&
                        charResult.Characteristics().Size() > 0) {
                        qDebug() << "[BLE:Thread] CyclingPower subscription successful";
                        m_impl->subscribeCharacteristic(
                            deviceId,
                            charResult.Characteristics().GetAt(0),
                            [this, deviceId](const uint8_t *d, size_t len) {
                                if (len < 4) return;
                                uint16_t flags = static_cast<uint16_t>(d[0] | (d[1] << 8));
                                uint16_t instantPower = static_cast<uint16_t>(d[2] | (d[3] << 8));
                                TrainerData td;
                                td.powerWatts = instantPower;
                                td.hasPower = true;
                                // Inertial speed: Euler integration of equation of motion
                                {
                                    const auto now = std::chrono::steady_clock::now();
                                    const double rawDt = std::chrono::duration<double>(now - m_impl->lastSpeedTs).count();
                                    const double dt = rawDt < 0.05 ? 0.05 : (rawDt > 2.0 ? 2.0 : rawDt);
                                    m_impl->lastSpeedTs = now;
                                    td.speedKph = applySpeedInertia(m_impl->currentSpeedMps, td.powerWatts,
                                                                    m_impl->riderWeightKg + 10.0,
                                                                    m_impl->currentGrade, dt);
                                    m_impl->currentSpeedMps = td.speedKph / 3.6;
                                }
                                qDebug() << "[BLE:CyclingPower] Raw data len=" << len << "flags=0x" << Qt::hex << flags 
                                         << "power=" << instantPower;
                                
                                size_t offset = 4;  // Start after power field
                                
                                // Optional fields based on flags
                                if ((flags & 0x0001) && len >= offset + 1) { // Pedal Power Balance
                                    offset += 1;
                                }
                                if ((flags & 0x0004) && len >= offset + 2) { // Accumulated Torque
                                    offset += 2;
                                }
                                if ((flags & 0x0010) && len >= offset + 6) { // Wheel Revolution Data
                                    offset += 6;
                                }
                                if ((flags & 0x0020) && len >= offset + 4) { // Crank Revolution Data (cadence)
                                    uint16_t cumulativeRevs = static_cast<uint16_t>(d[offset] | (d[offset+1] << 8));
                                    uint16_t eventTime = static_cast<uint16_t>(d[offset+2] | (d[offset+3] << 8));
                                    
                                    // Calculate cadence from crank data
                                    auto &entry = m_impl->connected[deviceId];
                                    if (entry.hasPreviousCadenceData) {
                                        // Calculate revolutions per minute
                                        int32_t revsDiff = static_cast<int32_t>(cumulativeRevs) - entry.lastCumulativeRevs;
                                        int32_t timeDiff = static_cast<int32_t>(eventTime) - entry.lastEventTime;
                                        
                                        // Handle 16-bit counter wraparound
                                        if (revsDiff < 0) revsDiff += 65536;
                                        if (timeDiff < 0) timeDiff += 65536;
                                        
                                        if (timeDiff > 0) {
                                            // Time is in 1/1024 second units, convert to minutes
                                            double timeMinutes = timeDiff / (1024.0 * 60.0);
                                            td.cadenceRpm = revsDiff / timeMinutes;
                                            td.hasCadence = true;
                                            qDebug() << "[BLE:CyclingPower] Cadence calc: revsDiff=" << revsDiff 
                                                     << "timeDiff=" << timeDiff << "rpm=" << td.cadenceRpm;
                                        }
                                    }
                                    
                                    // Update stored values
                                    entry.lastCumulativeRevs = cumulativeRevs;
                                    entry.lastEventTime = eventTime;
                                    entry.hasPreviousCadenceData = true;
                                }
                                QMetaObject::invokeMethod(this, [this, td]() {
                                    emit trainerDataUpdated(td);
                                }, Qt::QueuedConnection);
                            });
                    } else {
                        entry.hasTrainerService = false; // Reset if failed to get characteristic
                    }
                }
                else if (svcUuid == Uuid::CyclingPower && entry.hasTrainerService) {
                    qDebug() << "[BLE:Thread] Skipping CyclingPower - already have trainer service";
                }
                else if (svcUuid == Uuid::CyclingSpeedCad && !entry.hasTrainerService) {
                    entry.hasTrainerService = true; // Set immediately to prevent duplicate subscriptions
                    qDebug() << "[BLE:Thread] Subscribing to CyclingSpeedCad (CSC fallback)";
                    auto charResult = svc.GetCharacteristicsForUuidAsync(Uuid::CscMeasurement).get();
                    if (charResult.Status() == GattCommunicationStatus::Success &&
                        charResult.Characteristics().Size() > 0) {
                        qDebug() << "[BLE:Thread] CSC subscription successful";
                        m_impl->subscribeCharacteristic(
                            deviceId,
                            charResult.Characteristics().GetAt(0),
                            [this, deviceId](const uint8_t *d, size_t len) {
                                if (len < 1) return;
                                uint8_t flags = d[0];
                                TrainerData td;
                                
                                size_t offset = 1;
                                if ((flags & 0x01) && len >= offset + 4) { // Wheel Revolution Data Present
                                    // Skip wheel data for now
                                    offset += 4;
                                }
                                if ((flags & 0x02) && len >= offset + 2) { // Crank Revolution Data Present
                                    uint16_t cumulativeRevs = static_cast<uint16_t>(d[offset] | (d[offset+1] << 8));
                                    uint16_t eventTime = static_cast<uint16_t>(d[offset+2] | (d[offset+3] << 8));
                                    
                                    // Calculate cadence from CSC data
                                    auto &entry = m_impl->connected[deviceId];
                                    if (entry.hasPreviousCadenceData) {
                                        int32_t revsDiff = static_cast<int32_t>(cumulativeRevs) - entry.lastCumulativeRevs;
                                        int32_t timeDiff = static_cast<int32_t>(eventTime) - entry.lastEventTime;
                                        
                                        // Handle 16-bit counter wraparound
                                        if (revsDiff < 0) revsDiff += 65536;
                                        if (timeDiff < 0) timeDiff += 65536;
                                        
                                        if (timeDiff > 0) {
                                            // Time is in 1/1024 second units, convert to minutes
                                            double timeMinutes = timeDiff / (1024.0 * 60.0);
                                            td.cadenceRpm = revsDiff / timeMinutes;
                                            td.hasCadence = true;
                                            qDebug() << "[BLE:CSC] Cadence calc: revsDiff=" << revsDiff 
                                                     << "timeDiff=" << timeDiff << "rpm=" << td.cadenceRpm;
                                        }
                                    }
                                    
                                    // Update stored values
                                    entry.lastCumulativeRevs = cumulativeRevs;
                                    entry.lastEventTime = eventTime;
                                    entry.hasPreviousCadenceData = true;
                                }
                                QMetaObject::invokeMethod(this, [this, td]() {
                                    emit trainerDataUpdated(td);
                                }, Qt::QueuedConnection);
                            });
                    } else {
                        entry.hasTrainerService = false; // Reset if failed to get characteristic
                    }
                }
                else if (svcUuid == Uuid::CyclingSpeedCad && entry.hasTrainerService) {
                    qDebug() << "[BLE:Thread] Skipping CSC - already have trainer service";
                }
                else if (svcUuid == Uuid::FitnessMachine && !entry.hasTrainerService) {
                    entry.hasTrainerService = true; // Set immediately to prevent duplicate subscriptions
                    qDebug() << "[BLE:Thread] Subscribing to FitnessMachine (FTMS fallback)";
                    auto charResult = svc.GetCharacteristicsForUuidAsync(Uuid::IndoorBikeData).get();
                    if (charResult.Status() == GattCommunicationStatus::Success &&
                        charResult.Characteristics().Size() > 0) {
                        qDebug() << "[BLE:Thread] FTMS subscription successful";
                        m_impl->subscribeCharacteristic(
                            deviceId,
                            charResult.Characteristics().GetAt(0),
                            [this](const uint8_t *d, size_t len) {
                                if (len < 4) return;
                                uint16_t flags = static_cast<uint16_t>(d[0] | (d[1] << 8));
                                TrainerData td;
                                size_t offset = 2;
                                if ((flags & 0x0001) && len >= offset + 2) {
                                    // Speed field is present but will be calculated from power
                                    offset += 2;
                                }
                                if ((flags & 0x0004) && len >= offset + 2) {
                                    td.cadenceRpm = (d[offset] | (d[offset+1] << 8)) * 0.5;
                                    td.hasCadence = true;
                                    offset += 2;
                                }
                                if ((flags & 0x0020) && len >= offset + 2) {
                                    td.resistancePct = d[offset] | (d[offset+1] << 8);
                                    td.hasResistance = true;
                                    offset += 2;
                                }
                                if ((flags & 0x0040) && len >= offset + 2) {
                                    td.powerWatts = static_cast<int16_t>(d[offset] | (d[offset+1] << 8));
                                    td.hasPower = true;
                                    // Inertial speed: Euler integration of equation of motion
                                    {
                                        const auto now = std::chrono::steady_clock::now();
                                        const double rawDt = std::chrono::duration<double>(now - m_impl->lastSpeedTs).count();
                                        const double dt = rawDt < 0.05 ? 0.05 : (rawDt > 2.0 ? 2.0 : rawDt);
                                        m_impl->lastSpeedTs = now;
                                        td.speedKph = applySpeedInertia(m_impl->currentSpeedMps, td.powerWatts,
                                                                        m_impl->riderWeightKg + 10.0,
                                                                        m_impl->currentGrade, dt);
                                        m_impl->currentSpeedMps = td.speedKph / 3.6;
                                    }
                                    offset += 2;
                                }
                                QMetaObject::invokeMethod(this, [this, td]() {
                                    emit trainerDataUpdated(td);
                                }, Qt::QueuedConnection);
                            });
                    } else {
                        entry.hasTrainerService = false; // Reset if failed to get characteristic
                    }
                }
                else if (svcUuid == Uuid::FitnessMachine && entry.hasTrainerService) {
                    qDebug() << "[BLE:Thread] Skipping FTMS - already have trainer service";
                }
            }

            qDebug() << "[BLE:Thread] Successfully connected:" << deviceId;
            QMetaObject::invokeMethod(this, [this, deviceId]() {
                emit deviceConnected(deviceId);
            }, Qt::QueuedConnection);

        } catch (const winrt::hresult_error &e) {
            qDebug() << "[BLE:Thread] WinRT error:" << QString::fromStdWString(e.message().c_str());
            QMetaObject::invokeMethod(this, [this, e]() {
                emit errorOccurred(QString::fromStdWString(e.message().c_str()));
            }, Qt::QueuedConnection);
        } catch (const std::exception &ex) {
            qDebug() << "[BLE:Thread] std::exception:" << ex.what();
            QMetaObject::invokeMethod(this, [this, ex]() {
                emit errorOccurred(QString::fromStdString(ex.what()));
            }, Qt::QueuedConnection);
        } catch (...) {
            qDebug() << "[BLE:Thread] Unknown exception";
            QMetaObject::invokeMethod(this, [this]() {
                emit errorOccurred("Unknown error during connection");
            }, Qt::QueuedConnection);
        }
    });

    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void BleManager::disconnectDevice(const QString &deviceId)
{
    if (m_impl->connected.contains(deviceId)) {
        m_impl->connected[deviceId].device = nullptr;   // releases WinRT reference
        m_impl->connected.remove(deviceId);
    }
    emit deviceDisconnected(deviceId);
}

void BleManager::setResistance(const QString &deviceId, uint8_t percent)
{
    if (!m_impl->connected.contains(deviceId)) return;
    auto &entry = m_impl->connected[deviceId];
    if (!entry.device) return;

    // Run resistance control on background thread to avoid blocking
    QThread *thread = QThread::create([this, deviceId, percent, entry]() {
        try {
            auto svcResult = entry.device.GetGattServicesForUuidAsync(Uuid::FitnessMachine).get();
            if (svcResult.Status() != GattCommunicationStatus::Success ||
                svcResult.Services().Size() == 0) return;

            auto svc = svcResult.Services().GetAt(0);
            auto cResult = svc.GetCharacteristicsForUuidAsync(Uuid::FtmsControlPoint).get();
            if (cResult.Status() != GattCommunicationStatus::Success ||
                cResult.Characteristics().Size() == 0) return;

            auto ctrl = cResult.Characteristics().GetAt(0);

            // Op code 0x04 = Set Target Resistance Level, param = 0.1 unit steps
            DataWriter writer;
            writer.WriteByte(0x04);
            writer.WriteByte(static_cast<uint8_t>(percent * 10 / 100));  // 0-10 range
            ctrl.WriteValueAsync(writer.DetachBuffer()).get();
            
            qDebug() << "[BLE] Resistance set to" << percent << "%";

        } catch (const std::exception &e) {
            qDebug() << "[BLE] setResistance error:" << e.what();
        } catch (...) {
            qDebug() << "[BLE] Unknown error in setResistance";
        }
    });
    
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void BleManager::setTargetPower(const QString &deviceId, int watts)
{
    if (!m_impl->connected.contains(deviceId)) return;
    auto &entry = m_impl->connected[deviceId];
    if (!entry.device) return;

    const uint16_t targetW = static_cast<uint16_t>(std::max(0, std::min(watts, 4000)));
    QThread *thread = QThread::create([this, deviceId, targetW, entry]() {
        try {
            auto svcResult = entry.device.GetGattServicesForUuidAsync(Uuid::FitnessMachine).get();
            if (svcResult.Status() != GattCommunicationStatus::Success ||
                svcResult.Services().Size() == 0) return;
            auto svc = svcResult.Services().GetAt(0);
            auto cResult = svc.GetCharacteristicsForUuidAsync(Uuid::FtmsControlPoint).get();
            if (cResult.Status() != GattCommunicationStatus::Success ||
                cResult.Characteristics().Size() == 0) return;
            auto ctrl = cResult.Characteristics().GetAt(0);

            // Request Control (0x00) – required before any Set command
            { DataWriter w; w.WriteByte(0x00);
              ctrl.WriteValueAsync(w.DetachBuffer()).get(); }

            // Op 0x05 = Set Target Power, uint16 LE, 1 W resolution
            DataWriter writer;
            writer.WriteByte(0x05);
            writer.WriteByte(static_cast<uint8_t>(targetW & 0xFF));
            writer.WriteByte(static_cast<uint8_t>(targetW >> 8));
            ctrl.WriteValueAsync(writer.DetachBuffer()).get();
            qDebug() << "[BLE] ERG target power set to" << targetW << "W";
        } catch (const std::exception &e) {
            qDebug() << "[BLE] setTargetPower error:" << e.what();
        } catch (...) {
            qDebug() << "[BLE] Unknown error in setTargetPower";
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void BleManager::setSimulationGrade(const QString &deviceId, double gradePct)
{
    if (!m_impl->connected.contains(deviceId)) return;
    auto &entry = m_impl->connected[deviceId];
    if (!entry.device) return;

    const double clampedGrade = std::max(-20.0, std::min(gradePct, 20.0));
    QThread *thread = QThread::create([this, deviceId, clampedGrade, entry]() {
        try {
            auto svcResult = entry.device.GetGattServicesForUuidAsync(Uuid::FitnessMachine).get();
            if (svcResult.Status() != GattCommunicationStatus::Success ||
                svcResult.Services().Size() == 0) return;
            auto svc = svcResult.Services().GetAt(0);
            auto cResult = svc.GetCharacteristicsForUuidAsync(Uuid::FtmsControlPoint).get();
            if (cResult.Status() != GattCommunicationStatus::Success ||
                cResult.Characteristics().Size() == 0) return;
            auto ctrl = cResult.Characteristics().GetAt(0);

            // Request Control (0x00)
            { DataWriter w; w.WriteByte(0x00);
              ctrl.WriteValueAsync(w.DetachBuffer()).get(); }

            // Op 0x11 = Set Indoor Bike Simulation Parameters
            // wind_speed: sint16, 0.001 m/s  |  grade: sint16, 0.01%
            // crr: uint8, 0.0001             |  wind_resistance: uint8, 0.01 kg/m
            const int16_t gradeRaw = static_cast<int16_t>(clampedGrade * 100.0);
            DataWriter writer;
            writer.WriteByte(0x11);
            writer.WriteByte(0x00); writer.WriteByte(0x00);   // wind_speed = 0
            writer.WriteByte(static_cast<uint8_t>(gradeRaw & 0xFF));
            writer.WriteByte(static_cast<uint8_t>((gradeRaw >> 8) & 0xFF));
            writer.WriteByte(0x28);   // crr = 40  → 0.0040
            writer.WriteByte(0x14);   // wind_resistance = 20 → 0.20 kg/m
            ctrl.WriteValueAsync(writer.DetachBuffer()).get();
            qDebug() << "[BLE] Simulation grade set to" << clampedGrade << "%";
        } catch (const std::exception &e) {
            qDebug() << "[BLE] setSimulationGrade error:" << e.what();
        } catch (...) {
            qDebug() << "[BLE] Unknown error in setSimulationGrade";
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void BleManager::setRiderWeightKg(double kg)
{
    m_impl->riderWeightKg = kg;
}

void BleManager::setCurrentGrade(double gradePct)
{
    m_impl->currentGrade = gradePct;
}
