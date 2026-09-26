/*
    SPDX-FileCopyrightText: 2009-2010 Peter Penz <peter.penz19@gmail.com>
    SPDX-FileCopyrightText: 2026 Quick Look Contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <KFileItem>

#include <QMargins>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QWidget>

class KFileItemList;
class MediaWidget;
class PixmapViewer;
class QLabel;
class QPixmap;
class QTimer;

namespace KIO
{
class PreviewJob;
}

/**
 * Displays a Quick Look preview for a file item: a pixmap / media player for the
 * content, plus the item name and a "1 of N" badge for multi-selections.
 *
 * This is a port of Dolphin's InformationPanelContent limited to the preview half
 * (the Baloo metadata pane is out of scope for Quick Look v1).
 */
class PreviewContent : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewContent(QWidget *parent = nullptr);
    ~PreviewContent() override;

    /**
     * Shows the preview for the items in \a items. The first item is previewed and
     * a "1 of N" badge is shown when \a items contains more than one item.
     */
    void showItems(const KFileItemList &items);

    /**
     * Refreshes the preview display for the current item (used after a resize).
     */
    void refreshPreview();

    /**
     * Sets the auto play media mode; events playback when turned on, but does not
     * stop it when turned off.
     */
    void setPreviewAutoPlay(bool autoPlay);

    /**
     * Restricts the display size to \a max; the window calls this so the
     * content and the window agree on the screen-percentage cap.
     */
    void setMaximumContentSize(const QSize &max);

    /**
     * The frame padding the window draws around this content. The window
     * subtracts it from the screen-percentage cap before capping, so a
     * preview sized at the cap never gets clipped by the frame.
     */
    void setFramePadding(const QMargins &margins);

    QSize sizeHint() const override;

protected:
    void showEvent(QShowEvent *event) override;

public Q_SLOTS:
    /**
     * Stops any running media playback (called when the window is hidden).
     */
    void pauseMedia();

Q_SIGNALS:
    void contentChanged();
    void currentIndexChanged(int index);
    // Emitted when the preview's display size changes so the window can resize
    // itself to fit the content (macOS Quick Look sizes the window to the image).
    void sizeHintChanged();

private Q_SLOTS:
    void showIcon(const KFileItem &item);
    void showPreview(const KFileItem &item, const class QPixmap &pixmap);
    void markOutdatedPreview();
    void slotHasVideoChanged(bool hasVideo);

private:
    void refreshPixmapView();
    void refreshDisplaySize();
    void adjustWidgetSizes();
    void setNameLabelText(const QString &text);
    void updateCountLabel();
    void showCurrentItem();

    KFileItem m_item;
    KFileItemList m_items;
    int m_currentIndex = 0;

    QPointer<KIO::PreviewJob> m_previewJob;
    QTimer *m_outdatedPreviewTimer;

    PixmapViewer *m_preview;
    MediaWidget *m_mediaWidget;
    QLabel *m_nameLabel;
    QLabel *m_countLabel;

    bool m_isVideo;
    QUrl m_disabledPreviewUrl;

    // Cap on the display size, set by the owning window (screen-percentage).
    QSize m_maxContentSize;
    // Frame padding around the content, set by the owning window; accounted
    // for so the cap leaves room for the frame (see sizeHint()).
    QMargins m_framePadding;
    // The size the preview area is currently shown at; drives sizeHint() so the
    // window fits the content.
    QSize m_displaySize = QSize{640, 480};
};
