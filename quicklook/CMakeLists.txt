set(quicklook_SRCS
    main.cpp
    quicklookwindow.cpp
    quicklookwindow.h
    appadaptor.cpp
    appadaptor.h
    previewcontent.cpp
    previewcontent.h
    pixmapviewer.cpp
    pixmapviewer.h
    mediawidget.cpp
    mediawidget.h
    directoryview.cpp
    directoryview.h
    x11windowcenteredpositioner.cpp
    x11windowcenteredpositioner.h
)

set(quicklook_dbusAppXML dbus/org.kde.quicklook.App.xml)
find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Multimedia MultimediaWidgets Quick)
find_package(Qt6QuickPrivate ${QT_MIN_VERSION} REQUIRED NO_MODULE)
# The adaptor is written by hand (appadaptor.{h,cpp}) instead of being generated
# from the XML: its slots carry the incoming QDBusMessage so the caller's
# unique D-Bus name can be forwarded to QuickLookWindow for cross-process
# ownership arbitration. The XML remains the source of truth for the
# interface definition (it is installed, and the adaptor's introspection
# string must stay in sync with it).
ecm_generate_dbus_service_file(
    NAME org.kde.quicklook
    EXECUTABLE "${KDE_INSTALL_FULL_BINDIR}/plasma-quicklook --daemon"
    SYSTEMD_SERVICE plasma-quicklook.service
    DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR}
)

add_executable(plasma-quicklook ${quicklook_SRCS})

ecm_add_qml_module(quicklook
    URI "org.kde.quicklook.window"
    QML_FILES qml/QuickLookView.qml
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES org.kde.plasma.core
)
# Without this, the module's plugin/qmldir/typeinfo are built but never
# installed, so QuickLookWindow's setSourceFromModule("org.kde.quicklook.window", ...)
# cannot find it outside the build tree (KDE_INSTALL_QMLDIR is the default
# DESTINATION and is where QQmlEngine looks for it once installed).
ecm_finalize_qml_module(quicklook)

target_link_libraries(plasma-quicklook PRIVATE
    Qt::Quick
    Qt::QuickPrivate
    Qt::Widgets
    Qt::QuickWidgets
    Qt::Multimedia
    Qt::MultimediaWidgets
    KF6::ConfigWidgets
    KF6::GuiAddons
    KF6::I18n
    KF6::IconThemes
    KF6::KIOCore
    KF6::KIOGui
    KF6::KIOWidgets
    Plasma::PlasmaQuick
    KF6::DBusAddons
    KF6::Crash
    LayerShellQt::Interface
)
target_compile_definitions(plasma-quicklook PRIVATE -DPROJECT_VERSION="${PROJECT_VERSION}")

install(TARGETS plasma-quicklook ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ${quicklook_dbusAppXML} DESTINATION ${KDE_INSTALL_DBUSINTERFACEDIR})

# Deliberate packaging decision: no .desktop entry and no appstream metadata are
# installed for plasma-quicklook. It is a D-Bus-activated, system-tray-less
# daemon (like krunner's helpers) and is not meant to show up in the app
# launcher, so there is intentionally nothing to discover from the launcher.

ecm_install_configured_files(
    INPUT plasma-quicklook.service.in
    @ONLY
    DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR}
)
