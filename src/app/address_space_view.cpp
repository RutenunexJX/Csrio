#include "address_space_view.hpp"

#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QSizePolicy>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::optional<std::uint64_t> registerExtent(
    const regmap::Register& reg)
{
    Q_UNUSED(reg);
    return std::uint64_t{4};
}

[[nodiscard]] std::uint64_t inferredBlockExtent(
    const regmap::RegisterBlock& block)
{
    std::uint64_t extent = 0;
    for (const auto& reg : block.registers) {
        const auto current = registerExtent(reg);
        if (!current ||
            reg.offset >
                std::numeric_limits<std::uint64_t>::max() -
                    *current) {
            continue;
        }
        extent = std::max(
            extent,
            reg.offset + *current);
    }
    return std::max<std::uint64_t>(extent, 1);
}

[[nodiscard]] std::uint64_t saturatedEnd(
    const std::uint64_t offset,
    const std::uint64_t extent)
{
    if (extent == 0 ||
        offset >
            std::numeric_limits<std::uint64_t>::max() -
                extent) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return offset + extent;
}

[[nodiscard]] QString hex(const std::uint64_t value)
{
    return QStringLiteral("0x%1").arg(
        QString::number(value, 16).toUpper());
}

[[nodiscard]] QString fromUtf8(std::string_view value)
{
    return QString::fromUtf8(
        value.data(),
        static_cast<qsizetype>(value.size()));
}

} // namespace

AddressSpaceView::AddressSpaceView(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("addressSpaceView"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(80);
    setSizePolicy(
        QSizePolicy::Expanding,
        QSizePolicy::Fixed);
    setAccessibleName(
        QStringLiteral("Workspace address map"));
    setToolTip(
        QStringLiteral(
            "All Pages use one Block-offset scale. Click a Page lane or Block to locate it. Ctrl+wheel or +/- zooms; 0 resets; F focuses the selected Block."));
}

void AddressSpaceView::setWorkspace(
    const regmap::Workspace* workspace)
{
    pages_.clear();
    displayedSpan_ = 1;
    totalBlockCount_ = 0;
    conflictingBlockCount_ = 0;
    outOfRangeBlockCount_ = 0;
    hoveredPage_ = -1;
    hoveredBlock_ = -1;
    viewStart_ = 0;

    if (workspace == nullptr) {
        selectedPageId_.clear();
        selectedBlockId_.clear();
        setAccessibleDescription({});
        setVisible(false);
        updateGeometry();
        update();
        return;
    }

    pages_.reserve(workspace->addressSpaces.size());
    for (const auto& page : workspace->addressSpaces) {
        PageLane lane;
        lane.id = page.id;
        lane.name = fromUtf8(page.name);
        lane.baseAddress = page.baseAddress;
        lane.addressWidth = page.addressWidth;
        lane.blocks.reserve(page.blocks.size());

        for (const auto& block : page.blocks) {
            const bool declaredExtent =
                block.size.has_value() &&
                *block.size > 0;
            const std::uint64_t extent =
                declaredExtent
                    ? *block.size
                    : inferredBlockExtent(block);
            const std::uint64_t localEnd =
                saturatedEnd(
                    block.baseAddress,
                    extent);

            BlockSegment segment;
            segment.id = block.id;
            segment.name = fromUtf8(block.name);
            segment.offset = block.baseAddress;
            segment.extent = extent;
            segment.declaredExtent = declaredExtent;

            if (page.baseAddress <=
                std::numeric_limits<std::uint64_t>::max() -
                    block.baseAddress) {
                segment.absoluteStart =
                    page.baseAddress +
                    block.baseAddress;
                if (extent > 0 &&
                    *segment.absoluteStart <=
                        std::numeric_limits<std::uint64_t>::max() -
                            (extent - 1)) {
                    segment.absoluteEnd =
                        *segment.absoluteStart +
                        (extent - 1);
                }
            }

            segment.fullyOutsidePageRange =
                !segment.absoluteStart.has_value();
            segment.outsidePageRange =
                !segment.absoluteEnd.has_value();
            if (page.addressWidth > 0 &&
                page.addressWidth < 64) {
                const std::uint64_t limit =
                    std::uint64_t{1} <<
                    page.addressWidth;
                segment.fullyOutsidePageRange =
                    !segment.absoluteStart ||
                    *segment.absoluteStart >= limit;
                segment.outsidePageRange =
                    !segment.absoluteEnd ||
                    *segment.absoluteEnd >= limit;
            }

            lane.mappedSpan =
                std::max(
                    lane.mappedSpan,
                    localEnd);
            lane.blocks.push_back(
                std::move(segment));
        }

        for (std::size_t left = 0;
             left < lane.blocks.size(); ++left) {
            const std::uint64_t leftEnd =
                saturatedEnd(
                    lane.blocks[left].offset,
                    lane.blocks[left].extent);
            for (std::size_t right = left + 1;
                 right < lane.blocks.size(); ++right) {
                const std::uint64_t rightEnd =
                    saturatedEnd(
                        lane.blocks[right].offset,
                        lane.blocks[right].extent);
                if (std::max(
                        lane.blocks[left].offset,
                        lane.blocks[right].offset) <
                    std::min(leftEnd, rightEnd)) {
                    lane.blocks[left].conflicting = true;
                    lane.blocks[right].conflicting = true;
                }
            }
        }

        std::vector<
            std::pair<std::uint64_t, std::uint64_t>>
            intervals;
        intervals.reserve(lane.blocks.size());
        for (const auto& block : lane.blocks) {
            const std::uint64_t end =
                saturatedEnd(
                    block.offset,
                    block.extent);
            if (end > block.offset) {
                intervals.emplace_back(
                    block.offset, end);
            }
            conflictingBlockCount_ +=
                static_cast<std::size_t>(
                    block.conflicting);
            outOfRangeBlockCount_ +=
                static_cast<std::size_t>(
                    block.outsidePageRange);
        }
        std::ranges::sort(intervals);
        std::uint64_t mergedStart = 0;
        std::uint64_t mergedEnd = 0;
        bool hasMerged = false;
        const auto addAssigned =
            [&lane](const std::uint64_t start,
                    const std::uint64_t end) {
                const std::uint64_t length = end - start;
                if (lane.assignedBytes >
                    std::numeric_limits<std::uint64_t>::max() -
                        length) {
                    lane.assignedBytes =
                        std::numeric_limits<std::uint64_t>::max();
                } else {
                    lane.assignedBytes += length;
                }
            };
        for (const auto& [start, end] : intervals) {
            if (!hasMerged) {
                mergedStart = start;
                mergedEnd = end;
                hasMerged = true;
            } else if (start > mergedEnd) {
                addAssigned(
                    mergedStart, mergedEnd);
                mergedStart = start;
                mergedEnd = end;
            } else {
                mergedEnd =
                    std::max(mergedEnd, end);
            }
        }
        if (hasMerged) {
            addAssigned(
                mergedStart, mergedEnd);
        }

        totalBlockCount_ += lane.blocks.size();
        displayedSpan_ =
            std::max(
                displayedSpan_,
                lane.mappedSpan);
        pages_.push_back(std::move(lane));
    }

    QString description =
        QStringLiteral(
            "Workspace address map; %1 Page(s); %2 Block(s); common Block-offset scale ends at %3")
            .arg(pages_.size())
            .arg(totalBlockCount_)
            .arg(hex(displayedSpan_ - 1));
    if (conflictingBlockCount_ > 0) {
        description +=
            QStringLiteral(
                "; %1 overlapping Block(s)")
                .arg(conflictingBlockCount_);
    }
    if (outOfRangeBlockCount_ > 0) {
        description +=
            QStringLiteral(
                "; %1 Block(s) exceed their Page address range")
                .arg(outOfRangeBlockCount_);
    }
    description +=
        QStringLiteral(
            "; use Left and Right to browse Blocks, Home and End to jump, and Enter to locate");
    setAccessibleDescription(description);
    setVisible(true);
    updateGeometry();
    update();
}

void AddressSpaceView::setSelection(
    const std::string& pageId,
    const std::string& blockId)
{
    std::string resolvedPage = pageId;
    if (!blockId.empty()) {
        for (const auto& page : pages_) {
            if (std::ranges::any_of(
                    page.blocks,
                    [&blockId](const BlockSegment& block) {
                        return block.id == blockId;
                    })) {
                resolvedPage = page.id;
                break;
            }
        }
    }
    if (selectedPageId_ == resolvedPage &&
        selectedBlockId_ == blockId) {
        return;
    }
    selectedPageId_ = std::move(resolvedPage);
    selectedBlockId_ = blockId;
    if (zoomLevel_ > 1 &&
        !selectedBlockId_.empty()) {
        focusSelectedBlock();
    }
    update();
}

void AddressSpaceView::setZoomLevel(
    const std::uint32_t level)
{
    const std::uint32_t clamped =
        std::clamp<std::uint32_t>(
            level, 1, 32);
    if (zoomLevel_ == clamped) {
        return;
    }
    zoomLevel_ = clamped;
    if (zoomLevel_ == 1) {
        viewStart_ = 0;
    } else {
        focusSelectedBlock();
    }
    update();
}

void AddressSpaceView::focusSelectedBlock()
{
    const std::uint64_t span =
        visibleSpan();
    if (zoomLevel_ == 1 ||
        selectedBlockId_.empty()) {
        viewStart_ = 0;
        return;
    }
    for (const auto& page : pages_) {
        const auto block =
            std::ranges::find(
                page.blocks,
                selectedBlockId_,
                &BlockSegment::id);
        if (block == page.blocks.end()) {
            continue;
        }
        const std::uint64_t halfExtent =
            block->extent / 2;
        const std::uint64_t center =
            block->offset >
                    std::numeric_limits<std::uint64_t>::max() -
                        halfExtent
                ? std::numeric_limits<std::uint64_t>::max()
                : block->offset + halfExtent;
        const std::uint64_t proposed =
            center > span / 2
                ? center - span / 2
                : 0;
        const std::uint64_t maximumStart =
            displayedSpan_ > span
                ? displayedSpan_ - span
                : 0;
        viewStart_ =
            std::min(proposed, maximumStart);
        return;
    }
    viewStart_ = 0;
}

QSize AddressSpaceView::sizeHint() const
{
    const int requestedHeight =
        52 +
        static_cast<int>(pages_.size()) * 30;
    return QSize(
        720,
        std::max(requestedHeight, 80));
}

int AddressSpaceView::labelWidth() const
{
    return std::clamp(
        width() / 4,
        96,
        260);
}

std::uint64_t AddressSpaceView::visibleSpan() const
{
    const std::uint64_t quotient =
        displayedSpan_ / zoomLevel_;
    const std::uint64_t remainder =
        displayedSpan_ % zoomLevel_;
    return std::max<std::uint64_t>(
        1,
        quotient +
            static_cast<std::uint64_t>(
                remainder != 0));
}

QRect AddressSpaceView::laneRect(
    const std::size_t pageIndex) const
{
    if (pageIndex >= pages_.size()) {
        return {};
    }
    constexpr int top = 28;
    constexpr int bottom = 20;
    const int available =
        std::max(1, height() - top - bottom);
    const int count =
        std::max(1, static_cast<int>(pages_.size()));
    const int laneTop =
        top +
        static_cast<int>(pageIndex) *
            available / count;
    const int laneBottom =
        top +
        (static_cast<int>(pageIndex) + 1) *
            available / count;
    return QRect(
        8,
        laneTop,
        std::max(1, width() - 16),
        std::max(1, laneBottom - laneTop - 2));
}

QRect AddressSpaceView::trackRect(
    const std::size_t pageIndex) const
{
    const QRect lane = laneRect(pageIndex);
    if (!lane.isValid()) {
        return {};
    }
    const int left = labelWidth() + 16;
    return QRect(
        left,
        lane.top() + 3,
        std::max(1, width() - left - 12),
        std::max(3, lane.height() - 6));
}

QRect AddressSpaceView::pageSpanRect(
    const std::size_t pageIndex) const
{
    if (pageIndex >= pages_.size() ||
        pages_[pageIndex].mappedSpan == 0) {
        return {};
    }
    const QRect track = trackRect(pageIndex);
    const std::uint64_t span =
        visibleSpan();
    const std::uint64_t visibleEnd =
        saturatedEnd(viewStart_, span);
    const std::uint64_t clippedStart =
        viewStart_;
    const std::uint64_t clippedEnd =
        std::min(
            visibleEnd,
            pages_[pageIndex].mappedSpan);
    if (clippedEnd <= clippedStart) {
        return {};
    }
    const int left =
        track.left();
    const int spanWidth =
        std::clamp(
            static_cast<int>(
                (static_cast<long double>(
                     clippedEnd - clippedStart) *
                 static_cast<long double>(track.width())) /
                static_cast<long double>(span)),
            3,
            track.width());
    return QRect(
        left,
        track.top(),
        spanWidth,
        track.height());
}

QRect AddressSpaceView::blockRect(
    const std::size_t pageIndex,
    const std::size_t blockIndex) const
{
    if (pageIndex >= pages_.size() ||
        blockIndex >= pages_[pageIndex].blocks.size() ||
        displayedSpan_ == 0) {
        return {};
    }
    const QRect track = trackRect(pageIndex);
    const BlockSegment& block =
        pages_[pageIndex].blocks[blockIndex];
    const std::uint64_t span =
        visibleSpan();
    const std::uint64_t visibleEnd =
        saturatedEnd(viewStart_, span);
    const std::uint64_t blockEnd =
        saturatedEnd(
            block.offset,
            block.extent);
    if (blockEnd <= viewStart_ ||
        block.offset >= visibleEnd) {
        return {};
    }
    const std::uint64_t start =
        std::max(block.offset, viewStart_);
    const std::uint64_t end =
        std::min(blockEnd, visibleEnd);
    const int left =
        std::clamp(
            track.left() +
                static_cast<int>(
                    (static_cast<long double>(start - viewStart_) *
                     static_cast<long double>(track.width())) /
                    static_cast<long double>(span)),
            track.left(),
            track.right());
    const int right =
        std::clamp(
            track.left() +
                static_cast<int>(
                    (static_cast<long double>(end - viewStart_) *
                     static_cast<long double>(track.width())) /
                    static_cast<long double>(span)),
            left + 1,
            track.right() + 1);
    return QRect(
        left,
        track.top(),
        std::min(
            track.right() - left + 1,
            std::max(3, right - left)),
        track.height());
}

int AddressSpaceView::pageAt(
    const QPoint& position) const
{
    for (std::size_t page = 0;
         page < pages_.size(); ++page) {
        if (laneRect(page).contains(position)) {
            return static_cast<int>(page);
        }
    }
    return -1;
}

std::optional<std::pair<int, int>>
AddressSpaceView::blockAt(
    const QPoint& position) const
{
    for (std::size_t page = 0;
         page < pages_.size(); ++page) {
        const auto& blocks = pages_[page].blocks;
        for (auto block = blocks.size();
             block > 0; --block) {
            const QRect painted =
                blockRect(page, block - 1);
            const QRect hit =
                painted.isValid()
                    ? painted.adjusted(-5, -2, 5, 2)
                    : QRect{};
            if (hit.contains(position)) {
                return std::pair{
                    static_cast<int>(page),
                    static_cast<int>(block - 1)};
            }
        }
    }
    return std::nullopt;
}

void AddressSpaceView::updateHover(
    const int pageIndex,
    const int blockIndex)
{
    if (hoveredPage_ == pageIndex &&
        hoveredBlock_ == blockIndex) {
        return;
    }
    hoveredPage_ = pageIndex;
    hoveredBlock_ = blockIndex;

    if (pageIndex >= 0 &&
        pageIndex < static_cast<int>(pages_.size()) &&
        blockIndex >= 0 &&
        blockIndex < static_cast<int>(
                         pages_[static_cast<std::size_t>(pageIndex)]
                             .blocks.size())) {
        const PageLane& page =
            pages_[static_cast<std::size_t>(pageIndex)];
        const BlockSegment& block =
            page.blocks[static_cast<std::size_t>(blockIndex)];
        QString tooltip =
            QStringLiteral(
                "Page %1 / Block %2 - Base Offset %3 - Size %4 B (%5)")
                .arg(
                    page.name,
                    block.name,
                    hex(block.offset))
                .arg(block.extent)
                .arg(
                    block.declaredExtent
                        ? QStringLiteral("configured")
                        : QStringLiteral("inferred from Registers"));
        if (block.absoluteStart &&
            block.absoluteEnd) {
            tooltip +=
                QStringLiteral(
                    " - Address %1 to %2")
                    .arg(
                        hex(*block.absoluteStart),
                        hex(*block.absoluteEnd));
        }
        if (block.conflicting) {
            tooltip +=
                QStringLiteral(
                    " - Block overlap");
        }
        if (block.fullyOutsidePageRange) {
            tooltip +=
                QStringLiteral(
                    " - Outside Page address range");
        } else if (block.outsidePageRange) {
            tooltip +=
                QStringLiteral(
                    " - Extends past Page address range");
        }
        setToolTip(tooltip);
        setCursor(Qt::PointingHandCursor);
    } else if (
        pageIndex >= 0 &&
        pageIndex < static_cast<int>(pages_.size())) {
        const PageLane& page =
            pages_[static_cast<std::size_t>(pageIndex)];
        setToolTip(
            QStringLiteral(
                "Page %1 - Base %2 - mapped span %3 B - %4 Block(s) - %5-bit address width")
                .arg(
                    page.name,
                    hex(page.baseAddress))
                .arg(page.mappedSpan)
                .arg(page.blocks.size())
                .arg(page.addressWidth));
        setCursor(Qt::PointingHandCursor);
    } else {
        setToolTip(
            QStringLiteral(
                "All Pages use one Block-offset scale. Blue marks Blocks, green marks the selected Block, and red marks overlap or Page-range errors. Ctrl+wheel or +/- zooms; 0 resets; F focuses the selected Block."));
        unsetCursor();
    }
    update();
}

void AddressSpaceView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(
        QPainter::Antialiasing,
        true);

    QString summary =
        QStringLiteral(
            "Address map - %1 Page(s) - %2 Block(s) - scale %3 to %4 - %5x")
            .arg(pages_.size())
            .arg(totalBlockCount_)
            .arg(
                hex(viewStart_),
                hex(saturatedEnd(
                        viewStart_,
                        visibleSpan()) -
                    1))
            .arg(zoomLevel_);
    if (conflictingBlockCount_ > 0) {
        summary +=
            QStringLiteral(
                " - %1 overlapping")
                .arg(conflictingBlockCount_);
    }
    if (outOfRangeBlockCount_ > 0) {
        summary +=
            QStringLiteral(
                " - %1 outside Page range")
                .arg(outOfRangeBlockCount_);
    }
    painter.setPen(
        QColor(QStringLiteral("#243447")));
    painter.drawText(
        QRect(10, 3, width() - 20, 21),
        Qt::AlignLeft | Qt::AlignVCenter,
        painter.fontMetrics().elidedText(
            summary,
            Qt::ElideRight,
            std::max(0, width() - 20)));

    if (pages_.empty()) {
        painter.setPen(
            QColor(QStringLiteral("#526579")));
        painter.drawText(
            rect().adjusted(10, 28, -10, -10),
            Qt::AlignCenter,
            QStringLiteral(
                "No Pages"));
        return;
    }

    for (std::size_t pageIndex = 0;
         pageIndex < pages_.size(); ++pageIndex) {
        const PageLane& page = pages_[pageIndex];
        const QRect lane = laneRect(pageIndex);
        const QRect track = trackRect(pageIndex);
        const bool selectedPage =
            page.id == selectedPageId_;

        painter.setPen(
            selectedPage
                ? QPen(
                      QColor(QStringLiteral("#70AD47")),
                      2)
                : QPen(
                      QColor(QStringLiteral("#D5DEE8")),
                      1));
        painter.setBrush(
            selectedPage
                ? QColor(QStringLiteral("#F1F8EC"))
                : QColor(QStringLiteral("#F8FAFC")));
        painter.drawRoundedRect(
            lane,
            3,
            3);

        const QRect label(
            lane.left() + 5,
            lane.top(),
            std::max(1, labelWidth() - 8),
            lane.height());
        const QString span =
            page.mappedSpan == 0
                ? QStringLiteral("empty")
                : hex(page.mappedSpan);
        const QString pageText =
            QStringLiteral(
                "%1 | Base %2 | Span %3 | %4-bit")
                .arg(
                    page.name,
                    hex(page.baseAddress),
                    span)
                .arg(page.addressWidth);
        painter.setPen(
            QColor(QStringLiteral("#243447")));
        painter.drawText(
            label,
            Qt::AlignLeft | Qt::AlignVCenter,
            painter.fontMetrics().elidedText(
                pageText,
                Qt::ElideRight,
                label.width()));

        painter.setPen(
            QPen(
                QColor(QStringLiteral("#AAB7C7")),
                1));
        painter.setBrush(
            QColor(QStringLiteral("#EDF2F7")));
        painter.drawRoundedRect(
            track,
            2,
            2);

        const QRect spanRectangle =
            pageSpanRect(pageIndex);
        if (spanRectangle.isValid()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(
                QColor(QStringLiteral("#DCE6F1")));
            painter.drawRoundedRect(
                spanRectangle.adjusted(
                    1, 1, -1, -1),
                2,
                2);
        }

        for (std::size_t blockIndex = 0;
             blockIndex < page.blocks.size(); ++blockIndex) {
            const BlockSegment& block =
                page.blocks[blockIndex];
            QRect rectangle =
                blockRect(
                    pageIndex,
                    blockIndex)
                    .adjusted(1, 1, -1, -1);
            if (!rectangle.isValid()) {
                continue;
            }
            QColor fill =
                block.conflicting ||
                        block.outsidePageRange
                    ? QColor(QStringLiteral("#B3261E"))
                    : (block.id == selectedBlockId_
                           ? QColor(QStringLiteral("#70AD47"))
                           : QColor(QStringLiteral("#4472C4")));
            if (hoveredPage_ ==
                    static_cast<int>(pageIndex) &&
                hoveredBlock_ ==
                    static_cast<int>(blockIndex)) {
                fill = fill.lighter(120);
            }
            const bool selectedBlock =
                block.id == selectedBlockId_;
            painter.setPen(
                QPen(
                    selectedBlock
                        ? QColor(QStringLiteral("#1F4E2B"))
                        : QColor(QStringLiteral("#FFFFFF")),
                    selectedBlock ? 2 : 1,
                    block.declaredExtent
                        ? Qt::SolidLine
                        : Qt::DashLine));
            painter.setBrush(fill);
            painter.drawRoundedRect(
                rectangle,
                2,
                2);
            if (rectangle.width() >= 36 &&
                rectangle.height() >= 12) {
                painter.setPen(Qt::white);
                const QString labelText =
                    block.conflicting ||
                            block.outsidePageRange
                        ? QStringLiteral("! | %1")
                              .arg(block.name)
                        : block.name;
                painter.drawText(
                    rectangle.adjusted(
                        3, 0, -3, 0),
                    Qt::AlignCenter,
                    painter.fontMetrics().elidedText(
                        labelText,
                        Qt::ElideRight,
                        std::max(
                            0,
                            rectangle.width() - 6)));
            }
        }
    }

    const QRect lastTrack =
        trackRect(pages_.size() - 1);
    painter.setPen(
        QColor(QStringLiteral("#526579")));
    const QRect scaleLine(
        lastTrack.left(),
        height() - 18,
        lastTrack.width(),
        16);
    painter.drawText(
        scaleLine,
        Qt::AlignLeft | Qt::AlignVCenter,
        hex(viewStart_));
    painter.drawText(
        scaleLine,
        Qt::AlignRight | Qt::AlignVCenter,
        hex(saturatedEnd(
                viewStart_,
                visibleSpan()) -
            1));

    if (hasFocus()) {
        painter.setPen(
            QPen(
                palette().highlight().color(),
                2,
                Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(
            rect().adjusted(2, 2, -2, -2),
            4,
            4);
    }
}

void AddressSpaceView::mouseMoveEvent(
    QMouseEvent* event)
{
    const QPoint position =
        event->position().toPoint();
    if (const auto block = blockAt(position)) {
        updateHover(
            block->first,
            block->second);
    } else {
        updateHover(
            pageAt(position),
            -1);
    }
    QWidget::mouseMoveEvent(event);
}

void AddressSpaceView::mousePressEvent(
    QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        const QPoint position =
            event->position().toPoint();
        if (const auto block = blockAt(position)) {
            activateBlock(
                static_cast<std::size_t>(block->first),
                static_cast<std::size_t>(block->second));
            event->accept();
            return;
        }
        const int page = pageAt(position);
        if (page >= 0) {
            activatePage(
                static_cast<std::size_t>(page));
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void AddressSpaceView::keyPressEvent(
    QKeyEvent* event)
{
    if (event == nullptr) {
        return;
    }

    if (event->key() == Qt::Key_Plus ||
        event->key() == Qt::Key_Equal) {
        setZoomLevel(
            std::min<std::uint32_t>(
                32,
                zoomLevel_ * 2));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Minus) {
        setZoomLevel(
            std::max<std::uint32_t>(
                1,
                zoomLevel_ / 2));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_0) {
        setZoomLevel(1);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_F) {
        focusSelectedBlock();
        update();
        event->accept();
        return;
    }

    std::vector<
        std::pair<std::size_t, std::size_t>>
        ordered;
    for (std::size_t page = 0;
         page < pages_.size(); ++page) {
        for (std::size_t block = 0;
             block < pages_[page].blocks.size();
             ++block) {
            ordered.emplace_back(page, block);
        }
    }
    std::ranges::stable_sort(
        ordered,
        [this](const auto& left,
               const auto& right) {
            if (left.first != right.first) {
                return left.first < right.first;
            }
            const BlockSegment& leftBlock =
                pages_[left.first].blocks[left.second];
            const BlockSegment& rightBlock =
                pages_[right.first].blocks[right.second];
            return std::tie(
                       leftBlock.offset,
                       leftBlock.id) <
                std::tie(
                       rightBlock.offset,
                       rightBlock.id);
        });

    if (ordered.empty()) {
        if ((event->key() == Qt::Key_Return ||
             event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            !selectedPageId_.empty()) {
            const auto page =
                std::ranges::find(
                    pages_,
                    selectedPageId_,
                    &PageLane::id);
            if (page != pages_.end()) {
                activatePage(
                    static_cast<std::size_t>(
                        std::distance(
                            pages_.begin(),
                            page)));
                event->accept();
                return;
            }
        }
        QWidget::keyPressEvent(event);
        return;
    }

    const auto selected =
        std::ranges::find_if(
            ordered,
            [this](const auto& entry) {
                return pages_[entry.first]
                           .blocks[entry.second]
                           .id ==
                    selectedBlockId_;
            });
    const bool hasSelected =
        selected != ordered.end();
    const std::size_t selectedPosition =
        hasSelected
            ? static_cast<std::size_t>(
                  std::distance(
                      ordered.begin(),
                      selected))
            : 0;
    std::optional<std::size_t> target;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Up:
        target = hasSelected
            ? (selectedPosition == 0
                   ? 0
                   : selectedPosition - 1)
            : ordered.size() - 1;
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
        target = hasSelected
            ? std::min(
                  selectedPosition + 1,
                  ordered.size() - 1)
            : 0;
        break;
    case Qt::Key_Home:
        target = 0;
        break;
    case Qt::Key_End:
        target = ordered.size() - 1;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        target = hasSelected
            ? selectedPosition
            : 0;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    const auto [page, block] =
        ordered[*target];
    hoveredPage_ =
        static_cast<int>(page);
    hoveredBlock_ =
        static_cast<int>(block);
    activateBlock(page, block);
    setFocus(Qt::OtherFocusReason);
    event->accept();
}

void AddressSpaceView::wheelEvent(
    QWheelEvent* event)
{
    if (event != nullptr &&
        event->modifiers().testFlag(
            Qt::ControlModifier)) {
        if (event->angleDelta().y() > 0) {
            setZoomLevel(
                std::min<std::uint32_t>(
                    32,
                    zoomLevel_ * 2));
        } else if (
            event->angleDelta().y() < 0) {
            setZoomLevel(
                std::max<std::uint32_t>(
                    1,
                    zoomLevel_ / 2));
        }
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void AddressSpaceView::activatePage(
    const std::size_t pageIndex)
{
    if (pageIndex >= pages_.size()) {
        return;
    }
    selectedPageId_ = pages_[pageIndex].id;
    selectedBlockId_.clear();
    update();
    emit pageActivated(
        fromUtf8(selectedPageId_));
}

void AddressSpaceView::activateBlock(
    const std::size_t pageIndex,
    const std::size_t blockIndex)
{
    if (pageIndex >= pages_.size() ||
        blockIndex >= pages_[pageIndex].blocks.size()) {
        return;
    }
    selectedPageId_ = pages_[pageIndex].id;
    selectedBlockId_ =
        pages_[pageIndex].blocks[blockIndex].id;
    update();
    emit blockActivated(
        fromUtf8(selectedBlockId_));
}

void AddressSpaceView::leaveEvent(QEvent* event)
{
    updateHover(-1, -1);
    QWidget::leaveEvent(event);
}
