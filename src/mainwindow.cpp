#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "antmanager.h"
#include "blemanager.h"
#include "deviceselectiondialog.h"
#include "dashboardwidget.h"
#include "deviceconfig.h"
#include "stravauploader.h"
#include "tcxexporter.h"

#include <QMessageBox>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QTimer>
#include <QDateTime>
#include <QTabWidget>
#include <QListWidget>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QXmlStreamReader>
#include <QScreen>
#include <QDesktopServices>
#include <QGraphicsOpacityEffect>
#include <algorithm>
#include <tuple>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QSet>
#include <cmath>

// ── Tile math helpers for WorkoutMapWidget ────────────────────────────────────
static int    wm_tileX (double lon, int z) { return static_cast<int>((lon + 180.0) / 360.0 * (1 << z)); }
static int    wm_tileY (double lat, int z) { const double r = qDegreesToRadians(lat); return static_cast<int>((1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / M_PI) / 2.0 * (1 << z)); }
static double wm_tileXf(double lon, int z) { return (lon + 180.0) / 360.0 * (1 << z); }
static double wm_tileYf(double lat, int z) { const double r = qDegreesToRadians(lat); return (1.0 - std::log(std::tan(r) + 1.0 / std::cos(r)) / M_PI) / 2.0 * (1 << z); }

// ── WorkoutMapWidget ──────────────────────────────────────────────────────────
// Read-only satellite map shown in the saved workouts detail panel for map rides.
class WorkoutMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WorkoutMapWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_nam(new QNetworkAccessManager(this))
    {
        setMinimumHeight(180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet("background: #0d0d14; border-radius: 8px;");
    }

    void setRoute(const QVector<DashboardWidget::WorkoutSample> &samples)
    {
        m_points.clear();
        m_tileCache.clear();
        m_pendingTiles.clear();
        for (const auto &s : samples) {
            if (s.lat != 0.0 || s.lon != 0.0)
                m_points.append({s.lat, s.lon});
        }
        if (!m_points.isEmpty()) {
            m_minLat = m_maxLat = m_points.first().first;
            m_minLon = m_maxLon = m_points.first().second;
            for (const auto &pt : m_points) {
                m_minLat = qMin(m_minLat, pt.first);  m_maxLat = qMax(m_maxLat, pt.first);
                m_minLon = qMin(m_minLon, pt.second); m_maxLon = qMax(m_maxLon, pt.second);
            }
            chooseZoom();
            fetchTiles();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.fillRect(rect(), QColor("#0d0d14"));

        if (m_points.size() < 2) {
            p.setPen(QColor("#45475a"));
            p.setFont(QFont("Sans", 9));
            p.drawText(rect(), Qt::AlignCenter, "No GPS data recorded");
            return;
        }
        const double cLat = (m_minLat + m_maxLat) * 0.5;
        const double cLon = (m_minLon + m_maxLon) * 0.5;
        const double cTX  = wm_tileXf(cLon, m_zoom);
        const double cTY  = wm_tileYf(cLat, m_zoom);
        const double cx   = width()  / 2.0;
        const double cy   = height() / 2.0;
        constexpr double TILE = 256.0;

        auto proj = [&](double lat, double lon) {
            return QPointF(cx + (wm_tileXf(lon, m_zoom) - cTX) * TILE,
                           cy + (wm_tileYf(lat, m_zoom) - cTY) * TILE);
        };

        for (int tx = m_txMin; tx <= m_txMax; ++tx)
            for (int ty = m_tyMin; ty <= m_tyMax; ++ty) {
                const QString key = tileKey(tx, ty);
                if (m_tileCache.contains(key))
                    p.drawPixmap(QRectF(cx + (tx - cTX) * TILE, cy + (ty - cTY) * TILE, TILE, TILE),
                                 m_tileCache[key], QRectF());
            }

        QVector<QPointF> pts;
        pts.reserve(m_points.size());
        for (const auto &pt : m_points)
            pts.append(proj(pt.first, pt.second));

        QPen pen(QColor("#89b4fa"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        for (int i = 0; i < pts.size() - 1; ++i)
            p.drawLine(pts[i], pts[i + 1]);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#a6e3a1")); p.drawEllipse(pts.first(), 5.0, 5.0);
        p.setBrush(QColor("#f38ba8")); p.drawEllipse(pts.last(),  5.0, 5.0);

        // ESRI / OSM attribution
        p.setPen(QColor(255, 255, 255, 120));
        p.setFont(QFont("Sans", 5));
        p.drawText(rect().adjusted(0, 0, -4, -2),
                   Qt::AlignRight | Qt::AlignBottom,
                   m_satellite
                   ? QString::fromUtf8("Powered by Esri | \xC2\xA9 Esri, Maxar, Earthstar Geographics")
                   : QString::fromUtf8("\xC2\xA9 OpenStreetMap contributors"));
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        if (!m_points.isEmpty()) { chooseZoom(); fetchTiles(); }
    }

private:
    QString tileKey(int x, int y) const { return QString("%1/%2/%3").arg(m_zoom).arg(x).arg(y); }

    void chooseZoom()
    {
        const double padX = width()  * 0.85;   // usable width/height with margin
        const double padY = height() * 0.85;
        const double cLat = (m_minLat + m_maxLat) * 0.5;
        const double cLon = (m_minLon + m_maxLon) * 0.5;
        for (m_zoom = 18; m_zoom >= 1; --m_zoom) {
            double pxW = (wm_tileXf(m_maxLon, m_zoom) - wm_tileXf(m_minLon, m_zoom)) * 256.0;
            double pxH = (wm_tileYf(m_minLat, m_zoom) - wm_tileYf(m_maxLat, m_zoom)) * 256.0;
            if (pxW <= padX && pxH <= padY)
                break;
        }
        const int cx = wm_tileX(cLon, m_zoom);
        const int cy = wm_tileY(cLat, m_zoom);
        const int rX = qMax(2, static_cast<int>(std::ceil(width()  / 256.0 / 2.0)) + 1);
        const int rY = qMax(2, static_cast<int>(std::ceil(height() / 256.0 / 2.0)) + 1);
        const int mx = (1 << m_zoom) - 1;
        m_txMin = qMax(0, cx - rX); m_txMax = qMin(mx, cx + rX);
        m_tyMin = qMax(0, cy - rY); m_tyMax = qMin(mx, cy + rY);
    }

    void fetchTiles()
    {
        const int mx = (1 << m_zoom) - 1;
        for (int tx = qMax(0, m_txMin - 1); tx <= qMin(mx, m_txMax + 1); ++tx)
            for (int ty = qMax(0, m_tyMin - 1); ty <= qMin(mx, m_tyMax + 1); ++ty) {
                const QString key = tileKey(tx, ty);
                if (m_tileCache.contains(key) || m_pendingTiles.contains(key)) continue;
                m_pendingTiles.insert(key);
                QString url;
                if (m_satellite) {
                    url = QString(
                        "https://server.arcgisonline.com/ArcGIS/rest/services"
                        "/World_Imagery/MapServer/tile/%1/%2/%3")
                        .arg(m_zoom).arg(ty).arg(tx);
                } else {
                    url = QString(
                        "https://tile.openstreetmap.org/%1/%2/%3.png")
                        .arg(m_zoom).arg(tx).arg(ty);
                }
                QNetworkRequest req{QUrl{url}};
                req.setRawHeader("User-Agent", "WattsFun/1.0");
                QNetworkReply *reply = m_nam->get(req);
                connect(reply, &QNetworkReply::finished, this, [this, reply, key]() {
                    reply->deleteLater();
                    m_pendingTiles.remove(key);
                    if (reply->error() != QNetworkReply::NoError) return;
                    QPixmap pix;
                    if (pix.loadFromData(reply->readAll())) m_tileCache[key] = pix;
                    update();
                });
            }
    }

    QNetworkAccessManager          *m_nam;
    QMap<QString, QPixmap>          m_tileCache;
    QSet<QString>                   m_pendingTiles;
    QVector<QPair<double, double>>  m_points;   // (lat, lon)
    double m_minLat = 0, m_maxLat = 0, m_minLon = 0, m_maxLon = 0;
    int m_zoom = 14, m_txMin = 0, m_txMax = 0, m_tyMin = 0, m_tyMax = 0;
    bool m_satellite = true;  // default to satellite for saved workout view
};

#include "mainwindow.moc"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_ant(new AntManager(this))
    , m_ble(new BleManager(this))
    , m_config(this)
{
    ui->setupUi(this);

    // Remove outer margins — header bar and tab content manage their own padding
    ui->mainLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    ui->mainLayout->setSpacing(0);

    setWindowTitle("WattsFun – Virtual Bike Training App");
    setWindowIcon(QIcon(":/icon.ico"));
    setMinimumSize(900, 620);
    applyStyleSheet();
    updateDeviceStatusIcons();

    // ── Dashboard + Workouts tabs ────────────────────────────────────────────
    m_dashboard = new DashboardWidget(this);

    m_workoutPowerDial = new DialGauge("AVG POWER", "W", this);
    m_workoutPowerDial->setRange(0, 500);
    m_workoutPowerDial->setZones({
        {0, QColor("#a6e3a1")}, {150, QColor("#f9e2af")},
        {250, QColor("#fab387")}, {350, QColor("#f38ba8")}
    });

    m_workoutHrDial = new DialGauge("AVG HEART RATE", "bpm", this);
    m_workoutHrDial->setRange(40, 200);
    m_workoutHrDial->setZones({
        {40, QColor("#a6e3a1")}, {120, QColor("#f9e2af")},
        {150, QColor("#fab387")}, {170, QColor("#f38ba8")}
    });

    m_workoutList = new QListWidget(this);
    connect(m_workoutList, &QListWidget::currentRowChanged, this, &MainWindow::loadWorkoutSummary);

    m_deleteWorkoutButton = new QPushButton(tr("Delete Workout"), this);
    m_deleteWorkoutButton->setEnabled(false);
    connect(m_deleteWorkoutButton, &QPushButton::clicked, this, &MainWindow::onDeleteWorkout);
    connect(m_workoutList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_deleteWorkoutButton->setEnabled(row >= 0 && row < m_workouts.size());
    });

    m_uploadStravaButton = new QPushButton(tr("Upload to Strava"), this);
    m_uploadStravaButton->setEnabled(false);
    connect(m_workoutList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_uploadStravaButton->setEnabled(row >= 0 && row < m_workouts.size());
    });

    m_exportTcxButton = new QPushButton(tr("Export TCX…"), this);
    m_exportTcxButton->setEnabled(false);
    connect(m_workoutList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_exportTcxButton->setEnabled(row >= 0 && row < m_workouts.size());
    });

    // ── Workouts tab ──────────────────────────────────────────────────────────
    auto *workoutsLayout = new QHBoxLayout();
    workoutsLayout->setContentsMargins(16, 16, 16, 16);
    workoutsLayout->setSpacing(16);

    // Left: workout list panel
    auto *listPanel = new QWidget(this);
    listPanel->setObjectName("workoutListPanel");
    listPanel->setStyleSheet(
        "QWidget#workoutListPanel {"
        "  background-color: #181825;"
        "  border-radius: 12px;"
        "  border: 1px solid #313244;"
        "}");
    auto *listPanelLayout = new QVBoxLayout(listPanel);
    listPanelLayout->setContentsMargins(14, 14, 14, 14);
    listPanelLayout->setSpacing(10);

    auto *listTitle = new QLabel(tr("SAVED WORKOUTS"), listPanel);
    listTitle->setStyleSheet(
        "font-size: 11px; font-weight: 700; color: #6c7086; letter-spacing: 2px;");
    listPanelLayout->addWidget(listTitle);
    listPanelLayout->addWidget(m_workoutList, 1);

    listPanelLayout->addWidget(m_deleteWorkoutButton);

    auto *actionRow = new QWidget(listPanel);
    auto *actionRowLayout = new QHBoxLayout(actionRow);
    actionRowLayout->setContentsMargins(0, 0, 0, 0);
    actionRowLayout->setSpacing(8);
    actionRowLayout->addWidget(m_exportTcxButton, 1);
    actionRowLayout->addWidget(m_uploadStravaButton, 1);
    listPanelLayout->addWidget(actionRow);

    // Right: detail panel
    auto *detailPanel = new QWidget(this);
    detailPanel->setObjectName("workoutDetailPanel");
    detailPanel->setStyleSheet(
        "QWidget#workoutDetailPanel {"
        "  background-color: #181825;"
        "  border-radius: 12px;"
        "  border: 1px solid #313244;"
        "}");
    auto *detailLayout = new QVBoxLayout(detailPanel);
    detailLayout->setContentsMargins(20, 16, 20, 16);
    detailLayout->setSpacing(12);

    // Title row
    auto *detailTitle = new QLabel(tr("WORKOUT DETAILS"), detailPanel);
    detailTitle->setObjectName("workoutDetailTitle");
    detailLayout->addWidget(detailTitle);

    // Stacked widget: page 0 = dials (Free Ride / Intervals), page 1 = map (Map Ride)
    m_detailStack = new QStackedWidget(detailPanel);

    // Page 0: power + HR dials
    m_workoutPowerDial->setFixedSize(250, 190);
    m_workoutHrDial->setFixedSize(250, 190);
    auto *dialPage = new QWidget();
    auto *dialRowLayout = new QHBoxLayout(dialPage);
    dialRowLayout->setContentsMargins(2, 2, 2, 2);
    dialRowLayout->setSpacing(24);
    dialRowLayout->addStretch(1);
    dialRowLayout->addWidget(m_workoutPowerDial);
    dialRowLayout->addWidget(m_workoutHrDial);
    dialRowLayout->addStretch(1);
    m_detailStack->addWidget(dialPage);

    // Page 1: satellite route map
    m_workoutMapWidget = new WorkoutMapWidget();
    m_detailStack->addWidget(m_workoutMapWidget);

    detailLayout->addWidget(m_detailStack, 1);

    // Metric cards grid (2 columns)
    auto *metricsGrid = new QWidget(detailPanel);
    auto *metricsGridLayout = new QGridLayout(metricsGrid);
    metricsGridLayout->setContentsMargins(0, 0, 0, 0);
    metricsGridLayout->setSpacing(10);

    auto makeStatCard = [&](const QString &label, QLabel **valueOut, int row, int col) {
        auto *card = new QWidget(metricsGrid);
        card->setStyleSheet(
            "background-color: #1e1e2e;"
            "border-radius: 8px;"
            "border: 1px solid #313244;");
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);
        cardLayout->setSpacing(2);

        auto *lbl = new QLabel(label, card);
        lbl->setStyleSheet("font-size: 10px; font-weight: 700; color: #6c7086;"
                           " letter-spacing: 1px; background: transparent; border: none;");
        auto *val = new QLabel("-", card);
        val->setStyleSheet("font-size: 20px; font-weight: 700; color: #cdd6f4;"
                           " background: transparent; border: none;");
        cardLayout->addWidget(lbl);
        cardLayout->addWidget(val);
        metricsGridLayout->addWidget(card, row, col);
        *valueOut = val;
    };

    makeStatCard("AVG POWER",      &m_savedAvgPowerValue,   0, 0);
    makeStatCard("AVG CADENCE",    &m_savedAvgCadenceValue, 0, 1);
    makeStatCard("AVG HEART RATE", &m_savedAvgHrValue,      1, 0);
    makeStatCard("MAX HEART RATE", &m_savedMaxHrValue,      1, 1);
    makeStatCard("AVG SPEED",      &m_savedAvgSpeedValue,   2, 0);
    makeStatCard("DURATION",       &m_savedDurationValue,   2, 1);
    makeStatCard("DISTANCE",       &m_savedDistanceValue,   3, 0);
    makeStatCard("ELEVATION GAIN", &m_savedAscentValue,     3, 1);
    // TrainerRoad / TrainingPeaks-style advanced metrics
    makeStatCard("NORMALIZED PWR", &m_savedNPValue,         4, 0);
    makeStatCard("MAX POWER",      &m_savedMaxPowerValue,   4, 1);
    makeStatCard("INTENSITY (IF)", &m_savedIFValue,         5, 0);
    makeStatCard("TSS",            &m_savedTSSValue,        5, 1);
    // Power bests
    makeStatCard("BEST 5s",        &m_savedBest5sValue,     6, 0);
    makeStatCard("BEST 1 min",     &m_savedBest1minValue,   6, 1);
    makeStatCard("BEST 5 min",     &m_savedBest5minValue,   7, 0);
    makeStatCard("BEST 20 min",    &m_savedBest20minValue,  7, 1);

    detailLayout->addWidget(metricsGrid);

    // ── Time-in-Zone stacked bar ──────────────────────────────────────────
    {
        auto *zoneSection = new QWidget(detailPanel);
        zoneSection->setStyleSheet(
            "background-color: #1e1e2e;"
            "border-radius: 8px;"
            "border: 1px solid #313244;");
        auto *zoneLay = new QVBoxLayout(zoneSection);
        zoneLay->setContentsMargins(12, 8, 12, 8);
        zoneLay->setSpacing(4);

        auto *zoneTitle = new QLabel("TIME IN ZONE", zoneSection);
        zoneTitle->setStyleSheet("font-size: 10px; font-weight: 700; color: #6c7086;"
                                 " letter-spacing: 1px; background: transparent; border: none;");
        zoneLay->addWidget(zoneTitle);

        m_zoneBar = new QWidget(zoneSection);
        m_zoneBar->setFixedHeight(24);
        m_zoneBar->setStyleSheet("background: #313244; border-radius: 4px;");
        zoneLay->addWidget(m_zoneBar);

        m_zoneLegend = new QLabel("", zoneSection);
        m_zoneLegend->setStyleSheet("font-size: 10px; color: #a6adc8;"
                                    " background: transparent; border: none;");
        m_zoneLegend->setWordWrap(true);
        zoneLay->addWidget(m_zoneLegend);

        detailLayout->addWidget(zoneSection);
    }

    detailLayout->addStretch();

    workoutsLayout->addWidget(listPanel, 2);
    workoutsLayout->addWidget(detailPanel, 3);

    m_workoutsTab = new QWidget(this);
    m_workoutsTab->setLayout(workoutsLayout);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->addTab(m_dashboard, "Live Training");
    m_tabWidget->addTab(m_workoutsTab, "Workouts");

    ui->centralwidget->layout()->addWidget(m_tabWidget);

    // ── Menu ───────────────────────────────────────────────────────────────
    auto *fileMenu   = menuBar()->addMenu(tr("&File"));
    auto *actDevices = fileMenu->addAction(tr("&Select Devices…"), this, &MainWindow::onSelectDevices);
    actDevices->setShortcut(QKeySequence("Ctrl+D"));
    fileMenu->addAction(tr("C&yclist Profile…"), this, &MainWindow::onCyclistProfile);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), this, &QWidget::close, QKeySequence::Quit);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About WattsFun"), this, [this]() {
        QMessageBox::about(this, tr("WattsFun"),
                           tr("<b>WattsFun v1.0</b><br/>"
                              "Virtual bike training app<br/>"
                              "ANT+ | Bluetooth LE | Qt 6"));
    });

    // ── Export TCX button ──────────────────────────────────────────────────
    connect(m_exportTcxButton, &QPushButton::clicked, this, [this]() {
        const int row = m_workoutList->currentRow();
        if (row < 0 || row >= m_workouts.size()) return;
        const int wIdx = m_workouts.size() - 1 - row;
        const auto &entry = m_workouts[wIdx];
        if (entry.samples.isEmpty()) {
            QMessageBox::information(this, tr("Export TCX"),
                tr("No sample data available for this workout.\n"
                   "Only workouts recorded in this session can be exported."));
            return;
        }
        const QString defaultName = entry.summary.timestamp.toString("yyyy-MM-dd_hh-mm-ss") + ".tcx";
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Export TCX File"),
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/" + defaultName,
            tr("Training Center XML (*.tcx)"));
        if (path.isEmpty()) return;

        const QByteArray tcxData = TcxExporter::generate(entry.summary, entry.samples);
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(tcxData);
            onStatusBar(tr("TCX exported to %1").arg(path));
        } else {
            QMessageBox::warning(this, tr("Export Failed"),
                tr("Could not write file:\n%1").arg(path));
        }
    });

    // ── Strava menu ────────────────────────────────────────────────────────
#ifdef STRAVA_ENABLED
    m_strava = new StravaUploader(this);

    auto *stravaMenu = menuBar()->addMenu(tr("&Strava"));
    stravaMenu->addAction(tr("&Connect to Strava…"), this, [this]() {
        if (!m_strava->hasCredentials()) {
            // Ask for credentials first
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Strava API Credentials"));
            auto *form  = new QFormLayout(dlg);
            auto *idEdit     = new QLineEdit(dlg);
            auto *secretEdit = new QLineEdit(dlg);
            secretEdit->setEchoMode(QLineEdit::Password);
            form->addRow(tr("Client ID:"),     idEdit);
            form->addRow(tr("Client Secret:"), secretEdit);
            auto *note = new QLabel(
                tr("<small>Register your app at <b>strava.com/settings/api</b>.<br/>"
                   "Set the Authorization Callback Domain to <b>localhost</b>.</small>"), dlg);
            note->setWordWrap(true);
            form->addRow(note);
            auto *buttons = new QDialogButtonBox(
                QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
            if (dlg->exec() != QDialog::Accepted
                || idEdit->text().trimmed().isEmpty()
                || secretEdit->text().trimmed().isEmpty()) {
                dlg->deleteLater();
                return;
            }
            m_strava->setCredentials(idEdit->text().trimmed(),
                                     secretEdit->text().trimmed());
            dlg->deleteLater();
        }
        m_strava->authenticate();
    });
    stravaMenu->addAction(tr("&Clear Credentials"), this, [this]() {
        QSettings s;
        s.remove("strava/clientId");
        s.remove("strava/clientSecret");
        s.remove("strava/accessToken");
        s.remove("strava/refreshToken");
        s.remove("strava/tokenExpiry");
        onStatusBar(tr("Strava credentials cleared."));
    });

    connect(m_strava, &StravaUploader::authenticated, this, [this]() {
        onStatusBar(tr("Connected to Strava!"));
    });
    connect(m_strava, &StravaUploader::authFailed, this, [this](const QString &msg) {
        QMessageBox::warning(this, tr("Strava Auth"), msg);
    });
    connect(m_strava, &StravaUploader::uploadProgress, this, [this](const QString &msg) {
        onStatusBar(msg);
    });
    connect(m_strava, &StravaUploader::uploadFinished, this,
            [this](bool success, const QString &msg, qint64 activityId) {
                if (success) {
                    // Find screenshots taken during the workout time window
                    const int row = m_workoutList->currentRow();
                    QStringList matchedScreenshots;
                    if (row >= 0 && row < m_workouts.size()) {
                        const int wIdx = m_workouts.size() - 1 - row;
                        const auto &ws = m_workouts[wIdx].summary;
                        const QDateTime start = ws.timestamp.addSecs(
                            -static_cast<qint64>(ws.duration) - 60);
                        const QDateTime end = ws.timestamp.addSecs(60);
                        const QString ssDir =
                            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                            + "/WattsFun/screenshots";
                        const QDir dir(ssDir);
                        if (dir.exists()) {
                            const auto files = dir.entryInfoList(
                                QStringList{"*.png"}, QDir::Files, QDir::Name);
                            for (const auto &fi : files) {
                                const QString base = fi.baseName();
                                const QDateTime ts = QDateTime::fromString(
                                    base, "yyyy-MM-dd_HH-mm-ss");
                                if (ts.isValid() && ts >= start && ts <= end)
                                    matchedScreenshots.append(fi.absoluteFilePath());
                            }
                        }
                    }
                    QString fullMsg = msg;
                    if (!matchedScreenshots.isEmpty()) {
                        fullMsg += tr("\n\n%1 screenshot(s) from this ride:\n%2"
                                      "\n\nYou can add these photos on the Strava "
                                      "activity page.")
                            .arg(matchedScreenshots.size())
                            .arg(matchedScreenshots.join("\n"));
                    }
                    QMessageBox::information(this, tr("Strava Upload"), fullMsg);

                    // Open the Strava activity page if we have the activity ID
                    if (activityId > 0) {
                        QDesktopServices::openUrl(QUrl(
                            QString("https://www.strava.com/activities/%1")
                                .arg(activityId)));
                    }
                } else {
                    QMessageBox::warning(this, tr("Strava Upload Failed"), msg);
                }
            });

    connect(m_uploadStravaButton, &QPushButton::clicked, this, [this]() {
        const int row = m_workoutList->currentRow();
        if (row < 0 || row >= m_workouts.size()) return;
        const int wIdx = m_workouts.size() - 1 - row;
        const auto &entry = m_workouts[wIdx];
        if (entry.samples.isEmpty()) {
            QMessageBox::information(this, tr("Upload"),
                tr("No sample data available for this workout.\n"
                   "Only workouts recorded in this session can be uploaded."));
            return;
        }
        m_strava->upload(entry.summary, entry.samples);
    });
#else
    m_uploadStravaButton->setEnabled(false);
    m_uploadStravaButton->setToolTip(tr("Install Qt6NetworkAuth to enable Strava upload"));
#endif // STRAVA_ENABLED

    // ── Toolbar buttons ────────────────────────────────────────────────────
    connect(ui->btnDeviceSettings, &QPushButton::clicked, this, &MainWindow::onSelectDevices);
    // Start/Stop button now lives inside the dashboard's mode panel
    connect(m_dashboard, &DashboardWidget::startStopClicked, this, &MainWindow::onStartStop);

    // ── Screenshot button ──────────────────────────────────────────────────
    connect(m_dashboard, &DashboardWidget::screenshotRequested, this, [this]() {
        const QString dir =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + "/WattsFun/screenshots";
        QDir().mkpath(dir);
        const QString filename = QDateTime::currentDateTime()
                                     .toString("yyyy-MM-dd_HH-mm-ss") + ".png";
        const QString path = dir + "/" + filename;
        QScreen *screen = this->screen();
        if (!screen) {
            onStatusBar(tr("Screenshot failed: no screen available."));
            return;
        }
        QPixmap shot = screen->grabWindow(this->winId());
        if (shot.save(path, "PNG")) {
            onStatusBar(tr("Screenshot saved: %1").arg(filename));
        } else {
            onStatusBar(tr("Screenshot failed to save."));
        }
    });

    // ── Dashboard trainer controls → protocol manager ──────────────────────
    connect(m_dashboard, &DashboardWidget::ergTargetChanged, this, [this](int watts) {
        if (m_active && m_running && !m_trainerId.isEmpty())
            m_active->setTargetPower(m_trainerId, watts);
    });
    connect(m_dashboard, &DashboardWidget::gradeChanged, this, [this](double pct) {
        if (m_active && m_running && !m_trainerId.isEmpty())
            m_active->setSimulationGrade(m_trainerId, pct);
        m_ant->setCurrentGrade(pct);
        m_ble->setCurrentGrade(pct);
    });

    // ── Protocol manager signals ───────────────────────────────────────────
    connectManagerSignals(m_ant);
    connectManagerSignals(m_ble);

    statusBar()->showMessage(tr("Ready – click 'Select Devices' to begin"));

    loadWorkouts();

    // Apply saved cyclist profile to both protocol managers
    {
        const auto profile = m_config.loadProfile();
        m_ant->setRiderWeightKg(profile.weightKg);
        m_ble->setRiderWeightKg(profile.weightKg);
        m_dashboard->setRiderWeightKg(profile.weightKg);
        m_dashboard->setFtp(profile.ftpWatts);
    }

    // Schedule auto-connect asynchronously so the window shows first
    qDebug() << "[MAIN] Scheduling auto-connect in 500ms...";
    QTimer::singleShot(500, this, [this]() {
        qDebug() << "[MAIN] Auto-connect timer fired";
        tryAutoConnect();
    });
}

MainWindow::~MainWindow()
{
    if (m_active) m_active->stopScan();
    delete ui;
}

// ── Protocol fallback ─────────────────────────────────────────────────────────

IProtocolManager *MainWindow::preferredManager() const
{
    if (m_ant->isAvailable()) return m_ant;
    if (m_ble->isAvailable()) return m_ble;
    return nullptr;
}

void MainWindow::connectManagerSignals(IProtocolManager *mgr)
{
    connect(mgr, &IProtocolManager::trainerDataUpdated, this, &MainWindow::onTrainerDataUpdated);
    connect(mgr, &IProtocolManager::hrmDataUpdated,     this, &MainWindow::onHrmDataUpdated);
    connect(mgr, &IProtocolManager::deviceConnected,    this, &MainWindow::onDeviceConnected);
    connect(mgr, &IProtocolManager::deviceDisconnected, this, &MainWindow::onDeviceDisconnected);
    connect(mgr, &IProtocolManager::errorOccurred,      this, &MainWindow::onError);
}

// ── Auto-connect ──────────────────────────────────────────────────────────────

void MainWindow::tryAutoConnect()
{
    qDebug() << "[MAIN] Starting auto-connect...";
    
    try {
        qDebug() << "[MAIN] Loading saved devices from config...";
        SavedDevice trainer = m_config.loadTrainer();
        SavedDevice hrm     = m_config.loadHrm();
        
        qDebug() << "[MAIN] Trainer valid:" << trainer.isValid() << "ID:" << trainer.id;
        qDebug() << "[MAIN] HRM valid:" << hrm.isValid() << "ID:" << hrm.id;
        
        if (!trainer.isValid() && !hrm.isValid()) {
            qDebug() << "[MAIN] No saved devices found, skipping auto-connect";
            return;
        }

        qDebug() << "[MAIN] Getting preferred manager...";
        IProtocolManager *mgr = preferredManager();
        if (!mgr) {
            qDebug() << "[MAIN] No protocol manager available";
            onStatusBar(tr("No ANT+ stick or BLE adapter found."));
            return;
        }

        m_active = mgr;
        QString proto = (mgr == m_ant) ? "ANT+" : "BLE";
        qDebug() << "[MAIN] Using protocol:" << proto;
        onStatusBar(tr("Auto-connecting via %1…").arg(proto));

        if (trainer.isValid()) {
            m_trainerId = trainer.id;
            qDebug() << "[MAIN] Connecting trainer:" << m_trainerId;
            m_active->connectDevice(m_trainerId);
        }
        if (hrm.isValid()) {
            m_hrmId = hrm.id;
            qDebug() << "[MAIN] Connecting HRM:" << m_hrmId;
            m_active->connectDevice(m_hrmId);
        }
        
        // Enable START button (now inside the dashboard)
        m_dashboard->setStartEnabled(true);
        qDebug() << "[MAIN] START button enabled";
        
        qDebug() << "[MAIN] Auto-connect completed";
        
    } catch (const std::exception &e) {
        qDebug() << "[MAIN] Auto-connect exception:" << e.what();
        onStatusBar(tr("Error during auto-connect"));
    } catch (...) {
        qDebug() << "[MAIN] Unknown exception in auto-connect";
        onStatusBar(tr("Error during auto-connect"));
    }
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void MainWindow::onCyclistProfile()
{
    const auto current = m_config.loadProfile();

    auto *dlg  = new QDialog(this);
    dlg->setWindowTitle(tr("Cyclist Profile"));
    auto *form = new QFormLayout(dlg);

    auto *weightSpin = new QDoubleSpinBox(dlg);
    weightSpin->setRange(30.0, 200.0);
    weightSpin->setSingleStep(0.5);
    weightSpin->setDecimals(1);
    weightSpin->setSuffix(tr(" kg"));
    weightSpin->setValue(current.weightKg);
    form->addRow(tr("Rider Weight:"), weightSpin);

    auto *ftpSpin = new QSpinBox(dlg);
    ftpSpin->setRange(50, 700);
    ftpSpin->setSingleStep(5);
    ftpSpin->setSuffix(tr(" W"));
    ftpSpin->setValue(current.ftpWatts);
    form->addRow(tr("FTP:"), ftpSpin);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    if (dlg->exec() == QDialog::Accepted) {
        CyclistProfile p;
        p.weightKg  = weightSpin->value();
        p.ftpWatts  = ftpSpin->value();
        m_config.saveProfile(p);
        m_ant->setRiderWeightKg(p.weightKg);
        m_ble->setRiderWeightKg(p.weightKg);
        m_dashboard->setRiderWeightKg(p.weightKg);
        m_dashboard->setFtp(p.ftpWatts);
        onStatusBar(tr("Cyclist profile saved (%.1f kg, %d W FTP)")
                        .arg(p.weightKg).arg(p.ftpWatts));
    }
    dlg->deleteLater();
}

void MainWindow::onSelectDevices()
{
    IProtocolManager *mgr = preferredManager();
    if (!mgr) {
        QMessageBox::warning(this, tr("No Adapter"),
            tr("No ANT+ USB stick or Bluetooth adapter was found.\n"
               "Please attach a device and try again."));
        return;
    }

    DeviceSelectionDialog dlg(mgr, m_ant, m_ble, m_config, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_trainerId = dlg.selectedTrainerId();
        m_hrmId     = dlg.selectedHrmId();
        m_active    = mgr;
        onStatusBar(tr("Devices selected. Press Start to begin."));
        m_dashboard->setStartEnabled(true);
    }
}

void MainWindow::onStartStop()
{
    qDebug() << "[MAIN] onStartStop called, m_running=" << m_running << "m_active=" << (void*)m_active
             << "trainer=" << m_trainerId << "hrm=" << m_hrmId;
    if (!m_running) {
        // ── Start ─────────────────────────────────────────────────────────
        if (!m_active) {
            QMessageBox::information(this, tr("No devices"),
                tr("Please select devices first."));
            return;
        }
        if (!m_trainerId.isEmpty()) {
            qDebug() << "[MAIN] Connecting trainer" << m_trainerId;
            m_active->connectDevice(m_trainerId);
        }
        if (!m_hrmId.isEmpty()) {
            qDebug() << "[MAIN] Connecting HRM" << m_hrmId;
            m_active->connectDevice(m_hrmId);
        }

        m_running = true;
        m_dashboard->setRunningState(true);
        m_dashboard->reset();
        m_dashboard->startWorkout();
        onStatusBar(tr("Running…"));
    } else {
        // ── Stop ──────────────────────────────────────────────────────────
        if (m_active) {
            if (!m_trainerId.isEmpty()) m_active->disconnectDevice(m_trainerId);
            if (!m_hrmId.isEmpty())     m_active->disconnectDevice(m_hrmId);
        }
        m_running = false;
        m_dashboard->setRunningState(false);
        m_dashboard->stopWorkout();

        // Auto-save TCX to Documents/WattsFun/rides/
        {
            const QString ridesDir =
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + "/WattsFun/rides";
            QDir().mkpath(ridesDir);
            const QString filename = QDateTime::currentDateTime()
                                         .toString("yyyy-MM-dd_HH-mm-ss") + ".tcx";
            QFile tcxFile(ridesDir + "/" + filename);
            if (tcxFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                auto tmpSummary = m_dashboard->currentWorkoutSummary();
                tmpSummary.timestamp = QDateTime::currentDateTime();
                tcxFile.write(TcxExporter::generate(tmpSummary,
                                                    m_dashboard->workoutSamples()));
            }
        }

        // Save workout summary
        auto summary = m_dashboard->currentWorkoutSummary();
        summary.timestamp = QDateTime::currentDateTime();
        WorkoutEntry entry;
        entry.summary = summary;
        entry.samples = m_dashboard->workoutSamples();
        m_workouts.append(entry);

        QString itemLabel = tr("%1  P:%2W  NP:%3W  TSS:%4  C:%5rpm  HR:%6/%7  Dis:%8km  Dur:%9")
            .arg(summary.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
            .arg(summary.avgPower, 0, 'f', 0)
            .arg(summary.normalizedPower, 0, 'f', 0)
            .arg(summary.tss, 0, 'f', 0)
            .arg(summary.avgCadence, 0, 'f', 0)
            .arg(summary.avgHr, 0, 'f', 0)
            .arg(summary.maxHr, 0, 'f', 0)
            .arg(summary.distance, 0, 'f', 2)
            .arg(formatDuration(summary.duration));
        m_workoutList->insertItem(0, itemLabel);
        m_workoutList->setCurrentRow(0);
        saveWorkouts();

        // Show the saved workout tab for review
        m_tabWidget->setCurrentWidget(m_workoutsTab);

        onStatusBar(tr("Stopped and workout saved."));
    }
}

void MainWindow::onTrainerDataUpdated(const TrainerData &data)
{
    qDebug() << "[MainWindow] onTrainerDataUpdated called";
    m_dashboard->updateTrainer(data);
}

void MainWindow::onHrmDataUpdated(const HrmData &data)
{
    qDebug() << "[MainWindow] onHrmDataUpdated called";
    m_dashboard->updateHrm(data);
}

void MainWindow::onDeviceConnected(const QString &id)
{
    onStatusBar(tr("Device connected: %1").arg(id));
    if (id == m_trainerId) m_trainerConnected = true;
    if (id == m_hrmId)     m_hrmConnected     = true;
    updateDeviceStatusIcons();
}

void MainWindow::onDeviceDisconnected(const QString &id)
{
    onStatusBar(tr("Device disconnected: %1").arg(id));
    if (id == m_trainerId) m_trainerConnected = false;
    if (id == m_hrmId)     m_hrmConnected     = false;
    updateDeviceStatusIcons();
}

void MainWindow::onError(const QString &msg)
{
    QMessageBox::warning(this, tr("Device Error"), msg);
    onStatusBar(tr("Error: %1").arg(msg));
}

void MainWindow::onStatusBar(const QString &msg)
{
    statusBar()->showMessage(msg, 8000);
}

void MainWindow::updateDeviceStatusIcons()
{
    auto setOpacity = [](QLabel *label, bool connected) {
        auto *effect = qobject_cast<QGraphicsOpacityEffect *>(label->graphicsEffect());
        if (!effect) {
            effect = new QGraphicsOpacityEffect(label);
            label->setGraphicsEffect(effect);
        }
        effect->setOpacity(connected ? 1.0 : 0.25);
    };

    setOpacity(ui->lblTrainerStatus, m_trainerConnected);
    ui->lblTrainerStatus->setToolTip(m_trainerConnected
        ? tr("Trainer – connected") : tr("Trainer – not connected"));

    setOpacity(ui->lblHrmStatus, m_hrmConnected);
    ui->lblHrmStatus->setToolTip(m_hrmConnected
        ? tr("Heart rate monitor – connected") : tr("Heart rate monitor – not connected"));
}

void MainWindow::loadWorkoutSummary(int index)
{
    // List is displayed newest-first, so row 0 = last element in m_workouts
    const int wIndex = (index >= 0 && index < m_workouts.size())
                       ? (m_workouts.size() - 1 - index) : -1;
    if (wIndex < 0) {
        m_detailStack->setCurrentIndex(0);
        m_workoutPowerDial->setValue(0);
        m_workoutHrDial->setValue(40);
        m_savedAvgPowerValue->setText("-");
        m_savedAvgCadenceValue->setText("-");
        m_savedAvgHrValue->setText("-");
        m_savedMaxHrValue->setText("-");
        m_savedAvgSpeedValue->setText("-");
        m_savedDurationValue->setText("-");
        m_savedDistanceValue->setText("-");
        m_savedAscentValue->setText("-");
        m_savedNPValue->setText("-");
        m_savedIFValue->setText("-");
        m_savedTSSValue->setText("-");
        m_savedMaxPowerValue->setText("-");
        m_savedBest5sValue->setText("-");
        m_savedBest1minValue->setText("-");
        m_savedBest5minValue->setText("-");
        m_savedBest20minValue->setText("-");
        m_zoneLegend->setText("");
        return;
    }

    const auto &s    = m_workouts[wIndex].summary;
    const auto &samp = m_workouts[wIndex].samples;

    // Show map if any sample has GPS data, regardless of route name
    const bool hasGps = std::any_of(samp.begin(), samp.end(),
        [](const DashboardWidget::WorkoutSample &sp){ return sp.lat != 0.0 || sp.lon != 0.0; });

    if (hasGps) {
        m_detailStack->setCurrentIndex(1);
        m_workoutMapWidget->setRoute(samp);
    } else {
        m_detailStack->setCurrentIndex(0);
        m_workoutPowerDial->setValue(s.avgPower);
        m_workoutHrDial->setValue(s.avgHr);
    }

    const auto &entry = s;

    // Zone colour helpers — exact same thresholds as the DialGauge zones in the constructor
    auto powerColor = [](double w) -> QString {
        if (w < 150) return "#a6e3a1";  // green
        if (w < 250) return "#f9e2af";  // yellow
        if (w < 350) return "#fab387";  // orange
        return "#f38ba8";               // red
    };
    auto hrColor = [](double bpm) -> QString {
        if (bpm < 120) return "#a6e3a1";  // green
        if (bpm < 150) return "#f9e2af";  // yellow
        if (bpm < 170) return "#fab387";  // orange
        return "#f38ba8";                  // red
    };
    auto setColor = [](QLabel *lbl, const QString &col) {
        lbl->setStyleSheet(QString("font-size: 20px; font-weight: 700; color: %1;"
                                   " background: transparent; border: none;").arg(col));
    };

    m_savedAvgPowerValue->setText(tr("%1 W").arg(entry.avgPower, 0, 'f', 1));
    setColor(m_savedAvgPowerValue, powerColor(entry.avgPower));
    m_savedAvgCadenceValue->setText(tr("%1 rpm").arg(entry.avgCadence, 0, 'f', 1));
    m_savedAvgHrValue->setText(tr("%1 bpm").arg(entry.avgHr, 0, 'f', 1));
    setColor(m_savedAvgHrValue, hrColor(entry.avgHr));
    m_savedMaxHrValue->setText(tr("%1 bpm").arg(entry.maxHr, 0, 'f', 0));
    setColor(m_savedMaxHrValue, hrColor(entry.maxHr));
    m_savedAvgSpeedValue->setText(tr("%1 km/h").arg(entry.avgSpeed, 0, 'f', 2));
    m_savedDurationValue->setText(formatDuration(entry.duration));
    m_savedDistanceValue->setText(tr("%1 km").arg(entry.distance, 0, 'f', 2));
    m_savedAscentValue->setText(tr("%1 m").arg(entry.totalAscent, 0, 'f', 0));

    // NP / IF / TSS / Max Power
    m_savedNPValue->setText(entry.normalizedPower > 0
        ? tr("%1 W").arg(entry.normalizedPower, 0, 'f', 0) : "-");
    setColor(m_savedNPValue, powerColor(entry.normalizedPower));
    m_savedMaxPowerValue->setText(entry.maxPower > 0
        ? tr("%1 W").arg(entry.maxPower, 0, 'f', 0) : "-");
    setColor(m_savedMaxPowerValue, powerColor(entry.maxPower));
    m_savedIFValue->setText(entry.intensityFactor > 0
        ? QString::number(entry.intensityFactor, 'f', 2) : "-");
    m_savedTSSValue->setText(entry.tss > 0
        ? QString::number(entry.tss, 'f', 0) : "-");

    // Power bests
    m_savedBest5sValue->setText(entry.bestPower5s > 0
        ? tr("%1 W").arg(entry.bestPower5s, 0, 'f', 0) : "-");
    m_savedBest1minValue->setText(entry.bestPower1min > 0
        ? tr("%1 W").arg(entry.bestPower1min, 0, 'f', 0) : "-");
    m_savedBest5minValue->setText(entry.bestPower5min > 0
        ? tr("%1 W").arg(entry.bestPower5min, 0, 'f', 0) : "-");
    m_savedBest20minValue->setText(entry.bestPower20min > 0
        ? tr("%1 W").arg(entry.bestPower20min, 0, 'f', 0) : "-");

    // ── Time-in-zone bar ─────────────────────────────────────────────────
    {
        // Delete old children of m_zoneBar
        QLayoutItem *child;
        if (m_zoneBar->layout()) {
            while ((child = m_zoneBar->layout()->takeAt(0)) != nullptr) {
                delete child->widget();
                delete child;
            }
            delete m_zoneBar->layout();
        }

        int totalZoneSec = 0;
        for (int z = 0; z < DashboardWidget::NUM_ZONES; ++z)
            totalZoneSec += entry.timeInZone[static_cast<size_t>(z)];

        auto *barLay = new QHBoxLayout(m_zoneBar);
        barLay->setContentsMargins(0, 0, 0, 0);
        barLay->setSpacing(1);

        QString legend;
        if (totalZoneSec > 0) {
            for (int z = 0; z < DashboardWidget::NUM_ZONES; ++z) {
                const int sec = entry.timeInZone[static_cast<size_t>(z)];
                if (sec <= 0) continue;
                auto *seg = new QWidget(m_zoneBar);
                seg->setStyleSheet(QString("background:%1; border-radius:2px;")
                    .arg(DashboardWidget::zoneColor(z + 1).name()));
                barLay->addWidget(seg, sec);  // stretch = seconds in zone
                const int zm = sec / 60, zs = sec % 60;
                legend += QString("Z%1: %2:%3  ")
                    .arg(z + 1)
                    .arg(zm)
                    .arg(zs, 2, 10, QChar('0'));
            }
        }
        m_zoneLegend->setText(legend.trimmed());
    }

    // Show comprehensive workout metrics in status bar
    onStatusBar(tr("Workout %1: Power %2 W, NP %3 W, TSS %4, HR %5/%6 bpm")
                .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
                .arg(entry.avgPower, 0, 'f', 1)
                .arg(entry.normalizedPower, 0, 'f', 0)
                .arg(entry.tss, 0, 'f', 0)
                .arg(entry.avgHr, 0, 'f', 1)
                .arg(entry.maxHr, 0, 'f', 0));
}

QString MainWindow::formatDuration(double seconds) const
{
    int total = static_cast<int>(seconds);
    int h = total / 3600;
    int m = (total % 3600) / 60;
    int s = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ── Persistence ───────────────────────────────────────────────────────────────

QString MainWindow::workoutsFilePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/workouts.json";
}

void MainWindow::saveWorkouts()
{
    QJsonArray arr;
    for (const auto &entry : m_workouts) {
        const auto &s = entry.summary;
        QJsonObject obj;
        obj["timestamp"]  = s.timestamp.toString(Qt::ISODate);
        obj["avgPower"]   = s.avgPower;
        obj["avgHr"]      = s.avgHr;
        obj["avgCadence"] = s.avgCadence;
        obj["avgSpeed"]   = s.avgSpeed;
        obj["maxHr"]      = s.maxHr;
        obj["duration"]   = s.duration;
        obj["distance"]   = s.distance;
        obj["totalAscent"] = s.totalAscent;
        obj["normalizedPower"] = s.normalizedPower;
        obj["intensityFactor"] = s.intensityFactor;
        obj["tss"] = s.tss;
        obj["maxPower"] = s.maxPower;
        obj["bestPower5s"]   = s.bestPower5s;
        obj["bestPower1min"] = s.bestPower1min;
        obj["bestPower5min"] = s.bestPower5min;
        obj["bestPower20min"]= s.bestPower20min;
        // Time-in-zone array (Z1..Z7)
        {
            QJsonArray tiz;
            for (int z = 0; z < DashboardWidget::NUM_ZONES; ++z)
                tiz.append(s.timeInZone[static_cast<size_t>(z)]);
            obj["timeInZone"] = tiz;
        }
        if (!s.routeName.isEmpty())
            obj["routeName"] = s.routeName;
        if (!s.trainingModeName.isEmpty())
            obj["trainingModeName"] = s.trainingModeName;
        if (!s.programName.isEmpty())
            obj["programName"] = s.programName;

        // Per-second samples  [elapsed, power, hr, cadence, speed, lat, lon, ele]
        QJsonArray samplesArr;
        for (const auto &sample : entry.samples) {
            QJsonArray sa;
            sa.append(sample.elapsed);
            sa.append(sample.power);
            sa.append(sample.hr);
            sa.append(sample.cadence);
            sa.append(sample.speed);
            sa.append(sample.lat);
            sa.append(sample.lon);
            sa.append(sample.ele);
            samplesArr.append(sa);
        }
        obj["samples"] = samplesArr;

        arr.append(obj);
    }
    QFile f(workoutsFilePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }
}

void MainWindow::loadWorkouts()
{
    QFile f(workoutsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) return;

    const QJsonArray arr = doc.array();
    for (int _i = 0; _i < arr.size(); ++_i) {
        const QJsonValue &val = arr[_i];
        if (!val.isObject()) continue;
        const QJsonObject obj = val.toObject();

        DashboardWidget::WorkoutSummary s;
        s.timestamp  = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
        s.avgPower   = obj["avgPower"].toDouble();
        s.avgHr      = obj["avgHr"].toDouble();
        s.avgCadence = obj["avgCadence"].toDouble();
        s.avgSpeed   = obj["avgSpeed"].toDouble();
        s.maxHr      = obj["maxHr"].toDouble();
        s.duration   = obj["duration"].toDouble();
        s.distance   = obj["distance"].toDouble();
        s.totalAscent = obj["totalAscent"].toDouble();
        s.normalizedPower = obj["normalizedPower"].toDouble();
        s.intensityFactor = obj["intensityFactor"].toDouble();
        s.tss = obj["tss"].toDouble();
        s.maxPower = obj["maxPower"].toDouble();
        s.bestPower5s   = obj["bestPower5s"].toDouble();
        s.bestPower1min = obj["bestPower1min"].toDouble();
        s.bestPower5min = obj["bestPower5min"].toDouble();
        s.bestPower20min= obj["bestPower20min"].toDouble();
        if (obj.contains("timeInZone")) {
            const QJsonArray tiz = obj["timeInZone"].toArray();
            for (int z = 0; z < qMin(DashboardWidget::NUM_ZONES, static_cast<int>(tiz.size())); ++z)
                s.timeInZone[static_cast<size_t>(z)] = tiz[z].toInt();
        }
        s.routeName  = obj["routeName"].toString();   // empty string if not present
        s.trainingModeName = obj["trainingModeName"].toString();
        s.programName = obj["programName"].toString();

        WorkoutEntry entry;
        entry.summary = s;

        // Restore per-second samples if present — [elapsed, power, hr, cadence, speed, lat, lon, ele]
        if (obj.contains("samples")) {
            for (const QJsonValue &sv : obj["samples"].toArray()) {
                const QJsonArray sa = sv.toArray();
                if (sa.size() >= 5) {
                    DashboardWidget::WorkoutSample sample;
                    sample.elapsed = sa[0].toDouble();
                    sample.power   = sa[1].toDouble();
                    sample.hr      = sa[2].toDouble();
                    sample.cadence = sa[3].toDouble();
                    sample.speed   = sa[4].toDouble();
                    if (sa.size() >= 8) {
                        sample.lat = sa[5].toDouble();
                        sample.lon = sa[6].toDouble();
                        sample.ele = sa[7].toDouble();
                    }
                    entry.samples.append(sample);
                }
            }
        }

        m_workouts.append(entry);

        // Compute totalAscent from samples if not stored in JSON (old workouts)
        if (s.totalAscent <= 0.0 && !entry.samples.isEmpty()) {
            double ascent = 0.0;
            for (int si = 1; si < entry.samples.size(); ++si) {
                double dEle = entry.samples[si].ele - entry.samples[si - 1].ele;
                if (dEle > 0.0) ascent += dEle;
            }
            m_workouts.last().summary.totalAscent = ascent;
        }

        // ── GPS backfill from paired TCX file ────────────────────────────────
        // Workouts saved before lat/lon/ele fields were added have 5-element
        // sample arrays. Recover GPS from the auto-saved .tcx file.
        const bool needsGps = !entry.samples.isEmpty() &&
            std::none_of(entry.samples.begin(), entry.samples.end(),
                [](const DashboardWidget::WorkoutSample &sp){ return sp.lat != 0.0 || sp.lon != 0.0; });
        if (needsGps) {
            const QString ridesDir =
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                + "/WattsFun/rides";
            const QString tcxPath = ridesDir + "/"
                + s.timestamp.toString("yyyy-MM-dd_HH-mm-ss") + ".tcx";
            QFile tcxFile(tcxPath);
            if (tcxFile.open(QIODevice::ReadOnly)) {
                QVector<std::tuple<double,double,double>> gpsPoints; // lat, lon, ele
                QXmlStreamReader xml(&tcxFile);
                while (!xml.atEnd()) {
                    if (xml.readNext() != QXmlStreamReader::StartElement) continue;
                    if (xml.name() == QLatin1String("Trackpoint")) {
                        double lat = 0, lon = 0, ele = 0;
                        bool hasPos = false;
                        while (!xml.atEnd()) {
                            xml.readNext();
                            if (xml.isStartElement()) {
                                if (xml.name() == QLatin1String("LatitudeDegrees"))  { lat = xml.readElementText().toDouble(); hasPos = true; }
                                else if (xml.name() == QLatin1String("LongitudeDegrees")) { lon = xml.readElementText().toDouble(); }
                                else if (xml.name() == QLatin1String("AltitudeMeters"))   { ele = xml.readElementText().toDouble(); }
                            } else if (xml.isEndElement() && xml.name() == QLatin1String("Trackpoint")) break;
                        }
                        if (hasPos) gpsPoints.append({lat, lon, ele});
                    }
                }
                // Stamp GPS into samples, clamping to available points
                if (!gpsPoints.isEmpty()) {
                    auto &samps = m_workouts.last().samples;
                    for (int gi = 0; gi < samps.size(); ++gi) {
                        int pi = qMin(gi, gpsPoints.size() - 1);
                        samps[gi].lat = std::get<0>(gpsPoints[pi]);
                        samps[gi].lon = std::get<1>(gpsPoints[pi]);
                        samps[gi].ele = std::get<2>(gpsPoints[pi]);
                    }
                }
            }
        }

        const QString itemLabel = tr("%1  P:%2W  NP:%3W  TSS:%4  C:%5rpm  HR:%6/%7  Dis:%8km  Dur:%9")
            .arg(s.timestamp.toString("yyyy-MM-dd hh:mm:ss"))
            .arg(s.avgPower,   0, 'f', 0)
            .arg(s.normalizedPower, 0, 'f', 0)
            .arg(s.tss,        0, 'f', 0)
            .arg(s.avgCadence, 0, 'f', 0)
            .arg(s.avgHr,      0, 'f', 0)
            .arg(s.maxHr,      0, 'f', 0)
            .arg(s.distance,   0, 'f', 2)
            .arg(formatDuration(s.duration));
        m_workoutList->insertItem(0, itemLabel);
    }
}

void MainWindow::onDeleteWorkout()
{
    const int row = m_workoutList->currentRow();
    if (row < 0 || row >= m_workouts.size()) return;
    const int wIdx = m_workouts.size() - 1 - row;

    const auto answer = QMessageBox::question(this, tr("Delete Workout"),
        tr("Delete the selected workout? This cannot be undone."),
        QMessageBox::Yes | QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    m_workouts.removeAt(wIdx);
    delete m_workoutList->takeItem(row);

    // Clear detail panel if nothing selected after removal
    if (m_workouts.isEmpty())
        loadWorkoutSummary(-1);

    m_deleteWorkoutButton->setEnabled(!m_workouts.isEmpty() &&
                                      m_workoutList->currentRow() >= 0);
    saveWorkouts();
}

// ── Style ─────────────────────────────────────────────────────────────────────

void MainWindow::applyStyleSheet()
{
    setStyleSheet(R"(
        /* ── Base ── */
        QMainWindow, QWidget {
            background-color: #11111b;
            color: #cdd6f4;
            font-family: "Segoe UI", Arial, sans-serif;
            font-size: 13px;
        }

        /* ── Header bar ── */
        QWidget#headerBar {
            background-color: #181825;
            border-bottom: 1px solid #313244;
        }
        QLabel#lblAppName {
            background: transparent;
            font-size: 18px;
            font-weight: 700;
            color: #b4befe;
            letter-spacing: 1px;
        }

        /* ── Menu bar ── */
        QMenuBar {
            background-color: #181825;
            color: #cdd6f4;
            border: none;
            padding: 2px 8px;
        }
        QMenuBar::item { padding: 4px 10px; border-radius: 4px; }
        QMenuBar::item:selected { background-color: #313244; }
        QMenu {
            background-color: #1e1e2e;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 4px;
        }
        QMenu::item { padding: 6px 20px; border-radius: 4px; }
        QMenu::item:selected { background-color: #89b4fa; color: #1e1e2e; }
        QMenu::separator { height: 1px; background: #313244; margin: 4px 8px; }

        /* ── Status bar ── */
        QStatusBar {
            background-color: #181825;
            color: #6c7086;
            font-size: 11px;
            border-top: 1px solid #313244;
        }

        /* ── Generic buttons ── */
        QPushButton {
            background-color: #313244;
            color: #cdd6f4;
            border: 1px solid #45475a;
            border-radius: 8px;
            padding: 7px 20px;
            font-weight: 600;
            font-size: 12px;
        }
        QPushButton:hover  { background-color: #45475a; border-color: #585b70; }
        QPushButton:pressed { background-color: #585b70; }
        QPushButton:disabled { color: #45475a; border-color: #313244; }

        /* ── Header Devices section ── */
        QLabel#lblDevicesHeader {
            background: transparent;
            color: #cdd6f4;
            font-weight: 700;
            font-size: 16px;
        }
        QLabel#lblTrainerStatus, QLabel#lblHrmStatus {
            background: transparent;
            font-size: 22px;
            padding: 0 4px;
        }
        QPushButton#btnDeviceSettings {
            background-color: transparent;
            border: none;
            padding: 4px 12px;
            font-size: 20px;
        }
        QPushButton#btnDeviceSettings:hover { background-color: #313244; border-radius: 8px; }

        /* ── Start / Stop button ── */
        QPushButton#btnStartStop {
            background-color: #a6e3a1;
            color: #1e1e2e;
            border: none;
            border-radius: 8px;
            padding: 7px 28px;
            font-weight: 700;
            font-size: 13px;
            min-width: 90px;
        }
        QPushButton#btnStartStop:hover { background-color: #b8f0b3; }
        QPushButton#btnStartStop[running="true"] {
            background-color: #f38ba8;
            color: #1e1e2e;
        }
        QPushButton#btnStartStop[running="true"]:hover { background-color: #f5a0b5; }

        /* ── Tab widget ── */
        QTabWidget::pane {
            border: none;
            background-color: #11111b;
        }
        QTabBar {
            background-color: #181825;
        }
        QTabBar::tab {
            background-color: transparent;
            color: #6c7086;
            padding: 10px 28px;
            font-weight: 600;
            font-size: 13px;
            border-bottom: 2px solid transparent;
        }
        QTabBar::tab:selected {
            color: #89b4fa;
            border-bottom: 2px solid #89b4fa;
        }
        QTabBar::tab:hover:!selected { color: #a6adc8; }

        /* ── Workouts list ── */
        QListWidget {
            background-color: #181825;
            border: 1px solid #313244;
            border-radius: 8px;
            padding: 4px;
        }
        QListWidget::item {
            padding: 8px 10px;
            border-radius: 6px;
            color: #cdd6f4;
        }
        QListWidget::item:selected {
            background-color: #313244;
            color: #89b4fa;
        }
        QListWidget::item:hover:!selected { background-color: #1e1e2e; }

        /* ── Scroll bars ── */
        QScrollBar:vertical {
            background: #181825;
            width: 8px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical {
            background: #45475a;
            border-radius: 4px;
            min-height: 24px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal {
            background: #181825;
            height: 8px;
            border-radius: 4px;
        }
        QScrollBar::handle:horizontal {
            background: #45475a;
            border-radius: 4px;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        /* ── Line edits / dialogs ── */
        QLineEdit {
            background-color: #1e1e2e;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 5px 8px;
            color: #cdd6f4;
        }
        QLineEdit:focus { border-color: #89b4fa; }

        /* ── Combo box (view switcher) ── */
        QComboBox {
            background-color: #1e1e2e;
            border: 1px solid #45475a;
            border-radius: 6px;
            padding: 5px 10px;
            color: #cdd6f4;
            min-width: 110px;
        }
        QComboBox:hover { border-color: #585b70; }
        QComboBox::drop-down { border: none; width: 20px; }
        QComboBox QAbstractItemView {
            background-color: #1e1e2e;
            border: 1px solid #45475a;
            border-radius: 6px;
            color: #cdd6f4;
            selection-background-color: #313244;
        }

        /* ── Dialog ── */
        QDialog { background-color: #1e1e2e; }
        QDialogButtonBox QPushButton { min-width: 80px; }

        /* ── Labels in workout detail panel ── */
        QLabel#workoutDetailTitle {
            font-size: 14px;
            font-weight: 700;
            color: #a6adc8;
            letter-spacing: 1px;
        }
    )");
}
