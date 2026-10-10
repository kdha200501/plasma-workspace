/*
    SPDX-FileCopyrightText: 2008, 2009 Fredrik Höglund <fredrik@kde.org>

    SPDX-License-Identifier: LGPL-2.0-only
*/

#include "desktopnotifier.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KPluginFactory>
#include <KSharedConfig>

#include <kdirnotify.h>

#include <canberra.h>

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QStandardPaths>

#include <algorithm>

K_PLUGIN_CLASS_WITH_JSON(DesktopNotifier, "desktopnotifier.json")

using namespace Qt::StringLiterals;

namespace
{

// Like the standalone trash-sound service, always use the freedesktop theme: it ships both sounds
constexpr auto s_soundTheme = "freedesktop";

QString trashFilesPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + u"/Trash/files"_s;
}

int trashItemCount()
{
    return QDir(trashFilesPath()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).count();
}

void soundFinishCallback(ca_context *, uint32_t, int errorCode, void *)
{
    if (errorCode != CA_SUCCESS) {
        qWarning() << "Trash sound playback failed:" << ca_strerror(errorCode);
    }
}

// A single long-lived context: destroying one from its own completion callback aborts inside pulse's mainloop thread
ca_context *soundContext()
{
    static ca_context *context = nullptr;
    if (!context) {
        if (int ret = ca_context_create(&context); ret != CA_SUCCESS) {
            qWarning() << "Failed to create canberra context for trash sound:" << ca_strerror(ret);
            context = nullptr;
        }
    }
    return context;
}

void playTrashSound(const QString &soundName)
{
    if (!KSharedConfig::openConfig(u"kdeglobals"_s)->group(u"Sounds"_s).readEntry(u"Enable"_s, true)) {
        return;
    }

    ca_context *context = soundContext();
    if (!context) {
        return;
    }

    ca_proplist *props = nullptr;
    if (int ret = ca_proplist_create(&props); ret != CA_SUCCESS) {
        qWarning() << "Failed to create canberra property list:" << ca_strerror(ret);
        return;
    }

    ca_proplist_sets(props, CA_PROP_EVENT_ID, soundName.toUtf8().constData());
    ca_proplist_sets(props, CA_PROP_CANBERRA_XDG_THEME_NAME, s_soundTheme);
    // Streams with role "event" can be muted by a stored stream-restore rule, which would silence the sound
    ca_proplist_sets(props, CA_PROP_MEDIA_ROLE, "alert");
    ca_proplist_sets(props, CA_PROP_APPLICATION_NAME, "Plasma Desktop Notifier");

    // Distinct ids keep overlapping sounds from cancelling each other
    static uint32_t nextId = 0;
    if (int ret = ca_context_play_full(context, nextId++, props, soundFinishCallback, nullptr); ret != CA_SUCCESS) {
        qWarning() << "Failed to play trash sound:" << ca_strerror(ret);
    }

    ca_proplist_destroy(props);
}

} // namespace

DesktopNotifier::DesktopNotifier(QObject *parent, const QList<QVariant> &)
    : KDEDModule(parent)
    , m_trashCount(trashItemCount())
{
    m_desktopLocation = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));

    watcher = new QFileSystemWatcher(this);
    watcher->addPaths(QStringList{QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                                  QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + u"/trashrc",
                                  QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs")});

    connect(watcher, &QFileSystemWatcher::fileChanged, this, &DesktopNotifier::dirty);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &DesktopNotifier::dirty);

    auto bus = QDBusConnection::sessionBus();
    bus.connect(QString(), QString(), u"org.kde.KDirNotify"_s, u"FilesAdded"_s, this, SLOT(trashFilesAdded(QString)));
    bus.connect(QString(), QString(), u"org.kde.KDirNotify"_s, u"FilesRemoved"_s, this, SLOT(trashFilesRemoved(QStringList)));
}

void DesktopNotifier::trashFilesAdded(const QString &directory)
{
    if (!directory.startsWith(u"trash:"_s)) {
        return;
    }

    const int newCount = trashItemCount();
    const bool added = newCount > m_trashCount;
    m_trashCount = newCount;
    if (added) {
        playTrashSound(u"file-trash"_s);
    }
}

void DesktopNotifier::trashFilesRemoved(const QStringList &urls)
{
    const QString physicalPrefix = u"file://"_s + trashFilesPath() + u"/"_s;
    const bool isTrash = std::any_of(urls.cbegin(), urls.cend(), [&physicalPrefix](const QString &url) {
        return url.startsWith(u"trash:"_s) || url.startsWith(physicalPrefix);
    });
    if (!isTrash) {
        return;
    }

    const int newCount = trashItemCount();
    const int previousCount = m_trashCount;
    m_trashCount = newCount;
    // Restoring items leaves the trash non-empty, so only the full -> empty transition plays
    if (newCount == 0 && previousCount > 0) {
        playTrashSound(u"trash-empty"_s);
    }
}

void DesktopNotifier::watchDir(const QString &path)
{
    watcher->addPath(path);
}

void DesktopNotifier::dirty(const QString &path)
{
    if (path == QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + u"/trashrc") {
        QList<QUrl> trashUrls;

        // Check for any .desktop file linking to trash:/ to update its icon
        const auto desktopFiles = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).entryInfoList({QStringLiteral("*.desktop")});
        for (const auto &fi : desktopFiles) {
            KDesktopFile df(fi.absoluteFilePath());
            if (df.hasLinkType() && df.readUrl() == QLatin1String("trash:/")) {
                trashUrls << QUrl(QString(u"desktop:/" + fi.fileName()));
            }
        }

        if (!trashUrls.isEmpty()) {
            org::kde::KDirNotify::emitFilesChanged(trashUrls);
        }

        if (!watcher->files().contains(path)) {
            watcher->addPath(path);
        }
    } else if (path == QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs")) {
        checkDesktopLocation();

        if (!watcher->files().contains(path)) {
            watcher->addPath(path);
        }
    } else {
        // Emitting FilesAdded forces a re-read of the dir
        QUrl url;
        url.setScheme(QStringLiteral("desktop"));
        const auto relativePath = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).relativeFilePath(path);
        url.setPath(QStringLiteral("%1/%2").arg(url.path(), relativePath));
        url.setPath(QDir::cleanPath(url.path()));
        org::kde::KDirNotify::emitFilesAdded(url);
    }
}

void DesktopNotifier::checkDesktopLocation()
{
    const QUrl &currentLocation = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));

    if (m_desktopLocation != currentLocation) {
        m_desktopLocation = currentLocation;
        org::kde::KDirNotify::emitFilesChanged(QList{QUrl(QStringLiteral("desktop:/"))});
    }
}

#include <desktopnotifier.moc>

#include "moc_desktopnotifier.cpp"
