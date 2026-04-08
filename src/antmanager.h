#pragma once
#include "iprotocolmanager.h"

#ifdef ANTPLUS_ENABLED
// Forward-declare ANT SDK handle type so we don't pollute the whole project
struct _ANT_CHANNEL;
#endif

#include <QTimer>
#include <QMap>

// ──────────────────────────────────────────────────────────────────────────────
// ANT+ manager – wraps the Garmin ANT+ PC SDK.
//
// ANT+ profiles used:
//   • Bicycle Power  – profile 11 (device type 0x0B)
//   • Speed & Cadence – profile 121 (device type 0x79)
//   • Fitness Equipment – profile 17 (device type 0x11)  (FE-C for resistance)
//   • Heart Rate Monitor – profile 120 (device type 0x78)
//
// When ANTPLUS_ENABLED is not defined (SDK path not provided) the class
// compiles as a stub that reports isAvailable() == false so the app
// transparently falls back to BLE.
// ──────────────────────────────────────────────────────────────────────────────

class AntManager : public IProtocolManager
{
    Q_OBJECT
public:
    explicit AntManager(QObject *parent = nullptr);
    ~AntManager() override;

    bool isAvailable()  const override;
    void startScan()          override;
    void stopScan()           override;
    void connectDevice(const QString &deviceId)    override;
    void disconnectDevice(const QString &deviceId) override;
    void setTargetPower(const QString &deviceId, int watts) override;
    void setSimulationGrade(const QString &deviceId, double gradePct) override;
    void setRiderWeightKg(double kg) override { m_riderWeightKg = kg; }
    void setCurrentGrade(double gradePct) override { m_currentGrade = gradePct; }
    DataSource source() const override { return DataSource::ANT; }

private slots:
    void onPollTimer();
#ifdef ANTPLUS_ENABLED
    void handleAntData(int channel, int deviceType, QByteArray payload);
#endif

private:
#ifdef ANTPLUS_ENABLED
    void openAntChannel(uint8_t channelNum, uint8_t deviceType, uint16_t deviceId);
    void decodePower(uint8_t *data);
    void decodeCadenceSpeed(uint8_t *data);
    void decodeHrm(uint8_t *data);
    void decodeFec(uint8_t *data);

    bool    m_antLoaded    = false;
    uint8_t m_channelCount = 0;
#endif

    QTimer       m_pollTimer;
    bool         m_scanning      = false;
    double       m_riderWeightKg   = 75.0;  // set from CyclistProfile
    double       m_currentGrade    = 0.0;   // current road grade (%) for speed model
    double       m_currentSpeedMps = 0.0;   // running speed state for inertial model
    qint64       m_lastSpeedUpdateMs = 0;   // QDateTime ms, 0 = not yet set
    TrainerData  m_trainerData;
    HrmData      m_hrmData;

    // Channels we opened (device number -> channel index)
    QMap<QString, int> m_openChannels;
};
