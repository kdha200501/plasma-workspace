/*
    SPDX-FileCopyrightText: 2026 Jack

    SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <KDarkLightSchedule>

#include "utils/sequencetimeline.h"

class TestSequenceTimeline : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testParseManifest_data();
    void testParseManifest();
    void testBuildTimeline();
    void testBracketsAtAnchors();
    void testBracketBetweenAnchors();
    void testBracketWrapsAcrossMidnight();
    void testTwoFramesReduceToDayNightCrossfade();

private:
    QDate m_day = QDate(2026, 10, 18);

    QDateTime dt(int dayOffset, int minutes) const
    {
        return QDateTime(m_day.addDays(dayOffset), QTime(0, 0).addSecs(minutes * 60));
    }

    KDarkLightCycle makeCycle(int dayOffset) const
    {
        const int dawn = 4 * 60 + 30;
        const int sunrise = 5 * 60;
        const int noon = 12 * 60;
        const int sunset = 19 * 60 + 30;
        const int dusk = 20 * 60;
        const KDarkLightTransition morning(KDarkLightTransition::Morning, dt(dayOffset, dawn), dt(dayOffset, sunrise));
        const KDarkLightTransition evening(KDarkLightTransition::Evening, dt(dayOffset, sunset), dt(dayOffset, dusk));
        return KDarkLightCycle(dt(dayOffset, noon), morning, evening);
    }
};

void TestSequenceTimeline::testParseManifest_data()
{
    QTest::addColumn<QString>("json");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<int>("frameCount");
    QTest::addColumn<QString>("expectedError");

    QTest::newRow("valid")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [
                { "file": "frames/a.jpg", "anchor": "midnight", "t": 0.0 },
                { "file": "frames/b.jpg", "anchor": "noon", "t": 0.5 }
            ]
        })"
        << true << 2 << QString();

    QTest::newRow("bad format")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/2",
            "frames": [ { "file": "frames/a.jpg", "anchor": "noon", "t": 0.0 } ]
        })"
        << false << 0 << QStringLiteral("unsupported dynamic.json format");

    QTest::newRow("no frames")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": []
        })"
        << false << 0 << QStringLiteral("dynamic.json has no frames");

    QTest::newRow("shared anchor")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [
                { "file": "frames/a.jpg", "anchor": "noon", "t": 0.0 },
                { "file": "frames/b.jpg", "anchor": "noon", "t": 0.5 }
            ]
        })"
        << true << 2 << QString();

    QTest::newRow("duplicate anchor and t")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [
                { "file": "frames/a.jpg", "anchor": "noon", "t": 0.0 },
                { "file": "frames/b.jpg", "anchor": "noon", "t": 0.0 }
            ]
        })"
        << false << 0 << QStringLiteral("dynamic.json has duplicate anchor and t: noon t=0");

    QTest::newRow("unknown anchor")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [ { "file": "frames/a.jpg", "anchor": "daybreak", "t": 0.0 } ]
        })"
        << false << 0 << QStringLiteral("dynamic.json frame has an unknown anchor: daybreak");

    QTest::newRow("t out of range")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [ { "file": "frames/a.jpg", "anchor": "noon", "t": 1.5 } ]
        })"
        << false << 0 << QStringLiteral("dynamic.json frame t is out of range: 1.5");

    QTest::newRow("no file")
        << R"({
            "format": "org.kde.plasma.wallpaper.dynamic-sequence/1",
            "frames": [ { "file": "", "anchor": "noon", "t": 0.0 } ]
        })"
        << false << 0 << QStringLiteral("dynamic.json frame has no file");
}

void TestSequenceTimeline::testParseManifest()
{
    QFETCH(QString, json);
    QFETCH(bool, valid);
    QFETCH(int, frameCount);
    QFETCH(QString, expectedError);

    QTemporaryDir dir;
    QDir(dir.path()).mkpath(QStringLiteral("frames"));
    for (const QString &name : {QStringLiteral("frames/a.jpg"), QStringLiteral("frames/b.jpg"), QStringLiteral("frames/c.jpg")}) {
        QImage(1, 1, QImage::Format_RGB32).save(dir.filePath(name), "JPG");
    }

    QList<SequenceFrame> frames;
    QString error;
    const QJsonObject manifest = QJsonDocument::fromJson(json.toUtf8()).object();
    const bool result = SequenceTimeline::parseManifest(manifest, [&dir](const QString &file) { return QFileInfo(dir.filePath(file)).isFile(); }, frames, error);

    QCOMPARE(result, valid);
    if (!valid) {
        QCOMPARE(error, expectedError);
    } else {
        QCOMPARE(frames.size(), frameCount);
    }
}

static QList<SequenceFrame> testFrames()
{
    QList<SequenceFrame> frames;
    frames.append({QStringLiteral("frames/0.jpg"), int(SequenceTimeline::Midnight), 0.0});
    frames.append({QStringLiteral("frames/1.jpg"), int(SequenceTimeline::Sunrise), 0.0});
    frames.append({QStringLiteral("frames/2.jpg"), int(SequenceTimeline::Sunset), 0.0});
    return frames;
}

void TestSequenceTimeline::testBuildTimeline()
{
    const QList<KDarkLightCycle> cycles = {makeCycle(-1), makeCycle(0)};
    const QList<SequenceEvent> events = SequenceTimeline::buildTimeline(cycles, testFrames());

    QCOMPARE(events.size(), 6);
    QCOMPARE(events.at(0).time, dt(-1, 300));
    QCOMPARE(events.at(0).frame, 1);
    QCOMPARE(events.at(1).time, dt(-1, 19 * 60 + 30));
    QCOMPARE(events.at(1).frame, 2);
    QCOMPARE(events.at(2).time, dt(0, 0));
    QCOMPARE(events.at(2).frame, 0);
    QCOMPARE(events.at(3).time, dt(0, 300));
    QCOMPARE(events.at(3).frame, 1);
    QCOMPARE(events.at(4).time, dt(0, 19 * 60 + 30));
    QCOMPARE(events.at(4).frame, 2);
    QCOMPARE(events.at(5).time, dt(1, 0));
    QCOMPARE(events.at(5).frame, 0);
}

void TestSequenceTimeline::testBracketsAtAnchors()
{
    const QList<KDarkLightCycle> cycles = {makeCycle(-1), makeCycle(0)};
    const QList<SequenceEvent> events = SequenceTimeline::buildTimeline(cycles, testFrames());

    int bottom = -1;
    int top = -1;
    qreal blend = -1;

    QVERIFY(SequenceTimeline::bracket(events, dt(0, 300), bottom, top, blend));
    QCOMPARE(bottom, 1); // sunrise
    QCOMPARE(top, 2); // sunset
    QCOMPARE(blend, 0.0);
}

void TestSequenceTimeline::testBracketBetweenAnchors()
{
    const QList<KDarkLightCycle> cycles = {makeCycle(-1), makeCycle(0)};
    const QList<SequenceEvent> events = SequenceTimeline::buildTimeline(cycles, testFrames());

    int bottom = -1;
    int top = -1;
    qreal blend = -1;

    // Midnight to sunrise, at half of 5h.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 150), bottom, top, blend));
    QCOMPARE(bottom, 0); // midnight
    QCOMPARE(top, 1); // sunrise
    QVERIFY(qAbs(blend - 0.5) < 1e-3);

    // Sunrise to sunset, at 7h of 14.5h.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 720), bottom, top, blend));
    QCOMPARE(bottom, 1); // sunrise
    QCOMPARE(top, 2); // sunset
    QVERIFY(qAbs(blend - 7.0 / 14.5) < 1e-3);
}

void TestSequenceTimeline::testBracketWrapsAcrossMidnight()
{
    const QList<KDarkLightCycle> cycles = {makeCycle(-1), makeCycle(0)};
    const QList<SequenceEvent> events = SequenceTimeline::buildTimeline(cycles, testFrames());

    int bottom = -1;
    int top = -1;
    qreal blend = -1;

    // Sunset 19:30 (frame 2) to midnight 00:00 (frame 0); 21:00 is 1.5h of 4.5h.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 21 * 60), bottom, top, blend));
    QCOMPARE(bottom, 2); // sunset
    QCOMPARE(top, 0); // midnight
    QVERIFY(qAbs(blend - 1.0 / 3.0) < 1e-3);
}

void TestSequenceTimeline::testTwoFramesReduceToDayNightCrossfade()
{
    // A two-frame sequence anchored at civil dawn (day) and civil dusk (night) must behave
    // like the classic day/night crossfade: the day frame underneath during the day segment,
    // the night frame underneath during the night segment, with a linear blend in between.
    QList<SequenceFrame> frames;
    frames.append({QStringLiteral("frames/d.jpg"), int(SequenceTimeline::CivilDawn), 0.0});
    frames.append({QStringLiteral("frames/n.jpg"), int(SequenceTimeline::CivilDusk), 0.0});

    // Three day-cycles so that the night segment crossing midnight (dusk -> next dawn) is
    // bracketed by a real event, mirroring the engine's previous/current/next-day window.
    const QList<KDarkLightCycle> cycles = {makeCycle(-1), makeCycle(0), makeCycle(1)};
    const QList<SequenceEvent> events = SequenceTimeline::buildTimeline(cycles, frames);
    //  0: 10-17 04:30 frame 0 (civil dawn)
    //  1: 10-17 20:00 frame 1 (civil dusk)
    //  2: 10-18 04:30 frame 0 (civil dawn)
    //  3: 10-18 20:00 frame 1 (civil dusk)
    //  4: 10-19 04:30 frame 0 (civil dawn)
    //  5: 10-19 20:00 frame 1 (civil dusk)
    QCOMPARE(events.size(), 6);

    int bottom = -1;
    int top = -1;
    qreal blend = -1;

    // Exactly at civil dawn: the day frame is revealed with no blend.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 4 * 60 + 30), bottom, top, blend));
    QCOMPARE(bottom, 0); // day
    QCOMPARE(top, 1); // night
    QCOMPARE(blend, 0.0);

    // Day segment (04:30 .. 20:00): the day frame is underneath, the night frame fading in.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 720), bottom, top, blend));
    QCOMPARE(bottom, 0); // day
    QCOMPARE(top, 1); // night
    // Noon is 7.5h into the 15.5h day segment.
    QVERIFY(qAbs(blend - 7.5 / 15.5) < 1e-3);

    // Exactly at civil dusk: the night frame is revealed with no blend, the day frame (next
    // civil dawn) fading in above.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 20 * 60), bottom, top, blend));
    QCOMPARE(bottom, 1); // night
    QCOMPARE(top, 0); // day
    QCOMPARE(blend, 0.0);

    // Night segment (20:00 .. 04:30): the night frame is underneath, the day frame fading in.
    QVERIFY(SequenceTimeline::bracket(events, dt(0, 21 * 60), bottom, top, blend));
    QCOMPARE(bottom, 1); // night
    QCOMPARE(top, 0); // day
    QVERIFY(blend > 0.0);
}

QTEST_MAIN(TestSequenceTimeline)

#include "test_sequencetimeline.moc"
