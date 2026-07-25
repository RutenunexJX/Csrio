#pragma once

#include "regmap/core/model.hpp"

#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <utility>

class BitfieldView final : public QWidget {
    Q_OBJECT

public:
    explicit BitfieldView(QWidget* parent = nullptr);

    void setRegister(const regmap::Register* reg);
    void setSelectedField(const regmap::Field* field);

signals:
    void fieldActivated(const QString& fieldId);
    void fieldDragPreview(
        const QString& fieldId,
        std::uint32_t lsb,
        std::uint32_t msb);
    void fieldMoveRequested(
        const QString& fieldId,
        std::uint32_t lsb,
        std::uint32_t msb);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::optional<regmap::Register> register_;
    regmap::ObjectId selectedFieldId_;
    QVector<std::pair<QRect, QString>> hitRegions_;
    QString draggedFieldId_;
    std::optional<std::uint32_t> previewLsb_;
    std::uint32_t draggedWidth_ {0};
    std::uint32_t anchorFromLsb_ {0};
    bool dragging_ {false};

    [[nodiscard]] const regmap::Field* fieldById(const QString& id) const;
    [[nodiscard]] std::uint32_t bitAtX(qreal x) const;
    void updateDrag(qreal x);
};
