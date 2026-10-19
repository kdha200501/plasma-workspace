/*
    SPDX-FileCopyrightText: 2022 Fushan Wen <qydwhotmail@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "commontestdata.h"
#include "finder/imagefinder.h"

class ImageFinderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void testImageFinderCanFindImages();
    void testImageFinderIgnoresPackages();

private:
    QDir m_dataDir;
};

void ImageFinderTest::initTestCase()
{
    m_dataDir = QDir(QFINDTESTDATA("testdata/default"));
    QVERIFY(!m_dataDir.isEmpty());
    renameBizarreFile(m_dataDir);
}

void ImageFinderTest::cleanupTestCase()
{
    restoreBizarreFile(m_dataDir);
}

void ImageFinderTest::testImageFinderCanFindImages()
{
    /**
     * Expected result:
     *
     * - wallpaper.jpg.jpg is found.
     * - "# BUG454692 file name with hash char.png" is found.
     * - symlinkshouldnotbefoundbythefinder.jpg is ignored.
     * - screenshot.png is ignored.
     * - All images in package/contents/images/ are ignored.
     *
     * So the total number of images found by ImageFinder is 2.
     */
    const auto paths = ImageWallpaper::findAll({m_dataDir.absolutePath(), m_dataDir.absoluteFilePath(QStringLiteral("thispathdoesnotexist.jpg"))});

    qInfo() << "Found images:" << paths;
    QCOMPARE(paths.size(), ImageBackendTestData::defaultImageCount);
    QTRY_COMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultImageFileName1)), 1);
    QTRY_COMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultImageFileName2)), 1);
    QCOMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultImageFileName3)), 1);
    QCOMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultImageFileName4)), 1);
    QCOMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultImageFileName5)), 1);
    QCOMPARE(paths.count(m_dataDir.absoluteFilePath(ImageBackendTestData::defaultHiddenImageFileName)), 0);
}

void ImageFinderTest::testImageFinderIgnoresPackages()
{
    // Images inside a KPackage (metadata.json at the root) are exposed by the package model, so
    // ImageWallpaper must not list them as loose wallpapers (e.g. a sequence's frames).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = dir.path();
    QVERIFY(QDir(root).mkpath(QStringLiteral("packages/mojave/frames")));
    QFile metaJson(root + QStringLiteral("/packages/mojave/metadata.json"));
    QVERIFY(metaJson.open(QIODevice::WriteOnly | QIODevice::Text));
    metaJson.close();

    QImage(4, 4, QImage::Format_RGB32).save(root + QStringLiteral("/packages/mojave/frames/00.jpg"));
    QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(root + QStringLiteral("/loosetop.jpg")));
    QVERIFY(QDir(root).mkpath(QStringLiteral("loosepkglike/frames")));
    // A directory without metadata.json is not a package -> its images still count.
    QVERIFY(QImage(4, 4, QImage::Format_RGB32).save(root + QStringLiteral("/loosepkglike/frames/x.jpg")));

    const auto paths = ImageWallpaper::findAll({root});

    qInfo() << "Found images:" << paths;
    QCOMPARE(paths.count(root + QStringLiteral("/packages/mojave/frames/00.jpg")), 0);
    QCOMPARE(paths.count(root + QStringLiteral("/loosepkglike/frames/x.jpg")), 1);
    QCOMPARE(paths.count(root + QStringLiteral("/loosetop.jpg")), 1);
}

QTEST_MAIN(ImageFinderTest)

#include "test_imagefinder.moc"
