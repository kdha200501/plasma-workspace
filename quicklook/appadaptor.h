/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QString>

#include "quicklookwindow.h"

/**
 * D-Bus adaptor for the org.kde.quicklook.App interface.
 *
 * Like the class qdbusxml2cpp generates for that XML, but the slots take one
 * extra QDBusMessage parameter: QtDBus fills it with the incoming message,
 * whose sender (a unique D-Bus connection name) can be forwarded to
 * QuickLookWindow. That identity is what lets the (shared, system-wide)
 * preview window arbitrate ownership across the many separate processes that
 * can call it. QDBusContext does not work here: the dispatcher only
 * populates it on the object registered on the bus (QuickLookWindow), never
 * on this adaptor.
 */
class AppAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.quicklook.App")
    Q_CLASSINFO("D-Bus Introspection", ""
"  <interface name=\"org.kde.quicklook.App\">\n"
"    <method name=\"previewUrls\">\n"
"      <arg direction=\"in\" type=\"as\" name=\"urls\"/>\n"
"      <arg direction=\"in\" type=\"b\" name=\"explicitTrigger\"/>\n"
"    </method>\n"
"    <method name=\"close\"/>\n"
"    <method name=\"owner\">\n"
"      <arg direction=\"out\" type=\"s\" name=\"sender\"/>\n"
"    </method>\n"
"    <signal name=\"closed\"/>\n"
"  </interface>\n"
        "")

public:
    explicit AppAdaptor(QuickLookWindow *parent);
    ~AppAdaptor() override;

    inline QuickLookWindow *parent() const
    {
        return static_cast<QuickLookWindow *>(QObject::parent());
    }

Q_SIGNALS:
    void closed();

public Q_SLOTS:
    void close(const QDBusMessage &message);
    /**
     * Returns the D-Bus unique name of the caller that owns the preview (the
     * only sender whose non-explicit live-updates and close calls the service
     * honors). Empty string while the preview is closed, or when it was opened
     * locally / same-process. Lets a client detect that the open preview is owned
     * by someone else - the precondition for taking it over (see requestPreview()
     * on the client side).
     */
    QString owner(const QDBusMessage &message);
    void previewUrls(const QStringList &urls, bool explicitTrigger, const QDBusMessage &message);

private:
    static QString dbusSender(const QDBusMessage &message);
};
