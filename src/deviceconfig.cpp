#include "deviceconfig.h"

DeviceConfig::DeviceConfig(QObject *parent)
    : QObject(parent)
    , m_settings("WattsFun", "WattsFun")
{}

void DeviceConfig::saveTrainer(const SavedDevice &d)
{
    writeDevice(m_settings, "Trainer", d);
}

void DeviceConfig::saveHrm(const SavedDevice &d)
{
    writeDevice(m_settings, "HRM", d);
}

SavedDevice DeviceConfig::loadTrainer() const
{
    return readDevice(m_settings, "Trainer");
}

SavedDevice DeviceConfig::loadHrm() const
{
    return readDevice(m_settings, "HRM");
}

void DeviceConfig::clearTrainer()
{
    m_settings.remove("Trainer");
}

void DeviceConfig::clearHrm()
{
    m_settings.remove("HRM");
}

void DeviceConfig::saveProfile(const CyclistProfile &p)
{
    m_settings.beginGroup("CyclistProfile");
    m_settings.setValue("weightKg", p.weightKg);
    m_settings.setValue("ftpWatts", p.ftpWatts);
    m_settings.endGroup();
    m_settings.sync();
}

CyclistProfile DeviceConfig::loadProfile() const
{
    auto &ms = const_cast<QSettings &>(m_settings);
    ms.beginGroup("CyclistProfile");
    CyclistProfile p;
    p.weightKg = ms.value("weightKg", 75.0).toDouble();
    p.ftpWatts = ms.value("ftpWatts", 200).toInt();
    ms.endGroup();
    return p;
}

// ── private helpers ──────────────────────────────────────────────────────────

SavedDevice DeviceConfig::readDevice(const QSettings &s, const QString &group)
{
    // QSettings::value() is not const on older Qt; cast away const here
    auto &ms = const_cast<QSettings &>(s);
    ms.beginGroup(group);
    SavedDevice d;
    d.id     = ms.value("id").toString();
    d.name   = ms.value("name").toString();
    d.type   = ms.value("type").toString();
    d.source = sourceFromName(ms.value("source").toString());
    ms.endGroup();
    return d;
}

void DeviceConfig::writeDevice(QSettings &s, const QString &group, const SavedDevice &d)
{
    s.beginGroup(group);
    s.setValue("id",     d.id);
    s.setValue("name",   d.name);
    s.setValue("type",   d.type);
    s.setValue("source", sourceName(d.source));
    s.endGroup();
    s.sync();
}

QString DeviceConfig::sourceName(DataSource src)
{
    switch (src) {
    case DataSource::ANT: return "ANT";
    case DataSource::BLE: return "BLE";
    default:              return "None";
    }
}

DataSource DeviceConfig::sourceFromName(const QString &name)
{
    if (name == "ANT") return DataSource::ANT;
    if (name == "BLE") return DataSource::BLE;
    return DataSource::None;
}
