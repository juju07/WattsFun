#pragma once
#include "dashboardwidget.h"
#include <QString>
#include <QVector>

// ──────────────────────────────────────────────────────────────────────────────
// TrainingLibrary
//
// Persists interval training programs as XML files inside:
//   <AppData>/WattsFun/trainings/
//
// One XML file per program:
//
//   <training name="…" isErg="true">
//     <step name="Warm-Up" durationSec="300" ergWatts="100" gradePct="0"/>
//     …
//   </training>
// ──────────────────────────────────────────────────────────────────────────────

class TrainingLibrary
{
public:
    // Returns the directory used for storage (created on demand).
    static QString storageDir();

    // Load all programs from storageDir(). Builtin defaults are written
    // the first time (if the directory is empty).
    static QVector<DashboardWidget::TrainingProgram> loadAll();

    // Save a single program. The filename is derived from the program name.
    // Returns true on success.
    static bool save(const DashboardWidget::TrainingProgram &prog);

    // Delete the persisted file for the given program name.
    // Returns true if the file was removed (or did not exist).
    static bool remove(const QString &programName);

    // File path for a given program name.
    static QString filePathFor(const QString &name);

private:
    static void writeDefaults();
    static DashboardWidget::TrainingProgram parseFile(const QString &path);
    static bool writeFile(const QString &path,
                          const DashboardWidget::TrainingProgram &prog);
};
