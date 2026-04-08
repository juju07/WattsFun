#pragma once
#include "dashboardwidget.h"
#include <QByteArray>
#include <QVector>

// Generates a .tcx file (Garmin Training Center XML) from a workout summary
// and per-second sample data.  Power is encoded via the Garmin Activity
// Extension v2 (ns3:Watts) so that Strava displays a power curve.
class TcxExporter
{
public:
    static QByteArray generate(const DashboardWidget::WorkoutSummary &summary,
                               const QVector<DashboardWidget::WorkoutSample> &samples);
};
