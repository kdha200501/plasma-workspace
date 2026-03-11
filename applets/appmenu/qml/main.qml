/*
    SPDX-FileCopyrightText: 2013 Heena Mahour <heena393@gmail.com>
    SPDX-FileCopyrightText: 2013 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
// Deliberately imported after QtQuick to avoid missing restoreMode property in Binding. Fix in Qt 6.
import QtQml

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.keyboardindicator as KeyboardIndicator
import org.kde.plasma.private.sessions
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import plasma.applet.org.kde.plasma.appmenu

PlasmoidItem {
    id: root

    readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
    readonly property bool view: Plasmoid.configuration.compactView

    onViewChanged: {
        Plasmoid.view = view;
    }

    Plasmoid.constraintHints: Plasmoid.CanFillArea
    preferredRepresentation: Plasmoid.configuration.compactView ? compactRepresentation : fullRepresentation

    SessionManagement {
        id: session
    }

    // Only exists because the default CompactRepresentation doesn't expose a
    // way to mark its icon as disabled.
    // TODO remove once it gains that feature.
    compactRepresentation: PlasmaComponents3.ToolButton {
        readonly property int fakeIndex: 0
        Layout.fillWidth: false
        Layout.fillHeight: false
        Layout.minimumWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
        enabled: appMenuModel.menuAvailable
        checkable: appMenuModel.menuAvailable && Plasmoid.currentIndex === fakeIndex
        checked: checkable
        icon.name: "application-menu"

        display: PlasmaComponents3.AbstractButton.IconOnly
        text: Plasmoid.title
        Accessible.description: root.toolTipSubText

        onClicked: Plasmoid.trigger(this, 0);
    }

    fullRepresentation: GridLayout {
        id: buttonGrid

        Plasmoid.status: {
            if (appMenuModel.menuAvailable && Plasmoid.currentIndex > -1 && buttonRepeater.count > 0) {
                return PlasmaCore.Types.NeedsAttentionStatus;
            } else if (appMenuModel.hasActiveWindow) {
                return PlasmaCore.Types.ActiveStatus;
            } else {
                return PlasmaCore.Types.ActiveStatus;
            }
        }

        LayoutMirroring.enabled: Application.layoutDirection === Qt.RightToLeft
        Layout.minimumWidth: implicitWidth
        Layout.minimumHeight: implicitHeight

        flow: root.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        rowSpacing: 0
        columnSpacing: 0

        Binding {
            target: Plasmoid
            property: "buttonGrid"
            value: buttonGrid
            restoreMode: Binding.RestoreNone
        }

        Connections {
            target: Plasmoid
            function onRequestActivateIndex(index: int) {
                const button = buttonRepeater.itemAt(index) as MenuDelegate;
                if (button) {
                    button.activated();
                }
            }
        }

        Connections {
            target: Plasmoid
            function onActivated() {
                const button = buttonRepeater.itemAt(0) as MenuDelegate;
                if (button) {
                    button.activated();
                }
            }
        }

        Item {
            id: powerButton
            Layout.fillHeight: true
            Layout.preferredWidth: height + 12
            Layout.topMargin: 3
            Layout.bottomMargin: 3
            Layout.alignment: Qt.AlignVCenter

            readonly property bool isHighlighted: powerMenu.status !== PlasmaExtras.Menu.Closed

            Rectangle {
                anchors.fill: parent
                color: "#363698"
                visible: powerButton.isHighlighted
            }

            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: Qt.resolvedUrl("system-menu.svg")
                fillMode: Image.PreserveAspectFit
            }

            TapHandler {
                onTapped: powerMenu.openRelative()
            }
        }

        PlasmaComponents3.ToolButton {
            id: noMenuPlaceholder
            visible: false
            text: Plasmoid.title
            Layout.fillWidth: root.vertical
            Layout.fillHeight: !root.vertical
        }

        Repeater {
            id: buttonRepeater
            model: appMenuModel.visible ? appMenuModel : null

            MenuDelegate {
                required property int index
                required property string activeMenu
                required property PlasmaCore.Action activeActions
                readonly property int buttonIndex: index

                Layout.fillWidth: root.vertical
                Layout.fillHeight: !root.vertical
                text: activeMenu
                Kirigami.MnemonicData.active: altState.pressed

                down: Plasmoid.currentIndex === index
                visible: text !== "" && (activeActions?.visible ?? false)

                menuIsOpen: Plasmoid.currentIndex !== -1
                onActivated: Plasmoid.trigger(this, index)

                // So we can show mnemonic underlines only while Alt is pressed
                KeyboardIndicator.KeyState {
                    id: altState
                    key: Qt.Key_Alt
                }
            }
        }
        Item {
            Layout.preferredWidth: 0
            Layout.preferredHeight: 0
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    // Power menu - placed outside the GridLayout to avoid clipping
    PlasmaExtras.Menu {
        id: powerMenu
        visualParent: powerButton
        placement: PlasmaExtras.Menu.BottomPosedLeftAlignedPopup

        PlasmaExtras.MenuItem {
            text: qsTr("About This Mac")
            onClicked: AppLauncherHelper.launchApplication("kinfocenter")
        }

        PlasmaExtras.MenuItem {
            separator: true
        }

        PlasmaExtras.MenuItem {
            text: qsTr("System Preferences")
            onClicked: AppLauncherHelper.launchApplication("systemsettings")
        }

        PlasmaExtras.MenuItem {
            text: qsTr("App Store")
            onClicked: AppLauncherHelper.launchApplication("plasma-discover")
        }

        PlasmaExtras.MenuItem {
            separator: true
        }

        PlasmaExtras.MenuItem {
            text: qsTr("Sleep")
            onClicked: session.suspend()
        }

        PlasmaExtras.MenuItem {
            text: qsTr("Restart")
            onClicked: session.requestReboot()
        }

        PlasmaExtras.MenuItem {
            text: qsTr("Shut Down")
            onClicked: session.requestShutdown()
        }

        PlasmaExtras.MenuItem {
            separator: true
        }

        PlasmaExtras.MenuItem {
            text: qsTr("Lock Screen")
            onClicked: session.requestLogout()
        }
    }

    AppMenuModel {
        id: appMenuModel
        containmentStatus: Plasmoid.containment.status
        screenGeometry: root.screenGeometry
        onRequestActivateIndex: Plasmoid.requestActivateIndex(index)
        Component.onCompleted: {
            Plasmoid.model = appMenuModel;
        }
    }
}
