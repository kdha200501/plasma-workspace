/*
    SPDX-FileCopyrightText: 2009-2010 Peter Penz <peter.penz19@gmail.com>
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "previewcontent.h"

#include "mediawidget.h"
#include "pixmapviewer.h"

#include <KConfigGroup>
#include <KIconEffect>
#include <KIconLoader>
#include <KIconUtils>
#include <KIO/PreviewJob>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygon>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// Mirrors Dolphin's PLAY_ARROW_SIZE for the video preview play icon.
constexpr int PLAY_ARROW_SIZE = 24;
constexpr int PLAY_ARROW_BORDER_SIZE = 2;
// Fallback display size: media without a natural size (audio) and before the
// first preview arrives.
const QSize DEFAULT_DISPLAY_SIZE{640, 480};
}

PreviewContent::PreviewContent(QWidget *parent)
    : QWidget(parent)
    , m_item()
    , m_previewJob(nullptr)
    , m_outdatedPreviewTimer(nullptr)
    , m_preview(nullptr)
    , m_mediaWidget(nullptr)
    , m_nameLabel(nullptr)
    , m_countLabel(nullptr)
    , m_isVideo(false)
{
    // Initialize timer for disabling an outdated preview with a small
    // delay. This prevents flickering if the new preview can be generated
    // within a very small timeframe.
    m_outdatedPreviewTimer = new QTimer(this);
    m_outdatedPreviewTimer->setInterval(100);
    m_outdatedPreviewTimer->setSingleShot(true);
    connect(m_outdatedPreviewTimer, &QTimer::timeout, this, &PreviewContent::markOutdatedPreview);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QWidget *previewArea = new QWidget(this);
    QVBoxLayout *areaLayout = new QVBoxLayout(previewArea);
    areaLayout->setContentsMargins(0, 0, 0, 0);

    m_preview = new PixmapViewer(previewArea);

    m_mediaWidget = new MediaWidget(previewArea);
    m_mediaWidget->hide();
    m_mediaWidget->setAutoPlay(true);
    connect(m_mediaWidget, &MediaWidget::hasVideoChanged, this, &PreviewContent::slotHasVideoChanged);

    areaLayout->addWidget(m_preview);
    areaLayout->addWidget(m_mediaWidget);
    areaLayout->setAlignment(m_preview, Qt::AlignCenter);

    layout->addWidget(previewArea, 1);

    QHBoxLayout *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_countLabel = new QLabel(this);
    m_countLabel->setAlignment(Qt::AlignCenter);
    m_countLabel->setStyleSheet(QStringLiteral("color: rgba(255, 255, 255, 128);"));
    m_countLabel->hide();

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setWordWrap(true);
    QFont nameFont = m_nameLabel->font();
    nameFont.setBold(true);
    m_nameLabel->setFont(nameFont);

    headerLayout->addStretch();
    headerLayout->addWidget(m_countLabel);
    headerLayout->addStretch();
    headerLayout->addStretch();
    headerLayout->addWidget(m_nameLabel, 1);
    headerLayout->addStretch();

    layout->addLayout(headerLayout);
}

PreviewContent::~PreviewContent() = default;

void PreviewContent::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // This widget is a native QWindow that the QQuickWindowContainer embeds
    // over the Quick scene (see quicklookwindow.cpp). A widget window's
    // native surface only receives the frame painted by the time it is
    // exposed; on the daemon's first show after a cold start the initial
    // paint can land while the child native surface is not yet exposed, so
    // the first frame never makes it to the screen and the preview area
    // stays the surface's clear color (white). Force a synchronous paint
    // here so the current content is painted into the backing store and
    // flushed to the native surface before the window is composited.
    repaint();
}

QSize PreviewContent::sizeHint() const
{
    // The window sizes itself to the content (see QuickLookWindow::desiredWindowSize),
    // so report the size the preview is displayed at, plus the name-label row,
    // rather than a fixed box.
    const int headerHeight = m_nameLabel ? m_nameLabel->sizeHint().height() : 0;
    return m_displaySize + QSize(0, headerHeight);
}

void PreviewContent::setMaximumContentSize(const QSize &max)
{
    if (max == m_maxContentSize) {
        return;
    }
    m_maxContentSize = max;
    refreshDisplaySize();
}

void PreviewContent::setFramePadding(const QMargins &margins)
{
    if (margins == m_framePadding) {
        return;
    }
    m_framePadding = margins;
    refreshDisplaySize();
}

void PreviewContent::refreshDisplaySize()
{
    QSize s;
    const bool isAudio = !m_items.isEmpty() && m_items.at(m_currentIndex).mimetype().startsWith(QLatin1String("audio/"));
    if (isAudio) {
        // Audio has no natural dimensions; keep the fallback size.
        s = DEFAULT_DISPLAY_SIZE;
    } else if (!m_items.isEmpty()) {
        // Use the loaded preview's natural size, capped to the window's
        // screen-percentage cap per axis.
        const QPixmap pixmap = m_preview->pixmap();
        if (!pixmap.isNull()) {
            s = pixmap.size();
        } else {
            s = DEFAULT_DISPLAY_SIZE;
        }
    } else {
        s = DEFAULT_DISPLAY_SIZE;
    }

    // The window's content area is the screen cap minus the frame padding
    // (the content item is shrunk by exactly that, see
    // QuickLookWindow::updateWidgetGeometry), and the header row sits below the
    // preview, so allow the preview only the leftover space: a preview sized at
    // the cap must never get clipped by the frame.
    if (m_maxContentSize.isValid()) {
        const int headerHeight = m_nameLabel ? m_nameLabel->sizeHint().height() : 0;
        const int maxW = m_maxContentSize.width() - m_framePadding.left() - m_framePadding.right();
        const int maxH = m_maxContentSize.height() - m_framePadding.top() - m_framePadding.bottom() - headerHeight;
        s = s.boundedTo(QSize(qMax(1, maxW), qMax(1, maxH)));
    }
    s = s.expandedTo(QSize(1, 1));

    if (s != m_displaySize) {
        m_displaySize = s;
        adjustWidgetSizes();
        Q_EMIT sizeHintChanged();
    }
}

void PreviewContent::showItems(const KFileItemList &items)
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (Dolphin bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    m_items = items;
    m_isVideo = false;
    m_currentIndex = 0;

    if (items.isEmpty()) {
        m_item = KFileItem();
        m_preview->stopAnimatedImage();
        m_mediaWidget->clearUrl();
        m_mediaWidget->hide();
        m_preview->show();
        m_countLabel->hide();
        setNameLabelText(QString());
        Q_EMIT contentChanged();
        return;
    }

    updateCountLabel();
    showCurrentItem();
    Q_EMIT contentChanged();
}

void PreviewContent::updateCountLabel()
{
    if (m_items.count() > 1) {
        m_countLabel->setText(i18nc("current item position of total item count", "%1 of %2", m_currentIndex + 1, m_items.count()));
        m_countLabel->show();
    } else {
        m_countLabel->hide();
    }
}

void PreviewContent::showCurrentItem()
{
    const KFileItem &item = m_items.at(m_currentIndex);
    // Detect a switched item by url, not entry: the items this widget is
    // passed are built from bare URLs (see QuickLookWindow::previewUrls), so
    // their UDS entry is empty and entry() == entry() for *every* item, which
    // would never register a change. A url change *is* a new item.
    if (m_item.url() != item.url()) {
        m_item = item;
        m_preview->stopAnimatedImage();
        // Drop the previous item's image before requesting the new one: the
        // new job only fills the area once it delivers, so until then the
        // previous selection's preview would linger on screen (visible for
        // seconds when the new file's thumbnail is uncached).
        m_preview->clearPixmap();
    }
    refreshPreview();
    Q_EMIT currentIndexChanged(m_currentIndex);
}

void PreviewContent::refreshPreview()
{
    if (m_item.isNull()) {
        return;
    }

    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (Dolphin bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    m_preview->setCursor(Qt::ArrowCursor);
    setNameLabelText(m_item.text());

    const QUrl itemUrl = m_item.url();
    const bool isSearchUrl = itemUrl.scheme().contains(QLatin1String("search")) && m_item.localPath().isEmpty();
    if (isSearchUrl) {
        m_preview->show();
        m_mediaWidget->hide();
        m_preview->setPixmap(QIcon::fromTheme(QStringLiteral("baloo")).pixmap(m_preview->height(), m_preview->width()));
    } else {
        refreshPixmapView();

        const QString mimeType = m_item.mimetype();
        const bool isAnimatedImage = PixmapViewer::isAnimatedMimeType(mimeType);
        m_isVideo = !isAnimatedImage && mimeType.startsWith(QLatin1String("video/"));
        const bool useMedia = m_isVideo || mimeType.startsWith(QLatin1String("audio/"));

        if (useMedia) {
            m_preview->setCursor(Qt::PointingHandCursor);
            m_preview->installEventFilter(m_mediaWidget);

            m_mediaWidget->show();
            m_preview->hide();

            m_mediaWidget->setUrl(m_item.targetUrl(), m_isVideo ? MediaWidget::MediaKind::Video : MediaWidget::MediaKind::Audio);
            adjustWidgetSizes();
        } else {
            if (isAnimatedImage) {
                m_preview->setAnimatedImageFileName(itemUrl.toLocalFile());
            }
            // When we don't need it, hide the media widget first to avoid flickering
            m_mediaWidget->hide();
            m_preview->show();
            m_preview->removeEventFilter(m_mediaWidget);
            m_mediaWidget->clearUrl();
        }
    }
}

void PreviewContent::refreshPixmapView()
{
    // If there is a preview job, kill it to prevent that we have jobs for
    // multiple items running, and thus a race condition (Dolphin bug 250787).
    if (m_previewJob) {
        m_previewJob->kill();
    }

    // Reset disabled state when starting a new preview job
    m_disabledPreviewUrl.clear();

    // Mark the currently shown preview as outdated. This is done
    // with a small delay to prevent a flickering when the next preview
    // can be shown within a short timeframe.
    m_outdatedPreviewTimer->start();

    // Same scope of supported files as Dolphin's Information Panel: whatever
    // [PreviewSettings] Plugins in kdeglobals enables.
    const KConfigGroup globalConfig(KSharedConfig::openConfig(), QStringLiteral("PreviewSettings"));
    const QStringList plugins = globalConfig.readEntry(QStringLiteral("Plugins"), KIO::PreviewJob::defaultPlugins());

    // Deviation from Dolphin: request the preview at the window's target
    // content size rather than a small docked-panel size.
    m_previewJob = new KIO::PreviewJob(KFileItemList() << m_item, m_preview->size(), &plugins);
    m_previewJob->setScaleType(KIO::PreviewJob::Unscaled);
    m_previewJob->setIgnoreMaximumSize(m_item.isLocalFile() && !m_item.isSlow());
    m_previewJob->setDevicePixelRatio(devicePixelRatioF());
    if (m_previewJob->uiDelegate()) {
        KJobWidgets::setWindow(m_previewJob, this);
    }

    connect(m_previewJob.data(), &KIO::PreviewJob::gotPreview, this, &PreviewContent::showPreview);
    connect(m_previewJob.data(), &KIO::PreviewJob::failed, this, &PreviewContent::showIcon);
}

void PreviewContent::setPreviewAutoPlay(bool autoPlay)
{
    m_mediaWidget->setAutoPlay(autoPlay);
}

void PreviewContent::pauseMedia()
{
    if (m_mediaWidget->state() == QMediaPlayer::PlayingState) {
        m_mediaWidget->pause();
    }
}

void PreviewContent::showIcon(const KFileItem &item)
{
    m_outdatedPreviewTimer->stop();

    // A job for an earlier item can deliver after the display has moved on
    // (e.g. its kill() arrived just after the job had already produced its
    // result); PixmapViewer would draw the stale icon on top of the current
    // item, so only accept results for the item that is shown now.
    if (item.url() != m_item.url()) {
        return;
    }
    QIcon icon = QIcon::fromTheme(item.iconName());
    QPixmap pixmap = KIconUtils::addOverlays(icon, item.overlays()).pixmap(m_preview->size(), devicePixelRatioF());
    pixmap.setDevicePixelRatio(devicePixelRatioF());
    m_preview->setPixmap(pixmap);
    m_disabledPreviewUrl.clear();
    refreshDisplaySize();
}

void PreviewContent::showPreview(const KFileItem &item, const QPixmap &pixmap)
{
    m_outdatedPreviewTimer->stop();

    // Same staleness guard as showIcon(): a preview that arrives after the
    // display has moved to a different item must not be drawn over it.
    if (item.url() != m_item.url()) {
        return;
    }

    QPixmap p = pixmap;
    if (!item.overlays().isEmpty()) {
        // Avoid scaling the images that are smaller than the preview size, to be consistent when there is no overlays
        if (pixmap.height() < m_preview->height() && pixmap.width() < m_preview->width()) {
            p = QPixmap(m_preview->size() * devicePixelRatioF());
            p.fill(Qt::transparent);
            p.setDevicePixelRatio(devicePixelRatioF());

            QPainter painter(&p);
            painter.drawPixmap(QPointF{m_preview->width() / 2.0 - pixmap.width() / pixmap.devicePixelRatioF() / 2,
                                       m_preview->height() / 2.0 - pixmap.height() / pixmap.devicePixelRatioF() / 2}
                                   .toPoint(),
                               pixmap);
        }
        p = KIconUtils::addOverlays(p, item.overlays()).pixmap(m_preview->size(), devicePixelRatioF());
        p.setDevicePixelRatio(devicePixelRatioF());
    }

    if (m_isVideo) {
        // adds a play arrow overlay
        auto maxDim = qMax(p.width(), p.height());
        auto arrowSize = qMax(PLAY_ARROW_SIZE, maxDim / 8);

        // compute relative pixel positions
        const int zeroX = static_cast<int>((p.width() / 2 - arrowSize / 2) / p.devicePixelRatio());
        const int zeroY = static_cast<int>((p.height() / 2 - arrowSize / 2) / p.devicePixelRatio());

        QPolygon arrow;
        arrow << QPoint(zeroX, zeroY);
        arrow << QPoint(zeroX, zeroY + arrowSize);
        arrow << QPoint(zeroX + arrowSize, zeroY + arrowSize / 2);

        QPainterPath path;
        path.addPolygon(arrow);

        QLinearGradient gradient(QPointF(zeroX, zeroY + arrowSize / 2), QPointF(zeroX + arrowSize, zeroY + arrowSize / 2));

        QColor whiteColor = Qt::white;
        QColor blackColor = Qt::black;
        gradient.setColorAt(0, whiteColor);
        gradient.setColorAt(1, blackColor);

        QBrush brush(gradient);

        QPainter painter(&p);

        QPen pen(blackColor, PLAY_ARROW_BORDER_SIZE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);

        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawPolygon(arrow);
        painter.fillPath(path, brush);
    }

    m_preview->setPixmap(p);
    refreshDisplaySize();
}

void PreviewContent::markOutdatedPreview()
{
    if (m_item.isDir()) {
        // directory preview can be long
        // but since we always have icons to display
        // use it until the preview is done
        showIcon(m_item);
    } else {
        // Only apply disabled effect once per URL to avoid repeated brightening
        if (m_disabledPreviewUrl == m_item.url()) {
            return;
        }
        m_disabledPreviewUrl = m_item.url();

        QPixmap disabledPixmap = m_preview->pixmap();
        if (!disabledPixmap.isNull()) {
            KIconEffect::toDisabled(disabledPixmap);
            m_preview->setPixmap(disabledPixmap);
        }
    }
}

void PreviewContent::slotHasVideoChanged(bool hasVideo)
{
    m_preview->setVisible(!hasVideo);
    if (m_preview->isVisible() && m_preview->size().width() != m_preview->pixmap().size().width()) {
        // in case the preview has been resized when the media widget was displayed
        // we need to refresh its content
        refreshPixmapView();
    }
}

void PreviewContent::setNameLabelText(const QString &text)
{
    m_nameLabel->setText(text);
}

void PreviewContent::adjustWidgetSizes()
{
    const QSize targetSize = m_displaySize.isValid() ? m_displaySize : m_preview->size();
    m_mediaWidget->setVideoSize(targetSize);
    m_preview->setSizeHint(targetSize);
}

#include "moc_previewcontent.cpp"
