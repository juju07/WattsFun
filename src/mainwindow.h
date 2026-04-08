#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <memory>

#include "trainerdata.h"
#include "deviceconfig.h"
#include "dashboardwidget.h"
#ifdef STRAVA_ENABLED
#include "stravauploader.h"
#endif

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class IProtocolManager;
class AntManager;
class BleManager;
class DeviceSelectionDialog;
class DialGauge;
class WorkoutMapWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSelectDevices();
    void onCyclistProfile();
    void onStartStop();
    void onTrainerDataUpdated(const TrainerData &data);
    void onHrmDataUpdated(const HrmData &data);
    void onDeviceConnected(const QString &id);
    void onDeviceDisconnected(const QString &id);
    void onError(const QString &msg);
    void onStatusBar(const QString &msg);

private:
    void buildUi();
    void applyStyleSheet();
    void tryAutoConnect();
    IProtocolManager *preferredManager() const;
    void connectManagerSignals(IProtocolManager *mgr);
    void loadWorkoutSummary(int index);
    void onDeleteWorkout();
    QString formatDuration(double seconds) const;
    void saveWorkouts();
    void loadWorkouts();
    QString workoutsFilePath() const;

    struct WorkoutEntry {
        DashboardWidget::WorkoutSummary summary;
        QVector<DashboardWidget::WorkoutSample> samples;
    };

    Ui::MainWindow    *ui            = nullptr;
    AntManager        *m_ant         = nullptr;
    BleManager        *m_ble         = nullptr;
    IProtocolManager  *m_active      = nullptr;   // whichever is in use
    DeviceConfig       m_config;
    DashboardWidget   *m_dashboard   = nullptr;

    QTabWidget       *m_tabWidget       = nullptr;
    QWidget          *m_workoutsTab     = nullptr;
    QListWidget      *m_workoutList        = nullptr;
    QPushButton      *m_deleteWorkoutButton = nullptr;
    QPushButton      *m_exportTcxButton     = nullptr;
    QPushButton      *m_uploadStravaButton  = nullptr;
    DialGauge        *m_workoutPowerDial    = nullptr;
    DialGauge        *m_workoutHrDial       = nullptr;
    QLabel           *m_savedAvgPowerValue   = nullptr;
    QLabel           *m_savedAvgCadenceValue = nullptr;
    QLabel           *m_savedAvgHrValue      = nullptr;
    QLabel           *m_savedMaxHrValue      = nullptr;
    QLabel           *m_savedAvgSpeedValue   = nullptr;
    QLabel           *m_savedDurationValue   = nullptr;
    QLabel           *m_savedDistanceValue   = nullptr;
    QLabel           *m_savedAscentValue     = nullptr;
    QLabel           *m_savedNPValue         = nullptr;
    QLabel           *m_savedIFValue         = nullptr;
    QLabel           *m_savedTSSValue        = nullptr;
    QLabel           *m_savedMaxPowerValue   = nullptr;
    QWidget          *m_zoneBar              = nullptr;   // stacked time-in-zone bar
    QLabel           *m_zoneLegend           = nullptr;   // zone legend labels
    QLabel           *m_savedBest5sValue     = nullptr;
    QLabel           *m_savedBest1minValue   = nullptr;
    QLabel           *m_savedBest5minValue   = nullptr;
    QLabel           *m_savedBest20minValue  = nullptr;
    QStackedWidget   *m_detailStack         = nullptr;
    WorkoutMapWidget *m_workoutMapWidget    = nullptr;
    QVector<WorkoutEntry> m_workouts;
#ifdef STRAVA_ENABLED
    StravaUploader   *m_strava = nullptr;
#endif

    QString  m_trainerId;
    QString  m_hrmId;
    bool     m_running        = false;
    bool     m_trainerConnected = false;
    bool     m_hrmConnected     = false;

    void updateDeviceStatusIcons();
};
