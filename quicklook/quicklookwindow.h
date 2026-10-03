/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <PlasmaQuick/PlasmaWindow>
#include <PlasmaQuick/SharedQmlEngine>

#include <QPointer>
#include <QQuickItem>

class X11WindowCenteredPositioner;
class PreviewContent;
class QDBusServiceWatcher;
class QKeyEvent;
class QResizeEvent;

class QuickLookWindow : public PlasmaQuick::PlasmaWindow
{
    Q_OBJECT
    QML_UNCREATABLE("")
    Q_CLASSINFO("D-Bus Interface", "org.kde.quicklook.App")

public:
    explicit QuickLookWindow(PlasmaQuick::SharedQmlEngine *engine, QWindow *parent = nullptr);
    ~QuickLookWindow() override;

    void positionOnScreen();

Q_SIGNALS:
    void urlsChanged();
    void closed();

public Q_SLOTS:
    /**
     * Shows (or replaces the content of) the Quick Look preview for \a urls.
     * The second call while visible updates the preview in place.
     *
     * The window is shared system-wide (many separate processes can call it),
     * so the caller's identity arbitrates who may live-update the preview:
     * \a dbusSender is the caller's unique D-Bus connection name, empty for
     * local / same-process calls (which are always accepted). \a explicitTrigger
     * marks a deliberate user trigger (e.g. a Space key press): such a call
     * always takes over the preview, even from another sender. Only
     * non-explicit live-updates (explicitTrigger=false) from a sender other
     * than the owner are ignored - an explicit trigger from the new caller is
     * what switches the preview to it.
     */
    Q_SCRIPTABLE void previewUrls(const QStringList &urls, bool explicitTrigger = false, const QString &dbusSender = QString());

    /**
     * Hides the window (the process stays resident, D-Bus-activated like KRunner).
     */
    Q_SCRIPTABLE void close();

    /**
     * D-Bus unique name of the caller that last opened the preview, i.e. the
     * current owner (the only sender whose non-explicit live-updates and close
     * calls the service honors); the empty string while the preview is closed
     * or was opened locally / same-process. Reads of this by D-Bus clients go
     * through AppAdaptor's owner() method.
     */
    QString previewOwnerSender() const
    {
        return m_lastFreshSender;
    }

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

protected Q_SLOTS:
    void objectIncubated();

private:
    void updateSize();
    void updateWidgetGeometry();
    void updateContentCap();
    void updateContentPadding();
    void watchOwnerConnection(const QString &sender);
    QSize desiredWindowSize() const;
    QSize maxContentSize() const;

    PlasmaQuick::SharedQmlEngine *m_engine;
    QPointer<QQuickItem> m_backgroundItem;
    QPointer<QQuickItem> m_contentWidgetItem;
    PreviewContent *m_contentWidget = nullptr;
    X11WindowCenteredPositioner *m_x11Positioner = nullptr;
    QDBusServiceWatcher *m_ownerWatcher = nullptr;
    bool m_hasContent = false;
    // True while the window is actually shown; tracks the shown->hidden
    // transition for the closed() signal (see hideEvent).
    bool m_wasShown = false;
    // D-Bus unique name of the caller whose call last opened/toggled the
    // preview (or the empty string for a local / same-process call). See
    // previewUrls(urls, explicitTrigger, dbusSender).
    QString m_lastFreshSender;
};
