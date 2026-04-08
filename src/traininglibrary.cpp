#include "traininglibrary.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>

// ──────────────────────────────────────────────────────────────────────────────

static QString sanitizeFilename(const QString &name)
{
    QString s = name.trimmed();
    for (QChar &c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            c = '_';
    }
    return s.isEmpty() ? "unnamed" : s;
}

// ──────────────────────────────────────────────────────────────────────────────

QString TrainingLibrary::storageDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString dir  = base + "/WattsFun/trainings";
    QDir().mkpath(dir);
    return dir;
}

QString TrainingLibrary::filePathFor(const QString &name)
{
    return storageDir() + "/" + sanitizeFilename(name) + ".xml";
}

// ──────────────────────────────────────────────────────────────────────────────

DashboardWidget::TrainingProgram TrainingLibrary::parseFile(const QString &path)
{
    DashboardWidget::TrainingProgram prog;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return prog;

    QXmlStreamReader xml(&f);
    while (!xml.atEnd() && !xml.hasError()) {
        const auto tok = xml.readNext();
        if (tok != QXmlStreamReader::StartElement) continue;

        if (xml.name() == QLatin1String("training")) {
            prog.name  = xml.attributes().value("name").toString();
            prog.isErg = (xml.attributes().value("isErg").toString() == "true");
        } else if (xml.name() == QLatin1String("step")) {
            DashboardWidget::IntervalStep s;
            s.name        = xml.attributes().value("name").toString();
            s.durationSec = xml.attributes().value("durationSec").toInt();
            s.isErg       = (xml.attributes().value("isErg").toString() == "true");
            s.ergWatts    = xml.attributes().value("ergWatts").toInt();
            s.gradePct    = xml.attributes().value("gradePct").toDouble();
            prog.steps.append(s);
        }
    }
    return prog;
}

bool TrainingLibrary::writeFile(const QString &path,
                                const DashboardWidget::TrainingProgram &prog)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[TrainingLibrary] Cannot write" << path;
        return false;
    }

    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("training");
    xml.writeAttribute("name",  prog.name);
    xml.writeAttribute("isErg", prog.isErg ? "true" : "false");

    for (const auto &s : prog.steps) {
        xml.writeStartElement("step");
        xml.writeAttribute("name",        s.name);
        xml.writeAttribute("durationSec", QString::number(s.durationSec));
        xml.writeAttribute("isErg",       s.isErg ? "true" : "false");
        xml.writeAttribute("ergWatts",    QString::number(s.ergWatts));
        xml.writeAttribute("gradePct",    QString::number(s.gradePct, 'f', 2));
        xml.writeEndElement();
    }

    xml.writeEndElement(); // training
    xml.writeEndDocument();
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────

void TrainingLibrary::writeDefaults()
{
    // Default 1: ERG Power Intervals
    {
        DashboardWidget::TrainingProgram p;
        p.name  = "ERG Power Intervals";
        p.isErg = true;
        p.steps = {
            { "Warm-Up",       5 * 60, true, 100, 0.0 },
            { "Hard Interval", 3 * 60, true, 250, 0.0 },
            { "Recovery",      2 * 60, true, 130, 0.0 },
            { "Hard Interval", 3 * 60, true, 280, 0.0 },
            { "Cool-Down",     5 * 60, true, 100, 0.0 },
        };
        writeFile(filePathFor(p.name), p);
    }

    // Default 2: Hill Climb Intervals
    {
        DashboardWidget::TrainingProgram p;
        p.name  = "Hill Climb Intervals";
        p.isErg = false;
        p.steps = {
            { "Flat Approach",  3 * 60, false, 0, 0.0  },
            { "Moderate Climb", 4 * 60, false, 0, 4.0  },
            { "Short Descent",  2 * 60, false, 0, -2.0 },
            { "Steep Climb",    4 * 60, false, 0, 7.0  },
            { "Flat Recovery",  3 * 60, false, 0, 0.0  },
        };
        writeFile(filePathFor(p.name), p);
    }
}

QVector<DashboardWidget::TrainingProgram> TrainingLibrary::loadAll()
{
    QDir dir(storageDir());
    const QStringList xmlFiles = dir.entryList({ "*.xml" }, QDir::Files, QDir::Name);

    if (xmlFiles.isEmpty())
        writeDefaults();

    QVector<DashboardWidget::TrainingProgram> result;
    const QStringList reloaded = dir.entryList({ "*.xml" }, QDir::Files, QDir::Name);
    for (const QString &fn : reloaded) {
        auto prog = parseFile(dir.filePath(fn));
        if (!prog.name.isEmpty())
            result.append(prog);
    }
    return result;
}

bool TrainingLibrary::save(const DashboardWidget::TrainingProgram &prog)
{
    return writeFile(filePathFor(prog.name), prog);
}

bool TrainingLibrary::remove(const QString &programName)
{
    const QString path = filePathFor(programName);
    if (!QFile::exists(path)) return true;
    return QFile::remove(path);
}
