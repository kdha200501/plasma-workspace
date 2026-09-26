/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "appadaptor.h"

#include "quicklookwindow.h"

#include <QDBusMessage>

AppAdaptor::AppAdaptor(QuickLookWindow *parent)
    : QDBusAbstractAdaptor(parent)
{
    setAutoRelaySignals(true);
    // The window is registered with ExportAdaptors (the default), so its own
    // (non-scriptable) closed() signal is not relayed to D-Bus. The clients
    // (the folder views) watch for the closed signal on this adaptor's
    // interface, so forward the window's signal to the adaptor's, which is
    // the auto-relayed one.
    connect(parent, &QuickLookWindow::closed, this, &AppAdaptor::closed);
}

AppAdaptor::~AppAdaptor() = default;

QString AppAdaptor::dbusSender(const QDBusMessage &message)
{
    // For an incoming method call, service() is the D-Bus name of the sender
    // - a unique connection name, and thus a stable per-process identity (what
    // distinguishes two Dolphin processes from each other). It is empty for
    // calls made on this process's own connection (e.g.
    // `plasma-quicklook <url>`), which the window treats as authoritative.
    return message.service();
}

void AppAdaptor::previewUrls(const QStringList &urls, bool explicitTrigger, const QDBusMessage &message)
{
    parent()->previewUrls(urls, explicitTrigger, dbusSender(message));
}

void AppAdaptor::close(const QDBusMessage &message)
{
    // The window is shared system-wide, so a stale client must not be able to
    // hide a preview it does not own: its grace timer on a transient empty
    // selection would otherwise kill a preview just handed over to a newly
    // focused window, in the split second before the old owner drops the flag.
    // A preview opened locally (no owner registered) is not tied to a caller,
    // so any caller may close it.
    const QString sender = dbusSender(message);
    const QString owner = parent()->previewOwnerSender();
    if (!owner.isEmpty() && sender != owner) {
        return;
    }
    parent()->close();
}

QString AppAdaptor::owner(const QDBusMessage &)
{
    return parent()->previewOwnerSender();
}

#include "moc_appadaptor.cpp"
