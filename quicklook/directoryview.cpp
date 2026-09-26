/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "directoryview.h"

#include <KDirLister>
#include <KDirModel>
#include <KFileItem>
#include <KIconUtils>
#include <KIO/PreviewJob>

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QIcon>
#include <QPaintEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
constexpr int CELL_SIZE = 96;
constexpr int CELL_SPACING = 8;
constexpr int ICON_SIZE = 64;
constexpr int LABEL_MAX_LINES = 2;
constexpr int VIEW_COLUMNS = 3;
constexpr int VIEW_ROWS = 2;

QUrl localUrlForInternal(const QUrl &url)
{
    // The desktop sends desktop:/ URLs (see plasma-desktop
    // DesktopSchemeHelper); the desktop:/ scheme has no KIO listing support, so
    // resolve it to the matching file:// URL of the home desktop directory.
    // The desktop:/ path is a plain relative path ("desktop:/My Folder/" ->
    // <DesktopLocation>/My Folder), so plain string concatenation is the
    // reliable way to build the file URL (QUrl setPath on a non-file URL does
    // not round-trip through toLocalFile()).
    if (url.scheme() == QLatin1String("desktop")) {
        const QString desktopDir = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        const QString path = url.toString(QUrl::RemoveScheme); // "/..."
        return QUrl::fromLocalFile(QDir::cleanPath(path.isEmpty() ? desktopDir : desktopDir + path));
    }
    return url;
}

/**
 * One listing cell: the item icon centered in the top part and the item name
 * (up to two centered, word-wrapped lines) beneath it, fixed at CELL_SIZE.
 */
class Cell : public QWidget
{
public:
    explicit Cell(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(CELL_SIZE, CELL_SIZE);
    }

    void setIcon(const QPixmap &pix)
    {
        m_icon = pix;
        update();
    }

    void setText(const QString &text)
    {
        m_lines = buildLines(text);
        update();
    }

    // The y position of the first label line. Defaults to ICON_SIZE + 2 (the
    // directory listing); a larger value opens a gap between the icon and the
    // label (set for the direct-items listing so previewed files keep a margin
    // from the label).
    void setLabelTop(int y)
    {
        m_labelTop = y;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        if (!m_icon.isNull()) {
            p.drawPixmap((width() - m_icon.width()) / 2, CELL_SPACING / 2, m_icon);
        }
        // The listing sits on the modal's dark translucent background, so the
        // label is fixed to white regardless of the application palette.
        p.setPen(Qt::white);
        const QFontMetrics fm(labelFont());
        for (int i = 0; i < qMin(LABEL_MAX_LINES, m_lines.size()); ++i) {
            p.drawText(QRect(4, m_labelTop + i * fm.lineSpacing(), width() - 8, fm.lineSpacing()),
                       Qt::AlignHCenter | Qt::AlignVCenter,
                       m_lines.at(i));
        }
    }

private:
    // The label is slightly smaller than the default UI font, matching the
    // desktop's folder listing cells.
    QFont labelFont() const
    {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() * 0.9);
        return f;
    }

    QStringList buildLines(const QString &text) const
    {
        QFontMetrics fm(labelFont());

        QStringList lines;
        const int space = text.lastIndexOf(QChar::Space, int(text.length() / 2));
        if (fm.horizontalAdvance(text) > width() - 8 && space > 0) {
            lines.append(text.left(space));
            lines.append(fm.elidedText(text.mid(space + 1), Qt::ElideRight, width() - 8));
        } else {
            lines.append(fm.elidedText(text, Qt::ElideMiddle, width() - 8));
        }
        return lines.mid(0, LABEL_MAX_LINES);
    }

    QPixmap m_icon;
    QStringList m_lines;
    int m_labelTop = ICON_SIZE + 2;
};

/**
 * Sorts the listing directories-first, then alphabetically by name (the desktop
 * listing default).
 */
class SortProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        const KFileItem li = sourceModel()->data(mapToSource(left), KDirModel::FileItemRole).value<KFileItem>();
        const KFileItem ri = sourceModel()->data(mapToSource(right), KDirModel::FileItemRole).value<KFileItem>();
        const bool leftDir = li.isDir();
        const bool rightDir = ri.isDir();
        if (leftDir != rightDir) {
            return leftDir;
        }
        return sourceModel()->data(mapToSource(left), Qt::DisplayRole).toString()
            < sourceModel()->data(mapToSource(right), Qt::DisplayRole).toString();
    }
};
}

DirectoryView::DirectoryView(QWidget *parent)
    : QWidget(parent)
    , m_dirLister(new KDirLister(this))
    , m_dirModel(new KDirModel(this))
    , m_proxy(new SortProxy(this))
    , m_scroll(nullptr)
    , m_content(nullptr)
    , m_layout(nullptr)
    , m_cols(VIEW_COLUMNS)
    , m_preferredSize(CELL_SIZE * VIEW_COLUMNS + CELL_SPACING * (VIEW_COLUMNS - 1),
                      CELL_SIZE * VIEW_ROWS + CELL_SPACING * (VIEW_ROWS - 1))
{
    m_dirLister->setDelayedMimeTypes(true);
    m_dirModel->setDirLister(m_dirLister);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setSortLocaleAware(true);
    m_proxy->setDynamicSortFilter(true);
    m_proxy->setSortRole(Qt::DisplayRole);
    m_proxy->setSourceModel(m_dirModel);

    // A QListView cannot wrap first in reading order (Qt's LeftToRight flow is
    // one unbounded row), so the listing is a fixed-width grid of cell widgets
    // in a QGridLayout inside a scroll area: cells fill a row and wrap to the
    // next, the grid width is fixed, and only the height grows.
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // The listing is a transparent view over the modal background: PreviewContent
    // paints the modal's dark background on its (native) surface in every mode,
    // so this widget must not paint another copy behind the cells - stacking the
    // translucent modal color would shade the listing darker than the rest of
    // the preview (the modal's padding).
    QPalette scrollPalette = m_scroll->palette();
    scrollPalette.setColor(QPalette::Base, Qt::transparent);
    m_scroll->setPalette(scrollPalette);

    m_content = new QWidget;
    m_layout = new QGridLayout(m_content);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(CELL_SPACING);

    m_scroll->setWidget(m_content);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_scroll);

    // The empty listing is the natural window state; the placeholder stays
    // visible until the first rows arrive (loadingStarted/Finished drive the
    // window's own placeholder where it wants one).
    connect(m_dirLister, &KDirLister::started, this, [this] {
        rebuildCells();
        recalcHeight();
        Q_EMIT loadingStarted();
    });
    connect(m_dirLister, &KCoreDirLister::completed, this, [this] {
        m_proxy->sort(0, Qt::AscendingOrder);
        rebuildCells();
        // The listing is in: fetch the previews the cells are missing.
        requestPreviews();
        Q_EMIT loadingFinished();
    });
}

DirectoryView::~DirectoryView()
{
    cancelPreviewJobs();
}

void DirectoryView::rebuildCells()
{
    for (QWidget *cell : m_cells) {
        m_layout->removeWidget(cell);
    }
    qDeleteAll(m_cells);
    m_cells.clear();
    // Applies a preview already fetched for the item (the listing reuses the
    // cells on a reshow; a fresh one is requested in requestPreviews()).
    auto applyPreview = [this](Cell *cell, const KFileItem &fileItem, bool isDir) {
        if (!isDir) {
            const QPixmap preview = m_previewPixmaps.value(fileItem.url());
            if (!preview.isNull()) {
                cell->setIcon(preview);
            }
        }
    };

    if (!m_items.isEmpty()) {
        // The direct-items listing: one cell per given item, in the given
        // order, each folder as its folder icon and each file as its content
        // preview once fetched. The items are built from bare URLs (no stat,
        // see QuickLookWindow::previewUrls), so their isDir() is not
        // populated - the URL decides.
        for (int i = 0; i < m_items.count(); ++i) {
            const KFileItem &fileItem = m_items.at(i);
            const bool isDir = isDirectory(fileItem.url());
            auto *cell = new Cell(m_content);
            cell->setIcon(QIcon::fromTheme(isDir ? QStringLiteral("folder") : fileItem.iconName()).pixmap(QSize(ICON_SIZE, ICON_SIZE)));
            cell->setText(fileItem.text());
            // A 12px margin between the (content) preview and the label, matching
            // the icon bottom (CELL_SPACING/2 + ICON_SIZE): a content preview is
            // larger than a type icon, so the shared default label position would
            // crowd it.
            cell->setLabelTop(CELL_SPACING / 2 + ICON_SIZE + 12);
            applyPreview(cell, fileItem, isDir);
            m_layout->addWidget(cell, i / m_cols, i % m_cols);
            m_cells.append(cell);
        }
    } else {
        for (int i = 0; i < m_proxy->rowCount(); ++i) {
            const QModelIndex index = m_proxy->index(i, 0);
            auto *cell = new Cell(m_content);
            const QVariant iconData = index.data(Qt::DecorationRole);
            if (iconData.canConvert<QIcon>()) {
                cell->setIcon(iconData.value<QIcon>().pixmap(QSize(ICON_SIZE, ICON_SIZE)));
            }
            cell->setText(index.data(Qt::DisplayRole).toString());
            const KFileItem fileItem = index.data(KDirModel::FileItemRole).value<KFileItem>();
            applyPreview(cell, fileItem, !fileItem.isNull() && fileItem.isDir());
            m_layout->addWidget(cell, i / m_cols, i % m_cols);
            m_cells.append(cell);
        }
    }
}

void DirectoryView::recalcHeight()
{
    // The grid width (hence the column count) follows the widget width for a
    // user-resized window; relayout the cells when it changed.
    const int width = qMax(size().width(), m_preferredSize.width());
    // Columns that fit in the width (no trailing spacing after the last one).
    const int cols = qMax(1, (width + CELL_SPACING) / (CELL_SIZE + CELL_SPACING));
    if (cols != m_cols) {
        m_cols = cols;
        if (!m_cells.isEmpty()) {
            rebuildCells();
        }
    }

    const int count = m_items.count() ? m_items.count() : m_proxy->rowCount();
    // An empty (loading) listing keeps the default grid height; a listed
    // directory is one cell per item, wrapped into rows of `cols`.
    const int rows = count ? (count + m_cols - 1) / m_cols : VIEW_ROWS;

    // Pin the grid to the top-left when the viewport is wider/taller.
    for (int i = 0; i < m_layout->columnCount(); ++i) {
        m_layout->setColumnStretch(i, i == m_cols - 1);
    }
    for (int i = 0; i < m_layout->rowCount(); ++i) {
        m_layout->setRowStretch(i, i == rows - 1);
    }

    const QSize s{m_preferredSize.width(), rows * CELL_SIZE + (rows - 1) * CELL_SPACING};
    if (s != m_preferredSize) {
        m_preferredSize = s;
        Q_EMIT preferredSizeChanged();
    }
}

bool DirectoryView::previewsShown() const
{
    return m_previewsShown;
}

void DirectoryView::setPreviewsShown(bool show)
{
    if (show == m_previewsShown) {
        return;
    }
    m_previewsShown = show;
    if (show) {
        // Re-fetch: the listing may be complete (missing previews) or a new
        // toggle while one is in flight (the pending cells are re-requested).
        requestPreviews();
    } else {
        // The type icons are the model's decoration icons, already painted on
        // every cell, so dropping the previews and the in-flight jobs is a
        // no-op repaint: the cells show their icons as they did at build.
        cancelPreviewJobs();
        m_previewPixmaps.clear();
    }
}

void DirectoryView::requestPreviews()
{
    if (!m_previewsShown) {
        return;
    }
    KFileItemList pending;
    if (!m_items.isEmpty()) {
        // The direct-items listing: the files are the items themselves. They
        // are built from bare URLs (no stat), so the URL decides the
        // directory-ness (a folder's cell is its folder icon, no preview).
        for (const KFileItem &item : m_items) {
            if (isDirectory(item.url()) || m_previewJobs.contains(item.url()) || m_previewPixmaps.contains(item.url())) {
                continue;
            }
            pending << item;
        }
    } else {
        for (int i = 0; i < m_proxy->rowCount(); ++i) {
            const KFileItem item = m_proxy->index(i, 0).data(KDirModel::FileItemRole).value<KFileItem>();
            if (item.isNull() || item.isDir() || m_previewJobs.contains(item.url()) || m_previewPixmaps.contains(item.url())) {
                continue;
            }
            pending << item;
        }
    }
    if (pending.isEmpty()) {
        return;
    }
    // One batched job covers every item of the listing (KIO runs them with the
    // thumbnailer's max worker count); the per-item gotPreview/failed update
    // each cell as its preview lands. Same job pattern as the single-item
    // preview in PreviewContent.
    const QStringList plugins = KIO::PreviewJob::availablePlugins();
    QPointer<KIO::PreviewJob> job = new KIO::PreviewJob(pending, QSize(ICON_SIZE, ICON_SIZE), &plugins);
    job->setScaleType(KIO::PreviewJob::Unscaled);
    job->setIgnoreMaximumSize(true);
    job->setDevicePixelRatio(devicePixelRatioF());
    for (const KFileItem &item : pending) {
        m_previewJobs.insert(item.url(), job);
    }
    connect(job.data(), &KIO::PreviewJob::gotPreview, this, [this](const KFileItem &item, const QPixmap &pixmap) {
        onItemPreview(item, pixmap);
    });
    // A failed item: nothing to do, the cell keeps its type icon.
    connect(job.data(), &KIO::PreviewJob::finished, this, [this, job]() {
        for (auto it = m_previewJobs.begin(); it != m_previewJobs.end(); ) {
            if (it.value() == job) {
                it = m_previewJobs.erase(it);
            } else {
                ++it;
            }
        }
    });
}

void DirectoryView::cancelPreviewJobs()
{
    const QList<QPointer<KIO::PreviewJob>> jobs = m_previewJobs.values();
    for (const QPointer<KIO::PreviewJob> &job : jobs) {
        if (job) {
            job->kill();
        }
    }
    m_previewJobs.clear();
}

void DirectoryView::onItemPreview(const KFileItem &item, const QPixmap &pixmap)
{
    // Only a URL that the current listing shows has a cell; a late preview for
    // an item no longer listed (its job was killed on reload) is dropped here.
    int cellIndex = -1;
    if (!m_items.isEmpty()) {
        // The direct-items listing keeps the given order.
        for (int i = 0; i < m_items.count(); ++i) {
            if (m_items.at(i).url() == item.url()) {
                cellIndex = i;
                break;
            }
        }
    } else {
        for (int i = 0; i < m_proxy->rowCount(); ++i) {
            const KFileItem cellItem = m_proxy->index(i, 0).data(KDirModel::FileItemRole).value<KFileItem>();
            if (cellItem.url() == item.url()) {
                cellIndex = i;
                break;
            }
        }
    }
    if (cellIndex < 0) {
        return;
    }
    QPixmap p = pixmap;
    if (!item.overlays().isEmpty()) {
        p = KIconUtils::addOverlays(p, item.overlays()).pixmap(QSize(ICON_SIZE, ICON_SIZE), devicePixelRatioF());
        p.setDevicePixelRatio(devicePixelRatioF());
    }
    m_previewPixmaps.insert(item.url(), p);
    if (cellIndex < m_cells.size()) {
        static_cast<Cell *>(m_cells.at(cellIndex))->setIcon(p);
    }
}

void DirectoryView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalcHeight();
}

QUrl DirectoryView::localUrlFor(const QUrl &url)
{
    return localUrlForInternal(url);
}

bool DirectoryView::isDirectory(const QUrl &url)
{
    const QUrl local = localUrlForInternal(url);
    if (!local.isLocalFile()) {
        return false;
    }
    return QFileInfo(local.toLocalFile()).isDir();
}

bool DirectoryView::isEmpty(const QUrl &localUrl)
{
    if (!localUrl.isLocalFile()) {
        return false;
    }
    return QDir(localUrl.toLocalFile()).isEmpty();
}

QSize DirectoryView::preferredSize() const
{
    return m_preferredSize;
}

void DirectoryView::reset()
{
    // The directory listing itself is left where it is: the model is not read
    // in the direct-items mode, setUrl() (re)opens the directory when a
    // directory listing is shown again, and it guards against re-listing a
    // directory that the model already lists.
    m_items.clear();
    cancelPreviewJobs();
    m_previewPixmaps.clear();
}

void DirectoryView::setUrl(const QUrl &url)
{
    // The view is read-only: a fresh listing is a full state reset. A new
    // directory also invalidates the in-flight previews and their cache: the
    // cells rebuilt for the new listing must not be painted with the previous
    // directory's previews.
    const QUrl local = localUrlFor(url);
    if (m_items.isEmpty() && m_previewDirectory == local) {
        return;
    }
    reset();
    m_previewDirectory = local;
    m_dirLister->openUrl(local, KDirLister::Reload);
}

void DirectoryView::setItems(const KFileItemList &items)
{
    // A direct-items listing has no directory behind it: the grid is built
    // straight from the items (in their given order), so the directory
    // listing is dropped. The items are the same selection the preview was
    // handed, so no re-sort (folders and files in selection order) and no
    // deduplication (a duplicated URL would be one cell per entry, the
    // same way the selection lists it).
    if (m_previewDirectory.isEmpty() && m_items == items) {
        return;
    }
    reset();
    m_items = items;
    rebuildCells();
    recalcHeight();
    requestPreviews();
}

QSize DirectoryView::sizeHint() const
{
    return m_preferredSize;
}

#include "moc_directoryview.cpp"
