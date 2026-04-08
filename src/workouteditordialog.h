#pragma once
#include "dashboardwidget.h"
#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>

// ──────────────────────────────────────────────────────────────────────────────
// WorkoutEditorDialog
//
// Allows the user to create a new training program or edit an existing one.
//
// Layout (vertical):
//   ┌─ Name: [______________]
//   │
//   ├─ Steps table  (Name | Duration | Type | Value)     [+] add row
//   │                                                    [×] delete selected
//   │
//   ├─ Total duration label (auto-updated)
//   │
//   └─ [Cancel]  [Save]
// ──────────────────────────────────────────────────────────────────────────────

class WorkoutEditorDialog : public QDialog
{
    Q_OBJECT
public:
    // Construct for creating a new program.
    explicit WorkoutEditorDialog(QWidget *parent = nullptr);

    // Construct for editing an existing program.
    explicit WorkoutEditorDialog(const DashboardWidget::TrainingProgram &prog,
                                 QWidget *parent = nullptr);

    // Returns the program as the user left it (only valid after Accepted).
    DashboardWidget::TrainingProgram program() const;

private slots:
    void onAddStep();
    void onDeleteStep();
    void onCellChanged(int row, int col);
    void updateTotalDuration();

private:
    void setup();
    void populateFrom(const DashboardWidget::TrainingProgram &prog);
    void addStepRow(const DashboardWidget::IntervalStep &step = {});

    QLineEdit    *m_nameEdit   = nullptr;
    QTableWidget *m_table      = nullptr;
    QLabel       *m_totalLabel = nullptr;

    bool m_loading = false; // suppress signals while filling the table
};
