/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef X11WINDOWCENTEREDPOSITIONER_H
#define X11WINDOWCENTEREDPOSITIONER_H

#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QWindow>

/**
 * Positions a window centered on the active screen, keeping the position
 * in sync with screen and window size changes.
 *
 * Position changes apply just before the next frame.
 */
class X11WindowCenteredPositioner : public QObject
{
    Q_OBJECT
public:
    explicit X11WindowCenteredPositioner(QWindow *window);

    void polish();

    /**
     * Temporarily stops re-positioning the window. Used while the window is
     * being user-resized so the drag is not fought by the centering.
     */
    void setSuspended(bool suspended);

    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void geometryChanged();

private:
    void updatePolish();
    void reposition();
    void handleScreenChanged();

    bool m_needsRepositioning;
    bool m_suspended = false;
    QWindow *m_window;
    QPointer<QScreen> m_screen;
};

#endif // X11WINDOWCENTEREDPOSITIONER_H
