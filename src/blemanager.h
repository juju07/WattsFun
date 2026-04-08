#pragma once
#include "iprotocolmanager.h"
#include <QTimer>
#include <QMap>
#include <QString>

// ──────────────────────────────────────────────────────────────────────────────
// BLE manager using the Windows Runtime (WinRT) Bluetooth LE API.
//
// Key BLE GATT services & characteristics used:
//   Cycling Power Service          0x1818
//     Power Measurement            0x2A63
//   Cycling Speed & Cadence        0x1816
//     CSC Measurement              0x2A5B
//   Fitness Machine Service (FTMS) 0x1826
//     Indoor Bike Data             0x2AD2
//     Training Status              0x2AD3
//     Fitness Machine Control Pt   0x2AD9   (write resistance)
//   Heart Rate Service             0x180D
//     Heart Rate Measurement       0x2A37
// ──────────────────────────────────────────────────────────────────────────────

// Forward-declare WinRT types to avoid pulling <winrt/…> into every TU
namespace winrt {
    namespace Windows::Devices::Bluetooth { struct BluetoothLEDevice; }
    namespace Windows::Devices::Bluetooth::Advertisement { struct BluetoothLEAdvertisementWatcher; }
}

struct BleDeviceEntry;

class BleManager : public IProtocolManager
{
    Q_OBJECT
public:
    explicit BleManager(QObject *parent = nullptr);
    ~BleManager() override;

    bool isAvailable()  const override;
    void startScan()          override;
    void stopScan()           override;
    void connectDevice(const QString &deviceId)    override;
    void disconnectDevice(const QString &deviceId) override;
    DataSource source() const override { return DataSource::BLE; }

    void setResistance(const QString &deviceId, uint8_t percent);
    // ERG mode: target power (watts)
    void setTargetPower(const QString &deviceId, int watts) override;
    // Simulation mode: road grade (%)
    void setSimulationGrade(const QString &deviceId, double gradePct) override;
    // Update rider weight for virtual speed model
    void setRiderWeightKg(double kg) override;
    // Update road grade for virtual speed model
    void setCurrentGrade(double gradePct) override;

private:
    // Pimpl to hide WinRT headers from Qt MOC and keep compile times down
    struct Impl;
    Impl *m_impl = nullptr;
};
