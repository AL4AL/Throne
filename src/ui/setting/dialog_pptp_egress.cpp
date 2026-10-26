#include "include/ui/setting/dialog_pptp_egress.h"

#include "include/global/Configs.hpp"
#include "include/global/Validation.hpp"
#include "include/ui/utils/GuiUtils.hpp"
#include "include/ui/utils/ValidationForm.h"

#ifdef Q_OS_LINUX
#include "include/sys/linux/PptpEgress.h"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>

DialogPptpEgress::DialogPptpEgress(QWidget *parent) : QDialog(parent), ui(new Ui::DialogPptpEgress) {
    ui->setupUi(this);
    ADD_ASTERISK(this);
    form = new Validation::Form(this);
    form->attachStatus(ui->buttonBox);

    ui->pptp_protocol->addItem(tr("PPTP"), QStringLiteral("pptp"));
    ui->pptp_protocol->addItem(tr("L2TP / IPsec (pre-shared key)"), QStringLiteral("l2tp"));

    const auto &s = Configs::dataManager->settingsRepo;
    const int idx = ui->pptp_protocol->findData(s->egress_protocol.trimmed().toLower() == QLatin1String("l2tp")
                                                    ? QStringLiteral("l2tp")
                                                    : QStringLiteral("pptp"));
    ui->pptp_protocol->setCurrentIndex(idx >= 0 ? idx : 0);
    ui->pptp_gateway->setText(s->pptp_gateway);
    ui->pptp_username->setText(s->pptp_username);
    ui->pptp_password->setText(s->pptp_password);
    ui->pptp_psk->setText(s->egress_ipsec_psk);
    ui->pptp_mtu->setText(QString::number(s->pptp_mtu > 0 ? s->pptp_mtu : 1200));

    connect(ui->pptp_protocol, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { syncProtocolUi(); });
    connect(ui->btn_check_fix, &QPushButton::clicked, this, &DialogPptpEgress::onCheckFix);
    connect(ui->btn_test, &QPushButton::clicked, this, &DialogPptpEgress::onTestTunnel);

    using namespace Validation;
    form->bind(ui->pptp_gateway, Rules::Host()).label(ui->label_gateway->text());
    form->bind(ui->pptp_username, Rules::Required()).label(ui->label_username->text());
    form->bind(ui->pptp_password, Rules::Required()).label(ui->label_password->text());
    form->bind(ui->pptp_mtu, Rules::Int(576, 1500, Empty::Required)).label(ui->label_mtu->text());
    form->bind(ui->pptp_psk, Rules::Required())
        .label(ui->label_psk->text())
        .when([this] { return ui->pptp_protocol->currentData().toString() == QLatin1String("l2tp"); });
    syncProtocolUi();
    form->markAll();
    refreshRuntimeStatus();

#ifndef Q_OS_LINUX
    ui->runtime_status->setText(tr("PPTP / L2TP egress is only available on Linux."));
    ui->credentials_box->setEnabled(false);
    ui->btn_check_fix->setEnabled(false);
    ui->btn_test->setEnabled(false);
#endif
}

DialogPptpEgress::~DialogPptpEgress() {
    delete ui;
}

void DialogPptpEgress::syncProtocolUi() {
    const bool l2tp = ui->pptp_protocol->currentData().toString() == QLatin1String("l2tp");
    ui->label_psk->setEnabled(l2tp);
    ui->pptp_psk->setEnabled(l2tp);
    ui->pptp_psk->setPlaceholderText(l2tp ? tr("IPsec PSK (required)")
                                          : tr("Not used for PPTP — switch to L2TP/IPsec if your provider gave a PSK"));
    form->refresh();
}

void DialogPptpEgress::saveFieldsToSettings(bool persist) {
    const auto mtu = Validation::ToInt(ui->pptp_mtu->text().trimmed());
    if (!mtu) return;

    auto &s = Configs::dataManager->settingsRepo;
    s->egress_protocol = ui->pptp_protocol->currentData().toString();
    s->pptp_gateway = ui->pptp_gateway->text().trimmed();
    s->pptp_username = ui->pptp_username->text().trimmed();
    s->pptp_password = ui->pptp_password->text();
    s->egress_ipsec_psk = ui->pptp_psk->text();
    s->pptp_mtu = int(*mtu);
    if (persist) s->Save();
}

void DialogPptpEgress::refreshRuntimeStatus(const QString &detail) {
#ifdef Q_OS_LINUX
    const auto &s = Configs::dataManager->settingsRepo;
    QString text;
    if (Sys::PptpEgress::IsUp()) {
        const auto iface = Sys::PptpEgress::CurrentIface();
        const auto hosts = Sys::PptpEgress::LastRoutedHosts();
        text = tr("VPN is up (%1). Routed hosts: %2")
                   .arg(iface.isEmpty() ? tr("ppp") : iface, hosts.isEmpty() ? tr("(none yet)") : hosts.join(QStringLiteral(", ")));
    } else if (s->enable_pptp_egress) {
        text = tr("Egress is enabled; the tunnel comes up when a profile starts.");
    } else {
        text = tr("Egress is off. Use the main window checkbox after saving.");
    }
    if (!detail.isEmpty()) text += QStringLiteral("\n\n") + detail;
    ui->runtime_status->setText(text);
#else
    Q_UNUSED(detail);
#endif
}

void DialogPptpEgress::onCheckFix() {
#ifdef Q_OS_LINUX
    if (!form->validateAll()) return;
    saveFieldsToSettings(false);

    auto diag = Sys::PptpEgress::Diagnose();

    if (diag.suggestSwitchToL2tp) {
        if (QMessageBox::question(this, tr("Use L2TP/IPsec?"),
                                  tr("A pre-shared key is set, but PPTP ignores it.\n\nSwitch to L2TP/IPsec?")) ==
            QMessageBox::Yes) {
            const int idx = ui->pptp_protocol->findData(QStringLiteral("l2tp"));
            if (idx >= 0) ui->pptp_protocol->setCurrentIndex(idx);
            saveFieldsToSettings(false);
            diag = Sys::PptpEgress::Diagnose();
        }
    }

    if (diag.ready) {
        QMessageBox::information(this, tr("PPTP / L2TP Egress"), tr("System looks ready.\n\n%1").arg(diag.summary));
        refreshRuntimeStatus(diag.summary);
        return;
    }

    if (!diag.missingPackages.isEmpty() && diag.canInstallPackages) {
        if (QMessageBox::question(this, tr("Install VPN packages?"),
                                  tr("Install these Ubuntu packages?\n\n%1\n\nYou will be asked for your password.")
                                      .arg(diag.missingPackages.join(QLatin1Char('\n')))) != QMessageBox::Yes) {
            refreshRuntimeStatus(diag.summary);
            return;
        }
        ui->runtime_status->setText(tr("Installing packages…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const auto err = Sys::PptpEgress::InstallMissingPackages();
        QApplication::restoreOverrideCursor();
        if (!err.isEmpty()) {
            QMessageBox::warning(this, tr("Package install failed"), err);
            refreshRuntimeStatus(err);
            return;
        }
        diag = Sys::PptpEgress::Diagnose();
        QMessageBox::information(this, tr("Packages installed"), tr("Done.\n\n%1").arg(diag.summary));
        refreshRuntimeStatus(diag.summary);
        return;
    }

    QMessageBox::warning(this, tr("PPTP / L2TP Egress"), diag.summary);
    refreshRuntimeStatus(diag.summary);
#endif
}

void DialogPptpEgress::onTestTunnel() {
#ifdef Q_OS_LINUX
    if (!form->validateAll()) return;
    saveFieldsToSettings(false);

    const auto diag = Sys::PptpEgress::Diagnose();
    if (!diag.missingPackages.isEmpty()) {
        if (QMessageBox::question(this, tr("Packages missing"), tr("%1\n\nRun Check & fix system first?").arg(diag.summary)) ==
            QMessageBox::Yes) {
            onCheckFix();
        } else {
            refreshRuntimeStatus(diag.summary);
        }
        return;
    }

    ui->runtime_status->setText(tr("Bringing VPN up…"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto err = Sys::PptpEgress::EnsureVpnUp();
    QApplication::restoreOverrideCursor();

    if (!err.isEmpty() && ui->pptp_protocol->currentData().toString() != QLatin1String("l2tp") && !ui->pptp_psk->text().isEmpty()) {
        if (QMessageBox::question(this, tr("Try L2TP/IPsec?"),
                                  tr("PPTP failed, but a pre-shared key is present.\n\n%1\n\nSwitch to L2TP/IPsec and retry?")
                                      .arg(err)) == QMessageBox::Yes) {
            const int idx = ui->pptp_protocol->findData(QStringLiteral("l2tp"));
            if (idx >= 0) ui->pptp_protocol->setCurrentIndex(idx);
            saveFieldsToSettings(false);
            QApplication::setOverrideCursor(Qt::WaitCursor);
            err = Sys::PptpEgress::EnsureVpnUp();
            QApplication::restoreOverrideCursor();
        } else {
            refreshRuntimeStatus(err);
            return;
        }
    }

    if (!err.isEmpty()) {
        QMessageBox::warning(this, tr("Tunnel test failed"), err);
        refreshRuntimeStatus(err);
        return;
    }
    QMessageBox::information(this, tr("Tunnel test"), tr("VPN is up on %1.").arg(Sys::PptpEgress::CurrentIface()));
    refreshRuntimeStatus();
#endif
}

void DialogPptpEgress::accept() {
    if (!form->validateAll()) return;
    if (!Validation::ToInt(ui->pptp_mtu->text().trimmed())) return;
    saveFieldsToSettings(true);
    QDialog::accept();
}
