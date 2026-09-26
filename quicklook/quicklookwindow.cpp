/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quicklookwindow.h"

#include "appadaptor.h"
#include "previewcontent.h"
#include "x11windowcenteredpositioner.h"
#include <KCrash>
#include <KLocalizedString>
#include <KWindowEffects>
#include <KWindowSystem>
#include <KX11Extras>

#include <LayerShellQt/Window>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QMouseEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSurface>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <QWindow>

// FRAGILITY NOTE: this is Qt *private* API. QQuickWindowContainer is the only
// supported way to host the QWidget preview (PreviewContent) inside a QQuick
// window scene, and the CMake side already links Qt6::QuickPrivate for it -
// but unlike the public API, private headers can change or break across Qt
// minor versions without a deprecation warning. Re-check this include (and
// qquickwindowcontainer's API usage below) whenever the Qt version is bumped.
#include <QtQuick/private/qquickwindowcontainer_p.h>

using namespace Qt::StringLiterals;

namespace
{
constexpr double MAX_SCREEN_PERCENT = 0.8;
constexpr int MIN_CONTENT_PX = 320;
}

QuickLookWindow::QuickLookWindow(PlasmaQuick::SharedQmlEngine *engine, QWindow *)
    : PlasmaQuick::PlasmaWindow()
    , m_engine(engine)
{
    KCrash::initialize();

    if (KWindowSystem::isPlatformX11()) {
        m_x11Positioner = new X11WindowCenteredPositioner(this);
    }

    // used only by screen readers
    setTitle(i18n("Quick Look"));

    m_contentWidget = new PreviewContent();
    // force the widget to have a native QWindow so it can be embedded in the QQuick scene
    m_contentWidget->winId();
    // The content is plain 2D raster (pixmap, labels, sliders; PixmapViewer
    // paints via CPU), so the embedded window must never take a GPU surface:
    // on machines where the child window's GL/EGL path is broken (e.g. KVM
    // with a virtio-gpu) the child's GL surface presents only its clear
    // color and the preview area shows as a solid white rectangle (the name
    // label is white-on-white in the same window). Pin the child window to
    // a raster surface (X pixmap on X11, SHM on Wayland): the same drawing
    // code, no GPU dependency. Qt 6 starts a widget window as a raster
    // surface and only promotes it to a GL surface when the widget tree
    // requests RHI, so this is a no-op on a stock Qt and a permanent guard
    // against the child regaining a GL surface if a future one does.
    if (QWindow *contentWindow = m_contentWidget->windowHandle()) {
        if (contentWindow->surfaceType() != QSurface::RasterSurface) {
            contentWindow->setSurfaceType(QSurface::RasterSurface);
            // The surface type is resolved when the native surface is
            // created, so the platform surface must be recreated for the
            // new type to take effect.
            contentWindow->destroy();
            contentWindow->create();
        }
    }
    // QQuickWindowContainer is the QQuickItem that embeds a QWindow in a QQuick scene.
    auto *container = new QQuickWindowContainer();
    container->setContainedWindow(m_contentWidget->windowHandle());
    m_contentWidgetItem = container;
    m_contentWidgetItem->setFlags(m_contentWidgetItem->flags() | QQuickItem::ItemIsFocusScope);

    new AppAdaptor(this);
    QDBusConnection::sessionBus().registerObject(u"/App"_s, this);

    connect(m_engine, &PlasmaQuick::SharedQmlEngine::finished, this, &QuickLookWindow::objectIncubated);
    m_engine->setSourceFromModule("org.kde.quicklook.window", "QuickLookView");
    m_engine->completeInitialization({
        {u"quicklookWindow"_s, QVariant::fromValue(this)},
    });

    auto screenRemoved = [this](QScreen *screen) {
        // m_anchorScreen is a QPointer, so it clears itself here; the window
        // follows the anchor (see positionOnScreen), so drop it and land on
        // the primary.
        if (screen == this->screen()) {
            setScreen(qGuiApp->primaryScreen());
            hide();
        }
    };

    // The window never takes the keyboard focus (KeyboardInteractivityNone, see
    // positionOnScreen): the owning view keeps the focus the whole time the
    // modal is open, so it drives the dismiss keys (Space/Escape) and the
    // selection. There is nothing for this window to close on, so no focus-loss
    // auto-dismiss is needed - a blank-space dismiss and a click-away both
    // happen in the owning view, which knows where the input landed.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, screenRemoved);
    // The content sizes the window to the preview; react when the preview's
    // size changes (new item, preview arrived).
    connect(m_contentWidget, &PreviewContent::sizeHintChanged, this, &QuickLookWindow::updateSize);

    // The window is frameless (PlasmaWindow) and the compositor provides no
    // resize for it (it is a layer-shell floating surface on Wayland and a
    // top-level without WM decorations on X11), so the frame margins act as
    // resize grips. The drag is handled client-side in an application-level
    // event filter: no mouse grab is available for a plain layer-shell
    // surface (Qt only allows grabbing popup windows on Wayland), so the
    // drag is tracked through the global mouse position the whole time it is
    // active, from moves delivered to any window of the app.
    qGuiApp->installEventFilter(this);
    m_userDragFallbackTimer.setSingleShot(true);
    m_userDragFallbackTimer.setInterval(300);
    connect(&m_userDragFallbackTimer, &QTimer::timeout, this, &QuickLookWindow::userDragEndedByOtherInput);
}

QuickLookWindow::~QuickLookWindow() = default;

void QuickLookWindow::objectIncubated()
{
    auto item = qobject_cast<QQuickItem *>(m_engine->rootObject());
    if (!item) {
        qWarning() << "Quick Look: failed to incubate QuickLookView.qml";
        return;
    }
    setMainItem(item);
    m_backgroundItem = item;

    // Host the preview widget on top of the QML background.
    m_contentWidgetItem->setParentItem(item);
    m_contentWidgetItem->setZ(10);

    connect(item, &QQuickItem::widthChanged, this, &QuickLookWindow::updateWidgetGeometry);
    connect(item, &QQuickItem::heightChanged, this, &QuickLookWindow::updateWidgetGeometry);
    connect(this, &QuickLookWindow::paddingChanged, this, &QuickLookWindow::updateSize);
    updateWidgetGeometry();
}

bool QuickLookWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        if (watched == this) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                const Qt::Edges edges = resizeEdgesFor(me->globalPosition().toPoint());
                if (edges) {
                    m_userResizeEdges = edges;
                    m_userResizeStartPos = me->globalPosition().toPoint();
                    m_userResizeStartGeometry = geometry();
                    m_userResizeSize = size();
                    setUserResized(true);
                    m_userDragFallbackTimer.start();
                    return true;
                }
            }
        }
        break;
    }
    case QEvent::MouseMove: {
        if (m_userResizeEdges) {
            // Active drag: grow the start geometry towards the mouse for
            // each dragged edge, then clamp the size within the window
            // limits (keeping the opposite fixed edges in place).
            const QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            QRect newGeometry = m_userResizeStartGeometry;
            if (m_userResizeEdges & Qt::LeftEdge) {
                newGeometry.setLeft(globalPos.x());
            }
            if (m_userResizeEdges & Qt::RightEdge) {
                newGeometry.setRight(globalPos.x());
            }
            if (m_userResizeEdges & Qt::TopEdge) {
                newGeometry.setTop(globalPos.y());
            }
            if (m_userResizeEdges & Qt::BottomEdge) {
                newGeometry.setBottom(globalPos.y());
            }
            const QSize newSize(qBound<int>(minimumSize().width(), newGeometry.width(), maximumSize().width()),
                                qBound<int>(minimumSize().height(), newGeometry.height(), maximumSize().height()));
            applyUserResizedSize(newSize, newGeometry);
            // Keep the drag alive while it makes progress; the fallback ends
            // it once moves stop arriving (pointer released over another
            // window).
            m_userDragFallbackTimer.start();
            return true;
        }
        if (watched == this) {
            // Hover feedback on the grips (consume it so the quick scene does
            // not reset the cursor after we set the resize one).
            Qt::CursorShape cursor = Qt::ArrowCursor;
            const Qt::Edges edges = resizeEdgesFor(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
            if (edges == (Qt::LeftEdge | Qt::TopEdge) || edges == (Qt::RightEdge | Qt::BottomEdge)) {
                cursor = Qt::SizeFDiagCursor;
            } else if (edges == (Qt::LeftEdge | Qt::BottomEdge) || edges == (Qt::RightEdge | Qt::TopEdge)) {
                cursor = Qt::SizeBDiagCursor;
            } else if (edges & (Qt::LeftEdge | Qt::RightEdge)) {
                cursor = Qt::SizeHorCursor;
            } else if (edges) {
                cursor = Qt::SizeVerCursor;
            }
            setCursor(cursor);
            return !!edges;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        if (m_userResizeEdges && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            endUserDrag();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

Qt::Edges QuickLookWindow::resizeEdgesFor(const QPoint &globalPos) const
{
    const QRect geometry = this->geometry();
    const QMargins p = padding();
    Qt::Edges edges;
    if (globalPos.x() <= geometry.left() + p.left()) {
        edges |= Qt::LeftEdge;
    }
    if (globalPos.x() >= geometry.right() - p.right() + 1) {
        edges |= Qt::RightEdge;
    }
    if (globalPos.y() <= geometry.top() + p.top()) {
        edges |= Qt::TopEdge;
    }
    if (globalPos.y() >= geometry.bottom() - p.bottom() + 1) {
        edges |= Qt::BottomEdge;
    }
    return edges;
}

void QuickLookWindow::endUserDrag()
{
    if (!m_userResizeEdges) {
        return;
    }
    m_userResizeEdges = {};
    m_userDragFallbackTimer.stop();
    unsetCursor();
}

void QuickLookWindow::userDragEndedByOtherInput()
{
    // A resize drag has no mouse grab (a plain layer-shell surface cannot
    // grab the pointer), so if the pointer is released over another window
    // its release event never reaches this one: fall back and end the drag
    // once it makes no progress for a moment.
    if (m_userResizeEdges) {
        endUserDrag();
    }
}

void QuickLookWindow::applyUserResizedSize(const QSize &newSize, const QRect &requestedGeometry)
{
    if (newSize == size()) {
        return;
    }
    // Rebase the requested geometry onto the clamped size with the fixed
    // (non-dragged) edges anchored in place.
    QRect newGeometry = requestedGeometry;
    newGeometry.setSize(newSize);
    if (!(m_userResizeEdges & Qt::LeftEdge)) {
        newGeometry.moveLeft(requestedGeometry.left());
    }
    if (!(m_userResizeEdges & Qt::RightEdge)) {
        newGeometry.moveRight(requestedGeometry.right());
    }
    if (!(m_userResizeEdges & Qt::TopEdge)) {
        newGeometry.moveTop(requestedGeometry.top());
    }
    if (!(m_userResizeEdges & Qt::BottomEdge)) {
        newGeometry.moveBottom(requestedGeometry.bottom());
    }
    setGeometry(newGeometry);
}

void QuickLookWindow::setUserResized(bool userResized)
{
    if (userResized == m_userResized) {
        return;
    }
    m_userResized = userResized;
    if (userResized) {
        // Suspend the X11 centered positioner for the duration of the drag:
        // it re-centers the window on every resize, which would fight the
        // drag (and it is only created on X11).
        if (m_x11Positioner) {
            m_x11Positioner->setSuspended(true);
        }
        m_contentWidget->setUserDisplaySize(size().shrunkBy(padding()));
    } else {
        if (m_x11Positioner) {
            m_x11Positioner->setSuspended(false);
        }
        m_contentWidget->setUserDisplaySize(QSize());
        updateSize();
    }
}

QSize QuickLookWindow::desiredWindowSize() const
{
    // The window must be the content size PLUS the frame padding: the content
    // item is placed and sized padding-free inside the window (see
    // updateWidgetGeometry), so a window sized to the content alone would
    // shrink the content item and the preview bleeds over the frame (the edges
    // the user sees covered). The content's cap already leaves room for the
    // frame, so the resulting window is at most the screen percentage.
    const QSize contentSize = m_contentWidget->sizeHint().expandedTo(QSize(MIN_CONTENT_PX, MIN_CONTENT_PX));
    const QMargins p = padding();
    return contentSize + QSize(p.left() + p.right(), p.top() + p.bottom());
}

void QuickLookWindow::updateContentCap()
{
    // Tell the content the screen cap so it can size previews to fit before
    // the window geometry is set (desiredWindowSize reads back its sizeHint).
    m_contentWidget->setMaximumContentSize(maxContentSize());
    updateContentPadding();
}

void QuickLookWindow::updateContentPadding()
{
    // Tell the content the frame padding it is displayed in: the content item
    // is shrunk by exactly this, so the cap applied to the preview must leave
    // room for the frame or the preview gets clipped at the screen edge.
    m_contentWidget->setFramePadding(padding());
}

QScreen *QuickLookWindow::anchorScreenObject() const
{
    // The monitor this window belongs to: the anchor set by the last opener,
    // or the window's screen / the primary until one is set.
    if (m_anchorScreen) {
        return m_anchorScreen;
    }
    return screen() ? screen() : QGuiApplication::primaryScreen();
}

QSize QuickLookWindow::maxContentSize() const
{
    QScreen *const targetScreen = anchorScreenObject();
    const QRect geometry = targetScreen->availableGeometry();
    return QSize(int(geometry.width() * MAX_SCREEN_PERCENT), int(geometry.height() * MAX_SCREEN_PERCENT));
}

void QuickLookWindow::updateMinMaxSize()
{
    // Clamp the window between a content minimum and the screen-percentage
    // cap (plus the frame padding, the window is the content plus the
    // frame, see desiredWindowSize).
    const QMargins p = padding();
    setMinimumSize(QSize(MIN_CONTENT_PX, MIN_CONTENT_PX) + QSize(p.left() + p.right(), p.top() + p.bottom()));
    setMaximumSize(maxContentSize() + QSize(p.left() + p.right(), p.top() + p.bottom()));
}

void QuickLookWindow::updateSize()
{
    if (!m_backgroundItem) {
        return;
    }
    updateContentCap();
    if (!m_userResized) {
        // Auto-fit the window to the preview (as before); a user-resized
        // window keeps the size the user set.
        const QSize targetSize = desiredWindowSize();
        if (targetSize != size()) {
            resize(targetSize);
        }
    }
    positionOnScreen();
    updateMinMaxSize();
    updateWidgetGeometry();
}

void QuickLookWindow::updateWidgetGeometry()
{
    if (m_contentWidgetItem && m_backgroundItem) {
        // The main item is already positioned in the padding-free content
        // area by PlasmaWindow, so the container fills it exactly (it was
        // previously offset by the padding a second time, pushing the
        // preview over the bottom/right frame edges).
        m_contentWidgetItem->setSize(m_backgroundItem->size());
    }
}

void QuickLookWindow::positionOnScreen()
{
    QScreen *const targetScreen = anchorScreenObject();
    setScreen(targetScreen);

    if (KWindowSystem::isPlatformWayland()) {
        auto layerWindow = LayerShellQt::Window::get(this);
        // LayerTop so the preview floats above app windows like KRunner does.
        layerWindow->setLayer(LayerShellQt::Window::LayerTop);
        layerWindow->setScope(u"quicklook"_s);
        // Never take the keyboard focus: with OnDemand the layer-shell surface
        // activates (steals focus) on show, which is exactly what the owning
        // view drives - it keeps the focus while the modal is open so arrows move
        // the selection and Space/Escape are handled by the view (matching the
        // intended "view owns the dismiss keys" design). None keeps the focus
        // where it is, so the focus is put back on the item the modal opened for.
        layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
        // No anchors: a floating window, centered by the geometry set on X11 /
        // by the available-geometry cap below.
        layerWindow->setAnchors(LayerShellQt::Window::AnchorNone);
        layerWindow->setScreen(targetScreen);
    } else if (KWindowSystem::isPlatformX11()) {
        // Re-center only while auto-fitted: a user-resized window keeps where
        // the user left it (its size, and its position, persists across
        // preview replacements and screen changes until it is closed).
        if (!m_userResized) {
            m_x11Positioner->polish();
        }
        KX11Extras::setOnDesktop(winId(), KX11Extras::currentDesktop());
        KX11Extras::setState(winId(), NET::SkipTaskbar | NET::SkipPager);
    }
}

void QuickLookWindow::previewUrls(const QStringList &urls, bool explicitTrigger, const QString &dbusSender, const QString &anchorScreenName)
{
    const QList<QUrl> urlList(urls.begin(), urls.end());
    if (urlList.isEmpty()) {
        close();
        return;
    }

    KFileItemList items;
    items.reserve(urlList.size());
    for (const QUrl &u : urlList) {
        // Cheap construction (no stat): mimetype is resolved lazily by the
        // preview job / icon code, matching how Dolphin builds items.
        KFileItem item(u, QString(), KFileItem::Unknown);
        if (!item.isNull()) {
            items << item;
        }
    }

    if (items.isEmpty()) {
        close();
        return;
    }

    // A non-explicit live-update must never re-open a closed preview. A client
    // may still carry a stale "preview is open" flag (e.g. it missed the closed
    // signal) and send a live-update on the selection change that follows a
    // dismiss; honoring it here would re-show the window the user just closed,
    // so every subsequent click would pop the preview back up. Only an explicit
    // trigger (Space) re-opens.
    if (!explicitTrigger && !isVisible()) {
        return;
    }

    // Cross-caller ownership arbitration: the window is shared by every
    // process that can call it (multiple Dolphin windows, the desktop Folder
    // View, ...), so the owner is tracked here, on the only side all of them
    // pass through. The caller's unique D-Bus connection name identifies it;
    // an empty name (a call from this process itself, e.g. started with a
    // URL on the command line) is always accepted. A deliberate user trigger
    // (explicitTrigger, e.g. a Space key press in another window) takes over
    // the preview from the current owner; only a non-explicit live-update
    // from a different sender is ignored.
    if (isVisible() && !explicitTrigger && !dbusSender.isEmpty() && dbusSender != m_lastFreshSender) {
        qDebug().noquote()
                << "Quick Look: ignoring live-update from" << dbusSender
                << "because the preview is owned by" << m_lastFreshSender;
        return;
    }
    m_lastFreshSender = dbusSender;
    // Watch the owner's D-Bus connection so the modal can be closed when the
    // owning process goes away (e.g. its last window closes and the app quits):
    // its close call can no longer reach us, and the focus landing on an
    // unrelated window would otherwise leave the preview open with no one to
    // dismiss it.
    if (!dbusSender.isEmpty()) {
        watchOwnerConnection(dbusSender);
    }

    // The window follows the monitor the item is on (see previewUrls()): a
    // name that matches a screen re-anchors it, no information (an empty name
    // from a caller predating the argument) keeps the previous anchor. Only
    // applied for calls that are honored (the early returns above cover the
    // rejected ones, which must not move the window). The name is a stable
    // per-monitor identifier that is meaningful in every process, unlike a
    // screen index.
    if (!anchorScreenName.isEmpty()) {
        for (QScreen *s : qGuiApp->screens()) {
            if (s->name() == anchorScreenName) {
                m_anchorScreen = s;
                break;
            }
        }
    }

    // A previewUrls() while already visible (or a fresh one) replaces the
    // content in place (no hide/show flicker) - the widget swaps its own display.
    m_contentWidget->showItems(items);

    m_hasContent = true;
    updateSize();

    positionOnScreen();
    setVisible(true);
}

void QuickLookWindow::close()
{
    setVisible(false);
    m_hasContent = false;
    m_lastFreshSender.clear();
    // The anchor described the monitor of the (now gone) item: drop it, a
    // next preview starts from the primary screen until its opener provides
    // a fresh one.
    m_anchorScreen = nullptr;
}

void QuickLookWindow::watchOwnerConnection(const QString &sender)
{
    if (!m_ownerWatcher) {
        m_ownerWatcher = new QDBusServiceWatcher(this);
        // The default constructor leaves the watcher attached to an invalid
        // (unconnected) QDBusConnection, in which case addWatchedService() is a
        // no-op. Point it at the session bus before watching anything.
        m_ownerWatcher->setConnection(QDBusConnection::sessionBus());
        m_ownerWatcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
        connect(m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString &service) {
            // The connection that last opened the preview disconnected (its
            // process quit), so nobody left can dismiss the modal - close it.
            if (service == m_lastFreshSender) {
                close();
            }
        });
    }
    m_ownerWatcher->addWatchedService(sender);
}

void QuickLookWindow::showEvent(QShowEvent *event)
{
    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(winId(), true);
    }
    PlasmaQuick::PlasmaWindow::showEvent(event);
    m_wasShown = true;
    // Position it but never take the keyboard focus: the owning view keeps the
    // focus while the modal is open, so the focus stays on the item the modal
    // opened for (no requestActivate/forceActiveWindow, which would steal it).
    positionOnScreen();
}

void QuickLookWindow::hideEvent(QHideEvent *event)
{
    m_contentWidget->pauseMedia();
    // Drop any pending user-resize drag state: a new preview starts
    // auto-fitted to its content on the next show.
    endUserDrag();
    if (m_userResized) {
        if (m_x11Positioner) {
            m_x11Positioner->setSuspended(false);
        }
        m_userResized = false;
        m_contentWidget->setUserDisplaySize(QSize());
    }
    PlasmaQuick::PlasmaWindow::hideEvent(event);
    // Notify the clients that the preview went away so they drop their
    // "preview is open" state. isVisible() cannot be used for this: during
    // the hide event the visibility flag is already cleared (QWindow clears
    // it before delivering QHideEvent), so the old wasVisible check was
    // always false and the signal never reached the clients - their stale
    // open flag then made every next selection change re-open the preview.
    if (m_wasShown) {
        m_wasShown = false;
        Q_EMIT closed();
    }
}

void QuickLookWindow::resizeEvent(QResizeEvent *event)
{
    PlasmaQuick::PlasmaWindow::resizeEvent(event);
    updateWidgetGeometry();
    // Keep the content fitted to a user-resized window.
    if (m_userResized) {
        m_contentWidget->setUserDisplaySize(size().shrunkBy(padding()));
    }
    // Re-request the preview at the new size, the same way the Information
    // Panel refreshes on resize.
    if (isVisible() && m_hasContent) {
        m_contentWidget->refreshPreview();
    }
}

void QuickLookWindow::keyPressEvent(QKeyEvent *event)
{
    // The view that owns the preview keeps the keyboard focus while the
    // modal is open (the kwin focus fix keeps its focus there and lets it be
    // re-activated), so Space (the trigger key) and Escape are handled by the
    // view itself, matching macOS Quick Look: arrow keys live-update the
    // selection (and thus the preview) and Space/Escape close it. Only
    // keyboard input that no view handles can arrive here.
    PlasmaQuick::PlasmaWindow::keyPressEvent(event);
}

#include "moc_quicklookwindow.cpp"
