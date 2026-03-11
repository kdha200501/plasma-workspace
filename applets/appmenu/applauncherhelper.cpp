/*
    SPDX-FileCopyrightText: 2024 Plasma Workspace Authors

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "applauncherhelper.h"

#include <KIO/CommandLauncherJob>

AppLauncherHelper::AppLauncherHelper(QObject *parent)
    : QObject(parent)
{
}

void AppLauncherHelper::launchApplication(const QString &command) const
{
    auto *job = new KIO::CommandLauncherJob(command);
    job->start();
}

#include "moc_applauncherhelper.cpp"
