/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Quick Look window background. The preview content (pixmap / media player /
// name + count labels) is a C++ widget (PreviewContent) that the window
// embeds on top of this background, the same way KRunner embeds its content.

import QtQuick

Rectangle {
    id: root

    implicitWidth: 640
    implicitHeight: 480

    color: "#E6202024"
}
