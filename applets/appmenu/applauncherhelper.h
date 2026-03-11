/*
    SPDX-FileCopyrightText: 2024 Plasma Workspace Authors

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QObject>
#include <QQmlEngine>

class AppLauncherHelper : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit AppLauncherHelper(QObject *parent = nullptr);

    Q_INVOKABLE void launchApplication(const QString &command) const;
};
