#pragma once
#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QCategoryAxis>
#include <QTimer>
#include <QDateTime>
#include <QStackedWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QTextEdit>
#include <deque>
#include <QDial>
#include <QComboBox>
#include <QPushButton>
#include <array>

#include "trainerdata.h"

// ──────────────────────────────────────────────────────────────────────────────
// Real-time dashboard showing:
//   • Large “digital” readouts for Power, Cadence, Speed, HR (always visible)
//   • Scrolling line chart (power over time, HR over time) OR dial gauges
//   • Trainer control bar (ERG / Grade simulation)
// ──────────────────────────────────────────────────────────────────────────────

class MetricTile : public QWidget
{
    Q_OBJECT
public:
    explicit MetricTile(const QString &label, const QString &unit,
                        const QColor &accent, QWidget *parent = nullptr);
    void setValue(double v);
    void setValue(int v);
    void setValue(const QString &v);
    void setNoData();

private:
    QLabel *m_valueLabel = nullptr;
    QLabel *m_unitLabel  = nullptr;
    QString m_unit;
};

class DialGauge : public QWidget
{
    Q_OBJECT
public:
    explicit DialGauge(const QString &label, const QString &unit, QWidget *parent = nullptr);
    void setValue(double value);
    void setRange(double min, double max);
    void setZones(const QVector<QPair<double, QColor>> &zones);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_labelText;
    QString m_unitText;
    QVector<QPair<double, QColor>> m_zones;
    double m_min = 0;
    double m_max = 100;
    double m_value = 0;
};

// Forward declarations – defined in dashboardwidget.cpp
class GpxMapWidget;
class ElevationProfileWidget;
class ElevationZoomWidget;

class DashboardWidget : public QWidget
{
    Q_OBJECT
public:
    // ── Power Zone helpers (7-zone Coggan model based on FTP) ────────────
    static constexpr int NUM_ZONES = 7;
    // Returns 1-7 zone index for the given power and FTP
    static int powerZone(double watts, int ftp) {
        if (ftp <= 0) return 1;
        double pct = watts / ftp * 100.0;
        if (pct < 55)  return 1;  // Active Recovery
        if (pct < 75)  return 2;  // Endurance
        if (pct < 90)  return 3;  // Tempo
        if (pct < 105) return 4;  // Threshold
        if (pct < 120) return 5;  // VO2max
        if (pct < 150) return 6;  // Anaerobic
        return 7;                  // Neuromuscular
    }
    static QString zoneName(int z) {
        static const char *names[] = {"", "Recovery", "Endurance", "Tempo",
                                       "Threshold", "VO2max", "Anaerobic", "Neuromuscular"};
        return (z >= 1 && z <= 7) ? QString::fromLatin1(names[z]) : QString();
    }
    static QColor zoneColor(int z) {
        static const QColor cols[] = {QColor(), QColor("#a6adc8"), QColor("#a6e3a1"),
            QColor("#f9e2af"), QColor("#fab387"), QColor("#f38ba8"),
            QColor("#cba6f7"), QColor("#f5c2e7")};
        return (z >= 1 && z <= 7) ? cols[z] : QColor("#6c7086");
    }

    struct WorkoutSummary {
        QDateTime timestamp;
        double avgPower = 0.0;
        double avgHr = 0.0;
        double avgCadence = 0.0;
        double avgSpeed = 0.0;
        double maxHr = 0.0;
        double maxPower = 0.0;
        double duration = 0.0; // seconds
        double distance = 0.0; // km
        double totalAscent = 0.0; // metres of elevation gain
        double normalizedPower = 0.0;  // NP
        double intensityFactor = 0.0;  // IF = NP / FTP
        double tss = 0.0;              // TSS
        double bestPower5s   = 0.0;
        double bestPower1min = 0.0;
        double bestPower5min = 0.0;
        double bestPower20min = 0.0;
        std::array<int, 7> timeInZone = {};  // seconds in Z1..Z7 (index 0=Z1)
        QString routeName;     // non-empty when a GPX route was ridden
        QString trainingModeName; // "Free Ride", "Interval Training", "Map Ride"
        QString programName;   // interval program name or GPX route name
    };

    struct WorkoutSample {
        double elapsed  = 0.0; // seconds from workout start
        double power    = 0.0; // W
        double hr       = 0.0; // bpm
        double cadence  = 0.0; // rpm
        double speed    = 0.0; // km/h
        // GPS coordinates — only set during map-ride mode
        double lat      = 0.0; // degrees (0 = no data)
        double lon      = 0.0; // degrees
        double ele      = 0.0; // metres above sea level
    };

    // ── Interval training ────────────────────────────────────────────────────
    struct IntervalStep {
        QString name;
        int     durationSec = 60;
        bool    isErg       = true;
        int     ergWatts    = 100;
        double  gradePct    = 0.0;
    };

    struct TrainingProgram {
        QString             name;
        bool                isErg = true;
        QVector<IntervalStep> steps;
    };

    // ── GPX map riding ───────────────────────────────────────────────────────
    struct GpxPoint {
        double lat          = 0.0;
        double lon          = 0.0;
        double ele          = 0.0;
        double distFromStart = 0.0; // km, cumulative
    };

    explicit DashboardWidget(QWidget *parent = nullptr);

    void updateTrainer(const TrainerData &data);
    void updateHrm(const HrmData &data);
    void reset();
    void startWorkout();
    void stopWorkout();
    void setRunningState(bool running);
    void setStartEnabled(bool enabled);
    void setFtp(int watts) { m_ftpWatts = watts; if (m_selectedProgramIdx >= 0) showProgramPreview(m_selectedProgramIdx); }
    void setRiderWeightKg(double kg) { m_riderWeightKg = kg; }
    WorkoutSummary currentWorkoutSummary() const;
    const QVector<WorkoutSample>& workoutSamples() const { return m_samples; }

signals:
    void ergTargetChanged(int watts);
    void gradeChanged(double pct);
    void startStopClicked();
    void screenshotRequested();

private slots:
    void onChartTick();
    void onViewChanged(int index);
    void onIntervalTick();
    void onMapTimerTick();   // ~20 Hz smooth map repaint

private:
    enum ViewType    { ChartView, DialView };
    enum TrainingMode{ FreeRideMode, IntervalMode, MapRideMode };
    enum TrainerMode { ErgMode, GradeMode };

    void     buildTiles();
    void     buildChart();
    void     buildDialView();
    void     buildModePanel();
    void     buildFreeRideTab(QWidget *parent);
    void     buildIntervalTab(QWidget *parent);
    void     buildMapTab(QWidget *parent);
    void     addChartPoint(double powerW, double hrBpm);
    void     updateTimeAxisLabels(double minS, double maxS);
    void     updateAverages();
    void     updateControlDisplay();
    void     updateIntervalDisplay();
    void     applyIntervalStep(int stepIdx);
    void     loadGpxFromData(const QByteArray &data, const QString &name);
    void     updateMapGrade();
    void     refreshProgramList();   // reload m_programs from disk and repopulate list
    void     showProgramPreview(int idx); // fill step-detail view for selected program
    void     refreshRouteList();      // reload .gpx files from Documents/WattsFun/routes/
    void     sortRouteList();          // re-sort route items based on current sort choice
    QWidget *tileRow();
    QWidget *dialRow();

    // View switcher
    QComboBox *m_viewSwitcher = nullptr;
    ViewType m_currentView = ChartView;

    // Root layout pointers (used for map-mode layout switching)
    QVBoxLayout *m_rootLayout     = nullptr;
    QHBoxLayout *m_middleLay      = nullptr;
    QWidget     *m_chartContainer = nullptr;
    QVBoxLayout *m_chartLayout    = nullptr;
    bool         m_mapLayoutActive      = false;
    bool         m_intervalLayoutActive = false;

    // View containers
    QWidget *m_tileRowWidget = nullptr;
    QWidget *m_dialRowWidget = nullptr;

    // Tiles
    QLabel       *m_zoneOverlayLabel = nullptr;  // zone badge on chart top-right
    MetricTile *m_tilePower      = nullptr;
    MetricTile *m_tileCadence    = nullptr;
    MetricTile *m_tileSpeed      = nullptr;
    MetricTile *m_tileHr         = nullptr;
    MetricTile *m_tileDuration   = nullptr;
    MetricTile *m_tileDistance   = nullptr;
    MetricTile *m_tilePwrRatio   = nullptr;   // W/kg live

    // Average tiles
    MetricTile *m_tileAvgPower      = nullptr;
    MetricTile *m_tileAvgCadence    = nullptr;
    MetricTile *m_tileAvgSpeed      = nullptr;
    MetricTile *m_tileAvgHr         = nullptr;
    MetricTile *m_tileAvgPwrRatio   = nullptr;  // avg W/kg

    // Dial gauges
    DialGauge *m_dialPower = nullptr;
    DialGauge *m_dialHr    = nullptr;

    // ── Mode panel ───────────────────────────────────────────────────────────
    TrainingMode   m_trainingMode    = FreeRideMode;
    QWidget       *m_modePanel       = nullptr;
    QStackedWidget *m_modeStack      = nullptr;
    QPushButton   *m_btnStartStop    = nullptr;    QPushButton   *m_btnPause        = nullptr;    QPushButton   *m_btnScreenshot   = nullptr;    QPushButton   *m_btnModeFreeRide = nullptr;
    QPushButton   *m_btnModeInterval = nullptr;
    QPushButton   *m_btnModeMap      = nullptr;

    // ── Free Ride (ERG / Grade controls) ────────────────────────────────────
    TrainerMode  m_trainerMode       = GradeMode;
    int          m_ergTargetWatts    = 100;
    double       m_gradeTargetPct    = 0.0;
    QPushButton *m_btnErgMode        = nullptr;
    QPushButton *m_btnGradeMode      = nullptr;
    QLabel      *m_controlUnitLabel  = nullptr;
    QLabel      *m_controlValueLabel = nullptr;
    QPushButton *m_btnDecrement      = nullptr;
    QPushButton *m_btnIncrement      = nullptr;

    // ── Interval training ────────────────────────────────────────────────────
    int              m_ftpWatts             = 200;  // from CyclistProfile, used for zone colours
    double           m_riderWeightKg         = 75.0; // from CyclistProfile, used for W/kg
    QVector<TrainingProgram> m_programs;
    int          m_selectedProgramIdx   = 0;
    int          m_currentStepIdx       = -1;
    int          m_stepElapsedSec       = 0;
    QTimer       m_intervalTimer;
    QListWidget *m_programListWidget    = nullptr;
    QPushButton *m_programToggleBtn     = nullptr;
    bool         m_programListExpanded  = true;
    QLabel      *m_intervalProgramLabel = nullptr;
    QLabel      *m_intervalStepLabel    = nullptr;
    QLabel      *m_intervalTimeLabel    = nullptr;
    QLabel      *m_intervalTargetLabel  = nullptr;
    QProgressBar *m_intervalStepBar     = nullptr;
    QTextEdit   *m_intervalPreview      = nullptr; // step-detail read-only view
    QPushButton *m_btnNewProgram        = nullptr;
    QPushButton *m_btnEditProgram       = nullptr;
    QPushButton *m_btnDeleteProgram     = nullptr;

    // ── Map riding ───────────────────────────────────────────────────────────
    QVector<GpxPoint> m_gpxTrack;
    QString           m_gpxName;
    double            m_gpxTotalDistKm        = 0.0;
    double            m_gpxDistanceTravelled   = 0.0;
    // Current interpolated rider position (updated every tick in updateMapGrade)
    double            m_currentLat            = 0.0;
    double            m_currentLon            = 0.0;
    double            m_currentEle            = 0.0;
    // Sub-second map-repaint timer (~20 Hz) for smooth bike movement between 1s chart ticks
    QTimer  m_mapTimer;
    qint64  m_mapTimerLastMs = 0;  // QDateTime ms at last map-timer fire; 0 = first tick
    QListWidget          *m_routeListWidget           = nullptr;
    QPushButton          *m_routeToggleBtn             = nullptr;
    QComboBox            *m_routeSortCombo             = nullptr;
    bool                  m_routeListExpanded          = true;
    GpxMapWidget         *m_gpxMapWidget              = nullptr;
    ElevationProfileWidget *m_elevationProfileWidget  = nullptr;
    QLabel           *m_gpxNameLabel          = nullptr;
    QLabel           *m_gpxInfoLabel          = nullptr;
    QLabel           *m_gpxGradeLabel         = nullptr;
    QLabel           *m_gpxProgressLabel      = nullptr;
    QLabel           *m_gpxAscentLabel        = nullptr;
    ElevationZoomWidget *m_elevationZoomWidget = nullptr;
    QSlider          *m_gradeEffectSlider      = nullptr;
    QLabel           *m_gradeEffectLabel       = nullptr;
    int               m_gradeEffectPct         = 100;  // 50–100 %, scales grade sent to trainer & speed model
    double            m_gpxTotalAscent         = 0.0;
    double            m_rideAscent             = 0.0; // accumulated elevation gain this ride
    double            m_prevEleForAscent       = -1.0; // previous tick's elevation (-1 = uninit)

    // Chart
    QChartView         *m_chartView  = nullptr;
    QLineSeries        *m_serPower   = nullptr;
    QLineSeries        *m_serHr      = nullptr;
    QCategoryAxis    *m_axisX      = nullptr;
    QValueAxis         *m_axisYLeft  = nullptr;
    QValueAxis         *m_axisYRight = nullptr;

    QTimer  m_chartTimer;
    double  m_elapsed      = 0.0;   // seconds since reset
    double  m_lastPower    = 0.0;
    double  m_lastHr       = 0.0;
    double  m_lastCadence  = 0.0;
    double  m_lastSpeed    = 0.0;

    // Per-second samples recorded while workout is active
    QVector<WorkoutSample> m_samples;

    // Workout accumulation
    bool    m_workoutActive  = false;
    bool    m_workoutPaused  = false;  // true while paused (timers frozen, not recording)
    double  m_sumPower      = 0.0;
    int     m_countPower    = 0;
    double  m_sumCadence    = 0.0;
    int     m_countCadence  = 0;
    double  m_sumSpeed      = 0.0;
    int     m_countSpeed    = 0;
    double  m_sumHr         = 0.0;
    int     m_countHr       = 0;
    double  m_maxHr         = 0.0;
    double  m_workoutStartTime = 0.0;
    double  m_totalDistance = 0.0; // km

    double  m_avgPower      = 0.0;
    double  m_avgCadence    = 0.0;
    double  m_avgSpeed      = 0.0;
    double  m_avgHr         = 0.0;
    double  m_maxPowerSample = 0.0;

    // ── NP / IF / TSS (Coggan metrics) ───────────────────────────────────
    std::deque<double> m_npRolling;    // 30-second rolling power window
    double  m_npSumFourth   = 0.0;     // running sum of (30s-avg)^4
    int     m_npCount       = 0;       // number of 30s-avg values
    double  m_normalizedPower = 0.0;
    double  m_intensityFactor = 0.0;
    double  m_tss             = 0.0;

    // ── Power zone tracking ──────────────────────────────────────────────
    std::array<int, NUM_ZONES> m_timeInZone = {};  // seconds in Z1..Z7 (0-indexed: [0]=Z1)
    int     m_currentZone   = 0;

    // ── Power bests (rolling max over durations) ─────────────────────────
    std::deque<double> m_powerHistory;  // all per-second power readings
    double  m_bestPower5s   = 0.0;
    double  m_bestPower1min = 0.0;
    double  m_bestPower5min = 0.0;
    double  m_bestPower20min = 0.0;
    void    updatePowerBests();
    void    updateNpIfTss();

    static constexpr int    CHART_WINDOW_S        = 120;   // rolling 2-min window
    static constexpr double CHART_TICK_S          = 1.0;
    static constexpr double CHART_DEFAULT_MAX_POWER = 300.0; // W
    static constexpr double CHART_DEFAULT_MAX_HR    = 180.0; // bpm
};
