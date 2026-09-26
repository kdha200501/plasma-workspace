/*
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KFileItem>

#include <QHash>
#include <QMap>
#include <QPointer>
#include <QPixmap>
#include <QResizeEvent>
#include <QSize>
#include <QUrl>
#include <QWidget>

class KDirLister;
class KDirModel;
class QGridLayout;
class QScrollArea;
class QSortFilterProxyModel;

namespace KIO
{
class PreviewJob;
}

/**
 * Lists the contents of a single directory as a wrapping grid of icons and names
 * (the CSS `display: flex; flex-wrap: wrap` equivalent).
 *
 * This is the Quick Look port of the desktop's folder popup listing
 * (plasma-desktop FolderView/FolderViewDialog), reduced to the listing half only
 * (no selection, drag, rename, or open-on-click behavior). It mirrors the
 * FolderModel listing mechanism: a KDirLister feeding a KDirModel, opened with
 * openUrl() for the directory and displayed through a read-only icon view.
 *
 * The grid has a fixed width and wraps cells row-first, so a directory of any
 * size shows its full width at once and only grows (and scrolls) vertically.
 */
class DirectoryView : public QWidget
{
    Q_OBJECT

public:
    explicit DirectoryView(QWidget *parent = nullptr);
    ~DirectoryView() override;

    /**
     * Lists the contents of the directory at \a url, replacing any previous
     * listing.
     */
    void setUrl(const QUrl &url);

    /**
     * Lists the given items themselves, replacing any previous listing: the
     * items are shown in the given order as one grid (no directory listing,
     * no sort) - each folder as its folder icon, each file as its content
     * preview when one can be generated for its type.
     */
    void setItems(const KFileItemList &items);

    /**
     * The listable local URL behind \a url: a desktop:/ URL is resolved to the
     * matching file:// URL (the desktop:/ scheme has no KDirLister support), any
     * other URL is returned unchanged.
     */
    static QUrl localUrlFor(const QUrl &url);

    /**
     * True when \a url points to a local directory (desktop:/ URLs resolved,
     * other schemes always false).
     */
    static bool isDirectory(const QUrl &url);

    /**
     * True when the (already local) directory at \a localUrl contains no
     * entries, i.e. listing it would show nothing.
     */
    static bool isEmpty(const QUrl &localUrl);

    /**
     * The size the listing is displayed at: a fixed-width grid of cells that
     * wraps and grows its height with the row count, the size the owning window
     * sizes itself to for a directory preview.
     */
    QSize preferredSize() const;

    QSize sizeHint() const override;

    /**
     * Whether supported items are rendered as previews of their own content
     * (e.g. a PNG file renders the PNG) instead of as their type icon, matching
     * Dolphin's "Show Previews". Default true; turning previews off replaces
     * the current listing's previews with the type icons.
     */
    bool previewsShown() const;
    void setPreviewsShown(bool show);

protected:
    void resizeEvent(QResizeEvent *event) override;

Q_SIGNALS:
    void loadingStarted();
    void loadingFinished();
    void preferredSizeChanged();

private:
    // Clears any previous listing state (the directory model, the in-flight
    // previews and their cache, the directly listed items) in preparation for
    // a listing replacement (setUrl / setItems).
    void reset();

    // (Re)builds the grid cells from the currently listed items (the directly
    // listed items in their given order, or the sorted proxy model), wrapping
    // them into rows of the current column count.
    void rebuildCells();

    // Recomputes the preferred height once the full listing is laid out and
    // emits preferredSizeChanged() when it moved (a directory of many entries
    // wraps into more rows than the 3x2 default grid). Also relayouts the cells
    // when the column count changes with a resize.
    void recalcHeight();

    // Starts the content previews for the items of the current listing (Dolphin
    // "Show Previews"): each supported file renders its own content as the cell
    // icon, unsupported ones keep the type icon.
    void requestPreviews();
    // Cancels any in-flight preview jobs.
    void cancelPreviewJobs();
    // A preview arrived for an item that is still listed; paint it on the cell.
    void onItemPreview(const KFileItem &item, const QPixmap &pixmap);

    // The items listed directly (no directory behind them): a non-empty list
    // puts the grid in direct-items mode, built from the items themselves in
    // their given order (see setItems()); empty is the directory mode.
    KFileItemList m_items;

    KDirLister *m_dirLister;
    KDirModel *m_dirModel;
    QSortFilterProxyModel *m_proxy;
    QScrollArea *m_scroll;
    QWidget *m_content;
    QGridLayout *m_layout;
    QList<QWidget *> m_cells;
    int m_cols = 3; // default column count (3x2 grid, see the .cpp)
    // The width is the fixed grid width; the height grows with the wrapped row
    // count. Set in the constructor and in recalcHeight().
    QSize m_preferredSize;

    // Content previews for the cells (Dolphin "Show Previews").
    bool m_previewsShown = true;
    QUrl m_previewDirectory; // the listing the in-flight previews belong to
    QMap<QUrl, QPointer<KIO::PreviewJob>> m_previewJobs;
    // Rendered previews for the current listing, by URL; applied on cell build
    // and on late arrival. Cleared on toggle-off and on setUrl.
    QHash<QUrl, QPixmap> m_previewPixmaps;
};
