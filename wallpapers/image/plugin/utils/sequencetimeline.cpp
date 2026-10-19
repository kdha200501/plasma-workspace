/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sequencetimeline.h"

#include <QtGlobal>

#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>

using namespace SequenceTimeline;

const char *SequenceTimeline::anchorName(Anchor anchor)
{
    static const char *names[AnchorCount] = {
        "civilDawn",
        "sunrise",
        "noon",
        "sunset",
        "civilDusk",
        "midnight",
    };

    return anchor >= CivilDawn && anchor < AnchorCount ? names[anchor] : "";
}

int SequenceTimeline::anchorFromName(const QString &name)
{
    static const QHash<QString, int> names = {
        {QStringLiteral("civilDawn"), int(CivilDawn)},
        {QStringLiteral("sunrise"), int(Sunrise)},
        {QStringLiteral("noon"), int(Noon)},
        {QStringLiteral("sunset"), int(Sunset)},
        {QStringLiteral("civilDusk"), int(CivilDusk)},
        {QStringLiteral("midnight"), int(Midnight)},
    };

    return names.value(name, -1);
}

bool SequenceTimeline::parseManifest(const QJsonObject &manifest, const std::function<bool(const QString &)> &fileExists, QList<SequenceFrame> &frames, QString &error)
{
    frames.clear();

    if (manifest.value(QStringLiteral("format")).toString() != QLatin1String("org.kde.plasma.wallpaper.dynamic-sequence/1")) {
        error = QStringLiteral("unsupported dynamic.json format");
        return false;
    }

    const QJsonArray frameArray = manifest.value(QStringLiteral("frames")).toArray();
    if (frameArray.isEmpty()) {
        error = QStringLiteral("dynamic.json has no frames");
        return false;
    }

    // Multiple frames may share an anchor (positioned within the segment by their distinct `t`).
    // Only an identical (anchor, t) pair is rejected, as it would place two frames at the same time.
    QSet<QPair<qint32, qint32>> usedPositions;
    for (const QJsonValue &value : frameArray) {
        const QJsonObject frameObject = value.toObject();

        SequenceFrame frame;
        frame.file = frameObject.value(QStringLiteral("file")).toString();
        if (frame.file.isEmpty()) {
            error = QStringLiteral("dynamic.json frame has no file");
            return false;
        }

        frame.anchor = anchorFromName(frameObject.value(QStringLiteral("anchor")).toString());
        if (frame.anchor == -1) {
            error = QStringLiteral("dynamic.json frame has an unknown anchor: %1").arg(frameObject.value(QStringLiteral("anchor")).toString());
            return false;
        }

        frame.t = frameObject.value(QStringLiteral("t")).toDouble(0.0);
        if (frame.t < 0.0 || frame.t > 1.0) {
            error = QStringLiteral("dynamic.json frame t is out of range: %1").arg(frame.t);
            return false;
        }

        const QPair position{qint32(frame.anchor), qint32(qRound(frame.t * 100.0))};
        if (usedPositions.contains(position)) {
            error = QStringLiteral("dynamic.json has duplicate anchor and t: %1 t=%2").arg(anchorName(Anchor(frame.anchor))).arg(frameObject.value(QStringLiteral("t")).toVariant().toString());
            return false;
        }
        usedPositions.insert(position);

        if (!fileExists(frame.file)) {
            error = QStringLiteral("dynamic.json frame file is missing: %1").arg(frame.file);
            return false;
        }

        frames.append(frame);
    }

    return true;
}

static const QList<QDateTime> anchorTimesOf(const KDarkLightCycle &cycle)
{
    const KDarkLightTransition morning = cycle.morning();
    const KDarkLightTransition evening = cycle.evening();

    return {
        morning.startDateTime(), // CivilDawn
        morning.endDateTime(), // Sunrise
        cycle.noonDateTime(), // Noon
        evening.startDateTime(), // Sunset
        evening.endDateTime(), // CivilDusk
        cycle.noonDateTime().addSecs(60 * 60 * 12), // Midnight
    };
}

QList<SequenceEvent> SequenceTimeline::buildTimeline(const QList<KDarkLightCycle> &cycles, const QList<SequenceFrame> &frames)
{
    QList<SequenceEvent> events;

    if (cycles.isEmpty()) {
        return events;
    }

    const int dayCount = cycles.size();
    for (int day = 0; day < dayCount; ++day) {
        const QList<QDateTime> anchorTimes = anchorTimesOf(cycles.at(day));
        for (int i = 0; i < frames.size(); ++i) {
            const SequenceFrame &frame = frames.at(i);

            const QDateTime start = anchorTimes.at(frame.anchor);
            // The segment from midnight wraps to the civil dawn of the next cycle.
            const int nextAnchorDay = (frame.anchor == Midnight) ? (day + 1) % dayCount : day;
            const int nextAnchor = (frame.anchor == Midnight) ? int(CivilDawn) : (frame.anchor + 1) % AnchorCount;
            const QDateTime end = anchorTimesOf(cycles.at(nextAnchorDay)).at(nextAnchor);

            SequenceEvent event;
            event.time = start.msecsTo(end) >= 0 ? start.addMSecs(qRound(frame.t * start.msecsTo(end))) : start;
            event.frame = i;
            events.append(event);
        }
    }

    std::stable_sort(events.begin(), events.end(), [](const SequenceEvent &a, const SequenceEvent &b) {
        return a.time < b.time;
    });

    return events;
}

bool SequenceTimeline::bracket(const QList<SequenceEvent> &events, const QDateTime &now, int &bottom, int &top, qreal &blend)
{
    if (events.isEmpty()) {
        return false;
    }

    const int count = events.size();

    if (now < events.first().time) {
        // Before the window: the last frame (most recent midnight) is shown.
        bottom = events.last().frame;
        top = events.first().frame;
        blend = 0.0;
        return true;
    }

    if (now >= events.last().time) {
        // Past the window: the last frame is shown, blending toward the first frame, which next
        // appears one full day after its occurrence in the window.
        bottom = events.last().frame;
        top = events.first().frame;
        const qint64 span = events.last().time.msecsTo(events.first().time.addDays(1));
        blend = span > 0 ? qBound<qreal>(0.0, qreal(events.last().time.msecsTo(now)) / span, 1.0) : 0.0;
        return true;
    }

    int k = 0;
    for (int i = 0; i < count; ++i) {
        if (events.at(i).time <= now) {
            k = i;
        } else {
            break;
        }
    }

    const SequenceEvent &below = events.at(k);
    const SequenceEvent &above = events.at(k + 1);
    const qint64 span = below.time.msecsTo(above.time);
    blend = span > 0 ? qBound<qreal>(0.0, qreal(below.time.msecsTo(now)) / span, 1.0) : 0.0;
    bottom = below.frame;
    top = above.frame;

    return true;
}
