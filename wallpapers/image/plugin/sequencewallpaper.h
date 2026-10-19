/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <KDarkLightScheduleProvider>
#include <KSystemClockSkewNotifier>

#include <QDateTime>
#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <QUrl>
#include <qqmlintegration.h>

#include "utils/sequencetimeline.h"

/**
 * The SequenceWallpaperSnapshot type specifies what frames should be displayed
 * and how much they should be blended, if they need to be blended at all. The
 * disjoint flag specifies whether this snapshot can be displayed without a
 * smooth animation from the previous snapshot.
 */
class SequenceWallpaperSnapshot
{
    Q_GADGET
    QML_VALUE_TYPE(sequenceWallpaperSnapshot)
    Q_PROPERTY(QUrl bottom MEMBER m_bottom CONSTANT)
    Q_PROPERTY(QUrl top MEMBER m_top CONSTANT)
    Q_PROPERTY(qreal blendFactor MEMBER m_blendFactor CONSTANT)
    Q_PROPERTY(bool disjoint MEMBER m_disjoint CONSTANT)

public:
    auto operator<=>(const SequenceWallpaperSnapshot &other) const = default;

    SequenceWallpaperSnapshot();
    SequenceWallpaperSnapshot(const QDateTime &timestamp, const QUrl &bottom, const QUrl &top, qreal blendFactor, bool disjoint);

    bool isValid() const;
    QDateTime timestamp() const;
    QUrl bottom() const;
    QUrl top() const;
    qreal blendFactor() const;
    bool disjoint() const;

private:
    QDateTime m_timestamp;
    QUrl m_bottom;
    QUrl m_top;
    qreal m_blendFactor = 0.0;
    bool m_disjoint = false;
};

/**
 * The SequenceWallpaper type represents a dynamic wallpaper shown as a sequence of images.
 *
 * The sequence is described by the `dynamic.json` manifest in the package. Each frame is
 * anchored to a day-night schedule event. The frame matching the current position of the Sun
 * within the day is displayed, crossfading into the next one as its anchor approaches.
 *
 * Like DayNightWallpaper, if the device's current location is available, the frames are
 * synchronized with the position of the Sun. The `"X-KDE-CrossFade": false` metadata option
 * can be set if crossfading between frames is undesired.
 */
class SequenceWallpaper : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(SequenceWallpaperSnapshot snapshot READ snapshot NOTIFY snapshotChanged)
    Q_PROPERTY(QString initialState READ initialState WRITE setInitialState NOTIFY initialStateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit SequenceWallpaper(QObject *parent = nullptr);

    void classBegin() override;
    void componentComplete() override;

    QUrl source() const;
    void setSource(const QUrl &source);

    SequenceWallpaperSnapshot snapshot() const;
    void setSnapshot(const SequenceWallpaperSnapshot &snapshot);

    QString initialState() const;
    void setInitialState(const QString &state);

    QString state() const;
    void setState(const QString &state);

Q_SIGNALS:
    void sourceChanged();
    void snapshotChanged();
    void initialStateChanged();
    void stateChanged();

private:
    void load();
    void schedule();
    void update();

    KDarkLightScheduleProvider *m_darkLightScheduleProvider = nullptr;
    KSystemClockSkewNotifier *m_systemClockMonitor;

    QUrl m_source;
    bool m_crossfade = true;

    QList<SequenceFrame> m_frames;
    QList<QUrl> m_frameUrls;
    QList<SequenceEvent> m_events;

    QString m_initialState;
    QString m_state;

    SequenceWallpaperSnapshot m_snapshot;

    QTimer *m_blendTimer;
    QTimer *m_rescheduleTimer;

    bool m_complete = false;
};

inline bool SequenceWallpaperSnapshot::isValid() const
{
    return m_timestamp.isValid();
}

inline QDateTime SequenceWallpaperSnapshot::timestamp() const
{
    return m_timestamp;
}

inline QUrl SequenceWallpaperSnapshot::bottom() const
{
    return m_bottom;
}

inline QUrl SequenceWallpaperSnapshot::top() const
{
    return m_top;
}

inline qreal SequenceWallpaperSnapshot::blendFactor() const
{
    return m_blendFactor;
}

inline bool SequenceWallpaperSnapshot::disjoint() const
{
    return m_disjoint;
}

inline QUrl SequenceWallpaper::source() const
{
    return m_source;
}

inline QString SequenceWallpaper::initialState() const
{
    return m_initialState;
}

inline QString SequenceWallpaper::state() const
{
    return m_state;
}

inline SequenceWallpaperSnapshot SequenceWallpaper::snapshot() const
{
    return m_snapshot;
}
