#pragma once

#include <QString>
#include <QStringList>

// Per-platform. Both return what failed (empty on success) and may be called from any thread.

// windowsFormat is the Proxy Format setting, e.g. "{ip}:{port}" or "socks={ip}:{port}"; other platforms ignore it.
// extraIgnoreHosts (Linux): merged into GNOME ignore-hosts / KDE NoProxy for this apply (e.g. PPTP gateway).
QString SystemProxy_Apply(const QString &host, int port, const QString &windowsFormat,
                          const QStringList &extraIgnoreHosts = {});

// Undoes SystemProxy_Apply(); does nothing unless this process applied a proxy, so proxies set by other apps survive.
// Restores ignore-hosts/NoProxy to the snapshot taken before Throne's merge.
QString SystemProxy_Clear();
