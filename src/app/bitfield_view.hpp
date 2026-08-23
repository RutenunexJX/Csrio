#pragma once

#include "regmap/core/model.hpp"

#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

#include <cstdint>
#include <optional>
#include <utility>

class QEvent;
class QKeyEvent;

class BitfieldView final : public QWidget {
    Q_OBJECT

public:
    explicit BitfieldView(QWidget* parent = nullptr);

    void setRegister(const regmap::Register* reg);
    void setFieldContainer(
        const regmap::Field* container,
        const regmap::Field* selectedMember);
    void setSelectedField(const regmap::Field* field);

signals:
    void fieldActivated(const QString& fieldId);
    void fieldDragPreview(
        const QString& fieldId,
        std::uint32_t lsb,
        std::uint32_t msb);
    void fieldDragCancelled(
        const QString& fieldId);
    void fieldMoveRequested(
        const QString& fieldId,
        std::uint32_t lsb,
        std::uint32_t msb);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::optional<regmap::Register> register_;
    regmap::ObjectId selectedFieldId_;
    QString scopeLabel_;
    QVector<std::pair<QRect, QString>> exactHitRegions_;
    QVector<std::pair<QRect, QString>> hitRegions_;
    QString draggedFieldId_;
    QString hoveredFieldId_;
    std::optional<std::uint32_t> previewLsb_;
    std::uint32_t draggedWidth_ {0};
    std::uint32_t anchorFromLsb_ {0};
    QPoint dragStartPosition_;
    bool dragPending_ {false};
    bool dragging_ {false};

    [[nodiscard]] const regmap::Field* fieldById(const QString& id) const;
    [[nodiscard]] QString fieldIdAt(const QPoint& position) const;
    [[nodiscard]] std::uint32_t bitAtX(qreal x) const;
    void cancelDrag();
    void updateDrag(qreal x);
};
