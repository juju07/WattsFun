#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include "trainerdata.h"

// ──────────────────────────────────────────────────────────────────────────────
// Abstract interface that both AntManager and BleManager implement.
// The rest of the app only talks to this interface.
// ──────────────────────────────────────────────────────────────────────────────

struct DeviceInfo
{
    QString id;          // internal handle / ANT device number / BLE UUID
    QString name;        // human-readable
    QString type;        // "Trainer", "HRM", …
    DataSource source = DataSource::None;
};

class IProtocolManager : public QObject
{
    Q_OBJECT
public:
    explicit IProtocolManager(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IProtocolManager() = default;

    // Returns true when the physical adapter is present and ready
    virtual bool isAvailable() const = 0;

    // Start scanning for nearby devices; emits deviceFound() for each one
    virtual void startScan() = 0;
    virtual void stopScan()  = 0;

    // Connect to a previously found device by its id
    virtual void connectDevice(const QString &deviceId) = 0;
    virtual void disconnectDevice(const QString &deviceId) = 0;

    // Trainer control – ERG mode (target power) and simulation mode (road grade)
    virtual void setTargetPower(const QString &deviceId, int watts) {}
    virtual void setSimulationGrade(const QString &deviceId, double gradePct) {}
    // Update the rider weight used in the virtual speed physics model
    virtual void setRiderWeightKg(double /*kg*/) {}
    // Update the road grade (%) used in the virtual speed physics model
    virtual void setCurrentGrade(double /*gradePct*/) {}

    virtual DataSource source() const = 0;

signals:
    void deviceFound(const DeviceInfo &info);
    void scanFinished();
    void deviceConnected(const QString &deviceId);
    void deviceDisconnected(const QString &deviceId);
    void trainerDataUpdated(const TrainerData &data);
    void hrmDataUpdated(const HrmData &data);
    void errorOccurred(const QString &message);
};
