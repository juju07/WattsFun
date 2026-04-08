#include "workouteditordialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QHeaderView>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>

// Column indices in the step table
static constexpr int COL_NAME     = 0;
static constexpr int COL_DURATION = 1; // minutes
static constexpr int COL_TYPE     = 2; // "ERG" / "Grade"
static constexpr int COL_VALUE    = 3; // watts or %

static const QString DARK_STYLE =
    "QDialog { background: #1e1e2e; color: #cdd6f4; }"
    "QLabel  { color: #cdd6f4; background: transparent; border: none; }"
    "QLineEdit {"
    "  background: #181825; border: 1px solid #45475a; border-radius: 6px;"
    "  color: #cdd6f4; padding: 4px 8px; }"
    "QLineEdit:focus { border-color: #89b4fa; }"
    "QTableWidget {"
    "  background: #181825; border: 1px solid #313244; border-radius: 6px;"
    "  color: #cdd6f4; gridline-color: #313244; }"
    "QTableWidget::item { padding: 2px 4px; }"
    "QTableWidget::item:selected { background: #313244; color: #89b4fa; }"
    "QHeaderView::section {"
    "  background: #11111b; color: #6c7086; border: none;"
    "  border-bottom: 1px solid #313244; padding: 4px; font-size: 11px; font-weight: 700; }"
    "QComboBox {"
    "  background: #181825; border: 1px solid #45475a; border-radius: 4px;"
    "  color: #cdd6f4; padding: 2px 6px; }"
    "QComboBox::drop-down { border: none; }"
    "QComboBox QAbstractItemView { background: #181825; color: #cdd6f4; }"
    "QSpinBox, QDoubleSpinBox {"
    "  background: #181825; border: 1px solid #45475a; border-radius: 4px;"
    "  color: #cdd6f4; padding: 2px 4px; }"
    "QSpinBox:focus, QDoubleSpinBox:focus { border-color: #89b4fa; }"
    "QSpinBox::up-button, QDoubleSpinBox::up-button {"
    "  subcontrol-origin: border; subcontrol-position: top right;"
    "  width: 16px; border-left: 1px solid #45475a;"
    "  background: #313244; border-top-right-radius: 4px; }"
    "QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover { background: #45475a; }"
    "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
    "  image: url(:/arrow-up.svg); width: 8px; height: 5px; }"
    "QSpinBox::down-button, QDoubleSpinBox::down-button {"
    "  subcontrol-origin: border; subcontrol-position: bottom right;"
    "  width: 16px; border-left: 1px solid #45475a;"
    "  background: #313244; border-bottom-right-radius: 4px; }"
    "QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover { background: #45475a; }"
    "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
    "  image: url(:/arrow-down.svg); width: 8px; height: 5px; }"
    "QPushButton {"
    "  background: #313244; color: #cdd6f4; border: 1px solid #45475a;"
    "  border-radius: 6px; padding: 5px 14px; font-size: 12px; font-weight: 600; }"
    "QPushButton:hover { background: #45475a; }"
    "QPushButton#saveBtn {"
    "  background: #89b4fa; color: #1e1e2e; border-color: #89b4fa; }"
    "QPushButton#saveBtn:hover { background: #b4d0f5; }"
    "QPushButton#addBtn {"
    "  background: #a6e3a1; color: #1e1e2e; border-color: #a6e3a1; font-size: 14px; font-weight: 800; min-width: 28px; max-width: 28px; padding: 2px; }"
    "QPushButton#addBtn:hover { background: #b8f0b3; }"
    "QPushButton#delBtn {"
    "  background: #f38ba8; color: #1e1e2e; border-color: #f38ba8; font-size: 14px; font-weight: 800; min-width: 28px; max-width: 28px; padding: 2px; }"
    "QPushButton#delBtn:hover { background: #eba0ac; }";

// ──────────────────────────────────────────────────────────────────────────────

WorkoutEditorDialog::WorkoutEditorDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Training Program"));
    setup();
    // Start with one blank step
    DashboardWidget::IntervalStep blank;
    blank.name        = "Step 1";
    blank.durationSec = 5 * 60;
    blank.isErg       = true;
    blank.ergWatts    = 150;
    blank.gradePct    = 0.0;
    addStepRow(blank);
    updateTotalDuration();
}

WorkoutEditorDialog::WorkoutEditorDialog(const DashboardWidget::TrainingProgram &prog,
                                         QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Edit Training Program"));
    setup();
    populateFrom(prog);
}

// ──────────────────────────────────────────────────────────────────────────────

void WorkoutEditorDialog::setup()
{
    setStyleSheet(DARK_STYLE);
    setMinimumSize(560, 420);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setSpacing(10);
    root->setContentsMargins(16, 14, 16, 10);

    // ── Program name ──────────────────────────────────────────────────────
    auto *nameRow = new QHBoxLayout();
    auto *nameLbl = new QLabel(tr("Program name:"), this);
    nameLbl->setFixedWidth(110);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("e.g. Sweet Spot 3x10"));
    nameRow->addWidget(nameLbl);
    nameRow->addWidget(m_nameEdit);
    root->addLayout(nameRow);

    // ── Steps table header row ────────────────────────────────────────────
    auto *tableHeaderRow = new QHBoxLayout();
    auto *tblLbl = new QLabel(tr("Steps:"), this);
    tableHeaderRow->addWidget(tblLbl);
    tableHeaderRow->addStretch();

    auto *btnAdd = new QPushButton("+", this);
    btnAdd->setObjectName("addBtn");
    btnAdd->setToolTip(tr("Add step"));

    auto *btnDel = new QPushButton("\xc3\x97", this); // ×
    btnDel->setObjectName("delBtn");
    btnDel->setToolTip(tr("Delete selected step"));

    tableHeaderRow->addWidget(btnAdd);
    tableHeaderRow->addWidget(btnDel);
    root->addLayout(tableHeaderRow);

    // ── Steps table ───────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({ tr("Step Name"), tr("Duration"), tr("Type"), tr("Value") });
    m_table->horizontalHeader()->setSectionResizeMode(COL_NAME,     QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_DURATION, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TYPE,     QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_VALUE,    QHeaderView::Fixed);
    m_table->setColumnWidth(COL_DURATION, 90);
    m_table->setColumnWidth(COL_TYPE,     72);
    m_table->setColumnWidth(COL_VALUE,    88);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(false);
    root->addWidget(m_table, 1);

    // ── Total duration label ──────────────────────────────────────────────
    m_totalLabel = new QLabel(tr("Total duration: 0:00"), this);
    m_totalLabel->setStyleSheet("color: #a6adc8; font-size: 11px; background: transparent; border: none;");
    root->addWidget(m_totalLabel);

    // ── Dialog buttons ────────────────────────────────────────────────────
    auto *btnBox = new QDialogButtonBox(this);
    auto *saveBtn   = btnBox->addButton(tr("Save"), QDialogButtonBox::AcceptRole);
    auto *cancelBtn = btnBox->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    (void)cancelBtn;
    saveBtn->setObjectName("saveBtn");
    root->addWidget(btnBox);

    // ── Connections ───────────────────────────────────────────────────────
    connect(btnAdd, &QPushButton::clicked, this, &WorkoutEditorDialog::onAddStep);
    connect(btnDel, &QPushButton::clicked, this, &WorkoutEditorDialog::onDeleteStep);
    connect(m_table, &QTableWidget::cellChanged, this, &WorkoutEditorDialog::onCellChanged);
    connect(btnBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("Name required"),
                                 tr("Please enter a program name."));
            return;
        }
        if (m_table->rowCount() == 0) {
            QMessageBox::warning(this, tr("No steps"),
                                 tr("Please add at least one step."));
            return;
        }
        accept();
    });
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void WorkoutEditorDialog::populateFrom(const DashboardWidget::TrainingProgram &prog)
{
    m_loading = true;
    m_nameEdit->setText(prog.name);
    m_table->setRowCount(0);
    for (const auto &s : prog.steps)
        addStepRow(s);
    m_loading = false;
    updateTotalDuration();
}

// ──────────────────────────────────────────────────────────────────────────────

void WorkoutEditorDialog::addStepRow(const DashboardWidget::IntervalStep &step)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);

    // Col 0 – step name (plain text item, editable)
    m_table->setItem(row, COL_NAME, new QTableWidgetItem(step.name));

    // Col 1 – duration in minutes (QSpinBox: 1..999 min, displayed as mm:ss)
    // We store total seconds internally but show as QSpinBox in minutes for usability.
    // Format shown: minutes only (simple). For sub-minute control users set seconds.
    auto *durSpin = new QSpinBox();
    durSpin->setRange(1, 999);
    durSpin->setSuffix(" min");
    durSpin->setFrame(false);
    // Convert seconds → minutes (round up to at least 1)
    durSpin->setValue(qMax(1, (step.durationSec + 59) / 60));
    durSpin->setStyleSheet(
        "QSpinBox { background: #181825; border: none; color: #cdd6f4; padding: 2px 4px; }");
    connect(durSpin, &QSpinBox::valueChanged, this, &WorkoutEditorDialog::updateTotalDuration);
    m_table->setCellWidget(row, COL_DURATION, durSpin);

    // Col 2 – type: ERG / Grade combo
    auto *typeCombo = new QComboBox();
    typeCombo->addItem("ERG");
    typeCombo->addItem("Grade");
    typeCombo->setCurrentIndex(step.isErg ? 0 : 1);
    typeCombo->setFrame(false);
    typeCombo->setStyleSheet(
        "QComboBox { background: #181825; border: none; color: #cdd6f4; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #181825; color: #cdd6f4; }");
    m_table->setCellWidget(row, COL_TYPE, typeCombo);

    // Col 3 – value (QDoubleSpinBox: watts 0-2000 or grade -25..25)
    auto *valSpin = new QDoubleSpinBox();
    valSpin->setFrame(false);
    valSpin->setStyleSheet(
        "QDoubleSpinBox { background: #181825; border: none; color: #f9e2af; padding: 2px 4px; }");

    auto updateValueRange = [valSpin](int typeIdx) {
        if (typeIdx == 0) { // ERG
            valSpin->setRange(0, 2000);
            valSpin->setDecimals(0);
            valSpin->setSuffix(" W");
        } else {            // Grade
            valSpin->setRange(-25.0, 25.0);
            valSpin->setDecimals(1);
            valSpin->setSuffix(" %");
        }
    };
    updateValueRange(typeCombo->currentIndex());
    valSpin->setValue(step.isErg ? step.ergWatts : step.gradePct);

    connect(typeCombo, &QComboBox::currentIndexChanged, this, [valSpin, updateValueRange](int idx) {
        updateValueRange(idx);
    });

    m_table->setCellWidget(row, COL_VALUE, valSpin);
    m_table->setRowHeight(row, 30);
}

void WorkoutEditorDialog::onAddStep()
{
    DashboardWidget::IntervalStep blank;
    blank.name        = tr("Step %1").arg(m_table->rowCount() + 1);
    blank.durationSec = 5 * 60;
    blank.isErg       = true;
    blank.ergWatts    = 150;
    blank.gradePct    = 0.0;
    addStepRow(blank);
    updateTotalDuration();
    // Scroll to and select new row
    m_table->scrollToBottom();
    m_table->selectRow(m_table->rowCount() - 1);
}

void WorkoutEditorDialog::onDeleteStep()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount()) return;
    m_table->removeRow(row);
    updateTotalDuration();
}

void WorkoutEditorDialog::onCellChanged(int /*row*/, int /*col*/)
{
    if (!m_loading)
        updateTotalDuration();
}

void WorkoutEditorDialog::updateTotalDuration()
{
    int totalSec = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *spin = qobject_cast<QSpinBox *>(m_table->cellWidget(r, COL_DURATION));
        if (spin) totalSec += spin->value() * 60;
    }
    const int h = totalSec / 3600;
    const int m = (totalSec % 3600) / 60;
    QString txt;
    if (h > 0)
        txt = tr("Total duration: %1h %2min").arg(h).arg(m);
    else
        txt = tr("Total duration: %1 min").arg(m);
    m_totalLabel->setText(txt);
}

// ──────────────────────────────────────────────────────────────────────────────

DashboardWidget::TrainingProgram WorkoutEditorDialog::program() const
{
    DashboardWidget::TrainingProgram prog;
    prog.name = m_nameEdit->text().trimmed();

    for (int r = 0; r < m_table->rowCount(); ++r) {
        DashboardWidget::IntervalStep s;

        auto *nameItem = m_table->item(r, COL_NAME);
        s.name = nameItem ? nameItem->text().trimmed() : tr("Step %1").arg(r + 1);

        auto *durSpin = qobject_cast<QSpinBox *>(m_table->cellWidget(r, COL_DURATION));
        s.durationSec = durSpin ? durSpin->value() * 60 : 60;

        auto *typeCmb = qobject_cast<QComboBox *>(m_table->cellWidget(r, COL_TYPE));
        s.isErg = typeCmb ? (typeCmb->currentIndex() == 0) : true;

        auto *valSpin = qobject_cast<QDoubleSpinBox *>(m_table->cellWidget(r, COL_VALUE));
        const double val = valSpin ? valSpin->value() : 0.0;
        s.ergWatts  = s.isErg  ? static_cast<int>(val) : 0;
        s.gradePct  = !s.isErg ? val : 0.0;

        prog.steps.append(s);
    }

    // Determine isErg from the majority of steps (or first step)
    int ergCount = 0;
    for (const auto &s : prog.steps) if (s.isErg) ++ergCount;
    prog.isErg = (ergCount >= prog.steps.size() / 2 + 1) || prog.steps.isEmpty();

    return prog;
}
