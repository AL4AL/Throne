#pragma once

#include <QDialog>

#include "include/engine/EditLocks.h"
#include "ui_dialog_pptp_egress.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class DialogPptpEgress;
}
QT_END_NAMESPACE

namespace Validation {
    class Form;
}

class DialogPptpEgress : public QDialog {
    Q_OBJECT

public:
    explicit DialogPptpEgress(QWidget *parent = nullptr);
    ~DialogPptpEgress() override;

public slots:
    void accept() override;

private:
    void syncProtocolUi();
    void saveFieldsToSettings(bool persist);
    void refreshRuntimeStatus(const QString &detail = {});
    void onCheckFix();
    void onTestTunnel();

    Ui::DialogPptpEgress *ui;
    Validation::Form *form = nullptr;
    Engine::EditLock settingsLock{Engine::EditTarget::Settings};
};
