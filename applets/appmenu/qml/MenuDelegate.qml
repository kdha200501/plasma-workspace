/*
 * SPDX-FileCopyrightText: 2020 Carson Black <uhhadd@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls

import org.kde.ksvg as KSvg
import org.kde.plasma.components as PC3
import org.kde.kirigami as Kirigami

AbstractButton {
    id: controlRoot

    property bool menuIsOpen: false

    signal activated()

    // QMenu opens on press, so we'll replicate that here
    hoverEnabled: true

    // This will trigger even if hoverEnabled has just became true and the
    // mouse cursor is already hovering.
    //
    // In practice, this never works, at least on X11: when menuIsOpen the
    // hover event would not be delivered. Instead we rely on
    // plasmoid.requestActivateIndex signal to filter
    // QEvent::MouseMove events and tell us when to change the index.
    onHoveredChanged: if (hovered && menuIsOpen) { activated(); }

    // You don't actually have to "close" the menu via click/pressed handlers.
    // Instead, the menu will be closed automatically, as by any
    // other "outside of the menu" click event.
    onPressed: activated()

    enum State {
        Rest,
        Hover,
        Down
    }

    property int menuState: {
        // can't trust hovered state while QMenu is grabbing mouse pointer.
        if (down) {
            return MenuDelegate.State.Down;
        } else if (hovered && !menuIsOpen) {
            return MenuDelegate.State.Hover;
        }
        return MenuDelegate.State.Rest;
    }

    Kirigami.MnemonicData.controlType: Kirigami.MnemonicData.SecondaryControl
    Kirigami.MnemonicData.label: text

    topPadding: menuIconSource !== "" ? 5 : rest.margins.top
    leftPadding: menuIconSource !== "" ? 8 : rest.margins.left
    rightPadding: menuIconSource !== "" ? 8 : rest.margins.right
    bottomPadding: menuIconSource !== "" ? 5 : rest.margins.bottom

    // For icon-only mode the Label has empty text → implicitContentWidth=0 → Layout
    // would collapse the button. Explicitly declare the desired width so the Layout
    // allocates enough space (padding + a square content area = padding + availableHeight).
    implicitWidth: menuIconSource !== ""
        ? leftPadding + availableHeight + rightPadding
        : leftPadding + implicitContentWidth + rightPadding

    Accessible.description: i18nc("@info:usagetip", "Open a menu")

    background: KSvg.FrameSvgItem {
        id: rest
        imagePath: "widgets/menubaritem"
        prefix: switch (controlRoot.menuState) {
            case MenuDelegate.State.Down: return "pressed";
            case MenuDelegate.State.Hover: return "hover";
            case MenuDelegate.State.Rest: return "normal";
        }
    }

    FontLoader {
        id: virtueFont
        source: "qrc:/qt/qml/plasma/applet/org/kde/plasma/appmenu/virtue.ttf"
    }

    property string menuIconSource: ""

    contentItem: PC3.Label {
        id: menuLabel
        text: controlRoot.menuIconSource === "" ? controlRoot.Kirigami.MnemonicData.richTextLabel : ""
        textFormat: Text.StyledText
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        color: controlRoot.menuState === MenuDelegate.State.Rest ? "black" : "white"
        font.family: virtueFont.font.family
        font.pixelSize: 13

        Image {
            id: iconImage
            visible: controlRoot.menuIconSource !== ""
            anchors.fill: parent
            source: controlRoot.menuIconSource !== "" ? Qt.resolvedUrl(controlRoot.menuIconSource) : ""
            fillMode: Image.PreserveAspectFit
            smooth: true
            sourceSize.width: 64
            sourceSize.height: 64
        }
    }
}
