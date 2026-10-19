/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include <KDarkLightSchedule>

/**
 * A frame of a dynamic wallpaper sequence manifest (`dynamic.json`).
 *
 * Each frame is anchored to a day-night schedule event (@c anchor) and placed
 * at @c anchor + @c t times the length of the segment up to the next anchor
 * (anchor order: civil dawn, sunrise, noon, sunset, civil dusk, midnight).
 */
struct SequenceFrame
{
    QString file; // Path relative to the package root, e.g. "frames/01.jpg"
    int anchor = -1; // Anchor name, see SequenceTimeline::AnchorName
    qreal t = 0.0; // Fraction of the segment to the next anchor, in [0.0, 1.0]
};

/**
 * An event of a concrete instance of a sequence: a frame with the time it is
 * shown in a particular day, computed from a @c KDarkLightCycle.
 */
struct SequenceEvent
{
    QDateTime time;
    int frame = -1; // Index into the parsed frame list
};

namespace SequenceTimeline
{
/**
 * The day-night schedule anchors in the order they occur within a day.
 */
enum Anchor
{
    CivilDawn,
    Sunrise,
    Noon,
    Sunset,
    CivilDusk,
    Midnight,
    AnchorCount,
};

const char *anchorName(Anchor anchor);
int anchorFromName(const QString &name);

/**
 * Parses and validates a sequence manifest.
 *
 * A valid manifest is a JSON object with "format" =
 * "org.kde.plasma.wallpaper.dynamic-sequence/1" and a non-empty "frames" array.
 * Each frame must specify a valid "anchor" and a "t" in [0.0, 1.0]; several frames may
 * share an anchor (positioned within the segment by their distinct "t"). File existence
 * is checked with @c fileExists.
 */
bool parseManifest(const QJsonObject &manifest, const std::function<bool(const QString &)> &fileExists, QList<SequenceFrame> &frames, QString &error);

/**
 * Computes the concrete event times of all frames for every day in @c cycles,
 * sorted in chronological order.
 *
 * The anchor times of a cycle are taken from its transitions: civil dawn and
 * sunrise from the morning transition, sunset and civil dusk from the evening
 * transition, noon from the cycle, and midnight as noon + 12 h. The segment
 * from midnight wraps to the civil dawn of the next cycle in the list.
 */
QList<SequenceEvent> buildTimeline(const QList<KDarkLightCycle> &cycles, const QList<SequenceFrame> &frames);

/**
 * Finds the two frames bracketing @c now in the given sorted events.
 *
 * On success @c bottom is the index into @c frames of the frame shown under,
 * @c top of the frame fading in above, and @c blend in [0.0, 1.0] is how far
 * @c now is between the two frame times. If the events are empty, false is
 * returned.
 */
bool bracket(const QList<SequenceEvent> &events, const QDateTime &now, int &bottom, int &top, qreal &blend);
}
