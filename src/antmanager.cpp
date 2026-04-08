#include "antmanager.h"
#include "trainerdata.h"
#include <QDebug>

#ifdef ANTPLUS_ENABLED
// ANT+ PC SDK 3.5 headers (include dirs configured by CMake)
#ifndef NOMINMAX
#define NOMINMAX          // prevent windows.h min/max macro pollution
#endif
#include <ant.h>          // RESPONSE_FUNC / CHANNEL_EVENT_FUNC typedefs
#include <QLibrary>
#include <QThread>
#include <cstring>
#include <cmath>
#include <QDateTime>

// Function-pointer typedefs matching the exact ant.h signatures
typedef BOOL (*PFN_ANT_Init)(UCHAR, ULONG);
typedef void (*PFN_ANT_Close)(void);
typedef BOOL (*PFN_ANT_ResetSystem)(void);
typedef BOOL (*PFN_ANT_OpenChannel)(UCHAR);
typedef BOOL (*PFN_ANT_CloseChannel)(UCHAR);
typedef BOOL (*PFN_ANT_AssignChannel)(UCHAR, UCHAR, UCHAR);
typedef BOOL (*PFN_ANT_SetChannelId)(UCHAR, USHORT, UCHAR, UCHAR);
typedef BOOL (*PFN_ANT_SetChannelRFFreq)(UCHAR, UCHAR);
typedef BOOL (*PFN_ANT_SetChannelPeriod)(UCHAR, USHORT);
typedef BOOL (*PFN_ANT_SendAcknowledgedData)(UCHAR, UCHAR*);
typedef void (*PFN_ANT_AssignResponseFunction)(RESPONSE_FUNC, UCHAR*);
typedef void (*PFN_ANT_UnassignAllResponseFunctions)(void);
typedef void (*PFN_ANT_AssignChannelEventFunction)(UCHAR, CHANNEL_EVENT_FUNC, UCHAR*);

// Static DLL function pointers (null until loadAntDll() succeeds)
static PFN_ANT_Init                         pfANT_Init                         = nullptr;
static PFN_ANT_Close                        pfANT_Close                        = nullptr;
static PFN_ANT_ResetSystem                  pfANT_ResetSystem                  = nullptr;
static PFN_ANT_OpenChannel                  pfANT_OpenChannel                  = nullptr;
static PFN_ANT_CloseChannel                 pfANT_CloseChannel                 = nullptr;
static PFN_ANT_AssignChannel                pfANT_AssignChannel                = nullptr;
static PFN_ANT_SetChannelId                 pfANT_SetChannelId                 = nullptr;
static PFN_ANT_SetChannelRFFreq             pfANT_SetChannelRFFreq             = nullptr;
static PFN_ANT_SetChannelPeriod             pfANT_SetChannelPeriod             = nullptr;
static PFN_ANT_SendAcknowledgedData         pfANT_SendAcknowledgedData         = nullptr;
static PFN_ANT_AssignResponseFunction       pfANT_AssignResponseFunction       = nullptr;
static PFN_ANT_UnassignAllResponseFunctions pfANT_UnassignAllResponseFunctions = nullptr;
static PFN_ANT_AssignChannelEventFunction   pfANT_AssignChannelEventFunction   = nullptr;

// Static state
static QLibrary    s_antLib;
static AntManager *s_antInstance  = nullptr;
static constexpr int MAX_ANT_CHANNELS = 8;
static uint8_t   s_rxBufs[MAX_ANT_CHANNELS][MESG_BUFFER_SIZE];
static uint8_t   s_responseBuf[MESG_RESPONSE_EVENT_SIZE];
static uint8_t   s_chanDevTypes[MAX_ANT_CHANNELS];

// ANT+ profile constants
static constexpr uint8_t  DEVTYPE_HRM        = 0x78;  // 120
static constexpr uint8_t  DEVTYPE_CSC        = 0x79;  // 121 Speed & Cadence
static constexpr uint8_t  DEVTYPE_POWER      = 0x0B;  // 11
static constexpr uint8_t  DEVTYPE_FEC        = 0x11;  // 17  Fitness Equipment
static constexpr uint16_t WILDCARD_DEVNUM    = 0;
static constexpr uint8_t  WILDCARD_TRANSTYPE = 0;
static constexpr uint8_t  ANT_FREQ           = 57;    // 2457 MHz
static constexpr uint16_t MSG_PERIOD_HRM     = 8070;  // ~4 Hz
static constexpr uint16_t MSG_PERIOD_CSC     = 8086;
static constexpr uint16_t MSG_PERIOD_PWR     = 8182;
static constexpr uint16_t MSG_PERIOD_FEC     = 8192;

// Runtime DLL loader
static bool loadAntDll()
{
    if (s_antLib.isLoaded()) return true;
    s_antLib.setFileName("ANT_DLL");
    if (!s_antLib.load()) {
        qWarning() << "[ANT] Failed to load ANT_DLL.dll:" << s_antLib.errorString();
        return false;
    }
#define LOAD_FN(sym) \
    pf##sym = reinterpret_cast<PFN_##sym>(s_antLib.resolve(#sym)); \
    if (!pf##sym) { qWarning() << "[ANT] Missing symbol:" << #sym; s_antLib.unload(); return false; }
    LOAD_FN(ANT_Init)
    LOAD_FN(ANT_Close)
    LOAD_FN(ANT_ResetSystem)
    LOAD_FN(ANT_OpenChannel)
    LOAD_FN(ANT_CloseChannel)
    LOAD_FN(ANT_AssignChannel)
    LOAD_FN(ANT_SetChannelId)
    LOAD_FN(ANT_SetChannelRFFreq)
    LOAD_FN(ANT_SetChannelPeriod)
    LOAD_FN(ANT_SendAcknowledgedData)
    LOAD_FN(ANT_AssignResponseFunction)
    LOAD_FN(ANT_UnassignAllResponseFunctions)
    LOAD_FN(ANT_AssignChannelEventFunction)
#undef LOAD_FN
    qDebug() << "[ANT] ANT_DLL.dll loaded successfully";
    return true;
}

// SDK callbacks — called on the SDK's internal thread
static BOOL antChannelEventCb(UCHAR channel, UCHAR event)
{
    if ((event == EVENT_RX_BROADCAST         ||
         event == EVENT_RX_FLAG_BROADCAST    ||
         event == EVENT_RX_FLAG_ACKNOWLEDGED) &&
        s_antInstance && channel < static_cast<UCHAR>(MAX_ANT_CHANNELS))
    {
        uint8_t *buf = s_rxBufs[channel];
        int devType  = static_cast<int>(s_chanDevTypes[channel]);
        QByteArray payload(reinterpret_cast<const char*>(buf), MESG_BUFFER_SIZE);
        QMetaObject::invokeMethod(s_antInstance, "handleAntData",
                                  Qt::QueuedConnection,
                                  Q_ARG(int, channel),
                                  Q_ARG(int, devType),
                                  Q_ARG(QByteArray, payload));
    }
    return TRUE;
}

static BOOL antResponseCb(UCHAR channel, UCHAR msgId)
{
    if (msgId == MESG_RESPONSE_EVENT_ID) {
        if (s_responseBuf[1] == MESG_EVENT_ID &&
            s_responseBuf[2] == EVENT_RX_FAIL_GO_TO_SEARCH)
            qDebug() << "[ANT] Channel" << channel << "lost, rescanning";
    }
    return TRUE;
}

#endif // ANTPLUS_ENABLED (static setup)

// ──────────────────────────────────────────────────────────────────────────────

AntManager::AntManager(QObject *parent)
    : IProtocolManager(parent)
{
    connect(&m_pollTimer, &QTimer::timeout, this, &AntManager::onPollTimer);
}

AntManager::~AntManager()
{
#ifdef ANTPLUS_ENABLED
    if (m_antLoaded) {
        if (pfANT_UnassignAllResponseFunctions) pfANT_UnassignAllResponseFunctions();
        if (pfANT_Close)                        pfANT_Close();
        m_antLoaded = false;
    }
    s_antInstance = nullptr;
#endif
}

bool AntManager::isAvailable() const
{
#ifdef ANTPLUS_ENABLED
    if (!loadAntDll()) return false;
    qDebug() << "[ANT] Checking availability...";
    const bool result = pfANT_Init(0, 57600) != FALSE;
    if (result) pfANT_Close();
    qDebug() << "[ANT] Available:" << result;
    return result;
#else
    qDebug() << "[ANT] SDK not built in";
    return false;   // SDK not linked – fall through to BLE
#endif
}

void AntManager::startScan()
{
#ifdef ANTPLUS_ENABLED
    if (m_scanning) return;

    if (!loadAntDll()) {
        emit errorOccurred(tr("ANT+ DLL not found (ANT_DLL.dll). Check SDK installation."));
        return;
    }
    if (!pfANT_Init(0, 57600)) {
        emit errorOccurred(tr("Failed to open ANT+ USB stick (port 0)."));
        return;
    }
    m_antLoaded   = true;
    s_antInstance = this;

    pfANT_AssignResponseFunction(antResponseCb, s_responseBuf);
    pfANT_ResetSystem();
    QThread::msleep(500);

    // Open wildcard channels for all interesting profiles
    m_channelCount = 0;
    openAntChannel(m_channelCount++, DEVTYPE_HRM,   WILDCARD_DEVNUM);
    openAntChannel(m_channelCount++, DEVTYPE_CSC,   WILDCARD_DEVNUM);
    openAntChannel(m_channelCount++, DEVTYPE_POWER, WILDCARD_DEVNUM);
    openAntChannel(m_channelCount++, DEVTYPE_FEC,   WILDCARD_DEVNUM);

    m_scanning = true;
    m_pollTimer.start(2000);  // periodic keepalive / device-found heartbeat
#else
    emit errorOccurred(tr("ANT+ SDK not available."));
#endif
}

void AntManager::stopScan()
{
    m_scanning = false;
    m_pollTimer.stop();
#ifdef ANTPLUS_ENABLED
    if (m_antLoaded) {
        pfANT_UnassignAllResponseFunctions();
        pfANT_Close();
        m_antLoaded = false;
    }
    s_antInstance = nullptr;
#endif
    emit scanFinished();
}

void AntManager::connectDevice(const QString &deviceId)
{
    // In ANT+ world, "connecting" means opening a dedicated channel for the device.
    // During scan the wildcard channel already fires data; here we just acknowledge.
    emit deviceConnected(deviceId);
}

void AntManager::disconnectDevice(const QString &deviceId)
{
#ifdef ANTPLUS_ENABLED
    if (m_openChannels.contains(deviceId) && pfANT_CloseChannel) {
        uint8_t ch = static_cast<uint8_t>(m_openChannels.value(deviceId));
        pfANT_CloseChannel(ch);
        m_openChannels.remove(deviceId);
    }
#endif
    emit deviceDisconnected(deviceId);
}

void AntManager::setTargetPower(const QString &deviceId, int watts)
{
#ifdef ANTPLUS_ENABLED
    if (!m_openChannels.contains(deviceId)) return;
    uint8_t ch = static_cast<uint8_t>(m_openChannels.value(deviceId));
    // FE-C Data Page 49 (0x31) – Target Power
    // target_power: uint16 LE, 0.25 W per unit
    uint16_t targetRaw = static_cast<uint16_t>(std::max(0, std::min(watts, 4000)) * 4);
    uint8_t buf[8] = { 0x31, 0xFF, 0xFF, 0xFF, 0xFF,
                       static_cast<uint8_t>(targetRaw & 0xFF),
                       static_cast<uint8_t>(targetRaw >> 8), 0xFF };
    pfANT_SendAcknowledgedData(ch, buf);
    qDebug() << "[ANT] FE-C ERG target power" << watts << "W";
#else
    Q_UNUSED(deviceId) Q_UNUSED(watts)
#endif
}

void AntManager::setSimulationGrade(const QString &deviceId, double gradePct)
{
#ifdef ANTPLUS_ENABLED
    if (!m_openChannels.contains(deviceId)) return;
    uint8_t ch = static_cast<uint8_t>(m_openChannels.value(deviceId));
    // FE-C Data Page 51 (0x33) – Track Resistance
    // grade: sint16, 0.01%, offset -200% → encode as (grade + 200) * 100
    // crr:   uint8,  5e-5 per unit → 0.004 / 5e-5 = 80
    const double clamped = std::max(-20.0, std::min(gradePct, 20.0));
    uint16_t gradeRaw = static_cast<uint16_t>((clamped + 200.0) * 100.0);
    uint8_t buf[8] = { 0x33, 0xFF, 0xFF, 0xFF,
                       static_cast<uint8_t>(gradeRaw & 0xFF),
                       static_cast<uint8_t>(gradeRaw >> 8),
                       80, 0xFF };
    pfANT_SendAcknowledgedData(ch, buf);
    qDebug() << "[ANT] FE-C simulation grade" << clamped << "%";
#else
    Q_UNUSED(deviceId) Q_UNUSED(gradePct)
#endif
}

// ── private ──────────────────────────────────────────────────────────────────

void AntManager::onPollTimer()
{
    // Data arrives via antChannelEventCb callbacks driven by the SDK.
    // Nothing to poll; this timer is kept alive for future keepalive use.
}

#ifdef ANTPLUS_ENABLED

void AntManager::openAntChannel(uint8_t ch, uint8_t deviceType, uint16_t deviceId)
{
    uint16_t period = MSG_PERIOD_FEC;
    if (deviceType == DEVTYPE_HRM)   period = MSG_PERIOD_HRM;
    if (deviceType == DEVTYPE_CSC)   period = MSG_PERIOD_CSC;
    if (deviceType == DEVTYPE_POWER) period = MSG_PERIOD_PWR;

    s_chanDevTypes[ch] = deviceType;
    memset(s_rxBufs[ch], 0, MESG_BUFFER_SIZE);

    pfANT_AssignChannel   (ch, PARAMETER_RX_NOT_TX, 0);
    pfANT_SetChannelId    (ch, deviceId, deviceType, WILDCARD_TRANSTYPE);
    pfANT_SetChannelRFFreq(ch, ANT_FREQ);
    pfANT_SetChannelPeriod(ch, period);
    pfANT_AssignChannelEventFunction(ch, antChannelEventCb, s_rxBufs[ch]);
    pfANT_OpenChannel     (ch);
}

void AntManager::handleAntData(int channel, int deviceType, QByteArray payload)
{
    uint8_t *data = reinterpret_cast<uint8_t*>(payload.data());
    const QString devId = QString("ANT-%1-%2").arg(channel).arg(deviceType);

    switch (static_cast<uint8_t>(deviceType)) {
    case DEVTYPE_HRM:
        decodeHrm(data);
        emit hrmDataUpdated(m_hrmData);
        emit deviceFound({ devId, QString("ANT+ HRM ch%1").arg(channel), "HRM", DataSource::ANT });
        break;
    case DEVTYPE_CSC:
        decodeCadenceSpeed(data);
        emit trainerDataUpdated(m_trainerData);
        emit deviceFound({ devId, QString("ANT+ Speed/Cadence ch%1").arg(channel), "Trainer", DataSource::ANT });
        break;
    case DEVTYPE_POWER:
        decodePower(data);
        emit trainerDataUpdated(m_trainerData);
        emit deviceFound({ devId, QString("ANT+ Power Meter ch%1").arg(channel), "Trainer", DataSource::ANT });
        break;
    case DEVTYPE_FEC:
        decodeFec(data);
        emit trainerDataUpdated(m_trainerData);
        emit deviceFound({ devId, QString("ANT+ FE-C Trainer ch%1").arg(channel), "Trainer", DataSource::ANT });
        break;
    default:
        break;
    }
}

// ── ANT+ profile decoders ─────────────────────────────────────────────────────

// Profile 120 – Heart Rate Monitor  (page 4)
void AntManager::decodeHrm(uint8_t *d)
{
    m_hrmData.heartRateBpm = d[7];
    m_hrmData.valid = (m_hrmData.heartRateBpm > 0);
}

// Profile 121 – Speed & Cadence (page 0)
void AntManager::decodeCadenceSpeed(uint8_t *d)
{
    // Cadence event time (1/1024 s), cadence revolution count
    uint16_t cadenceEventTime = static_cast<uint16_t>(d[1] | (d[2] << 8));
    uint16_t cadenceRevCount  = static_cast<uint16_t>(d[3] | (d[4] << 8));
    (void)cadenceEventTime; (void)cadenceRevCount;
    // Speed event time, speed revolution count
    uint16_t speedEventTime   = static_cast<uint16_t>(d[5] | (d[6] << 8));
    uint16_t speedRevCount    = static_cast<uint16_t>(d[7] | (d[8] << 8));
    (void)speedEventTime; (void)speedRevCount;

    // Simplified: store raw counts; a real implementation would diff against
    // previous values and apply the wheel circumference.
    m_trainerData.hasCadence = true;
    // (calculation left as integration point)
}

// Profile 11 – Bicycle Power (page 0x10 – standard power-only)
void AntManager::decodePower(uint8_t *d)
{
    if (d[0] == 0x10) {  // page 16
        uint16_t instantPower = static_cast<uint16_t>(d[6] | (d[7] << 8));
        m_trainerData.powerWatts = instantPower;
        m_trainerData.hasPower   = true;
        // Inertial speed: Euler integration of equation of motion
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const double rawDt = m_lastSpeedUpdateMs > 0 ? (nowMs - m_lastSpeedUpdateMs) / 1000.0 : 1.0;
            const double dt = rawDt < 0.05 ? 0.05 : (rawDt > 2.0 ? 2.0 : rawDt);
            m_lastSpeedUpdateMs = nowMs;
            m_trainerData.speedKph = applySpeedInertia(m_currentSpeedMps,
                                                       m_trainerData.powerWatts,
                                                       m_riderWeightKg + 10.0,
                                                       m_currentGrade, dt);
            m_currentSpeedMps = m_trainerData.speedKph / 3.6;
        }
    }
}

// Profile 17 – Fitness Equipment Control (FE-C)
void AntManager::decodeFec(uint8_t *d)
{
    uint8_t page = d[0];
    if (page == 0x19) {   // page 25 – Trainer data
        uint16_t instantPower = static_cast<uint16_t>((d[5] & 0x0F) | (d[6] << 4));
        uint8_t  cadence      = d[4];
        m_trainerData.powerWatts  = instantPower;
        m_trainerData.cadenceRpm  = cadence;
        m_trainerData.hasPower    = true;
        m_trainerData.hasCadence  = true;
        // Inertial speed: Euler integration of equation of motion
        {
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const double rawDt = m_lastSpeedUpdateMs > 0 ? (nowMs - m_lastSpeedUpdateMs) / 1000.0 : 1.0;
            const double dt = rawDt < 0.05 ? 0.05 : (rawDt > 2.0 ? 2.0 : rawDt);
            m_lastSpeedUpdateMs = nowMs;
            m_trainerData.speedKph = applySpeedInertia(m_currentSpeedMps,
                                                       m_trainerData.powerWatts,
                                                       m_riderWeightKg + 10.0,
                                                       m_currentGrade, dt);
            m_currentSpeedMps = m_trainerData.speedKph / 3.6;
        }
    } else if (page == 0x30) {  // page 48 – Basic resistance
        double resistance = static_cast<double>(d[7]) / 2.0;  // 0.5 % steps
        m_trainerData.resistancePct  = resistance;
        m_trainerData.hasResistance  = true;
    }
}

#endif // ANTPLUS_ENABLED
