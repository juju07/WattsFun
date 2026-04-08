#include "tcxexporter.h"
#include <QTextStream>

// Build the TCX with plain string concatenation so the output is byte-for-byte
// identical to what Garmin devices emit (no unexpected namespace prefixes).
// Strava, TrainingPeaks, and Garmin Connect all accept this format.

static QString esc(const QString &s)
{
    QString o = s;
    o.replace('&',  "&amp;");
    o.replace('<',  "&lt;");
    o.replace('>',  "&gt;");
    o.replace('"',  "&quot;");
    return o;
}

QByteArray TcxExporter::generate(const DashboardWidget::WorkoutSummary &summary,
                                  const QVector<DashboardWidget::WorkoutSample> &samples)
{
    QString x;
    QTextStream ts(&x);

    const QString startTime = summary.timestamp.toUTC().toString(Qt::ISODate);

    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<TrainingCenterDatabase"
       << " xmlns=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2\""
       << " xmlns:ns3=\"http://www.garmin.com/xmlschemas/ActivityExtension/v2\""
       << " xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
       << " xsi:schemaLocation=\"http://www.garmin.com/xmlschemas/TrainingCenterDatabase/v2"
       << " http://www.garmin.com/xmlschemas/TrainingCenterDatabasev2.xsd\">\n";

    ts << "  <Activities>\n";
    ts << "    <Activity Sport=\"Biking\">\n";
    ts << "      <Id>" << esc(startTime) << "</Id>\n";
    if (!summary.routeName.isEmpty())
        ts << "      <Notes>" << esc(summary.routeName) << "</Notes>\n";

    // ── Lap ────────────────────────────────────────────────────────────────
    ts << "      <Lap StartTime=\"" << esc(startTime) << "\">\n";
    ts << "        <TotalTimeSeconds>" << QString::number(summary.duration, 'f', 0) << "</TotalTimeSeconds>\n";
    ts << "        <DistanceMeters>" << QString::number(summary.distance * 1000.0, 'f', 1) << "</DistanceMeters>\n";
    ts << "        <Calories>0</Calories>\n";

    ts << "        <AverageHeartRateBpm>\n";
    ts << "          <Value>" << static_cast<int>(summary.avgHr) << "</Value>\n";
    ts << "        </AverageHeartRateBpm>\n";

    ts << "        <MaximumHeartRateBpm>\n";
    ts << "          <Value>" << static_cast<int>(summary.maxHr) << "</Value>\n";
    ts << "        </MaximumHeartRateBpm>\n";

    ts << "        <Intensity>Active</Intensity>\n";
    ts << "        <Cadence>" << static_cast<int>(summary.avgCadence) << "</Cadence>\n";
    ts << "        <TriggerMethod>Manual</TriggerMethod>\n";

    ts << "        <Extensions>\n";
    ts << "          <ns3:LX>\n";
    ts << "            <ns3:AvgWatts>" << static_cast<int>(summary.avgPower) << "</ns3:AvgWatts>\n";
    ts << "          </ns3:LX>\n";
    ts << "        </Extensions>\n";

    // ── Track ──────────────────────────────────────────────────────────────
    ts << "        <Track>\n";

    const QDateTime lapStart = summary.timestamp.toUTC();
    double distanceM = 0.0;

    for (const auto &s : samples) {
        const QDateTime tp = lapStart.addMSecs(static_cast<qint64>(s.elapsed * 1000.0));
        distanceM += (s.speed / 3.6) * 1.0;

        ts << "          <Trackpoint>\n";
        ts << "            <Time>" << esc(tp.toString(Qt::ISODate)) << "</Time>\n";

        // GPS position — present only for map-ride recordings
        if (s.lat != 0.0 || s.lon != 0.0) {
            ts << "            <Position>\n";
            ts << "              <LatitudeDegrees>"  << QString::number(s.lat, 'f', 8) << "</LatitudeDegrees>\n";
            ts << "              <LongitudeDegrees>" << QString::number(s.lon, 'f', 8) << "</LongitudeDegrees>\n";
            ts << "            </Position>\n";
        }
        if (s.ele != 0.0)
            ts << "            <AltitudeMeters>" << QString::number(s.ele, 'f', 1) << "</AltitudeMeters>\n";

        ts << "            <DistanceMeters>" << QString::number(distanceM, 'f', 1) << "</DistanceMeters>\n";

        if (s.hr > 0) {
            ts << "            <HeartRateBpm>\n";
            ts << "              <Value>" << static_cast<int>(s.hr) << "</Value>\n";
            ts << "            </HeartRateBpm>\n";
        }

        if (s.cadence > 0)
            ts << "            <Cadence>" << static_cast<int>(s.cadence) << "</Cadence>\n";

        if (s.power > 0) {
            ts << "            <Extensions>\n";
            ts << "              <ns3:TPX>\n";
            ts << "                <ns3:Watts>" << static_cast<int>(s.power) << "</ns3:Watts>\n";
            ts << "              </ns3:TPX>\n";
            ts << "            </Extensions>\n";
        }

        ts << "          </Trackpoint>\n";
    }

    ts << "        </Track>\n";
    ts << "      </Lap>\n";

    // ── Creator ────────────────────────────────────────────────────────────
    ts << "      <Creator xsi:type=\"Device_t\">\n";
    ts << "        <Name>WattsFun</Name>\n";
    ts << "        <UnitId>0</UnitId>\n";
    ts << "        <ProductID>0</ProductID>\n";
    ts << "        <Version>\n";
    ts << "          <VersionMajor>1</VersionMajor>\n";
    ts << "          <VersionMinor>0</VersionMinor>\n";
    ts << "          <BuildMajor>0</BuildMajor>\n";
    ts << "          <BuildMinor>0</BuildMinor>\n";
    ts << "        </Version>\n";
    ts << "      </Creator>\n";

    ts << "    </Activity>\n";
    ts << "  </Activities>\n";

    // ── Author ─────────────────────────────────────────────────────────────
    ts << "  <Author xsi:type=\"Application_t\">\n";
    ts << "    <Name>WattsFun</Name>\n";
    ts << "    <Build>\n";
    ts << "      <Version>\n";
    ts << "        <VersionMajor>1</VersionMajor>\n";
    ts << "        <VersionMinor>0</VersionMinor>\n";
    ts << "        <BuildMajor>0</BuildMajor>\n";
    ts << "        <BuildMinor>0</BuildMinor>\n";
    ts << "      </Version>\n";
    ts << "    </Build>\n";
    ts << "    <LangID>en</LangID>\n";
    ts << "    <PartNumber>000-00000-00</PartNumber>\n";
    ts << "  </Author>\n";

    ts << "</TrainingCenterDatabase>\n";

    ts.flush();
    return x.toUtf8();
}
