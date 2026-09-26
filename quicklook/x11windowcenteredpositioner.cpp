/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "x11windowcenteredpositioner.h"

#include <QScreen>

#include <KWindowSystem>

X11WindowCenteredPositioner::X11WindowCenteredPositioner(QWindow *window)
    : QObject(window)
    , m_needsRepositioning(false)
    , m_window(window)
{
    if (!KWindowSystem::isPlatformX11()) {
        qCritical("X11WindowCenteredPositioner should only be used on X11 windows");
    }

    m_window->installEventFilter(this);
    connect(m_window, &QWindow::screenChanged, this, &X11WindowCenteredPositioner::handleScreenChanged);

    handleScreenChanged();
}

void X11WindowCenteredPositioner::polish()
{
    m_needsRepositioning = true;
    m_window->requestUpdate();
}

bool X11WindowCenteredPositioner::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window) {
        return false;
    }
    switch (event->type()) {
    case QEvent::UpdateRequest:
        updatePolish();
        break;
    case QEvent::Resize:
    case QEvent::Expose:
        polish();
        break;
    default:
        break;
    }
    return false;
}

void X11WindowCenteredPositioner::updatePolish()
{
    if (m_needsRepositioning) {
        reposition();
        m_needsRepositioning = false;
    }
}

void X11WindowCenteredPositioner::reposition()
{
    QScreen *screen = m_window->screen();
    if (!screen) {
        return;
    }

    const QRect screenRect = screen->availableGeometry();
    QRect targetRect(screenRect.topLeft(), m_window->size());
    targetRect.moveCenter(screenRect.center());

    m_window->setGeometry(targetRect);
}

void X11WindowCenteredPositioner::handleScreenChanged()
{
    QScreen *newScreen = m_window->screen();
    if (newScreen == m_screen) {
        return;
    }
    if (m_screen) {
        disconnect(m_screen, nullptr, this, nullptr);
    }
    m_screen = newScreen;
    connect(newScreen, &QScreen::availableGeometryChanged, this, &X11WindowCenteredPositioner::polish);
    polish();
}

#include "moc_x11windowcenteredpositioner.cpp"
