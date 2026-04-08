#pragma once
#include <QObject>
#include <QString>
#include <QSettings>
#include "iprotocolmanager.h"

// ──────────────────────────────────────────────────────────────────────────────
// Persists the user's chosen devices and cyclist profile to QSettings.
// ──────────────────────────────────────────────────────────────────────────────

struct SavedDevice
{
    QString    id;
    QString    name;
    QString    type;         // "Trainer" | "HRM"
    DataSource source = DataSource::None;

    bool isValid() const { return !id.isEmpty(); }
};

struct CyclistProfile
{
    double weightKg = 75.0;   // rider body weight
    int    ftpWatts = 200;    // functional threshold power (used for interval zones)
};

class DeviceConfig : public QObject
{
    Q_OBJECT
public:
    explicit DeviceConfig(QObject *parent = nullptr);

    void saveTrainer(const SavedDevice &d);
    void saveHrm(const SavedDevice &d);

    SavedDevice loadTrainer() const;
    SavedDevice loadHrm()     const;

    void clearTrainer();
    void clearHrm();

    void         saveProfile(const CyclistProfile &p);
    CyclistProfile loadProfile() const;

private:
    QSettings m_settings;

    static SavedDevice readDevice(const QSettings &s, const QString &group);
    static void        writeDevice(QSettings &s, const QString &group, const SavedDevice &d);
    static QString     sourceName(DataSource src);
    static DataSource  sourceFromName(const QString &name);
};
