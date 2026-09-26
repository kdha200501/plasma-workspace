/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Quick Look window background. The preview content (pixmap / media player /
// name + count labels) is a C++ widget (PreviewContent) that the window
// embeds on top of this background, the same way KRunner embeds its content.

import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: root

    property var quicklookWindow: null

    implicitWidth: 640
    implicitHeight: 480

    // Same look as the task switcher (kdeplasma-addons big_icons): the theme's
    // window background colour at 0.85 alpha. The PlasmaWindow dialog frame
    // (and its rim) is hidden in QuickLookWindow, so this rectangle covers the
    // whole window, including the frame padding. Its 6px radius matches the
    // blur/shadow layer the compositor still draws from the theme frame.
    color: "transparent"

    Rectangle {
        x: -(quicklookWindow?.leftPadding ?? 0)
        y: -(quicklookWindow?.topPadding ?? 0)
        width: quicklookWindow?.width ?? root.width
        height: quicklookWindow?.height ?? root.height
        radius: 6

        Kirigami.Theme.colorSet: Kirigami.Theme.Window
        Kirigami.Theme.inherit: false
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
    }
}
