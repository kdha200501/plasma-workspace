/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <PlasmaQuick/PlasmaWindow>
#include <PlasmaQuick/SharedQmlEngine>

#include <QPointer>
#include <QQuickItem>
#include <QScreen>
#include <QSize>
#include <QTimer>

class X11WindowCenteredPositioner;
class PreviewContent;
class QDBusServiceWatcher;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QWindow;

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
     * what switches the preview to it. \a anchorScreenName is the
     * QScreen::name() (the wl_output name, a stable per-monitor identifier in
     * every process - unlike screen indexes, which are per-process and not
     * comparable across D-Bus callers) of the monitor the item is on; an empty
     * name (or one no screen matches) means "no preference": the window keeps
     * its previously anchored monitor (or the primary one on first show).
     */
    Q_SCRIPTABLE void previewUrls(const QStringList &urls, bool explicitTrigger = false, const QString &dbusSender = QString(), const QString &anchorScreenName = QString());

    /**
     * Monitor the window is anchored to (see previewUrls()); null until the
     * first caller provides a name that matches a screen, after which the
     * window always shows on that monitor (a QPointer, so it clears itself
     * when the monitor is disconnected).
     */
    QScreen *anchorScreen() const
    {
        return m_anchorScreen;
    }

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
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    QScreen *anchorScreenObject() const;
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

    // --- user resize (grip drag on the frame) -------------------------
    // The window is frameless with no compositor-side resize
    // (layer-shell on Wayland, plain top-level on X11), so the frame
    // margins act as resize grips and the drag is handled here
    // client-side (see the QWindow mouse event filter).
    Qt::Edges m_userResizeEdges = {};
    QPoint m_userResizeStartPos;
    QRect m_userResizeStartGeometry;
    QSize m_userResizeSize;
    bool m_userResized = false;
    // Fallback that ends a resize drag whose pointer release was delivered
    // to another window (see userDragEndedByOtherInput).
    QTimer m_userDragFallbackTimer;

    void applyUserResizedSize(const QSize &newSize, const QRect &requestedGeometry);
    void endUserDrag();
    void userDragEndedByOtherInput();
    void setUserResized(bool userResized);
    void updateMinMaxSize();

    Qt::Edges resizeEdgesFor(const QPoint &globalPos) const;
    // D-Bus unique name of the caller whose call last opened/toggled the
    // preview (or the empty string for a local / same-process call). See
    // previewUrls(urls, explicitTrigger, dbusSender, anchorScreenName).
    QString m_lastFreshSender;
    // The monitor the last opener's item lived on: the window shows on it for
    // its whole lifetime (macOS Quick Look shows on the same monitor as the
    // item it opens for). Null = no preference yet (first show lands on the
    // primary screen). A QPointer: it clears itself if the monitor goes away.
    QPointer<QScreen> m_anchorScreen;
};
