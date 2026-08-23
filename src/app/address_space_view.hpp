#pragma once

#include "regmap/core/model.hpp"

#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

class AddressSpaceView final : public QWidget {
    Q_OBJECT

public:
    explicit AddressSpaceView(QWidget* parent = nullptr);

    void setWorkspace(const regmap::Workspace* workspace);
    void setSelection(
        const std::string& pageId,
        const std::string& blockId);
    [[nodiscard]] std::uint32_t zoomLevel() const noexcept
    {
        return zoomLevel_;
    }
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void pageActivated(const QString& id);
    void blockActivated(const QString& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    struct BlockSegment {
        std::string id;
        QString name;
        std::uint64_t offset{0};
        std::uint64_t extent{1};
        bool declaredExtent{false};
        bool conflicting{false};
        bool outsidePageRange{false};
        bool fullyOutsidePageRange{false};
        std::optional<std::uint64_t> absoluteStart;
        std::optional<std::uint64_t> absoluteEnd;
    };

    struct PageLane {
        std::string id;
        QString name;
        std::uint64_t baseAddress{0};
        std::uint32_t addressWidth{32};
        std::uint64_t mappedSpan{0};
        std::uint64_t assignedBytes{0};
        std::vector<BlockSegment> blocks;
    };

    std::vector<PageLane> pages_;
    std::uint64_t displayedSpan_{1};
    std::size_t totalBlockCount_{0};
    std::size_t conflictingBlockCount_{0};
    std::size_t outOfRangeBlockCount_{0};
    std::string selectedPageId_;
    std::string selectedBlockId_;
    int hoveredPage_{-1};
    int hoveredBlock_{-1};
    std::uint32_t zoomLevel_{1};
    std::uint64_t viewStart_{0};

    [[nodiscard]] int labelWidth() const;
    [[nodiscard]] std::uint64_t visibleSpan() const;
    [[nodiscard]] QRect laneRect(std::size_t pageIndex) const;
    [[nodiscard]] QRect trackRect(std::size_t pageIndex) const;
    [[nodiscard]] QRect pageSpanRect(std::size_t pageIndex) const;
    [[nodiscard]] QRect blockRect(
        std::size_t pageIndex,
        std::size_t blockIndex) const;
    [[nodiscard]] int pageAt(const QPoint& position) const;
    [[nodiscard]] std::optional<std::pair<int, int>>
    blockAt(const QPoint& position) const;
    void updateHover(int pageIndex, int blockIndex);
    void setZoomLevel(std::uint32_t level);
    void focusSelectedBlock();
    void activatePage(std::size_t pageIndex);
    void activateBlock(
        std::size_t pageIndex,
        std::size_t blockIndex);
};
