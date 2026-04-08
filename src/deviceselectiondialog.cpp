#include "deviceselectiondialog.h"
#include "ui_deviceselectiondialog.h"
#include "antmanager.h"
#include "blemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QListWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QTimer>
#include <QIcon>

DeviceSelectionDialog::DeviceSelectionDialog(IProtocolManager *primary,
                                             IProtocolManager *ant,
                                             IProtocolManager *ble,
                                             DeviceConfig     &config,
                                             QWidget          *parent)
    : QDialog(parent)
    , ui(new Ui::DeviceSelectionDialog)
    , m_primary(primary)
    , m_ant(ant)
    , m_ble(ble)
    , m_config(config)
{
    ui->setupUi(this);
    ui->verticalLayout->setContentsMargins(QMargins(14, 14, 14, 14));
    setWindowTitle(tr("Select Devices"));
    setMinimumSize(620, 480);

    // ── Wire scan buttons ──────────────────────────────────────────────────
    connect(ui->btnScan, &QPushButton::clicked, this, &DeviceSelectionDialog::onScanClicked);
    connect(ui->btnBox,  &QDialogButtonBox::accepted, this, &DeviceSelectionDialog::onAccepted);
    connect(ui->btnBox,  &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ── Listen to both managers ────────────────────────────────────────────
    for (auto *mgr : { m_ant, m_ble }) {
        if (!mgr) continue;
        connect(mgr, &IProtocolManager::deviceFound,  this, &DeviceSelectionDialog::onDeviceFound);
        connect(mgr, &IProtocolManager::scanFinished, this, &DeviceSelectionDialog::onScanFinished);
    }

    populateSavedDevices();
}

DeviceSelectionDialog::~DeviceSelectionDialog()
{
    if (m_ant) m_ant->stopScan();
    if (m_ble) m_ble->stopScan();
    delete ui;
}

// ── Public getters ────────────────────────────────────────────────────────────

QString DeviceSelectionDialog::selectedTrainerId() const
{
    auto *item = ui->listTrainers->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

QString DeviceSelectionDialog::selectedHrmId() const
{
    auto *item = ui->listHrm->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString{};
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void DeviceSelectionDialog::onScanClicked()
{
    ui->listTrainers->clear();
    ui->listHrm->clear();
    m_trainers.clear();
    m_hrms.clear();
    m_scanningAnt = false;
    m_scanningBle = false;

    ui->btnScan->setEnabled(false);
    ui->progressBar->setRange(0, 0);  // indeterminate
    ui->lblStatus->setText(tr("Scanning…"));

    // Try ANT+ first, then BLE
    if (m_ant && m_ant->isAvailable()) {
        m_scanningAnt = true;
        m_ant->startScan();
    }
    if (m_ble && m_ble->isAvailable()) {
        m_scanningBle = true;
        m_ble->startScan();
    }

    if (!m_scanningAnt && !m_scanningBle) {
        ui->lblStatus->setText(tr("No adapters available."));
        ui->btnScan->setEnabled(true);
        ui->progressBar->setRange(0, 1);
        return;
    }

    // Auto-stop BLE scan after 10 s
    QTimer::singleShot(10000, this, [this]() {
        if (m_ble && m_scanningBle) m_ble->stopScan();
    });
}

void DeviceSelectionDialog::onDeviceFound(const DeviceInfo &info)
{
    // Determine if this is trainer or HRM
    bool isHrm = (info.type == "HRM");

    if (isHrm) {
        if (m_hrms.contains(info.id)) return;
        m_hrms.insert(info.id, info);
        ui->listHrm->addItem(makeItem(info));
    } else {
        if (m_trainers.contains(info.id)) return;
        m_trainers.insert(info.id, info);
        ui->listTrainers->addItem(makeItem(info));
    }

    ui->lblStatus->setText(tr("Found %1 trainer(s), %2 HRM(s)…")
                           .arg(m_trainers.size()).arg(m_hrms.size()));
}

void DeviceSelectionDialog::onScanFinished()
{
    IProtocolManager *sender = qobject_cast<IProtocolManager *>(QObject::sender());
    if (sender == m_ant) m_scanningAnt = false;
    if (sender == m_ble) m_scanningBle = false;

    if (!m_scanningAnt && !m_scanningBle) {
        ui->progressBar->setRange(0, 1);
        ui->progressBar->setValue(1);
        ui->btnScan->setEnabled(true);
        ui->lblStatus->setText(tr("Scan complete – %1 trainer(s), %2 HRM(s) found.")
                               .arg(m_trainers.size()).arg(m_hrms.size()));
    }
}

void DeviceSelectionDialog::onAccepted()
{
    if (ui->chkSave->isChecked()) {
        // Save trainer
        auto *tItem = ui->listTrainers->currentItem();
        if (tItem) {
            QString id = tItem->data(Qt::UserRole).toString();
            if (m_trainers.contains(id)) {
                const DeviceInfo &di = m_trainers[id];
                m_config.saveTrainer({ di.id, di.name, di.type, di.source });
            }
        }
        // Save HRM
        auto *hItem = ui->listHrm->currentItem();
        if (hItem) {
            QString id = hItem->data(Qt::UserRole).toString();
            if (m_hrms.contains(id)) {
                const DeviceInfo &di = m_hrms[id];
                m_config.saveHrm({ di.id, di.name, di.type, di.source });
            }
        }
    }
    accept();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

QListWidgetItem *DeviceSelectionDialog::makeItem(const DeviceInfo &info)
{
    QString proto = (info.source == DataSource::ANT) ? "[ANT+]" : "[BLE]";
    auto *item = new QListWidgetItem(QString("%1  %2  %3").arg(proto, info.name, info.type));
    item->setData(Qt::UserRole, info.id);
    return item;
}

void DeviceSelectionDialog::populateSavedDevices()
{
    SavedDevice trainer = m_config.loadTrainer();
    SavedDevice hrm     = m_config.loadHrm();

    if (trainer.isValid()) {
        DeviceInfo di { trainer.id, trainer.name, trainer.type, trainer.source };
        m_trainers.insert(di.id, di);
        ui->listTrainers->addItem(makeItem(di));
        ui->lblStatus->setText(tr("Saved devices loaded – click Scan to refresh."));
    }
    if (hrm.isValid()) {
        DeviceInfo di { hrm.id, hrm.name, hrm.type, hrm.source };
        m_hrms.insert(di.id, di);
        ui->listHrm->addItem(makeItem(di));
    }
}
