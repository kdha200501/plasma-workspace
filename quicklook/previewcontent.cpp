/*
    SPDX-FileCopyrightText: 2009-2010 Peter Penz <peter.penz19@gmail.com>
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "previewcontent.h"

#include "directoryview.h"
#include "mediawidget.h"
#include "pixmapviewer.h"

#include <KIconEffect>
#include <KIconLoader>
#include <KIconUtils>
#include <KIO/PreviewJob>
#include <KJobWidgets>
#include <KLocalizedString>

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
// The modal's dark translucent background (see qml/QuickLookView.qml). Painted
// onto the native content surface so unpainted areas show the calibrated
// folder-mode background rather than the platform's default clear color.
const QColor MODAL_BACKGROUND(QStringLiteral("#E6202024"));
// The modal's padding around the content. The theme dialog frame already
// insets by 6px (dialogs/background, see PlasmaWindow::padding()); this inner
// margin brings the total to 36px between the window edge and the content.
constexpr int CONTENT_MARGIN = 30;
}

PreviewContent::PreviewContent(QWidget *parent)
    : QWidget(parent)
    , m_item()
    , m_previewJob(nullptr)
    , m_outdatedPreviewTimer(nullptr)
    , m_preview(nullptr)
    , m_mediaWidget(nullptr)
    , m_directoryView(nullptr)
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
    layout->setContentsMargins(CONTENT_MARGIN, CONTENT_MARGIN, CONTENT_MARGIN, CONTENT_MARGIN);

    QWidget *previewArea = new QWidget(this);
    QVBoxLayout *areaLayout = new QVBoxLayout(previewArea);
    areaLayout->setContentsMargins(0, 0, 0, 0);

    m_preview = new PixmapViewer(previewArea);

    m_mediaWidget = new MediaWidget(previewArea);
    m_mediaWidget->hide();
    connect(m_mediaWidget, &MediaWidget::hasVideoChanged, this, &PreviewContent::slotHasVideoChanged);

    // The directory listing replaces the preview/media area for folder items.
    m_directoryView = new DirectoryView(previewArea);
    m_directoryView->hide();
    // The listing's height follows its wrapped rows (flex-wrap); resize the
    // window to the new size whenever the row count changes.
    connect(m_directoryView, &DirectoryView::preferredSizeChanged, this, &PreviewContent::refreshDisplaySize);

    areaLayout->addWidget(m_preview);
    areaLayout->addWidget(m_mediaWidget);
    areaLayout->addWidget(m_directoryView, 1);
    areaLayout->setAlignment(m_preview, Qt::AlignCenter);
    areaLayout->setAlignment(m_mediaWidget, Qt::AlignCenter);

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
    m_nameLabel->setStyleSheet(QStringLiteral("color: white;"));
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
    // the first frame never makes it to the screen. Force a synchronous
    // paint here so the current content is painted into the backing store
    // and flushed to the native surface before the window is composited.
    repaint();
}

void PreviewContent::paintEvent(QPaintEvent *event)
{
    // The content is a native QWindow surface whose clear color is not
    // guaranteed to be the modal's dark background (it can be the platform
    // default, white), so the areas not covered by an opaque child (the empty
    // PixmapViewer preview, the gaps of the listing) show it through. Paint the
    // modal background explicitly, exactly as DirectoryView does for the folder
    // mode, so every mode shows the calibrated folder-mode background.
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.fillRect(rect(), MODAL_BACKGROUND);
}

QSize PreviewContent::sizeHint() const
{
    // The window sizes itself to the content (see QuickLookWindow::desiredWindowSize),
    // so report the size the preview is displayed at, plus the name-label row,
    // rather than a fixed box.
    const int headerHeight = m_nameLabel ? m_nameLabel->sizeHint().height() : 0;
    return m_displaySize + QSize(2 * CONTENT_MARGIN, 2 * CONTENT_MARGIN + headerHeight);
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

void PreviewContent::setUserDisplaySize(const QSize &size)
{
    if (size == m_userDisplaySize) {
        return;
    }
    m_userDisplaySize = size;
    refreshDisplaySize();
}

void PreviewContent::refreshDisplaySize()
{
    const int headerHeight = m_nameLabel ? m_nameLabel->sizeHint().height() : 0;

    // A listing preview sizes to the wrapped listing grid (the view fills it).
    if (m_listing) {
        const QSize s = m_directoryView->preferredSize();
        if (s != m_displaySize) {
            m_displaySize = s;
            Q_EMIT sizeHintChanged();
        }
        return;
    }

    if (m_userDisplaySize.isValid()) {
        // User-resized window: fit the natural preview size to the content
        // area left by the name-label row and the content margin, aspect
        // ratio preserved. A pixmap larger than the area is scaled down by
        // the viewer, a smaller one keeps its natural size (the viewer
        // centers it).
        const QSize area(qMax(1, m_userDisplaySize.width() - 2 * CONTENT_MARGIN),
                         qMax(1, m_userDisplaySize.height() - 2 * CONTENT_MARGIN - headerHeight));
        const bool isAudio = !m_items.isEmpty() && m_items.at(m_currentIndex).mimetype().startsWith(QLatin1String("audio/"));
        QSize s;
        if (isAudio) {
            s = DEFAULT_DISPLAY_SIZE;
        } else if (!m_items.isEmpty()) {
            const QPixmap pixmap = m_preview->pixmap();
            s = pixmap.isNull() ? DEFAULT_DISPLAY_SIZE : pixmap.size();
        } else {
            s = DEFAULT_DISPLAY_SIZE;
        }
        s = s.scaled(area, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        if (s != m_displaySize) {
            m_displaySize = s;
            adjustWidgetSizes();
            Q_EMIT sizeHintChanged();
        }
        return;
    }

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
        const int maxW = m_maxContentSize.width() - m_framePadding.left() - m_framePadding.right() - 2 * CONTENT_MARGIN;
        const int maxH = m_maxContentSize.height() - m_framePadding.top() - m_framePadding.bottom() - 2 * CONTENT_MARGIN - headerHeight;
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
        m_previewJob = nullptr;
    }

    m_items = items;
    m_isVideo = false;
    m_currentIndex = 0;

    if (items.isEmpty()) {
        m_item = KFileItem();
        m_listing = false;
        m_directoryUrl.clear();
        m_listingItems.clear();
        m_preview->stopAnimatedImage();
        m_mediaWidget->clearUrl();
        m_mediaWidget->hide();
        m_directoryView->hide();
        m_preview->show();
        m_countLabel->hide();
        setNameLabelText(QString());
        return;
    }

    if (items.count() > 1) {
        // A multi-item selection is shown in the listing view: every folder as
        // its folder icon, every file as its content preview (its type icon
        // when no preview can be generated for its type).
        showSelectedItems();
        return;
    }

    updateCountLabel();
    showCurrentItem();
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
}

void PreviewContent::showSelectedItems()
{
    const KFileItem &item = m_items.at(m_currentIndex);
    if (m_item.url() != item.url()) {
        m_item = item;
    }
    m_preview->stopAnimatedImage();
    m_preview->clearPixmap();
    // The grid shows the items themselves, so the name label row carries
    // nothing for a multi-selection.
    setNameLabelText(QString());
    m_countLabel->hide();

    m_listing = true;
    m_preview->hide();
    // Hiding the media widget stops its player and synchronously emits
    // hasVideoChanged(false), which would re-show the previous item's poster
    // over the listing; the guard in slotHasVideoChanged() keeps it down.
    m_mediaWidget->hide();
    m_mediaWidget->clearUrl();
    if (m_previewJob) {
        m_previewJob->kill();
        m_previewJob = nullptr;
    }
    m_directoryView->show();
    // The grid shows the items themselves (folders as their folder icon,
    // files as their content preview when supported): a resize, or the same
    // selection being shown again, must not re-list it. A directory listing
    // (m_directoryUrl set) is never this selection's list.
    const bool showingThisSelection = m_directoryUrl.isEmpty() && m_listingItems == m_items;
    if (!showingThisSelection) {
        m_listingItems = m_items;
        m_directoryUrl.clear();
        m_directoryView->setItems(m_items);
    }
    refreshDisplaySize();
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

    const QUrl itemUrl = m_item.url();

    if (m_items.count() > 1) {
        // A multi-item selection is shown in the listing view (the grid shows
        // the items themselves, so there is no name label / count row).
        showSelectedItems();
        return;
    }

    setNameLabelText(m_item.text());

    // A folder is shown as its contents (the desktop CTA popup listing)
    // rather than a file preview: a single folder is one unnamed grid, and
    // an empty folder (which has nothing to list) falls back to its folder
    // icon with the name below.
    m_listing = DirectoryView::isDirectory(itemUrl)
        && !DirectoryView::isEmpty(DirectoryView::localUrlFor(itemUrl));
    if (m_listing) {
        m_preview->hide();
        m_preview->clearPixmap();
        // Hiding the media widget stops its player and synchronously emits
        // hasVideoChanged(false), which would re-show the previous item's
        // poster over the listing; the guard in slotHasVideoChanged() keeps
        // it down.
        m_mediaWidget->hide();
        m_mediaWidget->clearUrl();
        if (m_previewJob) {
            m_previewJob->kill();
            m_previewJob = nullptr;
        }
        m_directoryView->show();
        // A resize (or an item swap back and forth) must not re-list a
        // directory that is already shown.
        if (m_directoryUrl != DirectoryView::localUrlFor(itemUrl)) {
            m_directoryUrl = DirectoryView::localUrlFor(itemUrl);
            m_listingItems.clear();
            m_directoryView->setUrl(itemUrl);
        }
        refreshDisplaySize();
        return;
    }
    m_listing = false;
    m_directoryUrl.clear();
    m_listingItems.clear();
    // The single-item preview takes back the preview area; a listing left
    // visible (a folder previewed, or a multi-selection item list, before
    // this item) would linger over it.
    m_directoryView->hide();
    updateCountLabel();

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

    // Quick Look always previews every content type an installed thumbnailer
    // can handle, independent of the [PreviewSettings] Plugins list in
    // kdeglobals (the per-content-type toggle that drives Dolphin's view
    // previews and Information Panel). A preview here is explicitly requested
    // for a single file, so honor the full set of thumbnailers rather than the
    // subset the user configured for Dolphin.
    const QStringList plugins = KIO::PreviewJob::availablePlugins();

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
    if (m_listing) {
        return;
    }
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
    // While listing a directory, the preview area is owned by the listing;
    // a MediaWidget::stop() (delivered synchronously from hide) must not
    // switch the preview poster back on over the listing.
    if (m_listing) {
        return;
    }
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
