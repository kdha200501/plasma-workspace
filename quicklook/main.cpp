/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <QApplication>
#include <QAction>
#include <QFont>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QQuickWindow>
#include <QSurfaceFormat>
#include <QUrl>

#include <KAboutData>
#include <KConfigGroup>
#include <KDBusService>
#include <KLocalizedString>
#include <KSharedConfig>

#include <Plasma/Plasma>
#include <PlasmaQuick/SharedQmlEngine>

#include <QQmlEngine>

#include "quicklookwindow.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    auto format = QSurfaceFormat::defaultFormat();
    format.setOption(QSurfaceFormat::ResetNotification);
    QSurfaceFormat::setDefaultFormat(format);

    QCommandLineParser parser;

    // this is needed to fake window position so Plasma Dialog sets correct borders
    qputenv("QT_WAYLAND_DISABLE_FIXED_POSITIONS", {});
    qunsetenv("QT_WAYLAND_RECONNECT");
    QQuickWindow::setDefaultAlphaBuffer(true);
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);

    QApplication app(argc, argv);
    qunsetenv("QT_WAYLAND_DISABLE_FIXED_POSITIONS");
    qputenv("QT_WAYLAND_RECONNECT", "1");

    KLocalizedString::setApplicationDomain(QByteArrayLiteral("plasma-quicklook"));

    KAboutData aboutData(u"plasma-quicklook"_s, i18n("Quick Look"), QStringLiteral(PROJECT_VERSION), i18n("Preview files in a modal window, like macOS Quick Look"), KAboutLicense::GPL);

    KAboutData::setApplicationData(aboutData);
    app.setQuitOnLastWindowClosed(false);
    app.setQuitLockEnabled(false);

    // KDBusService derives the D-Bus name it registers from the
    // applicationName (org.kde.<applicationName>). KAboutData::setApplicationData()
    // above overwrote applicationName with the component name
    // "plasma-quicklook"; set it after that so the daemon registers as
    // org.kde.quicklook - the name the clients and the D-Bus activation file
    // (plasma-quicklook.service) target.
    app.setApplicationName(QStringLiteral("quicklook"));

    QCommandLineOption replaceOption({u"replace"_s}, i18n("Replace an existing instance"));
    QCommandLineOption urlOption({u"url"_s, u"urls"_s}, i18n("URL(s) to preview immediately"), QStringLiteral("URL[s]"));
    QCommandLineOption daemonOption({u"d"_s, u"daemon"_s}, i18n("Start Quick Look in the background, don't show it."));

    parser.addOption(replaceOption);
    parser.addOption(urlOption);
    parser.addOption(daemonOption);
    aboutData.setupCommandLine(&parser);

    parser.process(app);
    aboutData.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique | KDBusService::StartupOption(parser.isSet(replaceOption) ? KDBusService::Replace : 0));

    // KRunner is normally launched by the session with the KDE platform theme, which injects the
    // system general font. When started standalone without QT_QPA_PLATFORMTHEME, fall back to the
    // font defined in kdeglobals so the preview window font matches the session.
    KConfigGroup generalFontGroup(KSharedConfig::openConfig(u"kdeglobals"_s), u"General"_s);
    const QString generalFontString = generalFontGroup.readEntry(u"font"_s, QString());
    if (!generalFontString.isEmpty()) {
        QFont generalFont = app.font();
        if (generalFont.fromString(generalFontString)) {
            app.setFont(generalFont);
        }
    }

    PlasmaQuick::SharedQmlEngine sharedEngine;
    // It is important to do this before the window is created, as it creates internally a framesvgitem
    // for the background that needs to use the current plasma theme
    Plasma::setupPlasmaStyle(sharedEngine.engine().get());
    sharedEngine.setInitializationDelayed(true);
    QuickLookWindow window(&sharedEngine);

    auto updateVisibility = [&]() {
        if (parser.isSet(daemonOption)) {
            window.setVisible(false);
        } else if (parser.isSet(urlOption)) {
            window.previewUrls(parser.values(urlOption));
        } else {
            window.setVisible(false);
        }
    };

    QObject::connect(&service, &KDBusService::activateRequested, &window, [&](const QStringList &arguments) {
        parser.parse(arguments);
        updateVisibility();
    });

    updateVisibility();

    return app.exec();
}
