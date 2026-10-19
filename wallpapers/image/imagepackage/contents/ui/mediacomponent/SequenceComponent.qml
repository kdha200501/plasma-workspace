/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */
pragma ComponentBehavior: Bound

import QtQuick
import org.kde.plasma.wallpapers.image

BaseMediaComponent {
    id: sequenceComponent
    blurSource: blurLoader.item

    readonly property alias status: sequenceView.status

    DayNightView {
        id: sequenceView
        anchors.fill: parent
        fillMode: sequenceComponent.fillMode
        snapshot: sequenceWallpaper.snapshot
    }

    SequenceWallpaper {
        id: sequenceWallpaper
        initialState: configuration.DarkLightScheduleState
        source: sequenceComponent.source

        onStateChanged: () => {
            if (configuration.DarkLightScheduleState != state) {
                configuration.DarkLightScheduleState = state;
                configuration.writeConfig();
            }
        }
    }

    Loader {
        id: blurLoader
        anchors.fill: parent
        active: sequenceComponent.blurEnabled
        sourceComponent: Image {
            asynchronous: true
            cache: false
            autoTransform: true
            fillMode: Image.PreserveAspectCrop
            source: sequenceWallpaper.snapshot.bottom
            sourceSize: sequenceComponent.sourceSize
            visible: false
        }
    }
}
