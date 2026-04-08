#pragma once
#include <QDialog>
#include <QListWidget>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMap>

#include "iprotocolmanager.h"
#include "deviceconfig.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DeviceSelectionDialog; }
QT_END_NAMESPACE

// ──────────────────────────────────────────────────────────────────────────────
// Dialog that lets the user:
//   1. Scan for devices via ANT+ and/or BLE
//   2. See discovered trainer modules (resistance / cadence / power) and HRM
//   3. Select which devices to use
//   4. Optionally save the selection
// ──────────────────────────────────────────────────────────────────────────────

class DeviceSelectionDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DeviceSelectionDialog(IProtocolManager *primary,
                                   IProtocolManager *ant,
                                   IProtocolManager *ble,
                                   DeviceConfig     &config,
                                   QWidget          *parent = nullptr);
    ~DeviceSelectionDialog() override;

    QString selectedTrainerId() const;
    QString selectedHrmId()     const;

private slots:
    void onScanClicked();
    void onDeviceFound(const DeviceInfo &info);
    void onScanFinished();
    void onAccepted();

private:
    void buildUi();
    void populateSavedDevices();
    QListWidgetItem *makeItem(const DeviceInfo &info);

    Ui::DeviceSelectionDialog *ui = nullptr;

    IProtocolManager *m_primary = nullptr;
    IProtocolManager *m_ant     = nullptr;
    IProtocolManager *m_ble     = nullptr;
    DeviceConfig     &m_config;

    // id → info for discovered devices
    QMap<QString, DeviceInfo> m_trainers;
    QMap<QString, DeviceInfo> m_hrms;

    bool m_scanningAnt = false;
    bool m_scanningBle = false;
};
