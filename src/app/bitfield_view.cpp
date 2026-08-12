#include "bitfield_view.hpp"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QToolTip>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] QString fromUtf8(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QColor fieldColor(const std::string& id)
{
    static const std::array<QColor, 6> colors{
        QColor(QStringLiteral("#5B9BD5")), QColor(QStringLiteral("#70AD47")),
        QColor(QStringLiteral("#ED7D31")), QColor(QStringLiteral("#FFC000")),
        QColor(QStringLiteral("#A5A5A5")), QColor(QStringLiteral("#4472C4")),
    };
    const std::size_t hash = qHash(fromUtf8(id));
    return colors[hash % colors.size()];
}

} // namespace

BitfieldView::BitfieldView(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(158);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setFocusPolicy(
        Qt::StrongFocus);
    setAccessibleName(
        QStringLiteral(
            "Register bit-field layout"));
    setToolTip(
        QStringLiteral(
            "Click or use Left/Right and Home/End to select a Field; drag or use Alt+Left/Alt+Right to move it."));
}

void BitfieldView::setRegister(const regmap::Register* reg)
{
    cancelDrag();
    register_ = reg == nullptr ? std::nullopt : std::optional<regmap::Register>{*reg};
    selectedFieldId_.clear();
    update();
}

void BitfieldView::setSelectedField(const regmap::Field* field)
{
    selectedFieldId_ = field == nullptr ? regmap::ObjectId{} : field->id;
    update();
}

bool BitfieldView::event(QEvent* event)
{
    if (dragPending_ ||
        dragging_) {
        switch (event->type()) {
        case QEvent::UngrabMouse:
            if (dragging_) {
                cancelDrag();
            }
            break;
        case QEvent::WindowDeactivate:
        case QEvent::Hide:
            cancelDrag();
            break;
        default:
            break;
        }
    }
    return QWidget::event(event);
}

void BitfieldView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(QStringLiteral("#F4F7FB")));
    hitRegions_.clear();

    if (!register_ || register_->width == 0) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("Select a register"));
        return;
    }

    const int left = 14;
    const int right = width() - 14;
    const int top = 66;
    const int barHeight = 54;
    const int available = std::max(1, right - left);
    const double pixelsPerBit = static_cast<double>(available) / register_->width;
    const QRect bar(left, top, available, barHeight);

    painter.setPen(QColor(QStringLiteral("#C6D2E1")));
    painter.setBrush(QColor(QStringLiteral("#E7ECF2")));
    painter.drawRoundedRect(bar, 3, 3);
    const QFont baseFont = painter.font();

    std::vector<const regmap::Field*> paintOrder;
    paintOrder.reserve(register_->fields.size());
    const regmap::Field* draggedField = nullptr;
    for (const auto& field : register_->fields) {
        if (dragging_ && fromUtf8(field.id) == draggedFieldId_) {
            draggedField = &field;
        } else {
            paintOrder.push_back(&field);
        }
    }
    if (draggedField != nullptr) {
        paintOrder.push_back(draggedField);
    }

    int fieldIndex = 0;
    for (const auto* fieldPointer : paintOrder) {
        const auto& field = *fieldPointer;
        const bool preview =
            dragging_ && fromUtf8(field.id) == draggedFieldId_ && previewLsb_.has_value();
        const std::uint32_t lsb = preview ? *previewLsb_ : field.lsb;
        const std::uint32_t msb = preview ? lsb + draggedWidth_ - 1 : field.msb;
        if (msb < lsb || msb >= register_->width) {
            continue;
        }
        const double startBit = static_cast<double>(register_->width - 1 - msb);
        const double bitCount = static_cast<double>(msb - lsb + 1);
        const int x = left + static_cast<int>(startBit * pixelsPerBit);
        const int fieldWidth = std::max(1, static_cast<int>(bitCount * pixelsPerBit));
        const QRect fieldRect(x, top, std::min(fieldWidth, right - x), barHeight);

        QColor color = fieldColor(field.id);
        const bool overlaps =
            preview && std::ranges::any_of(register_->fields, [&](const regmap::Field& other) {
                return other.id != field.id && other.msb >= other.lsb && lsb <= other.msb &&
                       other.lsb <= msb;
            });
        if (overlaps) {
            color = QColor(QStringLiteral("#F28B82"));
            painter.setPen(QPen(QColor(QStringLiteral("#B3261E")), 3));
        } else if (field.id == selectedFieldId_) {
            color = color.lighter(118);
            painter.setPen(QPen(QColor(QStringLiteral("#17365D")), 3));
        } else {
            painter.setPen(QPen(color.darker(145), 1));
        }
        painter.setBrush(color);
        painter.drawRoundedRect(fieldRect.adjusted(1, 1, -1, -1), 2, 2);

        const QString label = fromUtf8(field.name);
        if (fieldRect.width() >= painter.fontMetrics().horizontalAdvance(label) + 8) {
            painter.setPen(color.lightness() < 145 ? QColor(Qt::white)
                                                   : QColor(QStringLiteral("#0B1F33")));
            painter.drawText(fieldRect.adjusted(4, 2, -4, -2), Qt::AlignCenter, label);
        }

        const bool emphasized = preview || field.id == selectedFieldId_;
        QFont positionFont = baseFont;
        positionFont.setWeight(emphasized ? QFont::DemiBold : QFont::Normal);
        painter.setFont(positionFont);
        const int labelY = emphasized ? 10 : (fieldIndex % 2 == 0 ? 10 : 34);
        const QColor positionColor =
            preview ? QColor(QStringLiteral("#B3261E")) : QColor(QStringLiteral("#385D8A"));
        const auto endpointRectangle = [&](std::uint32_t value, int centerX, int y) {
            const QString text = QString::number(value);
            const int endpointWidth =
                painter.fontMetrics().horizontalAdvance(text) + (emphasized ? 12 : 6);
            const int endpointX = std::clamp(centerX - endpointWidth / 2, left,
                                             std::max(left, right - endpointWidth));
            return QRect(endpointX, y, endpointWidth, 18);
        };
        const auto drawEndpoint = [&](std::uint32_t value, const QRect& endpoint) {
            if (emphasized) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#DCE6F1")));
                painter.drawRoundedRect(endpoint, 3, 3);
            }
            painter.setPen(positionColor);
            painter.drawText(endpoint, Qt::AlignCenter, QString::number(value));
        };
        if (msb == lsb) {
            drawEndpoint(msb, endpointRectangle(msb, fieldRect.center().x(), labelY));
        } else {
            QRect msbRectangle = endpointRectangle(msb, fieldRect.left(), labelY);
            QRect lsbRectangle = endpointRectangle(lsb, fieldRect.right(), labelY);
            if (msbRectangle.intersects(lsbRectangle)) {
                msbRectangle.moveTop(10);
                lsbRectangle.moveTop(34);
            }
            drawEndpoint(msb, msbRectangle);
            drawEndpoint(lsb, lsbRectangle);
        }
        painter.setFont(baseFont);
        hitRegions_.push_back({fieldRect, fromUtf8(field.id)});
        ++fieldIndex;
    }

    if (hasFocus()) {
        painter.setPen(
            QPen(
                palette()
                    .highlight()
                    .color(),
                2,
                Qt::DashLine));
        painter.setBrush(
            Qt::NoBrush);
        painter.drawRoundedRect(
            bar.adjusted(
                -2, -2, 2, 2),
            4, 4);
    }
    painter.setPen(QColor(QStringLiteral("#385D8A")));
    painter.drawText(QRect(left, top + barHeight + 7, available, 20), Qt::AlignCenter,
                     dragging_ && previewLsb_ ? QStringLiteral("Moving field [%1:%2]")
                                                    .arg(*previewLsb_ + draggedWidth_ - 1)
                                                    .arg(*previewLsb_)
                                              : QStringLiteral("%1 bits").arg(register_->width));
}

void BitfieldView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() ==
        Qt::LeftButton) {
        setFocus(
            Qt::MouseFocusReason);
    }
    for (auto iterator = hitRegions_.crbegin(); iterator != hitRegions_.crend(); ++iterator) {
        const auto& [rectangle, id] = *iterator;
        if (rectangle.contains(event->position().toPoint())) {
            if (event->button() == Qt::LeftButton) {
                if (const auto* field = fieldById(id)) {
                    const std::uint64_t width = field->width();
                    if (width > 0 && width <= register_->width) {
                        draggedFieldId_ = id;
                        draggedWidth_ = static_cast<std::uint32_t>(width);
                        const std::uint32_t clickedBit = bitAtX(event->position().x());
                        anchorFromLsb_ = clickedBit > field->lsb
                                             ? std::min(clickedBit - field->lsb, draggedWidth_ - 1)
                                             : 0;
                        previewLsb_ = field->lsb;
                        dragStartPosition_ =
                            event->position()
                                .toPoint();
                        dragPending_ = true;
                        dragging_ = false;
                    }
                }
            }
            emit fieldActivated(id);
            setFocus(
                Qt::MouseFocusReason);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void BitfieldView::mouseMoveEvent(QMouseEvent* event)
{
    if (dragPending_) {
        if (!(event->buttons() &
              Qt::LeftButton)) {
            cancelDrag();
            QWidget::mouseMoveEvent(
                event);
            return;
        }
        const int distance =
            (event->position()
                 .toPoint() -
             dragStartPosition_)
                .manhattanLength();
        if (distance <
            QApplication::
                startDragDistance()) {
            event->accept();
            return;
        }
        dragPending_ = false;
        dragging_ = true;
        grabMouse();
    }
    if (dragging_) {
        updateDrag(event->position().x());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void BitfieldView::mouseReleaseEvent(QMouseEvent* event)
{
    if (dragPending_ &&
        event->button() ==
            Qt::LeftButton) {
        cancelDrag();
        event->accept();
        return;
    }
    if (dragging_ && event->button() == Qt::LeftButton) {
        updateDrag(event->position().x());
        const QString id = draggedFieldId_;
        const std::uint32_t lsb = previewLsb_.value_or(0);
        const std::uint32_t msb = lsb + draggedWidth_ - 1;
        cancelDrag();
        emit fieldMoveRequested(id, lsb, msb);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void BitfieldView::keyPressEvent(
    QKeyEvent* event)
{
    if (event == nullptr) {
        return;
    }
    if ((dragPending_ ||
         dragging_) &&
        event->key() ==
            Qt::Key_Escape) {
        const QString fieldId =
            draggedFieldId_;
        cancelDrag();
        QToolTip::hideText();
        emit fieldDragCancelled(
            fieldId);
        event->accept();
        return;
    }
    if (dragging_ ||
        dragPending_ ||
        !register_) {
        QWidget::keyPressEvent(
            event);
        return;
    }

    std::vector<std::size_t> ordered;
    ordered.reserve(
        register_->fields.size());
    for (std::size_t index = 0;
         index <
         register_->fields.size();
         ++index) {
        const regmap::Field& field =
            register_->fields[index];
        if (field.msb >= field.lsb &&
            field.msb <
                register_->width) {
            ordered.push_back(index);
        }
    }
    if (ordered.empty()) {
        QWidget::keyPressEvent(
            event);
        return;
    }
    std::ranges::stable_sort(
        ordered,
        [this](
            std::size_t left,
            std::size_t right) {
            const regmap::Field& leftField =
                register_->fields[left];
            const regmap::Field& rightField =
                register_->fields[right];
            if (leftField.msb !=
                rightField.msb) {
                return leftField.msb >
                    rightField.msb;
            }
            if (leftField.lsb !=
                rightField.lsb) {
                return leftField.lsb >
                    rightField.lsb;
            }
            return leftField.id <
                rightField.id;
        });
    const auto selected =
        std::ranges::find_if(
            ordered,
            [this](
                std::size_t index) {
                return register_
                           ->fields[index]
                           .id ==
                    selectedFieldId_;
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
    std::optional<std::size_t>
        targetPosition;
    switch (event->key()) {
    case Qt::Key_Left:
        targetPosition =
            hasSelected
            ? (selectedPosition == 0
                   ? 0
                   : selectedPosition - 1)
            : 0;
        break;
    case Qt::Key_Right:
        targetPosition =
            hasSelected
            ? std::min(
                  selectedPosition + 1,
                  ordered.size() - 1)
            : ordered.size() - 1;
        break;
    case Qt::Key_Home:
        targetPosition = 0;
        break;
    case Qt::Key_End:
        targetPosition =
            ordered.size() - 1;
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        targetPosition =
            hasSelected
            ? selectedPosition
            : 0;
        break;
    default:
        QWidget::keyPressEvent(
            event);
        return;
    }

    const regmap::Field& field =
        register_->fields[
            ordered[*targetPosition]];
    selectedFieldId_ = field.id;
    update();
    emit fieldActivated(
        fromUtf8(field.id));
    setFocus(
        Qt::OtherFocusReason);
    event->accept();
}

const regmap::Field* BitfieldView::fieldById(const QString& id) const
{
    if (!register_) {
        return nullptr;
    }
    const auto iterator = std::ranges::find_if(
        register_->fields, [&](const regmap::Field& field) { return fromUtf8(field.id) == id; });
    return iterator == register_->fields.end() ? nullptr : &*iterator;
}

std::uint32_t BitfieldView::bitAtX(qreal x) const
{
    if (!register_ || register_->width == 0) {
        return 0;
    }
    const qreal left = 14.0;
    const qreal available = std::max(1.0, static_cast<qreal>(width()) - 28.0);
    const qreal relative = std::clamp((x - left) / available, 0.0, 0.999999);
    const auto fromMsb =
        static_cast<std::uint32_t>(relative * static_cast<qreal>(register_->width));
    return register_->width - 1 - std::min(fromMsb, register_->width - 1);
}

void BitfieldView::cancelDrag()
{
    const bool changed =
        dragPending_ ||
        dragging_ ||
        !draggedFieldId_.isEmpty() ||
        previewLsb_.has_value();
    dragPending_ = false;
    dragging_ = false;
    draggedFieldId_.clear();
    previewLsb_.reset();
    draggedWidth_ = 0;
    anchorFromLsb_ = 0;
    dragStartPosition_ = {};

    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
    if (changed) {
        update();
    }
}

void BitfieldView::updateDrag(qreal x)
{
    if (!dragging_ || !register_ || draggedWidth_ == 0 || draggedWidth_ > register_->width) {
        return;
    }
    const std::uint32_t cursorBit = bitAtX(x);
    const std::uint32_t maximumLsb = register_->width - draggedWidth_;
    const std::uint32_t proposed = cursorBit >= anchorFromLsb_ ? cursorBit - anchorFromLsb_ : 0;
    const std::uint32_t lsb = std::min(proposed, maximumLsb);
    if (previewLsb_ == lsb) {
        return;
    }
    previewLsb_ = lsb;
    const std::uint32_t msb = lsb + draggedWidth_ - 1;
    emit fieldDragPreview(draggedFieldId_, lsb, msb);
    QToolTip::showText(mapToGlobal(QPoint(static_cast<int>(x), 24)),
                       QStringLiteral("[%1:%2]").arg(msb).arg(lsb), this);
    update();
}
