#include "dashboardwidget.h"
#include "traininglibrary.h"
#include "workouteditordialog.h"

#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QFont>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QXmlStreamReader>
#include <QFile>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSet>
#include <cmath>
#include <algorithm>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMouseEvent>
#include <QWheelEvent>

// ──────────────────────────────────────────────────────────────────────────────
// DialGauge
// ──────────────────────────────────────────────────────────────────────────────

DialGauge::DialGauge(const QString &label, const QString &unit, QWidget *parent)
    : QWidget(parent)
    , m_labelText(label)
    , m_unitText(unit)
{
    setStyleSheet("DialGauge { background-color: #24273a; border-radius: 10px; }");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_value = m_min;
}

void DialGauge::setValue(double value)
{
    if (value < m_min) value = m_min;
    if (value > m_max) value = m_max;

    m_value = value;
    update();
}

void DialGauge::setRange(double min, double max)
{
    m_min = min;
    m_max = max;
    if (m_value < m_min) m_value = m_min;
    if (m_value > m_max) m_value = m_max;
    update();
}

void DialGauge::setZones(const QVector<QPair<double, QColor>> &zones)
{
    m_zones = zones;
    std::sort(m_zones.begin(), m_zones.end(), [](const QPair<double, QColor> &a, const QPair<double, QColor> &b) {
        return a.first < b.first;
    });
    update();
}

void DialGauge::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();
    int margin = 12;

    const bool compactMode = (w < 320);

    if (compactMode) {
        QPoint center(w / 2, h / 2 - 6);
        int radius = qMax(24, qMin(w, h) / 2 - 28);
        int ringThickness = qBound(10, static_cast<int>(radius * 0.30), 20);

        // Gauge face
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#1e1e2e"));
        p.drawEllipse(center, radius + 8, radius + 8);

        // Track ring
        QPen trackPen(QColor("#313244"), ringThickness, Qt::SolidLine, Qt::FlatCap);
        p.setPen(trackPen);
        QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
        double startAngle = 120.0;
        double totalSpan = 300.0;
        p.drawArc(arcRect, static_cast<int>((360 - (startAngle + totalSpan)) * 16), static_cast<int>(totalSpan * 16));

        // Zones
        if (!m_zones.isEmpty() && m_max > m_min) {
            double currentStart = m_min;
            double angleCursor = startAngle;
            for (int i = 0; i < m_zones.size(); ++i) {
                double zoneStart = qMax(currentStart, m_zones[i].first);
                double zoneEnd = (i + 1 < m_zones.size() ? m_zones[i + 1].first : m_max);
                zoneEnd = qBound(m_min, zoneEnd, m_max);
                if (zoneEnd <= zoneStart) continue;
                double zoneSpan = (zoneEnd - zoneStart) / (m_max - m_min) * totalSpan;

                QPen zonePen(m_zones[i].second, ringThickness, Qt::SolidLine, Qt::FlatCap);
                p.setPen(zonePen);
                p.drawArc(arcRect,
                          static_cast<int>((360 - (angleCursor + zoneSpan)) * 16),
                          static_cast<int>(zoneSpan * 16));

                angleCursor += zoneSpan;
                currentStart = zoneEnd;
            }
        }

        // Needle
        if (m_max > m_min) {
            double valueRatio = qBound(0.0, (m_value - m_min) / (m_max - m_min), 1.0);
            double needleAngle = startAngle + valueRatio * totalSpan;
            QPoint needleEnd(center.x() + (radius - ringThickness / 2) * qCos(qDegreesToRadians(360 - needleAngle)),
                             center.y() - (radius - ringThickness / 2) * qSin(qDegreesToRadians(360 - needleAngle)));

            QPen needlePen(Qt::white, qBound(4, ringThickness / 2, 8), Qt::SolidLine, Qt::RoundCap);
            p.setPen(needlePen);
            p.drawLine(center, needleEnd);

            p.setPen(Qt::NoPen);
            p.setBrush(Qt::white);
            p.drawEllipse(center, 6, 6);
        }

        // Value + unit below the needle hub to avoid overlap with the needle.
        QColor valueColor("#cdd6f4");
        for (int i = 0; i < m_zones.size(); ++i) {
            if (m_value >= m_zones[i].first) {
                valueColor = m_zones[i].second;
            } else {
                break;
            }
        }

        int valuePx = qBound(14, static_cast<int>(radius * 0.30), 24);
        QFont valueFont("Segoe UI", valuePx, QFont::Bold);
        p.setFont(valueFont);
        p.setPen(valueColor);
        const int valueTop = center.y() + qBound(10, static_cast<int>(radius * 0.22), 20);
        p.drawText(QRect(center.x() - radius, valueTop, radius * 2, 28),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   QString::number(m_value, 'f', 0));

        QFont unitFont("Segoe UI", qBound(9, valuePx / 3, 13), QFont::DemiBold);
        p.setFont(unitFont);
        p.setPen(QColor("#a6adc8"));
        p.drawText(QRect(center.x() - radius, valueTop + 20, radius * 2, 18),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   m_unitText);

        // Single label at the bottom
        p.setFont(QFont("Segoe UI", 10, QFont::DemiBold));
        p.setPen(QColor("#6c7086"));
        p.drawText(QRect(margin, h - 24, w - 2 * margin, 18), Qt::AlignCenter, m_labelText);
        return;
    }

    // Dial area on the left and textual area on the right
    QRect dialRect(margin, margin + 18, qMax(180, w * 55 / 100), h - (2 * margin + 36));
    QPoint center(dialRect.center());
    int radius = qMin(dialRect.width(), dialRect.height()) / 2 - 14;

    // Background and gauge face
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#1e1e2e"));
    p.drawEllipse(center, radius + 4, radius + 4);

    // Gauge track
    QPen trackPen(QColor("#313244"), 26, Qt::SolidLine, Qt::FlatCap);
    p.setPen(trackPen);
    QRectF arcRect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    double startAngle = 120.0;
    double totalSpan = 300.0;
    p.drawArc(arcRect, static_cast<int>((360 - (startAngle + totalSpan)) * 16), static_cast<int>(totalSpan * 16));

    // Draw zones
    if (!m_zones.isEmpty() && m_max > m_min) {
        double currentStart = m_min;
        double angleCursor = startAngle;
        for (int i = 0; i < m_zones.size(); ++i) {
            double zoneStart = qMax(currentStart, m_zones[i].first);
            double zoneEnd = (i + 1 < m_zones.size() ? m_zones[i+1].first : m_max);
            zoneEnd = qBound(m_min, zoneEnd, m_max);
            if (zoneEnd <= zoneStart) continue;
            double zoneSpan = (zoneEnd - zoneStart) / (m_max - m_min) * totalSpan;

            QPen zonePen(m_zones[i].second, 28, Qt::SolidLine, Qt::FlatCap);
            p.setPen(zonePen);
            p.drawArc(arcRect,
                      static_cast<int>((360 - (angleCursor + zoneSpan)) * 16),
                      static_cast<int>(zoneSpan * 16));

            angleCursor += zoneSpan;
            currentStart = zoneEnd;
        }
    }

    // DIAL needle
    if (m_max > m_min) {
        double valueRatio = qBound(0.0, (m_value - m_min) / (m_max - m_min), 1.0);
        double needleAngle = startAngle + valueRatio * totalSpan;
        QPoint needleEnd(center.x() + (radius - 18) * qCos(qDegreesToRadians(360 - needleAngle)),
                         center.y() - (radius - 18) * qSin(qDegreesToRadians(360 - needleAngle)));

        QPen needlePen(Qt::white, 10, Qt::SolidLine, Qt::RoundCap);
        p.setPen(needlePen);
        p.drawLine(center, needleEnd);

        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawEllipse(center, 7, 7);
    }

    // Label and value text
    QRect rightArea(dialRect.right() + 12, margin + 10, w - dialRect.right() - margin - 14, h - 2 * margin - 20);
    p.setPen(QColor("#a6adc8"));
    p.setFont(QFont("Segoe UI", 12, QFont::DemiBold));
    p.drawText(QRect(rightArea.x(), rightArea.y(), rightArea.width(), 22), Qt::AlignLeft | Qt::AlignVCenter, m_labelText);

    // Value in colored text
    QColor valueColor("#cdd6f4");
    for (int i = 0; i < m_zones.size(); ++i) {
        if (m_value >= m_zones[i].first) {
            valueColor = m_zones[i].second;
        } else {
            break;
        }
    }

    QFont valueFont("Segoe UI", 34, QFont::Bold);
    p.setFont(valueFont);
    p.setPen(valueColor);
    QString valueStr = QString::number(m_value, 'f', 0);
    QFontMetrics valueFm(valueFont);
    int valueW = valueFm.horizontalAdvance(valueStr);
    int valueY = rightArea.y() + 30;
    p.drawText(QRect(rightArea.x(), valueY, rightArea.width(), 50), Qt::AlignLeft | Qt::AlignVCenter, valueStr);

    // Unit sits to the right of the value, baseline-aligned (slightly lower to sit at text base)
    QFont unitFont("Segoe UI", 13);
    p.setFont(unitFont);
    p.setPen(QColor("#6c7086"));
    int unitX = rightArea.x() + valueW + 5;
    int unitY = valueY + 50 - QFontMetrics(unitFont).height() - 2;
    p.drawText(QRect(unitX, unitY, rightArea.width() - valueW - 5, QFontMetrics(unitFont).height() + 4),
               Qt::AlignLeft | Qt::AlignVCenter, m_unitText);

    // Draw gauge name at the base
    p.setPen(QColor("#6c7086"));
    p.setFont(QFont("Segoe UI", 10));
    p.drawText(QRect(dialRect.x(), dialRect.bottom() + 4, dialRect.width(), 20), Qt::AlignCenter, m_labelText);
}

// ──────────────────────────────────────────────────────────────────────────────
// MetricTile
// ──────────────────────────────────────────────────────────────────────────────

MetricTile::MetricTile(const QString &label, const QString &unit,
                       const QColor &accent, QWidget *parent)
    : QWidget(parent)
    , m_unit(unit)
{
    auto *root  = new QVBoxLayout(this);
    root->setContentsMargins(14, 10, 14, 10);
    root->setSpacing(2);

    // Accent left border + card background
    setStyleSheet(QString(
        "MetricTile {"
        "  background-color: #1e1e2e;"
        "  border-radius: 10px;"
        "  border-left: 4px solid %1;"
        "}").arg(accent.name()));

    auto *lbl = new QLabel(label, this);
    lbl->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    lbl->setStyleSheet(
        "font-size: 10px; color: #6c7086; font-weight: 700;"
        " letter-spacing: 1px; background: transparent; border: none;");

    m_valueLabel = new QLabel("–", this);
    m_valueLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_valueLabel->setStyleSheet(
        QString("font-size: 36px; font-weight: 800; color: %1;"
                " background: transparent; border: none;").arg(accent.name()));

    m_unitLabel = new QLabel(unit, this);
    m_unitLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_unitLabel->setStyleSheet(
        "font-size: 14px; color: #585b70; background: transparent; border: none;");

    root->addWidget(lbl);
    root->addWidget(m_valueLabel);
    root->addWidget(m_unitLabel);
}

void MetricTile::setValue(double v)
{
    m_valueLabel->setText(QString::number(v, 'f', 1));
}

void MetricTile::setValue(int v)
{
    m_valueLabel->setText(QString::number(v));
}

void MetricTile::setValue(const QString &v)
{
    m_valueLabel->setText(v);
}

void MetricTile::setNoData()
{
    m_valueLabel->setText("–");
}

// ──────────────────────────────────────────────────────────────────────────────
// LiveChartWidget – QPainter-based dual-axis line chart
// ──────────────────────────────────────────────────────────────────────────────

LiveChartWidget::LiveChartWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(220);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("border: 1px solid #313244; border-radius: 8px;");
}

void LiveChartWidget::appendPower(double x, double y) { m_powerData.append({x, y}); update(); }
void LiveChartWidget::appendHr(double x, double y)    { m_hrData.append({x, y}); update(); }

void LiveChartWidget::clearSeries()
{
    m_powerData.clear();
    m_hrData.clear();
    update();
}

void LiveChartWidget::removePowerBefore(double x)
{
    while (!m_powerData.isEmpty() && m_powerData.first().x() < x)
        m_powerData.removeFirst();
}

void LiveChartWidget::removeHrBefore(double x)
{
    while (!m_hrData.isEmpty() && m_hrData.first().x() < x)
        m_hrData.removeFirst();
}

void LiveChartWidget::setXRange(double mn, double mx)      { m_xMin = mn; m_xMax = mx; update(); }
void LiveChartWidget::setYLeftRange(double mn, double mx)   { m_yLeftMin = mn; m_yLeftMax = mx; update(); }
void LiveChartWidget::setYRightRange(double mn, double mx)  { m_yRightMin = mn; m_yRightMax = mx; update(); }

QRectF LiveChartWidget::plotArea() const
{
    const double l = 68, r = 82, t = 40, b = 45;
    return QRectF(l, t, width() - l - r, height() - t - b);
}

double LiveChartWidget::niceStep(double range, int targetTicks)
{
    if (range <= 0) return 1;
    double rough = range / targetTicks;
    double mag   = std::pow(10.0, std::floor(std::log10(rough)));
    double norm  = rough / mag;
    double nice;
    if      (norm < 1.5) nice = 1;
    else if (norm < 3.0) nice = 2;
    else if (norm < 7.0) nice = 5;
    else                 nice = 10;
    return nice * mag;
}

static QString formatTimeTick(double seconds)
{
    int totalSec = static_cast<int>(seconds + 0.5);
    if (totalSec < 0) totalSec = 0;
    if (seconds < 120.0) {
        return QString("%1 s").arg(totalSec);
    } else if (seconds < 3600.0) {
        int m = totalSec / 60;
        int s = totalSec % 60;
        return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    } else {
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    }
}

void LiveChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Full background
    p.fillRect(rect(), QColor("#11111b"));

    QRectF plot = plotArea();
    if (plot.width() < 10 || plot.height() < 10) return;

    // Plot area background
    p.fillRect(plot, QColor("#181825"));

    const QColor gridCol("#313244");
    const QColor labelCol("#a6adc8");
    const QColor titleCol("#cdd6f4");
    const QColor powerCol("#89b4fa");
    const QColor hrCol("#f38ba8");

    QFont labelFont = font();
    labelFont.setPixelSize(10);
    QFont titleFont = font();
    titleFont.setPixelSize(11);
    titleFont.setBold(true);
    QFont mainFont = font();
    mainFont.setPixelSize(14);
    mainFont.setBold(true);

    // ── Y-axis left (Power) ──────────────────────────────────────────────
    {
        double range = m_yLeftMax - m_yLeftMin;
        double step  = niceStep(range, 5);
        double first = std::ceil(m_yLeftMin / step) * step;
        p.setFont(labelFont);
        for (double v = first; v <= m_yLeftMax + 0.5 * step; v += step) {
            if (v > m_yLeftMax) break;
            double py = plot.bottom() - (v - m_yLeftMin) / range * plot.height();
            p.setPen(QPen(gridCol, 1));
            p.drawLine(QPointF(plot.left(), py), QPointF(plot.right(), py));
            p.setPen(powerCol);
            p.drawText(QRectF(0, py - 8, plot.left() - 4, 16),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(static_cast<int>(v)));
        }
        p.save();
        p.setFont(titleFont);
        p.setPen(powerCol);
        p.translate(14, plot.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-50, -8, 100, 16), Qt::AlignCenter, "Power (W)");
        p.restore();
    }

    // ── Y-axis right (Heart Rate) ───────────────────────────────────────
    {
        double range = m_yRightMax - m_yRightMin;
        double step  = niceStep(range, 5);
        double first = std::ceil(m_yRightMin / step) * step;
        p.setFont(labelFont);
        for (double v = first; v <= m_yRightMax + 0.5 * step; v += step) {
            if (v > m_yRightMax) break;
            double py = plot.bottom() - (v - m_yRightMin) / range * plot.height();
            p.setPen(hrCol);
            p.drawText(QRectF(plot.right() + 4, py - 8, 60, 16),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QString::number(static_cast<int>(v)));
        }
        p.save();
        p.setFont(titleFont);
        p.setPen(hrCol);
        p.translate(width() - 14, plot.center().y());
        p.rotate(90);
        p.drawText(QRectF(-80, -8, 160, 16), Qt::AlignCenter, "Heart Rate (bpm)");
        p.restore();
    }

    // ── X-axis (Time) ────────────────────────────────────────────────────
    {
        double span = m_xMax - m_xMin;
        if (span <= 0) span = 1;
        double interval;
        if      (span <= 60)   interval = 10;
        else if (span <= 180)  interval = 30;
        else if (span <= 600)  interval = 60;
        else if (span <= 1800) interval = 300;
        else                   interval = 600;

        double first = std::ceil(m_xMin / interval) * interval;
        p.setFont(labelFont);
        for (double t = first; t <= m_xMax; t += interval) {
            double px = plot.left() + (t - m_xMin) / span * plot.width();
            p.setPen(QPen(gridCol, 1));
            p.drawLine(QPointF(px, plot.top()), QPointF(px, plot.bottom()));
            p.setPen(labelCol);
            p.drawText(QRectF(px - 30, plot.bottom() + 2, 60, 16),
                       Qt::AlignHCenter | Qt::AlignTop, formatTimeTick(t));
        }
        p.setFont(titleFont);
        p.setPen(titleCol);
        p.drawText(QRectF(plot.left(), plot.bottom() + 22, plot.width(), 16),
                   Qt::AlignCenter, "Time");
    }

    // ── Plot border ──────────────────────────────────────────────────────
    p.setPen(QPen(gridCol, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);

    // ── Series (clipped to plot area) ────────────────────────────────────
    p.save();
    p.setClipRect(plot);

    double xSpan = m_xMax - m_xMin;
    if (xSpan <= 0) xSpan = 1;

    auto mapPt = [&](const QPointF &pt, double yMin, double yMax) -> QPointF {
        double px = plot.left() + (pt.x() - m_xMin) / xSpan * plot.width();
        double yRange = yMax - yMin;
        if (yRange <= 0) yRange = 1;
        double py = plot.bottom() - (pt.y() - yMin) / yRange * plot.height();
        return {px, py};
    };

    // Power line
    if (m_powerData.size() >= 2) {
        QPainterPath path;
        path.moveTo(mapPt(m_powerData[0], m_yLeftMin, m_yLeftMax));
        for (int i = 1; i < m_powerData.size(); ++i)
            path.lineTo(mapPt(m_powerData[i], m_yLeftMin, m_yLeftMax));
        p.setPen(QPen(powerCol, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Heart-rate line
    if (m_hrData.size() >= 2) {
        QPainterPath path;
        path.moveTo(mapPt(m_hrData[0], m_yRightMin, m_yRightMax));
        for (int i = 1; i < m_hrData.size(); ++i)
            path.lineTo(mapPt(m_hrData[i], m_yRightMin, m_yRightMax));
        p.setPen(QPen(hrCol, 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    p.restore();

    // ── Title ────────────────────────────────────────────────────────────
    p.setFont(mainFont);
    p.setPen(titleCol);
    p.drawText(QRectF(plot.left(), 4, plot.width(), 28),
               Qt::AlignCenter, "Live Performance");

    // ── Legend ────────────────────────────────────────────────────────────
    {
        p.setFont(labelFont);
        int lx = static_cast<int>(plot.right()) - 200;
        int ly = 8;
        p.setPen(QPen(powerCol, 2));
        p.drawLine(lx, ly + 6, lx + 20, ly + 6);
        p.setPen(titleCol);
        p.drawText(lx + 24, ly + 11, "Power (W)");
        lx += 100;
        p.setPen(QPen(hrCol, 2));
        p.drawLine(lx, ly + 6, lx + 20, ly + 6);
        p.setPen(titleCol);
        p.drawText(lx + 24, ly + 11, "Heart Rate (bpm)");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// DashboardWidget
// ──────────────────────────────────────────────────────────────────────────────

DashboardWidget::DashboardWidget(QWidget *parent)
    : QWidget(parent)
{
    m_rootLayout = new QVBoxLayout(this);
    auto *root = m_rootLayout;
    root->setSpacing(10);
    root->setContentsMargins(14, 12, 14, 12);

    // View switcher
    m_viewSwitcher = new QComboBox(this);
    m_viewSwitcher->addItem("Chart", ChartView);
    m_viewSwitcher->addItem("Dials", DialView);
    connect(m_viewSwitcher, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DashboardWidget::onViewChanged);

    buildTiles();
    buildDialView();
    buildChart();

    // Create view widgets
    m_tileRowWidget = tileRow();
    m_dialRowWidget = dialRow();

    buildModePanel();

    root->addWidget(m_tileRowWidget);  // Always visible

    // ── Middle row: mode panel on left, chart/dials on right ─────────────
    auto *middleRow = new QWidget(this);
    m_middleLay = new QHBoxLayout(middleRow);
    m_middleLay->setContentsMargins(0, 0, 0, 0);
    m_middleLay->setSpacing(10);

    // Left: mode panel — stretch=2 gives it ~40% of the row
    m_modePanel->setMinimumWidth(240);
    m_modePanel->setMaximumWidth(QWIDGETSIZE_MAX);
    m_middleLay->addWidget(m_modePanel, 2);

    // Right: chart/dials — stretch=3 gives it ~60%
    m_chartContainer = new QWidget(middleRow);
    m_chartLayout = new QVBoxLayout(m_chartContainer);
    m_chartLayout->setContentsMargins(0, 0, 0, 0);
    m_chartLayout->setSpacing(0);

    auto *switcherRow = new QWidget(m_chartContainer);
    auto *switcherLayout = new QHBoxLayout(switcherRow);
    switcherLayout->setContentsMargins(0, 4, 0, 4);
    switcherLayout->addStretch();
    auto *viewLabel = new QLabel("View:", switcherRow);
    viewLabel->setStyleSheet("color: #6c7086; font-size: 11px; font-weight: 600;");
    switcherLayout->addWidget(viewLabel);
    switcherLayout->addWidget(m_viewSwitcher);
    m_chartLayout->addWidget(switcherRow, 0);

    m_chartLayout->addWidget(m_chartView);
    m_chartLayout->addWidget(m_dialRowWidget);

    m_middleLay->addWidget(m_chartContainer, 3);  // chart stretches
    root->addWidget(middleRow, 1);

    // Start with chart view
    onViewChanged(0);

    // Chart update timer (1 s)
    connect(&m_chartTimer, &QTimer::timeout, this, &DashboardWidget::onChartTick);
    m_chartTimer.start(static_cast<int>(CHART_TICK_S * 1000));

    // Map position timer (~50 ms / 20 Hz) — interpolates distance between 1s chart ticks
    // so the bike marker and map centre move smoothly rather than jumping once per second.
    connect(&m_mapTimer, &QTimer::timeout, this, &DashboardWidget::onMapTimerTick);
    m_mapTimer.start(50);

    // Interval timer – connected once here, started/stopped on workout start/stop
    connect(&m_intervalTimer, &QTimer::timeout, this, &DashboardWidget::onIntervalTick);
}

// ── private – tiles ───────────────────────────────────────────────────────────

void DashboardWidget::buildTiles()
{
    // Colours inspired by Catppuccin Mocha palette
    m_tilePower      = new MetricTile("POWER",      "W",   QColor("#89b4fa"), this);
    m_tileCadence    = new MetricTile("CADENCE",    "rpm", QColor("#a6e3a1"), this);
    m_tileSpeed      = new MetricTile("SPEED",      "km/h",QColor("#89dceb"), this);
    m_tileHr         = new MetricTile("HEART RATE", "bpm", QColor("#f38ba8"), this);
    m_tileDuration   = new MetricTile("DURATION",   "",    QColor("#f5c2e7"), this);
    m_tileDistance   = new MetricTile("DISTANCE",   "km",  QColor("#cba6f7"), this);
    m_tilePwrRatio   = new MetricTile("PWR RATIO",  "W/kg",QColor("#fab387"), this);

    // Average tiles (slightly different colors)
    m_tileAvgPower      = new MetricTile("AVG POWER",      "W",   QColor("#74c7ec"), this);
    m_tileAvgCadence    = new MetricTile("AVG CADENCE",    "rpm", QColor("#94e2d5"), this);
    m_tileAvgSpeed      = new MetricTile("AVG SPEED",      "km/h",QColor("#89dceb"), this);
    m_tileAvgHr         = new MetricTile("AVG HEART RATE", "bpm", QColor("#eba0ac"), this);
    m_tileAvgPwrRatio   = new MetricTile("AVG PWR RATIO",  "W/kg",QColor("#fab387"), this);

    // (NP / IF / TSS are computed internally for saved-workout summaries,
    //  but no longer shown as live tiles.)
}

// Helper called from constructor to build the tile row widget
// Layout: each metric column has LIVE tile on top, AVG tile below (where applicable).
// Columns: Power | Cadence | Speed | HR | Duration | Distance
QWidget *DashboardWidget::tileRow()
{
    auto *frame  = new QWidget(this);
    auto *outerLayout = new QHBoxLayout(frame);
    outerLayout->setSpacing(10);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Helper: build one column with a live tile and optional avg tile below
    auto makeCol = [&](MetricTile *live, MetricTile *avg) {
        auto *col = new QWidget(frame);
        auto *colLayout = new QVBoxLayout(col);
        colLayout->setSpacing(4);
        colLayout->setContentsMargins(0, 0, 0, 0);
        colLayout->addWidget(live, 1);
        if (avg)
            colLayout->addWidget(avg, 1);
        else
            colLayout->addStretch(1); // keep height consistent
        outerLayout->addWidget(col, 1);
    };

    makeCol(m_tilePower,      m_tileAvgPower);
    makeCol(m_tilePwrRatio,   m_tileAvgPwrRatio);
    makeCol(m_tileCadence,    m_tileAvgCadence);
    makeCol(m_tileSpeed,      m_tileAvgSpeed);
    makeCol(m_tileHr,         m_tileAvgHr);
    // Duration + Distance share one column (stacked)
    {
        auto *col = new QWidget(frame);
        auto *colLayout = new QVBoxLayout(col);
        colLayout->setSpacing(4);
        colLayout->setContentsMargins(0, 0, 0, 0);
        colLayout->addWidget(m_tileDuration, 1);
        colLayout->addWidget(m_tileDistance, 1);
        outerLayout->addWidget(col, 1);
    }
    return frame;
}

// ── private – mode panel ──────────────────────────────────────────────────────

void DashboardWidget::buildModePanel()
{
    m_modePanel = new QWidget(this);
    m_modePanel->setObjectName("modePanel");
    m_modePanel->setStyleSheet(
        "QWidget#modePanel {"
        "  background-color: #1e1e2e;"
        "  border-radius: 10px;"
        "  border: 1px solid #313244;"
        "}"
        "QPushButton#modeActive {"
        "  background-color: #89b4fa; color: #1e1e2e;"
        "  border: 1px solid #89b4fa; border-radius: 6px;"
        "  padding: 5px 14px; font-size: 12px; font-weight: 700;"
        "}"
        "QPushButton#modeInactive {"
        "  background-color: #181825; color: #6c7086;"
        "  border: 1px solid #45475a; border-radius: 6px;"
        "  padding: 5px 14px; font-size: 12px; font-weight: 700;"
        "}"
        "QPushButton#modeInactive:hover { background-color: #313244; color: #cdd6f4; }"
        "QPushButton#stepBtn {"
        "  background-color: #313244; color: #cdd6f4;"
        "  font-size: 18px; font-weight: 900;"
        "  min-width: 38px; max-width: 38px; padding: 2px 0px;"
        "  border-radius: 6px; border: 1px solid #45475a;"
        "}"
        "QPushButton#stepBtn:hover { background-color: #45475a; }"
        "QPushButton#stepBtn:pressed { background-color: #585b70; }"
        "QPushButton#startBtn {"
        "  background-color: #a6e3a1; color: #1e1e2e;"
        "  font-size: 14px; font-weight: 800;"
        "  border: 1px solid #a6e3a1; border-radius: 8px;"
        "  padding: 6px 28px; min-width: 100px;"
        "}"
        "QPushButton#startBtn:hover { background-color: #b8f0b3; }"
        "QPushButton#startBtn[running=\"true\"] {"
        "  background-color: #f38ba8; border-color: #f38ba8; color: #1e1e2e;"
        "}"
        "QPushButton#startBtn[running=\"true\"]:hover { background-color: #eba0ac; }"
        "QPushButton#startBtn:disabled {"
        "  background-color: #45475a; color: #6c7086; border-color: #45475a;"
        "}"
        "QPushButton#pauseBtn {"
        "  background-color: #fab387; color: #1e1e2e;"
        "  font-size: 14px; font-weight: 800;"
        "  border: 1px solid #fab387; border-radius: 8px;"
        "  padding: 6px 8px;"
        "}"
        "QPushButton#pauseBtn:hover { background-color: #fcc9a8; }"
        "QPushButton#pauseBtn[paused=\"true\"] {"
        "  background-color: #a6e3a1; border-color: #a6e3a1;"
        "}"
        "QPushButton#pauseBtn[paused=\"true\"]:hover { background-color: #b8f0b3; }"
        "QPushButton#pauseBtn:disabled {"
        "  background-color: #45475a; color: #6c7086; border-color: #45475a;"
        "}"
    );

    auto *outerLay = new QVBoxLayout(m_modePanel);
    outerLay->setContentsMargins(10, 8, 10, 8);
    outerLay->setSpacing(6);

    // ── START/STOP + PAUSE button row ────────────────────────────────────
    {
        auto *btnRow = new QWidget(m_modePanel);
        auto *btnLay = new QHBoxLayout(btnRow);
        btnLay->setContentsMargins(0, 0, 0, 0);
        btnLay->setSpacing(6);

        m_btnStartStop = new QPushButton("START", btnRow);
        m_btnStartStop->setObjectName("startBtn");
        m_btnStartStop->setEnabled(false);
        m_btnStartStop->setProperty("running", false);

        m_btnPause = new QPushButton("\u23f8 PAUSE", btnRow);
        m_btnPause->setObjectName("pauseBtn");
        m_btnPause->setEnabled(false);
        m_btnPause->setProperty("paused", false);

        m_btnScreenshot = new QPushButton(QString::fromUtf8("\xf0\x9f\x93\xb7"), btnRow);
        m_btnScreenshot->setToolTip("Take screenshot");
        m_btnScreenshot->setStyleSheet(
            "QPushButton {"
            "  background: rgba(30,30,46,210); color: #cdd6f4;"
            "  border: 1px solid #585b70; border-radius: 6px;"
            "  font-size: 18px; padding: 4px 8px;"
            "  min-width: 36px; max-width: 36px;"
            "}"
            "QPushButton:hover { background: rgba(69,71,90,230); }"
            "QPushButton:pressed { background: rgba(88,91,112,255); }");
        connect(m_btnScreenshot, &QPushButton::clicked,
                this, &DashboardWidget::screenshotRequested);

        btnLay->addWidget(m_btnStartStop, 3);
        btnLay->addWidget(m_btnPause,     2);
        btnLay->addWidget(m_btnScreenshot, 0);
        outerLay->addWidget(btnRow);
    }

    // ── Mode tab buttons ──────────────────────────────────────────────────
    auto *tabRow = new QWidget(m_modePanel);
    auto *tabLay = new QHBoxLayout(tabRow);
    tabLay->setContentsMargins(0, 0, 0, 0);
    tabLay->setSpacing(4);

    m_btnModeFreeRide = new QPushButton("Free Ride",  tabRow);
    m_btnModeInterval = new QPushButton("Intervals",  tabRow);
    m_btnModeMap      = new QPushButton("Map",         tabRow);

    tabLay->addWidget(m_btnModeFreeRide, 1);
    tabLay->addWidget(m_btnModeInterval, 1);
    tabLay->addWidget(m_btnModeMap,      1);
    outerLay->addWidget(tabRow);

    // ── Stacked content pages ─────────────────────────────────────────────
    m_modeStack = new QStackedWidget(m_modePanel);

    auto *freeRidePage = new QWidget(m_modeStack);
    buildFreeRideTab(freeRidePage);
    m_modeStack->addWidget(freeRidePage);

    auto *intervalPage = new QWidget(m_modeStack);
    buildIntervalTab(intervalPage);
    m_modeStack->addWidget(intervalPage);

    // Load persisted programs from disk after the tab widgets exist
    refreshProgramList();

    auto *mapPage = new QWidget(m_modeStack);
    buildMapTab(mapPage);
    m_modeStack->addWidget(mapPage);

    // Load persisted GPX routes from disk after the tab widgets exist
    refreshRouteList();

    outerLay->addWidget(m_modeStack, 1);

    // ── Mode button connections ───────────────────────────────────────────
    // Restores tiles to root top and resets all layout state (used by Free Ride)
    auto restoreNormalLayout = [this]() {
        if (m_mapLayoutActive || m_intervalLayoutActive) {
            m_chartLayout->removeWidget(m_tileRowWidget);
            m_rootLayout->insertWidget(0, m_tileRowWidget);
            if (m_mapLayoutActive) {
                m_middleLay->setStretch(0, 2);
                m_middleLay->setStretch(1, 3);
            }
            m_mapLayoutActive      = false;
            m_intervalLayoutActive = false;
        }
    };
    connect(m_btnModeFreeRide, &QPushButton::clicked, this, [this, restoreNormalLayout]() {
        m_trainingMode = FreeRideMode;
        m_modeStack->setCurrentIndex(0);
        restoreNormalLayout();
        m_btnModeFreeRide->setObjectName("modeActive");
        m_btnModeInterval->setObjectName("modeInactive");
        m_btnModeMap->setObjectName("modeInactive");
        for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
            { b->style()->unpolish(b); b->style()->polish(b); }
    });
    connect(m_btnModeInterval, &QPushButton::clicked, this, [this]() {
        m_trainingMode = IntervalMode;
        m_modeStack->setCurrentIndex(1);
        if (m_mapLayoutActive) {
            // Coming from Map: tiles already in chart col; just restore the 3→2 stretch
            m_middleLay->setStretch(0, 2);
            m_middleLay->setStretch(1, 3);
            m_mapLayoutActive      = false;
            m_intervalLayoutActive = true;
        } else if (!m_intervalLayoutActive) {
            // Coming from Free Ride: move tiles to chart col for full vertical height
            m_rootLayout->removeWidget(m_tileRowWidget);
            m_chartLayout->insertWidget(0, m_tileRowWidget);
            m_intervalLayoutActive = true;
        }
        m_btnModeFreeRide->setObjectName("modeInactive");
        m_btnModeInterval->setObjectName("modeActive");
        m_btnModeMap->setObjectName("modeInactive");
        for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
            { b->style()->unpolish(b); b->style()->polish(b); }
    });
    connect(m_btnModeMap, &QPushButton::clicked, this, [this]() {
        m_trainingMode = MapRideMode;
        m_modeStack->setCurrentIndex(2);
        if (!m_mapLayoutActive) {
            if (!m_intervalLayoutActive) {
                // Coming from Free Ride: move tiles from root to chart col
                m_rootLayout->removeWidget(m_tileRowWidget);
                m_chartLayout->insertWidget(0, m_tileRowWidget);
            }
            // Either way, apply the wide map stretch
            m_middleLay->setStretch(0, 3);
            m_middleLay->setStretch(1, 1);
            m_intervalLayoutActive = false;
            m_mapLayoutActive      = true;
        }
        m_btnModeFreeRide->setObjectName("modeInactive");
        m_btnModeInterval->setObjectName("modeInactive");
        m_btnModeMap->setObjectName("modeActive");
        for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
            { b->style()->unpolish(b); b->style()->polish(b); }
    });

    // Initialise Free Ride as active
    m_btnModeFreeRide->setObjectName("modeActive");
    m_btnModeInterval->setObjectName("modeInactive");
    m_btnModeMap->setObjectName("modeInactive");
    for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
        { b->style()->unpolish(b); b->style()->polish(b); }

    // ── Start/Stop signal ─────────────────────────────────────────────────
    connect(m_btnStartStop, &QPushButton::clicked, this, &DashboardWidget::startStopClicked);

    // ── Pause/Resume signal ─────────────────────────────────────────────
    connect(m_btnPause, &QPushButton::clicked, this, [this]() {
        if (!m_workoutActive && !m_workoutPaused) return;

        m_workoutPaused = !m_workoutPaused;

        if (m_workoutPaused) {
            // ── Pause ────────────────────────────────────────────
            m_workoutActive = false;
            m_chartTimer.stop();
            m_intervalTimer.stop();
            m_mapTimerLastMs = 0;  // reset so resume doesn't add accumulated pause time
            m_btnPause->setText("▶ RESUME");
            m_btnPause->setProperty("paused", true);
        } else {
            // ── Resume ───────────────────────────────────────────
            m_workoutActive = true;
            m_chartTimer.start(static_cast<int>(CHART_TICK_S * 1000));
            if (m_trainingMode == IntervalMode) {
                m_intervalTimer.start(1000);
                // Re-emit the current step's trainer target after resume
                if (m_currentStepIdx >= 0
                    && m_selectedProgramIdx >= 0
                    && m_selectedProgramIdx < m_programs.size()) {
                    const auto &step = m_programs[m_selectedProgramIdx].steps[m_currentStepIdx];
                    if (step.isErg) emit ergTargetChanged(step.ergWatts);
                    else            emit gradeChanged(step.gradePct);
                }
            }
            m_btnPause->setText("⏸ PAUSE");
            m_btnPause->setProperty("paused", false);
        }
        m_btnPause->style()->unpolish(m_btnPause);
        m_btnPause->style()->polish(m_btnPause);
    });
}

void DashboardWidget::buildFreeRideTab(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(6);

    // Row 1: ERG / GRADE mode toggles
    auto *modeRow = new QWidget(parent);
    auto *modeLay = new QHBoxLayout(modeRow);
    modeLay->setContentsMargins(0, 0, 0, 0);
    modeLay->setSpacing(6);
    m_btnGradeMode = new QPushButton("GRADE", modeRow);
    m_btnErgMode   = new QPushButton("ERG",   modeRow);
    modeLay->addWidget(m_btnGradeMode, 1);
    modeLay->addWidget(m_btnErgMode, 1);
    layout->addWidget(modeRow);

    // Row 2: unit label
    m_controlUnitLabel = new QLabel("Grade", parent);
    m_controlUnitLabel->setStyleSheet(
        "color: #6c7086; font-size: 10px; font-weight: 600; letter-spacing: 1px;"
        " background: transparent; border: none;");
    layout->addWidget(m_controlUnitLabel);

    // Row 3: decrement | value | increment
    auto *ctrlRow = new QWidget(parent);
    auto *ctrlLay = new QHBoxLayout(ctrlRow);
    ctrlLay->setContentsMargins(0, 0, 0, 0);
    ctrlLay->setSpacing(6);
    m_btnDecrement = new QPushButton("−", ctrlRow);
    m_btnDecrement->setObjectName("stepBtn");
    m_controlValueLabel = new QLabel("0.0 %", ctrlRow);
    m_controlValueLabel->setStyleSheet(
        "color: #89b4fa; font-size: 18px; font-weight: 800;"
        " background: transparent; border: none;");
    m_controlValueLabel->setAlignment(Qt::AlignCenter);
    m_btnIncrement = new QPushButton("+", ctrlRow);
    m_btnIncrement->setObjectName("stepBtn");
    ctrlLay->addWidget(m_btnDecrement);
    ctrlLay->addWidget(m_controlValueLabel, 1);
    ctrlLay->addWidget(m_btnIncrement);
    layout->addWidget(ctrlRow);
    layout->addStretch();

    // Connections
    connect(m_btnErgMode, &QPushButton::clicked, this, [this]() {
        m_trainerMode = ErgMode;
        emit ergTargetChanged(m_ergTargetWatts);
        updateControlDisplay();
    });
    connect(m_btnGradeMode, &QPushButton::clicked, this, [this]() {
        m_trainerMode = GradeMode;
        emit gradeChanged(m_gradeTargetPct);
        updateControlDisplay();
    });
    connect(m_btnDecrement, &QPushButton::clicked, this, [this]() {
        if (m_trainerMode == ErgMode) {
            m_ergTargetWatts = std::max(0, m_ergTargetWatts - 10);
            emit ergTargetChanged(m_ergTargetWatts);
        } else {
            m_gradeTargetPct = std::max(-20.0, m_gradeTargetPct - 1.0);
            emit gradeChanged(m_gradeTargetPct);
        }
        updateControlDisplay();
    });
    connect(m_btnIncrement, &QPushButton::clicked, this, [this]() {
        if (m_trainerMode == ErgMode) {
            m_ergTargetWatts = std::min(1500, m_ergTargetWatts + 10);
            emit ergTargetChanged(m_ergTargetWatts);
        } else {
            m_gradeTargetPct = std::min(20.0, m_gradeTargetPct + 1.0);
            emit gradeChanged(m_gradeTargetPct);
        }
        updateControlDisplay();
    });

    updateControlDisplay();
}

void DashboardWidget::buildIntervalTab(QWidget *parent)
{
    // Programs are loaded from disk by the constructor via refreshProgramList().

    // ── Layout ────────────────────────────────────────────────────────────
    auto *lay = new QVBoxLayout(parent);
    lay->setContentsMargins(0, 4, 0, 4);
    lay->setSpacing(4);

    // ── Programs list header + action buttons ─────────────────────────────
    auto *listHeaderRow = new QHBoxLayout();

    // Toggle button: expands/collapses the program list
    m_programToggleBtn = new QPushButton("\xE2\x96\xBE  Programs", parent);
    m_programToggleBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none;"
        "  color: #89b4fa; font-size: 15px; font-weight: 700;"
        "  text-align: left; padding: 0; }"
        "QPushButton:hover { color: #cdd6f4; }");
    m_programToggleBtn->setCursor(Qt::PointingHandCursor);
    listHeaderRow->addWidget(m_programToggleBtn, 1);

    const QString actBtnStyle =
        "QPushButton { background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
        "  border-radius: 4px; font-size: 14px; font-weight: 700;"
        "  padding: 2px 8px; min-height: 26px; max-height: 26px; }"
        "QPushButton:hover { background: #45475a; }";

    m_btnNewProgram    = new QPushButton("+", parent);
    m_btnEditProgram   = new QPushButton("\xe2\x9c\x8e", parent);  // ✎
    m_btnDeleteProgram = new QPushButton("\xe2\x9c\x95", parent);  // ✕
    for (auto *b : { m_btnNewProgram, m_btnEditProgram, m_btnDeleteProgram }) {
        b->setStyleSheet(actBtnStyle);
        b->setFixedWidth(28);
    }
    m_btnNewProgram->setToolTip("New program");
    m_btnEditProgram->setToolTip("Edit selected program");
    m_btnDeleteProgram->setToolTip("Delete selected program");

    listHeaderRow->addWidget(m_btnNewProgram);
    listHeaderRow->addWidget(m_btnEditProgram);
    listHeaderRow->addWidget(m_btnDeleteProgram);
    lay->addLayout(listHeaderRow);

    // ── Program list ──────────────────────────────────────────────────────
    m_programListWidget = new QListWidget(parent);
    m_programListWidget->setMinimumHeight(100);
    m_programListWidget->setMaximumHeight(200);
    m_programListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_programListWidget->setStyleSheet(
        "QListWidget { background: #181825; border: 1px solid #45475a;"
        "  border-radius: 6px; color: #cdd6f4; font-size: 15px; padding: 2px; }"
        "QListWidget::item { padding: 5px 10px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #313244; color: #89b4fa; }"
        "QListWidget::item:hover:!selected { background: #252535; }");
    lay->addWidget(m_programListWidget);

    // Wire the toggle button
    connect(m_programToggleBtn, &QPushButton::clicked, this, [this]() {
        m_programListExpanded = !m_programListExpanded;
        m_programListWidget->setVisible(m_programListExpanded);
        m_programToggleBtn->setText(
            m_programListExpanded ? "\xE2\x96\xBE  Programs" : "\xE2\x96\xB8  Programs");
    });

    // ── Step preview (read-only, shown when a program is selected) ────────
    m_intervalPreview = new QTextEdit(parent);
    m_intervalPreview->setReadOnly(true);
    m_intervalPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_intervalPreview->setStyleSheet(
        "QTextEdit { background: #181825; border: 1px solid #313244;"
        "  border-radius: 6px; color: #a6adc8; font-size: 15px; padding: 8px; }"
        "QScrollBar:vertical { background: #181825; width: 6px; }"
        "QScrollBar::handle:vertical { background: #45475a; border-radius: 3px; }");
    lay->addWidget(m_intervalPreview, 1);  // stretch=1: fills all remaining vertical space

    // ── Live run labels ───────────────────────────────────────────────────
    m_intervalProgramLabel = new QLabel("", parent);
    m_intervalProgramLabel->setStyleSheet(
        "color: #cdd6f4; font-size: 17px; font-weight: 700;"
        " background: transparent; border: none;");
    m_intervalProgramLabel->setWordWrap(true);
    lay->addWidget(m_intervalProgramLabel);

    m_intervalStepLabel = new QLabel(u8"\u2013", parent);
    m_intervalStepLabel->setStyleSheet(
        "color: #89b4fa; font-size: 20px; font-weight: 800;"
        " background: transparent; border: none;");
    m_intervalStepLabel->setWordWrap(true);
    lay->addWidget(m_intervalStepLabel);

    m_intervalTimeLabel = new QLabel("Select a program, then press START", parent);
    m_intervalTimeLabel->setStyleSheet(
        "color: #a6adc8; font-size: 17px; background: transparent; border: none;");
    lay->addWidget(m_intervalTimeLabel);

    m_intervalTargetLabel = new QLabel("", parent);
    m_intervalTargetLabel->setStyleSheet(
        "color: #f9e2af; font-size: 19px; font-weight: 600;"
        " background: transparent; border: none;");
    lay->addWidget(m_intervalTargetLabel);

    m_intervalStepBar = new QProgressBar(parent);
    m_intervalStepBar->setRange(0, 100);
    m_intervalStepBar->setValue(0);
    m_intervalStepBar->setFixedHeight(10);
    m_intervalStepBar->setTextVisible(false);
    m_intervalStepBar->setStyleSheet(
        "QProgressBar { background: #313244; border-radius: 5px; border: none; }"
        "QProgressBar::chunk { background: #89b4fa; border-radius: 5px; }");
    lay->addWidget(m_intervalStepBar);

    // ── Connect program selection: show preview ───────────────────────────
    connect(m_programListWidget, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0 && row < m_programs.size()) {
            m_selectedProgramIdx = row;
            m_currentStepIdx = -1;
            m_intervalStepLabel->setText(u8"\u2013");
            m_intervalTimeLabel->setText("Ready \xe2\x80\x93 press START to begin");
            m_intervalTargetLabel->setText("");
            m_intervalStepBar->setValue(0);
            showProgramPreview(row);
            // Collapse the list now that a program is selected
            if (m_programListExpanded && m_programToggleBtn) {
                m_programListExpanded = false;
                m_programListWidget->setVisible(false);
                m_programToggleBtn->setText("\xE2\x96\xB8  Programs");
            }
        }
    });

    // ── New / Edit / Delete ───────────────────────────────────────────────
    connect(m_btnNewProgram, &QPushButton::clicked, this, [this]() {
        WorkoutEditorDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return;
        TrainingProgram prog = dlg.program();
        TrainingLibrary::save(prog);
        refreshProgramList();
        // Select the newly created program
        for (int i = 0; i < m_programs.size(); ++i) {
            if (m_programs[i].name == prog.name) {
                m_programListWidget->setCurrentRow(i);
                break;
            }
        }
    });

    connect(m_btnEditProgram, &QPushButton::clicked, this, [this]() {
        const int row = m_programListWidget->currentRow();
        if (row < 0 || row >= m_programs.size()) return;
        const QString oldName = m_programs[row].name;
        WorkoutEditorDialog dlg(m_programs[row], this);
        if (dlg.exec() != QDialog::Accepted) return;
        TrainingProgram prog = dlg.program();
        // If the name changed, remove the old file
        if (prog.name != oldName)
            TrainingLibrary::remove(oldName);
        TrainingLibrary::save(prog);
        refreshProgramList();
        for (int i = 0; i < m_programs.size(); ++i) {
            if (m_programs[i].name == prog.name) {
                m_programListWidget->setCurrentRow(i);
                break;
            }
        }
    });

    connect(m_btnDeleteProgram, &QPushButton::clicked, this, [this]() {
        const int row = m_programListWidget->currentRow();
        if (row < 0 || row >= m_programs.size()) return;
        const QString name = m_programs[row].name;
        if (QMessageBox::question(this, "Delete program",
                QString("Delete \"%1\"?").arg(name),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) return;
        TrainingLibrary::remove(name);
        refreshProgramList();
    });
}

void DashboardWidget::refreshProgramList()
{
    m_programs = TrainingLibrary::loadAll();

    // Block signals while repopulating to avoid recursion
    m_programListWidget->blockSignals(true);
    m_programListWidget->clear();
    for (const auto &p : m_programs)
        m_programListWidget->addItem(p.name);
    m_programListWidget->blockSignals(false);

    // Clamp selection
    if (m_programs.isEmpty()) {
        m_selectedProgramIdx = -1;
        m_intervalPreview->clear();
        m_intervalProgramLabel->setText("");
    } else {
        m_selectedProgramIdx = qBound(0, m_selectedProgramIdx, m_programs.size() - 1);
        m_programListWidget->setCurrentRow(m_selectedProgramIdx);
        showProgramPreview(m_selectedProgramIdx);
        m_intervalProgramLabel->setText(m_programs[m_selectedProgramIdx].name);
    }
}

// Returns a Catppuccin-Mocha themed hex colour for a wattage relative to FTP
static QString zoneColorForWatts(int watts, int ftp)
{
    if (ftp <= 0) return "#89b4fa";
    const double pct = watts * 100.0 / ftp;
    if (pct < 55)  return "#6c7086";  // Z1 recovery   – subtext0 grey
    if (pct < 75)  return "#89b4fa";  // Z2 endurance  – blue
    if (pct < 90)  return "#a6e3a1";  // Z3 tempo      – green
    if (pct < 105) return "#f9e2af";  // Z4 threshold  – yellow
    if (pct < 120) return "#fab387";  // Z5 VO2max     – peach/orange
    return "#f38ba8";                  // Z6 anaerobic  – red/pink
}

void DashboardWidget::showProgramPreview(int idx)
{
    if (idx < 0 || idx >= m_programs.size()) {
        m_intervalPreview->clear();
        return;
    }
    const TrainingProgram &prog = m_programs[idx];

    // Build a compact HTML with a TrainerRoad-style workout profile and step table
    QString html;

    // ── Workout profile: colored bars proportional to duration, height = intensity ──
    {
        int totalSec = 0;
        double maxVal = 0.0;
        for (const auto &s : prog.steps) {
            totalSec += s.durationSec;
            double v = s.isErg ? static_cast<double>(s.ergWatts) : (std::abs(s.gradePct) * 30.0 + 50.0);
            if (v > maxVal) maxVal = v;
        }
        if (maxVal < 50.0) maxVal = 50.0;
        if (totalSec <= 0) totalSec = 1;

        html += "<div style='display:flex; align-items:flex-end; height:70px;"
                " gap:2px; margin-bottom:8px; padding:4px 0;'>";
        for (const auto &s : prog.steps) {
            const double widthPct = s.durationSec * 100.0 / totalSec;
            double v = s.isErg ? static_cast<double>(s.ergWatts)
                               : (std::abs(s.gradePct) * 30.0 + 50.0);
            const double heightPct = v / maxVal * 100.0;
            const int z = s.isErg ? powerZone(s.ergWatts, m_ftpWatts) : 3;
            const QString col = zoneColor(z).name();
            html += QString(
                "<div style='flex: %1 0 0; height:%2%;"
                " background:%3; border-radius:3px 3px 0 0;"
                " min-width:4px; opacity:0.85;'"
                " title='%4: %5'></div>")
                .arg(widthPct, 0, 'f', 2)
                .arg(heightPct, 0, 'f', 0)
                .arg(col)
                .arg(s.name.toHtmlEscaped())
                .arg(s.isErg ? QString("%1W").arg(s.ergWatts)
                             : QString("%1%").arg(s.gradePct, 0, 'f', 1));
        }
        html += "</div>";

        // FTP reference line label
        if (prog.isErg && m_ftpWatts > 0) {
            html += QString("<div style='font-size:13px; color:#6c7086; margin-bottom:4px;'>"
                            "FTP: %1 W</div>").arg(m_ftpWatts);
        }
    }

    // ── Step table ──
    html += "<table style='font-size:15px; color:#cdd6f4; width:100%; border-spacing:0;'>";
    int totalSec = 0;
    for (int si = 0; si < prog.steps.size(); ++si) {
        const auto &s = prog.steps[si];
        totalSec += s.durationSec;
        const int m = s.durationSec / 60;
        const int sc = s.durationSec % 60;
        const QString dur = sc ? QString("%1:%2").arg(m).arg(sc, 2, 10, QChar('0'))
                                : QString("%1 min").arg(m);
        const int z = s.isErg ? powerZone(s.ergWatts, m_ftpWatts) : 3;
        const QString zCol = zoneColor(z).name();
        const QString val = s.isErg
            ? QString("<span style='color:%1;'>%2 W</span>")
                  .arg(zCol).arg(s.ergWatts)
            : QString("<span style='color:#f9e2af;'>%1%</span>").arg(s.gradePct, 0, 'f', 1);
        const QString zoneCol = s.isErg
            ? QString("<td style='padding:6px 0 6px 16px; color:%1;'>Z%2</td>")
                  .arg(zCol).arg(z)
            : QString("<td></td>");
        html += QString("<tr><td style='padding:6px 8px 6px 0;'>%1</td>"
                        "<td style='color:#a6adc8; padding:6px 14px;'>%2</td>"
                        "<td style='padding:6px 0;'>%3</td>%4</tr>")
                        .arg(s.name.toHtmlEscaped()).arg(dur).arg(val).arg(zoneCol);
    }
    const int th = totalSec / 3600, tm = (totalSec % 3600) / 60;
    const QString totalStr = th > 0
        ? QString("%1h %2min").arg(th).arg(tm)
        : QString("%1 min").arg(tm);
    html += QString("<tr><td colspan='4' style='color:#6c7086; padding-top:8px; font-size:14px;'>Total: %1</td></tr>")
                    .arg(totalStr);
    html += "</table>";

    m_intervalPreview->setHtml(html);
    m_intervalProgramLabel->setText(prog.name);
}

// ── Mini GPS map widget ───────────────────────────────────────────────────────
// Draws OSM street tile imagery as background, then overlays the full GPX track
// with the ridden portion highlighted and a dot at the current position.
// Tiles are fetched asynchronously and cached in memory.

// OSM tile math ───────────────────────────────────────────────────────────────
static int tileX(double lon, int zoom)
{
    return static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << zoom)));
}
static int tileY(double lat, int zoom)
{
    const double rad = qDegreesToRadians(lat);
    return static_cast<int>(std::floor(
        (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / M_PI) / 2.0 * (1 << zoom)));
}
// Floating-point tile coordinates — used for sub-pixel smooth rendering
static double tileXf(double lon, int zoom)
{
    return (lon + 180.0) / 360.0 * static_cast<double>(1 << zoom);
}
static double tileYf(double lat, int zoom)
{
    const double rad = qDegreesToRadians(lat);
    return (1.0 - std::log(std::tan(rad) + 1.0 / std::cos(rad)) / M_PI) / 2.0
           * static_cast<double>(1 << zoom);
}
// Top-left longitude of a tile
static double tileToLon(int x, int zoom)
{
    return x / static_cast<double>(1 << zoom) * 360.0 - 180.0;
}
// Top-left latitude of a tile
static double tileToLat(int y, int zoom)
{
    const double n = M_PI - 2.0 * M_PI * y / static_cast<double>(1 << zoom);
    return qRadiansToDegrees(std::atan(0.5 * (std::exp(n) - std::exp(-n))));
}

class GpxMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GpxMapWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_nam(new QNetworkAccessManager(this))
    {
        setMinimumHeight(100);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet("background: #0d0d14; border-radius: 8px;");
        setCursor(Qt::OpenHandCursor);

        const QString zoomBtnStyle =
            "QPushButton {"
            "  background: rgba(30,30,46,210); color: #cdd6f4;"
            "  border: 1px solid #585b70; border-radius: 5px;"
            "  font-size: 16px; font-weight: 700;"
            "  min-width: 28px; max-width: 28px;"
            "  min-height: 28px; max-height: 28px; padding: 0px;"
            "}"
            "QPushButton:hover { background: rgba(69,71,90,230); }"
            "QPushButton:pressed { background: rgba(88,91,112,255); }";
        const QString rotateBtnStyle =
            "QPushButton {"
            "  background: rgba(30,30,46,210); color: #cdd6f4;"
            "  border: 1px solid #585b70; border-radius: 6px;"
            "  font-size: 22px; font-weight: 700;"
            "  min-width: 40px; max-width: 40px;"
            "  min-height: 40px; max-height: 40px; padding: 0px;"
            "}"
            "QPushButton:hover { background: rgba(69,71,90,230); }"
            "QPushButton:pressed { background: rgba(88,91,112,255); }";

        m_btnZoomIn  = new QPushButton("+", this);
        m_btnZoomOut = new QPushButton("\xe2\x88\x92", this);  // − (minus sign)
        m_btnRotate  = new QPushButton("\xf0\x9f\xa7\xad", this); // 🧭 compass
        m_btnZoomIn ->setStyleSheet(zoomBtnStyle);
        m_btnZoomOut->setStyleSheet(zoomBtnStyle);
        // Compass heading-up is on by default — show button as active
        m_btnRotate ->setStyleSheet(
            "QPushButton {"
            "  background: rgba(137,180,250,230); color: #1e1e2e;"
            "  border: 1px solid #89b4fa; border-radius: 6px;"
            "  font-size: 22px; font-weight: 700;"
            "  min-width: 40px; max-width: 40px;"
            "  min-height: 40px; max-height: 40px; padding: 0px;"
            "}"
            "QPushButton:hover { background: rgba(180,208,245,240); }"
            "QPushButton:pressed { background: rgba(205,214,244,255); }");
        m_btnRotate ->setToolTip("Heading-up rotation (bike always points up)");

        connect(m_btnZoomIn, &QPushButton::clicked, this, [this]() {
            m_zoom = qMin(m_zoom + 1, 19);
            m_manualZoom = true;
            m_tileCache.clear();
            m_pendingTiles.clear();
            centerTilesOnBike();
            fetchTiles();
            update();
        });
        connect(m_btnZoomOut, &QPushButton::clicked, this, [this]() {
            m_zoom = qMax(m_zoom - 1, 1);
            m_manualZoom = true;
            m_tileCache.clear();
            m_pendingTiles.clear();
            centerTilesOnBike();
            fetchTiles();
            update();
        });
        connect(m_btnRotate, &QPushButton::clicked, this, [this]() {
            m_headingUp = !m_headingUp;
            // Highlight button when active
            const QString activeStyle =
                "QPushButton {"
                "  background: rgba(137,180,250,230); color: #1e1e2e;"
                "  border: 1px solid #89b4fa; border-radius: 6px;"
                "  font-size: 22px; font-weight: 700;"
                "  min-width: 40px; max-width: 40px;"
                "  min-height: 40px; max-height: 40px; padding: 0px;"
                "}"
                "QPushButton:hover { background: rgba(180,208,245,240); }"
                "QPushButton:pressed { background: rgba(205,214,244,255); }";
            const QString normalStyle =
                "QPushButton {"
                "  background: rgba(30,30,46,210); color: #cdd6f4;"
                "  border: 1px solid #585b70; border-radius: 6px;"
                "  font-size: 22px; font-weight: 700;"
                "  min-width: 40px; max-width: 40px;"
                "  min-height: 40px; max-height: 40px; padding: 0px;"
                "}"
                "QPushButton:hover { background: rgba(69,71,90,230); }"
                "QPushButton:pressed { background: rgba(88,91,112,255); }";
            m_btnRotate->setStyleSheet(m_headingUp ? activeStyle : normalStyle);
            m_btnRotate->setToolTip(m_headingUp
                ? "Heading-up rotation (bike always points up)"
                : "North-up (fixed orientation)");
            centerTilesOnBike();
            fetchTiles();
            update();
        });

        // ── Satellite / street toggle button ──────────────────────────────────
        m_btnLayers = new QPushButton(QString::fromUtf8("\xf0\x9f\x97\xba"), this); // 🗺
        m_btnLayers->setStyleSheet(
            "QPushButton {"
            "  background: rgba(137,180,250,230); color: #1e1e2e;"
            "  border: 1px solid #89b4fa; border-radius: 6px;"
            "  font-size: 22px; font-weight: 700;"
            "  min-width: 40px; max-width: 40px;"
            "  min-height: 40px; max-height: 40px; padding: 0px;"
            "}"
            "QPushButton:hover { background: rgba(180,208,245,240); }"
            "QPushButton:pressed { background: rgba(205,214,244,255); }");
        m_btnLayers->setToolTip("Switch to street map");
        connect(m_btnLayers, &QPushButton::clicked, this, [this]() {
            m_satellite = !m_satellite;
            const QString activeStyle =
                "QPushButton {"
                "  background: rgba(137,180,250,230); color: #1e1e2e;"
                "  border: 1px solid #89b4fa; border-radius: 6px;"
                "  font-size: 22px; font-weight: 700;"
                "  min-width: 40px; max-width: 40px;"
                "  min-height: 40px; max-height: 40px; padding: 0px;"
                "}"
                "QPushButton:hover { background: rgba(180,208,245,240); }"
                "QPushButton:pressed { background: rgba(205,214,244,255); }";
            const QString normalStyle =
                "QPushButton {"
                "  background: rgba(30,30,46,210); color: #cdd6f4;"
                "  border: 1px solid #585b70; border-radius: 6px;"
                "  font-size: 22px; font-weight: 700;"
                "  min-width: 40px; max-width: 40px;"
                "  min-height: 40px; max-height: 40px; padding: 0px;"
                "}"
                "QPushButton:hover { background: rgba(69,71,90,230); }"
                "QPushButton:pressed { background: rgba(88,91,112,255); }";
            m_btnLayers->setStyleSheet(m_satellite ? activeStyle : normalStyle);
            m_btnLayers->setText(m_satellite
                ? QString::fromUtf8("\xf0\x9f\x97\xba")   // 🗺
                : QString::fromUtf8("\xf0\x9f\x9b\xb0"));  // 🛰
            m_btnLayers->setToolTip(m_satellite
                ? "Switch to street map"
                : "Switch to satellite view");
            m_tileCache.clear();
            m_pendingTiles.clear();
            fetchTiles();
            update();
        });
    }

    void setTrack(const QVector<DashboardWidget::GpxPoint> &track)
    {
        m_track = track;
        m_tileCache.clear();
        m_pendingTiles.clear();
        m_manualZoom = false;  // reset to auto-fit on new route load

        if (track.isEmpty()) {
            m_minLat = m_maxLat = m_minLon = m_maxLon = 0.0;
            update();
            return;
        }

        m_minLat = m_maxLat = track.first().lat;
        m_minLon = m_maxLon = track.first().lon;
        for (const auto &pt : track) {
            if (pt.lat < m_minLat) m_minLat = pt.lat;
            if (pt.lat > m_maxLat) m_maxLat = pt.lat;
            if (pt.lon < m_minLon) m_minLon = pt.lon;
            if (pt.lon > m_maxLon) m_maxLon = pt.lon;
        }
        m_distTravelled = 0.0;
        m_smoothedHeadingDeg = 0.0;  // reset compass on new route
        m_panOffsetTX = 0.0;
        m_panOffsetTY = 0.0;
        chooseZoom();
        fetchTiles();
        update();
    }

    void setProgress(double distTravelledKm)
    {
        const bool justStarted = (m_distTravelled == 0.0 && distTravelledKm > 0.0);
        m_distTravelled = distTravelledKm;

        // ── Smooth heading for compass mode (exponential moving average at 20 Hz) ──
        // The look-ahead blended bearing already anticipates turns, so we can
        // use a higher α for responsiveness while keeping rotation silky-smooth.
        // α ≈ 0.18 → time-constant ≈ 0.25 s at 20 Hz.
        if (m_headingUp && !m_track.isEmpty() && m_distTravelled > 0.0) {
            const double rawHeading = currentHeadingDeg();
            if (justStarted) {
                m_smoothedHeadingDeg = rawHeading;  // snap on first frame
            } else {
                double diff = rawHeading - m_smoothedHeadingDeg;
                // Wrap to shortest angular path
                while (diff >  180.0) diff -= 360.0;
                while (diff < -180.0) diff += 360.0;
                m_smoothedHeadingDeg =
                    std::fmod(m_smoothedHeadingDeg + 0.18 * diff + 360.0, 360.0);
            }
        }

        if (!m_track.isEmpty() && m_distTravelled > 0.0) {
            if (justStarted && !m_manualZoom) {
                // Ride just started — switch to a close riding zoom and re-center
                m_zoom = 15;
                m_tileCache.clear();
                m_pendingTiles.clear();
                centerTilesOnBike();
                fetchTiles();
            } else {
                // Always keep bike centred; only re-fetch when we cross a tile boundary
                const int prevTxMin = m_txMin, prevTyMin = m_tyMin;
                centerTilesOnBike();
                if (m_txMin != prevTxMin || m_tyMin != prevTyMin)
                    fetchTiles();
            }
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

        if (m_track.size() < 2) {
            p.setPen(QColor("#45475a"));
            p.setFont(QFont("Sans", 9));
            p.drawText(rect(), Qt::AlignCenter, "No route loaded");
            return;
        }

        // ── View centre + heading rotation ────────────────────────────────────
        double centerLat, centerLon;
        getCurrentViewCenter(centerLat, centerLon);
        const double centerTX = tileXf(centerLon, m_zoom) + m_panOffsetTX;
        const double centerTY = tileYf(centerLat, m_zoom) + m_panOffsetTY;
        const double cx = width()  / 2.0;
        const double cy = height() / 2.0;
        constexpr double TILE_PX = 256.0;

        // When heading-up is on, rotate the painter so the current travel direction
        // points straight up.  Heading is clockwise from North; to bring it to the
        // top of the screen we rotate the map COUNTER-CLOCKWISE by that amount,
        // i.e. p.rotate(-headingDeg).  The smoothed value avoids abrupt jumps at
        // segment boundaries.  Overlays are drawn after resetTransform().
        const double headingDeg = m_headingUp ? m_smoothedHeadingDeg : 0.0;
        if (m_headingUp) {
            p.translate(cx, cy);
            p.rotate(-headingDeg);  // CCW rotation → heading points up
            p.translate(-cx, -cy);
        }

        // Projection lambda — works in the (possibly rotated) painter space
        auto project = [&](double lat, double lon) -> QPointF {
            return QPointF(cx + (tileXf(lon, m_zoom) - centerTX) * TILE_PX,
                           cy + (tileYf(lat, m_zoom) - centerTY) * TILE_PX);
        };

        // ── Draw satellite tiles ──────────────────────────────────────────────
        for (int tx = m_txMin; tx <= m_txMax; ++tx) {
            for (int ty = m_tyMin; ty <= m_tyMax; ++ty) {
                const QString key = tileKey(tx, ty, m_zoom);
                if (m_tileCache.contains(key)) {
                    const double px = cx + (tx - centerTX) * TILE_PX;
                    const double py = cy + (ty - centerTY) * TILE_PX;
                    p.drawPixmap(QRectF(px, py, TILE_PX, TILE_PX),
                                 m_tileCache[key], QRectF());
                }
            }
        }

        // ── Project all GPX waypoints ─────────────────────────────────────────
        int curIdx = static_cast<int>(m_track.size()) - 1;
        for (int i = 0; i < static_cast<int>(m_track.size()) - 1; ++i) {
            if (m_track[i + 1].distFromStart >= m_distTravelled) { curIdx = i; break; }
        }

        QVector<QPointF> pts;
        pts.reserve(m_track.size());
        for (const auto &pt : m_track)
            pts.append(project(pt.lat, pt.lon));

        // ── Track lines (transparent yellow, zoom-proportional width) ───
        {
            const double pw = qMax(3.0, 0.4 * m_zoom);
            QPen pen(QColor(255, 230, 50, 120), pw, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            for (int i = 0; i < pts.size() - 1; ++i)
                p.drawLine(pts[i], pts[i + 1]);
        }

        // ── Sub-segment interpolated bike position ────────────────────────────
        QPointF bikePos;
        if (curIdx + 1 < pts.size()) {
            const auto  &gp1    = m_track[curIdx];
            const auto  &gp2    = m_track[curIdx + 1];
            const double segLen = gp2.distFromStart - gp1.distFromStart;
            const double tc     = (segLen > 1e-6)
                ? qBound(0.0, (m_distTravelled - gp1.distFromStart) / segLen, 1.0)
                : 0.0;
            bikePos = QPointF(cx + (tileXf(gp1.lon, m_zoom) * (1.0 - tc)
                                    + tileXf(gp2.lon, m_zoom) * tc - centerTX) * TILE_PX,
                              cy + (tileYf(gp1.lat, m_zoom) * (1.0 - tc)
                                    + tileYf(gp2.lat, m_zoom) * tc - centerTY) * TILE_PX);
        } else {
            bikePos = pts.last();
        }

        // ── Ridden portion (blue) ─────────────────────────────────────────────
        if (curIdx > 0 || m_distTravelled > 0.0) {
            QPen pen(QColor("#89b4fa"), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            for (int i = 0; i < curIdx; ++i)
                p.drawLine(pts[i], pts[i + 1]);
            if (curIdx + 1 < pts.size())
                p.drawLine(pts[curIdx], bikePos);
        }

        // ── Start dot ──────────────────────────────────────────────────────
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#a6e3a1"));
        p.drawEllipse(pts.first(), 5.0, 5.0);

        // ── Checkered flag at finish ──────────────────────────────────────────
        {
            const QPointF fp = pts.last();
            const double flagSz = 18.0;  // total flag size
            const int cells = 4;         // 4×4 checkerboard
            const double cellSz = flagSz / cells;
            const double x0 = fp.x() - flagSz / 2.0;
            const double y0 = fp.y() - flagSz / 2.0;
            // White background with border
            p.setPen(QPen(QColor(180, 180, 180, 200), 1.0));
            p.setBrush(QColor(255, 255, 255, 220));
            p.drawRect(QRectF(x0, y0, flagSz, flagSz));
            // Black cells of the checkerboard
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 220));
            for (int r = 0; r < cells; ++r)
                for (int c = 0; c < cells; ++c)
                    if ((r + c) % 2 == 0)
                        p.drawRect(QRectF(x0 + c * cellSz, y0 + r * cellSz, cellSz, cellSz));
        }

        // ── Remove rotation before drawing overlays ───────────────────────────
        // The bike marker is always at (cx,cy); we reset the transform so it and
        // any text overlays are drawn upright regardless of rotation mode.
        p.resetTransform();

        // ── Bike position marker ──────────────────────────────────────────────
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 40, 40, 100));    // large semi-transparent red glow
        p.drawEllipse(bikePos, 16.0, 16.0);
        p.setBrush(QColor(255, 30, 30, 200));    // solid red core
        p.drawEllipse(bikePos, 7.0, 7.0);

        // ── North arrow (always visible) ──────────────────────────────────────
        if (!m_track.isEmpty()) {
            const int ax = 28, ay = 40, ar = 20;
            // Rotate arrow so N points in the direction of true north on screen
            // i.e. the opposite of the heading rotation applied to the map
            p.save();
            p.translate(ax, ay);
            p.rotate(headingDeg);   // map was rotated -headingDeg; undo → N points up
            // "N" arrow: red top half, white bottom half
            QPainterPath north, south;
            north.moveTo(0, -ar);
            north.lineTo(ar * 0.4, 0);
            north.lineTo(-ar * 0.4, 0);
            north.closeSubpath();
            south.moveTo(0, ar);
            south.lineTo(ar * 0.4, 0);
            south.lineTo(-ar * 0.4, 0);
            south.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#f38ba8"));
            p.drawPath(north);
            p.setBrush(QColor(200, 200, 200, 180));
            p.drawPath(south);
            // "N" label
            p.setPen(QColor("#cdd6f4"));
            QFont f("Sans", 8, QFont::Bold);
            p.setFont(f);
            p.drawText(QRectF(-ar, -ar - 14, ar * 2, 14), Qt::AlignHCenter, "N");
            p.restore();
        }

        // ── Attribution ───────────────────────────────────────────────────────
        p.setPen(QColor(255, 255, 255, 120));
        p.setFont(QFont("Sans", 5));
        p.drawText(rect().adjusted(0, 0, -4, -2),
                   Qt::AlignRight | Qt::AlignBottom,
                   m_satellite
                   ? QString::fromUtf8("Powered by Esri | \xC2\xA9 Esri, Maxar, Earthstar Geographics")
                   : QString::fromUtf8("\xC2\xA9 OpenStreetMap contributors"));
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragLastPos = e->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_dragging) {
            const double dx = e->pos().x() - m_dragLastPos.x();
            const double dy = e->pos().y() - m_dragLastPos.y();
            m_dragLastPos = e->pos();
            const double h = m_headingUp ? qDegreesToRadians(m_smoothedHeadingDeg) : 0.0;
            m_panOffsetTX -= ( std::cos(h) * dx - std::sin(h) * dy) / 256.0;
            m_panOffsetTY -= ( std::sin(h) * dx + std::cos(h) * dy) / 256.0;
            centerTilesOnBike();
            fetchTiles();
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
    }

    void wheelEvent(QWheelEvent *e) override
    {
        const int delta = e->angleDelta().y();
        if (delta == 0) return;
        const int oldZoom = m_zoom;
        if (delta > 0)
            m_zoom = qMin(m_zoom + 1, 19);
        else
            m_zoom = qMax(m_zoom - 1, 1);
        if (m_zoom == oldZoom) return;
        m_manualZoom = true;
        // Zoom centred on cursor position
        const double scale = static_cast<double>(1 << m_zoom)
                           / static_cast<double>(1 << oldZoom);
        const double cx = width()  / 2.0;
        const double cy = height() / 2.0;
        const QPointF cur = e->position();
        // Widget-pixel offset from centre, rotated into tile space for heading-up
        double dx = cur.x() - cx;
        double dy = cur.y() - cy;
        if (m_headingUp) {
            const double h = qDegreesToRadians(m_smoothedHeadingDeg);
            const double rx =  std::cos(h) * dx - std::sin(h) * dy;
            const double ry =  std::sin(h) * dx + std::cos(h) * dy;
            dx = rx;
            dy = ry;
        }
        const double cursorTileOffX = dx / 256.0;
        const double cursorTileOffY = dy / 256.0;
        m_panOffsetTX = m_panOffsetTX * scale + cursorTileOffX * (scale - 1.0);
        m_panOffsetTY = m_panOffsetTY * scale + cursorTileOffY * (scale - 1.0);
        m_tileCache.clear();
        m_pendingTiles.clear();
        centerTilesOnBike();
        fetchTiles();
        update();
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        // Position buttons: stacked in the top-right corner
        const int margin  = 8;
        const int gap     = 4;
        const int bw = 28, bh = 28;   // zoom buttons
        const int rw = 40, rh = 40;   // rotate + layers buttons (larger)
        m_btnZoomIn ->move(width() - margin - bw, margin);
        m_btnZoomOut->move(width() - margin - bw, margin + bh + gap);
        m_btnLayers ->move(width() - margin - rw, margin + bh * 2 + gap * 2);
        m_btnRotate ->move(width() - margin - rw, margin + bh * 2 + gap * 2 + rh + gap);
        if (!m_track.isEmpty()) {
            chooseZoom();
            fetchTiles();
        }
    }

private:
    static QString tileKey(int x, int y, int z)
    {
        return QString("%1/%2/%3").arg(z).arg(x).arg(y);
    }

    // Returns the view centre in lat/lon.
    // — Not riding (dist=0): route midpoint, so the auto-fit overview is centred.
    // — Riding: sub-segment interpolated bike position, so the map scrolls
    //   continuously under the (always-centred) bike marker at 20 Hz.
    void getCurrentViewCenter(double &lat, double &lon) const
    {
        if (m_track.isEmpty() || m_distTravelled <= 0.0) {
            lat = (m_minLat + m_maxLat) * 0.5;
            lon = (m_minLon + m_maxLon) * 0.5;
            return;
        }
        int idx = static_cast<int>(m_track.size()) - 1;
        for (int i = 0; i < static_cast<int>(m_track.size()) - 1; ++i) {
            if (m_track[i + 1].distFromStart >= m_distTravelled) { idx = i; break; }
        }
        if (idx + 1 < static_cast<int>(m_track.size())) {
            const auto  &gp1    = m_track[idx];
            const auto  &gp2    = m_track[idx + 1];
            const double segLen = gp2.distFromStart - gp1.distFromStart;
            const double tc     = (segLen > 1e-6)
                ? qBound(0.0, (m_distTravelled - gp1.distFromStart) / segLen, 1.0)
                : 0.0;
            lat = gp1.lat + tc * (gp2.lat - gp1.lat);
            lon = gp1.lon + tc * (gp2.lon - gp1.lon);
        } else {
            lat = m_track.last().lat;
            lon = m_track.last().lon;
        }
    }

    // Bearing (degrees CW from north) between two track points.
    static double segmentBearing(double lat1d, double lon1d,
                                 double lat2d, double lon2d)
    {
        const double lat1 = qDegreesToRadians(lat1d);
        const double lat2 = qDegreesToRadians(lat2d);
        const double dLon = qDegreesToRadians(lon2d - lon1d);
        const double y = std::sin(dLon) * std::cos(lat2);
        const double x = std::cos(lat1) * std::sin(lat2)
                       - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
        return std::fmod(qRadiansToDegrees(std::atan2(y, x)) + 360.0, 360.0);
    }

    // Compute heading by blending bearings over the next ~120 m of the route.
    // This anticipates upcoming turns and produces a much smoother signal than
    // a single-segment bearing, reducing jerkiness at segment boundaries.
    double currentHeadingDeg() const
    {
        if (m_track.size() < 2 || m_distTravelled <= 0.0) return 0.0;

        const int N = static_cast<int>(m_track.size());
        int idx = 0;
        for (int i = 0; i < N - 1; ++i) {
            if (m_track[i + 1].distFromStart >= m_distTravelled) { idx = i; break; }
        }
        if (idx + 1 >= N) return 0.0;

        // Look-ahead distance in km — blend bearings over this window
        constexpr double kLookAheadKm = 0.12;  // 120 m

        // Accumulate weighted circular mean (sin/cos) of segment bearings
        double sumSin = 0.0, sumCos = 0.0, accumulated = 0.0;

        // Remaining distance in the current segment (weight for current bearing)
        const double segLen0 = m_track[idx + 1].distFromStart
                             - m_track[idx].distFromStart;
        const double frac    = (segLen0 > 1e-9)
            ? qBound(0.0, (m_distTravelled - m_track[idx].distFromStart) / segLen0, 1.0)
            : 0.0;
        const double remaining0 = segLen0 * (1.0 - frac);

        for (int s = idx; s < N - 1 && accumulated < kLookAheadKm; ++s) {
            const double segLen = m_track[s + 1].distFromStart
                                - m_track[s].distFromStart;
            if (segLen < 1e-9) continue;

            // How much of this segment falls in the look-ahead window
            double contrib = segLen;
            if (s == idx) contrib = remaining0;
            if (accumulated + contrib > kLookAheadKm)
                contrib = kLookAheadKm - accumulated;

            // Exponential decay weight: nearby segments matter most
            const double w = contrib * std::exp(-3.0 * accumulated / kLookAheadKm);

            const double brg = segmentBearing(
                m_track[s].lat, m_track[s].lon,
                m_track[s + 1].lat, m_track[s + 1].lon);
            const double brgRad = qDegreesToRadians(brg);
            sumSin += w * std::sin(brgRad);
            sumCos += w * std::cos(brgRad);

            accumulated += contrib;
        }

        if (sumSin == 0.0 && sumCos == 0.0) return 0.0;
        return std::fmod(qRadiansToDegrees(std::atan2(sumSin, sumCos)) + 360.0, 360.0);
    }

    void centerTilesOnBike()
    {
        if (m_track.isEmpty()) return;
        double bikeLat, bikeLon;
        getCurrentViewCenter(bikeLat, bikeLon);

        const int centerX = static_cast<int>(std::floor(
            tileXf(bikeLon, m_zoom) + m_panOffsetTX));
        const int centerY = static_cast<int>(std::floor(
            tileYf(bikeLat, m_zoom) + m_panOffsetTY));

        // When heading-up is active the viewport is rotated, so we need tiles
        // covering the full diagonal of the widget (×√2) to avoid blank corners.
        const double diagScale = m_headingUp ? 1.5 : 1.0;
        const int rX = qMax(2, static_cast<int>(std::ceil(width()  / 256.0 / 2.0 * diagScale)) + 1);
        const int rY = qMax(2, static_cast<int>(std::ceil(height() / 256.0 / 2.0 * diagScale)) + 1);

        m_txMin = centerX - rX;
        m_txMax = centerX + rX;
        m_tyMin = centerY - rY;
        m_tyMax = centerY + rY;

        const int maxTile = (1 << m_zoom) - 1;
        m_txMin = qMax(0, m_txMin);
        m_txMax = qMin(maxTile, m_txMax);
        m_tyMin = qMax(0, m_tyMin);
        m_tyMax = qMin(maxTile, m_tyMax);
    }

    void chooseZoom()
    {
        if (m_track.isEmpty()) return;
        if (m_manualZoom) {
            centerTilesOnBike();
            return;
        }
        // Auto-fit: choose zoom so the route span fits within the visible widget
        // (measured in 256×256-pixel tiles)
        const int maxFitX = qMax(1, static_cast<int>(width()  / 256));
        const int maxFitY = qMax(1, static_cast<int>(height() / 256));
        for (m_zoom = 16; m_zoom >= 1; --m_zoom) {
            const int nX = tileX(m_maxLon, m_zoom) - tileX(m_minLon, m_zoom) + 1;
            const int nY = tileY(m_minLat, m_zoom) - tileY(m_maxLat, m_zoom) + 1;
            if (nX <= maxFitX && nY <= maxFitY) break;
        }
        // Build tile range centred on route midpoint with viewport-sized radius
        centerTilesOnBike();
    }

    void fetchTiles()
    {
        const int maxTile = (1 << m_zoom) - 1;
        for (int tx = qMax(0, m_txMin - 1); tx <= qMin(maxTile, m_txMax + 1); ++tx) {
            for (int ty = qMax(0, m_tyMin - 1); ty <= qMin(maxTile, m_tyMax + 1); ++ty) {
                const QString key = tileKey(tx, ty, m_zoom);
                if (m_tileCache.contains(key) || m_pendingTiles.contains(key))
                    continue;
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
                    if (pix.loadFromData(reply->readAll()))
                        m_tileCache[key] = pix;
                    update();
                });
            }
        }
    }

    QPointF projectSimple(double lat, double lon, const QRectF &drawRect) const
    {
        const double midLat = (m_minLat + m_maxLat) * 0.5;
        const double cosLat = qCos(qDegreesToRadians(midLat));
        const double latSpan    = m_maxLat - m_minLat;
        const double lonSpanAdj = (m_maxLon - m_minLon) * cosLat;
        const double eps = 1e-10;
        double scale;
        if (latSpan < eps && lonSpanAdj < eps) return drawRect.center();
        else if (latSpan < eps)    scale = drawRect.width()  / lonSpanAdj;
        else if (lonSpanAdj < eps) scale = drawRect.height() / latSpan;
        else scale = qMin(drawRect.width() / lonSpanAdj, drawRect.height() / latSpan);
        const double midLon = (m_minLon + m_maxLon) * 0.5;
        return QPointF(drawRect.center().x() + (lon - midLon) * cosLat * scale,
                       drawRect.center().y() - (lat - midLat) * scale);
    }

    QNetworkAccessManager *m_nam;
    QMap<QString, QPixmap> m_tileCache;
    QSet<QString>          m_pendingTiles;
    QVector<DashboardWidget::GpxPoint> m_track;
    double m_distTravelled = 0.0;
    double m_minLat = 0.0, m_maxLat = 0.0;
    double m_minLon = 0.0, m_maxLon = 0.0;
    int    m_zoom       = 14;
    bool   m_manualZoom = false;
    bool   m_headingUp           = true;   // true = rotate so current heading points up
    double m_smoothedHeadingDeg  = 0.0;   // exponentially smoothed heading for compass
    int    m_txMin = 0, m_txMax = 0;
    int    m_tyMin = 0, m_tyMax = 0;
    QPushButton *m_btnZoomIn  = nullptr;
    QPushButton *m_btnZoomOut = nullptr;
    QPushButton *m_btnRotate  = nullptr;
    QPushButton *m_btnLayers  = nullptr;
    bool         m_satellite  = true;   // default to satellite view

    // Mouse drag-to-pan state
    bool    m_dragging     = false;
    QPoint  m_dragLastPos;
    double  m_panOffsetTX  = 0.0;   // pan offset in fractional tile units
    double  m_panOffsetTY  = 0.0;
};

// ── Elevation profile widget ──────────────────────────────────────────────────
// Fixed-height bar showing the full route elevation, ridden portion highlighted,
// and a vertical cursor at the current bike position.
class ElevationProfileWidget : public QWidget
{
public:
    explicit ElevationProfileWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(52);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setStyleSheet("background: #181825; border-radius: 6px;");
    }

    void setTrack(const QVector<DashboardWidget::GpxPoint> &track)
    {
        m_track = track;
        m_distTravelled = 0.0;
        if (track.isEmpty()) { update(); return; }
        m_minEle = m_maxEle = track.first().ele;
        for (const auto &pt : track) {
            if (pt.ele < m_minEle) m_minEle = pt.ele;
            if (pt.ele > m_maxEle) m_maxEle = pt.ele;
        }
        update();
    }

    void setProgress(double distTravelledKm)
    {
        m_distTravelled = distTravelledKm;
        update();
    }

    void setZoomWindow(double startKm, double endKm)
    {
        m_zoomStart = startKm;
        m_zoomEnd   = endKm;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.fillRect(rect(), QColor("#181825"));

        if (m_track.size() < 2) return;
        const double totalDist = m_track.last().distFromStart;
        if (totalDist < 1e-6) return;

        const int padX = 6, padY = 6;
        const QRectF r(padX, padY, width() - 2 * padX, height() - 2 * padY);

        // Elevation range with 10% head/foot room
        const double eleSpan  = (m_maxEle - m_minEle) < 1.0 ? 1.0 : (m_maxEle - m_minEle);
        const double elevPad  = eleSpan * 0.15;
        const double eleBot   = m_minEle - elevPad;
        const double eleRange = (m_maxEle + elevPad) - eleBot;

        auto xFor = [&](double dist) { return r.left() + dist / totalDist * r.width(); };
        auto yFor = [&](double ele)  { return r.bottom() - (ele - eleBot) / eleRange * r.height(); };

        // ── Full elevation polygon (dim fill) ────────────────────────────
        QPolygonF poly;
        poly << QPointF(r.left(), r.bottom());
        for (const auto &pt : m_track)
            poly << QPointF(xFor(pt.distFromStart), yFor(pt.ele));
        poly << QPointF(r.right(), r.bottom());

        QLinearGradient bgGrad(0, r.top(), 0, r.bottom());
        bgGrad.setColorAt(0.0, QColor(137, 180, 250, 55));
        bgGrad.setColorAt(1.0, QColor(137, 180, 250, 15));
        p.setPen(Qt::NoPen);
        p.setBrush(bgGrad);
        p.drawPolygon(poly);

        // ── Ridden portion (brighter fill) ────────────────────────────────
        int curIdx = static_cast<int>(m_track.size()) - 1;
        for (int i = 0; i < static_cast<int>(m_track.size()) - 1; ++i) {
            if (m_track[i + 1].distFromStart >= m_distTravelled) { curIdx = i; break; }
        }
        const auto &cp1 = m_track[curIdx];
        const auto &cp2 = (curIdx + 1 < m_track.size()) ? m_track[curIdx + 1] : cp1;
        const double ct = (cp2.distFromStart > cp1.distFromStart)
            ? qBound(0.0, (m_distTravelled - cp1.distFromStart) / (cp2.distFromStart - cp1.distFromStart), 1.0)
            : 0.0;
        const double curEle = cp1.ele + ct * (cp2.ele - cp1.ele);
        const double curX   = xFor(m_distTravelled);

        if (m_distTravelled > 0.0) {
            QPolygonF ridden;
            ridden << QPointF(r.left(), r.bottom());
            for (int i = 0; i <= curIdx; ++i)
                ridden << QPointF(xFor(m_track[i].distFromStart), yFor(m_track[i].ele));
            ridden << QPointF(curX, yFor(curEle));
            ridden << QPointF(curX, r.bottom());

            QLinearGradient ridGrad(0, r.top(), 0, r.bottom());
            ridGrad.setColorAt(0.0, QColor(137, 180, 250, 170));
            ridGrad.setColorAt(1.0, QColor(137, 180, 250, 55));
            p.setBrush(ridGrad);
            p.drawPolygon(ridden);
        }

        // ── Outline polyline ──────────────────────────────────────────────
        QPolygonF line;
        for (const auto &pt : m_track)
            line << QPointF(xFor(pt.distFromStart), yFor(pt.ele));
        p.setPen(QPen(QColor(137, 180, 250, 110), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);

        // ── Vertical bike cursor ──────────────────────────────────────────
        if (m_distTravelled > 0.0) {
            p.setPen(QPen(QColor("#f38ba8"), 1.5));
            p.drawLine(QPointF(curX, r.top()), QPointF(curX, r.bottom()));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#f38ba8"));
            p.drawEllipse(QPointF(curX, yFor(curEle)), 3.0, 3.0);
        }

        // ── Min / max elevation labels ────────────────────────────────────
        p.setPen(QColor("#585b70"));
        p.setFont(QFont("Sans", 9));
        p.drawText(QRectF(r.left(), r.top() - 1, 45, 13),
                   Qt::AlignLeft | Qt::AlignTop,
                   QString("%1 m").arg(static_cast<int>(m_maxEle)));
        p.drawText(QRectF(r.left(), r.bottom() - 12, 45, 13),
                   Qt::AlignLeft | Qt::AlignBottom,
                   QString("%1 m").arg(static_cast<int>(m_minEle)));
    }

private:
    QVector<DashboardWidget::GpxPoint> m_track;
    double m_distTravelled = 0.0;
    double m_minEle = 0.0, m_maxEle = 0.0;
    double m_zoomStart = -1.0, m_zoomEnd = -1.0;
};

// ── 300 m sliding-window elevation zoom widget ────────────────────────────────
// Shows the elevation profile for a 300 m window around the current bike position.
class ElevationZoomWidget : public QWidget
{
public:
    explicit ElevationZoomWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setStyleSheet("background: #181825; border-radius: 6px;");
    }

    void setTrack(const QVector<DashboardWidget::GpxPoint> &track)
    {
        m_track = track;
        m_distTravelled = 0.0;
        update();
    }

    void setProgress(double distTravelledKm)
    {
        m_distTravelled = distTravelledKm;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#181825"));

        if (m_track.size() < 2) {
            p.setPen(QColor("#45475a"));
            p.setFont(QFont("Sans", 8));
            p.drawText(rect(), Qt::AlignCenter, "300 m window");
            return;
        }

        // Window: 150 m before and after the bike, clamped to route extents
        const double windowM = 300.0;
        const double halfW   = windowM / 2.0 / 1000.0;   // km
        const double totalDist = m_track.last().distFromStart;
        const double wStart  = qMax(0.0, m_distTravelled - halfW);
        const double wEnd    = qMin(totalDist, m_distTravelled + halfW);
        if (wEnd <= wStart) return;

        // Interpolate elevation at any dist position within the track
        auto interpolateEle = [&](double dist) -> double {
            for (int i = 0; i < m_track.size() - 1; ++i) {
                if (m_track[i + 1].distFromStart >= dist) {
                    const double d1 = m_track[i].distFromStart;
                    const double d2 = m_track[i + 1].distFromStart;
                    const double t  = (d2 > d1) ? (dist - d1) / (d2 - d1) : 0.0;
                    return m_track[i].ele + t * (m_track[i + 1].ele - m_track[i].ele);
                }
            }
            return m_track.last().ele;
        };

        // Build window points with interpolated endpoints for smooth scrolling
        struct Pt { double dist; double ele; };
        QVector<Pt> pts;
        pts.append({wStart, interpolateEle(wStart)});
        for (const auto &g : m_track) {
            if (g.distFromStart > wStart && g.distFromStart < wEnd)
                pts.append({g.distFromStart, g.ele});
        }
        pts.append({wEnd, interpolateEle(wEnd)});

        double minE = pts.first().ele, maxE = pts.first().ele;
        for (const auto &q : pts) {
            if (q.ele < minE) minE = q.ele;
            if (q.ele > maxE) maxE = q.ele;
        }
        const double span = (maxE - minE) < 1.0 ? 1.0 : (maxE - minE);
        const double pad  = span * 0.20;
        const double eBot = minE - pad;
        const double eRng = span + 2 * pad;

        const int px = 6, py = 6;
        const QRectF r(px, py, width() - 2 * px, height() - 2 * py);

        auto xFor = [&](double d) { return r.left() + (d - wStart) / (wEnd - wStart) * r.width(); };
        auto yFor = [&](double e) { return r.bottom() - (e - eBot) / eRng * r.height(); };

        // Filled polygon
        QPolygonF poly;
        poly << QPointF(r.left(), r.bottom());
        for (const auto &q : pts)
            poly << QPointF(xFor(q.dist), yFor(q.ele));
        poly << QPointF(r.right(), r.bottom());

        QLinearGradient grad(0, r.top(), 0, r.bottom());
        grad.setColorAt(0.0, QColor(166, 227, 161, 100));
        grad.setColorAt(1.0, QColor(166, 227, 161, 20));
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawPolygon(poly);

        // Outline
        QPolygonF line;
        for (const auto &q : pts)
            line << QPointF(xFor(q.dist), yFor(q.ele));
        p.setPen(QPen(QColor("#a6e3a1"), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(line);

        // Bike position: red outer dot + white inner dot on the elevation curve
        const double bikeEle = interpolateEle(m_distTravelled);
        const double bx = xFor(m_distTravelled);
        const double by = yFor(bikeEle);
        if (bx >= r.left() && bx <= r.right()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#f38ba8")); // red
            p.drawEllipse(QPointF(bx, by), 6.0, 6.0);
            p.setBrush(Qt::white);
            p.drawEllipse(QPointF(bx, by), 3.0, 3.0);
        }

        // Min / max labels
        p.setPen(QColor("#585b70"));
        p.setFont(QFont("Sans", 9));
        p.drawText(QRectF(r.left(), r.top() - 1, 50, 13),
                   Qt::AlignLeft | Qt::AlignTop,
                   QString("%1 m").arg(static_cast<int>(maxE)));
        p.drawText(QRectF(r.left(), r.bottom() - 12, 50, 13),
                   Qt::AlignLeft | Qt::AlignBottom,
                   QString("%1 m").arg(static_cast<int>(minE)));
    }

private:
    QVector<DashboardWidget::GpxPoint> m_track;
    double m_distTravelled = 0.0;
};

#include "dashboardwidget.moc"

// ─────────────────────────────────────────────────────────────────────────────

static QString gpxStorageDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                  + "/WattsFun/routes";
    QDir().mkpath(dir);
    return dir;
}

void DashboardWidget::buildMapTab(QWidget *parent)
{
    parent->setStyleSheet("QWidget { background: transparent; color: #cdd6f4; }");
    auto *lay = new QVBoxLayout(parent);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(4);

    const QString btnStyle =
        "QPushButton { background: #313244; border-radius: 4px;"
        "  font-size: 14px; font-weight: 600; padding: 2px 8px;"
        "  min-height: 26px; max-height: 26px;"
        "  border: 1px solid #45475a; }"
        "QPushButton:hover { background: #45475a; }";

    // ── Route list header ─────────────────────────────────────────────────
    auto *hdrRow = new QWidget(parent);
    auto *hdrLay = new QHBoxLayout(hdrRow);
    hdrLay->setContentsMargins(0, 0, 0, 0);
    hdrLay->setSpacing(4);

    // Toggle button: expands/collapses the route list
    m_routeToggleBtn = new QPushButton("\xE2\x96\xBE  Saved Routes", hdrRow);
    m_routeToggleBtn->setStyleSheet(
        "QPushButton { background: transparent; border: none;"
        "  color: #89b4fa; font-size: 15px; font-weight: 700;"
        "  text-align: left; padding: 0; }"
        "QPushButton:hover { color: #cdd6f4; }");
    m_routeToggleBtn->setCursor(Qt::PointingHandCursor);
    hdrLay->addWidget(m_routeToggleBtn, 1);

    m_routeSortCombo = new QComboBox(hdrRow);
    m_routeSortCombo->addItem("Sort: Name",      0);
    m_routeSortCombo->addItem("Sort: Distance",   1);
    m_routeSortCombo->addItem("Sort: Elevation",  2);
    m_routeSortCombo->setStyleSheet(
        "QComboBox { background: #313244; color: #cdd6f4;"
        "  border: 1px solid #45475a; border-radius: 4px;"
        "  padding: 3px 8px; font-size: 13px; }"
        "QComboBox:focus { border-color: #89b4fa; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1e1e2e; color: #cdd6f4;"
        "  border: 1px solid #45475a; selection-background-color: #313244; }");
    hdrLay->addWidget(m_routeSortCombo);

    auto *btnImportGpx = new QPushButton("+ Import GPX\xe2\x80\xa6", hdrRow);
    btnImportGpx->setStyleSheet(btnStyle + "QPushButton { color: #a6e3a1; }");
    hdrLay->addWidget(btnImportGpx);
    lay->addWidget(hdrRow);

    // ── Route list ────────────────────────────────────────────────────────
    m_routeListWidget = new QListWidget(parent);
    m_routeListWidget->setMinimumHeight(120);
    m_routeListWidget->setMaximumHeight(220);
    m_routeListWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_routeListWidget->setStyleSheet(
        "QListWidget { background: #181825; border: 1px solid #45475a;"
        "  border-radius: 6px; color: #cdd6f4; font-size: 15px; padding: 2px; }"
        "QListWidget::item { padding: 5px 10px; border-radius: 4px; }"
        "QListWidget::item:selected { background: #313244; color: #89b4fa; }"
        "QListWidget::item:hover:!selected { background: #252535; }");
    lay->addWidget(m_routeListWidget);

    // Wire the toggle button
    connect(m_routeToggleBtn, &QPushButton::clicked, this, [this]() {
        m_routeListExpanded = !m_routeListExpanded;
        m_routeListWidget->setVisible(m_routeListExpanded);
        m_routeToggleBtn->setText(
            m_routeListExpanded ? "\xE2\x96\xBE  Saved Routes" : "\xE2\x96\xB8  Saved Routes");
    });

    // ── Route info (compact, updated on GPX load) ─────────────────────────
    m_gpxNameLabel = new QLabel("No route loaded", parent);
    m_gpxNameLabel->setStyleSheet(
        "color: #cdd6f4; font-size: 15px; font-weight: 700; background: transparent; border: none;");
    lay->addWidget(m_gpxNameLabel);

    m_gpxInfoLabel = new QLabel("", parent);
    m_gpxInfoLabel->setStyleSheet(
        "color: #a6adc8; font-size: 14px; background: transparent; border: none;");
    lay->addWidget(m_gpxInfoLabel);

    // ── Mini map (fills all available vertical space) ─────────────────────
    m_gpxMapWidget = new GpxMapWidget(parent);
    lay->addWidget(m_gpxMapWidget, 1);

    // ── Elevation profile ─────────────────────────────────────────────────
    m_elevationProfileWidget = new ElevationProfileWidget(parent);
    lay->addWidget(m_elevationProfileWidget);

    // ── Bottom panel: 1/3 live data | 1/3 Grade Effect | 1/3 300m zoom ──
    auto *bottomRow = new QWidget(parent);
    bottomRow->setFixedHeight(90);
    auto *bottomLay = new QHBoxLayout(bottomRow);
    bottomLay->setContentsMargins(0, 2, 0, 0);
    bottomLay->setSpacing(6);

    // ── Left third: grade / distance / ascent ─────────────────────────────
    auto *leftCol = new QWidget(bottomRow);
    leftCol->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(2);
    leftLay->setAlignment(Qt::AlignVCenter);

    m_gpxGradeLabel = new QLabel("", leftCol);
    m_gpxGradeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_gpxGradeLabel->setStyleSheet(
        "color: #f9e2af; font-size: 16px; font-weight: 700;"
        " background: transparent; border: none;");
    leftLay->addWidget(m_gpxGradeLabel);

    m_gpxProgressLabel = new QLabel("", leftCol);
    m_gpxProgressLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_gpxProgressLabel->setStyleSheet(
        "color: #a6adc8; font-size: 14px; background: transparent; border: none;");
    leftLay->addWidget(m_gpxProgressLabel);

    m_gpxAscentLabel = new QLabel("", leftCol);
    m_gpxAscentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_gpxAscentLabel->setStyleSheet(
        "color: #a6e3a1; font-size: 14px; background: transparent; border: none;");
    leftLay->addWidget(m_gpxAscentLabel);

    bottomLay->addWidget(leftCol, 1);

    // ── Middle third: 300 m elevation zoom ────────────────────────────────
    m_elevationZoomWidget = new ElevationZoomWidget(bottomRow);
    bottomLay->addWidget(m_elevationZoomWidget, 1);

    // ── Right third: Grade Effect slider ──────────────────────────────────
    auto *rightCol = new QWidget(bottomRow);
    auto *rightLay = new QVBoxLayout(rightCol);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(2);

    auto *geTitle2 = new QLabel("Grade Effect", rightCol);
    geTitle2->setAlignment(Qt::AlignCenter);
    geTitle2->setStyleSheet(
        "color: #a6adc8; font-size: 13px; background: transparent; border: none;");
    rightLay->addWidget(geTitle2);

    auto *geSlider2 = new QSlider(Qt::Horizontal, rightCol);
    geSlider2->setRange(50, 100);
    geSlider2->setValue(100);
    geSlider2->setFixedHeight(18);
    geSlider2->setStyleSheet(
        "QSlider::groove:horizontal { height:4px; background:#313244; border-radius:2px; }"
        "QSlider::handle:horizontal { width:14px; height:14px; margin:-5px 0;"
        " background:#89b4fa; border-radius:7px; }"
        "QSlider::sub-page:horizontal { background:#89b4fa; border-radius:2px; }");
    rightLay->addWidget(geSlider2);

    auto *geValLabel2 = new QLabel("100%", rightCol);
    geValLabel2->setAlignment(Qt::AlignCenter);
    geValLabel2->setStyleSheet(
        "color: #89b4fa; font-size: 14px; font-weight: 700;"
        " background: transparent; border: none;");
    rightLay->addWidget(geValLabel2);

    rightLay->addStretch();
    bottomLay->addWidget(rightCol, 1);

    // Wire the new right-column slider to the same members
    m_gradeEffectSlider = geSlider2;
    m_gradeEffectLabel  = geValLabel2;
    connect(geSlider2, &QSlider::valueChanged, this, [this](int val) {
        m_gradeEffectPct = val;
        m_gradeEffectLabel->setText(QString("%1%").arg(val));
    });

    lay->addWidget(bottomRow);



    // ── Connections ───────────────────────────────────────────────────────
    connect(btnImportGpx, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, "Import GPX File",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "GPX Files (*.gpx);;All Files (*)");
        if (path.isEmpty()) return;
        const QString destDir  = gpxStorageDir();
        const QString destPath = destDir + "/" + QFileInfo(path).fileName();
        if (!QFile::exists(destPath))
            QFile::copy(path, destPath);
        refreshRouteList();
        for (int i = 0; i < m_routeListWidget->count(); ++i) {
            if (m_routeListWidget->item(i)->data(Qt::UserRole).toString() == destPath) {
                m_routeListWidget->setCurrentRow(i);
                break;
            }
        }
        QFile f(destPath);
        if (f.open(QIODevice::ReadOnly))
            loadGpxFromData(f.readAll(), QFileInfo(destPath).baseName());
    });
    connect(m_routeListWidget, &QListWidget::currentItemChanged,
            this, [this](QListWidgetItem *item, QListWidgetItem *) {
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            loadGpxFromData(f.readAll(), QFileInfo(path).baseName());
    });

    // Wire sort combo
    connect(m_routeSortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { sortRouteList(); });
}

// Lightweight GPX stats parser — reads only lat/lon/ele, no smoothing.
static void parseGpxQuick(const QString &path, double &distKm, double &elevGainM)
{
    distKm = 0.0;
    elevGainM = 0.0;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QXmlStreamReader xml(&f);
    double prevLat = 0.0, prevLon = 0.0, prevEle = 0.0;
    bool first = true;
    while (!xml.atEnd() && !xml.hasError()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        if (xml.name() != QLatin1String("trkpt")) continue;
        double lat = xml.attributes().value("lat").toDouble();
        double lon = xml.attributes().value("lon").toDouble();
        double ele = 0.0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("ele"))
                ele = xml.readElementText().toDouble();
            else if (xml.isEndElement() && xml.name() == QLatin1String("trkpt"))
                break;
        }
        if (!first) {
            // Flat-earth approximation is sufficient for a label
            constexpr double R = 6371.0;
            const double dLat = qDegreesToRadians(lat - prevLat);
            const double dLon = qDegreesToRadians(lon - prevLon);
            const double a = qSin(dLat/2)*qSin(dLat/2)
                           + qCos(qDegreesToRadians(prevLat))*qCos(qDegreesToRadians(lat))
                             *qSin(dLon/2)*qSin(dLon/2);
            distKm += R * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
            const double diff = ele - prevEle;
            if (diff > 0.0) elevGainM += diff;
        }
        prevLat = lat; prevLon = lon; prevEle = ele;
        first = false;
    }
}

void DashboardWidget::refreshRouteList()
{
    if (!m_routeListWidget) return;
    const QString dir = gpxStorageDir();
    const QStringList files = QDir(dir).entryList({"*.gpx"}, QDir::Files, QDir::Name | QDir::Reversed);
    m_routeListWidget->clear();
    for (const QString &fname : files) {
        double distKm = 0.0, elevGainM = 0.0;
        parseGpxQuick(dir + "/" + fname, distKm, elevGainM);
        const QString label = QString("%1   %2 km  +%3 m")
            .arg(QFileInfo(fname).baseName())
            .arg(distKm, 0, 'f', 1)
            .arg(static_cast<int>(elevGainM));
        auto *item = new QListWidgetItem(label);
        item->setData(Qt::UserRole,     dir + "/" + fname);
        item->setData(Qt::UserRole + 1, distKm);
        item->setData(Qt::UserRole + 2, elevGainM);
        m_routeListWidget->addItem(item);
    }
}

void DashboardWidget::sortRouteList()
{
    if (!m_routeListWidget || m_routeListWidget->count() == 0) return;
    const int mode = m_routeSortCombo ? m_routeSortCombo->currentData().toInt() : 0;

    // Collect items with metadata
    struct Entry { QString label; QString path; double dist; double elev; };
    QVector<Entry> entries;
    entries.reserve(m_routeListWidget->count());
    for (int i = 0; i < m_routeListWidget->count(); ++i) {
        auto *item = m_routeListWidget->item(i);
        entries.append({ item->text(),
                         item->data(Qt::UserRole).toString(),
                         item->data(Qt::UserRole + 1).toDouble(),
                         item->data(Qt::UserRole + 2).toDouble() });
    }

    // Sort
    std::sort(entries.begin(), entries.end(), [mode](const Entry &a, const Entry &b) {
        if (mode == 1) return a.dist < b.dist;
        if (mode == 2) return a.elev < b.elev;
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });

    // Rebuild list
    m_routeListWidget->blockSignals(true);
    m_routeListWidget->clear();
    for (const auto &e : entries) {
        auto *item = new QListWidgetItem(e.label);
        item->setData(Qt::UserRole,     e.path);
        item->setData(Qt::UserRole + 1, e.dist);
        item->setData(Qt::UserRole + 2, e.elev);
        m_routeListWidget->addItem(item);
    }
    m_routeListWidget->blockSignals(false);
}

// ── GPX helpers ───────────────────────────────────────────────────────────────

static double haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 6371.0; // Earth radius km
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2) * qSin(dLat / 2)
                   + qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2))
                   * qSin(dLon / 2) * qSin(dLon / 2);
    return R * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
}

void DashboardWidget::loadGpxFromData(const QByteArray &data, const QString &name)
{
    m_gpxTrack.clear();
    m_gpxName = name;
    m_gpxDistanceTravelled = 0.0;

    QXmlStreamReader xml(data);
    QString trkName;

    while (!xml.atEnd() && !xml.hasError()) {
        const auto token = xml.readNext();
        if (token != QXmlStreamReader::StartElement) continue;

        if (trkName.isEmpty() && xml.name() == QLatin1String("name"))
            trkName = xml.readElementText();
        else if (xml.name() == QLatin1String("trkpt")) {
            GpxPoint pt;
            pt.lat = xml.attributes().value("lat").toDouble();
            pt.lon = xml.attributes().value("lon").toDouble();
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("ele"))
                    pt.ele = xml.readElementText().toDouble();
                else if (xml.isEndElement() && xml.name() == QLatin1String("trkpt"))
                    break;
            }
            pt.distFromStart = m_gpxTrack.isEmpty()
                ? 0.0
                : m_gpxTrack.last().distFromStart
                  + haversineKm(m_gpxTrack.last().lat, m_gpxTrack.last().lon, pt.lat, pt.lon);
            m_gpxTrack.append(pt);
        }
    }

    if (m_gpxTrack.isEmpty()) {
        m_gpxNameLabel->setText("Failed to parse GPX file");
        m_gpxInfoLabel->setText("");
        return;
    }

    // ── Triangular-kernel elevation smoothing (±50 m window) ──────────
    // Raw GPS elevations are noisy; smooth them before computing grade or
    // total ascent.  A triangular kernel (linear decay with distance) is
    // used so nearby points have more influence.
    {
        const double halfW = 0.050;  // 50 m in km
        QVector<double> smoothed(m_gpxTrack.size());
        for (int i = 0; i < m_gpxTrack.size(); ++i) {
            const double cDist = m_gpxTrack[i].distFromStart;
            double wSum = 0.0, eSum = 0.0;
            // look left
            for (int j = i; j >= 0; --j) {
                const double d = cDist - m_gpxTrack[j].distFromStart;
                if (d > halfW) break;
                const double w = 1.0 - d / halfW;  // triangular weight
                wSum += w;
                eSum += w * m_gpxTrack[j].ele;
            }
            // look right (skip i itself, already counted)
            for (int j = i + 1; j < m_gpxTrack.size(); ++j) {
                const double d = m_gpxTrack[j].distFromStart - cDist;
                if (d > halfW) break;
                const double w = 1.0 - d / halfW;
                wSum += w;
                eSum += w * m_gpxTrack[j].ele;
            }
            smoothed[i] = (wSum > 0.0) ? eSum / wSum : m_gpxTrack[i].ele;
        }
        for (int i = 0; i < m_gpxTrack.size(); ++i)
            m_gpxTrack[i].ele = smoothed[i];
    }

    m_gpxTotalDistKm = m_gpxTrack.last().distFromStart;
    double totalAscent = 0.0;
    for (int i = 1; i < m_gpxTrack.size(); ++i) {
        const double diff = m_gpxTrack[i].ele - m_gpxTrack[i - 1].ele;
        if (diff > 0.0) totalAscent += diff;
    }
    m_gpxTotalAscent = totalAscent;
    m_rideAscent = 0.0;
    m_prevEleForAscent = -1.0;

    const QString displayName = trkName.isEmpty() ? name : trkName;
    m_gpxNameLabel->setText(displayName);
    m_gpxInfoLabel->setText(
        QString("%1 km  |  +%2 m elevation")
        .arg(m_gpxTotalDistKm, 0, 'f', 1)
        .arg(totalAscent, 0, 'f', 0));
    m_gpxGradeLabel->setText("Route loaded \xe2\x80\x93 press START to ride");
    m_gpxProgressLabel->setText(QString("0.00 / %1 km").arg(m_gpxTotalDistKm, 0, 'f', 1));
    if (m_gpxAscentLabel)
        m_gpxAscentLabel->setText(QString("+0 / %1 m").arg(static_cast<int>(totalAscent)));

    if (m_gpxMapWidget) {
        m_gpxMapWidget->setTrack(m_gpxTrack);
        m_gpxMapWidget->setProgress(0.0);
    }
    if (m_elevationProfileWidget) {
        m_elevationProfileWidget->setTrack(m_gpxTrack);
        m_elevationProfileWidget->setProgress(0.0);
    }
    if (m_elevationZoomWidget) {
        m_elevationZoomWidget->setTrack(m_gpxTrack);
        m_elevationZoomWidget->setProgress(0.0);
    }
}

// ── Map position interpolation timer (~20 Hz) ─────────────────────────────────
// Advances m_gpxDistanceTravelled between the 1-second chart ticks so the bike
// marker and map scroll smoothly.  Defined here so that GpxMapWidget and
// ElevationProfileWidget are fully declared before this function body.
void DashboardWidget::onMapTimerTick()
{
    if (!m_workoutActive || m_trainingMode != MapRideMode || m_gpxTrack.size() < 2) return;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_mapTimerLastMs > 0) {
        const double dt = (nowMs - m_mapTimerLastMs) / 1000.0;
        if (dt > 0.0 && dt < 0.5) {  // guard against clock jumps / first call
            m_gpxDistanceTravelled += m_lastSpeed * dt / 3600.0;
            // Loop: wrap distance back to start when we exceed total route length
            if (m_gpxTotalDistKm > 0.0 && m_gpxDistanceTravelled >= m_gpxTotalDistKm)
                m_gpxDistanceTravelled = std::fmod(m_gpxDistanceTravelled, m_gpxTotalDistKm);
        }
    }
    m_mapTimerLastMs = nowMs;

    if (m_gpxMapWidget)
        m_gpxMapWidget->setProgress(m_gpxDistanceTravelled);
    if (m_elevationProfileWidget)
        m_elevationProfileWidget->setProgress(m_gpxDistanceTravelled);
    if (m_elevationZoomWidget)
        m_elevationZoomWidget->setProgress(m_gpxDistanceTravelled);
}

void DashboardWidget::updateMapGrade()
{
    if (m_gpxTrack.size() < 2 || !m_workoutActive) return;

    // Distance is now advanced by m_mapTimer at ~20 Hz; no advance here.
    // Loop: wrap distance back to start when we exceed total route length
    if (m_gpxTotalDistKm > 0.0 && m_gpxDistanceTravelled >= m_gpxTotalDistKm)
        m_gpxDistanceTravelled = std::fmod(m_gpxDistanceTravelled, m_gpxTotalDistKm);

    // Binary search for the current segment
    int segIdx = static_cast<int>(m_gpxTrack.size()) - 2;
    for (int i = 0; i < static_cast<int>(m_gpxTrack.size()) - 1; ++i) {
        if (m_gpxTrack[i + 1].distFromStart >= m_gpxDistanceTravelled) {
            segIdx = i;
            break;
        }
    }

    const GpxPoint &p1 = m_gpxTrack[segIdx];
    const GpxPoint &p2 = m_gpxTrack[segIdx + 1];
    const double segDistKm = p2.distFromStart - p1.distFromStart;
    double currentEle = p1.ele;

    if (segDistKm > 1e-4) {
        const double t = (m_gpxDistanceTravelled - p1.distFromStart) / segDistKm;
        currentEle = p1.ele + t * (p2.ele - p1.ele);
        m_currentLat = p1.lat + t * (p2.lat - p1.lat);
        m_currentLon = p1.lon + t * (p2.lon - p1.lon);
    } else {
        m_currentLat = p1.lat;
        m_currentLon = p1.lon;
    }
    m_currentEle = currentEle;

    // Smooth grade: distance-weighted average over a 100 m window starting
    // at the current position and looking forward.  This eliminates the
    // perceived lag between the grade label and the 300 m zoom profile.
    const int n = static_cast<int>(m_gpxTrack.size());
    const double windowKm = 0.100;  // 100 m in km
    const double curDist  = m_gpxDistanceTravelled;
    int iFirst = segIdx, iLast = segIdx;
    // Only look forward from the current position
    while (iLast < n - 2 && (m_gpxTrack[iLast + 1].distFromStart - curDist) < windowKm)
        ++iLast;
    double gradeSum  = 0.0;
    double weightSum = 0.0;
    for (int i = iFirst; i <= iLast; ++i) {
        const double d = m_gpxTrack[i + 1].distFromStart - m_gpxTrack[i].distFromStart;
        if (d > 1e-4) {
            const double g = (m_gpxTrack[i + 1].ele - m_gpxTrack[i].ele) / (d * 1000.0) * 100.0;
            gradeSum  += g * d;
            weightSum += d;
        }
    }
    double grade = (weightSum > 1e-9) ? gradeSum / weightSum : 0.0;
    grade = std::max(-25.0, std::min(25.0, grade));

    // Apply grade effect factor (50–100 %): scales grade for both trainer resistance and speed model
    const double effectiveGrade = grade * (m_gradeEffectPct / 100.0);
    m_gradeTargetPct = effectiveGrade;  // store for speed model in updateTrainer
    emit gradeChanged(effectiveGrade);

    // Show real grade in label; indicate effect factor if < 100 %
    if (m_gradeEffectPct < 100) {
        m_gpxGradeLabel->setText(
            QString("Grade: %1% (\u00d7%2)")
            .arg(grade, 0, 'f', 1)
            .arg(m_gradeEffectPct / 100.0, 0, 'f', 2));
    } else {
        m_gpxGradeLabel->setText(
            QString("Grade: %1%")
            .arg(grade, 0, 'f', 1));
    }
    // Live elevation gain: accumulate positive changes each 1-second tick
    if (m_prevEleForAscent >= 0.0 && currentEle > m_prevEleForAscent)
        m_rideAscent += currentEle - m_prevEleForAscent;
    m_prevEleForAscent = currentEle;
    if (m_gpxAscentLabel)
        m_gpxAscentLabel->setText(
            QString("+%1 / %2 m")
            .arg(static_cast<int>(m_rideAscent))
            .arg(static_cast<int>(m_gpxTotalAscent)));

    m_gpxProgressLabel->setText(
        QString("%1 / %2 km")
        .arg(m_gpxDistanceTravelled, 0, 'f', 2)
        .arg(m_gpxTotalDistKm, 0, 'f', 1));
    if (m_gpxMapWidget)
        m_gpxMapWidget->setProgress(m_gpxDistanceTravelled);
    if (m_elevationProfileWidget)
        m_elevationProfileWidget->setProgress(m_gpxDistanceTravelled);
    if (m_elevationZoomWidget)
        m_elevationZoomWidget->setProgress(m_gpxDistanceTravelled);
}

// ── Interval training helpers ─────────────────────────────────────────────────

void DashboardWidget::applyIntervalStep(int stepIdx)
{
    if (m_selectedProgramIdx < 0 || m_selectedProgramIdx >= m_programs.size()) return;
    const auto &prog = m_programs[m_selectedProgramIdx];
    if (stepIdx < 0 || stepIdx >= prog.steps.size()) return;

    m_currentStepIdx = stepIdx;
    m_stepElapsedSec = 0;
    const auto &step = prog.steps[stepIdx];

    if (step.isErg) {
        m_ergTargetWatts = step.ergWatts;
        emit ergTargetChanged(m_ergTargetWatts);
    } else {
        m_gradeTargetPct = step.gradePct;
        emit gradeChanged(m_gradeTargetPct);
    }
    updateIntervalDisplay();
}

void DashboardWidget::updateIntervalDisplay()
{
    if (m_selectedProgramIdx < 0 || m_selectedProgramIdx >= m_programs.size()) return;
    const auto &prog = m_programs[m_selectedProgramIdx];

    if (m_currentStepIdx < 0) {
        m_intervalStepLabel->setText(u8"\u2013");
        m_intervalTimeLabel->setText(
            QString("Ready \xe2\x80\x93 %1 steps | press START to begin").arg(prog.steps.size()));
        m_intervalTargetLabel->setStyleSheet(
            "color: #cdd6f4; font-size: 14px; font-weight: 600;"
            " background: transparent; border: none;");
        m_intervalTargetLabel->setText("");
        m_intervalStepBar->setValue(0);
        return;
    }

    const auto &step = prog.steps[m_currentStepIdx];
    const int remaining = step.durationSec - m_stepElapsedSec;
    m_intervalStepLabel->setText(
        QString("Step %1/%2: %3")
        .arg(m_currentStepIdx + 1).arg(prog.steps.size()).arg(step.name));
    {
        int rh = remaining / 3600;
        int rm = (remaining % 3600) / 60;
        int rs = remaining % 60;
        if (rh > 0)
            m_intervalTimeLabel->setText(
                QString("%1:%2:%3 remaining")
                .arg(rh).arg(rm, 2, 10, QChar('0')).arg(rs, 2, 10, QChar('0')));
        else
            m_intervalTimeLabel->setText(
                QString("%1:%2 remaining")
                .arg(rm, 2, 10, QChar('0')).arg(rs, 2, 10, QChar('0')));
    }
    if (step.isErg) {
        const QString col = zoneColorForWatts(step.ergWatts, m_ftpWatts);
        m_intervalTargetLabel->setStyleSheet(
            QString("color: %1; font-size: 14px; font-weight: 600;"
                    " background: transparent; border: none;").arg(col));
        m_intervalTargetLabel->setText(QString("Target: %1 W").arg(step.ergWatts));
    } else {
        m_intervalTargetLabel->setStyleSheet(
            "color: #f9e2af; font-size: 14px; font-weight: 600;"
            " background: transparent; border: none;");
        m_intervalTargetLabel->setText(
            QString("Target: %1% grade").arg(step.gradePct, 0, 'f', 1));
    }
    if (step.durationSec > 0)
        m_intervalStepBar->setValue(m_stepElapsedSec * 100 / step.durationSec);
}

void DashboardWidget::onIntervalTick()
{
    if (!m_workoutActive || m_trainingMode != IntervalMode) {
        m_intervalTimer.stop();
        return;
    }
    if (m_selectedProgramIdx < 0 || m_selectedProgramIdx >= m_programs.size()) return;
    const auto &prog = m_programs[m_selectedProgramIdx];
    if (m_currentStepIdx < 0 || m_currentStepIdx >= prog.steps.size()) return;

    ++m_stepElapsedSec;
    if (m_stepElapsedSec >= prog.steps[m_currentStepIdx].durationSec) {
        const int nextStep = m_currentStepIdx + 1;
        if (nextStep < prog.steps.size()) {
            applyIntervalStep(nextStep);
        } else {
            m_intervalStepLabel->setText("Program Complete!");
            m_intervalTimeLabel->setText("Great workout! Well done.");
            m_intervalTargetLabel->setText("");
            m_intervalStepBar->setValue(100);
            m_intervalTimer.stop();
        }
    } else {
        updateIntervalDisplay();
    }
}

// ── Running state helpers ─────────────────────────────────────────────────────

void DashboardWidget::setRunningState(bool running)
{
    m_btnStartStop->setText(running ? "STOP" : "START");
    m_btnStartStop->setProperty("running", running);
    m_btnStartStop->style()->unpolish(m_btnStartStop);
    m_btnStartStop->style()->polish(m_btnStartStop);

    // Pause is only available while a workout is running
    m_btnPause->setEnabled(running);
    if (!running) {
        // Reset pause button appearance when workout ends
        m_btnPause->setText("⏸ PAUSE");
        m_btnPause->setProperty("paused", false);
        m_btnPause->style()->unpolish(m_btnPause);
        m_btnPause->style()->polish(m_btnPause);
    }
}

void DashboardWidget::setStartEnabled(bool enabled)
{
    m_btnStartStop->setEnabled(enabled);
}

void DashboardWidget::updateControlDisplay()
{
    const bool erg = (m_trainerMode == ErgMode);

    m_btnErgMode->setObjectName(erg   ? "modeActive" : "modeInactive");
    m_btnGradeMode->setObjectName(erg ? "modeInactive" : "modeActive");
    // Re-polish so the stylesheet is re-applied after objectName change
    m_btnErgMode->style()->unpolish(m_btnErgMode);
    m_btnErgMode->style()->polish(m_btnErgMode);
    m_btnGradeMode->style()->unpolish(m_btnGradeMode);
    m_btnGradeMode->style()->polish(m_btnGradeMode);

    if (erg) {
        m_controlUnitLabel->setText("Target Power");
        m_controlValueLabel->setText(QString("%1 W").arg(m_ergTargetWatts));
        m_btnDecrement->setToolTip("-10 W");
        m_btnIncrement->setToolTip("+10 W");
    } else {
        m_controlUnitLabel->setText("Grade");
        m_controlValueLabel->setText(QString("%1 %").arg(m_gradeTargetPct, 0, 'f', 1));
        m_btnDecrement->setToolTip("-1 %");
        m_btnIncrement->setToolTip("+1 %");
    }
}

// ── private – dial view ──────────────────────────────────────────────────────

void DashboardWidget::buildDialView()
{
    // Power dial
    m_dialPower = new DialGauge("POWER", "W", this);
    m_dialPower->setMinimumSize(280, 240);
    m_dialPower->setRange(0, 500);
    m_dialPower->setZones({
        {0, QColor("#a6e3a1")},     // Green: 0-150W
        {150, QColor("#f9e2af")},   // Yellow: 150-250W
        {250, QColor("#fab387")},  // Orange: 250-350W
        {350, QColor("#f38ba8")}   // Red: 350+W
    });

    // Heart rate dial
    m_dialHr = new DialGauge("HEART RATE", "bpm", this);
    m_dialHr->setMinimumSize(280, 240);
    m_dialHr->setRange(40, 200);
    m_dialHr->setZones({
        {40, QColor("#a6e3a1")},    // Green: 40-120 bpm
        {120, QColor("#f9e2af")},  // Yellow: 120-150 bpm
        {150, QColor("#fab387")}, // Orange: 150-170 bpm
        {170, QColor("#f38ba8")}  // Red: 170+ bpm
    });
}

// Helper called from constructor to build the dial row widget
QWidget *DashboardWidget::dialRow()
{
    auto *frame = new QWidget(this);
    auto *layout = new QHBoxLayout(frame);
    layout->setSpacing(20);
    layout->setContentsMargins(20, 0, 20, 0);
    layout->setAlignment(Qt::AlignCenter);

    layout->addWidget(m_dialPower);
    layout->addWidget(m_dialHr);

    return frame;
}

// ── private – chart ───────────────────────────────────────────────────────────

void DashboardWidget::buildChart()
{
    m_chartView = new LiveChartWidget(this);
    m_chartView->setXRange(0, CHART_WINDOW_S);
    m_chartView->setYLeftRange(0, CHART_DEFAULT_MAX_POWER);
    m_chartView->setYRightRange(40, CHART_DEFAULT_MAX_HR);

    // Zone overlay badge anchored to top-right of the chart
    m_zoneOverlayLabel = new QLabel(m_chartView);
    m_zoneOverlayLabel->setStyleSheet(
        "QLabel { color: #a6adc8; background: rgba(17,17,27,180);"
        "  border-radius: 6px; padding: 4px 10px;"
        "  font-size: 16px; font-weight: 800; }");
    m_zoneOverlayLabel->setAlignment(Qt::AlignCenter);
    m_zoneOverlayLabel->move(8, 8);  // will be repositioned on resize
    m_zoneOverlayLabel->hide();
}

// ── slots ─────────────────────────────────────────────────────────────────────

void DashboardWidget::updateTrainer(const TrainerData &data)
{
    qDebug() << "[Dashboard] updateTrainer: power=" << data.powerWatts 
             << "cadence=" << data.cadenceRpm << "speed=" << data.speedKph;
    
    if (data.hasPower) {
        m_tilePower->setValue(data.powerWatts);
        m_dialPower->setValue(data.powerWatts);
        if (m_riderWeightKg > 0.0)
            m_tilePwrRatio->setValue(data.powerWatts / m_riderWeightKg);
        else
            m_tilePwrRatio->setNoData();
        // Live power zone overlay on chart
        const int z = powerZone(data.powerWatts, m_ftpWatts);
        if (m_zoneOverlayLabel) {
            m_zoneOverlayLabel->setText(QString("Z%1  %2").arg(z).arg(zoneName(z)));
            m_zoneOverlayLabel->setStyleSheet(QString(
                "QLabel { color: %1; background: rgba(17,17,27,180);"
                "  border-radius: 6px; padding: 4px 10px;"
                "  font-size: 16px; font-weight: 800; }").arg(zoneColor(z).name()));
            m_zoneOverlayLabel->adjustSize();
            m_zoneOverlayLabel->move(
                m_chartView->width() - m_zoneOverlayLabel->width() - 12, 8);
            m_zoneOverlayLabel->show();
        }
    } else {
        m_tilePower->setNoData();
        m_dialPower->setValue(0);
        m_tilePwrRatio->setNoData();
        if (m_zoneOverlayLabel) m_zoneOverlayLabel->hide();
    }

    if (data.hasCadence) {
        m_tileCadence->setValue(data.cadenceRpm);
        m_lastCadence = data.cadenceRpm;
    } else {
        m_tileCadence->setNoData();
    }

    double speedKph = 0.0;
    if (data.hasPower) {
        if (m_trainingMode == MapRideMode) {
            // Map mode: always compute speed from power + current grade so
            // the physics model governs virtual speed, not the trainer flywheel.
            speedKph = calculateSpeedFromPower(data.powerWatts, m_riderWeightKg,
                                               m_gradeTargetPct);
        } else if (data.speedKph > 0.0) {
            speedKph = data.speedKph;
        } else {
            speedKph = calculateSpeedFromPower(data.powerWatts);
        }
        m_tileSpeed->setValue(speedKph);
        m_lastSpeed = speedKph;
    } else {
        m_tileSpeed->setNoData();
    }

    // Update duration and distance
    if (m_workoutActive) {
        double duration = m_elapsed - m_workoutStartTime;
        int dtotal = static_cast<int>(duration);
        int dh = dtotal / 3600;
        int dm = (dtotal % 3600) / 60;
        int ds = dtotal % 60;
        if (dh > 0)
            m_tileDuration->setValue(QString("%1:%2:%3").arg(dh).arg(dm, 2, 10, QChar('0')).arg(ds, 2, 10, QChar('0')));
        else
            m_tileDuration->setValue(QString("%1:%2").arg(dm).arg(ds, 2, 10, QChar('0')));
        m_tileDistance->setValue(m_totalDistance);
    } else {
        m_tileDuration->setNoData();
        m_tileDistance->setNoData();
    }

    m_lastPower = data.hasPower ? data.powerWatts : m_lastPower;

    // Accumulate for averages if workout active
    if (m_workoutActive) {
        if (data.hasPower) {
            m_sumPower += data.powerWatts;
            m_countPower++;
        }
        if (data.hasCadence) {
            m_sumCadence += data.cadenceRpm;
            m_countCadence++;
        }
        if (data.hasPower) {
            m_sumSpeed += speedKph;
            m_countSpeed++;
            // Accumulate distance: speed (km/h) * time (hours) = distance (km)
            // Since we get updates every CHART_TICK_S seconds, convert to hours
            m_totalDistance += speedKph * (CHART_TICK_S / 3600.0);
        }
        updateAverages();
    }
}

void DashboardWidget::updateHrm(const HrmData &data)
{
    qDebug() << "[Dashboard] updateHrm: bpm=" << data.heartRateBpm << "valid=" << data.valid;
    
    if (data.valid) {
        m_tileHr->setValue(data.heartRateBpm);
        m_dialHr->setValue(data.heartRateBpm);
    } else {
        m_tileHr->setNoData();
        m_dialHr->setValue(40); // Minimum value
    }

    m_lastHr = data.valid ? static_cast<double>(data.heartRateBpm) : m_lastHr;

    // Accumulate for averages if workout active
    if (m_workoutActive && data.valid) {
        m_sumHr += data.heartRateBpm;
        m_countHr++;
        if (data.heartRateBpm > m_maxHr) {
            m_maxHr = data.heartRateBpm;
        }
        updateAverages();
    }
}

void DashboardWidget::reset()
{
    m_elapsed  = 0.0;
    m_lastPower = m_lastHr = 0.0;
    m_chartView->clearSeries();
    m_chartView->setXRange(0, CHART_WINDOW_S);
    m_tilePower->setNoData();
    m_tileCadence->setNoData();
    m_tileSpeed->setNoData();
    m_tileHr->setNoData();
    m_tileDuration->setNoData();
    m_tileDistance->setNoData();
    m_tileAvgPower->setNoData();
    m_tileAvgCadence->setNoData();
    m_tileAvgSpeed->setNoData();
    m_tileAvgHr->setNoData();
    if (m_zoneOverlayLabel) m_zoneOverlayLabel->hide();

    // Reset dials
    m_dialPower->setValue(0);
    m_dialHr->setValue(40);

    // Reset accumulation
    m_workoutActive = false;
    m_sumPower = m_sumCadence = m_sumSpeed = m_sumHr = 0.0;
    m_countPower = m_countCadence = m_countSpeed = m_countHr = 0;

    m_avgPower = m_avgCadence = m_avgSpeed = m_avgHr = 0.0;
    m_maxHr = 0.0;
    m_maxPowerSample = 0.0;
    m_workoutStartTime = 0.0;
    m_totalDistance = 0.0;
    m_lastCadence = m_lastSpeed = 0.0;
    m_samples.clear();

    // NP / IF / TSS
    m_npRolling.clear();
    m_npSumFourth = 0.0;
    m_npCount = 0;
    m_normalizedPower = 0.0;
    m_intensityFactor = 0.0;
    m_tss = 0.0;

    // Zone tracking
    m_timeInZone.fill(0);
    m_currentZone = 0;

    // Power bests
    m_powerHistory.clear();
    m_bestPower5s = 0.0;
    m_bestPower1min = 0.0;
    m_bestPower5min = 0.0;
    m_bestPower20min = 0.0;
}

void DashboardWidget::startWorkout()
{
    // If we were paused and the user somehow re-triggers start, make sure
    // the chart timer is running before we overwrite state.
    if (!m_chartTimer.isActive())
        m_chartTimer.start(static_cast<int>(CHART_TICK_S * 1000));

    m_workoutPaused    = false;
    m_workoutActive    = true;
    m_workoutStartTime = m_elapsed;
    m_totalDistance = 0.0;
    m_maxHr = 0.0;
    m_lastSpeed = 0.0;          // prevent phantom speed from previous ride
    emit gradeChanged(0.0);     // ensure grade/resistance starts at zero
    // Reset averages for new workout
    m_sumPower = m_sumCadence = m_sumSpeed = m_sumHr = 0.0;
    m_countPower = m_countCadence = m_countSpeed = m_countHr = 0;
    m_samples.clear();
    updateAverages();

    if (m_trainingMode == IntervalMode) {
        m_currentStepIdx = -1;
        m_stepElapsedSec = 0;
        applyIntervalStep(0);
        m_intervalTimer.start(1000);
    } else if (m_trainingMode == MapRideMode) {
        m_gpxDistanceTravelled = 0.0;
        m_rideAscent = 0.0;
        m_prevEleForAscent = -1.0;
        m_mapTimerLastMs = 0;  // start fresh; first map-timer tick will initialise the timestamp
        if (m_gpxMapWidget) m_gpxMapWidget->setProgress(0.0);
        if (m_elevationProfileWidget) m_elevationProfileWidget->setProgress(0.0);
        if (m_elevationZoomWidget) m_elevationZoomWidget->setProgress(0.0);
    }

    // Lock mode switching and selection lists while training is active
    for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
        if (b) b->setEnabled(false);
    if (m_programListWidget) m_programListWidget->setEnabled(false);
    if (m_routeListWidget)   m_routeListWidget->setEnabled(false);
}

void DashboardWidget::stopWorkout()
{
    m_workoutPaused = false;
    m_workoutActive = false;
    // Ensure the chart timer is running again (may have been stopped during pause)
    if (!m_chartTimer.isActive())
        m_chartTimer.start(static_cast<int>(CHART_TICK_S * 1000));
    m_intervalTimer.stop();
    m_currentStepIdx = -1;
    updateIntervalDisplay();

    // Re-enable mode switching and selection lists after training stops
    for (auto *b : {m_btnModeFreeRide, m_btnModeInterval, m_btnModeMap})
        if (b) b->setEnabled(true);
    if (m_programListWidget) m_programListWidget->setEnabled(true);
    if (m_routeListWidget)   m_routeListWidget->setEnabled(true);
}

// ── private helpers ───────────────────────────────────────────────────────────

void DashboardWidget::updateAverages()
{
    if (m_countPower > 0) {
        m_avgPower = m_sumPower / m_countPower;
        m_tileAvgPower->setValue(m_avgPower);
        if (m_riderWeightKg > 0.0)
            m_tileAvgPwrRatio->setValue(m_avgPower / m_riderWeightKg);
        else
            m_tileAvgPwrRatio->setNoData();
    } else {
        m_avgPower = 0.0;
        m_tileAvgPower->setNoData();
        m_tileAvgPwrRatio->setNoData();
    }

    if (m_countCadence > 0) {
        m_avgCadence = m_sumCadence / m_countCadence;
        m_tileAvgCadence->setValue(m_avgCadence);
    } else {
        m_avgCadence = 0.0;
        m_tileAvgCadence->setNoData();
    }

    if (m_countSpeed > 0) {
        m_avgSpeed = m_sumSpeed / m_countSpeed;
        m_tileAvgSpeed->setValue(m_avgSpeed);
    } else {
        m_avgSpeed = 0.0;
        m_tileAvgSpeed->setNoData();
    }

    if (m_countHr > 0) {
        m_avgHr = m_sumHr / m_countHr;
        m_tileAvgHr->setValue(m_avgHr);
    } else {
        m_avgHr = 0.0;
        m_tileAvgHr->setNoData();
    }
}

// ── Power bests: scan rolling windows of {5s, 1min, 5min, 20min} ──────────
void DashboardWidget::updatePowerBests()
{
    const int n = static_cast<int>(m_powerHistory.size());
    auto bestAvg = [&](int window) -> double {
        if (n < window) return 0.0;
        // Incremental: only check the latest window (the history is append-only)
        double sum = 0.0;
        for (int i = n - window; i < n; ++i)
            sum += m_powerHistory[static_cast<size_t>(i)];
        return sum / window;
    };

    double v5   = bestAvg(5);
    double v60  = bestAvg(60);
    double v300 = bestAvg(300);
    double v1200= bestAvg(1200);

    if (v5    > m_bestPower5s)    m_bestPower5s   = v5;
    if (v60   > m_bestPower1min)  m_bestPower1min = v60;
    if (v300  > m_bestPower5min)  m_bestPower5min = v300;
    if (v1200 > m_bestPower20min) m_bestPower20min= v1200;
}

void DashboardWidget::addChartPoint(double powerW, double hrBpm)
{
    m_chartView->appendPower(m_elapsed, powerW);
    m_chartView->appendHr(m_elapsed, hrBpm);

    // Scroll the window
    if (m_elapsed > CHART_WINDOW_S) {
        double lo = m_elapsed - CHART_WINDOW_S;
        double hi = m_elapsed;
        m_chartView->setXRange(lo, hi);

        // Prune old points (keep only last CHART_WINDOW_S+10 seconds)
        m_chartView->removePowerBefore(m_elapsed - CHART_WINDOW_S - 10);
        m_chartView->removeHrBefore(m_elapsed - CHART_WINDOW_S - 10);
    }

    // Autoscale power: scan visible window, shrink back to default when values drop
    {
        double maxP = 0.0;
        for (int i = 0; i < m_chartView->powerCount(); ++i)
            maxP = std::max(maxP, m_chartView->powerAt(i).y());
        m_chartView->setYLeftRange(0, std::max(CHART_DEFAULT_MAX_POWER, maxP * 1.1));
    }

    // Autoscale HR: same principle
    {
        double maxHr = 0.0;
        for (int i = 0; i < m_chartView->hrCount(); ++i)
            maxHr = std::max(maxHr, m_chartView->hrAt(i).y());
        m_chartView->setYRightRange(40, std::max(CHART_DEFAULT_MAX_HR, maxHr * 1.05));
    }
}

// ── slots ─────────────────────────────────────────────────────────────────────

void DashboardWidget::onViewChanged(int index)
{
    ViewType view = static_cast<ViewType>(m_viewSwitcher->itemData(index).toInt());
    m_currentView = view;

    // Always show top data tiles.
    if (m_tileRowWidget) m_tileRowWidget->setVisible(true);

    // Bottom panel toggles between chart and dials.
    if (m_chartView) m_chartView->setVisible(view == ChartView);
    if (m_dialRowWidget) m_dialRowWidget->setVisible(view == DialView);
}

DashboardWidget::WorkoutSummary DashboardWidget::currentWorkoutSummary() const
{
    WorkoutSummary s;
    s.timestamp = QDateTime::currentDateTime();
    s.avgPower = m_avgPower;
    s.avgHr = m_avgHr;
    s.avgCadence = m_avgCadence;
    s.avgSpeed = m_avgSpeed;
    s.maxHr = m_maxHr;
    s.maxPower = m_maxPowerSample;
    s.duration = m_elapsed - m_workoutStartTime;
    s.distance = m_totalDistance;
    s.totalAscent = m_gpxTotalAscent;
    s.normalizedPower = m_normalizedPower;
    s.intensityFactor = m_intensityFactor;
    s.tss = m_tss;
    s.bestPower5s   = m_bestPower5s;
    s.bestPower1min = m_bestPower1min;
    s.bestPower5min = m_bestPower5min;
    s.bestPower20min = m_bestPower20min;
    s.timeInZone = m_timeInZone;
    if (m_trainingMode == MapRideMode && !m_gpxName.isEmpty())
        s.routeName = m_gpxName;

    switch (m_trainingMode) {
    case FreeRideMode:
        s.trainingModeName = QStringLiteral("Free Ride");
        break;
    case IntervalMode:
        s.trainingModeName = QStringLiteral("Interval Training");
        if (m_selectedProgramIdx >= 0 && m_selectedProgramIdx < m_programs.size())
            s.programName = m_programs[m_selectedProgramIdx].name;
        break;
    case MapRideMode:
        s.trainingModeName = QStringLiteral("Map Ride");
        {
            QString rn = m_gpxName;
            rn.replace('_', ' ');
            s.programName = rn;
        }
        break;
    }
    return s;
}

void DashboardWidget::onChartTick()
{
    m_elapsed += CHART_TICK_S;
    addChartPoint(m_lastPower, m_lastHr);

    if (m_workoutActive) {
        WorkoutSample s;
        s.elapsed  = m_elapsed - m_workoutStartTime;
        s.power    = m_lastPower;
        s.hr       = m_lastHr;
        s.cadence  = m_lastCadence;
        s.speed    = m_lastSpeed;
        m_samples.append(s);

        // ── NP / IF / TSS computation ────────────────────────────────
        m_npRolling.push_back(m_lastPower);
        if (static_cast<int>(m_npRolling.size()) > 30)
            m_npRolling.pop_front();

        if (static_cast<int>(m_npRolling.size()) == 30) {
            double avg30 = 0.0;
            for (double v : m_npRolling) avg30 += v;
            avg30 /= 30.0;
            const double p4 = avg30 * avg30 * avg30 * avg30;
            m_npSumFourth += p4;
            m_npCount++;
            m_normalizedPower = std::pow(m_npSumFourth / m_npCount, 0.25);
            m_intensityFactor = (m_ftpWatts > 0)
                ? m_normalizedPower / m_ftpWatts : 0.0;
            const double dur = m_elapsed - m_workoutStartTime;
            m_tss = (m_ftpWatts > 0 && dur > 0.0)
                ? (dur * m_normalizedPower * m_intensityFactor) / (m_ftpWatts * 3600.0) * 100.0
                : 0.0;

            // NP/IF/TSS are computed but only persisted in saved workouts,
            // no live tiles for these metrics.
        }

        // ── Power zone time tracking ─────────────────────────────────
        const int z = powerZone(m_lastPower, m_ftpWatts);
        m_currentZone = z;
        if (z >= 1 && z <= NUM_ZONES)
            m_timeInZone[static_cast<size_t>(z - 1)]++;

        // ── Max power tracking ───────────────────────────────────────
        if (m_lastPower > m_maxPowerSample)
            m_maxPowerSample = m_lastPower;

        // ── Power bests (rolling max averages) ───────────────────────
        m_powerHistory.push_back(m_lastPower);
        updatePowerBests();

        // Map riding: advance position and update grade every tick
        if (m_trainingMode == MapRideMode && m_gpxTrack.size() >= 2) {
            updateMapGrade();
            // Stamp GPS coords computed by updateMapGrade() onto the last sample
            m_samples.last().lat = m_currentLat;
            m_samples.last().lon = m_currentLon;
            m_samples.last().ele = m_currentEle;
        }
    }
}
