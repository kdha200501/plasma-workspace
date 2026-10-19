/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sequencewallpaper.h"

#include <KPluginMetaData>

#include <QFileInfo>
#include <QUrlQuery>

SequenceWallpaperSnapshot::SequenceWallpaperSnapshot() = default;

SequenceWallpaperSnapshot::SequenceWallpaperSnapshot(const QDateTime &timestamp, const QUrl &bottom, const QUrl &top, qreal blendFactor, bool disjoint)
    : m_timestamp(timestamp)
    , m_bottom(bottom)
    , m_top(top)
    , m_blendFactor(blendFactor)
    , m_disjoint(disjoint)
{
}

SequenceWallpaper::SequenceWallpaper(QObject *parent)
    : QObject(parent)
    , m_systemClockMonitor(new KSystemClockSkewNotifier(this))
    , m_blendTimer(new QTimer(this))
    , m_rescheduleTimer(new QTimer(this))
{
    m_blendTimer->setSingleShot(true);
    connect(m_blendTimer, &QTimer::timeout, this, &SequenceWallpaper::update);

    m_systemClockMonitor->setActive(true);
    connect(m_systemClockMonitor, &KSystemClockSkewNotifier::skewed, this, &SequenceWallpaper::schedule);

    m_rescheduleTimer->setSingleShot(true);
    connect(m_rescheduleTimer, &QTimer::timeout, this, &SequenceWallpaper::schedule);
}

void SequenceWallpaper::classBegin()
{
}

void SequenceWallpaper::componentComplete()
{
    m_darkLightScheduleProvider = new KDarkLightScheduleProvider(m_initialState, this);
    connect(m_darkLightScheduleProvider, &KDarkLightScheduleProvider::scheduleChanged, this, [this]() {
        setState(m_darkLightScheduleProvider->state());
        schedule();
    });

    load();
    m_complete = true;
}

void SequenceWallpaper::setSource(const QUrl &source)
{
    if (m_source != source) {
        m_source = source;
        if (m_complete) {
            load();
        }
        Q_EMIT sourceChanged();
    }
}

void SequenceWallpaper::setSnapshot(const SequenceWallpaperSnapshot &snapshot)
{
    if (m_snapshot != snapshot) {
        m_snapshot = snapshot;
        Q_EMIT snapshotChanged();
    }
}

void SequenceWallpaper::setInitialState(const QString &state)
{
    if (m_initialState != state) {
        m_initialState = state;
        Q_EMIT initialStateChanged();
    }
}

void SequenceWallpaper::setState(const QString &state)
{
    if (m_state != state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void SequenceWallpaper::load()
{
    const QString rootPath = m_source.toLocalFile();

    auto metaData = KPluginMetaData::fromJsonFile(rootPath + u"/metadata.json");
    m_crossfade = metaData.value(u"X-KDE-CrossFade", true);

    QList<SequenceFrame> frames;
    QString error;
    QFile manifestFile(rootPath + u"/dynamic.json");
    if (manifestFile.open(QIODevice::ReadOnly) && SequenceTimeline::parseManifest(QJsonDocument::fromJson(manifestFile.readAll()).object(),
                                                                                  [&rootPath](const QString &file) { return QFileInfo(rootPath + u'/' + file).isFile(); },
                                                                                   frames,
                                                                                   error)) {
        m_frames = std::move(frames);
        m_frameUrls.clear();
        for (const SequenceFrame &frame : m_frames) {
            QUrl url(QStringLiteral("image://package/get"));
            url.setQuery({std::make_pair(QStringLiteral("dir"), rootPath), std::make_pair(QStringLiteral("file"), frame.file)});
            m_frameUrls.append(url);
        }
    } else {
        m_frames.clear();
        m_frameUrls.clear();
    }

    if (!m_frames.isEmpty()) {
        schedule();
    }
}

void SequenceWallpaper::schedule()
{
    if (m_frames.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QList<KDarkLightCycle> cycles = m_darkLightScheduleProvider->schedule().cycles();
    if (cycles.isEmpty()) {
        return;
    }

    // The current cycle is the one whose day owns now: the last cycle whose civil dawn is at
    // or before now. Before the first civil dawn the first cycle is used.
    int current = 0;
    const int cycleCount = cycles.size();
    for (int i = 0; i < cycleCount; ++i) {
        if (cycles.at(i).morning().startDateTime() > now) {
            break;
        }
        current = i;
    }

    // The timeline spans the previous, current and next day so that a frame anchored in the
    // current day that wraps across midnight (e.g. a sunset frame that continues past midnight
    // to the next civil dawn) is bracketed by a real event.
    const QList<KDarkLightCycle> window = {
        cycles.at(qMax(0, current - 1)),
        cycles.at(current),
        cycles.at(qMin(current + 1, cycleCount - 1)),
    };
    m_events = SequenceTimeline::buildTimeline(window, m_frames);

    // Rebuild the timeline when the next day becomes current. If now falls in the last cycle of
    // the schedule, the next day is not included; assume a full day later.
    const QDateTime rescheduleAt = current + 1 < cycleCount
        ? cycles.at(current + 1).morning().startDateTime()
        : cycles.at(current).morning().startDateTime().addSecs(60 * 60 * 24);
    m_rescheduleTimer->start(qMax(0, now.msecsTo(rescheduleAt)));

    update();
}

void SequenceWallpaper::update()
{
    if (m_frames.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();

    int bottom = -1;
    int top = -1;
    qreal blend = 0.0;
    if (!SequenceTimeline::bracket(m_events, now, bottom, top, blend)) {
        return;
    }

    if (!m_crossfade) {
        // Hard switch: keep the under frame until it is fully covered, then jump to the next.
        blend = blend < 0.5 ? 0.0 : 1.0;
    }

    const QUrl bottomUrl = m_frameUrls.at(bottom);
    const QUrl topUrl = m_frameUrls.at(top);

    const int blendInterval = 60000;
    m_blendTimer->start(blendInterval);

    // An update is considered discontinuous if it's blendInterval away from the previous.
    // However, in order to handle timers being imprecise, we add another blendInterval on top.
    bool disjoint = true;
    if (m_snapshot.isValid()) {
        const int disjointThreshold = 2 * blendInterval;
        // The time of the most recent event not after now (the one bracket() reports as bottom).
        QDateTime belowTime;
        for (const SequenceEvent &event : m_events) {
            if (event.time <= now) {
                belowTime = event.time;
            } else {
                break;
            }
        }
        if (m_snapshot.bottom() == bottomUrl && m_snapshot.top() == topUrl) {
            // The frames did not change. Disjoint only if we were suspended (or the clock
            // skewed) long enough for the blend to be off.
            disjoint = std::abs(m_snapshot.timestamp().msecsTo(now)) > disjointThreshold;
        } else if (m_snapshot.top() == bottomUrl) {
            // The frame on top became the bottom one, i.e. a regular advance to the next interval.
            // Disjoint only if the boundary was crossed long ago (e.g. after a suspend).
            disjoint = belowTime.isValid() && belowTime.msecsTo(now) > disjointThreshold;
        }
    }

    setSnapshot(SequenceWallpaperSnapshot(now, bottomUrl, topUrl, blend, disjoint));
}
