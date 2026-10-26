#include "include/ui/setting/dialog_pptp_egress.h"

#include "include/global/Configs.hpp"
#include "include/global/Validation.hpp"
#include "include/ui/utils/GuiUtils.hpp"
#include "include/ui/utils/ValidationForm.h"

#ifdef Q_OS_LINUX
#include "include/sys/linux/PptpEgress.h"
#endif

#include <QCheckBox>
#include <QCheckBox>
#include <QMessageBox>

DialogPptpEgress::DialogPptpEgress(QWidget *parent) : QDialog(parent), ui(new Ui::DialogPptpEgress) {
    ui->setupUi(this);
    ADD_ASTERISK(this);
    form = new Validation::Form(this);
    form->attachStatus(ui->buttonBox);

    const auto &s = Configs::dataManager->settingsRepo;
    ui->pptp_gateway->setText(s->pptp_gateway);
    ui->pptp_username->setText(s->pptp_username);
    ui->pptp_password->setText(s->pptp_password);
    ui->pptp_mtu->setText(QString::number(s->pptp_mtu > 0 ? s->pptp_mtu : 1200));

#ifdef Q_OS_LINUX
    if (Sys::PptpEgress::IsUp()) {
        const auto iface = Sys::PptpEgress::CurrentIface();
        const auto hosts = Sys::PptpEgress::LastRoutedHosts();
        ui->runtime_status->setText(
            tr("VPN is up (%1). Routed hosts: %2")
                .arg(iface.isEmpty() ? tr("ppp") : iface, hosts.isEmpty() ? tr("(none yet)") : hosts.join(QStringLiteral(", "))));
    } else if (s->enable_pptp_egress) {
        ui->runtime_status->setText(tr("PPTP egress is enabled; the tunnel comes up when a profile starts."));
    } else {
        ui->runtime_status->setText(tr("PPTP egress is off. Use the main window checkbox after saving."));
    }
#else
    ui->runtime_status->setText(tr("PPTP egress is only available on Linux."));
    ui->credentials_box->setEnabled(false);
#endif

    using namespace Validation;
    form->bind(ui->pptp_gateway, Rules::Host()).label(ui->label_gateway->text());
    form->bind(ui->pptp_username, Rules::Required()).label(ui->label_username->text());
    form->bind(ui->pptp_password, Rules::Required()).label(ui->label_password->text());
    form->bind(ui->pptp_mtu, Rules::Int(576, 1500, Empty::Required)).label(ui->label_mtu->text());
    form->markAll();
}

DialogPptpEgress::~DialogPptpEgress() {
    delete ui;
}

void DialogPptpEgress::accept() {
    if (!form->validateAll()) return;

    const auto mtu = Validation::ToInt(ui->pptp_mtu->text().trimmed());
    if (!mtu) return;

    auto &s = Configs::dataManager->settingsRepo;
    s->pptp_gateway = ui->pptp_gateway->text().trimmed();
    s->pptp_username = ui->pptp_username->text().trimmed();
    s->pptp_password = ui->pptp_password->text();
    s->pptp_mtu = int(*mtu);
    s->Save();

    QDialog::accept();
}
