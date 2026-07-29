#include "main_window.hpp"

#include "bitfield_view.hpp"
#include "source_navigation.hpp"

#include "regmap/core/model_tokens.hpp"
#include "regmap/core/sync_diff.hpp"
#include "regmap/core/unsigned_value.hpp"
#include "regmap/core/validation.hpp"
#include "regmap/core/workspace_store.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDrag>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QModelIndex>
#include <QMouseEvent>
#include <QMimeData>
#include <QPaintEvent>
#include <QPersistentModelIndex>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShortcut>
#include <QSettings>
#include <QSizePolicy>
#include <QStyle>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QStyleOptionButton>
#include <QStyleOptionViewItem>
#include <QStandardItem>
#include <QStandardPaths>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QDropEvent>
#include <QStringList>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t maximumEditableWidth = 65536;
constexpr std::string_view addressWidthDiagnosticCode = "RM3010";
constexpr std::string_view addressRangeDiagnosticCode = "RM3011";
constexpr std::string_view strideDiagnosticCode = "RM3022";
constexpr std::string_view registerRangeDiagnosticCode = "RM3023";
constexpr std::string_view addressOverlapDiagnosticCode = "RM3024";
constexpr std::string_view blockOverlapDiagnosticCode = "RM3025";
constexpr std::string_view fieldRangeDiagnosticCode = "RM3030";
constexpr std::string_view fieldOverlapDiagnosticCode = "RM3031";
constexpr std::string_view fieldTypeDiagnosticCode = "RM3036";
constexpr std::string_view numericRangeDiagnosticCode = "RM3052";
constexpr auto tableClipboardMimeType =
    "application/x-regmap-workbench-table-cells";
constexpr auto hierarchyClipboardMimeType =
    "application/x-regmap-workbench-hierarchy-object";
constexpr auto hierarchyDragMimeType =
    "application/x-regmap-workbench-hierarchy-drag";
constexpr auto recentProjectsSettingsKey = "projects/recent";
constexpr auto choiceEditorPopupProperty =
    "regmapWorkbenchChoiceEditorPopup";
constexpr auto mainWindowGeometrySettingsKey =
    "ui/v1/mainWindowGeometry";
constexpr auto workspaceSplitterSettingsKey =
    "ui/v1/workspaceSplitter";
constexpr auto editorSplitterSettingsKey =
    "ui/v1/editorSplitter";
constexpr auto resultsSplitterSettingsKey =
    "ui/v1/resultsSplitter";
constexpr auto advancedFieldsSettingsKey =
    "ui/v1/showAdvancedFieldColumns";
constexpr auto registerHeaderSettingsKey =
    "ui/v1/registerHeader";
constexpr auto fieldHeaderSettingsKey =
    "ui/v1/fieldHeader";
constexpr qsizetype maximumRecentProjectCount = 8;

enum class HierarchyDropPlacement {
    onItem,
    aboveItem,
    belowItem,
    viewport,
};

enum RegisterColumn {
    registerNameColumn = 0,
    registerOffsetColumn,
    registerAddressColumn,
    registerWidthColumn,
    registerTypeColumn,
    registerFieldsColumn,
    registerRangeColumn,
    registerInitialColumn,
    registerResetColumn,
    registerAccessColumn,
    registerTagsColumn,
    registerDescriptionColumn,
};

enum FieldColumn {
    fieldNameColumn = 0,
    fieldParentColumn,
    fieldMsbColumn,
    fieldLsbColumn,
    fieldWidthColumn,
    fieldTypeColumn,
    fieldMinimumColumn,
    fieldMaximumColumn,
    fieldSoftwareAccessColumn,
    fieldHardwareAccessColumn,
    fieldResetColumn,
    fieldReadEffectColumn,
    fieldWriteEffectColumn,
    fieldDescriptionColumn,
};

enum EnumColumn {
    enumNameColumn = 0,
    enumValueColumn,
    enumDescriptionColumn,
};

[[nodiscard]] QString unsavedChangeSummary(std::size_t count)
{
    if (count == 1) {
        return QStringLiteral("1 unsaved Workbench change");
    }
    return QStringLiteral("%1 unsaved Workbench changes").arg(count);
}

[[nodiscard]] QString fromUtf8(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

[[nodiscard]] QString fromPath(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

[[nodiscard]] QString projectChooserDirectory(
    const std::filesystem::path& manifestPath)
{
    if (!manifestPath.empty() && !manifestPath.parent_path().empty()) {
        return fromPath(manifestPath.parent_path());
    }
    return QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
}

[[nodiscard]] QString normalizedProjectPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

[[nodiscard]] bool sameProjectPath(const QString& left,
                                   const QString& right)
{
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

[[nodiscard]] QString hex(std::uint64_t value)
{
    return fromUtf8(regmap::UnsignedValue(value).toHexString());
}

[[nodiscard]] QString valueText(const std::optional<regmap::UnsignedValue>& value)
{
    return value ? fromUtf8(value->toHexString()) : QString{};
}

[[nodiscard]] QString sourceText(const regmap::SourceLocation& source)
{
    if (source.empty()) {
        return {};
    }
    QString result = fromPath(source.workbook);
    if (!source.sheet.empty()) {
        result += QStringLiteral(" / ") + fromUtf8(source.sheet);
    }
    if (!source.cell.empty()) {
        result += QStringLiteral("!") + fromUtf8(source.cell);
    } else if (source.row.has_value()) {
        result += QStringLiteral(":%1").arg(*source.row);
    }
    return result;
}

[[nodiscard]] QString accessText(regmap::AccessMode access)
{
    switch (access) {
    case regmap::AccessMode::none:
        return QStringLiteral("NONE");
    case regmap::AccessMode::readOnly:
        return QStringLiteral("RO");
    case regmap::AccessMode::writeOnly:
        return QStringLiteral("WO");
    case regmap::AccessMode::readWrite:
        return QStringLiteral("RW");
    }
    return QStringLiteral("UNKNOWN");
}

[[nodiscard]] bool accessCanRead(regmap::AccessMode access) noexcept
{
    return access == regmap::AccessMode::readOnly ||
           access == regmap::AccessMode::readWrite;
}

[[nodiscard]] bool accessCanWrite(regmap::AccessMode access) noexcept
{
    return access == regmap::AccessMode::writeOnly ||
           access == regmap::AccessMode::readWrite;
}

[[nodiscard]] bool accessFitsWithin(regmap::AccessMode access,
                                    regmap::AccessMode containingAccess) noexcept
{
    return (!accessCanRead(access) || accessCanRead(containingAccess)) &&
           (!accessCanWrite(access) || accessCanWrite(containingAccess));
}

[[nodiscard]] bool
fieldAccessFitsRegister(const std::vector<regmap::Field>& fields,
                        regmap::AccessMode registerAccess)
{
    return std::ranges::all_of(fields, [registerAccess](const regmap::Field& field) {
        return accessFitsWithin(field.softwareAccess, registerAccess) &&
               fieldAccessFitsRegister(field.members, registerAccess);
    });
}

[[nodiscard]] QString valueTypeText(regmap::FieldType type, std::uint64_t width)
{
    switch (type) {
    case regmap::FieldType::bits:
        return QStringLiteral("bits");
    case regmap::FieldType::boolean:
        return QStringLiteral("bool");
    case regmap::FieldType::unsignedInteger:
        return QStringLiteral("uint%1").arg(width);
    case regmap::FieldType::signedInteger:
        return QStringLiteral("int%1").arg(width);
    case regmap::FieldType::enumeration:
        return QStringLiteral("enum");
    case regmap::FieldType::structure:
        return QStringLiteral("field");
    case regmap::FieldType::reserved:
        return QStringLiteral("reserved");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString fieldTypeText(const regmap::Field& field)
{
    return valueTypeText(field.type, field.width());
}

[[nodiscard]] QString registerTypeText(const regmap::Register& reg)
{
    return valueTypeText(reg.type, reg.width);
}

[[nodiscard]] QString rangeText(const std::optional<std::string>& minimum,
                                const std::optional<std::string>& maximum)
{
    if (!minimum && !maximum) {
        return {};
    }
    return QStringLiteral("%1 .. %2")
        .arg(minimum ? fromUtf8(*minimum) : QString{}, maximum ? fromUtf8(*maximum) : QString{});
}

[[nodiscard]] QString readSideEffectText(regmap::ReadSideEffect effect)
{
    switch (effect) {
    case regmap::ReadSideEffect::none:
        return QStringLiteral("none");
    case regmap::ReadSideEffect::clear:
        return QStringLiteral("clear");
    case regmap::ReadSideEffect::set:
        return QStringLiteral("set");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString writeSideEffectText(regmap::WriteSideEffect effect)
{
    switch (effect) {
    case regmap::WriteSideEffect::none:
        return QStringLiteral("none");
    case regmap::WriteSideEffect::write:
        return QStringLiteral("write");
    case regmap::WriteSideEffect::oneToClear:
        return QStringLiteral("w1c");
    case regmap::WriteSideEffect::oneToSet:
        return QStringLiteral("w1s");
    case regmap::WriteSideEffect::zeroToClear:
        return QStringLiteral("w0c");
    case regmap::WriteSideEffect::zeroToSet:
        return QStringLiteral("w0s");
    case regmap::WriteSideEffect::toggle:
        return QStringLiteral("toggle");
    }
    return QStringLiteral("unknown");
}

[[nodiscard]] QString severityText(regmap::DiagnosticSeverity severity)
{
    switch (severity) {
    case regmap::DiagnosticSeverity::information:
        return QStringLiteral("Info");
    case regmap::DiagnosticSeverity::warning:
        return QStringLiteral("Warning");
    case regmap::DiagnosticSeverity::error:
        return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

[[nodiscard]] QStandardItem* item(const QString& text)
{
    auto* result = new QStandardItem(text);
    result->setEditable(false);
    result->setTextAlignment(Qt::AlignCenter);
    return result;
}

[[nodiscard]] QStandardItem* editableItem(const QString& text, const regmap::ObjectId& objectId,
                                          std::string_view property, int objectRole,
                                          int propertyRole)
{
    auto* result = new QStandardItem(text);
    result->setEditable(true);
    result->setTextAlignment(property == "description" ? Qt::AlignLeft | Qt::AlignVCenter
                                                        : Qt::AlignCenter);
    result->setData(fromUtf8(objectId), objectRole);
    result->setData(fromUtf8(property), propertyRole);
    return result;
}

[[nodiscard]] QStandardItem* addRowItem(const QString& text, int addRole)
{
    auto* result = new QStandardItem(text);
    result->setEditable(false);
    result->setTextAlignment(Qt::AlignCenter);
    result->setData(true, addRole);
    QFont font = result->font();
    font.setWeight(QFont::DemiBold);
    result->setFont(font);
    result->setForeground(QColor(QStringLiteral("#385D8A")));
    return result;
}

[[nodiscard]] QStandardItem* pasteableItem(const QString& text, std::string_view objectId,
                                           std::string_view property, int objectRole,
                                           int propertyRole)
{
    auto* result = item(text);
    result->setData(fromUtf8(objectId), objectRole);
    result->setData(fromUtf8(property), propertyRole);
    return result;
}

struct TableCellReference {
    std::string objectId;
    std::string property;

    bool operator==(const TableCellReference&) const = default;
};

struct TableSelectionSnapshot {
    std::vector<TableCellReference> selected;
    std::optional<TableCellReference> current;
    int horizontalScroll{0};
    int verticalScroll{0};
};

[[nodiscard]] std::optional<TableCellReference>
tableCellReference(const QModelIndex& index, int objectRole, int propertyRole)
{
    if (!index.isValid()) {
        return std::nullopt;
    }
    TableCellReference reference{
        index.data(objectRole).toString().toUtf8().toStdString(),
        index.data(propertyRole).toString().toUtf8().toStdString(),
    };
    if (reference.objectId.empty() || reference.property.empty()) {
        return std::nullopt;
    }
    return reference;
}

[[nodiscard]] TableSelectionSnapshot
captureTableSelection(QTableView* view, int objectRole, int propertyRole)
{
    TableSelectionSnapshot snapshot;
    if (view == nullptr || view->selectionModel() == nullptr) {
        return snapshot;
    }
    for (const QModelIndex& index : view->selectionModel()->selectedIndexes()) {
        if (const auto reference = tableCellReference(index, objectRole, propertyRole);
            reference &&
            std::ranges::find(snapshot.selected, *reference) == snapshot.selected.end()) {
            snapshot.selected.push_back(*reference);
        }
    }
    snapshot.current =
        tableCellReference(view->currentIndex(), objectRole, propertyRole);
    snapshot.horizontalScroll =
        view->horizontalScrollBar()->value();
    snapshot.verticalScroll =
        view->verticalScrollBar()->value();
    return snapshot;
}

void restoreTableSelection(QTableView* view, const TableSelectionSnapshot& snapshot,
                           int objectRole, int propertyRole)
{
    if (view == nullptr || view->model() == nullptr || view->selectionModel() == nullptr ||
        snapshot.selected.empty()) {
        return;
    }
    QModelIndex first;
    QModelIndex current;
    view->selectionModel()->clearSelection();
    for (int row = 0; row < view->model()->rowCount(); ++row) {
        for (int column = 0; column < view->model()->columnCount(); ++column) {
            const QModelIndex index = view->model()->index(row, column);
            const auto reference = tableCellReference(index, objectRole, propertyRole);
            if (!reference ||
                std::ranges::find(snapshot.selected, *reference) == snapshot.selected.end()) {
                continue;
            }
            view->selectionModel()->select(index, QItemSelectionModel::Select);
            if (!first.isValid()) {
                first = index;
            }
            if (snapshot.current && *reference == *snapshot.current) {
                current = index;
            }
        }
    }
    const QModelIndex restoredCurrent = current.isValid() ? current : first;
    if (restoredCurrent.isValid()) {
        view->selectionModel()->setCurrentIndex(
            restoredCurrent, QItemSelectionModel::NoUpdate);
        view->horizontalScrollBar()->setValue(
            snapshot.horizontalScroll);
        view->verticalScrollBar()->setValue(
            snapshot.verticalScroll);
    }
}

[[nodiscard]] QString tagsText(const std::vector<std::string>& tags)
{
    QStringList values;
    for (const auto& tag : tags) {
        values.push_back(fromUtf8(tag));
    }
    return values.join(QStringLiteral(", "));
}

constexpr auto popupListStyle = R"(
QListWidget {
    border: 1px solid #C6D2E1;
    border-radius: 3px;
    background: #FFFDF5;
    outline: none;
}
QListWidget::item {
    border: 0;
    border-radius: 3px;
    padding: 5px 8px;
}
QListWidget::item:hover {
    background: #DCE6F1;
    color: #17365D;
}
QListWidget::item:selected {
    background: #385D8A;
    color: white;
}
)";

class HierarchyItemDelegate final : public QStyledItemDelegate {
public:
    explicit HierarchyItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override
    {
        QSize result = QStyledItemDelegate::sizeHint(option, index);
        result.setHeight(std::max(result.height(), 30));
        return result;
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override
    {
        Q_UNUSED(index)
        editor->setGeometry(option.rect.adjusted(1, 1, -1, -1));
    }
};

class AccessItemDelegate final : public QStyledItemDelegate {
public:
    explicit AccessItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    [[nodiscard]] QWidget* createEditor(
        QWidget* parent, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        Q_UNUSED(option)
        Q_UNUSED(index)
        auto* editor = new QComboBox(parent);
        editor->setObjectName(QStringLiteral("fieldAccessEditor"));
        editor->setEditable(false);
        editor->addItems({QStringLiteral("NONE"), QStringLiteral("RO"),
                          QStringLiteral("WO"), QStringLiteral("RW")});
        auto* delegate = const_cast<AccessItemDelegate*>(this);
        connect(editor, QOverload<int>::of(&QComboBox::activated), editor,
                [delegate, editor] {
                    Q_EMIT delegate->commitData(editor);
                    Q_EMIT delegate->closeEditor(editor);
                });
        return editor;
    }

    void setEditorData(QWidget* editor, const QModelIndex& index) const override
    {
        auto* combo = qobject_cast<QComboBox*>(editor);
        if (combo == nullptr) {
            return;
        }
        const int current = combo->findText(index.data().toString(), Qt::MatchFixedString);
        if (current >= 0) {
            combo->setCurrentIndex(current);
        }
    }

    void setModelData(QWidget* editor, QAbstractItemModel* model,
                      const QModelIndex& index) const override
    {
        const auto* combo = qobject_cast<QComboBox*>(editor);
        if (combo != nullptr) {
            model->setData(index, combo->currentText());
        }
    }

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const override
    {
        Q_UNUSED(index)
        editor->setGeometry(option.rect);
    }
};

class FieldsButtonDelegate final : public QStyledItemDelegate {
public:
    FieldsButtonDelegate(int actionRole, int activeRole, QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
        , actionRole_(actionRole)
        , activeRole_(activeRole)
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        if (!index.data(actionRole_).toBool()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionButton button;
        button.rect = option.rect.adjusted(6, 3, -6, -3);
        button.state = QStyle::State_Enabled;
        if (index.data(activeRole_).toBool()) {
            button.state |= QStyle::State_On | QStyle::State_Sunken;
        } else {
            button.state |= QStyle::State_Raised;
        }
        if (option.state & QStyle::State_MouseOver) {
            button.state |= QStyle::State_MouseOver;
        }
        if (option.state & QStyle::State_HasFocus) {
            button.state |= QStyle::State_HasFocus;
        }
        button.palette = option.palette;
        button.text = index.data(Qt::DisplayRole).toString();
        QStyle* style = option.widget == nullptr ? QApplication::style() : option.widget->style();
        style->drawControl(QStyle::CE_PushButton, &button, painter, option.widget);
    }

private:
    int actionRole_;
    int activeRole_;
};

class HierarchyTreeView final : public QTreeView {
public:
    using DropHandler = std::function<void(const std::string&, const std::string&, int)>;

    explicit HierarchyTreeView(int objectRole, QWidget* parent = nullptr)
        : QTreeView(parent)
        , objectRole_(objectRole)
    {
    }

    void setDropHandler(DropHandler handler) { dropHandler_ = std::move(handler); }

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        const QModelIndex source = currentIndex();
        const QString sourceId = source.data(objectRole_).toString();
        if (!source.isValid() || sourceId.isEmpty() ||
            !(source.flags() & Qt::ItemIsDragEnabled) || model() == nullptr) {
            return;
        }

        QModelIndexList indexes{source};
        QMimeData* mimeData = model()->mimeData(indexes);
        if (mimeData == nullptr) {
            mimeData = new QMimeData;
        }
        mimeData->setData(QString::fromLatin1(hierarchyDragMimeType), sourceId.toUtf8());

        QDrag drag(this);
        drag.setMimeData(mimeData);
        static_cast<void>(drag.exec(supportedActions & Qt::MoveAction, Qt::MoveAction));
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData() != nullptr &&
            event->mimeData()->hasFormat(QString::fromLatin1(hierarchyDragMimeType))) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        QTreeView::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (event->mimeData() != nullptr &&
            event->mimeData()->hasFormat(QString::fromLatin1(hierarchyDragMimeType))) {
            QTreeView::dragMoveEvent(event);
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
        QTreeView::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        const QMimeData* mimeData = event->mimeData();
        if (mimeData == nullptr ||
            !mimeData->hasFormat(QString::fromLatin1(hierarchyDragMimeType)) ||
            !dropHandler_) {
            QTreeView::dropEvent(event);
            return;
        }

        const std::string sourceId =
            mimeData->data(QString::fromLatin1(hierarchyDragMimeType))
                .toStdString();
        const QModelIndex target = indexAt(event->position().toPoint());
        const std::string targetId =
            target.data(objectRole_).toString().toUtf8().toStdString();
        HierarchyDropPlacement placement = HierarchyDropPlacement::viewport;
        if (target.isValid()) {
            const QRect rectangle = visualRect(target);
            const int edge = std::max(4, rectangle.height() / 4);
            const int y = event->position().toPoint().y();
            if (y < rectangle.top() + edge) {
                placement = HierarchyDropPlacement::aboveItem;
            } else if (y > rectangle.bottom() - edge) {
                placement = HierarchyDropPlacement::belowItem;
            } else {
                placement = HierarchyDropPlacement::onItem;
            }
        }

        event->setDropAction(Qt::MoveAction);
        event->accept();
        QTimer::singleShot(
            0, this,
            [this, sourceId, targetId, placement] {
                dropHandler_(sourceId, targetId, static_cast<int>(placement));
            });
    }

private:
    int objectRole_;
    DropHandler dropHandler_;
};

[[nodiscard]] bool focusBelongsToWidgetOutsidePopup(
    const QWidget* focus, const QWidget* widget)
{
    for (const QWidget* current = focus; current != nullptr;
         current = current->parentWidget()) {
        if (current->windowType() == Qt::Popup) {
            return false;
        }
        if (current == widget) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool focusUsesChoiceEditor(
    const QWidget* focus)
{
    for (const QWidget* current = focus; current != nullptr;
         current = current->parentWidget()) {
        if (qobject_cast<const QComboBox*>(current) != nullptr ||
            current->property(choiceEditorPopupProperty).toBool()) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] QFrame* createAnchoredPopup(QTableView* view, const QModelIndex& index,
                                          const QString& objectName, int preferredWidth,
                                          int preferredHeight)
{
    if (QWidget* active = QApplication::activePopupWidget()) {
        active->close();
    }

    auto* popup = new QFrame(view, Qt::Popup);
    popup->setObjectName(objectName);
    popup->setProperty(choiceEditorPopupProperty, true);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setFrameShadow(QFrame::Raised);
    popup->resize(std::max(preferredWidth, view->visualRect(index).width()), preferredHeight);

    auto* closeShortcut =
        new QShortcut(QKeySequence::Cancel, popup);
    closeShortcut->setObjectName(
        QStringLiteral("closeAnchoredPopupShortcut"));
    closeShortcut->setContext(
        Qt::WidgetWithChildrenShortcut);
    const QPersistentModelIndex anchor(index);
    QObject::connect(
        closeShortcut, &QShortcut::activated, popup,
        [popup, view, anchor] {
            popup->close();
            if (anchor.isValid()) {
                view->setCurrentIndex(anchor);
                view->scrollTo(anchor);
            }
            view->setFocus(Qt::OtherFocusReason);
        });

    const QRect cell = view->visualRect(index);
    QPoint position = view->viewport()->mapToGlobal(cell.bottomLeft() + QPoint(0, 1));
    if (const QScreen* screen = QApplication::screenAt(position)) {
        const QRect available = screen->availableGeometry();
        if (position.y() + popup->height() > available.bottom()) {
            position.setY(view->viewport()->mapToGlobal(cell.topLeft()).y() - popup->height() - 1);
        }
        position.setX(
            std::clamp(position.x(), available.left(),
                       std::max(available.left(), available.right() - popup->width() + 1)));
    }
    popup->move(position);
    return popup;
}

class RegisterTableView final : public QTableView {
public:
    using BoundaryPredicate = std::function<bool(int)>;
    using InsertHandler = std::function<void(int)>;
    using CellActionPredicate = std::function<bool(const QModelIndex&)>;
    using CellActionHandler = std::function<void(const QModelIndex&)>;

    explicit RegisterTableView(QWidget* parent = nullptr)
        : QTableView(parent)
    {
        setMouseTracking(true);
        viewport()->setMouseTracking(true);
    }

    void setBoundaryPredicate(BoundaryPredicate predicate)
    {
        boundaryPredicate_ = std::move(predicate);
    }

    void setInsertHandler(InsertHandler handler) { insertHandler_ = std::move(handler); }
    void setCellActionPredicate(CellActionPredicate predicate)
    {
        cellActionPredicate_ = std::move(predicate);
    }
    void setCellActionHandler(CellActionHandler handler)
    {
        cellActionHandler_ = std::move(handler);
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        const QModelIndex index = currentIndex();
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
             event->key() == Qt::Key_Space) &&
            cellActionPredicate_ && cellActionPredicate_(index) && cellActionHandler_) {
            cellActionHandler_(index);
            event->accept();
            return;
        }
        QTableView::keyPressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const QPoint position = event->position().toPoint();
        int candidate = -1;
        if (position.x() <= insertionHotspotWidth && model() != nullptr &&
            model()->rowCount() >= 3) {
            const int row = rowAt(position.y());
            if (row >= 0) {
                const QRect rowRectangle = visualRect(model()->index(row, 0));
                constexpr int activationDistance = 7;
                if (row > 0 && std::abs(position.y() - rowRectangle.top()) <= activationDistance) {
                    candidate = row;
                } else if (row + 1 < model()->rowCount() - 1 &&
                           std::abs(position.y() - rowRectangle.bottom()) <= activationDistance) {
                    candidate = row + 1;
                }
            }
        }
        if (candidate >= 0 && boundaryPredicate_ && !boundaryPredicate_(candidate)) {
            candidate = -1;
        }
        setInsertionBoundary(candidate);
        QTableView::mouseMoveEvent(event);
        const bool overCellAction =
            cellActionPredicate_ && cellActionPredicate_(indexAt(position));
        const bool overInsertionAction =
            insertionBoundary_ >= 0 && plusRectangle().contains(position);
        if (overCellAction || overInsertionAction) {
            viewport()->setCursor(Qt::PointingHandCursor);
        } else {
            viewport()->unsetCursor();
        }
    }

    void leaveEvent(QEvent* event) override
    {
        setInsertionBoundary(-1);
        viewport()->unsetCursor();
        QTableView::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && insertionBoundary_ >= 0 &&
            plusRectangle().contains(event->position().toPoint()) && insertHandler_) {
            const int boundary = insertionBoundary_;
            setInsertionBoundary(-1);
            viewport()->unsetCursor();
            insertHandler_(boundary);
            event->accept();
            return;
        }
        QTableView::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        QTableView::paintEvent(event);
        if (insertionBoundary_ < 0 || model() == nullptr ||
            insertionBoundary_ >= model()->rowCount()) {
            return;
        }
        const int boundaryY = rowViewportPosition(insertionBoundary_);
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(QStringLiteral("#4472C4")), 2));
        painter.drawLine(4, boundaryY, viewport()->width() - 4, boundaryY);
        painter.setBrush(QColor(QStringLiteral("#4472C4")));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPoint(plusCenterX, boundaryY), 9, 9);
        painter.setPen(QPen(Qt::white, 2));
        painter.drawLine(plusCenterX - 4, boundaryY, plusCenterX + 4, boundaryY);
        painter.drawLine(plusCenterX, boundaryY - 4, plusCenterX, boundaryY + 4);
    }

private:
    static constexpr int insertionHotspotWidth = 40;
    static constexpr int plusCenterX = 18;

    BoundaryPredicate boundaryPredicate_;
    InsertHandler insertHandler_;
    CellActionPredicate cellActionPredicate_;
    CellActionHandler cellActionHandler_;
    int insertionBoundary_{-1};

    [[nodiscard]] QRect plusRectangle() const
    {
        if (insertionBoundary_ < 0) {
            return {};
        }
        const int boundaryY = rowViewportPosition(insertionBoundary_);
        return QRect(plusCenterX - 11, boundaryY - 11, 22, 22);
    }

    void setInsertionBoundary(int boundary)
    {
        if (insertionBoundary_ == boundary) {
            return;
        }
        insertionBoundary_ = boundary;
        viewport()->update();
    }
};

[[nodiscard]] std::uint64_t registerExtent(const regmap::Register& reg)
{
    const std::uint64_t byteWidth = (static_cast<std::uint64_t>(reg.width) + 7U) / 8U;
    return reg.array.count > 1
               ? static_cast<std::uint64_t>(reg.array.count - 1) * reg.array.stride + byteWidth
               : byteWidth;
}

[[nodiscard]] const regmap::Field* findFieldRecursive(const std::vector<regmap::Field>& fields,
                                                      std::string_view id)
{
    for (const auto& field : fields) {
        if (field.id == id) {
            return &field;
        }
        if (const auto* result = findFieldRecursive(field.members, id)) {
            return result;
        }
    }
    return nullptr;
}

struct EnumOwnerContext {
    const std::vector<regmap::EnumValue>* values;
    std::size_t width;
    regmap::ObjectId ownerId;
    regmap::FieldType ownerType;
    bool registerOwner;
};

[[nodiscard]] std::optional<EnumOwnerContext>
enumOwnerContext(const std::vector<regmap::Field>& fields,
                 std::string_view enumValueId)
{
    for (const auto& field : fields) {
        if (std::ranges::any_of(field.enumValues, [&](const regmap::EnumValue& value) {
                return value.id == enumValueId;
            })) {
            return EnumOwnerContext{&field.enumValues,
                                    static_cast<std::size_t>(field.width()),
                                    field.id, field.type, false};
        }
        if (auto context = enumOwnerContext(field.members, enumValueId)) {
            return context;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<EnumOwnerContext>
enumOwnerContext(const regmap::Workspace& workspace, std::string_view enumValueId)
{
    for (const auto& addressSpace : workspace.addressSpaces) {
        for (const auto& block : addressSpace.blocks) {
            for (const auto& reg : block.registers) {
                if (std::ranges::any_of(reg.enumValues, [&](const regmap::EnumValue& value) {
                        return value.id == enumValueId;
                    })) {
                    return EnumOwnerContext{&reg.enumValues,
                                            static_cast<std::size_t>(reg.width),
                                            reg.id, reg.type, true};
                }
                if (auto context = enumOwnerContext(reg.fields, enumValueId)) {
                    return context;
                }
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool enumValuesFitWidth(const std::vector<regmap::EnumValue>& values,
                                      std::size_t width)
{
    return std::ranges::all_of(values, [width](const regmap::EnumValue& value) {
        return value.value.fitsInBits(width);
    });
}

[[nodiscard]] bool registerValuesFitWidth(const regmap::Register& reg, std::size_t width,
                                          bool includeEnumValues)
{
    const bool initialFits = !reg.initialValue || reg.initialValue->fitsInBits(width);
    const bool resetFits = !reg.resetValue || reg.resetValue->fitsInBits(width);
    return initialFits && resetFits &&
           (!includeEnumValues || enumValuesFitWidth(reg.enumValues, width));
}

[[nodiscard]] bool fieldValuesFitWidth(const regmap::Field& field,
                                       const regmap::Register& owner, std::size_t width,
                                       bool includeEnumValues)
{
    const bool resetFits =
        owner.resetValue || !field.resetValue || field.resetValue->fitsInBits(width);
    const bool enumsFit =
        !includeEnumValues || enumValuesFitWidth(field.enumValues, width);
    const bool membersFit =
        std::ranges::all_of(field.members, [width](const regmap::Field& member) {
            return member.msb < width;
        });
    return resetFits && enumsFit && membersFit;
}

void configureTable(QTableView* view)
{
    view->setSelectionBehavior(QAbstractItemView::SelectItems);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(false);
    view->setShowGrid(true);
    view->setWordWrap(false);
    view->setCornerButtonEnabled(false);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(28);
    view->horizontalHeader()->setMinimumSectionSize(72);
    view->horizontalHeader()->setStretchLastSection(true);
}

[[nodiscard]] bool addOverflow(std::uint64_t left, std::uint64_t right, std::uint64_t& output)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return true;
    }
    output = left + right;
    return false;
}

struct BlockAddressInterval {
    std::uint64_t first{0};
    std::uint64_t last{0};
};

[[nodiscard]] std::optional<std::uint64_t>
checkedRegisterExtent(const regmap::Register& reg)
{
    const std::uint64_t byteWidth =
        (static_cast<std::uint64_t>(reg.width) + 7U) / 8U;
    if (reg.width == 0 || reg.array.count == 0) {
        return std::nullopt;
    }
    if (reg.array.count == 1) {
        return byteWidth;
    }
    const std::uint64_t instances =
        static_cast<std::uint64_t>(reg.array.count - 1);
    if (reg.array.stride != 0 &&
        instances >
            std::numeric_limits<std::uint64_t>::max() /
                reg.array.stride) {
        return std::nullopt;
    }
    const std::uint64_t lastOffset = instances * reg.array.stride;
    std::uint64_t extent = 0;
    return addOverflow(lastOffset, byteWidth, extent)
               ? std::nullopt
               : std::optional{extent};
}

[[nodiscard]] std::optional<std::uint64_t>
blockAllocationSize(const regmap::RegisterBlock& block)
{
    std::uint64_t allocation = block.size.value_or(0);
    if (block.size.has_value() && *block.size == 0) {
        return std::nullopt;
    }
    for (const auto& reg : block.registers) {
        const auto extent = checkedRegisterExtent(reg);
        std::uint64_t end = 0;
        if (!extent || addOverflow(reg.offset, *extent, end)) {
            return std::nullopt;
        }
        allocation = std::max(allocation, end);
    }
    return allocation == 0 ? std::optional<std::uint64_t>{1}
                           : std::optional{allocation};
}

[[nodiscard]] std::optional<BlockAddressInterval>
blockAddressInterval(const regmap::RegisterBlock& block)
{
    const auto allocation = blockAllocationSize(block);
    std::uint64_t last = 0;
    if (!allocation ||
        addOverflow(block.baseAddress, *allocation - 1, last)) {
        return std::nullopt;
    }
    return BlockAddressInterval{block.baseAddress, last};
}

[[nodiscard]] bool blockFitsPage(const regmap::AddressSpace& page,
                                 const regmap::RegisterBlock& block)
{
    const auto interval = blockAddressInterval(block);
    std::uint64_t absoluteFirst = 0;
    std::uint64_t absoluteLast = 0;
    if (!interval || page.addressWidth == 0 || page.addressWidth > 64 ||
        addOverflow(page.baseAddress, interval->first, absoluteFirst) ||
        addOverflow(page.baseAddress, interval->last, absoluteLast)) {
        return false;
    }
    if (page.addressWidth == 64) {
        return true;
    }
    return absoluteLast < (std::uint64_t{1} << page.addressWidth);
}

[[nodiscard]] bool intervalsOverlap(const BlockAddressInterval& left,
                                    const BlockAddressInterval& right) noexcept
{
    return left.first <= right.last && right.first <= left.last;
}

[[nodiscard]] bool blockPlacementAvailable(
    const regmap::AddressSpace& page, const regmap::RegisterBlock& block,
    std::string_view ignoredBlockId = {})
{
    const auto candidate = blockAddressInterval(block);
    if (!candidate || !blockFitsPage(page, block)) {
        return false;
    }
    for (const auto& existing : page.blocks) {
        if (!ignoredBlockId.empty() && existing.id == ignoredBlockId) {
            continue;
        }
        const auto occupied = blockAddressInterval(existing);
        if (!occupied || intervalsOverlap(*candidate, *occupied)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::uint64_t>
availableBlockBase(const regmap::AddressSpace& page,
                   const regmap::RegisterBlock& block)
{
    const auto searchFrom =
        [&](std::uint64_t startingBase) -> std::optional<std::uint64_t> {
        regmap::RegisterBlock candidate = block;
        candidate.baseAddress = startingBase;
        for (std::size_t attempt = 0; attempt <= page.blocks.size(); ++attempt) {
            if (blockPlacementAvailable(page, candidate)) {
                return candidate.baseAddress;
            }
            const auto candidateInterval = blockAddressInterval(candidate);
            if (!candidateInterval) {
                return std::nullopt;
            }
            std::optional<std::uint64_t> nextBase;
            for (const auto& existing : page.blocks) {
                const auto occupied = blockAddressInterval(existing);
                if (!occupied) {
                    return std::nullopt;
                }
                if (intervalsOverlap(*candidateInterval, *occupied)) {
                    if (occupied->last ==
                        std::numeric_limits<std::uint64_t>::max()) {
                        return std::nullopt;
                    }
                    nextBase = std::max(
                        nextBase.value_or(0), occupied->last + 1);
                }
            }
            if (!nextBase || *nextBase <= candidate.baseAddress) {
                return std::nullopt;
            }
            candidate.baseAddress = *nextBase;
        }
        return std::nullopt;
    };

    if (const auto retainedBase = searchFrom(block.baseAddress)) {
        return retainedBase;
    }
    return block.baseAddress == 0 ? std::nullopt : searchFrom(0);
}

[[nodiscard]] std::optional<std::uint64_t>
fieldAbsoluteLsb(const std::vector<regmap::Field>& fields, std::string_view fieldId,
                 std::uint64_t parentLsb = 0)
{
    for (const auto& field : fields) {
        std::uint64_t absoluteLsb = 0;
        if (addOverflow(parentLsb, field.lsb, absoluteLsb)) {
            continue;
        }
        if (field.id == fieldId) {
            return absoluteLsb;
        }
        if (const auto nested = fieldAbsoluteLsb(field.members, fieldId, absoluteLsb)) {
            return nested;
        }
    }
    return std::nullopt;
}

[[nodiscard]] regmap::Register* findRegisterContainingField(regmap::Workspace& workspace,
                                                            std::string_view fieldId)
{
    for (auto& addressSpace : workspace.addressSpaces) {
        for (auto& block : addressSpace.blocks) {
            for (auto& reg : block.registers) {
                if (findFieldRecursive(reg.fields, fieldId) != nullptr) {
                    return &reg;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const regmap::Register*
findRegisterContainingField(const regmap::Workspace& workspace, std::string_view fieldId)
{
    for (const auto& addressSpace : workspace.addressSpaces) {
        for (const auto& block : addressSpace.blocks) {
            for (const auto& reg : block.registers) {
                if (findFieldRecursive(reg.fields, fieldId) != nullptr) {
                    return &reg;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::string copiedObjectId(const regmap::Workspace& workspace,
                                         std::string_view prefix,
                                         std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    for (;;) {
        std::string candidate = regmap::makeStableObjectId(workspace, prefix);
        if (generatedIds.insert(candidate).second) {
            return candidate;
        }
    }
}

void prepareCopiedEnumValue(const regmap::Workspace& workspace, regmap::EnumValue& value,
                            std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    value.id = copiedObjectId(workspace, "enum", generatedIds);
    value.source = {};
    value.propertySources.clear();
}

void prepareCopiedField(const regmap::Workspace& workspace, regmap::Field& field,
                        std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    field.id = copiedObjectId(workspace, "field", generatedIds);
    field.source = {};
    field.propertySources.clear();
    for (auto& value : field.enumValues) {
        prepareCopiedEnumValue(workspace, value, generatedIds);
    }
    for (auto& member : field.members) {
        prepareCopiedField(workspace, member, generatedIds);
    }
}

void prepareCopiedRegister(const regmap::Workspace& workspace, regmap::Register& reg,
                           std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    reg.id = copiedObjectId(workspace, "reg", generatedIds);
    reg.source = {};
    reg.propertySources.clear();
    for (auto& value : reg.enumValues) {
        prepareCopiedEnumValue(workspace, value, generatedIds);
    }
    for (auto& field : reg.fields) {
        prepareCopiedField(workspace, field, generatedIds);
    }
}

void prepareCopiedBlock(const regmap::Workspace& workspace, regmap::RegisterBlock& block,
                        std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    block.id = copiedObjectId(workspace, "block", generatedIds);
    block.source = {};
    block.propertySources.clear();
    for (auto& reg : block.registers) {
        prepareCopiedRegister(workspace, reg, generatedIds);
    }
}

void prepareCopiedPage(const regmap::Workspace& workspace, regmap::AddressSpace& page,
                       std::set<regmap::ObjectId, std::less<>>& generatedIds)
{
    page.id = copiedObjectId(workspace, "space", generatedIds);
    page.source = {};
    page.propertySources.clear();
    for (auto& block : page.blocks) {
        prepareCopiedBlock(workspace, block, generatedIds);
    }
}

template <typename Exists>
[[nodiscard]] std::string uniqueCopiedName(std::string_view original, Exists&& exists)
{
    const std::string base = original.empty() ? std::string{"Untitled"} : std::string{original};
    for (std::size_t suffix = 1;; ++suffix) {
        std::string candidate = base + " Copy";
        if (suffix > 1) {
            candidate += " " + std::to_string(suffix);
        }
        if (!exists(candidate)) {
            return candidate;
        }
    }
}

template <typename Value>
[[nodiscard]] std::string
uniqueDefaultName(const std::vector<Value>& values, std::string_view base)
{
    std::string candidate(base);
    for (std::size_t suffix = 2;
         std::ranges::any_of(
             values, [&](const Value& value) { return value.name == candidate; });
         ++suffix) {
        candidate = std::string(base) + '_' + std::to_string(suffix);
    }
    return candidate;
}

[[nodiscard]] regmap::Field
makeDefaultRegisterField(const regmap::Workspace& workspace,
                         const regmap::Register& reg, std::uint32_t bit)
{
    regmap::Field field;
    field.id = regmap::makeStableObjectId(workspace, "field");
    field.name = uniqueDefaultName(reg.fields, "NEW_FIELD");
    field.msb = bit;
    field.lsb = bit;
    field.type = regmap::FieldType::bits;
    field.softwareAccess = reg.access;
    field.hardwareAccess = regmap::AccessMode::none;
    field.resetValue =
        reg.resetValue
            ? std::optional{reg.resetValue->slice(static_cast<std::size_t>(bit), 1)}
            : std::nullopt;
    field.writeSideEffect =
        reg.access == regmap::AccessMode::writeOnly ||
                reg.access == regmap::AccessMode::readWrite
            ? regmap::WriteSideEffect::write
            : regmap::WriteSideEffect::none;
    return field;
}

[[nodiscard]] std::size_t fieldTreeSize(const std::vector<regmap::Field>& fields)
{
    std::size_t result = fields.size();
    for (const auto& field : fields) {
        result += fieldTreeSize(field.members);
    }
    return result;
}

struct RegisterDeletionImpact {
    std::size_t fieldCount{0};
    std::size_t enumValueCount{0};
    std::size_t rangeBoundCount{0};
    std::size_t tagCount{0};
    std::size_t nonZeroValueCount{0};
    std::size_t descriptionCount{0};
};

void accumulateFieldDeletionImpact(const std::vector<regmap::Field>& fields,
                                   RegisterDeletionImpact& impact);

void accumulateFieldContentsDeletionImpact(const regmap::Field& field,
                                           RegisterDeletionImpact& impact)
{
    impact.enumValueCount += field.enumValues.size();
    impact.rangeBoundCount +=
        static_cast<std::size_t>(field.minimumValue.has_value()) +
        field.maximumValue.has_value();
    impact.nonZeroValueCount += static_cast<std::size_t>(
        field.resetValue.has_value() && !field.resetValue->isZero());
    impact.descriptionCount +=
        static_cast<std::size_t>(!field.description.empty());
    impact.descriptionCount += static_cast<std::size_t>(
        std::ranges::count_if(field.enumValues, [](const regmap::EnumValue& value) {
            return !value.description.empty();
        }));
    accumulateFieldDeletionImpact(field.members, impact);
}

void accumulateFieldDeletionImpact(const std::vector<regmap::Field>& fields,
                                   RegisterDeletionImpact& impact)
{
    for (const auto& field : fields) {
        ++impact.fieldCount;
        accumulateFieldContentsDeletionImpact(field, impact);
    }
}

void accumulateRegisterDeletionImpact(const regmap::Register& reg,
                                      RegisterDeletionImpact& impact)
{
    impact.enumValueCount += reg.enumValues.size();
    impact.rangeBoundCount +=
        static_cast<std::size_t>(reg.minimumValue.has_value()) +
        reg.maximumValue.has_value();
    impact.tagCount += reg.tags.size();
    impact.nonZeroValueCount +=
        static_cast<std::size_t>(
            reg.initialValue.has_value() && !reg.initialValue->isZero()) +
        static_cast<std::size_t>(
            reg.resetValue.has_value() && !reg.resetValue->isZero());
    impact.descriptionCount +=
        static_cast<std::size_t>(!reg.description.empty()) +
        static_cast<std::size_t>(
            std::ranges::count_if(reg.enumValues, [](const regmap::EnumValue& value) {
                return !value.description.empty();
            }));
    accumulateFieldDeletionImpact(reg.fields, impact);
}

void appendDeletionCount(QStringList& output, std::size_t count,
                         const QString& singular, const QString& plural)
{
    if (count == 0) {
        return;
    }
    output << (
        count == 1
            ? QStringLiteral("1 %1").arg(singular)
            : QStringLiteral("%1 %2").arg(count).arg(plural));
}

[[nodiscard]] QStringList
configuredDeletionItems(const RegisterDeletionImpact& impact)
{
    QStringList result;
    appendDeletionCount(result, impact.fieldCount, QStringLiteral("Field"),
                        QStringLiteral("Fields/Members"));
    appendDeletionCount(result, impact.enumValueCount,
                        QStringLiteral("Enum value"),
                        QStringLiteral("Enum values"));
    appendDeletionCount(result, impact.rangeBoundCount,
                        QStringLiteral("Range bound"),
                        QStringLiteral("Range bounds"));
    appendDeletionCount(result, impact.tagCount, QStringLiteral("tag"),
                        QStringLiteral("tags"));
    appendDeletionCount(result, impact.nonZeroValueCount,
                        QStringLiteral("non-zero Initial/Reset value"),
                        QStringLiteral("non-zero Initial/Reset values"));
    appendDeletionCount(result, impact.descriptionCount,
                        QStringLiteral("description"),
                        QStringLiteral("descriptions"));
    return result;
}

[[nodiscard]] std::optional<regmap::UnsignedValue>
effectiveFieldReset(const regmap::Field& field, const regmap::Register* owner)
{
    if (field.resetValue) {
        return field.resetValue;
    }
    if (owner == nullptr || !owner->resetValue) {
        return std::nullopt;
    }
    const auto absoluteLsb = fieldAbsoluteLsb(owner->fields, field.id);
    if (!absoluteLsb) {
        return std::nullopt;
    }
    return owner->resetValue->slice(
        static_cast<std::size_t>(*absoluteLsb),
        static_cast<std::size_t>(field.width()));
}

[[nodiscard]] std::vector<regmap::EnumValue>
makeDefaultEnumValues(
    const regmap::Workspace& workspace,
    const std::vector<std::optional<regmap::UnsignedValue>>& referencedValues)
{
    std::vector<regmap::EnumValue> result;
    std::set<regmap::ObjectId, std::less<>> generatedIds;
    const auto append = [&](const regmap::UnsignedValue& value) {
        if (std::ranges::any_of(
                result, [&](const regmap::EnumValue& candidate) {
                    return candidate.value == value;
                })) {
            return;
        }
        regmap::EnumValue enumValue;
        enumValue.id = copiedObjectId(workspace, "enum", generatedIds);
        enumValue.name = uniqueDefaultName(result, "NEW_VALUE");
        enumValue.value = value;
        result.push_back(std::move(enumValue));
    };

    for (const auto& value : referencedValues) {
        if (value) {
            append(*value);
        }
    }
    if (result.empty()) {
        append(regmap::UnsignedValue(0));
    }
    return result;
}

[[nodiscard]] QString enumValuesCreatedText(std::size_t count)
{
    return count == 1
               ? QStringLiteral("NEW_VALUE created")
               : QStringLiteral("%1 Enum values created").arg(count);
}

template <typename Value>
[[nodiscard]] bool
isUniqueSiblingName(const std::vector<Value>& values, std::string_view objectId,
                    std::string_view name)
{
    return !name.empty() &&
           std::ranges::none_of(values, [&](const Value& value) {
               return value.id != objectId && value.name == name;
           });
}

[[nodiscard]] std::optional<std::uint32_t>
availableFieldLsb(const std::vector<regmap::Field>& fields,
                  std::uint64_t desiredWidth,
                  std::uint64_t containerWidth)
{
    if (desiredWidth == 0 || desiredWidth > containerWidth) {
        return std::nullopt;
    }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> occupied;
    occupied.reserve(fields.size());
    for (const auto& field : fields) {
        if (field.msb < field.lsb || field.lsb >= containerWidth) {
            continue;
        }
        occupied.emplace_back(
            field.lsb,
            std::min<std::uint64_t>(field.msb, containerWidth - 1));
    }
    std::ranges::sort(occupied);

    std::uint64_t cursor = 0;
    for (const auto& [first, last] : occupied) {
        if (first > cursor && desiredWidth <= first - cursor) {
            return static_cast<std::uint32_t>(cursor);
        }
        if (last >= cursor) {
            cursor = last + 1;
            if (cursor >= containerWidth) {
                break;
            }
        }
    }
    if (cursor < containerWidth &&
        desiredWidth <= containerWidth - cursor &&
        cursor <= std::numeric_limits<std::uint32_t>::max()) {
        return static_cast<std::uint32_t>(cursor);
    }
    return std::nullopt;
}

[[nodiscard]] const std::vector<regmap::Field>*
fieldSiblings(const std::vector<regmap::Field>& fields, std::string_view fieldId)
{
    if (std::ranges::any_of(
            fields, [&](const regmap::Field& field) { return field.id == fieldId; })) {
        return &fields;
    }
    for (const auto& field : fields) {
        if (const auto* siblings = fieldSiblings(field.members, fieldId)) {
            return siblings;
        }
    }
    return nullptr;
}

[[nodiscard]] const std::vector<regmap::Field>*
fieldSiblings(const regmap::Workspace& workspace, std::string_view fieldId)
{
    for (const auto& page : workspace.addressSpaces) {
        for (const auto& block : page.blocks) {
            for (const auto& reg : block.registers) {
                if (const auto* siblings = fieldSiblings(reg.fields, fieldId)) {
                    return siblings;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const regmap::Field*
parentFieldOf(const std::vector<regmap::Field>& fields, std::string_view fieldId)
{
    for (const auto& field : fields) {
        if (std::ranges::any_of(
                field.members,
                [&](const regmap::Field& member) { return member.id == fieldId; })) {
            return &field;
        }
        if (const auto* parent = parentFieldOf(field.members, fieldId)) {
            return parent;
        }
    }
    return nullptr;
}

[[nodiscard]] const regmap::Field*
parentFieldOf(const regmap::Workspace& workspace, std::string_view fieldId)
{
    for (const auto& page : workspace.addressSpaces) {
        for (const auto& block : page.blocks) {
            for (const auto& reg : block.registers) {
                if (const auto* parent = parentFieldOf(reg.fields, fieldId)) {
                    return parent;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const std::vector<regmap::Register>*
registerSiblings(const regmap::Workspace& workspace, std::string_view registerId)
{
    for (const auto& page : workspace.addressSpaces) {
        for (const auto& block : page.blocks) {
            if (std::ranges::any_of(
                    block.registers,
                    [&](const regmap::Register& reg) { return reg.id == registerId; })) {
                return &block.registers;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] const std::vector<regmap::RegisterBlock>*
blockSiblings(const regmap::Workspace& workspace, std::string_view blockId)
{
    for (const auto& page : workspace.addressSpaces) {
        if (std::ranges::any_of(
                page.blocks,
                [&](const regmap::RegisterBlock& block) { return block.id == blockId; })) {
            return &page.blocks;
        }
    }
    return nullptr;
}

[[nodiscard]] bool isAddressLayoutDiagnostic(std::string_view code) noexcept
{
    return code == addressWidthDiagnosticCode ||
           code == addressRangeDiagnosticCode ||
           code == strideDiagnosticCode ||
           code == registerRangeDiagnosticCode ||
           code == addressOverlapDiagnosticCode ||
           code == blockOverlapDiagnosticCode;
}

[[nodiscard]] const regmap::AddressSpace*
addressSpaceContaining(const regmap::Workspace& workspace,
                       std::string_view objectId)
{
    for (const auto& page : workspace.addressSpaces) {
        if (page.id == objectId) {
            return &page;
        }
        for (const auto& block : page.blocks) {
            if (block.id == objectId ||
                std::ranges::any_of(
                    block.registers,
                    [objectId](const regmap::Register& reg) {
                        return reg.id == objectId;
                    })) {
                return &page;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::vector<std::string>
addressLayoutIssues(const regmap::Workspace& workspace,
                    std::string_view targetId)
{
    const auto* page = addressSpaceContaining(workspace, targetId);
    if (page == nullptr) {
        return {std::string{"missing-address-owner:"} + std::string{targetId}};
    }

    std::set<std::string, std::less<>> objectIds{page->id};
    for (const auto& block : page->blocks) {
        objectIds.insert(block.id);
        for (const auto& reg : block.registers) {
            objectIds.insert(reg.id);
        }
    }

    std::vector<std::string> issues;
    for (const auto& diagnostic : regmap::validateWorkspace(workspace)) {
        if (!isAddressLayoutDiagnostic(diagnostic.code) ||
            !objectIds.contains(diagnostic.objectId)) {
            continue;
        }
        issues.push_back(
            diagnostic.code + '\n' + diagnostic.objectId + '\n' +
            diagnostic.message);
    }
    std::ranges::sort(issues);
    return issues;
}

[[nodiscard]] bool introducesNoNewIssues(
    const std::vector<std::string>& beforeIssues,
    const std::vector<std::string>& afterIssues)
{
    return std::includes(
        beforeIssues.begin(), beforeIssues.end(),
        afterIssues.begin(), afterIssues.end());
}

[[nodiscard]] std::vector<std::string>
validationIssueSignatures(const regmap::Workspace& workspace)
{
    std::vector<std::string> issues;
    for (const auto& diagnostic : regmap::validateWorkspace(workspace)) {
        issues.push_back(
            diagnostic.code + '\n' + diagnostic.objectId + '\n' +
            diagnostic.message);
    }
    std::ranges::sort(issues);
    return issues;
}

[[nodiscard]] bool workspaceEditDoesNotWorsen(
    const regmap::Workspace& before,
    const regmap::Workspace& after)
{
    return introducesNoNewIssues(
        validationIssueSignatures(before),
        validationIssueSignatures(after));
}

[[nodiscard]] bool addressEditDoesNotWorsen(
    const regmap::Workspace& before, const regmap::Workspace& after,
    std::string_view targetId)
{
    const auto beforeIssues = addressLayoutIssues(before, targetId);
    const auto afterIssues = addressLayoutIssues(after, targetId);
    return introducesNoNewIssues(beforeIssues, afterIssues);
}

[[nodiscard]] bool isFieldGeometryDiagnostic(std::string_view code) noexcept
{
    return code == fieldRangeDiagnosticCode ||
           code == fieldOverlapDiagnosticCode ||
           code == fieldTypeDiagnosticCode;
}

void collectFieldObjectIds(
    const std::vector<regmap::Field>& fields,
    std::set<std::string, std::less<>>& objectIds)
{
    for (const auto& field : fields) {
        objectIds.insert(field.id);
        collectFieldObjectIds(field.members, objectIds);
    }
}

[[nodiscard]] std::vector<std::string>
fieldGeometryIssues(const regmap::Workspace& workspace,
                    std::string_view fieldId)
{
    const auto* reg = findRegisterContainingField(workspace, fieldId);
    if (reg == nullptr) {
        return {std::string{"missing-field-owner:"} + std::string{fieldId}};
    }

    std::set<std::string, std::less<>> objectIds;
    collectFieldObjectIds(reg->fields, objectIds);
    std::vector<std::string> issues;
    for (const auto& diagnostic : regmap::validateWorkspace(workspace)) {
        if (!isFieldGeometryDiagnostic(diagnostic.code) ||
            !objectIds.contains(diagnostic.objectId)) {
            continue;
        }
        issues.push_back(
            diagnostic.code + '\n' + diagnostic.objectId + '\n' +
            diagnostic.message);
    }
    std::ranges::sort(issues);
    return issues;
}

[[nodiscard]] bool fieldGeometryEditDoesNotWorsen(
    const regmap::Workspace& before, const regmap::Workspace& after,
    std::string_view fieldId)
{
    const auto beforeIssues = fieldGeometryIssues(before, fieldId);
    const auto afterIssues = fieldGeometryIssues(after, fieldId);
    return introducesNoNewIssues(beforeIssues, afterIssues);
}

[[nodiscard]] std::vector<std::string>
numericRangeIssues(const regmap::Workspace& workspace,
                   std::string_view objectId)
{
    std::vector<std::string> issues;
    for (const auto& diagnostic : regmap::validateWorkspace(workspace)) {
        if (diagnostic.code != numericRangeDiagnosticCode ||
            diagnostic.objectId != objectId) {
            continue;
        }
        issues.push_back(
            diagnostic.code + '\n' + diagnostic.objectId + '\n' +
            diagnostic.message);
    }
    std::ranges::sort(issues);
    return issues;
}

[[nodiscard]] bool numericRangeEditDoesNotWorsen(
    const regmap::Workspace& before, const regmap::Workspace& after,
    std::string_view objectId)
{
    const auto beforeIssues = numericRangeIssues(before, objectId);
    const auto afterIssues = numericRangeIssues(after, objectId);
    return introducesNoNewIssues(beforeIssues, afterIssues);
}

[[nodiscard]] std::optional<std::size_t>
pagePosition(const regmap::Workspace& workspace, std::string_view pageId)
{
    for (std::size_t index = 0; index < workspace.addressSpaces.size(); ++index) {
        if (workspace.addressSpaces[index].id == pageId) {
            return index;
        }
    }
    return std::nullopt;
}

struct BlockPosition {
    std::size_t page{0};
    std::size_t block{0};
};

[[nodiscard]] std::optional<BlockPosition>
blockPosition(const regmap::Workspace& workspace, std::string_view blockId)
{
    for (std::size_t page = 0; page < workspace.addressSpaces.size(); ++page) {
        const auto& blocks = workspace.addressSpaces[page].blocks;
        for (std::size_t block = 0; block < blocks.size(); ++block) {
            if (blocks[block].id == blockId) {
                return BlockPosition{page, block};
            }
        }
    }
    return std::nullopt;
}

void refreshFieldResets(regmap::Field& field,
                        const std::optional<regmap::UnsignedValue>& registerReset,
                        std::uint64_t parentLsb)
{
    std::uint64_t absoluteLsb = 0;
    if (field.msb < field.lsb || addOverflow(parentLsb, field.lsb, absoluteLsb)) {
        return;
    }
    const std::size_t width = field.width();
    if (registerReset) {
        field.resetValue = registerReset->slice(static_cast<std::size_t>(absoluteLsb), width);
    } else if (field.resetValue && !field.resetValue->fitsInBits(width)) {
        field.resetValue = field.resetValue->slice(0, width);
    }
    for (auto& member : field.members) {
        refreshFieldResets(member, registerReset, absoluteLsb);
    }
}

void refreshRegisterFieldResets(regmap::Register& reg)
{
    for (auto& field : reg.fields) {
        refreshFieldResets(field, reg.resetValue, 0);
    }
}

[[nodiscard]] std::optional<regmap::UnsignedValue>
mergedRegisterReset(const regmap::Register& reg, std::string_view fieldId,
                    const regmap::UnsignedValue& fieldReset)
{
    if (!reg.resetValue) {
        return std::nullopt;
    }
    const auto* field = findFieldRecursive(reg.fields, fieldId);
    const auto absoluteLsb = fieldAbsoluteLsb(reg.fields, fieldId);
    if (field == nullptr || !absoluteLsb) {
        return std::nullopt;
    }
    return reg.resetValue->replacingSlice(
        static_cast<std::size_t>(*absoluteLsb),
        static_cast<std::size_t>(field->width()), fieldReset);
}

void assignFieldReset(regmap::Workspace& workspace, std::string_view fieldId,
                      const std::optional<regmap::UnsignedValue>& value)
{
    auto* field = regmap::findField(workspace, fieldId);
    if (field == nullptr) {
        return;
    }
    auto* reg = findRegisterContainingField(workspace, fieldId);
    if (value && reg != nullptr) {
        const auto merged = mergedRegisterReset(*reg, fieldId, *value);
        if (merged) {
            reg->resetValue = *merged;
            refreshRegisterFieldResets(*reg);
            return;
        }
    }
    field->resetValue = value;
}

[[nodiscard]] bool enumValueIsReferenced(const regmap::Workspace& workspace,
                                         const EnumOwnerContext& context,
                                         const regmap::UnsignedValue& value)
{
    if (context.registerOwner) {
        const auto* reg = regmap::findRegister(workspace, context.ownerId);
        return reg != nullptr &&
               ((reg->initialValue && *reg->initialValue == value) ||
                (reg->resetValue && *reg->resetValue == value));
    }

    const auto* field = regmap::findField(workspace, context.ownerId);
    if (field == nullptr) {
        return false;
    }
    if (field->resetValue) {
        return *field->resetValue == value;
    }
    const auto* reg = findRegisterContainingField(workspace, context.ownerId);
    if (reg == nullptr || !reg->resetValue) {
        return false;
    }
    const auto absoluteLsb = fieldAbsoluteLsb(reg->fields, context.ownerId);
    return absoluteLsb &&
           reg->resetValue
                   ->slice(static_cast<std::size_t>(*absoluteLsb),
                           static_cast<std::size_t>(field->width())) ==
               value;
}

enum class EnumDeleteBlock {
    none,
    referencedValue,
    lastEnumerationValue,
};

[[nodiscard]] EnumDeleteBlock
enumDeleteBlock(const regmap::Workspace& workspace,
                const EnumOwnerContext& context,
                const regmap::EnumValue& value)
{
    if (context.ownerType == regmap::FieldType::enumeration &&
        context.values->size() == 1) {
        return EnumDeleteBlock::lastEnumerationValue;
    }
    const bool enumerationLike =
        context.ownerType == regmap::FieldType::enumeration ||
        context.ownerType == regmap::FieldType::boolean;
    if (!enumerationLike ||
        !enumValueIsReferenced(workspace, context, value.value)) {
        return EnumDeleteBlock::none;
    }
    const bool representedAfterDeletion =
        std::ranges::any_of(
            *context.values, [&](const regmap::EnumValue& candidate) {
                return candidate.id != value.id && candidate.value == value.value;
            });
    if (representedAfterDeletion ||
        (context.ownerType == regmap::FieldType::boolean &&
         context.values->size() == 1)) {
        return EnumDeleteBlock::none;
    }
    return EnumDeleteBlock::referencedValue;
}

[[nodiscard]] bool
resetMatchesFieldEnums(const std::vector<regmap::Field>& fields,
                       const regmap::UnsignedValue& reset,
                       std::uint64_t containerWidth,
                       std::uint64_t parentLsb = 0)
{
    for (const auto& field : fields) {
        if (field.msb < field.lsb || field.msb >= containerWidth) {
            continue;
        }
        std::uint64_t absoluteLsb = 0;
        if (addOverflow(parentLsb, field.lsb, absoluteLsb)) {
            continue;
        }
        const std::size_t width = static_cast<std::size_t>(field.width());
        const bool enumerationLike =
            field.type == regmap::FieldType::enumeration ||
            field.type == regmap::FieldType::boolean;
        if (enumerationLike && !field.enumValues.empty()) {
            const regmap::UnsignedValue fieldReset =
                reset.slice(static_cast<std::size_t>(absoluteLsb), width);
            if (std::ranges::none_of(
                    field.enumValues, [&](const regmap::EnumValue& enumValue) {
                        return enumValue.value == fieldReset;
                    })) {
                return false;
            }
        }
        if (!resetMatchesFieldEnums(field.members, reset, field.width(),
                                    absoluteLsb)) {
            return false;
        }
    }
    return true;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , controller_(this)
{
    buildUi();
    buildActions();
    connectSignals();
    resize(1500, 920);
    restoreUiState();
    refreshProject();
    statusBar()->setSizeGripEnabled(false);
    statusBar()->showMessage(QStringLiteral("Open a .regmap.yaml project to begin"));
    setWindowTitle(QStringLiteral("Register Map Workbench"));
}

void MainWindow::buildUi()
{
    hierarchyModel_ = new QStandardItemModel(this);
    registerModel_ = new QStandardItemModel(this);
    fieldModel_ = new QStandardItemModel(this);
    enumModel_ = new QStandardItemModel(this);
    problemsModel_ = new QStandardItemModel(this);
    generatedModel_ = new QStandardItemModel(this);
    diffModel_ = new QStandardItemModel(this);

    auto* hierarchyTree = new HierarchyTreeView(objectIdRole, this);
    hierarchyView_ = hierarchyTree;
    hierarchyView_->setObjectName(QStringLiteral("hierarchyView"));
    hierarchyView_->setModel(hierarchyModel_);
    hierarchyView_->setHeaderHidden(true);
    hierarchyView_->setUniformRowHeights(true);
    hierarchyView_->setItemDelegate(new HierarchyItemDelegate(hierarchyView_));
    hierarchyView_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                    QAbstractItemView::EditKeyPressed);
    hierarchyView_->setExpandsOnDoubleClick(false);
    hierarchyView_->setContextMenuPolicy(Qt::CustomContextMenu);
    hierarchyView_->setSelectionMode(QAbstractItemView::SingleSelection);
    hierarchyView_->setDragEnabled(true);
    hierarchyView_->setAcceptDrops(true);
    hierarchyView_->setDropIndicatorShown(true);
    hierarchyView_->setDragDropMode(QAbstractItemView::InternalMove);
    hierarchyView_->setDefaultDropAction(Qt::MoveAction);
    hierarchyView_->setDragDropOverwriteMode(false);
    hierarchyView_->setAutoExpandDelay(600);
    hierarchyTree->setDropHandler(
        [this](const std::string& sourceId, const std::string& targetId, int placement) {
            moveHierarchyObject(sourceId, targetId, placement);
        });
    auto* hierarchyPanel = new QWidget(this);
    hierarchyPanel->setObjectName(QStringLiteral("hierarchyPanel"));
    hierarchyPanel->setMinimumWidth(220);
    auto* hierarchyLayout = new QVBoxLayout(hierarchyPanel);
    hierarchyLayout->setContentsMargins(0, 0, 0, 0);
    hierarchyLayout->setSpacing(4);
    auto* hierarchyHeader = new QWidget(hierarchyPanel);
    hierarchyHeader->setObjectName(QStringLiteral("hierarchyHeaderBar"));
    auto* hierarchyHeaderLayout = new QHBoxLayout(hierarchyHeader);
    hierarchyHeaderLayout->setContentsMargins(8, 4, 6, 4);
    hierarchyHeaderLayout->setSpacing(6);
    auto* hierarchyTitle =
        new QLabel(QStringLiteral("Workspace"), hierarchyHeader);
    hierarchyTitle->setObjectName(QStringLiteral("hierarchyTitle"));
    hierarchyTitle->setToolTip(QStringLiteral("Workspace / Pages / Blocks"));
    hierarchyAddButton_ = new QPushButton(QStringLiteral("+ Page"), hierarchyHeader);
    hierarchyAddButton_->setObjectName(QStringLiteral("hierarchyAddButton"));
    hierarchyAddButton_->setEnabled(false);
    hierarchyHeaderLayout->addWidget(hierarchyTitle, 1);
    hierarchyHeaderLayout->addWidget(hierarchyAddButton_);
    hierarchyLayout->addWidget(hierarchyHeader);
    hierarchyLayout->addWidget(hierarchyView_, 1);

    auto* registerTable = new RegisterTableView(this);
    registerView_ = registerTable;
    registerView_->setObjectName(QStringLiteral("registerView"));
    registerView_->setModel(registerModel_);
    configureTable(registerView_);
    registerView_->setItemDelegateForColumn(
        registerFieldsColumn,
        new FieldsButtonDelegate(openFieldsRole, fieldsOpenRole, registerView_));
    registerView_->setMinimumHeight(120);
    registerView_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    registerView_->setContextMenuPolicy(Qt::CustomContextMenu);
    registerTable->setBoundaryPredicate([this](int row) { return canInsertRegisterAt(row); });
    registerTable->setInsertHandler([this](int row) { insertRegisterAt(row); });
    registerTable->setCellActionPredicate(
        [this](const QModelIndex& index) { return index.data(openFieldsRole).toBool(); });
    registerTable->setCellActionHandler(
        [this](const QModelIndex& index) { openFieldsAt(index); });

    pageContextLabel_ = new QLabel(QStringLiteral("Page: —"), this);
    pageContextLabel_->setObjectName(QStringLiteral("contextTitle"));
    pageBaseEdit_ = new QLineEdit(this);
    pageBaseEdit_->setObjectName(QStringLiteral("pageBaseEdit"));
    pageBaseEdit_->setPlaceholderText(QStringLiteral("Page base"));
    pageBaseEdit_->setMaximumWidth(120);
    pageWidthEdit_ = new QLineEdit(this);
    pageWidthEdit_->setObjectName(QStringLiteral("pageWidthEdit"));
    pageWidthEdit_->setPlaceholderText(QStringLiteral("Address width"));
    pageWidthEdit_->setMaximumWidth(90);
    pageDescriptionEdit_ = new QLineEdit(this);
    pageDescriptionEdit_->setObjectName(QStringLiteral("pageDescriptionEdit"));
    pageDescriptionEdit_->setPlaceholderText(QStringLiteral("Page description"));
    pageDescriptionEdit_->setMinimumWidth(170);

    blockContextLabel_ = new QLabel(QStringLiteral("Block: —"), this);
    blockContextLabel_->setObjectName(QStringLiteral("contextTitle"));
    blockBaseEdit_ = new QLineEdit(this);
    blockBaseEdit_->setObjectName(QStringLiteral("blockBaseEdit"));
    blockBaseEdit_->setPlaceholderText(QStringLiteral("Block base"));
    blockBaseEdit_->setMaximumWidth(120);
    blockSizeEdit_ = new QLineEdit(this);
    blockSizeEdit_->setObjectName(QStringLiteral("blockSizeEdit"));
    blockSizeEdit_->setPlaceholderText(QStringLiteral("Block size"));
    blockSizeEdit_->setMaximumWidth(120);
    blockDescriptionEdit_ = new QLineEdit(this);
    blockDescriptionEdit_->setObjectName(QStringLiteral("blockDescriptionEdit"));
    blockDescriptionEdit_->setPlaceholderText(QStringLiteral("Block description"));
    blockDescriptionEdit_->setMinimumWidth(170);

    tagFilter_ = new QComboBox(this);
    tagFilter_->setObjectName(QStringLiteral("tagFilter"));
    tagFilter_->setMinimumContentsLength(16);
    tagFilter_->addItem(QStringLiteral("All tags"));
    auto* registerPanel = new QWidget(this);
    auto* registerLayout = new QVBoxLayout(registerPanel);
    registerLayout->setContentsMargins(0, 0, 0, 0);
    registerLayout->setSpacing(6);
    auto* contextBar = new QWidget(registerPanel);
    contextBar->setObjectName(QStringLiteral("registerContextBar"));
    auto* contextLayout = new QVBoxLayout(contextBar);
    contextLayout->setContentsMargins(10, 5, 10, 5);
    contextLayout->setSpacing(5);

    auto* pageRow = new QHBoxLayout;
    pageRow->setContentsMargins(0, 0, 0, 0);
    pageRow->addWidget(pageContextLabel_);
    pageRow->addWidget(new QLabel(QStringLiteral("Base"), contextBar));
    pageRow->addWidget(pageBaseEdit_);
    pageRow->addWidget(new QLabel(QStringLiteral("Address Width"), contextBar));
    pageRow->addWidget(pageWidthEdit_);
    pageRow->addWidget(new QLabel(QStringLiteral("Description"), contextBar));
    pageRow->addWidget(pageDescriptionEdit_, 1);
    pageRow->addSpacing(12);
    pageRow->addWidget(new QLabel(QStringLiteral("Tag Filter"), contextBar));
    pageRow->addWidget(tagFilter_);
    contextLayout->addLayout(pageRow);

    auto* blockRow = new QHBoxLayout;
    blockRow->setContentsMargins(0, 0, 0, 0);
    blockRow->addWidget(blockContextLabel_);
    blockRow->addWidget(new QLabel(QStringLiteral("Base"), contextBar));
    blockRow->addWidget(blockBaseEdit_);
    blockRow->addWidget(new QLabel(QStringLiteral("Size"), contextBar));
    blockRow->addWidget(blockSizeEdit_);
    blockRow->addWidget(new QLabel(QStringLiteral("Description"), contextBar));
    blockRow->addWidget(blockDescriptionEdit_, 1);
    contextLayout->addLayout(blockRow);

    registerLayout->addWidget(contextBar);
    registerLayout->addWidget(registerView_, 1);

    fieldView_ = new QTableView(this);
    fieldView_->setObjectName(QStringLiteral("fieldView"));
    fieldView_->setModel(fieldModel_);
    configureTable(fieldView_);
    fieldView_->setItemDelegateForColumn(
        fieldSoftwareAccessColumn, new AccessItemDelegate(fieldView_));
    fieldView_->setItemDelegateForColumn(
        fieldHardwareAccessColumn, new AccessItemDelegate(fieldView_));
    fieldView_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    fieldView_->setContextMenuPolicy(Qt::CustomContextMenu);

    bitfieldView_ = new BitfieldView(this);
    bitfieldView_->setObjectName(QStringLiteral("bitfieldView"));
    fieldPanel_ = new QWidget(this);
    fieldPanel_->setObjectName(QStringLiteral("fieldPanel"));
    auto* fieldPanel = fieldPanel_;
    auto* fieldLayout = new QVBoxLayout(fieldPanel);
    fieldLayout->setContentsMargins(0, 0, 0, 0);
    fieldLayout->setSpacing(6);
    fieldHeaderBar_ = new QWidget(fieldPanel);
    fieldHeaderBar_->setObjectName(QStringLiteral("fieldHeaderBar"));
    auto* fieldHeaderLayout = new QHBoxLayout(fieldHeaderBar_);
    fieldHeaderLayout->setContentsMargins(10, 5, 8, 5);
    fieldHeaderLayout->setSpacing(8);
    fieldContextLabel_ = new QLabel(QStringLiteral("Fields"), fieldHeaderBar_);
    fieldContextLabel_->setObjectName(QStringLiteral("fieldContextLabel"));
    closeFieldsButton_ = new QPushButton(QStringLiteral("Close Fields"), fieldHeaderBar_);
    closeFieldsButton_->setObjectName(QStringLiteral("closeFieldsButton"));
    closeFieldsButton_->setToolTip(QStringLiteral("Close the Field workspace"));
    closeFieldsButton_->setAutoDefault(false);
    fieldHeaderLayout->addWidget(fieldContextLabel_);
    fieldHeaderLayout->addStretch(1);
    fieldHeaderLayout->addWidget(closeFieldsButton_);
    enumPanel_ = new QWidget(fieldPanel);
    enumPanel_->setObjectName(QStringLiteral("enumPanel"));
    enumPanel_->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* enumLayout = new QVBoxLayout(enumPanel_);
    enumLayout->setContentsMargins(0, 0, 0, 0);
    enumLayout->setSpacing(4);
    enumContextLabel_ = new QLabel(QStringLiteral("Enum Values"), enumPanel_);
    enumContextLabel_->setObjectName(QStringLiteral("contextTitle"));
    enumView_ = new QTableView(enumPanel_);
    enumView_->setObjectName(QStringLiteral("enumView"));
    enumView_->setModel(enumModel_);
    configureTable(enumView_);
    enumView_->setEditTriggers(
        QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    enumView_->setContextMenuPolicy(Qt::CustomContextMenu);
    enumView_->setFixedHeight(112);
    enumLayout->addWidget(enumContextLabel_);
    enumLayout->addWidget(enumView_);

    fieldLayout->addWidget(fieldHeaderBar_);
    fieldLayout->addWidget(bitfieldView_);
    fieldLayout->addWidget(fieldView_, 1);
    fieldLayout->addWidget(enumPanel_, 0, Qt::AlignTop);

    editorSplitter_ = new QSplitter(Qt::Vertical, this);
    editorSplitter_->setObjectName(
        QStringLiteral("editorSplitter"));
    editorSplitter_->addWidget(registerPanel);
    editorSplitter_->addWidget(fieldPanel);
    editorSplitter_->setStretchFactor(0, 3);
    editorSplitter_->setStretchFactor(1, 2);
    editorSplitter_->setChildrenCollapsible(false);
    editorSplitter_->setHandleWidth(4);

    workspaceSplitter_ = new QSplitter(Qt::Horizontal, this);
    workspaceSplitter_->setObjectName(
        QStringLiteral("workspaceSplitter"));
    workspaceSplitter_->addWidget(hierarchyPanel);
    workspaceSplitter_->addWidget(editorSplitter_);
    workspaceSplitter_->setStretchFactor(0, 0);
    workspaceSplitter_->setStretchFactor(1, 1);
    workspaceSplitter_->setChildrenCollapsible(false);
    workspaceSplitter_->setHandleWidth(4);
    workspaceSplitter_->setSizes({230, 1260});

    problemsView_ = new QTableView(this);
    problemsView_->setObjectName(QStringLiteral("problemsView"));
    problemsView_->setModel(problemsModel_);
    configureTable(problemsView_);
    generatedView_ = new QTableView(this);
    generatedView_->setObjectName(QStringLiteral("generatedView"));
    generatedView_->setModel(generatedModel_);
    configureTable(generatedView_);
    diffView_ = new QTableView(this);
    diffView_->setObjectName(QStringLiteral("diffView"));
    diffView_->setModel(diffModel_);
    configureTable(diffView_);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("resultTabs"));
    tabs_->setDocumentMode(true);
    tabs_->addTab(problemsView_, QStringLiteral("Problems"));
    auto* generatedPanel = new QWidget(tabs_);
    auto* generatedLayout = new QVBoxLayout(generatedPanel);
    generatedLayout->setContentsMargins(0, 0, 0, 0);
    generatedLayout->setSpacing(4);
    auto* generatedActions = new QHBoxLayout;
    generatedActions->setContentsMargins(6, 4, 6, 0);
    generatedActions->addStretch();
    retryOutputsButton_ = new QPushButton(QStringLiteral("Retry outputs"), generatedPanel);
    retryOutputsButton_->setObjectName(QStringLiteral("retryOutputsButton"));
    retryOutputsButton_->setVisible(false);
    generatedActions->addWidget(retryOutputsButton_);
    generatedLayout->addLayout(generatedActions);
    generatedLayout->addWidget(generatedView_, 1);
    tabs_->addTab(generatedPanel, QStringLiteral("Generated"));
    auto* diffPanel = new QWidget(tabs_);
    auto* diffLayout = new QVBoxLayout(diffPanel);
    diffLayout->setContentsMargins(0, 0, 0, 0);
    diffLayout->setSpacing(4);
    conflictBar_ = new QWidget(diffPanel);
    conflictBar_->setObjectName(QStringLiteral("conflictBar"));
    auto* conflictLayout = new QHBoxLayout(conflictBar_);
    conflictLayout->setContentsMargins(8, 5, 8, 5);
    conflictSummaryLabel_ = new QLabel(conflictBar_);
    conflictSummaryLabel_->setObjectName(QStringLiteral("conflictSummaryLabel"));
    conflictLayout->addWidget(conflictSummaryLabel_, 1);
    keepWorkbenchButton_ =
        new QPushButton(QStringLiteral("Keep Workbench changes"), conflictBar_);
    keepWorkbenchButton_->setObjectName(QStringLiteral("keepWorkbenchButton"));
    keepWorkbenchButton_->setToolTip(
        QStringLiteral("Use Workbench values for every listed conflict, then synchronize."));
    conflictLayout->addWidget(keepWorkbenchButton_);
    useRtlButton_ = new QPushButton(QStringLiteral("Use RTL changes"), conflictBar_);
    useRtlButton_->setObjectName(QStringLiteral("useRtlButton"));
    useRtlButton_->setToolTip(
        QStringLiteral("Use RTL values for every listed conflict, then synchronize."));
    conflictLayout->addWidget(useRtlButton_);
    conflictBar_->setVisible(false);
    diffLayout->addWidget(conflictBar_);
    diffLayout->addWidget(diffView_, 1);
    tabs_->addTab(diffPanel, QStringLiteral("Diff"));
    tabs_->setMinimumHeight(170);

    resultsSplitter_ = new QSplitter(Qt::Vertical, this);
    resultsSplitter_->setObjectName(
        QStringLiteral("resultsSplitter"));
    resultsSplitter_->addWidget(workspaceSplitter_);
    resultsSplitter_->addWidget(tabs_);
    resultsSplitter_->setStretchFactor(0, 1);
    resultsSplitter_->setStretchFactor(1, 0);
    resultsSplitter_->setChildrenCollapsible(false);
    resultsSplitter_->setHandleWidth(4);
    resultsSplitter_->setContentsMargins(6, 6, 6, 6);
    resultsSplitter_->setSizes({690, 230});
    setCentralWidget(resultsSplitter_);
}

void MainWindow::buildActions()
{
    auto* newAction = new QAction(QStringLiteral("New Project..."), this);
    newAction->setObjectName(QStringLiteral("newProjectAction"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        QString path =
            QFileDialog::getSaveFileName(
                this, QStringLiteral("Create register-map project"),
                projectChooserDirectory(controller_.manifestPath()),
                QStringLiteral(
                    "Register Map Project (*.regmap.yaml)"));
        if (path.isEmpty()) {
            return;
        }
        if (!path.endsWith(QStringLiteral(".regmap.yaml"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".regmap.yaml");
        }
        if (!confirmProjectReplacement()) {
            return;
        }
        if (!controller_.createProject(path)) {
            const QString detail = statusBar()->currentMessage().isEmpty()
                ? QStringLiteral(
                      "The register-map project could not be created.")
                : statusBar()->currentMessage();
            QMessageBox::critical(this, QStringLiteral("Create Project"),
                                  detail);
            return;
        }
        rememberRecentProject(fromPath(controller_.manifestPath()));
    });

    auto* openAction = new QAction(QStringLiteral("Open Project…"), this);
    openAction->setObjectName(QStringLiteral("openProjectAction"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Open register-map project"),
            projectChooserDirectory(controller_.manifestPath()),
            QStringLiteral("Register Map Project (*.regmap.yaml *.yaml *.yml);;All "
                           "Files (*)"));
        if (!path.isEmpty()) {
            if (confirmProjectReplacement()) {
                if (!openProjectPath(path)) {
                    reportProjectOpenFailure();
                    return;
                }
                rememberRecentProject(path);
            }
        }
    });

    saveAction_ = new QAction(QStringLiteral("Save && Sync"), this);
    saveAction_->setObjectName(QStringLiteral("saveSyncAction"));
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setEnabled(false);
    connect(saveAction_, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        controller_.save();
    });

    reloadAction_ = new QAction(QStringLiteral("Reload from Disk"), this);
    reloadAction_->setShortcut(QKeySequence::Refresh);
    reloadAction_->setEnabled(false);
    connect(reloadAction_, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        if (controller_.isDirty()) {
            const std::size_t changeCount = controller_.changes().size();
            const QString changeSummary =
                changeCount == 0
                    ? QStringLiteral("the unsaved Workbench edits")
                    : (changeCount == 1
                           ? QStringLiteral("1 unsaved Workbench change")
                           : QStringLiteral("%1 unsaved Workbench changes")
                                 .arg(changeCount));
            QMessageBox dialog(this);
            dialog.setIcon(QMessageBox::Warning);
            dialog.setWindowTitle(QStringLiteral("Reload Project"));
            dialog.setText(
                QStringLiteral("Discard %1 and reload %2 from disk?")
                    .arg(
                        changeSummary,
                        QFileInfo(fromPath(controller_.manifestPath()))
                            .fileName()));
            dialog.setInformativeText(
                QStringLiteral(
                    "The current Workbench model will be replaced by the saved "
                    "project and managed RTL. Undo history for the discarded "
                    "local edits will be cleared.\n\nUse Save & Sync first if "
                    "you need to keep these edits."));
            auto* reload = dialog.addButton(
                QStringLiteral("Discard and Reload"),
                QMessageBox::DestructiveRole);
            reload->setObjectName(
                QStringLiteral("confirmReloadFromDiskButton"));
            auto* cancel = dialog.addButton(QMessageBox::Cancel);
            dialog.setDefaultButton(cancel);
            dialog.setEscapeButton(cancel);
            dialog.exec();
            if (dialog.clickedButton() != reload) {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Reload cancelled · unsaved Workbench edits kept"),
                    7000);
                return;
            }
        }
        controller_.reload();
    });

    undoAction_ = new QAction(QStringLiteral("Undo"), this);
    undoAction_->setObjectName(
        QStringLiteral("undoAction"));
    undoAction_->setShortcut(QKeySequence::Undo);
    undoAction_->setEnabled(false);
    connect(undoAction_, &QAction::triggered, this, [this] {
        QWidget* focus = QApplication::focusWidget();
        if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
            edit->undo();
            return;
        }
        if (focusUsesChoiceEditor(focus)) {
            return;
        }
        controller_.undo();
    });

    redoAction_ = new QAction(QStringLiteral("Redo"), this);
    redoAction_->setObjectName(
        QStringLiteral("redoAction"));
    redoAction_->setShortcut(QKeySequence::Redo);
    redoAction_->setEnabled(false);
    connect(redoAction_, &QAction::triggered, this, [this] {
        QWidget* focus = QApplication::focusWidget();
        if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
            edit->redo();
            return;
        }
        if (focusUsesChoiceEditor(focus)) {
            return;
        }
        controller_.redo();
    });

    generateAction_ = new QAction(QStringLiteral("Generate"), this);
    generateAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+G")));
    generateAction_->setEnabled(false);
    connect(generateAction_, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        controller_.generateNow();
    });

    synchronizeAction_ = new QAction(QStringLiteral("Synchronize RTL"), this);
    synchronizeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+S")));
    synchronizeAction_->setEnabled(false);
    connect(synchronizeAction_, &QAction::triggered, this, [this] {
        if (!commitActiveEditor()) {
            return;
        }
        controller_.synchronizeNow();
    });

    useWorkbenchAction_ = new QAction(QStringLiteral("Resolve Conflicts Using Workbench"), this);
    useWorkbenchAction_->setEnabled(false);
    connect(useWorkbenchAction_, &QAction::triggered, this,
            [this] { resolveConflictsWithConfirmation(false); });
    useRtlAction_ = new QAction(QStringLiteral("Resolve Conflicts Using RTL"), this);
    useRtlAction_->setEnabled(false);
    connect(useRtlAction_, &QAction::triggered, this,
            [this] { resolveConflictsWithConfirmation(true); });

    openSourceAction_ = new QAction(QStringLiteral("Open Source"), this);
    openSourceAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));
    openSourceAction_->setEnabled(false);
    connect(openSourceAction_, &QAction::triggered, this, [this] {
        if (currentSource_.has_value()) {
            openSource(*currentSource_);
        }
    });

    auto* addEnumAction = new QAction(QStringLiteral("Add Enum Value"), this);
    connect(addEnumAction, &QAction::triggered, this, &MainWindow::addEnumValue);

    deleteAction_ = new QAction(QStringLiteral("Delete Selected Object"), this);
    deleteAction_->setObjectName(
        QStringLiteral("deleteSelectionAction"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    deleteAction_->setEnabled(false);
    connect(deleteAction_, &QAction::triggered, this, &MainWindow::deleteSelection);

    copyAction_ = new QAction(QStringLiteral("Copy"), this);
    copyAction_->setObjectName(
        QStringLiteral("copySelectionAction"));
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this, &MainWindow::copySelection);
    pasteAction_ = new QAction(QStringLiteral("Paste"), this);
    pasteAction_->setObjectName(
        QStringLiteral("pasteSelectionAction"));
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, &MainWindow::pasteSelection);

    duplicateAction_ =
        new QAction(QStringLiteral("Duplicate Register / Field"), this);
    duplicateAction_->setObjectName(
        QStringLiteral("duplicateSelectionAction"));
    duplicateAction_->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+D")));
    duplicateAction_->setShortcutContext(
        Qt::WidgetWithChildrenShortcut);
    duplicateAction_->setEnabled(false);
    registerView_->addAction(duplicateAction_);
    fieldView_->addAction(duplicateAction_);
    connect(duplicateAction_, &QAction::triggered,
            this, &MainWindow::duplicateFocusedObject);

    auto* findAction = new QAction(QStringLiteral("Find"), this);
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this] {
        globalSearchEdit_->setFocus();
        globalSearchEdit_->selectAll();
    });
    auto* findNextAction = new QAction(QStringLiteral("Find Next"), this);
    findNextAction->setShortcut(QKeySequence(Qt::Key_F3));
    connect(findNextAction, &QAction::triggered, this, [this] { runSearch(false); });
    auto* findPreviousAction = new QAction(QStringLiteral("Find Previous"), this);
    findPreviousAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));
    connect(findPreviousAction, &QAction::triggered, this, [this] { runSearch(true); });

    showAdvancedFieldsAction_ = new QAction(QStringLiteral("Show Advanced Field Columns"), this);
    showAdvancedFieldsAction_->setObjectName(
        QStringLiteral("showAdvancedFieldsAction"));
    showAdvancedFieldsAction_->setCheckable(true);
    showAdvancedFieldsAction_->setChecked(false);
    connect(showAdvancedFieldsAction_, &QAction::toggled, this,
            [this] { applyFieldColumnVisibility(); });

    auto* exitAction = new QAction(QStringLiteral("Exit"), this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    recentProjectsMenu_ =
        fileMenu->addMenu(QStringLiteral("Open Recent"));
    recentProjectsMenu_->setObjectName(
        QStringLiteral("recentProjectsMenu"));
    rebuildRecentProjectsMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(saveAction_);
    fileMenu->addAction(reloadAction_);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    editMenu->addAction(undoAction_);
    editMenu->addAction(redoAction_);
    editMenu->addSeparator();
    editMenu->addAction(copyAction_);
    editMenu->addAction(pasteAction_);
    editMenu->addAction(duplicateAction_);
    editMenu->addSeparator();
    editMenu->addAction(findAction);
    editMenu->addAction(findNextAction);
    editMenu->addAction(findPreviousAction);
    editMenu->addSeparator();
    editMenu->addAction(addEnumAction);
    editMenu->addAction(deleteAction_);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    viewMenu->addAction(showAdvancedFieldsAction_);

    QMenu* projectMenu = menuBar()->addMenu(QStringLiteral("Project"));
    projectMenu->addAction(synchronizeAction_);
    projectMenu->addAction(generateAction_);
    projectMenu->addSeparator();
    projectMenu->addAction(useWorkbenchAction_);
    projectMenu->addAction(useRtlAction_);
    projectMenu->addAction(openSourceAction_);

    QToolBar* toolbar = addToolBar(QStringLiteral("Project"));
    toolbar->setObjectName(QStringLiteral("projectToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction_);

    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    globalSearchEdit_ = new QLineEdit(toolbar);
    globalSearchEdit_->setObjectName(QStringLiteral("globalSearchEdit"));
    globalSearchEdit_->setClearButtonEnabled(true);
    globalSearchEdit_->setPlaceholderText(
        QStringLiteral("Search name, address, tag, description…  Ctrl+F"));
    globalSearchEdit_->setToolTip(
        QStringLiteral("Press Enter or F3 for the next match; Shift+F3 for the previous match."));
    toolbar->addWidget(globalSearchEdit_);
    searchResultLabel_ = new QLabel(toolbar);
    searchResultLabel_->setObjectName(QStringLiteral("searchResultLabel"));
    toolbar->addWidget(searchResultLabel_);
    syncStateLabel_ = new QLabel(QStringLiteral("No project"), toolbar);
    syncStateLabel_->setObjectName(QStringLiteral("syncStateBadge"));
    syncStateLabel_->setProperty("state", QStringLiteral("idle"));
    toolbar->addWidget(syncStateLabel_);

    connect(globalSearchEdit_, &QLineEdit::textChanged, this, [this] {
        searchQuery_.clear();
        searchResults_.clear();
        searchResultIndex_ = -1;
        searchResultLabel_->clear();
    });
    connect(globalSearchEdit_, &QLineEdit::returnPressed, this,
            [this] { runSearch(false); });
    connect(retryOutputsButton_, &QPushButton::clicked, &controller_,
            &ProjectController::generateNow);
    connect(keepWorkbenchButton_, &QPushButton::clicked, this,
            [this] { resolveConflictsWithConfirmation(false); });
    connect(useRtlButton_, &QPushButton::clicked, this,
            [this] { resolveConflictsWithConfirmation(true); });
}

void MainWindow::connectSignals()
{
    connect(&controller_, &ProjectController::projectChanged, this, [this] {
        searchQuery_.clear();
        searchResults_.clear();
        searchResultIndex_ = -1;
        searchResultLabel_->clear();
        requestProjectRefresh();
    });
    connect(&controller_, &ProjectController::diagnosticsChanged, this, [this] {
        if (modelEditInProgress_) {
            QTimer::singleShot(0, this, &MainWindow::refreshDiagnostics);
        } else {
            refreshDiagnostics();
        }
    });
    connect(&controller_, &ProjectController::generationChanged, this,
            &MainWindow::refreshGenerated);
    connect(&controller_, &ProjectController::editStateChanged, this,
            &MainWindow::updateEditActions);
    connect(&controller_, &ProjectController::conflictsChanged, this, &MainWindow::refreshDiff);
    connect(&controller_, &ProjectController::syncStatusChanged, this,
            [this](const QString& text) {
                statusBar()->showMessage(text);
                updateSyncPresentation(text);
            });

    connect(hierarchyView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (refreshing_) {
                    return;
                }
                selectedAddressId_ = current.data(addressIdRole).toString().toUtf8().toStdString();
                selectedBlockId_ = current.data(blockIdRole).toString().toUtf8().toStdString();
                selectedRegisterId_.clear();
                selectedFieldId_.clear();
                openFieldsRegisterId_.clear();
                updateContextBar();
                populateRegisters();
                const auto* workspace = controller_.workspace();
                const std::string objectId =
                    current.data(objectIdRole).toString().toUtf8().toStdString();
                if (workspace != nullptr && objectId == workspace->id) {
                    setCurrentSource({});
                } else if (const auto* block = findBlock(selectedBlockId_)) {
                    setCurrentSource(block->source);
                } else if (const auto* page = findAddressSpace(selectedAddressId_)) {
                    setCurrentSource(page->source);
                } else {
                    setCurrentSource({});
                }
                updateHierarchyAddAction();
                updateEditActions();
            });
    connect(registerView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (refreshing_) {
                    return;
                }
                if (current.data(addRowRole).toBool()) {
                    return;
                }
                const QModelIndex first = registerModel_->index(current.row(), 0);
                const std::string nextRegisterId =
                    first.data(objectIdRole).toString().toUtf8().toStdString();
                if (nextRegisterId != selectedRegisterId_) {
                    selectedFieldId_.clear();
                    if (openFieldsRegisterId_ != nextRegisterId) {
                        const std::string closedRegisterId = openFieldsRegisterId_;
                        openFieldsRegisterId_.clear();
                        updateFieldsAction(closedRegisterId);
                    }
                }
                selectedRegisterId_ = nextRegisterId;
                const regmap::Register* reg = findRegister(selectedRegisterId_);
                populateFields(reg);
                setCurrentSource(reg == nullptr ? regmap::SourceLocation{} : reg->source);
                updateContextBar();
                updateEditActions();
            });
    connect(fieldView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current) {
                if (refreshing_) {
                    return;
                }
                if (current.data(addRowRole).toBool()) {
                    return;
                }
                const QModelIndex first = fieldModel_->index(current.row(), 0);
                selectedFieldId_ = first.data(objectIdRole).toString().toUtf8().toStdString();
                const regmap::Register* reg = findRegister(selectedRegisterId_);
                const regmap::Field* field =
                    reg == nullptr ? nullptr : findField(*reg, selectedFieldId_);
                bitfieldView_->setSelectedField(field);
                populateEnumValues(reg, field);
                setCurrentSource(field == nullptr ? regmap::SourceLocation{} : field->source);
                updateEditActions();
            });
    connect(bitfieldView_, &BitfieldView::fieldActivated, this,
            [this](const QString& id) { selectField(id.toUtf8().toStdString()); });
    connect(bitfieldView_, &BitfieldView::fieldDragPreview, this,
            [this](const QString&, std::uint32_t lsb, std::uint32_t msb) {
                statusBar()->showMessage(QStringLiteral("Moving field [%1:%2]").arg(msb).arg(lsb));
            });
    connect(bitfieldView_, &BitfieldView::fieldMoveRequested, this,
            [this](const QString& id, std::uint32_t lsb, std::uint32_t msb) {
                moveField(id.toUtf8().toStdString(), lsb, msb);
            });
    connect(closeFieldsButton_, &QPushButton::clicked, this, &MainWindow::closeFields);

    connect(hierarchyAddButton_, &QPushButton::clicked, this, [this] {
        if (controller_.workspace() == nullptr) {
            return;
        }
        const QModelIndex current = hierarchyView_->currentIndex();
        if (current.isValid() && !current.data(blockIdRole).toString().isEmpty()) {
            addRegister();
            return;
        }
        if (current.isValid() && !current.data(addressIdRole).toString().isEmpty()) {
            addBlock();
            return;
        }
        addAddressSpace();
    });

    connect(registerView_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (index.data(addRowRole).toBool()) {
            if (index.column() == registerNameColumn) {
                addRegister();
            }
            return;
        }
        if (index.data(openFieldsRole).toBool()) {
            openFieldsAt(index);
            return;
        }
    });
    connect(registerView_, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) {
                if (index.column() != registerTagsColumn &&
                    index.column() != registerAccessColumn) {
                    return;
                }
                const QPersistentModelIndex target(index);
                QTimer::singleShot(0, this, [this, target] {
                    if (!target.isValid()) {
                        return;
                    }
                    if (target.column() == registerTagsColumn) {
                        editRegisterTags(target);
                    } else {
                        editRegisterAccess(target);
                    }
                });
    });
    connect(fieldView_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (index.data(addRowRole).toBool()) {
            if (index.column() == fieldNameColumn) {
                addField();
            }
        }
    });
    connect(enumView_, &QTableView::clicked, this, [this](const QModelIndex& index) {
        if (index.data(addRowRole).toBool()) {
            if (index.column() == enumNameColumn) {
                addEnumValue();
            }
        }
    });
    connect(enumView_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        const QModelIndex index = enumView_->indexAt(position);
        const std::string id = index.data(objectIdRole).toString().toUtf8().toStdString();
        if (id.empty()) {
            return;
        }
        const auto* workspace = controller_.workspace();
        const auto* enumValue =
            workspace == nullptr ? nullptr : regmap::findEnumValue(*workspace, id);
        if (enumValue == nullptr) {
            return;
        }
        const auto ownerContext = enumOwnerContext(*workspace, id);
        const EnumDeleteBlock deleteBlock =
            ownerContext ? enumDeleteBlock(*workspace, *ownerContext, *enumValue)
                         : EnumDeleteBlock::none;
        const QString label = fromUtf8(enumValue->name);
        QMenu menu(this);
        menu.setObjectName(QStringLiteral("enumContextMenu"));
        QAction* remove = menu.addAction(QStringLiteral("Delete Enum Value"));
        remove->setObjectName(QStringLiteral("deleteEnumValueAction"));
        if (menu.exec(enumView_->viewport()->mapToGlobal(position)) != remove) {
            return;
        }
        if (deleteBlock == EnumDeleteBlock::lastEnumerationValue) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot delete %1: an Enumeration must retain at least one value. "
                    "Change its Type instead.")
                    .arg(label),
                7000);
            return;
        }
        if (deleteBlock == EnumDeleteBlock::referencedValue) {
            statusBar()->showMessage(
                (ownerContext->registerOwner
                     ? QStringLiteral(
                           "Cannot delete %1: this value is used by Register "
                           "Initial/Reset. Change the reference first.")
                     : QStringLiteral(
                           "Cannot delete %1: this value is used by Field Reset. "
                           "Change the reference first."))
                    .arg(label),
                7000);
            return;
        }
        if (controller_.editWorkspace(
                QStringLiteral("Delete enum value"),
                [id](regmap::Workspace& candidate) {
                    static_cast<void>(regmap::removeObject(candidate, id));
                })) {
            refreshProject();
            statusBar()->showMessage(
                QStringLiteral("Deleted enum value %1 · Ctrl+Z to restore").arg(label),
                5000);
        }
    });
    connect(hierarchyView_, &QWidget::customContextMenuRequested, this,
            &MainWindow::showHierarchyContextMenu);
    connect(registerView_, &QWidget::customContextMenuRequested, this,
            &MainWindow::showRegisterContextMenu);
    connect(fieldView_, &QWidget::customContextMenuRequested, this,
            &MainWindow::showFieldContextMenu);
    connect(tagFilter_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (refreshing_) {
            return;
        }
        selectedTagFilter_ =
            index <= 0 ? std::string{} : tagFilter_->itemText(index).toUtf8().toStdString();
        populateRegisters();
    });
    connect(pageBaseEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            pageBaseEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* page = findAddressSpace(id)) {
            applyPropertyEdit(page->id, "base", pageBaseEdit_->text());
        }
    });
    connect(blockBaseEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            blockBaseEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* block = findBlock(id)) {
            applyPropertyEdit(block->id, "base", blockBaseEdit_->text());
        }
    });
    connect(pageWidthEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            pageWidthEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* page = findAddressSpace(id)) {
            applyPropertyEdit(page->id, "address_width", pageWidthEdit_->text());
        }
    });
    connect(pageDescriptionEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            pageDescriptionEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* page = findAddressSpace(id)) {
            applyPropertyEdit(page->id, "description", pageDescriptionEdit_->text());
        }
    });
    connect(blockSizeEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            blockSizeEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* block = findBlock(id)) {
            applyPropertyEdit(block->id, "size", blockSizeEdit_->text());
        }
    });
    connect(blockDescriptionEdit_, &QLineEdit::editingFinished, this, [this] {
        const std::string id =
            blockDescriptionEdit_->property("objectId").toString().toUtf8().toStdString();
        if (const auto* block = findBlock(id)) {
            applyPropertyEdit(block->id, "description", blockDescriptionEdit_->text());
        }
    });
    connect(problemsView_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int diagnosticIndex =
            problemsModel_->index(index.row(), 0).data(rowIndexRole).toInt();
        if (diagnosticIndex >= 0 &&
            static_cast<std::size_t>(diagnosticIndex) < controller_.diagnostics().size()) {
            const auto& diagnostic =
                controller_.diagnostics()[static_cast<std::size_t>(diagnosticIndex)];
            if (diagnostic.objectId.empty() || !navigateToObject(diagnostic.objectId)) {
                openSource(diagnostic.source);
            }
        }
    });
    connect(generatedView_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int artifactIndex = generatedModel_->index(index.row(), 0).data(rowIndexRole).toInt();
        if (artifactIndex >= 0 &&
            static_cast<std::size_t>(artifactIndex) < controller_.artifacts().size()) {
            if (!SourceNavigation::openFile(
                    controller_.artifacts()[static_cast<std::size_t>(artifactIndex)].path)) {
                statusBar()->showMessage(QStringLiteral("Could not open generated artifact"));
            }
        }
    });
    connect(diffView_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int changeIndex = diffModel_->index(index.row(), 0).data(rowIndexRole).toInt();
        if (changeIndex >= 0 &&
            static_cast<std::size_t>(changeIndex) < controller_.changes().size()) {
            const auto& change = controller_.changes()[static_cast<std::size_t>(changeIndex)];
            if (!navigateToObject(change.id)) {
                openSource(change.afterSource.empty() ? change.beforeSource : change.afterSource);
            }
        } else {
            const std::string objectId =
                diffModel_->index(index.row(), 3).data().toString().toUtf8().toStdString();
            static_cast<void>(navigateToObject(objectId));
        }
    });

    const auto handleModelEdit = [this](QStandardItem* changedItem) {
        if (refreshing_ || changedItem == nullptr) {
            return;
        }
        const std::string objectId =
            changedItem->data(objectIdRole).toString().toUtf8().toStdString();
        const std::string property =
            changedItem->data(propertyRole).toString().toUtf8().toStdString();
        if (!objectId.empty() && !property.empty()) {
            {
                const QScopedValueRollback editGuard(modelEditInProgress_, true);
                applyPropertyEdit(objectId, property, changedItem->text());
            }
            if (refreshPending_) {
                QTimer::singleShot(0, this, [this] {
                    if (modelEditInProgress_ || !refreshPending_) {
                        return;
                    }
                    refreshPending_ = false;
                    refreshProject();
                });
            }
        }
    };
    connect(hierarchyModel_, &QStandardItemModel::itemChanged, this, handleModelEdit);
    connect(registerModel_, &QStandardItemModel::itemChanged, this, handleModelEdit);
    connect(fieldModel_, &QStandardItemModel::itemChanged, this, handleModelEdit);
    connect(enumModel_, &QStandardItemModel::itemChanged, this, handleModelEdit);
}

bool MainWindow::commitActiveEditor()
{
    activeEditorCommitRejected_ = false;
    if (auto* edit = qobject_cast<QLineEdit*>(QApplication::focusWidget())) {
        const QScopedValueRollback commitGuard(committingActiveEditor_, true);
        edit->clearFocus();
    } else if (auto* combo =
                   qobject_cast<QComboBox*>(QApplication::focusWidget())) {
        const QScopedValueRollback commitGuard(committingActiveEditor_, true);
        combo->clearFocus();
    }
    return !activeEditorCommitRejected_;
}

void MainWindow::restoreUiState()
{
    QSettings settings;
    const QByteArray geometry =
        settings.value(
                    QString::fromLatin1(
                        mainWindowGeometrySettingsKey))
            .toByteArray();
    if (!geometry.isEmpty()) {
        static_cast<void>(restoreGeometry(geometry));
    }
    const auto restoreSplitter =
        [&settings](QSplitter* splitter, const char* key) {
            const QByteArray state =
                settings.value(QString::fromLatin1(key))
                    .toByteArray();
            if (!state.isEmpty()) {
                static_cast<void>(
                    splitter->restoreState(state));
            }
        };
    restoreSplitter(
        workspaceSplitter_, workspaceSplitterSettingsKey);
    restoreSplitter(
        editorSplitter_, editorSplitterSettingsKey);
    restoreSplitter(
        resultsSplitter_, resultsSplitterSettingsKey);
    expandedEditorSplitterState_ =
        editorSplitter_->saveState();
    registerHeaderState_ =
        settings.value(
                    QString::fromLatin1(
                        registerHeaderSettingsKey))
            .toByteArray();
    fieldHeaderState_ =
        settings.value(
                    QString::fromLatin1(
                        fieldHeaderSettingsKey))
            .toByteArray();
    showAdvancedFieldsAction_->setChecked(
        settings.value(
                    QString::fromLatin1(
                        advancedFieldsSettingsKey),
                    false)
            .toBool());
    applyFieldColumnVisibility();
}

void MainWindow::saveUiState() const
{
    QSettings settings;
    settings.setValue(
        QString::fromLatin1(mainWindowGeometrySettingsKey),
        saveGeometry());
    settings.setValue(
        QString::fromLatin1(workspaceSplitterSettingsKey),
        workspaceSplitter_->saveState());
    QByteArray editorState =
        expandedEditorSplitterState_;
    if (!enumOnlyEditorLayout_ &&
        fieldPanel_->isVisible()) {
        editorState = editorSplitter_->saveState();
    }
    if (editorState.isEmpty()) {
        editorState = editorSplitter_->saveState();
    }
    settings.setValue(
        QString::fromLatin1(editorSplitterSettingsKey),
        editorState);
    settings.setValue(
        QString::fromLatin1(resultsSplitterSettingsKey),
        resultsSplitter_->saveState());
    settings.setValue(
        QString::fromLatin1(advancedFieldsSettingsKey),
        showAdvancedFieldsAction_->isChecked());
    const QByteArray registerHeaderState =
        registerColumnsInitialized_
            ? registerView_->horizontalHeader()->saveState()
            : registerHeaderState_;
    const QByteArray fieldHeaderState =
        fieldColumnsInitialized_
            ? fieldView_->horizontalHeader()->saveState()
            : fieldHeaderState_;
    if (!registerHeaderState.isEmpty()) {
        settings.setValue(
            QString::fromLatin1(
                registerHeaderSettingsKey),
            registerHeaderState);
    }
    if (!fieldHeaderState.isEmpty()) {
        settings.setValue(
            QString::fromLatin1(fieldHeaderSettingsKey),
            fieldHeaderState);
    }
    settings.sync();
}

void MainWindow::updateEditorPanelMode(
    bool hasOpenFieldEditor, bool hasEnumEditor)
{
    const bool nextEnumOnly =
        hasEnumEditor && !hasOpenFieldEditor;
    if (!enumOnlyEditorLayout_ &&
        fieldPanel_->isVisible()) {
        expandedEditorSplitterState_ =
            editorSplitter_->saveState();
    }

    if (nextEnumOnly) {
        enumOnlyEditorLayout_ = true;
        fieldPanel_->setMaximumHeight(
            enumPanel_->sizeHint().height());
        fieldPanel_->setVisible(true);
        fieldPanel_->updateGeometry();
        return;
    }

    enumOnlyEditorLayout_ = false;
    fieldPanel_->setMaximumHeight(
        QWIDGETSIZE_MAX);
    fieldPanel_->setVisible(
        hasOpenFieldEditor);
    fieldPanel_->updateGeometry();
    if (hasOpenFieldEditor &&
        !expandedEditorSplitterState_.isEmpty()) {
        static_cast<void>(
            editorSplitter_->restoreState(
                expandedEditorSplitterState_));
    }
}

MainWindow::UnsavedChoice MainWindow::promptUnsavedChanges(bool closing)
{
    const QString changeSummary =
        unsavedChangeSummary(controller_.changes().size());

    QString projectName =
        QFileInfo(fromPath(controller_.manifestPath())).fileName();
    if (projectName.isEmpty() && controller_.workspace() != nullptr) {
        projectName = fromUtf8(controller_.workspace()->name);
    }
    if (projectName.isEmpty()) {
        projectName = QStringLiteral("the current project");
    }

    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(QStringLiteral("Unsaved Register Map"));
    dialog.setText(
        QStringLiteral("Save %1 in %2 before %3?")
            .arg(changeSummary, projectName,
                 closing ? QStringLiteral("closing")
                         : QStringLiteral("opening another project")));
    dialog.setInformativeText(
        closing
            ? QStringLiteral(
                  "Save & Sync writes the current Workbench model, "
                  "synchronizes managed RTL, and updates the read-only "
                  "outputs.\n\nDiscard closes the application without "
                  "saving these local edits. Their Undo history is cleared, "
                  "and the edits cannot be recovered after the application "
                  "closes.")
            : QStringLiteral(
                  "Save & Sync writes the current Workbench model before "
                  "the project changes.\n\nDiscard lets the selected project "
                  "replace this one without saving these local edits. If "
                  "replacement succeeds, their Undo history is cleared and "
                  "the edits cannot be recovered."));

    auto* save = dialog.addButton(QMessageBox::Save);
    save->setObjectName(QStringLiteral("saveUnsavedChangesButton"));
    auto* discard = dialog.addButton(QMessageBox::Discard);
    discard->setObjectName(QStringLiteral("discardUnsavedChangesButton"));
    auto* cancel = dialog.addButton(QMessageBox::Cancel);
    cancel->setObjectName(QStringLiteral("cancelUnsavedChangesButton"));
    dialog.setDefaultButton(save);
    dialog.setEscapeButton(cancel);
    dialog.exec();

    if (dialog.clickedButton() == save) {
        return UnsavedChoice::save;
    }
    if (dialog.clickedButton() == discard) {
        return UnsavedChoice::discard;
    }
    return UnsavedChoice::cancel;
}

bool MainWindow::confirmProjectReplacement()
{
    if (!commitActiveEditor()) {
        return false;
    }
    if (!controller_.isDirty()) {
        return true;
    }

    const UnsavedChoice choice = promptUnsavedChanges(false);
    if (choice == UnsavedChoice::cancel) {
        statusBar()->showMessage(
            QStringLiteral(
                "Project change cancelled · %1 kept")
                .arg(unsavedChangeSummary(controller_.changes().size())),
            7000);
        return false;
    }
    if (choice == UnsavedChoice::save) {
        controller_.save();
        return !controller_.isDirty();
    }

    return true;
}

bool MainWindow::openProjectPath(const QString& path)
{
    return controller_.openProject(path);
}

bool MainWindow::openStartupProjectPath(const QString& path)
{
    if (!openProjectPath(path)) {
        reportProjectOpenFailure();
        return false;
    }
    rememberRecentProject(fromPath(controller_.manifestPath()));
    return true;
}

void MainWindow::reportProjectOpenFailure()
{
    const QString detail =
        statusBar()->currentMessage().isEmpty()
            ? QStringLiteral(
                  "The register-map project could not be opened.")
            : statusBar()->currentMessage();
    QMessageBox::critical(
        this, QStringLiteral("Open Project"), detail);
}

void MainWindow::rebuildRecentProjectsMenu()
{
    if (recentProjectsMenu_ == nullptr) {
        return;
    }
    recentProjectsMenu_->clear();
    QSettings settings;
    const QStringList paths =
        settings.value(
                    QString::fromLatin1(recentProjectsSettingsKey))
            .toStringList();
    if (paths.isEmpty()) {
        QAction* empty = recentProjectsMenu_->addAction(
            QStringLiteral("(No recent projects)"));
        empty->setEnabled(false);
        return;
    }

    for (qsizetype index = 0; index < paths.size(); ++index) {
        const QString path = paths[index];
        const QFileInfo information(path);
        const QString label =
            QStringLiteral("&%1 %2 — %3")
                .arg(index + 1)
                .arg(information.fileName(),
                     QDir::toNativeSeparators(
                         information.absolutePath()));
        QAction* action = recentProjectsMenu_->addAction(label);
        action->setObjectName(QStringLiteral("recentProjectAction"));
        action->setData(path);
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this,
                [this, path] { openRecentProject(path); });
    }

    recentProjectsMenu_->addSeparator();
    QAction* clear = recentProjectsMenu_->addAction(
        QStringLiteral("Clear Recent Projects"));
    clear->setObjectName(QStringLiteral("clearRecentProjectsAction"));
    connect(clear, &QAction::triggered, this, [this] {
        QSettings settings;
        settings.remove(
            QString::fromLatin1(recentProjectsSettingsKey));
        settings.sync();
        QTimer::singleShot(0, this, [this] {
            rebuildRecentProjectsMenu();
        });
        statusBar()->showMessage(
            QStringLiteral("Recent projects cleared"), 4000);
    });
}

void MainWindow::rememberRecentProject(const QString& path)
{
    const QString normalized = normalizedProjectPath(path);
    if (normalized.isEmpty()) {
        return;
    }
    QSettings settings;
    QStringList paths =
        settings.value(
                    QString::fromLatin1(recentProjectsSettingsKey))
            .toStringList();
    paths.removeIf([&](const QString& candidate) {
        return sameProjectPath(candidate, normalized);
    });
    paths.prepend(normalized);
    while (paths.size() > maximumRecentProjectCount) {
        paths.removeLast();
    }
    settings.setValue(
        QString::fromLatin1(recentProjectsSettingsKey), paths);
    settings.sync();
    QTimer::singleShot(0, this, [this] {
        rebuildRecentProjectsMenu();
    });
}

void MainWindow::removeRecentProject(const QString& path)
{
    QSettings settings;
    QStringList paths =
        settings.value(
                    QString::fromLatin1(recentProjectsSettingsKey))
            .toStringList();
    paths.removeIf([&](const QString& candidate) {
        return sameProjectPath(candidate, path);
    });
    settings.setValue(
        QString::fromLatin1(recentProjectsSettingsKey), paths);
    settings.sync();
}

void MainWindow::openRecentProject(const QString& path)
{
    if (!commitActiveEditor()) {
        return;
    }
    const QString normalized = normalizedProjectPath(path);
    const QFileInfo information(normalized);
    if (!information.exists() || !information.isFile()) {
        removeRecentProject(normalized);
        QMessageBox::warning(
            this, QStringLiteral("Recent Project Missing"),
            QStringLiteral(
                "Could not find:\n%1\n\nThe entry was removed from "
                "Open Recent. The current project is unchanged.")
                .arg(QDir::toNativeSeparators(normalized)));
        QTimer::singleShot(0, this, [this] {
            rebuildRecentProjectsMenu();
        });
        statusBar()->showMessage(
            QStringLiteral(
                "Recent project missing · entry removed · current project unchanged"),
            7000);
        return;
    }
    if (!confirmProjectReplacement()) {
        return;
    }
    if (!openProjectPath(normalized)) {
        reportProjectOpenFailure();
        return;
    }
    rememberRecentProject(normalized);
}

void MainWindow::requestProjectRefresh()
{
    if (!modelEditInProgress_) {
        refreshPending_ = false;
        refreshProject();
        return;
    }
    if (refreshPending_) {
        return;
    }
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        if (modelEditInProgress_ || !refreshPending_) {
            return;
        }
        refreshPending_ = false;
        refreshProject();
    });
}

void MainWindow::refreshProject()
{
    const auto& manifestPath = controller_.manifestPath();
    if (manifestPath != displayedManifestPath_) {
        displayedManifestPath_ = manifestPath;
        selectedAddressId_.clear();
        selectedBlockId_.clear();
        selectedRegisterId_.clear();
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        selectedTagFilter_.clear();
        searchQuery_.clear();
        searchResults_.clear();
        searchResultIndex_ = -1;
        globalSearchEdit_->clear();
        searchResultLabel_->clear();
        hierarchyInitialized_ = false;
    }
    populateHierarchy();
    updateHierarchyAddAction();
    updateContextBar();
    updateTagFilter();
    populateRegisters();
    refreshDiff();
    reloadAction_->setEnabled(!controller_.manifestPath().empty());
    generateAction_->setEnabled(controller_.workspace() != nullptr &&
                                !controller_.hasProjectErrors());
    updateEditActions();

    if (const auto* workspace = controller_.workspace()) {
        setWindowTitle(fromUtf8(workspace->name) +
                       (controller_.isDirty() ? QStringLiteral(" *") : QString{}) +
                       QStringLiteral(" — Register Map Workbench"));
    } else {
        setWindowTitle(QStringLiteral("Register Map Workbench"));
    }
    updateSyncPresentation();
    updateBottomPanelVisibility();
}

void MainWindow::updateContextBar()
{
    std::string pageId = selectedAddressId_;
    std::string blockId = selectedBlockId_;
    if (const auto* workspace = controller_.workspace()) {
        bool foundContext = false;
        for (const auto& candidatePage : workspace->addressSpaces) {
            for (const auto& candidateBlock : candidatePage.blocks) {
                const bool selectedBlock = !blockId.empty() && candidateBlock.id == blockId;
                const bool selectedRegister =
                    !selectedRegisterId_.empty() &&
                    std::ranges::any_of(candidateBlock.registers, [&](const regmap::Register& reg) {
                        return reg.id == selectedRegisterId_;
                    });
                if (selectedBlock || selectedRegister) {
                    pageId = candidatePage.id;
                    blockId = candidateBlock.id;
                    foundContext = true;
                    break;
                }
            }
            if (foundContext) {
                break;
            }
        }
    }
    const auto* page = findAddressSpace(pageId);
    const auto* block = findBlock(blockId);
    const QSignalBlocker pageBaseBlocker(pageBaseEdit_);
    const QSignalBlocker pageWidthBlocker(pageWidthEdit_);
    const QSignalBlocker pageDescriptionBlocker(pageDescriptionEdit_);
    const QSignalBlocker blockBaseBlocker(blockBaseEdit_);
    const QSignalBlocker blockSizeBlocker(blockSizeEdit_);
    const QSignalBlocker blockDescriptionBlocker(blockDescriptionEdit_);

    const QString pageObjectId = page == nullptr ? QString{} : fromUtf8(page->id);
    for (QLineEdit* edit : {pageBaseEdit_, pageWidthEdit_, pageDescriptionEdit_}) {
        edit->setProperty("objectId", pageObjectId);
        edit->setEnabled(page != nullptr);
    }
    pageContextLabel_->setText(page == nullptr
                                   ? QStringLiteral("Page: select in tree")
                                   : QStringLiteral("Page: %1").arg(fromUtf8(page->name)));
    pageBaseEdit_->setText(page == nullptr ? QString{} : hex(page->baseAddress));
    pageWidthEdit_->setText(page == nullptr ? QString{} : QString::number(page->addressWidth));
    pageDescriptionEdit_->setText(page == nullptr ? QString{} : fromUtf8(page->description));

    const QString blockObjectId = block == nullptr ? QString{} : fromUtf8(block->id);
    for (QLineEdit* edit : {blockBaseEdit_, blockSizeEdit_, blockDescriptionEdit_}) {
        edit->setProperty("objectId", blockObjectId);
        edit->setEnabled(block != nullptr);
    }
    blockContextLabel_->setText(block == nullptr
                                    ? QStringLiteral("Block: select in tree")
                                    : QStringLiteral("Block: %1").arg(fromUtf8(block->name)));
    blockBaseEdit_->setText(block == nullptr ? QString{} : hex(block->baseAddress));
    blockSizeEdit_->setText(block == nullptr || !block->size ? QString{} : hex(*block->size));
    blockDescriptionEdit_->setText(block == nullptr ? QString{} : fromUtf8(block->description));
}

void MainWindow::populateHierarchy()
{
    QScopedValueRollback guard(refreshing_, true);
    const bool restoreExpansion =
        hierarchyInitialized_;
    std::set<std::string> expandedObjects;
    QString previousCurrentObject;
    const int previousVerticalScroll =
        restoreExpansion
            ? hierarchyView_->verticalScrollBar()->value() : 0;
    if (restoreExpansion) {
        previousCurrentObject =
            hierarchyView_->currentIndex()
                .data(objectIdRole)
                .toString();
        const auto captureExpansion =
            [&](const auto& self,
                const QModelIndex& parent) -> void {
                for (int row = 0;
                     row < hierarchyModel_->rowCount(parent);
                     ++row) {
                    const QModelIndex index =
                        hierarchyModel_->index(row, 0, parent);
                    if (hierarchyView_->isExpanded(index)) {
                        expandedObjects.insert(
                            index.data(objectIdRole)
                                .toString()
                                .toUtf8()
                                .toStdString());
                    }
                    self(self, index);
                }
            };
        captureExpansion(captureExpansion, {});
    }
    hierarchyModel_->clear();
    hierarchyModel_->setHorizontalHeaderLabels({QStringLiteral("Workspace / Pages / Blocks")});
    const regmap::Workspace* workspace = controller_.workspace();
    if (workspace == nullptr) {
        hierarchyInitialized_ = false;
        return;
    }

    auto* workspaceItem =
        editableItem(fromUtf8(workspace->name), workspace->id, "name", objectIdRole, propertyRole);
    workspaceItem->setData(fromUtf8(workspace->id), objectIdRole);
    Qt::ItemFlags workspaceFlags = workspaceItem->flags();
    workspaceFlags.setFlag(Qt::ItemIsDragEnabled, false);
    workspaceFlags.setFlag(Qt::ItemIsDropEnabled, true);
    workspaceItem->setFlags(workspaceFlags);
    hierarchyModel_->appendRow(workspaceItem);
    QModelIndex selected = workspaceItem->index();
    for (const auto& addressSpace : workspace->addressSpaces) {
        auto* addressItem = editableItem(fromUtf8(addressSpace.name), addressSpace.id, "name",
                                         objectIdRole, propertyRole);
        addressItem->setData(fromUtf8(addressSpace.id), objectIdRole);
        addressItem->setData(fromUtf8(addressSpace.id), addressIdRole);
        Qt::ItemFlags addressFlags = addressItem->flags();
        addressFlags.setFlag(Qt::ItemIsDragEnabled, true);
        addressFlags.setFlag(Qt::ItemIsDropEnabled, true);
        addressItem->setFlags(addressFlags);
        workspaceItem->appendRow(addressItem);
        if (addressSpace.id == selectedAddressId_ && selectedBlockId_.empty()) {
            selected = addressItem->index();
        }
        for (const auto& block : addressSpace.blocks) {
            auto* blockItem =
                editableItem(fromUtf8(block.name), block.id, "name", objectIdRole, propertyRole);
            blockItem->setData(fromUtf8(block.id), objectIdRole);
            blockItem->setData(fromUtf8(addressSpace.id), addressIdRole);
            blockItem->setData(fromUtf8(block.id), blockIdRole);
            Qt::ItemFlags blockFlags = blockItem->flags();
            blockFlags.setFlag(Qt::ItemIsDragEnabled, true);
            blockFlags.setFlag(Qt::ItemIsDropEnabled, true);
            blockItem->setFlags(blockFlags);
            addressItem->appendRow(blockItem);
            if (addressSpace.id == selectedAddressId_ && block.id == selectedBlockId_) {
                selected = blockItem->index();
            }
        }
    }
    if (!restoreExpansion) {
        hierarchyView_->expandAll();
    } else {
        const auto restoreObjectExpansion =
            [&](const auto& self,
                const QModelIndex& parent) -> void {
                for (int row = 0;
                     row < hierarchyModel_->rowCount(parent);
                     ++row) {
                    const QModelIndex index =
                        hierarchyModel_->index(row, 0, parent);
                    const std::string id =
                        index.data(objectIdRole)
                            .toString()
                            .toUtf8()
                            .toStdString();
                    hierarchyView_->setExpanded(
                        index, expandedObjects.contains(id));
                    self(self, index);
                }
            };
        restoreObjectExpansion(restoreObjectExpansion, {});
    }
    if (selected.isValid()) {
        const std::string newAddressId =
            selected.data(addressIdRole).toString().toUtf8().toStdString();
        const std::string newBlockId = selected.data(blockIdRole).toString().toUtf8().toStdString();
        if (newAddressId != selectedAddressId_ || newBlockId != selectedBlockId_) {
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
        }
        selectedAddressId_ = newAddressId;
        selectedBlockId_ = newBlockId;
        hierarchyView_->setCurrentIndex(selected);
        const QString selectedObject =
            selected.data(objectIdRole).toString();
        if (restoreExpansion &&
            selectedObject != previousCurrentObject) {
            for (QModelIndex parent = selected.parent();
                 parent.isValid();
                 parent = parent.parent()) {
                hierarchyView_->setExpanded(parent, true);
            }
            hierarchyView_->scrollTo(selected);
        } else if (restoreExpansion) {
            hierarchyView_->verticalScrollBar()->setValue(
                previousVerticalScroll);
        }
    }
    hierarchyInitialized_ = true;
    hierarchyView_->resizeColumnToContents(0);
}

void MainWindow::updateHierarchyAddAction()
{
    if (controller_.workspace() == nullptr) {
        hierarchyAddButton_->setText(QStringLiteral("+ Page"));
        hierarchyAddButton_->setToolTip(
            QStringLiteral("Open or create a project before adding a Page"));
        hierarchyAddButton_->setEnabled(false);
        return;
    }

    const QModelIndex current = hierarchyView_->currentIndex();
    if (current.isValid() && !current.data(blockIdRole).toString().isEmpty()) {
        hierarchyAddButton_->setText(QStringLiteral("+ Register"));
        hierarchyAddButton_->setToolTip(
            QStringLiteral("Add a Register to the selected Block"));
    } else if (current.isValid() && !current.data(addressIdRole).toString().isEmpty()) {
        hierarchyAddButton_->setText(QStringLiteral("+ Block"));
        hierarchyAddButton_->setToolTip(
            QStringLiteral("Add a Register Block to the selected Page"));
    } else {
        hierarchyAddButton_->setText(QStringLiteral("+ Page"));
        hierarchyAddButton_->setToolTip(
            QStringLiteral("Add a Page to this Workspace"));
    }
    hierarchyAddButton_->setEnabled(true);
}

void MainWindow::populateRegisters()
{
    QScopedValueRollback guard(refreshing_, true);
    const TableSelectionSnapshot previousSelection =
        captureTableSelection(registerView_, objectIdRole, propertyRole);
    const std::string preferredRegister = selectedRegisterId_;
    if (registerColumnsInitialized_) {
        registerHeaderState_ =
            registerView_->horizontalHeader()->saveState();
    }
    registerModel_->clear();
    registerModel_->setHorizontalHeaderLabels(
        {QStringLiteral("Register"), QStringLiteral("Offset"), QStringLiteral("Address"),
         QStringLiteral("Width"), QStringLiteral("Type"), QStringLiteral("Fields"),
         QStringLiteral("Range"), QStringLiteral("Initial"), QStringLiteral("Reset"),
         QStringLiteral("Access"), QStringLiteral("Tags"), QStringLiteral("Description")});

    const regmap::Workspace* workspace = controller_.workspace();
    if (workspace == nullptr) {
        if (!registerHeaderState_.isEmpty() &&
            registerView_->horizontalHeader()->restoreState(
                registerHeaderState_)) {
            registerColumnsInitialized_ = true;
        }
        openFieldsRegisterId_.clear();
        populateFields(nullptr);
        setCurrentSource({});
        return;
    }

    int preferredRow = -1;
    for (const auto& addressSpace : workspace->addressSpaces) {
        if (!selectedAddressId_.empty() && addressSpace.id != selectedAddressId_) {
            continue;
        }
        for (const auto& block : addressSpace.blocks) {
            if (!selectedBlockId_.empty() && block.id != selectedBlockId_) {
                continue;
            }
            for (const auto& reg : block.registers) {
                if (!selectedTagFilter_.empty() &&
                    std::ranges::find(reg.tags, selectedTagFilter_) == reg.tags.end()) {
                    continue;
                }
                std::uint64_t address = 0;
                std::uint64_t blockAddress = 0;
                const bool overflow =
                    addOverflow(addressSpace.baseAddress, block.baseAddress, blockAddress) ||
                    addOverflow(blockAddress, reg.offset, address);
                QList<QStandardItem*> row;
                auto* name =
                    editableItem(fromUtf8(reg.name), reg.id, "name", objectIdRole, propertyRole);
                name->setData(fromUtf8(reg.id), objectIdRole);
                name->setData(fromUtf8(addressSpace.id), addressIdRole);
                name->setData(fromUtf8(block.id), blockIdRole);
                const bool canOpenFields =
                    !reg.reserved && reg.type == regmap::FieldType::structure;
                const bool fieldsOpen =
                    canOpenFields && openFieldsRegisterId_ == reg.id;
                auto* fieldsAction =
                    item(canOpenFields
                             ? QStringLiteral("%1 (%2)")
                                   .arg(fieldsOpen ? QStringLiteral("Editing")
                                                   : QStringLiteral("Open"))
                                   .arg(reg.fields.size())
                             : QString{});
                fieldsAction->setData(canOpenFields, openFieldsRole);
                fieldsAction->setData(fieldsOpen, fieldsOpenRole);
                fieldsAction->setData(fromUtf8(reg.id), objectIdRole);
                fieldsAction->setToolTip(
                    canOpenFields
                        ? (fieldsOpen ? QStringLiteral("These fields are open below")
                                      : QStringLiteral("Open and edit this register's fields"))
                        : QString{});
                row << name
                    << editableItem(hex(reg.offset), reg.id, "offset", objectIdRole, propertyRole)
                    << item(overflow ? QStringLiteral("overflow") : hex(address))
                    << editableItem(QString::number(reg.width), reg.id, "width", objectIdRole,
                                    propertyRole)
                    << editableItem(registerTypeText(reg), reg.id, "type", objectIdRole,
                                    propertyRole)
                    << fieldsAction
                    << editableItem(rangeText(reg.minimumValue, reg.maximumValue), reg.id, "range",
                                    objectIdRole, propertyRole)
                    << editableItem(valueText(reg.initialValue), reg.id, "initial", objectIdRole,
                                    propertyRole)
                    << editableItem(valueText(reg.resetValue), reg.id, "reset", objectIdRole,
                                    propertyRole)
                    << pasteableItem(accessText(reg.access).toUpper(), reg.id, "access",
                                     objectIdRole, propertyRole)
                    << pasteableItem(tagsText(reg.tags), reg.id, "tags", objectIdRole,
                                     propertyRole)
                    << editableItem(fromUtf8(reg.description), reg.id, "description", objectIdRole,
                                    propertyRole);
                if (reg.reserved) {
                    for (auto* current : row) {
                        QFont font = current->font();
                        font.setWeight(QFont::DemiBold);
                        current->setFont(font);
                        current->setForeground(QColor(QStringLiteral("#B3261E")));
                    }
                }
                const int rowNumber = registerModel_->rowCount();
                registerModel_->appendRow(row);
                if (reg.id == preferredRegister) {
                    preferredRow = rowNumber;
                }
            }
        }
    }

    QList<QStandardItem*> addRow;
    for (int column = 0; column <= registerDescriptionColumn; ++column) {
        addRow << addRowItem(column == registerNameColumn ? QStringLiteral("+") : QString{},
                             addRowRole);
    }
    registerModel_->appendRow(addRow);
    const int dataRowCount = registerModel_->rowCount() - 1;
    const bool restoredHeader =
        !registerHeaderState_.isEmpty() &&
        registerView_->horizontalHeader()->restoreState(
            registerHeaderState_);
    if (!restoredHeader) {
        registerView_->resizeColumnsToContents();
        registerView_->setColumnWidth(
            registerFieldsColumn,
            std::max(
                registerView_->columnWidth(
                    registerFieldsColumn),
                112));
    }
    if (restoredHeader || dataRowCount > 0) {
        registerColumnsInitialized_ = true;
        registerHeaderState_ =
            registerView_->horizontalHeader()->saveState();
    }
    if (preferredRow < 0 && dataRowCount > 0) {
        preferredRow = 0;
    }
    if (preferredRow >= 0) {
        registerView_->setCurrentIndex(registerModel_->index(preferredRow, 0));
        const std::string id = registerModel_->index(preferredRow, 0)
                                   .data(objectIdRole)
                                   .toString()
                                   .toUtf8()
                                   .toStdString();
        if (id != selectedRegisterId_) {
            selectedFieldId_.clear();
            if (openFieldsRegisterId_ != id) {
                openFieldsRegisterId_.clear();
            }
        }
        selectedRegisterId_ = id;
        const regmap::Register* selectedRegister = findRegister(selectedRegisterId_);
        populateFields(selectedRegister);
        if (selectedFieldId_.empty()) {
            setCurrentSource(selectedRegister == nullptr ? regmap::SourceLocation{}
                                                         : selectedRegister->source);
        }
    } else {
        selectedRegisterId_.clear();
        openFieldsRegisterId_.clear();
        populateFields(nullptr);
        setCurrentSource({});
    }
    if (previousSelection.current &&
        previousSelection.current->objectId == selectedRegisterId_) {
        restoreTableSelection(
            registerView_, previousSelection, objectIdRole, propertyRole);
    }
    updateContextBar();
}

void MainWindow::populateFields(const regmap::Register* reg)
{
    QScopedValueRollback guard(refreshing_, true);
    const TableSelectionSnapshot previousSelection =
        captureTableSelection(fieldView_, objectIdRole, propertyRole);
    const std::string preferredField = selectedFieldId_;
    if (fieldColumnsInitialized_) {
        fieldHeaderState_ =
            fieldView_->horizontalHeader()->saveState();
    }
    fieldModel_->clear();
    fieldModel_->setHorizontalHeaderLabels(
        {QStringLiteral("Field"), QStringLiteral("Parent"), QStringLiteral("MSB"),
         QStringLiteral("LSB"), QStringLiteral("Width"), QStringLiteral("Type"),
         QStringLiteral("Minimum"), QStringLiteral("Maximum"), QStringLiteral("SW"),
         QStringLiteral("HW"), QStringLiteral("Reset"), QStringLiteral("Read Effect"),
         QStringLiteral("Write Effect"), QStringLiteral("Description")});
    bitfieldView_->setRegister(reg);
    const bool hasFieldEditor =
        reg != nullptr && !reg->reserved && reg->type == regmap::FieldType::structure;
    if (!hasFieldEditor && reg != nullptr && openFieldsRegisterId_ == reg->id) {
        openFieldsRegisterId_.clear();
    }
    const bool showFieldEditor =
        hasFieldEditor && openFieldsRegisterId_ == reg->id;
    fieldHeaderBar_->setVisible(showFieldEditor);
    bitfieldView_->setVisible(showFieldEditor);
    fieldView_->setVisible(showFieldEditor);
    if (!showFieldEditor) {
        selectedFieldId_.clear();
        if (!fieldHeaderState_.isEmpty() &&
            fieldView_->horizontalHeader()->restoreState(
                fieldHeaderState_)) {
            fieldColumnsInitialized_ = true;
        }
        restoreTableSelection(
            fieldView_, previousSelection, objectIdRole, propertyRole);
        populateEnumValues(reg, nullptr);
        applyFieldColumnVisibility();
        return;
    }
    fieldContextLabel_->setText(
        QStringLiteral("Fields — %1 · %2 field(s)")
            .arg(fromUtf8(reg->name))
            .arg(reg->fields.size()));

    int preferredRow = -1;
    const auto appendFields = [&](const auto& self, const std::vector<regmap::Field>& fields,
                                  const std::string& parentPath) -> void {
        for (const auto& field : fields) {
            QList<QStandardItem*> row;
            auto* name =
                editableItem(fromUtf8(field.name), field.id, "name", objectIdRole, propertyRole);
            name->setData(fromUtf8(field.id), objectIdRole);
            row << name << item(fromUtf8(parentPath))
                << editableItem(QString::number(field.msb), field.id, "msb", objectIdRole,
                                propertyRole)
                << item(QString::number(field.lsb))
                << editableItem(QString::number(field.width()), field.id, "field_width",
                                objectIdRole, propertyRole)
                << editableItem(fieldTypeText(field), field.id, "type", objectIdRole, propertyRole)
                << editableItem(field.minimumValue ? fromUtf8(*field.minimumValue) : QString{},
                                field.id, "minimum", objectIdRole, propertyRole)
                << editableItem(field.maximumValue ? fromUtf8(*field.maximumValue) : QString{},
                                field.id, "maximum", objectIdRole, propertyRole)
                << editableItem(accessText(field.softwareAccess), field.id, "sw_access",
                                objectIdRole, propertyRole)
                << editableItem(accessText(field.hardwareAccess), field.id, "hw_access",
                                objectIdRole, propertyRole)
                << editableItem(valueText(field.resetValue), field.id, "reset", objectIdRole,
                                propertyRole)
                << editableItem(readSideEffectText(field.readSideEffect), field.id,
                                "read_side_effect", objectIdRole, propertyRole)
                << editableItem(writeSideEffectText(field.writeSideEffect), field.id,
                                "write_side_effect", objectIdRole, propertyRole)
                << editableItem(fromUtf8(field.description), field.id, "description", objectIdRole,
                                propertyRole);
            const int rowNumber = fieldModel_->rowCount();
            fieldModel_->appendRow(row);
            if (field.id == preferredField) {
                preferredRow = rowNumber;
            }
            const std::string path =
                parentPath.empty() ? field.name : parentPath + '.' + field.name;
            self(self, field.members, path);
        }
    };
    appendFields(appendFields, reg->fields, {});
    if (!reg->reserved) {
        QList<QStandardItem*> addRow;
        for (int column = 0; column <= fieldDescriptionColumn; ++column) {
            addRow << addRowItem(column == fieldNameColumn ? QStringLiteral("+") : QString{},
                                 addRowRole);
        }
        fieldModel_->appendRow(addRow);
    }
    const int fieldCount = fieldModel_->rowCount() - (reg->reserved ? 0 : 1);
    const bool restoredHeader =
        !fieldHeaderState_.isEmpty() &&
        fieldView_->horizontalHeader()->restoreState(
            fieldHeaderState_);
    if (!restoredHeader) {
        fieldView_->resizeColumnsToContents();
    }
    if (restoredHeader || fieldCount > 0) {
        fieldColumnsInitialized_ = true;
        fieldHeaderState_ =
            fieldView_->horizontalHeader()->saveState();
    }
    if (preferredRow < 0 && fieldCount > 0) {
        preferredRow = 0;
    }
    if (preferredRow >= 0) {
        fieldView_->setCurrentIndex(fieldModel_->index(preferredRow, 0));
        selectedFieldId_ = fieldModel_->index(preferredRow, 0)
                               .data(objectIdRole)
                               .toString()
                               .toUtf8()
                               .toStdString();
        const regmap::Field* selectedField = findField(*reg, selectedFieldId_);
        bitfieldView_->setSelectedField(selectedField);
        populateEnumValues(reg, selectedField);
        setCurrentSource(selectedField == nullptr ? regmap::SourceLocation{}
                                                  : selectedField->source);
    } else {
        selectedFieldId_.clear();
        populateEnumValues(reg, nullptr);
        setCurrentSource(reg->source);
    }
    applyFieldColumnVisibility();
    if (previousSelection.current &&
        previousSelection.current->objectId == selectedFieldId_) {
        restoreTableSelection(
            fieldView_, previousSelection, objectIdRole, propertyRole);
    }
}

void MainWindow::populateEnumValues(const regmap::Register* reg, const regmap::Field* field)
{
    QScopedValueRollback guard(refreshing_, true);
    const TableSelectionSnapshot previousSelection =
        captureTableSelection(enumView_, objectIdRole, propertyRole);
    enumView_->reset();
    enumModel_->clear();
    enumModel_->setHorizontalHeaderLabels(
        {QStringLiteral("Name"), QStringLiteral("Value"), QStringLiteral("Description")});

    const std::vector<regmap::EnumValue>* enumValues = nullptr;
    regmap::FieldType type = regmap::FieldType::bits;
    QString ownerKind;
    QString ownerName;
    if (field != nullptr) {
        enumValues = &field->enumValues;
        type = field->type;
        ownerKind = QStringLiteral("Field");
        ownerName = fromUtf8(field->name);
    } else if (reg != nullptr) {
        enumValues = &reg->enumValues;
        type = reg->type;
        ownerKind = QStringLiteral("Register");
        ownerName = fromUtf8(reg->name);
    }
    const bool visible =
        enumValues != nullptr && (type == regmap::FieldType::boolean ||
                                  type == regmap::FieldType::enumeration || !enumValues->empty());
    enumPanel_->setVisible(visible);
    const bool hasOpenFieldEditor =
        reg != nullptr && !reg->reserved && reg->type == regmap::FieldType::structure &&
        openFieldsRegisterId_ == reg->id;
    updateEditorPanelMode(
        hasOpenFieldEditor, visible);
    if (!visible) {
        restoreTableSelection(
            enumView_, previousSelection, objectIdRole, propertyRole);
        return;
    }

    enumContextLabel_->setText(QStringLiteral("%1 Enum Values — %2").arg(ownerKind, ownerName));
    if (type == regmap::FieldType::boolean && enumValues->empty()) {
        auto* falseDescription = item(QStringLiteral("Implicit"));
        falseDescription->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        enumModel_->appendRow(
            {item(QStringLiteral("FALSE")), item(QStringLiteral("0")), falseDescription});
        auto* trueDescription = item(QStringLiteral("Implicit"));
        trueDescription->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        enumModel_->appendRow(
            {item(QStringLiteral("TRUE")), item(QStringLiteral("1")), trueDescription});
    } else {
        for (const auto& enumValue : *enumValues) {
            enumModel_->appendRow({editableItem(fromUtf8(enumValue.name), enumValue.id, "name",
                                                objectIdRole, propertyRole),
                                   editableItem(fromUtf8(enumValue.value.toHexString()),
                                                enumValue.id, "value", objectIdRole, propertyRole),
                                   editableItem(fromUtf8(enumValue.description), enumValue.id,
                                                "description", objectIdRole, propertyRole)});
        }
        QList<QStandardItem*> addRow;
        for (int column = 0; column <= enumDescriptionColumn; ++column) {
            addRow << addRowItem(column == enumNameColumn ? QStringLiteral("+") : QString{},
                                 addRowRole);
        }
        enumModel_->appendRow(addRow);
    }
    enumView_->resizeColumnsToContents();
    restoreTableSelection(
        enumView_, previousSelection, objectIdRole, propertyRole);
}
void MainWindow::refreshDiagnostics()
{
    problemsModel_->clear();
    problemsModel_->setHorizontalHeaderLabels(
        {QStringLiteral("Severity"), QStringLiteral("Code"), QStringLiteral("Message"),
         QStringLiteral("Object ID"), QStringLiteral("Source")});
    const auto& diagnostics = controller_.diagnostics();
    int errorCount = 0;
    int nonOutputErrorCount = 0;
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        const auto& diagnostic = diagnostics[index];
        auto* severity = item(severityText(diagnostic.severity));
        severity->setData(static_cast<int>(index), rowIndexRole);
        if (diagnostic.severity == regmap::DiagnosticSeverity::error) {
            severity->setForeground(QBrush(QColor(190, 35, 35)));
            ++errorCount;
            if (!diagnostic.code.starts_with("RM4")) {
                ++nonOutputErrorCount;
            }
        } else if (diagnostic.severity == regmap::DiagnosticSeverity::warning) {
            severity->setForeground(QBrush(QColor(180, 115, 0)));
        }
        problemsModel_->appendRow(
            {severity, item(fromUtf8(diagnostic.code)), item(fromUtf8(diagnostic.message)),
             item(fromUtf8(diagnostic.objectId)), item(sourceText(diagnostic.source))});
    }
    problemsView_->resizeColumnsToContents();
    tabs_->setTabText(0, QStringLiteral("Problems (%1)").arg(diagnostics.size()));
    generateAction_->setEnabled(controller_.workspace() != nullptr &&
                                !controller_.hasProjectErrors());
    if (errorCount > 0) {
        const bool onlyOutputErrors =
            nonOutputErrorCount == 0 && generatedModel_->rowCount() > 0;
        tabs_->setCurrentIndex(onlyOutputErrors ? 1 : 0);
    }
    updateBottomPanelVisibility();
    updateSyncPresentation();
}

void MainWindow::refreshGenerated()
{
    generatedModel_->clear();
    generatedModel_->setHorizontalHeaderLabels(
        {QStringLiteral("Target"), QStringLiteral("Path"), QStringLiteral("Status"),
         QStringLiteral("Updated")});
    const auto& artifacts = controller_.artifacts();
    bool retryNeeded = false;
    for (std::size_t index = 0; index < artifacts.size(); ++index) {
        const auto& artifact = artifacts[index];
        auto* kind = item(fromUtf8(regmap::toString(artifact.kind)));
        kind->setData(static_cast<int>(index), rowIndexRole);
        QString failure;
        for (const auto& diagnostic : controller_.diagnostics()) {
            if (diagnostic.severity == regmap::DiagnosticSeverity::error &&
                diagnostic.code == "RM4000" &&
                diagnostic.source.workbook.lexically_normal() == artifact.path.lexically_normal()) {
                failure = fromUtf8(diagnostic.message);
                break;
            }
        }
        const QFileInfo information(fromPath(artifact.path));
        QString status = QStringLiteral("Synchronized");
        if (!failure.isEmpty()) {
            status = QStringLiteral("Failed");
            retryNeeded = true;
        } else if (!information.exists()) {
            status = QStringLiteral("Missing");
            retryNeeded = true;
        }
        auto* statusItem = item(status);
        statusItem->setToolTip(failure);
        if (status != QStringLiteral("Synchronized")) {
            statusItem->setForeground(QColor(QStringLiteral("#B3261E")));
        }
        generatedModel_->appendRow(
            {kind, item(fromPath(artifact.path)), statusItem,
             item(information.exists()
                      ? information.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                      : QStringLiteral("—"))});
    }
    generatedView_->resizeColumnsToContents();
    tabs_->setTabText(1, QStringLiteral("Generated (%1)").arg(artifacts.size()));
    retryOutputsButton_->setVisible(retryNeeded);
    updateBottomPanelVisibility();
    updateSyncPresentation();
}

void MainWindow::refreshDiff()
{
    diffModel_->clear();
    diffModel_->setHorizontalHeaderLabels({QStringLiteral("Change"), QStringLiteral("Object Type"),
                                           QStringLiteral("Name"), QStringLiteral("Stable ID"),
                                           QStringLiteral("Summary"), QStringLiteral("Source")});
    const auto& changes = controller_.changes();
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const auto& change = changes[index];
        auto* kind = item(fromUtf8(regmap::toString(change.change)));
        kind->setData(static_cast<int>(index), rowIndexRole);
        const auto& source = change.afterSource.empty() ? change.beforeSource : change.afterSource;
        diffModel_->appendRow({kind, item(fromUtf8(regmap::toString(change.objectKind))),
                               item(fromUtf8(change.name)), item(fromUtf8(change.id)),
                               item(fromUtf8(change.summary)), item(sourceText(source))});
    }
    const auto displayValue = [](const std::optional<std::string>& value) {
        return value ? fromUtf8(*value) : QStringLiteral("<absent>");
    };
    for (const auto& conflict : controller_.conflicts()) {
        auto* kind = item(QStringLiteral("Conflict"));
        kind->setData(-1, rowIndexRole);
        const QString summary =
            QStringLiteral("%1: Workbench=%2; RTL=%3; Base=%4")
                .arg(fromUtf8(conflict.property), displayValue(conflict.workbenchValue),
                     displayValue(conflict.rtlValue), displayValue(conflict.baseValue));
        diffModel_->appendRow({kind, item(fromUtf8(regmap::toString(conflict.objectKind))),
                               item(fromUtf8(conflict.objectName)),
                               item(fromUtf8(conflict.objectId)), item(summary),
                               item(controller_.manifest() == nullptr
                                        ? QString{}
                                        : fromPath(controller_.manifest()->rtl.path.resolved))});
    }
    diffView_->resizeColumnsToContents();
    tabs_->setTabText(
        2, QStringLiteral("Diff (%1)").arg(changes.size() + controller_.conflicts().size()));
    const std::size_t conflictCount = controller_.conflicts().size();
    conflictBar_->setVisible(conflictCount > 0);
    if (controller_.requiresInitialSyncChoice()) {
        conflictSummaryLabel_->setText(
            QStringLiteral("No synchronization baseline exists and Workbench differs from RTL. "
                           "Choose one complete source. No file has been overwritten."));
    } else {
        conflictSummaryLabel_->setText(
            QStringLiteral("%1 RTL conflict(s) require a choice. No file is overwritten yet.")
                .arg(conflictCount));
    }
    if (conflictCount > 0) {
        tabs_->setCurrentIndex(2);
    }
    updateBottomPanelVisibility();
    updateSyncPresentation();
}

void MainWindow::updateBottomPanelVisibility()
{
    const bool showProblems = problemsModel_->rowCount() > 0;
    const bool showGenerated = generatedModel_->rowCount() > 0;
    const bool showDiff = diffModel_->rowCount() > 0;
    tabs_->setTabVisible(0, showProblems);
    tabs_->setTabVisible(1, showGenerated);
    tabs_->setTabVisible(2, showDiff);
    tabs_->setVisible(showProblems || showGenerated || showDiff);
}

void MainWindow::updateSyncPresentation(const QString& message)
{
    if (syncStateLabel_ == nullptr) {
        return;
    }
    if (!message.isEmpty()) {
        lastSyncMessage_ = message;
    }

    QString state = QStringLiteral("idle");
    QString text = QStringLiteral("No project");
    const auto* workspace = controller_.workspace();
    const bool outputFailure =
        std::ranges::any_of(controller_.diagnostics(), [](const regmap::Diagnostic& diagnostic) {
            return diagnostic.severity == regmap::DiagnosticSeverity::error &&
                diagnostic.code.starts_with("RM4");
        });
    const bool busy = lastSyncMessage_.startsWith(QStringLiteral("Loading")) ||
        lastSyncMessage_.startsWith(QStringLiteral("Synchronizing")) ||
        lastSyncMessage_.startsWith(QStringLiteral("Merging")) ||
        lastSyncMessage_.startsWith(QStringLiteral("Generating")) ||
        lastSyncMessage_.startsWith(QStringLiteral("Waiting"));

    if (workspace == nullptr) {
        // Keep the no-project state.
    } else if (controller_.hasConflicts()) {
        state = QStringLiteral("conflict");
        text = QStringLiteral("Conflict · %1").arg(controller_.conflicts().size());
    } else if (outputFailure) {
        state = QStringLiteral("partial");
        text = QStringLiteral("Saved · output failed");
    } else if (controller_.hasProjectErrors()) {
        state = QStringLiteral("blocked");
        if (controller_.isDirty()) {
            const std::size_t changeCount = controller_.changes().size();
            text = changeCount == 0
                ? QStringLiteral("Blocked · unsaved changes")
                : QStringLiteral("Blocked · %1 unsaved change(s)")
                      .arg(changeCount);
        } else {
            text = QStringLiteral("Blocked · see Problems");
        }
    } else if (controller_.isDirty()) {
        state = QStringLiteral("dirty");
        text = QStringLiteral("Unsaved · %1 change(s)").arg(controller_.changes().size());
    } else if (busy) {
        state = QStringLiteral("busy");
        text = QStringLiteral("Synchronizing…");
    } else {
        state = QStringLiteral("synced");
        text = QStringLiteral("Synchronized · %1 output(s)").arg(controller_.artifacts().size());
    }

    syncStateLabel_->setText(text);
    syncStateLabel_->setToolTip(lastSyncMessage_);
    if (syncStateLabel_->property("state").toString() != state) {
        syncStateLabel_->setProperty("state", state);
        syncStateLabel_->style()->unpolish(syncStateLabel_);
        syncStateLabel_->style()->polish(syncStateLabel_);
    }
}

void MainWindow::resolveConflictsWithConfirmation(bool useRtl)
{
    if (!commitActiveEditor() || !controller_.hasConflicts()) {
        return;
    }

    const bool initialChoice = controller_.requiresInitialSyncChoice();
    const std::size_t impactCount =
        initialChoice
            ? std::max<std::size_t>(controller_.changes().size(), 1)
            : controller_.conflicts().size();
    QMessageBox dialog(this);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(
        initialChoice
            ? QStringLiteral("Choose Initial Synchronization Source")
            : QStringLiteral("Resolve RTL Conflicts"));

    if (initialChoice) {
        dialog.setText(
            useRtl
                ? QStringLiteral(
                      "Use the complete managed RTL model as the initial "
                      "synchronization source?")
                : QStringLiteral(
                      "Use the complete Workbench model as the initial "
                      "synchronization source?"));
        dialog.setInformativeText(
            useRtl
                ? QStringLiteral(
                      "This resolves %1 detected model difference(s). The "
                      "current Workbench model and project file will be replaced "
                      "from managed RTL, then the synchronization baseline and "
                      "read-only outputs will be created.\n\nNo file is changed "
                      "if you cancel.")
                      .arg(impactCount)
                : QStringLiteral(
                      "This resolves %1 detected model difference(s). The "
                      "managed RTL region will be replaced from Workbench, then "
                      "the project file, synchronization baseline, and read-only "
                      "outputs will be updated.\n\nNo file is changed if you "
                      "cancel.")
                      .arg(impactCount));
    } else {
        dialog.setText(
            QStringLiteral("Resolve %1 RTL conflict(s) using %2 values?")
                .arg(impactCount)
                .arg(useRtl ? QStringLiteral("RTL")
                            : QStringLiteral("Workbench")));
        dialog.setInformativeText(
            useRtl
                ? QStringLiteral(
                      "The current Workbench value for every listed conflict "
                      "will be replaced by its RTL value. Non-conflicting "
                      "Workbench edits remain merged. The project, managed RTL, "
                      "baseline, and outputs will then be saved.\n\nNo file is "
                      "changed if you cancel.")
                : QStringLiteral(
                      "The managed RTL value for every listed conflict will be "
                      "replaced by its Workbench value. Non-conflicting RTL "
                      "edits remain merged. The project, managed RTL, baseline, "
                      "and outputs will then be saved.\n\nNo file is changed if "
                      "you cancel."));
    }

    auto* resolve = dialog.addButton(
        initialChoice
            ? (useRtl ? QStringLiteral("Use complete RTL")
                      : QStringLiteral("Use complete Workbench"))
            : (useRtl ? QStringLiteral("Use RTL values")
                      : QStringLiteral("Use Workbench values")),
        QMessageBox::DestructiveRole);
    resolve->setObjectName(QStringLiteral("confirmConflictResolutionButton"));
    auto* cancel = dialog.addButton(QMessageBox::Cancel);
    dialog.setDefaultButton(cancel);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    if (dialog.clickedButton() != resolve) {
        statusBar()->showMessage(
            initialChoice
                ? QStringLiteral(
                      "Initial synchronization choice cancelled · no file changed")
                : QStringLiteral(
                      "Conflict resolution cancelled · no file changed"),
            7000);
        return;
    }

    if (useRtl) {
        controller_.useRtlForConflicts();
    } else {
        controller_.useWorkbenchForConflicts();
    }
}

void MainWindow::applyFieldColumnVisibility()
{
    if (fieldView_ == nullptr) {
        return;
    }
    const bool advanced =
        showAdvancedFieldsAction_ != nullptr && showAdvancedFieldsAction_->isChecked();
    for (const int column :
         {fieldParentColumn, fieldHardwareAccessColumn, fieldReadEffectColumn,
          fieldWriteEffectColumn}) {
        fieldView_->setColumnHidden(column, !advanced);
    }
}

void MainWindow::setCurrentSource(const regmap::SourceLocation& source)
{
    if (source.empty()) {
        currentSource_.reset();
        openSourceAction_->setEnabled(false);
        return;
    }
    currentSource_ = source;
    openSourceAction_->setEnabled(true);
}

const regmap::Register* MainWindow::findRegister(const std::string& id) const
{
    const regmap::Workspace* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return nullptr;
    }
    for (const auto& addressSpace : workspace->addressSpaces) {
        for (const auto& block : addressSpace.blocks) {
            const auto iterator = std::ranges::find(block.registers, id, &regmap::Register::id);
            if (iterator != block.registers.end()) {
                return &*iterator;
            }
        }
    }
    return nullptr;
}

const regmap::AddressSpace* MainWindow::findAddressSpace(const std::string& id) const
{
    const regmap::Workspace* workspace = controller_.workspace();
    return workspace == nullptr ? nullptr : regmap::findAddressSpace(*workspace, id);
}

const regmap::RegisterBlock* MainWindow::findBlock(const std::string& id) const
{
    const regmap::Workspace* workspace = controller_.workspace();
    return workspace == nullptr ? nullptr : regmap::findRegisterBlock(*workspace, id);
}

const regmap::Field* MainWindow::findField(const regmap::Register& reg, const std::string& id) const
{
    return findFieldRecursive(reg.fields, id);
}

MainWindow::PropertyEditResult
MainWindow::applyPropertyEdit(const std::string& objectId, const std::string& property,
                              const QString& value, bool reportFeedback)
{
    const regmap::Workspace* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return {PropertyEditStatus::rejected, QStringLiteral("an open project")};
    }
    const std::string textValue = value.trimmed().toUtf8().toStdString();
    const auto parseUnsigned = [&]() -> std::optional<regmap::UnsignedValue> {
        return regmap::UnsignedValue::parse(textValue);
    };
    const auto parseUInt64 = [&]() -> std::optional<std::uint64_t> {
        const auto parsed = parseUnsigned();
        return parsed ? parsed->toUInt64() : std::nullopt;
    };
    const auto parseUInt32 = [&]() -> std::optional<std::uint32_t> {
        const auto parsed = parseUInt64();
        if (!parsed || *parsed > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*parsed);
    };
    const QString description = QStringLiteral("Edit %1").arg(fromUtf8(property));
    const auto commit =
        [this, &description](const regmap::WorkspaceStore::Mutation& mutation) {
            if (controller_.editWorkspace(description, mutation)) {
                return PropertyEditResult{PropertyEditStatus::changed, {}};
            }
            return PropertyEditResult{PropertyEditStatus::unchanged, {}};
        };
    const auto reject = [this, reportFeedback](const QString& expectation) {
        if (committingActiveEditor_) {
            activeEditorCommitRejected_ = true;
        }
        if (reportFeedback) {
            statusBar()->showMessage(
                QStringLiteral("Edit rejected: expected %1").arg(expectation), 5000);
            requestProjectRefresh();
        }
        return PropertyEditResult{PropertyEditStatus::rejected, expectation};
    };
    const auto cancelEdit =
        [this](const QString& expectation, const QString& message) {
            if (committingActiveEditor_) {
                activeEditorCommitRejected_ = true;
            }
            statusBar()->showMessage(message, 6000);
            requestProjectRefresh();
            return PropertyEditResult{
                PropertyEditStatus::rejected, expectation};
        };
    const auto acceptsNumericRange =
        [workspace](std::string_view targetId,
                    const std::optional<std::string>& minimum,
                    const std::optional<std::string>& maximum) {
            regmap::Workspace candidate = *workspace;
            if (auto* field = regmap::findField(candidate, targetId)) {
                field->minimumValue = minimum;
                field->maximumValue = maximum;
            } else if (auto* reg = regmap::findRegister(candidate, targetId)) {
                reg->minimumValue = minimum;
                reg->maximumValue = maximum;
            } else {
                return false;
            }
            const auto diagnostics = regmap::validateWorkspace(candidate);
            return std::ranges::none_of(
                diagnostics, [targetId](const regmap::Diagnostic& diagnostic) {
                    return diagnostic.code == numericRangeDiagnosticCode &&
                           diagnostic.objectId == targetId;
                });
        };
    const auto acceptsAddressEdit =
        [workspace](std::string_view targetId,
                    const regmap::WorkspaceStore::Mutation& mutation) {
            regmap::Workspace candidate = *workspace;
            mutation(candidate);
            return addressEditDoesNotWorsen(*workspace, candidate, targetId);
        };
    const auto acceptsFieldGeometryEdit =
        [workspace](std::string_view fieldId,
                    const regmap::WorkspaceStore::Mutation& mutation) {
            regmap::Workspace candidate = *workspace;
            mutation(candidate);
            return fieldGeometryEditDoesNotWorsen(
                *workspace, candidate, fieldId);
        };
    const auto acceptsNumericRangeEdit =
        [workspace](std::string_view targetId,
                    const regmap::WorkspaceStore::Mutation& mutation) {
            regmap::Workspace candidate = *workspace;
            mutation(candidate);
            return numericRangeEditDoesNotWorsen(
                *workspace, candidate, targetId);
        };

    if (workspace->id == objectId && property == "name") {
        if (textValue.empty()) {
            return reject(QStringLiteral("a non-empty Workspace name"));
        }
        return commit(
            [=](regmap::Workspace& candidate) { candidate.name = textValue; });
    }

    if (const auto* currentEnum = regmap::findEnumValue(*workspace, objectId)) {
        if (property == "description") {
            return commit([=](regmap::Workspace& candidate) {
                if (auto* enumValue = regmap::findEnumValue(candidate, objectId)) {
                    enumValue->description = textValue;
                }
            });
        }
        const auto ownerContext = enumOwnerContext(*workspace, objectId);
        if (property == "name") {
            if (textValue.empty()) {
                return reject(QStringLiteral("a non-empty unique Enum name"));
            }
            if (!ownerContext) {
                return reject(QStringLiteral("a unique Enum name with a valid owner"));
            }
            const bool duplicate = std::ranges::any_of(
                *ownerContext->values, [&](const regmap::EnumValue& value) {
                    return value.id != objectId && value.name == textValue;
                });
            if (duplicate) {
                return reject(QStringLiteral("a unique Enum name within its owner"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* enumValue = regmap::findEnumValue(candidate, objectId)) {
                    enumValue->name = textValue;
                }
            });
        }
        if (property == "value") {
            const auto parsed = parseUnsigned();
            if (!parsed || !ownerContext || !parsed->fitsInBits(ownerContext->width)) {
                return reject(
                    ownerContext
                        ? QStringLiteral(
                              "an unsigned integer fitting the owning %1-bit value")
                              .arg(ownerContext->width)
                        : QStringLiteral("an unsigned integer with a valid Enum owner"));
            }
            const bool duplicate = std::ranges::any_of(
                *ownerContext->values, [&](const regmap::EnumValue& value) {
                    return value.id != objectId && value.value == *parsed;
                });
            if (duplicate) {
                return reject(QStringLiteral("a unique Enum value within its owner"));
            }
            const regmap::UnsignedValue previousValue = currentEnum->value;
            const bool updatesReference =
                enumValueIsReferenced(*workspace, *ownerContext, previousValue);
            const PropertyEditResult result =
                commit([=](regmap::Workspace& candidate) {
                    if (auto* enumValue =
                            regmap::findEnumValue(candidate, objectId)) {
                        enumValue->value = *parsed;
                    }
                    if (!updatesReference) {
                        return;
                    }
                    if (ownerContext->registerOwner) {
                        if (auto* reg =
                                regmap::findRegister(candidate, ownerContext->ownerId)) {
                            bool resetChanged = false;
                            if (reg->initialValue && *reg->initialValue == previousValue) {
                                reg->initialValue = *parsed;
                            }
                            if (reg->resetValue && *reg->resetValue == previousValue) {
                                reg->resetValue = *parsed;
                                resetChanged = true;
                            }
                            if (resetChanged) {
                                refreshRegisterFieldResets(*reg);
                            }
                        }
                    } else {
                        assignFieldReset(candidate, ownerContext->ownerId, *parsed);
                    }
                });
            if (result.status == PropertyEditStatus::changed &&
                updatesReference && reportFeedback) {
                statusBar()->showMessage(
                    ownerContext->registerOwner
                        ? QStringLiteral(
                              "Enum value changed · matching Initial/Reset updated · "
                              "Ctrl+Z to restore")
                        : QStringLiteral(
                              "Enum value changed · matching Field/Register Reset updated · "
                              "Ctrl+Z to restore"),
                    6000);
            }
            return result;
        }
    }

    if (regmap::findField(*workspace, objectId) != nullptr) {
        if (property == "name") {
            const auto* siblings = fieldSiblings(*workspace, objectId);
            if (siblings == nullptr ||
                !isUniqueSiblingName(*siblings, objectId, textValue)) {
                return reject(
                    QStringLiteral("a non-empty unique Field name within its parent"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->name = textValue;
                }
            });
        }
        if (property == "description") {
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->description = textValue;
                }
            });
        }
        if (property == "minimum" || property == "maximum") {
            const std::optional<std::string> range =
                textValue.empty() ? std::nullopt : std::optional{textValue};
            const auto* current = regmap::findField(*workspace, objectId);
            if (current == nullptr) {
                return reject(QStringLiteral("a numeric range with a valid Field owner"));
            }
            std::optional<std::string> minimum = current->minimumValue;
            std::optional<std::string> maximum = current->maximumValue;
            (property == "minimum" ? minimum : maximum) = range;
            const bool editedBoundValid =
                property == "minimum"
                    ? acceptsNumericRange(objectId, minimum, std::nullopt)
                    : acceptsNumericRange(objectId, std::nullopt, maximum);
            const bool otherBoundValid =
                property == "minimum"
                    ? acceptsNumericRange(objectId, std::nullopt, maximum)
                    : acceptsNumericRange(objectId, minimum, std::nullopt);
            if (!editedBoundValid ||
                (otherBoundValid &&
                 !acceptsNumericRange(objectId, minimum, maximum))) {
                return reject(
                    QStringLiteral(
                        "a numeric range whose bounds fit the Field type and width "
                        "and whose minimum does not exceed its maximum"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->minimumValue = minimum;
                    field->maximumValue = maximum;
                }
            });
        }
        if (property == "msb") {
            const auto parsed = parseUInt32();
            const auto* current = regmap::findField(*workspace, objectId);
            const auto* owner = findRegisterContainingField(*workspace, objectId);
            if (!parsed || current == nullptr || owner == nullptr || *parsed < current->lsb ||
                *parsed >= owner->width ||
                !fieldValuesFitWidth(
                    *current, *owner,
                    static_cast<std::size_t>(
                        static_cast<std::uint64_t>(*parsed) - current->lsb + 1),
                    true)) {
                return reject(
                    QStringLiteral(
                        "an MSB between the field LSB and register width that preserves "
                        "Reset, Enum, and member Field values"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->msb = *parsed;
                    if (auto* reg = findRegisterContainingField(candidate, objectId)) {
                        refreshRegisterFieldResets(*reg);
                    }
                }
            };
            if (!acceptsFieldGeometryEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "an MSB that preserves a valid, non-overlapping Field layout"));
            }
            if (!acceptsNumericRangeEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "an MSB whose resulting width preserves the existing "
                        "numeric Range"));
            }
            return commit(mutation);
        }
        if (property == "field_width") {
            const auto parsed = parseUInt32();
            const auto* current = regmap::findField(*workspace, objectId);
            const auto* owner = findRegisterContainingField(*workspace, objectId);
            if (!parsed || *parsed == 0 || *parsed > maximumEditableWidth || current == nullptr ||
                owner == nullptr ||
                static_cast<std::uint64_t>(current->lsb) + *parsed - 1 >= owner->width ||
                !fieldValuesFitWidth(*current, *owner, static_cast<std::size_t>(*parsed), true)) {
                return reject(
                    QStringLiteral(
                        "a positive field width that fits in the register and preserves "
                        "Reset, Enum, and member Field values"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->msb = field->lsb + *parsed - 1;
                    if (auto* reg = findRegisterContainingField(candidate, objectId)) {
                        refreshRegisterFieldResets(*reg);
                    }
                }
            };
            if (!acceptsFieldGeometryEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Width that preserves a valid, non-overlapping Field layout"));
            }
            if (!acceptsNumericRangeEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Width that preserves the existing numeric Range"));
            }
            return commit(mutation);
        }
        if (property == "reset") {
            std::optional<regmap::UnsignedValue> parsed;
            const auto* current = regmap::findField(*workspace, objectId);
            const auto* owner = findRegisterContainingField(*workspace, objectId);
            if (!textValue.empty()) {
                parsed = parseUnsigned();
                if (!parsed || current == nullptr ||
                    !parsed->fitsInBits(static_cast<std::size_t>(current->width()))) {
                    return reject(
                        current == nullptr
                            ? QStringLiteral("an unsigned integer with a valid Field owner")
                            : QStringLiteral(
                                  "an unsigned integer fitting the %1-bit field, or empty")
                                  .arg(current->width()));
                }
            }
            const bool enumerationLike =
                current != nullptr &&
                (current->type == regmap::FieldType::enumeration ||
                 current->type == regmap::FieldType::boolean);
            if (parsed && enumerationLike && !current->enumValues.empty() &&
                std::ranges::none_of(
                    current->enumValues, [&](const regmap::EnumValue& enumValue) {
                        return enumValue.value == *parsed;
                    })) {
                return reject(
                    QStringLiteral("an existing Enum value for this field, or empty"));
            }
            if (parsed && owner != nullptr && owner->resetValue) {
                const auto merged =
                    mergedRegisterReset(*owner, objectId, *parsed);
                if (!merged ||
                    !resetMatchesFieldEnums(
                        owner->fields, *merged,
                        static_cast<std::uint64_t>(owner->width))) {
                    return reject(
                        QStringLiteral(
                            "a Reset value whose Enum/Bool Field slices match "
                            "existing Enum values, or empty"));
                }
            }
            const bool updatesRegisterReset =
                parsed.has_value() && current != nullptr && owner != nullptr &&
                owner->resetValue.has_value();
            const PropertyEditResult result =
                commit([=](regmap::Workspace& candidate) {
                    assignFieldReset(candidate, objectId, parsed);
                });
            if (result.status == PropertyEditStatus::changed &&
                updatesRegisterReset && reportFeedback) {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Field Reset updated the matching Register Reset bits · "
                        "Ctrl+Z to restore"),
                    6000);
            }
            return result;
        }
        if (property == "type") {
            const QString normalized = value.trimmed().toLower();
            const QRegularExpression numericPattern(QStringLiteral(R"(^(u?int)([1-9][0-9]*)$)"));
            const QRegularExpressionMatch numericMatch = numericPattern.match(normalized);
            std::optional<regmap::FieldType> parsed;
            std::optional<std::uint32_t> numericWidth;
            if (numericMatch.hasMatch()) {
                bool ok = false;
                const qulonglong width = numericMatch.captured(2).toULongLong(&ok);
                if (ok && width <= std::numeric_limits<std::uint32_t>::max()) {
                    numericWidth = static_cast<std::uint32_t>(width);
                    parsed = numericMatch.captured(1) == QStringLiteral("uint")
                                 ? regmap::FieldType::unsignedInteger
                                 : regmap::FieldType::signedInteger;
                }
            } else {
                parsed = regmap::parseFieldType(textValue);
            }
            if (!parsed) {
                return reject(
                    QStringLiteral("bits, bool, intN, uintN, enum, field, or reserved"));
            }
            const auto* currentField = regmap::findField(*workspace, objectId);
            const auto* owner = findRegisterContainingField(*workspace, objectId);
            const bool removesMembers =
                currentField != nullptr &&
                *parsed != regmap::FieldType::structure &&
                !currentField->members.empty();
            const std::size_t removedMemberCount =
                removesMembers ? fieldTreeSize(currentField->members) : 0;
            if (numericWidth &&
                (currentField == nullptr || owner == nullptr ||
                 *numericWidth > maximumEditableWidth ||
                 static_cast<std::uint64_t>(currentField->lsb) + *numericWidth - 1 >=
                     owner->width)) {
                return reject(
                    QStringLiteral("a numeric type width that fits the field position"));
            }
            const bool enumerationLike =
                *parsed == regmap::FieldType::enumeration ||
                *parsed == regmap::FieldType::boolean;
            const std::optional<std::uint32_t> targetWidth =
                numericWidth
                    ? numericWidth
                    : (*parsed == regmap::FieldType::boolean
                           ? std::optional<std::uint32_t>{1}
                           : std::nullopt);
            if (targetWidth && currentField != nullptr && owner != nullptr) {
                regmap::Field valueShape = *currentField;
                if (removesMembers) {
                    valueShape.members.clear();
                }
                if (!fieldValuesFitWidth(
                        valueShape, *owner, *targetWidth, enumerationLike)) {
                    return reject(QStringLiteral(
                        "a type width that preserves existing Reset and Enum values"));
                }
            }
            const bool numeric = *parsed == regmap::FieldType::signedInteger ||
                                 *parsed == regmap::FieldType::unsignedInteger;
            const bool clearsEnumValues =
                currentField != nullptr && !enumerationLike && !currentField->enumValues.empty();
            const std::size_t removedEnumValueCount =
                clearsEnumValues ? currentField->enumValues.size() : 0;
            const bool clearsRange = currentField != nullptr && !numeric &&
                                     (currentField->minimumValue || currentField->maximumValue);
            const std::size_t removedRangeBoundCount =
                clearsRange ? static_cast<std::size_t>(
                                  currentField->minimumValue.has_value()) +
                                  currentField->maximumValue.has_value()
                            : 0;
            const std::vector<regmap::EnumValue> defaultEnumValues =
                *parsed == regmap::FieldType::enumeration &&
                        currentField != nullptr &&
                        currentField->enumValues.empty()
                    ? makeDefaultEnumValues(
                          *workspace,
                          {effectiveFieldReset(*currentField, owner)})
                    : std::vector<regmap::EnumValue>{};
            std::optional<regmap::Field> defaultMember;
            if (*parsed == regmap::FieldType::structure && currentField != nullptr &&
                owner != nullptr && currentField->members.empty()) {
                regmap::Field member;
                member.id = regmap::makeStableObjectId(*workspace, "field");
                member.name = uniqueDefaultName(currentField->members, "NEW_MEMBER");
                member.msb = 0;
                member.lsb = 0;
                member.type = regmap::FieldType::bits;
                member.softwareAccess = currentField->softwareAccess;
                member.hardwareAccess = currentField->hardwareAccess;
                if (member.softwareAccess == regmap::AccessMode::none &&
                    member.hardwareAccess == regmap::AccessMode::none) {
                    member.softwareAccess = owner->access;
                }
                member.readSideEffect = currentField->readSideEffect;
                member.writeSideEffect = currentField->writeSideEffect;
                if (currentField->resetValue) {
                    member.resetValue = currentField->resetValue->slice(0, 1);
                } else if (owner->resetValue) {
                    const auto absoluteLsb =
                        fieldAbsoluteLsb(owner->fields, objectId);
                    if (absoluteLsb) {
                        member.resetValue = owner->resetValue->slice(*absoluteLsb, 1);
                    }
                }
                defaultMember = std::move(member);
            }
            const regmap::AccessMode scalarAccess =
                owner == nullptr ? regmap::AccessMode::none
                                 : owner->access;
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->type = *parsed;
                    if (numericWidth) {
                        field->msb = field->lsb + *numericWidth - 1;
                    } else if (*parsed == regmap::FieldType::boolean) {
                        field->msb = field->lsb;
                    } else if (*parsed == regmap::FieldType::structure ||
                               *parsed == regmap::FieldType::reserved) {
                        field->softwareAccess = regmap::AccessMode::none;
                        field->hardwareAccess = regmap::AccessMode::none;
                        field->readSideEffect = regmap::ReadSideEffect::none;
                        field->writeSideEffect = regmap::WriteSideEffect::none;
                    }
                    if (!enumerationLike) {
                        field->enumValues.clear();
                    }
                    if (!defaultEnumValues.empty() &&
                        field->enumValues.empty()) {
                        field->enumValues = defaultEnumValues;
                    }
                    if (!numeric) {
                        field->minimumValue.reset();
                        field->maximumValue.reset();
                    }
                    if (defaultMember && field->members.empty()) {
                        field->members.push_back(*defaultMember);
                    }
                    if (removesMembers) {
                        field->members.clear();
                        if (*parsed != regmap::FieldType::reserved) {
                            field->softwareAccess = scalarAccess;
                            field->hardwareAccess = regmap::AccessMode::none;
                            field->readSideEffect = regmap::ReadSideEffect::none;
                            field->writeSideEffect =
                                accessCanWrite(scalarAccess)
                                    ? regmap::WriteSideEffect::write
                                    : regmap::WriteSideEffect::none;
                        }
                    }
                    if (auto* reg = findRegisterContainingField(candidate, objectId)) {
                        refreshRegisterFieldResets(*reg);
                    }
                }
            };
            if (!acceptsFieldGeometryEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Type whose width preserves a valid, non-overlapping Field layout"));
            }
            if (!acceptsNumericRangeEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a numeric Type whose width and signedness preserve the "
                        "existing Range"));
            }
            if (removesMembers) {
                const QString memberLabel =
                    removedMemberCount == 1
                        ? QStringLiteral("1 Member Field")
                        : QStringLiteral("%1 Member Fields").arg(removedMemberCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(memberLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Compound Field Type"),
                    QStringLiteral(
                        "Change Field %1 to %2?\n\n"
                        "This removes %3 and all nested data.\n\n"
                        "Ctrl+Z can restore the complete compound Field.")
                        .arg(fromUtf8(currentField->name), value.trimmed(), memberLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral(
                            "Type change cancelled · %1 kept")
                            .arg(memberLabel));
                }
            }
            if (clearsEnumValues) {
                const QString enumLabel =
                    removedEnumValueCount == 1
                        ? QStringLiteral("1 Enum value")
                        : QStringLiteral("%1 Enum values")
                              .arg(removedEnumValueCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(enumLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Enum Field Type"),
                    QStringLiteral(
                        "Change Field %1 to %2?\n\n"
                        "This removes %3 and their descriptions.\n\n"
                        "Ctrl+Z can restore the complete Enum definition.")
                        .arg(fromUtf8(currentField->name),
                             value.trimmed(), enumLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral("Type change cancelled · %1 kept")
                            .arg(enumLabel));
                }
            }
            if (clearsRange) {
                const QString rangeLabel =
                    removedRangeBoundCount == 1
                        ? QStringLiteral("1 Range bound")
                        : QStringLiteral("%1 Range bounds")
                              .arg(removedRangeBoundCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(rangeLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Numeric Field Type"),
                    QStringLiteral(
                        "Change Field %1 to %2?\n\n"
                        "This removes %3.\n\n"
                        "Ctrl+Z can restore the complete numeric Range.")
                        .arg(fromUtf8(currentField->name),
                             value.trimmed(), rangeLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral("Type change cancelled · %1 kept")
                            .arg(rangeLabel));
                }
            }
            const PropertyEditResult result = commit(mutation);
            if (result.status == PropertyEditStatus::changed && reportFeedback) {
                QStringList changes;
                if (removesMembers) {
                    changes << (
                        removedMemberCount == 1
                            ? QStringLiteral("1 Member Field removed")
                            : QStringLiteral("%1 Member Fields removed")
                                  .arg(removedMemberCount));
                }
                if (defaultMember) {
                    changes << QStringLiteral("NEW_MEMBER created");
                }
                if (!defaultEnumValues.empty()) {
                    changes << enumValuesCreatedText(defaultEnumValues.size());
                }
                if (clearsEnumValues) {
                    changes << (
                        removedEnumValueCount == 1
                            ? QStringLiteral("1 Enum value removed")
                            : QStringLiteral("%1 Enum values removed")
                                  .arg(removedEnumValueCount));
                }
                if (clearsRange) {
                    changes << (
                        removedRangeBoundCount == 1
                            ? QStringLiteral("1 Range bound removed")
                            : QStringLiteral("%1 Range bounds removed")
                                  .arg(removedRangeBoundCount));
                }
                if (!changes.empty()) {
                    statusBar()->showMessage(
                        QStringLiteral("Type changed · %1 · Ctrl+Z to restore")
                            .arg(changes.join(QStringLiteral(" · "))),
                        6000);
                }
            }
            return result;
        }
        if (property == "sw_access") {
            const auto parsed = regmap::parseAccessMode(textValue);
            if (!parsed) {
                return reject(QStringLiteral("none, ro, wo, or rw"));
            }
            const auto* current = regmap::findField(*workspace, objectId);
            const auto* owner = findRegisterContainingField(*workspace, objectId);
            if (current == nullptr || owner == nullptr) {
                return reject(
                    QStringLiteral("software access with a valid containing Register"));
            }
            if (current->type == regmap::FieldType::reserved &&
                *parsed != regmap::AccessMode::none) {
                return reject(QStringLiteral("NONE software access for a reserved Field"));
            }
            if (!accessFitsWithin(*parsed, owner->access)) {
                return reject(
                    QStringLiteral(
                        "Field software access permitted by its containing Register"));
            }
            const bool clearsReadEffect =
                current->readSideEffect != regmap::ReadSideEffect::none &&
                !accessCanRead(*parsed);
            const bool clearsWriteEffect =
                current->writeSideEffect != regmap::WriteSideEffect::none &&
                !accessCanWrite(*parsed);
            const PropertyEditResult result = commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->softwareAccess = *parsed;
                    if (clearsReadEffect) {
                        field->readSideEffect = regmap::ReadSideEffect::none;
                    }
                    if (clearsWriteEffect) {
                        field->writeSideEffect = regmap::WriteSideEffect::none;
                    }
                }
            });
            if (result.status == PropertyEditStatus::changed &&
                (clearsReadEffect || clearsWriteEffect) && reportFeedback) {
                const QString cleared =
                    clearsReadEffect && clearsWriteEffect
                        ? QStringLiteral("Read and Write Effects")
                        : (clearsReadEffect ? QStringLiteral("Read Effect")
                                            : QStringLiteral("Write Effect"));
                statusBar()->showMessage(
                    QStringLiteral(
                        "Software Access changed; incompatible %1 cleared; Ctrl+Z to restore")
                        .arg(cleared),
                    6000);
            }
            return result;
        }
        if (property == "hw_access") {
            const auto parsed = regmap::parseAccessMode(textValue);
            const auto* current = regmap::findField(*workspace, objectId);
            if (!parsed) {
                return reject(QStringLiteral("none, ro, wo, or rw"));
            }
            if (current == nullptr ||
                (current->type == regmap::FieldType::reserved &&
                 *parsed != regmap::AccessMode::none)) {
                return reject(QStringLiteral("NONE hardware access for a reserved Field"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->hardwareAccess = *parsed;
                }
            });
        }
        if (property == "read_side_effect") {
            const auto parsed = regmap::parseReadSideEffect(textValue);
            if (!parsed) {
                return reject(QStringLiteral("none, clear, or set"));
            }
            const auto* current = regmap::findField(*workspace, objectId);
            if (current == nullptr ||
                (*parsed != regmap::ReadSideEffect::none &&
                 !accessCanRead(current->softwareAccess))) {
                return reject(QStringLiteral("a Read Effect with software-readable access"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->readSideEffect = *parsed;
                }
            });
        }
        if (property == "write_side_effect") {
            const auto parsed = regmap::parseWriteSideEffect(textValue);
            if (!parsed) {
                return reject(
                    QStringLiteral("none, write, w1c, w1s, w0c, w0s, or toggle"));
            }
            const auto* current = regmap::findField(*workspace, objectId);
            if (current == nullptr ||
                (*parsed != regmap::WriteSideEffect::none &&
                 !accessCanWrite(current->softwareAccess))) {
                return reject(QStringLiteral("a Write Effect with software-writable access"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* field = regmap::findField(candidate, objectId)) {
                    field->writeSideEffect = *parsed;
                }
            });
        }
    }

    if (regmap::findRegister(*workspace, objectId) != nullptr) {
        if (property == "name") {
            const auto* siblings = registerSiblings(*workspace, objectId);
            if (siblings == nullptr ||
                !isUniqueSiblingName(*siblings, objectId, textValue)) {
                return reject(
                    QStringLiteral("a non-empty unique Register name within its Block"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    reg->name = textValue;
                }
            });
        }
        if (property == "description") {
            return commit([=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    reg->description = textValue;
                }
            });
        }
        if (property == "tags") {
            std::vector<std::string> tags;
            const QStringList candidates = value.split(',', Qt::SkipEmptyParts);
            for (const QString& candidate : candidates) {
                const QString normalized = candidate.trimmed();
                if (normalized.isEmpty() ||
                    std::ranges::any_of(tags, [&normalized](const std::string& existing) {
                        return fromUtf8(existing).compare(normalized, Qt::CaseInsensitive) == 0;
                    })) {
                    continue;
                }
                tags.push_back(normalized.toUtf8().toStdString());
            }
            std::ranges::sort(tags);
            return commit(
                [objectId, tags = std::move(tags)](regmap::Workspace& candidate) {
                    if (auto* reg = regmap::findRegister(candidate, objectId)) {
                        reg->tags = tags;
                    }
                });
        }
        if (property == "range") {
            std::optional<std::string> minimum;
            std::optional<std::string> maximum;
            if (!value.trimmed().isEmpty()) {
                const QRegularExpression rangePattern(
                    QStringLiteral(R"(^\s*(.*?)\s*\.\.\s*(.*?)\s*$)"));
                const QRegularExpressionMatch match = rangePattern.match(value);
                if (!match.hasMatch()) {
                    return reject(
                        QStringLiteral("a range in the form minimum .. maximum"));
                }
                const QString minimumText = match.captured(1).trimmed();
                const QString maximumText = match.captured(2).trimmed();
                if (minimumText.isEmpty() && maximumText.isEmpty()) {
                    return reject(QStringLiteral("at least one range bound"));
                }
                if (!minimumText.isEmpty()) {
                    minimum = minimumText.toUtf8().toStdString();
                }
                if (!maximumText.isEmpty()) {
                    maximum = maximumText.toUtf8().toStdString();
                }
            }
            if (!acceptsNumericRange(objectId, minimum, maximum)) {
                return reject(
                    QStringLiteral(
                        "a numeric range whose bounds fit the Register type and width "
                        "and whose minimum does not exceed its maximum"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    reg->minimumValue = minimum;
                    reg->maximumValue = maximum;
                }
            });
        }
        if (property == "type") {
            const QString normalized = value.trimmed().toLower();
            const QRegularExpression numericPattern(QStringLiteral(R"(^(u?int)([1-9][0-9]*)$)"));
            const QRegularExpressionMatch numericMatch = numericPattern.match(normalized);
            std::optional<regmap::FieldType> parsed;
            std::optional<std::uint32_t> numericWidth;
            if (numericMatch.hasMatch()) {
                bool ok = false;
                const qulonglong width = numericMatch.captured(2).toULongLong(&ok);
                if (ok && width <= maximumEditableWidth) {
                    numericWidth = static_cast<std::uint32_t>(width);
                    parsed = numericMatch.captured(1) == QStringLiteral("uint")
                                 ? regmap::FieldType::unsignedInteger
                                 : regmap::FieldType::signedInteger;
                }
            } else {
                parsed = regmap::parseFieldType(textValue);
            }
            const auto* current = regmap::findRegister(*workspace, objectId);
            if (!parsed || current == nullptr) {
                return reject(
                    QStringLiteral("bits, bool, intN, uintN, enum, field, or reserved"));
            }
            const bool removesFields =
                *parsed != regmap::FieldType::structure &&
                !current->fields.empty();
            const std::size_t removedFieldCount =
                removesFields ? fieldTreeSize(current->fields) : 0;
            const bool enumerationLike =
                *parsed == regmap::FieldType::enumeration ||
                *parsed == regmap::FieldType::boolean;
            const std::optional<std::uint32_t> targetWidth =
                numericWidth
                    ? numericWidth
                    : (*parsed == regmap::FieldType::boolean
                           ? std::optional<std::uint32_t>{1}
                           : std::nullopt);
            if (targetWidth &&
                !registerValuesFitWidth(*current, *targetWidth, enumerationLike)) {
                return reject(
                    enumerationLike
                        ? QStringLiteral(
                              "a type width that fits existing Initial, Reset, and Enum values")
                        : QStringLiteral(
                              "a type width that fits existing Initial and Reset values"));
            }
            const bool numeric = *parsed == regmap::FieldType::signedInteger ||
                                 *parsed == regmap::FieldType::unsignedInteger;
            const bool clearsEnumValues = !enumerationLike && !current->enumValues.empty();
            const std::size_t removedEnumValueCount =
                clearsEnumValues ? current->enumValues.size() : 0;
            const bool clearsRange =
                !numeric && (current->minimumValue || current->maximumValue);
            const std::size_t removedRangeBoundCount =
                clearsRange ? static_cast<std::size_t>(
                                  current->minimumValue.has_value()) +
                                  current->maximumValue.has_value()
                            : 0;
            const std::vector<regmap::EnumValue> defaultEnumValues =
                *parsed == regmap::FieldType::enumeration &&
                        current->enumValues.empty()
                    ? makeDefaultEnumValues(
                          *workspace,
                          {current->initialValue, current->resetValue})
                    : std::vector<regmap::EnumValue>{};
            const bool normalizesReserved =
                *parsed == regmap::FieldType::reserved &&
                (current->access != regmap::AccessMode::none ||
                 (current->initialValue && !current->initialValue->isZero()) ||
                 (current->resetValue && !current->resetValue->isZero()));
            std::optional<regmap::Field> defaultField;
            if (*parsed == regmap::FieldType::structure &&
                current->fields.empty()) {
                defaultField = makeDefaultRegisterField(*workspace, *current, 0);
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    reg->type = *parsed;
                    reg->reserved = *parsed == regmap::FieldType::reserved;
                    if (numericWidth) {
                        reg->width = *numericWidth;
                    } else if (*parsed == regmap::FieldType::boolean) {
                        reg->width = 1;
                    } else if (*parsed == regmap::FieldType::reserved) {
                        reg->access = regmap::AccessMode::none;
                        reg->minimumValue.reset();
                        reg->maximumValue.reset();
                        reg->enumValues.clear();
                        reg->initialValue = regmap::UnsignedValue(0);
                        reg->resetValue = regmap::UnsignedValue(0);
                    }
                    if (!enumerationLike) {
                        reg->enumValues.clear();
                    }
                    if (!defaultEnumValues.empty() &&
                        reg->enumValues.empty()) {
                        reg->enumValues = defaultEnumValues;
                    }
                    if (!numeric) {
                        reg->minimumValue.reset();
                        reg->maximumValue.reset();
                    }
                    if (defaultField && reg->fields.empty()) {
                        reg->fields.push_back(*defaultField);
                    }
                    if (removesFields) {
                        reg->fields.clear();
                    }
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Register Type whose width preserves the address layout"));
            }
            if (!acceptsNumericRangeEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a numeric Type whose width and signedness preserve the "
                        "existing Range"));
            }
            if (removesFields) {
                const QString fieldLabel =
                    removedFieldCount == 1
                        ? QStringLiteral("1 Field")
                        : QStringLiteral("%1 Fields").arg(removedFieldCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(fieldLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Field Register Type"),
                    QStringLiteral(
                        "Change Register %1 to %2?\n\n"
                        "This removes %3, including nested Members and Enum values.\n\n"
                        "Ctrl+Z can restore the complete field Register.")
                        .arg(fromUtf8(current->name), value.trimmed(), fieldLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral("Type change cancelled · %1 kept")
                            .arg(fieldLabel));
                }
            }
            if (clearsEnumValues) {
                const QString enumLabel =
                    removedEnumValueCount == 1
                        ? QStringLiteral("1 Enum value")
                        : QStringLiteral("%1 Enum values")
                              .arg(removedEnumValueCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(enumLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Enum Register Type"),
                    QStringLiteral(
                        "Change Register %1 to %2?\n\n"
                        "This removes %3 and their descriptions.\n\n"
                        "Ctrl+Z can restore the complete Enum definition.")
                        .arg(fromUtf8(current->name),
                             value.trimmed(), enumLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral("Type change cancelled · %1 kept")
                            .arg(enumLabel));
                }
            }
            if (clearsRange) {
                const QString rangeLabel =
                    removedRangeBoundCount == 1
                        ? QStringLiteral("1 Range bound")
                        : QStringLiteral("%1 Range bounds")
                              .arg(removedRangeBoundCount);
                const QString expectation =
                    QStringLiteral(
                        "an individually confirmed Type change before removing %1")
                        .arg(rangeLabel);
                if (!reportFeedback) {
                    return reject(expectation);
                }
                const auto answer = QMessageBox::warning(
                    this, QStringLiteral("Change Numeric Register Type"),
                    QStringLiteral(
                        "Change Register %1 to %2?\n\n"
                        "This removes %3.\n\n"
                        "Ctrl+Z can restore the complete numeric Range.")
                        .arg(fromUtf8(current->name),
                             value.trimmed(), rangeLabel),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (answer != QMessageBox::Yes) {
                    return cancelEdit(
                        expectation,
                        QStringLiteral("Type change cancelled · %1 kept")
                            .arg(rangeLabel));
                }
            }
            const PropertyEditResult result = commit(mutation);
            if (result.status == PropertyEditStatus::changed && reportFeedback) {
                QStringList changes;
                if (removesFields) {
                    changes << (
                        removedFieldCount == 1
                            ? QStringLiteral("1 Field removed")
                            : QStringLiteral("%1 Fields removed")
                                  .arg(removedFieldCount));
                }
                if (defaultField) {
                    changes << QStringLiteral("NEW_FIELD created");
                }
                if (!defaultEnumValues.empty()) {
                    changes << enumValuesCreatedText(defaultEnumValues.size());
                }
                if (clearsEnumValues) {
                    changes << (
                        removedEnumValueCount == 1
                            ? QStringLiteral("1 Enum value removed")
                            : QStringLiteral("%1 Enum values removed")
                                  .arg(removedEnumValueCount));
                }
                if (clearsRange) {
                    changes << (
                        removedRangeBoundCount == 1
                            ? QStringLiteral("1 Range bound removed")
                            : QStringLiteral("%1 Range bounds removed")
                                  .arg(removedRangeBoundCount));
                }
                if (normalizesReserved) {
                    changes << QStringLiteral(
                        "incompatible data cleared or normalized");
                }
                if (!changes.empty()) {
                    statusBar()->showMessage(
                        QStringLiteral("Type changed · %1 · Ctrl+Z to restore")
                            .arg(changes.join(QStringLiteral(" · "))),
                        6000);
                }
            }
            return result;
        }
        if (property == "offset" || property == "stride") {
            const auto parsed = parseUInt64();
            if (!parsed) {
                return reject(QStringLiteral("a 64-bit unsigned integer"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    (property == "offset" ? reg->offset : reg->array.stride) = *parsed;
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "an address layout without new overlaps, overflow, "
                        "or out-of-block Register instances"));
            }
            return commit(mutation);
        }
        if (property == "width" || property == "array_count") {
            const auto parsed = parseUInt32();
            if (!parsed || *parsed == 0 ||
                (property == "width" && *parsed > maximumEditableWidth)) {
                return reject(
                    property == "width"
                        ? QStringLiteral("a register width from 1 to 65536 that contains all fields")
                        : QStringLiteral("a positive array count"));
            }
            const auto* current = regmap::findRegister(*workspace, objectId);
            if (property == "width" && current != nullptr &&
                !std::ranges::all_of(current->fields, [&](const regmap::Field& field) {
                    return field.msb < *parsed;
                })) {
                return reject(
                    QStringLiteral("a register width that contains all existing fields"));
            }
            if (property == "width" && current != nullptr &&
                !registerValuesFitWidth(*current, *parsed, true)) {
                return reject(
                    QStringLiteral(
                        "a register width that fits existing Initial, Reset, and Enum values"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    (property == "width" ? reg->width : reg->array.count) = *parsed;
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Register extent that preserves a non-overlapping "
                        "in-block address layout"));
            }
            if (property == "width" &&
                !acceptsNumericRangeEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a register width that preserves the existing numeric Range"));
            }
            return commit(mutation);
        }
        if (property == "initial" || property == "reset") {
            std::optional<regmap::UnsignedValue> parsed;
            const auto* current = regmap::findRegister(*workspace, objectId);
            if (!textValue.empty()) {
                parsed = parseUnsigned();
                if (!parsed || current == nullptr || !parsed->fitsInBits(current->width)) {
                    return reject(
                        QStringLiteral("an unsigned integer fitting the register width, or empty"));
                }
                const bool enumerationLike =
                    current->type == regmap::FieldType::enumeration ||
                    current->type == regmap::FieldType::boolean;
                if (enumerationLike && !current->enumValues.empty() &&
                    std::ranges::none_of(
                        current->enumValues, [&](const regmap::EnumValue& enumValue) {
                            return enumValue.value == *parsed;
                        })) {
                    return reject(
                        QStringLiteral("an existing Enum value for this register, or empty"));
                }
                if (property == "reset" &&
                    !resetMatchesFieldEnums(
                        current->fields, *parsed,
                        static_cast<std::uint64_t>(current->width))) {
                    return reject(
                        QStringLiteral(
                            "a Reset value whose Enum/Bool Field slices match "
                            "existing Enum values, or empty"));
                }
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    if (property == "initial") {
                        reg->initialValue = parsed;
                    } else {
                        reg->resetValue = parsed;
                        refreshRegisterFieldResets(*reg);
                    }
                }
            });
        }
        if (property == "access") {
            const auto parsed = regmap::parseAccessMode(textValue);
            if (!parsed) {
                return reject(QStringLiteral("none, ro, wo, or rw"));
            }
            const auto* current = regmap::findRegister(*workspace, objectId);
            if (current == nullptr ||
                (current->reserved && *parsed != regmap::AccessMode::none)) {
                return reject(QStringLiteral("NONE access for a reserved Register"));
            }
            if (!fieldAccessFitsRegister(current->fields, *parsed)) {
                return reject(
                    QStringLiteral(
                        "Register Access that permits every Field software access"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* reg = regmap::findRegister(candidate, objectId)) {
                    reg->access = *parsed;
                }
            });
        }
    }

    if (regmap::findRegisterBlock(*workspace, objectId) != nullptr) {
        if (property == "name") {
            const auto* siblings = blockSiblings(*workspace, objectId);
            if (siblings == nullptr ||
                !isUniqueSiblingName(*siblings, objectId, textValue)) {
                return reject(
                    QStringLiteral("a non-empty unique Block name within its Page"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* block = regmap::findRegisterBlock(candidate, objectId)) {
                    block->name = textValue;
                }
            });
        }
        if (property == "description") {
            return commit([=](regmap::Workspace& candidate) {
                if (auto* block = regmap::findRegisterBlock(candidate, objectId)) {
                    block->description = textValue;
                }
            });
        }
        if (property == "base" || property == "size") {
            std::optional<std::uint64_t> parsed;
            const bool optionalSize = property == "size" && textValue.empty();
            if (!optionalSize) {
                parsed = parseUInt64();
                if (!parsed) {
                    return reject(QStringLiteral("a 64-bit unsigned integer"));
                }
                if (property == "size" && *parsed == 0) {
                    return reject(
                        QStringLiteral("a positive block size or an empty value"));
                }
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* block = regmap::findRegisterBlock(candidate, objectId)) {
                    if (property == "base") {
                        block->baseAddress = *parsed;
                    } else {
                        block->size = parsed;
                    }
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Block address layout without new overflow, overlap, "
                        "or out-of-block Register instances"));
            }
            return commit(mutation);
        }
    }

    if (regmap::findAddressSpace(*workspace, objectId) != nullptr) {
        if (property == "name") {
            if (!isUniqueSiblingName(
                    workspace->addressSpaces, objectId, textValue)) {
                return reject(
                    QStringLiteral("a non-empty unique Page name within the Workspace"));
            }
            return commit([=](regmap::Workspace& candidate) {
                if (auto* space = regmap::findAddressSpace(candidate, objectId)) {
                    space->name = textValue;
                }
            });
        }
        if (property == "description") {
            return commit([=](regmap::Workspace& candidate) {
                if (auto* space = regmap::findAddressSpace(candidate, objectId)) {
                    space->description = textValue;
                }
            });
        }
        if (property == "base") {
            const auto parsed = parseUInt64();
            if (!parsed) {
                return reject(QStringLiteral("a 64-bit unsigned integer"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* space = regmap::findAddressSpace(candidate, objectId)) {
                    space->baseAddress = *parsed;
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "a Page Base that keeps its address layout within range"));
            }
            return commit(mutation);
        }
        if (property == "address_width") {
            const auto parsed = parseUInt32();
            if (!parsed || *parsed == 0 || *parsed > 64) {
                return reject(
                    QStringLiteral("an address width from 1 to 64 bits"));
            }
            const regmap::WorkspaceStore::Mutation mutation =
                [=](regmap::Workspace& candidate) {
                if (auto* space = regmap::findAddressSpace(candidate, objectId)) {
                    space->addressWidth = *parsed;
                }
            };
            if (!acceptsAddressEdit(objectId, mutation)) {
                return reject(
                    QStringLiteral(
                        "an Address Width containing the Page Base and all Registers"));
            }
            return commit(mutation);
        }
    }

    return reject(QStringLiteral("a supported editable property"));
}

void MainWindow::addAddressSpace()
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return;
    }
    regmap::AddressSpace addressSpace;
    addressSpace.id = regmap::makeStableObjectId(*workspace, "space");
    addressSpace.name =
        uniqueDefaultName(workspace->addressSpaces, "NEW_PAGE");
    addressSpace.addressWidth = 32;
    const std::string newId = addressSpace.id;
    if (controller_.editWorkspace(
            QStringLiteral("Add page"),
            [addressSpace = std::move(addressSpace)](regmap::Workspace& candidate) mutable {
                candidate.addressSpaces.push_back(std::move(addressSpace));
            })) {
        selectedAddressId_ = newId;
        selectedBlockId_.clear();
        selectedRegisterId_.clear();
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        beginHierarchyRename(newId);
    }
}

void MainWindow::addBlock(std::string parentId)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || workspace->addressSpaces.empty()) {
        statusBar()->showMessage(QStringLiteral("Add a page first"), 5000);
        return;
    }
    const bool explicitParent = !parentId.empty();
    if (parentId.empty()) {
        parentId = selectedAddressId_;
    }
    if (regmap::findAddressSpace(*workspace, parentId) == nullptr) {
        if (explicitParent) {
            statusBar()->showMessage(
                QStringLiteral("The target Page no longer exists; reopen the menu and try again"),
                5000);
            return;
        }
        parentId = workspace->addressSpaces.front().id;
    }
    regmap::RegisterBlock block;
    block.id = regmap::makeStableObjectId(*workspace, "block");
    const auto* parent = regmap::findAddressSpace(*workspace, parentId);
    if (parent == nullptr) {
        statusBar()->showMessage(
            QStringLiteral("The target Page no longer exists; reopen the menu and try again"),
            5000);
        return;
    }
    block.name = uniqueDefaultName(parent->blocks, "NEW_BLOCK");

    constexpr std::uint64_t preferredBlockSize = 0x1000;
    std::optional<std::uint64_t> placement;
    for (std::uint64_t size = preferredBlockSize;; size /= 2) {
        block.size = size;
        placement = availableBlockBase(*parent, block);
        if (placement || size == 1) {
            break;
        }
    }
    if (!placement) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot add a Register Block to Page %1: no free address range "
                "remains. Resize or move an existing Block, or increase Address Width.")
                .arg(fromUtf8(parent->name)),
            8000);
        return;
    }
    block.baseAddress = *placement;
    const std::string newId = block.id;
    const std::uint64_t addedBase = block.baseAddress;
    const std::uint64_t addedSize = block.size.value_or(1);
    if (controller_.editWorkspace(
            QStringLiteral("Add register block"),
            [parentId, block = std::move(block)](regmap::Workspace& candidate) mutable {
                if (auto* parent = regmap::findAddressSpace(candidate, parentId)) {
                    parent->blocks.push_back(std::move(block));
                }
            })) {
        selectedAddressId_ = parentId;
        selectedBlockId_ = newId;
        selectedRegisterId_.clear();
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        statusBar()->showMessage(
            QStringLiteral(
                "Added Register Block at Base %1, Size %2; Ctrl+Z to restore")
                .arg(hex(addedBase), hex(addedSize)),
            5000);
        beginHierarchyRename(newId);
    }
}

void MainWindow::addRegister(std::string parentId)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return;
    }
    const bool explicitParent = !parentId.empty();
    if (parentId.empty()) {
        parentId = selectedBlockId_;
    }
    const regmap::RegisterBlock* parent = nullptr;
    std::string parentAddressId;
    for (const auto& space : workspace->addressSpaces) {
        const auto block = std::ranges::find(space.blocks, parentId, &regmap::RegisterBlock::id);
        if (block != space.blocks.end()) {
            parent = &*block;
            parentAddressId = space.id;
            break;
        }
    }
    if (parent == nullptr) {
        if (explicitParent) {
            statusBar()->showMessage(
                QStringLiteral("The target Register Block no longer exists; reopen the menu and "
                               "try again"),
                5000);
            return;
        }
        for (const auto& space : workspace->addressSpaces) {
            if (!space.blocks.empty()) {
                parent = &space.blocks.front();
                parentId = parent->id;
                parentAddressId = space.id;
                break;
            }
        }
    }
    if (parent == nullptr) {
        statusBar()->showMessage(QStringLiteral("Add a register block first"), 5000);
        return;
    }

    constexpr std::uint64_t registerAlignment = 4;
    regmap::Register reg;
    reg.id = regmap::makeStableObjectId(*workspace, "reg");
    reg.name =
        uniqueDefaultName(parent->registers, "NEW_REGISTER");
    reg.width = 32;
    reg.array.count = 1;
    reg.array.stride = 4;
    reg.type = regmap::FieldType::unsignedInteger;
    reg.initialValue = regmap::UnsignedValue(0);
    reg.resetValue = regmap::UnsignedValue(0);
    reg.access = regmap::AccessMode::readWrite;

    std::uint64_t appendOffset = 0;
    for (const auto& existing : parent->registers) {
        const auto extent = checkedRegisterExtent(existing);
        std::uint64_t end = 0;
        if (!extent || addOverflow(existing.offset, *extent, end)) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot add a Register to Block %1: an existing Register extent "
                    "overflows. Repair its Offset or Width first.")
                    .arg(fromUtf8(parent->name)),
                8000);
            return;
        }
        appendOffset = std::max(appendOffset, end);
    }
    if (appendOffset >
        std::numeric_limits<std::uint64_t>::max() -
            (registerAlignment - 1)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot add a Register to Block %1: no aligned Offset remains.")
                .arg(fromUtf8(parent->name)),
            8000);
        return;
    }
    reg.offset =
        (appendOffset + (registerAlignment - 1)) &
        ~(registerAlignment - 1);

    regmap::Workspace candidate = *workspace;
    if (auto* candidateBlock =
            regmap::findRegisterBlock(candidate, parentId)) {
        candidateBlock->registers.push_back(reg);
    }
    if (!addressEditDoesNotWorsen(*workspace, candidate, parentId)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot add a 32-bit Register at Offset %1 in Block %2: the aligned "
                "slot does not fit its Size or the Page address range. Increase Block "
                "Size or Address Width, or adjust existing Offsets.")
                .arg(hex(reg.offset), fromUtf8(parent->name)),
            9000);
        return;
    }

    const std::string newId = reg.id;
    const std::uint64_t addedOffset = reg.offset;
    if (controller_.editWorkspace(
            QStringLiteral("Add register"),
            [parentId, reg = std::move(reg)](regmap::Workspace& candidate) mutable {
                if (auto* block = regmap::findRegisterBlock(candidate, parentId)) {
                    block->registers.push_back(std::move(reg));
                }
            })) {
        selectedAddressId_ = parentAddressId;
        selectedBlockId_ = parentId;
        selectedRegisterId_ = newId;
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        selectRegister(newId);
        statusBar()->showMessage(
            QStringLiteral("Added Register at Offset %1; Ctrl+Z to restore")
                .arg(hex(addedOffset)), 5000);
        beginRegisterRename(newId);
    }
}

bool MainWindow::canInsertRegisterAt(int row) const
{
    if (!selectedTagFilter_.empty() || registerModel_ == nullptr || row <= 0 ||
        row >= registerModel_->rowCount() - 1) {
        return false;
    }
    const QModelIndex previous = registerModel_->index(row - 1, registerNameColumn);
    const QModelIndex next = registerModel_->index(row, registerNameColumn);
    const std::string previousBlock = previous.data(blockIdRole).toString().toUtf8().toStdString();
    const std::string nextBlock = next.data(blockIdRole).toString().toUtf8().toStdString();
    if (previousBlock.empty() || previousBlock != nextBlock) {
        return false;
    }
    const auto* block = findBlock(previousBlock);
    if (block == nullptr) {
        return false;
    }
    const std::string previousId = previous.data(objectIdRole).toString().toUtf8().toStdString();
    const std::string nextId = next.data(objectIdRole).toString().toUtf8().toStdString();
    const auto previousIterator =
        std::ranges::find(block->registers, previousId, &regmap::Register::id);
    const auto nextIterator = std::ranges::find(block->registers, nextId, &regmap::Register::id);
    return previousIterator != block->registers.end() && nextIterator != block->registers.end() &&
           std::next(previousIterator) == nextIterator;
}

void MainWindow::insertRegisterAt(int row)
{
    if (!canInsertRegisterAt(row)) {
        return;
    }
    const QModelIndex nextIndex = registerModel_->index(row, registerNameColumn);
    const std::string blockId = nextIndex.data(blockIdRole).toString().toUtf8().toStdString();
    const std::string addressId = nextIndex.data(addressIdRole).toString().toUtf8().toStdString();
    const std::string nextId = nextIndex.data(objectIdRole).toString().toUtf8().toStdString();
    const auto* workspace = controller_.workspace();
    const auto* block = workspace == nullptr ? nullptr : findBlock(blockId);
    if (workspace == nullptr || block == nullptr) {
        return;
    }
    const auto next = std::ranges::find(block->registers, nextId, &regmap::Register::id);
    if (next == block->registers.end()) {
        return;
    }

    regmap::Register reg;
    reg.id = regmap::makeStableObjectId(*workspace, "reg");
    reg.name =
        uniqueDefaultName(block->registers, "NEW_REGISTER");
    reg.offset = next->offset;
    reg.width = 32;
    reg.array.count = 1;
    reg.array.stride = 4;
    reg.type = regmap::FieldType::unsignedInteger;
    reg.initialValue = regmap::UnsignedValue(0);
    reg.resetValue = regmap::UnsignedValue(0);
    reg.access = regmap::AccessMode::readWrite;
    const std::uint64_t shift = registerExtent(reg);
    if (std::ranges::any_of(next, block->registers.end(), [shift](const regmap::Register& current) {
            return current.offset > std::numeric_limits<std::uint64_t>::max() - shift;
        })) {
        statusBar()->showMessage(
            QStringLiteral("Cannot insert: a following register offset would overflow"), 5000);
        return;
    }

    regmap::Workspace candidate = *workspace;
    if (auto* targetBlock =
            regmap::findRegisterBlock(candidate, blockId)) {
        const auto insertion =
            std::ranges::find(
                targetBlock->registers, nextId,
                &regmap::Register::id);
        if (insertion != targetBlock->registers.end()) {
            for (auto current = insertion;
                 current != targetBlock->registers.end(); ++current) {
                current->offset += shift;
            }
            targetBlock->registers.insert(insertion, reg);
        }
    }
    if (!addressEditDoesNotWorsen(*workspace, candidate, blockId)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot insert a 32-bit Register at Offset %1: shifting following "
                "Registers would exceed Block Size or the Page address range. "
                "Increase Block Size or Address Width, or adjust existing Offsets.")
                .arg(hex(reg.offset)),
            9000);
        return;
    }

    const std::string newId = reg.id;
    const std::uint64_t addedOffset = reg.offset;
    const std::size_t shiftedCount =
        static_cast<std::size_t>(
            std::distance(next, block->registers.end()));
    if (controller_.editWorkspace(
            QStringLiteral("Insert register and shift following offsets"),
            [blockId, nextId, shift, reg = std::move(reg)](regmap::Workspace& candidate) mutable {
                auto* targetBlock = regmap::findRegisterBlock(candidate, blockId);
                if (targetBlock == nullptr) {
                    return;
                }
                const auto insertion =
                    std::ranges::find(targetBlock->registers, nextId, &regmap::Register::id);
                if (insertion == targetBlock->registers.end()) {
                    return;
                }
                for (auto current = insertion; current != targetBlock->registers.end(); ++current) {
                    current->offset += shift;
                }
                targetBlock->registers.insert(insertion, std::move(reg));
            })) {
        selectedAddressId_ = addressId;
        selectedBlockId_ = blockId;
        selectedRegisterId_ = newId;
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        selectRegister(newId);
        statusBar()->showMessage(
            QStringLiteral(
                "Inserted Register at Offset %1 and shifted %2 following "
                "Register(s); Ctrl+Z to restore")
                .arg(hex(addedOffset))
                .arg(shiftedCount), 6000);
        beginRegisterRename(newId);
    }
}

void MainWindow::addField()
{
    const auto* workspace = controller_.workspace();
    const auto* reg =
        workspace == nullptr ? nullptr : regmap::findRegister(*workspace, selectedRegisterId_);
    if (workspace == nullptr || reg == nullptr) {
        statusBar()->showMessage(QStringLiteral("Select a register first"), 5000);
        return;
    }
    if (reg->reserved) {
        statusBar()->showMessage(QStringLiteral("Reserved registers cannot contain fields"), 5000);
        return;
    }
    std::vector<bool> used(reg->width, false);
    for (const auto& field : reg->fields) {
        if (field.msb < reg->width && field.lsb <= field.msb) {
            for (std::uint32_t bit = field.lsb; bit <= field.msb; ++bit) {
                used[bit] = true;
            }
        }
    }
    const auto freeBit = std::ranges::find(used, false);
    if (freeBit == used.end()) {
        statusBar()->showMessage(QStringLiteral("The selected register has no free bit"), 5000);
        return;
    }

    regmap::Field field = makeDefaultRegisterField(
        *workspace, *reg, static_cast<std::uint32_t>(std::distance(used.begin(), freeBit)));
    const std::string newId = field.id;
    const std::string registerId = reg->id;
    if (controller_.editWorkspace(
            QStringLiteral("Add field"),
            [registerId, field = std::move(field)](regmap::Workspace& candidate) mutable {
                if (auto* target = regmap::findRegister(candidate, registerId)) {
                    target->type = regmap::FieldType::structure;
                    target->minimumValue.reset();
                    target->maximumValue.reset();
                    target->enumValues.clear();
                    target->fields.push_back(std::move(field));
                }
            })) {
        selectedFieldId_ = newId;
        refreshProject();
        selectRegister(registerId);
        selectField(newId);
        beginFieldRename(newId);
    }
}

void MainWindow::addSubfield()
{
    const auto* workspace = controller_.workspace();
    const auto* reg = workspace == nullptr ? nullptr : findRegister(selectedRegisterId_);
    const auto* parent =
        workspace == nullptr ? nullptr : regmap::findField(*workspace, selectedFieldId_);
    if (workspace == nullptr || reg == nullptr || parent == nullptr ||
        parent->type != regmap::FieldType::structure) {
        statusBar()->showMessage(QStringLiteral("Select a field/compound field first"), 5000);
        return;
    }
    if (parent->width() > std::numeric_limits<std::size_t>::max()) {
        statusBar()->showMessage(QStringLiteral("Compound field width is too large"), 5000);
        return;
    }
    std::vector<bool> used(static_cast<std::size_t>(parent->width()), false);
    for (const auto& member : parent->members) {
        if (member.msb < used.size() && member.lsb <= member.msb) {
            for (std::uint32_t bit = member.lsb; bit <= member.msb; ++bit) {
                used[bit] = true;
            }
        }
    }
    const auto freeBit = std::ranges::find(used, false);
    if (freeBit == used.end()) {
        statusBar()->showMessage(QStringLiteral("The compound field has no free bit"), 5000);
        return;
    }

    regmap::Field member;
    member.id = regmap::makeStableObjectId(*workspace, "field");
    member.name = uniqueDefaultName(parent->members, "NEW_MEMBER");
    member.msb = member.lsb = static_cast<std::uint32_t>(std::distance(used.begin(), freeBit));
    member.type = regmap::FieldType::bits;
    member.softwareAccess = reg->access;
    member.hardwareAccess = regmap::AccessMode::none;
    member.writeSideEffect = (reg->access == regmap::AccessMode::writeOnly ||
                              reg->access == regmap::AccessMode::readWrite)
                                 ? regmap::WriteSideEffect::write
                                 : regmap::WriteSideEffect::none;
    const std::string newId = member.id;
    const std::string parentId = parent->id;
    const std::string registerId = reg->id;
    if (controller_.editWorkspace(
            QStringLiteral("Add member field"), [parentId, registerId, member = std::move(member)](
                                                    regmap::Workspace& candidate) mutable {
                if (auto* target = regmap::findField(candidate, parentId)) {
                    target->members.push_back(std::move(member));
                    if (auto* owner = regmap::findRegister(candidate, registerId)) {
                        refreshRegisterFieldResets(*owner);
                    }
                }
            })) {
        selectedFieldId_ = newId;
        refreshProject();
        selectRegister(registerId);
        selectField(newId);
        beginFieldRename(newId);
    }
}

void MainWindow::addEnumValue()
{
    const auto* workspace = controller_.workspace();
    const auto* reg =
        workspace == nullptr ? nullptr : regmap::findRegister(*workspace, selectedRegisterId_);
    const auto* field = workspace == nullptr || selectedFieldId_.empty()
                            ? nullptr
                            : regmap::findField(*workspace, selectedFieldId_);
    if (workspace == nullptr || (field == nullptr && reg == nullptr)) {
        statusBar()->showMessage(QStringLiteral("Select a register or field first"), 5000);
        return;
    }

    const std::size_t structuredChildCount =
        field != nullptr
            ? fieldTreeSize(field->members)
            : fieldTreeSize(reg->fields);
    const bool structuredOwner =
        field != nullptr
            ? field->type == regmap::FieldType::structure
            : reg->type == regmap::FieldType::structure;
    if (structuredOwner) {
        const QString childLabel =
            field != nullptr
                ? (structuredChildCount == 1
                       ? QStringLiteral("1 Member Field")
                       : QStringLiteral("%1 Member Fields")
                             .arg(structuredChildCount))
                : (structuredChildCount == 1
                       ? QStringLiteral("1 Field")
                       : QStringLiteral("%1 Fields/Members")
                             .arg(structuredChildCount));
        statusBar()->showMessage(
            QStringLiteral(
                "Change Type to enum first to review removal of %1")
                .arg(childLabel),
            6000);
        return;
    }
    const bool reservedOwner =
        field != nullptr
            ? field->type == regmap::FieldType::reserved
            : reg->reserved || reg->type == regmap::FieldType::reserved;
    if (reservedOwner) {
        statusBar()->showMessage(
            QStringLiteral(
                "Change Type from reserved before adding Enum values"),
            6000);
        return;
    }

    const regmap::FieldType ownerType =
        field != nullptr ? field->type : reg->type;
    const bool changesType =
        ownerType != regmap::FieldType::enumeration &&
        ownerType != regmap::FieldType::boolean;
    const bool hasRange =
        field != nullptr
            ? field->minimumValue.has_value() ||
                  field->maximumValue.has_value()
            : reg->minimumValue.has_value() ||
                  reg->maximumValue.has_value();
    if (changesType && hasRange) {
        statusBar()->showMessage(
            QStringLiteral(
                "Change Type to enum first to review removal of the existing Range"),
            6000);
        return;
    }

    const auto& currentValues = field != nullptr ? field->enumValues : reg->enumValues;
    std::vector<regmap::EnumValue> addedValues;
    if (currentValues.empty()) {
        addedValues =
            field != nullptr
                ? makeDefaultEnumValues(
                      *workspace, {effectiveFieldReset(*field, reg)})
                : makeDefaultEnumValues(
                      *workspace, {reg->initialValue, reg->resetValue});
    } else {
        const std::uint64_t width = field != nullptr ? field->width() : reg->width;
        std::uint64_t value = 0;
        while (std::ranges::any_of(
            currentValues, [&](const regmap::EnumValue& item) {
                return item.value == regmap::UnsignedValue(value);
            })) {
            ++value;
        }
        if (!regmap::UnsignedValue(value).fitsInBits(width)) {
            statusBar()->showMessage(
                QStringLiteral("No unused enum value fits in this object"), 5000);
            return;
        }
        regmap::EnumValue enumValue;
        enumValue.id = regmap::makeStableObjectId(*workspace, "enum");
        enumValue.name = uniqueDefaultName(currentValues, "NEW_VALUE");
        enumValue.value = regmap::UnsignedValue(value);
        addedValues.push_back(std::move(enumValue));
    }
    const std::string newId = addedValues.front().id;
    const std::string ownerId = field != nullptr ? field->id : reg->id;
    const bool fieldOwner = field != nullptr;
    if (controller_.editWorkspace(
            QStringLiteral("Add enum value"),
            [ownerId, fieldOwner,
             addedValues = std::move(addedValues)](
                regmap::Workspace& candidate) mutable {
                if (fieldOwner) {
                    if (auto* target = regmap::findField(candidate, ownerId)) {
                        if (target->type != regmap::FieldType::boolean) {
                            target->type = regmap::FieldType::enumeration;
                        }
                        target->minimumValue.reset();
                        target->maximumValue.reset();
                        target->enumValues.insert(
                            target->enumValues.end(),
                            std::make_move_iterator(addedValues.begin()),
                            std::make_move_iterator(addedValues.end()));
                    }
                } else if (auto* target = regmap::findRegister(candidate, ownerId)) {
                    if (target->type != regmap::FieldType::boolean) {
                        target->type = regmap::FieldType::enumeration;
                    }
                    target->minimumValue.reset();
                    target->maximumValue.reset();
                    target->enumValues.insert(
                        target->enumValues.end(),
                        std::make_move_iterator(addedValues.begin()),
                        std::make_move_iterator(addedValues.end()));
                }
            })) {
        refreshProject();
        const auto* selectedRegister = findRegister(selectedRegisterId_);
        const auto* selectedField = selectedRegister == nullptr || selectedFieldId_.empty()
                                        ? nullptr
                                        : findField(*selectedRegister, selectedFieldId_);
        populateEnumValues(selectedRegister, selectedField);
        beginEnumRename(newId, ownerId, fieldOwner);
    }
}
void MainWindow::updateTagFilter()
{
    std::set<std::string, std::less<>> tags;
    if (const auto* workspace = controller_.workspace()) {
        for (const auto& space : workspace->addressSpaces) {
            for (const auto& block : space.blocks) {
                for (const auto& reg : block.registers) {
                    tags.insert(reg.tags.begin(), reg.tags.end());
                }
            }
        }
    }
    if (!selectedTagFilter_.empty() && !tags.contains(selectedTagFilter_)) {
        selectedTagFilter_.clear();
    }
    const QSignalBlocker blocker(tagFilter_);
    tagFilter_->clear();
    tagFilter_->addItem(QStringLiteral("All tags"));
    int selectedIndex = 0;
    for (const auto& tag : tags) {
        tagFilter_->addItem(fromUtf8(tag));
        if (tag == selectedTagFilter_) {
            selectedIndex = tagFilter_->count() - 1;
        }
    }
    tagFilter_->setCurrentIndex(selectedIndex);
}

void MainWindow::editRegisterTags(const QModelIndex& index)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || !index.isValid()) {
        return;
    }
    const std::string registerId = registerModel_->index(index.row(), registerNameColumn)
                                       .data(objectIdRole)
                                       .toString()
                                       .toUtf8()
                                       .toStdString();
    const auto* reg = findRegister(registerId);
    if (reg == nullptr) {
        return;
    }

    QStringList available;
    const auto appendUnique = [&available](const std::string& value) {
        const QString candidate = fromUtf8(value);
        if (std::ranges::none_of(available, [&](const QString& current) {
                return current.compare(candidate, Qt::CaseInsensitive) == 0;
            })) {
            available.push_back(candidate);
        }
    };
    for (const auto& space : workspace->addressSpaces) {
        for (const auto& block : space.blocks) {
            for (const auto& current : block.registers) {
                for (const auto& tag : current.tags) {
                    appendUnique(tag);
                }
            }
        }
    }
    for (const auto& tag : reg->tags) {
        appendUnique(tag);
    }
    available.sort(Qt::CaseInsensitive);

    auto* popup = createAnchoredPopup(registerView_, index, QStringLiteral("tagPopup"), 300, 270);
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(6);
    auto* inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    auto* search = new QLineEdit(popup);
    search->setObjectName(QStringLiteral("tagSearch"));
    search->setPlaceholderText(QStringLiteral("Filter or enter a tag"));
    auto* add = new QToolButton(popup);
    add->setObjectName(QStringLiteral("addTagButton"));
    add->setText(QStringLiteral("+"));
    add->setToolTip(QStringLiteral("Create and select this tag"));
    add->setFixedWidth(34);
    add->setEnabled(false);
    inputRow->addWidget(search, 1);
    inputRow->addWidget(add);
    layout->addLayout(inputRow);

    auto* list = new QListWidget(popup);
    constexpr int selectedRole = Qt::UserRole;
    list->setObjectName(QStringLiteral("tagOptions"));
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    list->setMouseTracking(true);
    list->setStyleSheet(QString::fromLatin1(popupListStyle));
    for (const QString& value : available) {
        auto* current = new QListWidgetItem(value, list);
        current->setFlags(current->flags() & ~Qt::ItemIsUserCheckable);
        const bool selected = std::ranges::any_of(reg->tags, [&](const std::string& tag) {
            return fromUtf8(tag).compare(value, Qt::CaseInsensitive) == 0;
        });
        current->setData(selectedRole, selected);
        current->setSelected(selected);
    }
    layout->addWidget(list, 1);

    const auto containsTag = [list](const QString& value) {
        for (int itemIndex = 0; itemIndex < list->count(); ++itemIndex) {
            if (list->item(itemIndex)->text().compare(value, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    const auto commit = [this, popup, list, registerId] {
        std::vector<std::string> tags;
        for (int itemIndex = 0; itemIndex < list->count(); ++itemIndex) {
            if (list->item(itemIndex)->data(selectedRole).toBool()) {
                tags.push_back(list->item(itemIndex)->text().trimmed().toUtf8().toStdString());
            }
        }
        std::ranges::sort(tags);
        tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
        selectedRegisterId_ = registerId;
        const bool changed = controller_.editWorkspace(
            QStringLiteral("Edit register tags"),
            [registerId, tags = std::move(tags)](regmap::Workspace& candidate) {
                if (auto* target = regmap::findRegister(candidate, registerId)) {
                    target->tags = tags;
                }
            });
        if (!changed || selectedTagFilter_.empty()) {
            return;
        }
        const auto* updated = findRegister(registerId);
        if (updated == nullptr ||
            std::ranges::find(updated->tags, selectedTagFilter_) ==
                updated->tags.end()) {
            popup->close();
        }
    };
    const auto updateFilter = [list, add, containsTag](const QString& text) {
        const QString filter = text.trimmed();
        const bool containsSeparator = filter.contains(',');
        const bool duplicate = !filter.isEmpty() && containsTag(filter);
        for (int itemIndex = 0; itemIndex < list->count(); ++itemIndex) {
            list->item(itemIndex)->setHidden(
                !filter.isEmpty() &&
                !list->item(itemIndex)->text().contains(filter, Qt::CaseInsensitive));
        }
        add->setEnabled(!filter.isEmpty() && !containsSeparator && !duplicate);
        if (containsSeparator) {
            add->setToolTip(QStringLiteral("Tag names cannot contain commas; commas separate tags"));
        } else if (duplicate) {
            add->setToolTip(
                QStringLiteral("Press Enter to select or clear this existing tag"));
        } else {
            add->setToolTip(QStringLiteral("Create and select this tag"));
        }
    };
    connect(search, &QLineEdit::textChanged, popup, updateFilter);
    const auto toggleTag = [list, commit](QListWidgetItem* current) {
        current->setData(selectedRole, !current->data(selectedRole).toBool());
        for (int itemIndex = 0; itemIndex < list->count(); ++itemIndex) {
            list->item(itemIndex)->setSelected(
                list->item(itemIndex)->data(selectedRole).toBool());
        }
        commit();
    };
    connect(list, &QListWidget::itemClicked, popup, toggleTag);
    connect(add, &QToolButton::clicked, popup, [=] {
        const QString candidate = search->text().trimmed();
        if (candidate.isEmpty() || candidate.contains(',') ||
            containsTag(candidate)) {
            return;
        }
        auto* current = new QListWidgetItem(candidate, list);
        current->setFlags(current->flags() & ~Qt::ItemIsUserCheckable);
        current->setData(selectedRole, true);
        current->setSelected(true);
        list->sortItems(Qt::AscendingOrder);
        search->clear();
        commit();
    });
    connect(search, &QLineEdit::returnPressed, popup,
            [this, search, add, list, toggleTag] {
                if (add->isEnabled()) {
                    add->click();
                    return;
                }
                const QString candidate = search->text().trimmed();
                if (candidate.isEmpty()) {
                    return;
                }
                if (candidate.contains(',')) {
                    statusBar()->showMessage(
                        QStringLiteral("Tag was not created: commas separate tags"), 5000);
                    return;
                }
                for (int itemIndex = 0; itemIndex < list->count(); ++itemIndex) {
                    auto* current = list->item(itemIndex);
                    if (current->text().compare(candidate, Qt::CaseInsensitive) == 0) {
                        toggleTag(current);
                        return;
                    }
                }
            });

    popup->show();
    search->setFocus();
}

void MainWindow::editRegisterAccess(const QModelIndex& index)
{
    if (!index.isValid()) {
        return;
    }
    const std::string registerId = registerModel_->index(index.row(), registerNameColumn)
                                       .data(objectIdRole)
                                       .toString()
                                       .toUtf8()
                                       .toStdString();
    const auto* reg = findRegister(registerId);
    if (reg == nullptr) {
        return;
    }

    auto* popup =
        createAnchoredPopup(registerView_, index, QStringLiteral("accessPopup"), 150, 158);
    auto* layout = new QVBoxLayout(popup);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* list = new QListWidget(popup);
    list->setObjectName(QStringLiteral("accessOptions"));
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMouseTracking(true);
    list->setStyleSheet(QString::fromLatin1(popupListStyle));
    const QString currentAccess = accessText(reg->access).toUpper();
    for (const QString& value : {QStringLiteral("NONE"), QStringLiteral("RO"), QStringLiteral("WO"),
                                 QStringLiteral("RW")}) {
        auto* current = new QListWidgetItem(value, list);
        current->setFlags(current->flags() & ~Qt::ItemIsUserCheckable);
        if (value == currentAccess) {
            list->setCurrentItem(current);
        }
    }
    layout->addWidget(list);
    const auto chooseAccess =
        [this, popup, registerId](QListWidgetItem* current) {
            selectedRegisterId_ = registerId;
            applyPropertyEdit(registerId, "access", current->text().toLower());
            popup->close();
        };
    connect(list, &QListWidget::itemClicked, popup, chooseAccess);
    connect(list, &QListWidget::itemActivated, popup, chooseAccess);
    popup->show();
    list->setFocus();
}

void MainWindow::showHierarchyContextMenu(const QPoint& position)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return;
    }

    if (QWidget* active = QApplication::activePopupWidget()) {
        active->close();
    }
    const QModelIndex index = hierarchyView_->indexAt(position);
    if (index.isValid()) {
        hierarchyView_->setCurrentIndex(index);
        hierarchyView_->setFocus(Qt::OtherFocusReason);
    }

    const std::string objectId =
        index.data(objectIdRole).toString().toUtf8().toStdString();
    const std::string addressId =
        index.data(addressIdRole).toString().toUtf8().toStdString();
    const std::string blockId =
        index.data(blockIdRole).toString().toUtf8().toStdString();
    const bool workspaceItem = index.isValid() && objectId == workspace->id;
    const bool pageItem = index.isValid() && !addressId.empty() && blockId.empty();
    const bool blockItem = index.isValid() && !blockId.empty();

    auto* menu = new QMenu(hierarchyView_);
    menu->setObjectName(QStringLiteral("hierarchyContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto action = [menu](const QString& text, const QString& objectName) {
        QAction* result = menu->addAction(text);
        result->setObjectName(objectName);
        return result;
    };

    QAction* newPage = nullptr;
    QAction* newBlock = nullptr;
    QAction* newRegister = nullptr;
    if (!index.isValid() || workspaceItem) {
        newPage = action(QStringLiteral("New Page"), QStringLiteral("newPageContextAction"));
    } else if (pageItem) {
        newBlock = action(QStringLiteral("New Register Block"),
                          QStringLiteral("newBlockContextAction"));
    } else if (blockItem) {
        newRegister =
            action(QStringLiteral("New Register"), QStringLiteral("newRegisterContextAction"));
    }

    QAction* rename = nullptr;
    QAction* remove = nullptr;
    if (index.isValid()) {
        menu->addSeparator();
        rename = action(QStringLiteral("Rename"), QStringLiteral("renameContextAction"));
        rename->setShortcut(QKeySequence(Qt::Key_F2));
        if (pageItem || blockItem) {
            remove = action(pageItem ? QStringLiteral("Delete Page")
                                     : QStringLiteral("Delete Register Block"),
                            QStringLiteral("deleteContextAction"));
            remove->setShortcut(QKeySequence::Delete);
        }
    }
    menu->addSeparator();
    QAction* expand =
        action(QStringLiteral("Expand All"), QStringLiteral("expandAllContextAction"));
    QAction* collapse =
        action(QStringLiteral("Collapse All"), QStringLiteral("collapseAllContextAction"));

    if (newPage != nullptr) {
        connect(newPage, &QAction::triggered, this, [this, menu] {
            menu->close();
            addAddressSpace();
        });
    }
    if (newBlock != nullptr) {
        connect(newBlock, &QAction::triggered, this, [this, menu, objectId] {
            menu->close();
            addBlock(objectId);
        });
    }
    if (newRegister != nullptr) {
        connect(newRegister, &QAction::triggered, this, [this, menu, objectId] {
            menu->close();
            addRegister(objectId);
        });
    }
    if (rename != nullptr) {
        connect(rename, &QAction::triggered, this, [this, menu, objectId] {
            menu->close();
            if (!navigateToObject(objectId)) {
                statusBar()->showMessage(
                    QStringLiteral("The target no longer exists; reopen the menu and try again"),
                    5000);
                return;
            }
            beginHierarchyRename(objectId);
        });
    }
    if (remove != nullptr) {
        connect(remove, &QAction::triggered, this, [this, menu, objectId] {
            menu->close();
            deleteObject(objectId);
        });
    }
    connect(expand, &QAction::triggered, this, [this, menu] {
        menu->close();
        hierarchyView_->expandAll();
    });
    connect(collapse, &QAction::triggered, this, [this, menu] {
        menu->close();
        hierarchyView_->collapseAll();
    });

    menu->popup(hierarchyView_->viewport()->mapToGlobal(position));
}

void MainWindow::openFieldsAt(const QModelIndex& index)
{
    if (!index.isValid() || !index.data(openFieldsRole).toBool()) {
        return;
    }
    const std::string registerId =
        registerModel_->index(index.row(), registerNameColumn)
            .data(objectIdRole)
            .toString()
            .toUtf8()
            .toStdString();
    openFieldsForRegister(registerId);
}

void MainWindow::openFieldsForRegister(const std::string& registerId)
{
    const auto* reg = findRegister(registerId);
    if (reg == nullptr) {
        return;
    }
    if (reg->reserved || reg->type != regmap::FieldType::structure) {
        statusBar()->showMessage(QStringLiteral("This register has no Field editor"), 4000);
        return;
    }

    const std::string previousRegisterId = openFieldsRegisterId_;
    openFieldsRegisterId_ = registerId;
    selectedRegisterId_ = registerId;
    selectedFieldId_.clear();
    selectRegister(registerId);
    populateFields(reg);
    fieldPanel_->setVisible(true);
    if (previousRegisterId != registerId) {
        updateFieldsAction(previousRegisterId);
    }
    updateFieldsAction(registerId);
    if (fieldModel_->rowCount() > 0) {
        const QModelIndex first = fieldModel_->index(0, fieldNameColumn);
        fieldView_->setCurrentIndex(first);
        fieldView_->scrollTo(first);
    }
    fieldView_->setFocus(Qt::OtherFocusReason);
    statusBar()->showMessage(
        QStringLiteral("Opened Fields for %1").arg(fromUtf8(reg->name)), 3000);
}

void MainWindow::updateFieldsAction(const std::string& registerId)
{
    if (registerId.empty()) {
        return;
    }
    const auto* reg = findRegister(registerId);
    for (int row = 0; row < registerModel_->rowCount(); ++row) {
        if (registerModel_->index(row, registerNameColumn)
                .data(objectIdRole)
                .toString()
                .toUtf8()
                .toStdString() != registerId) {
            continue;
        }
        auto* action = registerModel_->item(row, registerFieldsColumn);
        if (action == nullptr) {
            return;
        }
        const bool canOpenFields =
            reg != nullptr && !reg->reserved && reg->type == regmap::FieldType::structure;
        const bool fieldsOpen = canOpenFields && openFieldsRegisterId_ == registerId;
        action->setText(
            canOpenFields
                ? QStringLiteral("%1 (%2)")
                      .arg(fieldsOpen ? QStringLiteral("Editing") : QStringLiteral("Open"))
                      .arg(reg->fields.size())
                : QString{});
        action->setData(canOpenFields, openFieldsRole);
        action->setData(fieldsOpen, fieldsOpenRole);
        action->setToolTip(
            canOpenFields
                ? (fieldsOpen ? QStringLiteral("These fields are open below")
                              : QStringLiteral("Open and edit this register's fields"))
                : QString{});
        return;
    }
}

void MainWindow::closeFields()
{
    if (openFieldsRegisterId_.empty()) {
        return;
    }
    const std::string closedRegisterId = openFieldsRegisterId_;
    openFieldsRegisterId_.clear();
    updateFieldsAction(closedRegisterId);
    selectedFieldId_.clear();
    const auto* reg = findRegister(selectedRegisterId_);
    populateFields(reg);
    setCurrentSource(reg == nullptr ? regmap::SourceLocation{} : reg->source);
    registerView_->setFocus(Qt::OtherFocusReason);
    statusBar()->showMessage(
        reg == nullptr
            ? QStringLiteral("Closed Fields")
            : QStringLiteral("Closed Fields for %1").arg(fromUtf8(reg->name)),
        3000);
}

void MainWindow::duplicateFocusedObject()
{
    QWidget* focus = QApplication::focusWidget();
    const bool fieldFocused =
        focusBelongsToWidgetOutsidePopup(focus, fieldView_);
    const bool registerFocused =
        !fieldFocused &&
        focusBelongsToWidgetOutsidePopup(focus, registerView_);
    QTableView* view =
        fieldFocused
            ? fieldView_
            : (registerFocused ? registerView_ : nullptr);
    if (view == nullptr) {
        return;
    }

    const QModelIndex current = view->currentIndex();
    const std::string objectId =
        current.data(objectIdRole).toString().toUtf8().toStdString();
    if (!current.isValid() ||
        current.data(addRowRole).toBool() ||
        objectId.empty()) {
        statusBar()->showMessage(
            QStringLiteral(
                "Duplicate skipped: select an existing %1 row")
                .arg(fieldFocused
                         ? QStringLiteral("Field")
                         : QStringLiteral("Register")),
            5000);
        return;
    }
    if (!commitActiveEditor()) {
        return;
    }

    if (fieldFocused) {
        if (controller_.workspace() == nullptr ||
            regmap::findField(*controller_.workspace(),
                              objectId) == nullptr) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Duplicate skipped: the selected Field no longer exists"),
                5000);
            return;
        }
        selectedFieldId_ = objectId;
        duplicateSelectedField();
        return;
    }

    if (controller_.workspace() == nullptr ||
        regmap::findRegister(*controller_.workspace(),
                             objectId) == nullptr) {
        statusBar()->showMessage(
            QStringLiteral(
                "Duplicate skipped: the selected Register no longer exists"),
            5000);
        return;
    }
    selectedRegisterId_ = objectId;
    duplicateSelectedRegister();
}

void MainWindow::duplicateSelectedRegister()
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || selectedRegisterId_.empty()) {
        return;
    }

    const regmap::RegisterBlock* parent = nullptr;
    const regmap::Register* source = nullptr;
    std::string addressId;
    for (const auto& space : workspace->addressSpaces) {
        for (const auto& block : space.blocks) {
            const auto match = std::ranges::find(
                block.registers, selectedRegisterId_,
                &regmap::Register::id);
            if (match != block.registers.end()) {
                parent = &block;
                source = &*match;
                addressId = space.id;
                break;
            }
        }
        if (source != nullptr) {
            break;
        }
    }
    if (parent == nullptr || source == nullptr) {
        statusBar()->showMessage(
            QStringLiteral(
                "The selected Register no longer exists; reopen the menu and try again"),
            5000);
        return;
    }

    constexpr std::uint64_t registerAlignment = 4;
    std::uint64_t appendOffset = 0;
    for (const auto& existing : parent->registers) {
        const auto extent = checkedRegisterExtent(existing);
        std::uint64_t end = 0;
        if (!extent || addOverflow(existing.offset, *extent, end)) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot duplicate Register %1: Register %2 has an invalid "
                    "or overflowing extent. Repair its Offset, Width, or Array first.")
                    .arg(fromUtf8(source->name),
                         fromUtf8(existing.name)),
                9000);
            return;
        }
        appendOffset = std::max(appendOffset, end);
    }
    if (appendOffset >
        std::numeric_limits<std::uint64_t>::max() -
            (registerAlignment - 1)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot duplicate Register %1: no aligned Offset remains.")
                .arg(fromUtf8(source->name)),
            8000);
        return;
    }

    regmap::Register copy = *source;
    copy.name = uniqueCopiedName(
        copy.name, [&](const std::string& candidate) {
            return std::ranges::any_of(
                parent->registers,
                [&](const regmap::Register& reg) {
                    return reg.name == candidate;
                });
        });
    copy.offset =
        (appendOffset + (registerAlignment - 1)) &
        ~(registerAlignment - 1);
    std::set<regmap::ObjectId, std::less<>> generatedIds;
    prepareCopiedRegister(*workspace, copy, generatedIds);

    regmap::Workspace candidate = *workspace;
    if (auto* candidateBlock =
            regmap::findRegisterBlock(candidate, parent->id)) {
        candidateBlock->registers.push_back(copy);
    }
    if (!addressEditDoesNotWorsen(*workspace, candidate, parent->id)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot duplicate Register %1 at Offset %2: the copy does not "
                "fit Block %3 or the Page address range. Increase Block Size "
                "or Address Width, or adjust existing Offsets.")
                .arg(fromUtf8(source->name), hex(copy.offset),
                     fromUtf8(parent->name)),
            9000);
        return;
    }

    const std::string blockId = parent->id;
    const std::string newId = copy.id;
    const QString sourceName = fromUtf8(source->name);
    const QString copyName = fromUtf8(copy.name);
    const std::uint64_t copyOffset = copy.offset;
    if (controller_.editWorkspace(
            QStringLiteral("Duplicate register %1").arg(sourceName),
            [blockId, copy = std::move(copy)](
                regmap::Workspace& target) mutable {
                if (auto* block =
                        regmap::findRegisterBlock(target, blockId)) {
                    block->registers.push_back(std::move(copy));
                }
            })) {
        selectedAddressId_ = addressId;
        selectedBlockId_ = blockId;
        selectedRegisterId_ = newId;
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        selectRegister(newId);
        statusBar()->showMessage(
            QStringLiteral(
                "Duplicated Register %1 as %2 at Offset %3; Ctrl+Z to restore")
                .arg(sourceName, copyName, hex(copyOffset)),
            7000);
    }
}

void MainWindow::showRegisterContextMenu(const QPoint& position)
{
    const QModelIndex index = registerView_->indexAt(position);
    if (!index.isValid()) {
        return;
    }
    if (index.data(addRowRole).toBool()) {
        addRegister();
        return;
    }
    registerView_->setCurrentIndex(registerModel_->index(index.row(), registerNameColumn));
    const auto* reg = findRegister(selectedRegisterId_);
    if (reg == nullptr) {
        return;
    }

    QMenu menu(this);
    QAction* duplicate =
        menu.addAction(QStringLiteral("Duplicate Register"));
    duplicate->setObjectName(QStringLiteral("duplicateRegisterAction"));
    duplicate->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+D")));
    duplicate->setShortcutContext(Qt::WidgetShortcut);
    QAction* editTags = menu.addAction(QStringLiteral("编辑标签…"));
    menu.addSeparator();
    QAction* reserve = menu.addAction(QStringLiteral("设为 Reserved（保留偏移）"));
    reserve->setObjectName(QStringLiteral("reserveRegisterAction"));
    QFont reserveFont = reserve->font();
    reserveFont.setWeight(QFont::DemiBold);
    reserve->setFont(reserveFont);
    QPixmap reserveIcon(10, 10);
    reserveIcon.fill(QColor(QStringLiteral("#B3261E")));
    reserve->setIcon(QIcon(reserveIcon));
    reserve->setEnabled(!reg->reserved);
    QAction* removeAndShift = menu.addAction(QStringLiteral("删除（后续寄存器顺延）"));
    QAction* chosen = menu.exec(registerView_->viewport()->mapToGlobal(position));
    if (chosen == editTags) {
        editRegisterTags(registerModel_->index(index.row(), registerTagsColumn));
    } else if (chosen == duplicate) {
        duplicateSelectedRegister();
    } else if (chosen == reserve) {
        convertSelectedRegisterToReserved();
    } else if (chosen == removeAndShift) {
        deleteSelectedRegisterAndShift();
    }
}

void MainWindow::duplicateSelectedField()
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || selectedFieldId_.empty()) {
        return;
    }

    const auto* reg =
        findRegisterContainingField(*workspace, selectedFieldId_);
    const auto* source =
        reg == nullptr
            ? nullptr
            : findFieldRecursive(reg->fields, selectedFieldId_);
    const auto* siblings =
        reg == nullptr
            ? nullptr
            : fieldSiblings(reg->fields, selectedFieldId_);
    const auto* parent =
        reg == nullptr
            ? nullptr
            : parentFieldOf(reg->fields, selectedFieldId_);
    if (reg == nullptr || source == nullptr || siblings == nullptr) {
        statusBar()->showMessage(
            QStringLiteral(
                "The selected Field no longer exists; reopen the menu and try again"),
            5000);
        return;
    }

    const std::uint64_t containerWidth =
        parent == nullptr ? reg->width : parent->width();
    const auto targetLsb =
        availableFieldLsb(*siblings, source->width(), containerWidth);
    const QString containerLabel =
        parent == nullptr
            ? QStringLiteral("Register %1").arg(fromUtf8(reg->name))
            : QStringLiteral("compound Field %1")
                  .arg(fromUtf8(parent->name));
    if (!targetLsb) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot duplicate Field %1: no contiguous %2-bit free range "
                "remains in %3. Move or narrow Fields, or increase the "
                "containing width; no change was made.")
                .arg(fromUtf8(source->name))
                .arg(source->width())
                .arg(containerLabel),
            9000);
        return;
    }

    std::uint64_t containerAbsoluteLsb = 0;
    if (parent != nullptr) {
        const auto absolute =
            fieldAbsoluteLsb(reg->fields, parent->id);
        if (!absolute) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot duplicate Field %1: its compound Field position "
                    "is invalid; repair the Field layout first.")
                    .arg(fromUtf8(source->name)),
                8000);
            return;
        }
        containerAbsoluteLsb = *absolute;
    }

    regmap::Field copy = *source;
    copy.name = uniqueCopiedName(
        copy.name, [&](const std::string& candidate) {
            return std::ranges::any_of(
                *siblings, [&](const regmap::Field& field) {
                    return field.name == candidate;
                });
        });
    copy.lsb = *targetLsb;
    copy.msb = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(*targetLsb) +
        source->width() - 1);
    std::set<regmap::ObjectId, std::less<>> generatedIds;
    prepareCopiedField(*workspace, copy, generatedIds);
    refreshFieldResets(copy, reg->resetValue, containerAbsoluteLsb);
    const bool resetAdapted = copy.resetValue != source->resetValue;

    const std::string registerId = reg->id;
    const std::string parentId =
        parent == nullptr ? std::string{} : parent->id;
    regmap::Workspace candidate = *workspace;
    if (parentId.empty()) {
        if (auto* candidateRegister =
                regmap::findRegister(candidate, registerId)) {
            candidateRegister->fields.push_back(copy);
        }
    } else if (auto* candidateParent =
                   regmap::findField(candidate, parentId)) {
        candidateParent->members.push_back(copy);
    }
    if (!workspaceEditDoesNotWorsen(*workspace, candidate)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot duplicate Field %1 at bits %2:%3: its access, Reset, "
                "Enum values, Range, or Members would be invalid there; no "
                "change was made.")
                .arg(fromUtf8(source->name))
                .arg(copy.msb)
                .arg(copy.lsb),
            9000);
        return;
    }

    const std::string newId = copy.id;
    const QString sourceName = fromUtf8(source->name);
    const QString copyName = fromUtf8(copy.name);
    const std::uint32_t copyMsb = copy.msb;
    const std::uint32_t copyLsb = copy.lsb;
    if (controller_.editWorkspace(
            QStringLiteral("Duplicate field %1").arg(sourceName),
            [registerId, parentId, copy = std::move(copy)](
                regmap::Workspace& target) mutable {
                if (parentId.empty()) {
                    if (auto* targetRegister =
                            regmap::findRegister(target, registerId)) {
                        targetRegister->fields.push_back(
                            std::move(copy));
                    }
                } else if (auto* targetParent =
                               regmap::findField(target, parentId)) {
                    targetParent->members.push_back(
                        std::move(copy));
                }
            })) {
        selectedRegisterId_ = registerId;
        openFieldsRegisterId_ = registerId;
        selectedFieldId_ = newId;
        refreshProject();
        selectRegister(registerId);
        selectField(newId);
        statusBar()->showMessage(
            QStringLiteral(
                "Duplicated Field %1 as %2 at bits %3:%4%5; Ctrl+Z to restore")
                .arg(sourceName, copyName)
                .arg(copyMsb)
                .arg(copyLsb)
                .arg(resetAdapted
                         ? QStringLiteral(
                               "; Reset adapted to destination bits")
                         : QString{}),
            8000);
    }
}

void MainWindow::showFieldContextMenu(const QPoint& position)
{
    const QModelIndex index = fieldView_->indexAt(position);
    if (!index.isValid()) {
        return;
    }
    if (index.data(addRowRole).toBool()) {
        addField();
        return;
    }
    fieldView_->setCurrentIndex(fieldModel_->index(index.row(), fieldNameColumn));
    const auto* workspace = controller_.workspace();
    const auto* field =
        workspace == nullptr ? nullptr : regmap::findField(*workspace, selectedFieldId_);
    if (field == nullptr) {
        return;
    }

    QMenu menu(this);
    QAction* duplicate =
        menu.addAction(QStringLiteral("Duplicate Field"));
    duplicate->setObjectName(
        QStringLiteral("duplicateFieldAction"));
    duplicate->setShortcut(
        QKeySequence(QStringLiteral("Ctrl+D")));
    duplicate->setShortcutContext(Qt::WidgetShortcut);
    menu.addSeparator();
    QMenu* typeMenu = menu.addMenu(QStringLiteral("设置类型"));
    const std::vector<QString> types{
        QStringLiteral("bits"),    QStringLiteral("bool"),   QStringLiteral("enum"),
        QStringLiteral("uint8"),   QStringLiteral("uint16"), QStringLiteral("uint32"),
        QStringLiteral("uint64"),  QStringLiteral("int8"),   QStringLiteral("int16"),
        QStringLiteral("int32"),   QStringLiteral("int64"),  QStringLiteral("field"),
        QStringLiteral("reserved")};
    for (const auto& type : types) {
        QAction* action = typeMenu->addAction(type);
        action->setData(type);
    }
    QAction* addEnum = menu.addAction(QStringLiteral("添加枚举值"));
    QAction* addMember = menu.addAction(QStringLiteral("添加内部 Field"));
    addMember->setObjectName(QStringLiteral("addMemberFieldAction"));
    addMember->setEnabled(field->type == regmap::FieldType::structure);
    menu.addSeparator();
    QAction* remove = menu.addAction(QStringLiteral("删除 Field"));
    QAction* chosen = menu.exec(fieldView_->viewport()->mapToGlobal(position));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == duplicate) {
        duplicateSelectedField();
    } else if (chosen->data().isValid()) {
        applyPropertyEdit(selectedFieldId_, "type", chosen->data().toString());
    } else if (chosen == addEnum) {
        addEnumValue();
    } else if (chosen == addMember) {
        addSubfield();
    } else if (chosen == remove) {
        deleteSelection();
    }
}

void MainWindow::convertSelectedRegisterToReserved()
{
    const auto* reg = findRegister(selectedRegisterId_);
    if (reg == nullptr || reg->reserved) {
        return;
    }
    const std::string registerId = reg->id;
    const std::string reservedName =
        (QStringLiteral("RESERVED_%1").arg(hex(reg->offset).mid(2).toUpper()))
            .toUtf8()
            .toStdString();
    const std::size_t fieldCount = fieldTreeSize(reg->fields);
    const std::size_t enumValueCount = reg->enumValues.size();
    const std::size_t rangeBoundCount =
        static_cast<std::size_t>(reg->minimumValue.has_value()) +
        reg->maximumValue.has_value();
    const std::size_t nonZeroValueCount =
        static_cast<std::size_t>(
            reg->initialValue.has_value() && !reg->initialValue->isZero()) +
        static_cast<std::size_t>(
            reg->resetValue.has_value() && !reg->resetValue->isZero());
    const bool replacesDescription =
        !reg->description.empty() &&
        reg->description != "Reserved address slot.";
    QStringList impacts;
    if (fieldCount != 0) {
        impacts << (
            fieldCount == 1
                ? QStringLiteral("1 Field and its definition")
                : QStringLiteral("%1 Fields/Members and their definitions")
                      .arg(fieldCount));
    }
    if (enumValueCount != 0) {
        impacts << (
            enumValueCount == 1
                ? QStringLiteral("1 Register Enum value")
                : QStringLiteral("%1 Register Enum values").arg(enumValueCount));
    }
    if (rangeBoundCount != 0) {
        impacts << (
            rangeBoundCount == 1
                ? QStringLiteral("1 Range bound")
                : QStringLiteral("%1 Range bounds").arg(rangeBoundCount));
    }
    if (nonZeroValueCount != 0) {
        impacts << (
            nonZeroValueCount == 1
                ? QStringLiteral("1 non-zero Initial/Reset value")
                : QStringLiteral("%1 non-zero Initial/Reset values")
                      .arg(nonZeroValueCount));
    }
    if (replacesDescription) {
        impacts << QStringLiteral("the Register description");
    }
    if (!impacts.empty()) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Set Register to Reserved"),
            QStringLiteral(
                "Set Register %1 at offset %2 to Reserved?\n\n"
                "The offset and tags will be kept. The name changes to %3; "
                "Type, Access, Initial, Reset, and Description use Reserved defaults.\n\n"
                "This removes or replaces %4.\n\n"
                "Ctrl+Z can restore the complete Register definition.")
                .arg(fromUtf8(reg->name), hex(reg->offset),
                     fromUtf8(reservedName), impacts.join(QStringLiteral(", "))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            statusBar()->showMessage(
                QStringLiteral("Reserved conversion cancelled · %1 kept")
                    .arg(fromUtf8(reg->name)),
                6000);
            return;
        }
    }
    if (controller_.editWorkspace(QStringLiteral("Reserve register address"),
                                  [registerId, reservedName](regmap::Workspace& candidate) {
                                      if (auto* target =
                                              regmap::findRegister(candidate, registerId)) {
                                          target->name = reservedName;
                                          target->reserved = true;
                                          target->type = regmap::FieldType::reserved;
                                          target->access = regmap::AccessMode::none;
                                          target->minimumValue.reset();
                                          target->maximumValue.reset();
                                          target->enumValues.clear();
                                          target->initialValue = regmap::UnsignedValue(0);
                                          target->resetValue = regmap::UnsignedValue(0);
                                          target->fields.clear();
                                          target->description = "Reserved address slot.";
                                      }
                                  })) {
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        selectedRegisterId_ = registerId;
        refreshProject();
        selectRegister(registerId);
        const QString impactFeedback =
            impacts.empty()
                ? QString{}
                : QStringLiteral(" · %1 removed or normalized")
                      .arg(impacts.join(QStringLiteral(", ")));
        statusBar()->showMessage(
            QStringLiteral("Register set to Reserved%1 · Ctrl+Z to restore")
                .arg(impactFeedback),
            6000);
    }
}

void MainWindow::deleteSelectedRegisterAndShift()
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || selectedRegisterId_.empty()) {
        return;
    }
    std::string blockId;
    std::string nextId;
    std::string registerName;
    std::uint64_t registerOffset = 0;
    std::uint64_t shift = 0;
    std::uint64_t firstFollowingOffset = 0;
    std::uint64_t lastFollowingOffset = 0;
    std::size_t affectedCount = 0;
    std::string underflowRegisterName;
    RegisterDeletionImpact contentImpact;
    std::uint64_t underflowOffset = 0;
    const std::string registerId = selectedRegisterId_;
    for (const auto& space : workspace->addressSpaces) {
        for (const auto& block : space.blocks) {
            const auto iterator =
                std::ranges::find(block.registers, selectedRegisterId_, &regmap::Register::id);
            if (iterator == block.registers.end()) {
                continue;
            }
            blockId = block.id;
            registerName = iterator->name;
            registerOffset = iterator->offset;
            accumulateRegisterDeletionImpact(*iterator, contentImpact);
            const auto extent = checkedRegisterExtent(*iterator);
            if (!extent) {
                statusBar()->showMessage(
                    QStringLiteral(
                        "Cannot delete and shift %1: its Register extent is invalid")
                        .arg(fromUtf8(registerName)),
                    7000);
                return;
            }
            shift = *extent;
            const auto index =
                static_cast<std::size_t>(std::distance(block.registers.begin(), iterator));
            affectedCount = block.registers.size() - index - 1;
            if (affectedCount > 0) {
                nextId = block.registers[index + 1].id;
                firstFollowingOffset = block.registers[index + 1].offset;
                lastFollowingOffset = block.registers.back().offset;
                const auto underflow = std::find_if(
                    std::next(iterator), block.registers.end(),
                    [shift](const regmap::Register& following) {
                        return following.offset < shift;
                    });
                if (underflow != block.registers.end()) {
                    underflowRegisterName = underflow->name;
                    underflowOffset = underflow->offset;
                }
            } else if (index > 0) {
                nextId = block.registers[index - 1].id;
            }
            break;
        }
        if (!blockId.empty()) {
            break;
        }
    }
    if (blockId.empty()) {
        return;
    }
    if (!underflowRegisterName.empty()) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot delete and shift %1: following Register %2 at Offset %3 "
                "would underflow when shifted by %4. Reorder the rows or edit "
                "Offsets first.")
                .arg(fromUtf8(registerName),
                     fromUtf8(underflowRegisterName), hex(underflowOffset),
                     hex(shift)),
            9000);
        return;
    }

    const regmap::WorkspaceStore::Mutation deleteAndShift =
        [blockId, registerId](regmap::Workspace& candidate) {
            auto* block =
                regmap::findRegisterBlock(candidate, blockId);
            if (block == nullptr) {
                return;
            }
            const auto iterator =
                std::ranges::find(
                    block->registers, registerId,
                    &regmap::Register::id);
            if (iterator == block->registers.end()) {
                return;
            }
            const auto extent = checkedRegisterExtent(*iterator);
            if (!extent ||
                std::ranges::any_of(
                    std::next(iterator), block->registers.end(),
                    [extent](const regmap::Register& following) {
                        return following.offset < *extent;
                    })) {
                return;
            }
            const auto firstFollowing = block->registers.erase(iterator);
            for (auto current = firstFollowing;
                 current != block->registers.end(); ++current) {
                current->offset -= *extent;
            }
        };
    regmap::Workspace candidate = *workspace;
    deleteAndShift(candidate);
    if (regmap::findRegister(candidate, registerId) != nullptr ||
        !addressEditDoesNotWorsen(*workspace, candidate, blockId)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot delete and shift %1: the resulting Offsets would "
                "create an invalid Block/Page address layout. Reorder the rows "
                "or edit Offsets first.")
                .arg(fromUtf8(registerName)),
            9000);
        return;
    }

    QString shiftImpact = QStringLiteral("No following register will move.");
    if (affectedCount > 0) {
        shiftImpact =
            QStringLiteral("%1 following register(s) will shift by %2:\n%3 … %4  →  %5 … %6")
                .arg(affectedCount)
                .arg(hex(shift), hex(firstFollowingOffset), hex(lastFollowingOffset),
                     hex(firstFollowingOffset - shift),
                     hex(lastFollowingOffset - shift));
    }
    const QStringList removedContent = configuredDeletionItems(contentImpact);
    const QString contentSummary =
        removedContent.empty()
            ? QStringLiteral(
                  "The Register has no Fields, Enum values, Range bounds, tags, "
                  "non-zero Initial/Reset values, or descriptions.")
            : QStringLiteral("Deleting it also removes: %1.")
                  .arg(removedContent.join(QStringLiteral(", ")));
    const QString prompt =
        QStringLiteral(
            "Delete %1 at offset %2?\n\n%3\n\n%4\n\n"
            "This can be restored with Ctrl+Z.")
            .arg(fromUtf8(registerName), hex(registerOffset), contentSummary,
                 shiftImpact);
    if (QMessageBox::warning(this, QStringLiteral("Delete and Shift Registers"), prompt,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        statusBar()->showMessage(
            QStringLiteral("Delete cancelled · %1 kept")
                .arg(fromUtf8(registerName)),
            6000);
        return;
    }

    if (controller_.editWorkspace(
            QStringLiteral("Delete register and shift following offsets"),
            deleteAndShift)) {
        selectedRegisterId_ = nextId;
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        if (!nextId.empty()) {
            selectRegister(nextId);
        }
        statusBar()->showMessage(
            QStringLiteral("Deleted %1 · shifted %2 register(s) · Ctrl+Z to restore")
                .arg(fromUtf8(registerName))
                .arg(affectedCount),
            6000);
    }
}

void MainWindow::moveField(const std::string& fieldId, std::uint32_t lsb, std::uint32_t msb)
{
    const auto* workspace = controller_.workspace();
    const auto* reg =
        workspace == nullptr ? nullptr : findRegister(selectedRegisterId_);
    if (reg == nullptr || msb < lsb || msb >= reg->width) {
        statusBar()->showMessage(QStringLiteral("Field move is outside the register width"), 5000);
        return;
    }
    const auto moving = std::ranges::find(reg->fields, fieldId, &regmap::Field::id);
    if (moving == reg->fields.end()) {
        statusBar()->showMessage(QStringLiteral("Only top-level fields can be dragged"), 5000);
        return;
    }
    if (moving->lsb == lsb && moving->msb == msb) {
        return;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> obstacles;
    QStringList fullyCoveredFields;
    for (const auto& field : reg->fields) {
        if (field.id != fieldId && field.msb >= field.lsb && lsb <= field.msb && field.lsb <= msb) {
            obstacles.emplace_back(field.lsb, field.msb);
            if (lsb <= field.lsb && msb >= field.msb) {
                fullyCoveredFields.push_back(fromUtf8(field.name));
            }
        }
    }

    enum class Resolution { moveOnly, trimMoving, trimOthers, cancel };
    Resolution resolution = obstacles.empty() ? Resolution::moveOnly : Resolution::cancel;
    if (!obstacles.empty()) {
        QMessageBox dialog(this);
        dialog.setWindowTitle(QStringLiteral("Resolve field overlap"));
        dialog.setIcon(QMessageBox::Warning);
        dialog.setText(QStringLiteral("The moved field overlaps one or more fields. Choose "
                                      "which side may adapt its width."));
        auto* trimMoving =
            dialog.addButton(QStringLiteral("Trim moving field"), QMessageBox::AcceptRole);
        auto* trimOthers = dialog.addButton(
            fullyCoveredFields.empty()
                ? QStringLiteral("Trim overlapping fields")
                : QStringLiteral("Trim/delete overlapping fields"),
            QMessageBox::DestructiveRole);
        if (!fullyCoveredFields.empty()) {
            dialog.setInformativeText(
                QStringLiteral(
                    "Choosing the destructive option will delete %1 fully covered "
                    "Field(s): %2. Partially covered Fields will be narrowed. "
                    "Ctrl+Z restores the entire operation.")
                    .arg(fullyCoveredFields.size())
                    .arg(fullyCoveredFields.join(QStringLiteral(", "))));
        }
        auto* cancel = dialog.addButton(QMessageBox::Cancel);
        dialog.setDefaultButton(cancel);
        dialog.setEscapeButton(cancel);
        dialog.exec();
        if (dialog.clickedButton() == trimMoving) {
            resolution = Resolution::trimMoving;
        } else if (dialog.clickedButton() == trimOthers) {
            resolution = Resolution::trimOthers;
        }
    }
    if (resolution == Resolution::cancel) {
        statusBar()->showMessage(QStringLiteral("Field move cancelled"), 3000);
        return;
    }

    if (resolution == Resolution::trimMoving) {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> segments{{lsb, msb}};
        std::ranges::sort(obstacles);
        for (const auto& [obstacleLsb, obstacleMsb] : obstacles) {
            std::vector<std::pair<std::uint32_t, std::uint32_t>> next;
            for (const auto& [segmentLsb, segmentMsb] : segments) {
                if (obstacleMsb < segmentLsb || obstacleLsb > segmentMsb) {
                    next.emplace_back(segmentLsb, segmentMsb);
                    continue;
                }
                if (obstacleLsb > segmentLsb) {
                    next.emplace_back(segmentLsb, obstacleLsb - 1);
                }
                if (obstacleMsb < segmentMsb) {
                    next.emplace_back(obstacleMsb + 1, segmentMsb);
                }
            }
            segments = std::move(next);
        }
        if (segments.empty()) {
            statusBar()->showMessage(QStringLiteral("The moving field has no non-overlapping bits"),
                                     5000);
            return;
        }
        const auto best =
            std::ranges::max_element(segments, [](const auto& left, const auto& right) {
                return (left.second - left.first) < (right.second - right.first);
            });
        lsb = best->first;
        msb = best->second;
    }

    const std::string registerId = reg->id;
    const regmap::WorkspaceStore::Mutation move =
        [registerId, fieldId, lsb, msb, resolution](
            regmap::Workspace& candidate) {
        auto* target = regmap::findRegister(candidate, registerId);
        if (target == nullptr) {
            return;
        }
        if (resolution == Resolution::trimOthers) {
            std::erase_if(target->fields, [&](regmap::Field& field) {
                if (field.id == fieldId || field.msb < field.lsb || msb < field.lsb ||
                    lsb > field.msb) {
                        return false;
                }
                const bool hasLeft = field.lsb < lsb;
                const bool hasRight = field.msb > msb;
                if (!hasLeft && !hasRight) {
                    return true;
                }
                const std::uint32_t leftWidth = hasLeft ? lsb - field.lsb : 0;
                const std::uint32_t rightWidth = hasRight ? field.msb - msb : 0;
                if (leftWidth >= rightWidth) {
                    field.msb = lsb - 1;
                } else {
                    field.lsb = msb + 1;
                }
                return false;
            });
        }
        if (auto* field = regmap::findField(candidate, fieldId)) {
            field->lsb = lsb;
            field->msb = msb;
        }
        refreshRegisterFieldResets(*target);
    };
    regmap::Workspace candidate = *workspace;
    move(candidate);
    if (!workspaceEditDoesNotWorsen(*workspace, candidate)) {
        statusBar()->showMessage(
            QStringLiteral(
                "Cannot move Field %1: trimming would invalidate an existing "
                "Reset, Enum value, numeric Range, access rule, or Member Field. "
                "Edit the affected values or choose another placement; no change "
                "was made.")
                .arg(fromUtf8(moving->name)),
            10000);
        return;
    }

    if (controller_.editWorkspace(QStringLiteral("Move field"), move)) {
        selectedFieldId_ = fieldId;
        refreshProject();
        selectRegister(registerId);
        selectField(fieldId);
        statusBar()->showMessage(
            QStringLiteral(
                "Field moved: MSB %1 · LSB %2 · Ctrl+Z to restore")
                .arg(msb).arg(lsb), 5000);
    }
}

void MainWindow::deleteSelection()
{
    const QWidget* focus = QApplication::focusWidget();
    if (qobject_cast<const QLineEdit*>(focus) != nullptr ||
        qobject_cast<const QComboBox*>(focus) != nullptr) {
        return;
    }
    std::string id;
    const bool deletingEnumValue =
        focusBelongsToWidgetOutsidePopup(focus, enumView_);
    if (deletingEnumValue) {
        id = enumView_->currentIndex().data(objectIdRole).toString().toUtf8().toStdString();
    } else if (
        focusBelongsToWidgetOutsidePopup(focus, fieldView_) ||
        focusBelongsToWidgetOutsidePopup(focus, bitfieldView_)) {
        id = selectedFieldId_;
    } else if (
        focusBelongsToWidgetOutsidePopup(focus, registerView_)) {
        if (!selectedRegisterId_.empty()) {
            deleteSelectedRegisterAndShift();
        }
        return;
    } else if (
        focusBelongsToWidgetOutsidePopup(focus, hierarchyView_)) {
        id = !selectedBlockId_.empty() ? selectedBlockId_ : selectedAddressId_;
    } else {
        statusBar()->showMessage(
            QStringLiteral("Delete applies only to the focused hierarchy or editor"), 4000);
        return;
    }
    if (id.empty()) {
        return;
    }
    deleteObject(id, deletingEnumValue);
}

void MainWindow::deleteObject(const std::string& id, bool deletingEnumValue)
{
    if (id.empty()) {
        return;
    }
    QString label = fromUtf8(id);
    QString compositeImpact;
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr) {
        return;
    }
    bool deletingPage = false;
    bool deletingBlock = false;
    bool deletingRegister = false;
    bool deletingField = false;
    if (const auto* page = regmap::findAddressSpace(*workspace, id)) {
        deletingPage = true;
        RegisterDeletionImpact impact;
        impact.descriptionCount +=
            static_cast<std::size_t>(!page->description.empty());
        std::size_t registerCount = 0;
        for (const auto& block : page->blocks) {
            impact.descriptionCount +=
                static_cast<std::size_t>(!block.description.empty());
            registerCount += block.registers.size();
            for (const auto& reg : block.registers) {
                accumulateRegisterDeletionImpact(reg, impact);
            }
        }
        label = QStringLiteral("page %1").arg(fromUtf8(page->name));
        QStringList removedContent;
        appendDeletionCount(removedContent, page->blocks.size(),
                            QStringLiteral("Block"), QStringLiteral("Blocks"));
        appendDeletionCount(removedContent, registerCount,
                            QStringLiteral("Register"),
                            QStringLiteral("Registers"));
        removedContent.append(configuredDeletionItems(impact));
        compositeImpact =
            removedContent.empty()
                ? QStringLiteral("This Page contains no Blocks or Registers.")
                : QStringLiteral("This removes: %1.")
                      .arg(removedContent.join(QStringLiteral(", ")));
    } else if (const auto* block = regmap::findRegisterBlock(*workspace, id)) {
        deletingBlock = true;
        label = QStringLiteral("block %1").arg(fromUtf8(block->name));
        RegisterDeletionImpact impact;
        impact.descriptionCount +=
            static_cast<std::size_t>(!block->description.empty());
        for (const auto& reg : block->registers) {
            accumulateRegisterDeletionImpact(reg, impact);
        }
        QStringList removedContent;
        appendDeletionCount(removedContent, block->registers.size(),
                            QStringLiteral("Register"),
                            QStringLiteral("Registers"));
        removedContent.append(configuredDeletionItems(impact));
        compositeImpact =
            removedContent.empty()
                ? QStringLiteral("This Block contains no Registers.")
                : QStringLiteral("This removes: %1.")
                      .arg(removedContent.join(QStringLiteral(", ")));
    } else if (const auto* reg = regmap::findRegister(*workspace, id)) {
        deletingRegister = true;
        label = QStringLiteral("register %1").arg(fromUtf8(reg->name));
    } else if (const auto* field = regmap::findField(*workspace, id)) {
        deletingField = true;
        const auto* parent = parentFieldOf(*workspace, id);
        if (parent != nullptr && parent->type == regmap::FieldType::structure &&
            parent->members.size() == 1) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot delete the final Member of compound Field %1; "
                    "add another Member first (increase Width if needed), or delete "
                    "the compound Field")
                    .arg(fromUtf8(parent->name)),
                7000);
            return;
        }
        label = QStringLiteral("field %1").arg(fromUtf8(field->name));
        RegisterDeletionImpact impact;
        accumulateFieldContentsDeletionImpact(*field, impact);
        const QStringList removedContent = configuredDeletionItems(impact);
        if (!removedContent.empty()) {
            compositeImpact = QStringLiteral("This also removes: %1.")
                                  .arg(removedContent.join(QStringLiteral(", ")));
        }
    } else if (const auto* enumValue = regmap::findEnumValue(*workspace, id)) {
        deletingEnumValue = true;
        label = QStringLiteral("enum value %1").arg(fromUtf8(enumValue->name));
    } else {
        statusBar()->showMessage(
            QStringLiteral("The target no longer exists; reopen the menu and try again"), 5000);
        return;
    }
    if (!compositeImpact.isEmpty()) {
        const auto answer = QMessageBox::warning(
            this, QStringLiteral("Delete Register-Map Objects"),
            QStringLiteral("Delete %1?\n\n%2\n\nThis can be restored with Ctrl+Z.")
                .arg(label, compositeImpact),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            statusBar()->showMessage(
                QStringLiteral("Delete cancelled · %1 kept").arg(label),
                6000);
            return;
        }
    }

    if (controller_.editWorkspace(QStringLiteral("Delete %1").arg(label),
                                  [id](regmap::Workspace& candidate) {
                                      static_cast<void>(regmap::removeObject(candidate, id));
                                  })) {
        if (deletingEnumValue) {
            // Keep the current register/field context while its value list refreshes.
        } else if (deletingField && id == selectedFieldId_) {
            selectedFieldId_.clear();
        } else if (deletingRegister && id == selectedRegisterId_) {
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
        } else if (deletingBlock && id == selectedBlockId_) {
            selectedBlockId_.clear();
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
        } else if (deletingPage && id == selectedAddressId_) {
            selectedAddressId_.clear();
            selectedBlockId_.clear();
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
        }
        refreshProject();
        statusBar()->showMessage(QStringLiteral("Deleted %1 · Ctrl+Z to restore").arg(label), 5000);
    }
}

bool MainWindow::navigateToObject(const std::string& id)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || id.empty()) {
        return false;
    }

    if (workspace->id == id) {
        selectedAddressId_.clear();
        selectedBlockId_.clear();
        selectedRegisterId_.clear();
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        selectedTagFilter_.clear();
        refreshProject();
        statusBar()->showMessage(QStringLiteral("Located workspace"), 3000);
        return true;
    }

    for (const auto& page : workspace->addressSpaces) {
        if (page.id == id) {
            selectedAddressId_ = page.id;
            selectedBlockId_.clear();
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
            selectedTagFilter_.clear();
            refreshProject();
            statusBar()->showMessage(
                QStringLiteral("Located page %1").arg(fromUtf8(page.name)), 3000);
            return true;
        }
        for (const auto& block : page.blocks) {
            if (block.id == id) {
                selectedAddressId_ = page.id;
                selectedBlockId_ = block.id;
                selectedRegisterId_.clear();
                selectedFieldId_.clear();
                openFieldsRegisterId_.clear();
                selectedTagFilter_.clear();
                refreshProject();
                statusBar()->showMessage(
                    QStringLiteral("Located block %1").arg(fromUtf8(block.name)), 3000);
                return true;
            }
            for (const auto& reg : block.registers) {
                bool registerMatch = reg.id == id;
                std::string enumValueId;
                if (!registerMatch) {
                    const auto enumValue = std::ranges::find_if(
                        reg.enumValues, [&](const regmap::EnumValue& value) {
                            return value.id == id;
                        });
                    if (enumValue != reg.enumValues.end()) {
                        registerMatch = true;
                        enumValueId = enumValue->id;
                    }
                }
                std::string fieldId;
                const auto findFieldOwner =
                    [&](const auto& self, const std::vector<regmap::Field>& fields) -> bool {
                    for (const auto& field : fields) {
                        if (field.id == id) {
                            fieldId = field.id;
                            return true;
                        }
                        const auto enumValue = std::ranges::find_if(
                            field.enumValues, [&](const regmap::EnumValue& value) {
                                return value.id == id;
                            });
                        if (enumValue != field.enumValues.end()) {
                            fieldId = field.id;
                            enumValueId = enumValue->id;
                            return true;
                        }
                        if (self(self, field.members)) {
                            return true;
                        }
                    }
                    return false;
                };
                if (!registerMatch && !findFieldOwner(findFieldOwner, reg.fields)) {
                    continue;
                }

                selectedAddressId_ = page.id;
                selectedBlockId_ = block.id;
                selectedRegisterId_ = reg.id;
                const bool canOpenField =
                    !fieldId.empty() && !reg.reserved &&
                    reg.type == regmap::FieldType::structure;
                if (canOpenField) {
                    selectedFieldId_ = fieldId;
                    openFieldsRegisterId_ = reg.id;
                } else {
                    selectedFieldId_.clear();
                    openFieldsRegisterId_.clear();
                }
                selectedTagFilter_.clear();
                refreshProject();
                selectRegister(reg.id);
                if (canOpenField) {
                    selectField(fieldId);
                } else if (!fieldId.empty() && registerView_->currentIndex().isValid()) {
                    const QModelIndex typeIndex =
                        registerModel_->index(registerView_->currentIndex().row(),
                                              registerTypeColumn);
                    registerView_->setCurrentIndex(typeIndex);
                    registerView_->scrollTo(typeIndex);
                    registerView_->setFocus(Qt::OtherFocusReason);
                }
                const bool canOpenEnum =
                    !enumValueId.empty() && (fieldId.empty() || canOpenField);
                if (canOpenEnum) {
                    for (int row = 0; row < enumModel_->rowCount(); ++row) {
                        const QModelIndex enumIndex =
                            enumModel_->index(row, enumNameColumn);
                        if (enumIndex.data(objectIdRole).toString().toUtf8().toStdString() !=
                            enumValueId) {
                            continue;
                        }
                        enumView_->setCurrentIndex(enumIndex);
                        enumView_->scrollTo(enumIndex);
                        enumView_->setFocus(Qt::OtherFocusReason);
                        break;
                    }
                }
                QString located =
                    QStringLiteral("Located register %1").arg(fromUtf8(reg.name));
                if (!fieldId.empty()) {
                    if (const auto* field = findField(reg, fieldId)) {
                        located = QStringLiteral("Located field %1").arg(fromUtf8(field->name));
                    } else {
                        located = QStringLiteral("Located field");
                    }
                }
                if (canOpenEnum) {
                    if (const auto* enumValue =
                            regmap::findEnumValue(*workspace, enumValueId)) {
                        located = QStringLiteral("Located enum value %1")
                                      .arg(fromUtf8(enumValue->name));
                    }
                }
                if (!fieldId.empty() && !canOpenField) {
                    const auto* field = findField(reg, fieldId);
                    located =
                        QStringLiteral("Located register %1; set Type to field to edit %2")
                            .arg(fromUtf8(reg.name),
                                 field == nullptr ? QStringLiteral("this field")
                                                  : fromUtf8(field->name));
                }
                statusBar()->showMessage(located, fieldId.empty() || canOpenField ? 3000 : 6000);
                return true;
            }
        }
    }
    return false;
}

void MainWindow::rebuildSearchResults()
{
    searchResults_.clear();
    searchResultIndex_ = -1;
    searchQuery_ = globalSearchEdit_->text().trimmed();
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || searchQuery_.isEmpty()) {
        return;
    }

    std::set<std::string> seen;
    const auto addIfMatch = [&](const std::string& id, const QStringList& values) {
        if (seen.contains(id)) {
            return;
        }
        const bool exact = std::ranges::any_of(values, [&](const QString& value) {
            return value.compare(searchQuery_, Qt::CaseInsensitive) == 0;
        });
        const bool partial = exact || std::ranges::any_of(values, [&](const QString& value) {
            return value.contains(searchQuery_, Qt::CaseInsensitive);
        });
        if (partial) {
            seen.insert(id);
            if (exact) {
                searchResults_.insert(searchResults_.begin(), id);
            } else {
                searchResults_.push_back(id);
            }
        }
    };
    const auto addEnumValues = [&](const std::vector<regmap::EnumValue>& values) {
        for (const auto& value : values) {
            addIfMatch(value.id, {fromUtf8(value.name), fromUtf8(value.id),
                                  fromUtf8(value.value.toHexString()),
                                  fromUtf8(value.description)});
        }
    };

    addIfMatch(workspace->id, {fromUtf8(workspace->name), fromUtf8(workspace->id)});
    for (const auto& page : workspace->addressSpaces) {
        addIfMatch(page.id, {fromUtf8(page.name), fromUtf8(page.id), hex(page.baseAddress),
                             fromUtf8(page.description)});
        for (const auto& block : page.blocks) {
            addIfMatch(block.id, {fromUtf8(block.name), fromUtf8(block.id),
                                  hex(block.baseAddress), fromUtf8(block.description)});
            for (const auto& reg : block.registers) {
                std::uint64_t pageBlockAddress = 0;
                std::uint64_t absoluteAddress = 0;
                const bool addressOverflow =
                    addOverflow(page.baseAddress, block.baseAddress, pageBlockAddress) ||
                    addOverflow(pageBlockAddress, reg.offset, absoluteAddress);
                QStringList registerValues{
                    fromUtf8(reg.name), fromUtf8(reg.id), hex(reg.offset),
                    addressOverflow ? QStringLiteral("overflow") : hex(absoluteAddress),
                    registerTypeText(reg), accessText(reg.access), tagsText(reg.tags),
                    fromUtf8(reg.description)};
                addIfMatch(reg.id, registerValues);
                addEnumValues(reg.enumValues);

                const auto visitFields =
                    [&](const auto& self, const std::vector<regmap::Field>& fields,
                        const QString& parentPath) -> void {
                    for (const auto& field : fields) {
                        const QString path = parentPath.isEmpty()
                            ? fromUtf8(field.name)
                            : parentPath + QStringLiteral(".") + fromUtf8(field.name);
                        QStringList fieldValues{
                            fromUtf8(field.name), fromUtf8(field.id), path,
                            QString::number(field.msb), QString::number(field.lsb),
                            fieldTypeText(field), accessText(field.softwareAccess),
                            fromUtf8(field.description)};
                        addIfMatch(field.id, fieldValues);
                        addEnumValues(field.enumValues);
                        self(self, field.members, path);
                    }
                };
                visitFields(visitFields, reg.fields, {});
            }
        }
    }
}

void MainWindow::runSearch(bool reverse)
{
    if (globalSearchEdit_->text().trimmed().isEmpty()) {
        globalSearchEdit_->setFocus();
        searchResultLabel_->clear();
        return;
    }
    if (searchQuery_ != globalSearchEdit_->text().trimmed() || searchResults_.empty()) {
        rebuildSearchResults();
    }
    if (searchResults_.empty()) {
        searchResultLabel_->setText(QStringLiteral("0/0"));
        statusBar()->showMessage(QStringLiteral("No matching register-map object"), 3000);
        return;
    }

    const int count = static_cast<int>(searchResults_.size());
    if (reverse) {
        searchResultIndex_ = searchResultIndex_ <= 0 ? count - 1 : searchResultIndex_ - 1;
    } else {
        searchResultIndex_ = (searchResultIndex_ + 1) % count;
    }
    static_cast<void>(navigateToObject(
        searchResults_[static_cast<std::size_t>(searchResultIndex_)]));
    searchResultLabel_->setText(
        QStringLiteral("%1/%2").arg(searchResultIndex_ + 1).arg(count));
}

void MainWindow::copySelection()
{
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
        edit->copy();
        return;
    }
    if (focusUsesChoiceEditor(focus)) {
        return;
    }
    if (focusBelongsToWidgetOutsidePopup(focus, hierarchyView_)) {
        copyHierarchySelection();
        return;
    }
    QTableView* view = nullptr;
    for (QTableView* candidate :
         {registerView_, fieldView_, enumView_, problemsView_, generatedView_, diffView_}) {
        if (focusBelongsToWidgetOutsidePopup(focus, candidate)) {
            view = candidate;
            break;
        }
    }
    if (view == nullptr) {
        return;
    }
    QModelIndexList indexes = view->selectionModel()->selectedIndexes();
    if (indexes.empty()) {
        if (view->currentIndex().isValid()) {
            indexes.push_back(view->currentIndex());
        } else {
            statusBar()->showMessage(QStringLiteral("Copy skipped: select at least one cell"),
                                     4000);
            return;
        }
    }
    int firstRow = indexes.front().row();
    int lastRow = firstRow;
    int firstColumn = indexes.front().column();
    int lastColumn = firstColumn;
    for (const QModelIndex& index : indexes) {
        firstRow = std::min(firstRow, index.row());
        lastRow = std::max(lastRow, index.row());
        firstColumn = std::min(firstColumn, index.column());
        lastColumn = std::max(lastColumn, index.column());
    }
    const qsizetype selectedArea =
        static_cast<qsizetype>(lastRow - firstRow + 1) *
        static_cast<qsizetype>(lastColumn - firstColumn + 1);
    if (indexes.size() != selectedArea) {
        statusBar()->showMessage(
            QStringLiteral(
                "Copy skipped: select one cell or a contiguous rectangular range"),
            5000);
        return;
    }
    QStringList lines;
    for (int row = firstRow; row <= lastRow; ++row) {
        QStringList values;
        for (int column = firstColumn; column <= lastColumn; ++column) {
            const QModelIndex index = view->model()->index(row, column);
            values << index.data().toString();
        }
        lines << values.join('\t');
    }
    auto* mimeData = new QMimeData;
    mimeData->setText(lines.join('\n'));
    mimeData->setData(QString::fromLatin1(tableClipboardMimeType), QByteArrayLiteral("1"));
    QApplication::clipboard()->setMimeData(mimeData);
    statusBar()->showMessage(QStringLiteral("Copied %1 cell(s)").arg(indexes.size()), 2000);
}

void MainWindow::pasteSelection()
{
    QWidget* focus = QApplication::focusWidget();
    if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
        edit->paste();
        return;
    }
    if (focusUsesChoiceEditor(focus)) {
        return;
    }
    if (focusBelongsToWidgetOutsidePopup(focus, hierarchyView_)) {
        pasteHierarchySelection();
        return;
    }
    QTableView* view = nullptr;
    for (QTableView* candidate : {registerView_, fieldView_, enumView_}) {
        if (focusBelongsToWidgetOutsidePopup(focus, candidate)) {
            view = candidate;
            break;
        }
    }
    if (view == nullptr) {
        return;
    }
    QModelIndexList selected = view->selectionModel()->selectedIndexes();
    if (view->currentIndex().isValid() &&
        !view->selectionModel()->isSelected(view->currentIndex())) {
        selected.clear();
    }
    if (selected.empty() && !view->currentIndex().isValid()) {
        statusBar()->showMessage(QStringLiteral("Paste skipped: select at least one cell"),
                                 4000);
        return;
    }

    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    const bool copiedFromWorkbenchTable =
        mimeData != nullptr &&
        mimeData->hasFormat(QString::fromLatin1(tableClipboardMimeType));
    QString clipboard = mimeData == nullptr ? QString{} : mimeData->text();
    clipboard.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    clipboard.replace('\r', '\n');
    QStringList rows = clipboard.split('\n', Qt::KeepEmptyParts);
    if (!copiedFromWorkbenchTable && !rows.empty() && rows.back().isEmpty()) {
        rows.removeLast();
    }
    if (rows.empty()) {
        return;
    }

    struct PasteTarget {
        std::string objectId;
        std::string property;
        QString value;
        int row{0};
        int column{0};
        QString objectName;
        QString propertyName;
    };
    std::vector<PasteTarget> targets;
    std::size_t skipped = 0;
    const auto appendTarget = [&](const QModelIndex& target, const QString& text) {
        if (!target.isValid() || target.data(addRowRole).toBool()) {
            ++skipped;
            return;
        }
        const std::string objectId =
            target.data(objectIdRole).toString().toUtf8().toStdString();
        const std::string property =
            target.data(propertyRole).toString().toUtf8().toStdString();
        if (objectId.empty() || property.empty()) {
            ++skipped;
            return;
        }
        const bool duplicate =
            std::ranges::any_of(targets, [&](const PasteTarget& existing) {
                return existing.objectId == objectId && existing.property == property;
            });
        if (!duplicate) {
            const QString objectName =
                view->model()->index(target.row(), 0).data().toString();
            const QString propertyName =
                view->model()->headerData(target.column(), Qt::Horizontal).toString();
            targets.push_back(PasteTarget{objectId, property, text, target.row(),
                                          target.column(), objectName, propertyName});
        }
    };

    const QStringList firstCells = rows.front().split('\t', Qt::KeepEmptyParts);
    const bool scalarClipboard = rows.size() == 1 && firstCells.size() == 1;
    QModelIndex pasteStart =
        selected.empty() ? view->currentIndex() : selected.front();
    bool contiguousSelection = true;
    if (selected.size() > 1) {
        int firstRow = selected.front().row();
        int lastRow = firstRow;
        int firstColumn = selected.front().column();
        int lastColumn = firstColumn;
        for (const QModelIndex& index : selected) {
            firstRow = std::min(firstRow, index.row());
            lastRow = std::max(lastRow, index.row());
            firstColumn = std::min(firstColumn, index.column());
            lastColumn = std::max(lastColumn, index.column());
        }
        const qsizetype selectedArea =
            static_cast<qsizetype>(lastRow - firstRow + 1) *
            static_cast<qsizetype>(lastColumn - firstColumn + 1);
        contiguousSelection = selected.size() == selectedArea;
        if (contiguousSelection) {
            pasteStart = view->model()->index(firstRow, firstColumn);
        }
    }
    if (!scalarClipboard && selected.size() > 1 && !contiguousSelection) {
        statusBar()->showMessage(
            QStringLiteral(
                "Paste skipped: multi-cell data requires one cell or a contiguous "
                "rectangular target"),
            5000);
        return;
    }
    if (scalarClipboard && selected.size() > 1) {
        std::ranges::sort(selected, [](const QModelIndex& left, const QModelIndex& right) {
            return left.row() == right.row() ? left.column() < right.column()
                                            : left.row() < right.row();
        });
        for (const QModelIndex& target : selected) {
            appendTarget(target, firstCells.front());
        }
    } else {
        for (int rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
            const QStringList cells = rows[rowOffset].split('\t', Qt::KeepEmptyParts);
            for (int columnOffset = 0; columnOffset < cells.size(); ++columnOffset) {
                appendTarget(
                    view->model()->index(pasteStart.row() + rowOffset,
                                         pasteStart.column() + columnOffset),
                    cells[columnOffset]);
            }
        }
    }

    if (targets.empty()) {
        statusBar()->showMessage(
            QStringLiteral(
                "Paste skipped: %1 skipped cell(s) (read-only, action, add row, or outside table)")
                .arg(skipped), 5000);
        return;
    }

    const QModelIndex previousCurrent = view->currentIndex();
    QModelIndex restoredCurrent;
    view->selectionModel()->clearSelection();
    for (const auto& target : targets) {
        const QModelIndex index = view->model()->index(target.row, target.column);
        view->selectionModel()->select(index, QItemSelectionModel::Select);
        if (target.row == previousCurrent.row() &&
            target.column == previousCurrent.column()) {
            restoredCurrent = index;
        }
    }
    if (!restoredCurrent.isValid()) {
        restoredCurrent = view->model()->index(targets.front().row, targets.front().column);
    }
    view->selectionModel()->setCurrentIndex(
        restoredCurrent, QItemSelectionModel::NoUpdate);

    const std::size_t undoDepth = controller_.undoDepth();
    std::size_t changed = 0;
    std::size_t unchanged = 0;
    std::size_t rejected = 0;
    QString firstRejected;
    QModelIndex firstRejectedIndex;
    {
        const QScopedValueRollback editGuard(modelEditInProgress_, true);
        for (const auto& target : targets) {
            const PropertyEditResult result =
                applyPropertyEdit(target.objectId, target.property, target.value, false);
            switch (result.status) {
            case PropertyEditStatus::changed:
                ++changed;
                break;
            case PropertyEditStatus::unchanged:
                ++unchanged;
                break;
            case PropertyEditStatus::rejected:
                ++rejected;
                if (!firstRejectedIndex.isValid()) {
                    firstRejectedIndex =
                        view->model()->index(target.row, target.column);
                    const QString displayValue =
                        target.value.isEmpty() ? QStringLiteral("<empty>") : target.value;
                    firstRejected =
                        QStringLiteral("%1 / %2 = \"%3\" (expected %4)")
                            .arg(target.objectName, target.propertyName, displayValue,
                                 result.expectation);
                }
                break;
            }
        }
    }
    if (firstRejectedIndex.isValid()) {
        view->selectionModel()->setCurrentIndex(
            firstRejectedIndex, QItemSelectionModel::NoUpdate);
        view->scrollTo(firstRejectedIndex, QAbstractItemView::PositionAtCenter);
    }
    if (changed > 0) {
        static_cast<void>(controller_.squashUndoSince(
            undoDepth, QStringLiteral("Paste %1 cell(s)").arg(changed)));
    }

    QString message;
    if (changed == 0) {
        message = QStringLiteral(
                      "Paste made no changes: %1 unchanged, %2 rejected, %3 skipped")
                      .arg(unchanged)
                      .arg(rejected)
                      .arg(skipped);
    } else {
        message =
            QStringLiteral(
                "Paste complete: %1 changed, %2 unchanged, %3 rejected, %4 skipped")
                .arg(changed)
                .arg(unchanged)
                .arg(rejected)
                .arg(skipped);
    }
    if (!firstRejected.isEmpty()) {
        message += QStringLiteral("; first rejected: ") + firstRejected;
    }
    if (changed > 0) {
        message += QStringLiteral("; Ctrl+Z restores this paste");
    }
    statusBar()->showMessage(message, rejected > 0 ? 7000 : 4000);
}

void MainWindow::copyHierarchySelection()
{
    const auto* workspace = controller_.workspace();
    const QModelIndex current = hierarchyView_->currentIndex();
    if (workspace == nullptr || !current.isValid()) {
        return;
    }

    const std::string objectId =
        current.data(objectIdRole).toString().toUtf8().toStdString();
    QByteArray kind;
    QString label;
    if (const auto* page = regmap::findAddressSpace(*workspace, objectId)) {
        copiedPage_ = *page;
        copiedBlock_.reset();
        kind = QByteArrayLiteral("page");
        label = fromUtf8(page->name);
    } else if (const auto* block = regmap::findRegisterBlock(*workspace, objectId)) {
        copiedBlock_ = *block;
        copiedPage_.reset();
        kind = QByteArrayLiteral("block");
        label = fromUtf8(block->name);
    } else {
        statusBar()->showMessage(
            QStringLiteral("Select a Page or Register Block to copy"), 4000);
        return;
    }

    auto* mimeData = new QMimeData;
    mimeData->setData(QString::fromLatin1(hierarchyClipboardMimeType), kind);
    mimeData->setText(label);
    QApplication::clipboard()->setMimeData(mimeData);
    statusBar()->showMessage(
        QStringLiteral("Copied %1 %2")
            .arg(kind == QByteArrayLiteral("page") ? QStringLiteral("Page")
                                                   : QStringLiteral("Register Block"),
                 label),
        3000);
}

void MainWindow::pasteHierarchySelection()
{
    const auto* workspace = controller_.workspace();
    const QMimeData* mimeData = QApplication::clipboard()->mimeData();
    if (workspace == nullptr || mimeData == nullptr ||
        !mimeData->hasFormat(QString::fromLatin1(hierarchyClipboardMimeType))) {
        statusBar()->showMessage(
            QStringLiteral("Copy a Page or Register Block before pasting"), 4000);
        return;
    }

    const QModelIndex current = hierarchyView_->currentIndex();
    const std::string targetId =
        current.data(objectIdRole).toString().toUtf8().toStdString();
    const QByteArray kind =
        mimeData->data(QString::fromLatin1(hierarchyClipboardMimeType));

    if (kind == QByteArrayLiteral("page") && copiedPage_) {
        std::size_t insertion = workspace->addressSpaces.size();
        if (const auto targetPage = pagePosition(*workspace, targetId)) {
            insertion = *targetPage + 1;
        } else if (const auto targetBlock = blockPosition(*workspace, targetId)) {
            insertion = targetBlock->page + 1;
        } else if (!targetId.empty() && targetId != workspace->id) {
            statusBar()->showMessage(QStringLiteral("Select the Workspace, a Page, or a Block"),
                                     4000);
            return;
        }

        regmap::AddressSpace copy = *copiedPage_;
        copy.name = uniqueCopiedName(copy.name, [&](const std::string& candidate) {
            return std::ranges::any_of(
                workspace->addressSpaces,
                [&](const regmap::AddressSpace& page) { return page.name == candidate; });
        });
        std::set<regmap::ObjectId, std::less<>> generatedIds;
        prepareCopiedPage(*workspace, copy, generatedIds);
        const std::string newId = copy.id;
        const QString label = fromUtf8(copy.name);
        if (controller_.editWorkspace(
                QStringLiteral("Paste page %1").arg(label),
                [insertion, copy = std::move(copy)](regmap::Workspace& candidate) mutable {
                    const auto position = candidate.addressSpaces.begin() +
                        static_cast<std::ptrdiff_t>(
                            std::min(insertion, candidate.addressSpaces.size()));
                    candidate.addressSpaces.insert(position, std::move(copy));
                })) {
            selectedAddressId_ = newId;
            selectedBlockId_.clear();
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
            refreshProject();
            statusBar()->showMessage(
                QStringLiteral("Pasted Page %1; Ctrl+Z to restore").arg(label), 5000);
        }
        return;
    }

    if (kind == QByteArrayLiteral("block") && copiedBlock_) {
        std::size_t pageIndex = 0;
        std::size_t insertion = 0;
        if (const auto targetBlock = blockPosition(*workspace, targetId)) {
            pageIndex = targetBlock->page;
            insertion = targetBlock->block + 1;
        } else if (const auto targetPage = pagePosition(*workspace, targetId)) {
            pageIndex = *targetPage;
            insertion = workspace->addressSpaces[pageIndex].blocks.size();
        } else {
            statusBar()->showMessage(
                QStringLiteral("Select a destination Page or Register Block"), 4000);
            return;
        }

        const std::string destinationPageId = workspace->addressSpaces[pageIndex].id;
        regmap::RegisterBlock copy = *copiedBlock_;
        copy.name = uniqueCopiedName(
            copy.name, [&](const std::string& candidate) {
                return std::ranges::any_of(
                    workspace->addressSpaces[pageIndex].blocks,
                    [&](const regmap::RegisterBlock& block) {
                        return block.name == candidate;
                    });
            });
        std::set<regmap::ObjectId, std::less<>> generatedIds;
        prepareCopiedBlock(*workspace, copy, generatedIds);
        const std::uint64_t originalBase = copy.baseAddress;
        const auto pasteBase =
            availableBlockBase(workspace->addressSpaces[pageIndex], copy);
        if (!pasteBase) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot paste Register Block %1: no non-overlapping Block Base "
                    "fits Page %2")
                    .arg(fromUtf8(copy.name),
                         fromUtf8(workspace->addressSpaces[pageIndex].name)),
                7000);
            return;
        }
        copy.baseAddress = *pasteBase;
        const std::string newId = copy.id;
        const QString label = fromUtf8(copy.name);
        const QString baseFeedback =
            originalBase == *pasteBase
                ? QStringLiteral(" at Base %1").arg(hex(*pasteBase))
                : QStringLiteral(" at Base %1 (adjusted from %2)")
                      .arg(hex(*pasteBase), hex(originalBase));
        if (controller_.editWorkspace(
                QStringLiteral("Paste register block %1").arg(label),
                [destinationPageId, insertion,
                 copy = std::move(copy)](regmap::Workspace& candidate) mutable {
                    if (auto* page =
                            regmap::findAddressSpace(candidate, destinationPageId)) {
                        const auto position = page->blocks.begin() +
                            static_cast<std::ptrdiff_t>(
                                std::min(insertion, page->blocks.size()));
                        page->blocks.insert(position, std::move(copy));
                    }
                })) {
            selectedAddressId_ = destinationPageId;
            selectedBlockId_ = newId;
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
            refreshProject();
            statusBar()->showMessage(
                QStringLiteral("Pasted Register Block %1%2; Ctrl+Z to restore")
                    .arg(label, baseFeedback),
                6000);
        }
        return;
    }

    statusBar()->showMessage(
        QStringLiteral("The copied hierarchy object is no longer available"), 4000);
}

void MainWindow::moveHierarchyObject(const std::string& sourceId,
                                     const std::string& targetId, int placementValue)
{
    const auto* workspace = controller_.workspace();
    if (workspace == nullptr || sourceId.empty() || sourceId == targetId) {
        return;
    }
    const auto placement = static_cast<HierarchyDropPlacement>(placementValue);

    if (const auto sourcePage = pagePosition(*workspace, sourceId)) {
        std::size_t insertion = workspace->addressSpaces.size();
        if (targetId == workspace->id || targetId.empty()) {
            insertion = workspace->addressSpaces.size();
        } else if (const auto targetPage = pagePosition(*workspace, targetId)) {
            insertion = *targetPage +
                (placement == HierarchyDropPlacement::aboveItem ? 0U : 1U);
        } else if (const auto targetBlock = blockPosition(*workspace, targetId)) {
            insertion = targetBlock->page +
                (placement == HierarchyDropPlacement::aboveItem ? 0U : 1U);
        } else {
            return;
        }
        std::size_t normalizedInsertion = insertion;
        if (normalizedInsertion > *sourcePage) {
            --normalizedInsertion;
        }
        if (normalizedInsertion == *sourcePage) {
            return;
        }

        const QString label = fromUtf8(workspace->addressSpaces[*sourcePage].name);
        if (controller_.editWorkspace(
                QStringLiteral("Move page %1").arg(label),
                [sourceId, insertion](regmap::Workspace& candidate) {
                    const auto source = pagePosition(candidate, sourceId);
                    if (!source) {
                        return;
                    }
                    regmap::AddressSpace moved =
                        std::move(candidate.addressSpaces[*source]);
                    candidate.addressSpaces.erase(
                        candidate.addressSpaces.begin() +
                        static_cast<std::ptrdiff_t>(*source));
                    std::size_t destination = insertion;
                    if (destination > *source) {
                        --destination;
                    }
                    destination = std::min(destination, candidate.addressSpaces.size());
                    candidate.addressSpaces.insert(
                        candidate.addressSpaces.begin() +
                            static_cast<std::ptrdiff_t>(destination),
                        std::move(moved));
                })) {
            selectedAddressId_ = sourceId;
            selectedBlockId_.clear();
            selectedRegisterId_.clear();
            selectedFieldId_.clear();
            openFieldsRegisterId_.clear();
            refreshProject();
            statusBar()->showMessage(
                QStringLiteral("Moved Page %1; Ctrl+Z to restore").arg(label), 5000);
        }
        return;
    }

    const auto sourceBlock = blockPosition(*workspace, sourceId);
    if (!sourceBlock) {
        return;
    }
    std::size_t destinationPage = 0;
    std::size_t insertion = 0;
    if (const auto targetBlock = blockPosition(*workspace, targetId)) {
        destinationPage = targetBlock->page;
        insertion = targetBlock->block +
            (placement == HierarchyDropPlacement::aboveItem ? 0U : 1U);
    } else if (const auto targetPage = pagePosition(*workspace, targetId)) {
        destinationPage = *targetPage;
        insertion = workspace->addressSpaces[destinationPage].blocks.size();
    } else {
        statusBar()->showMessage(
            QStringLiteral("Drop a Register Block onto a Page or another Block"), 4000);
        return;
    }

    std::size_t normalizedInsertion = insertion;
    if (sourceBlock->page == destinationPage &&
        normalizedInsertion > sourceBlock->block) {
        --normalizedInsertion;
    }
    if (sourceBlock->page == destinationPage &&
        normalizedInsertion == sourceBlock->block) {
        return;
    }

    const std::string destinationPageId =
        workspace->addressSpaces[destinationPage].id;
    const auto& movingBlock =
        workspace->addressSpaces[sourceBlock->page]
            .blocks[sourceBlock->block];
    const QString label = fromUtf8(movingBlock.name);
    const auto& destinationAddressSpace =
        workspace->addressSpaces[destinationPage];
    if (sourceBlock->page != destinationPage) {
        const auto duplicateName = std::ranges::find(
            destinationAddressSpace.blocks, movingBlock.name,
            &regmap::RegisterBlock::name);
        if (duplicateName != destinationAddressSpace.blocks.end()) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot move Register Block %1: Page %2 already contains "
                    "a Block with that name")
                    .arg(label, fromUtf8(destinationAddressSpace.name)),
                7000);
            return;
        }
        if (!blockPlacementAvailable(destinationAddressSpace, movingBlock)) {
            statusBar()->showMessage(
                QStringLiteral(
                    "Cannot move Register Block %1: Base %2 overlaps another "
                    "Block or does not fit Page %3. Edit Block Base or free the "
                    "destination range, then retry.")
                    .arg(label, hex(movingBlock.baseAddress),
                         fromUtf8(destinationAddressSpace.name)),
                8000);
            return;
        }
    }
    if (controller_.editWorkspace(
            QStringLiteral("Move register block %1").arg(label),
            [sourceId, destinationPageId, insertion](regmap::Workspace& candidate) {
                const auto source = blockPosition(candidate, sourceId);
                const auto destination = pagePosition(candidate, destinationPageId);
                if (!source || !destination) {
                    return;
                }
                regmap::RegisterBlock moved =
                    std::move(candidate.addressSpaces[source->page].blocks[source->block]);
                auto& sourceBlocks = candidate.addressSpaces[source->page].blocks;
                sourceBlocks.erase(sourceBlocks.begin() +
                                   static_cast<std::ptrdiff_t>(source->block));
                std::size_t destinationIndex = insertion;
                if (source->page == *destination &&
                    destinationIndex > source->block) {
                    --destinationIndex;
                }
                auto& destinationBlocks =
                    candidate.addressSpaces[*destination].blocks;
                destinationIndex =
                    std::min(destinationIndex, destinationBlocks.size());
                destinationBlocks.insert(
                    destinationBlocks.begin() +
                        static_cast<std::ptrdiff_t>(destinationIndex),
                    std::move(moved));
            })) {
        selectedAddressId_ = destinationPageId;
        selectedBlockId_ = sourceId;
        selectedRegisterId_.clear();
        selectedFieldId_.clear();
        openFieldsRegisterId_.clear();
        refreshProject();
        statusBar()->showMessage(
            QStringLiteral("Moved Register Block %1; Ctrl+Z to restore").arg(label),
            5000);
    }
}

void MainWindow::updateEditActions()
{
    const bool hasWorkspace = controller_.workspace() != nullptr;
    saveAction_->setEnabled(hasWorkspace);
    synchronizeAction_->setEnabled(hasWorkspace);
    duplicateAction_->setEnabled(hasWorkspace);
    useWorkbenchAction_->setEnabled(controller_.hasConflicts());
    useRtlAction_->setEnabled(controller_.hasConflicts());
    undoAction_->setEnabled(controller_.canUndo());
    redoAction_->setEnabled(controller_.canRedo());
    undoAction_->setText(controller_.canUndo()
                             ? QStringLiteral("Undo %1").arg(controller_.undoText())
                             : QStringLiteral("Undo"));
    redoAction_->setText(controller_.canRedo()
                             ? QStringLiteral("Redo %1").arg(controller_.redoText())
                             : QStringLiteral("Redo"));
    deleteAction_->setEnabled(hasWorkspace &&
                              (!selectedAddressId_.empty() || !selectedBlockId_.empty() ||
                               !selectedRegisterId_.empty() || !selectedFieldId_.empty()));
    updateSyncPresentation();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!commitActiveEditor()) {
        event->ignore();
        return;
    }
    if (!controller_.isDirty()) {
        saveUiState();
        event->accept();
        return;
    }

    const UnsavedChoice choice = promptUnsavedChanges(true);
    if (choice == UnsavedChoice::cancel) {
        statusBar()->showMessage(
            QStringLiteral(
                "Close cancelled · %1 kept")
                .arg(unsavedChangeSummary(controller_.changes().size())),
            7000);
        event->ignore();
        return;
    }
    if (choice == UnsavedChoice::save) {
        controller_.save();
        if (controller_.isDirty()) {
            event->ignore();
            return;
        }
    }

    saveUiState();
    event->accept();
}

void MainWindow::selectRegister(const std::string& id)
{
    for (int row = 0; row < registerModel_->rowCount(); ++row) {
        if (registerModel_->index(row, 0).data(objectIdRole).toString().toUtf8().toStdString() ==
            id) {
            registerView_->setCurrentIndex(registerModel_->index(row, 0));
            registerView_->scrollTo(registerModel_->index(row, 0));
            return;
        }
    }
}

void MainWindow::selectField(const std::string& id)
{
    for (int row = 0; row < fieldModel_->rowCount(); ++row) {
        if (fieldModel_->index(row, 0).data(objectIdRole).toString().toUtf8().toStdString() == id) {
            fieldView_->setCurrentIndex(fieldModel_->index(row, 0));
            fieldView_->scrollTo(fieldModel_->index(row, 0));
            return;
        }
    }
}

void MainWindow::beginHierarchyRename(const std::string& id)
{
    const std::filesystem::path manifestPath = controller_.manifestPath();
    QTimer::singleShot(0, this, [this, id, manifestPath] {
        const QModelIndex current = hierarchyView_->currentIndex();
        if (controller_.manifestPath() != manifestPath || !current.isValid() ||
            current.data(objectIdRole).toString().toUtf8().toStdString() != id) {
            return;
        }
        hierarchyView_->setFocus(Qt::OtherFocusReason);
        hierarchyView_->edit(current);
    });
}

void MainWindow::beginRegisterRename(const std::string& id)
{
    const std::filesystem::path manifestPath = controller_.manifestPath();
    QTimer::singleShot(0, this, [this, id, manifestPath] {
        if (controller_.manifestPath() != manifestPath) {
            return;
        }
        const QModelIndex current = registerView_->currentIndex();
        if (!current.isValid()) {
            return;
        }
        const QModelIndex name =
            registerModel_->index(current.row(), registerNameColumn);
        if (name.data(objectIdRole).toString().toUtf8().toStdString() != id) {
            return;
        }
        registerView_->setCurrentIndex(name);
        registerView_->scrollTo(name);
        registerView_->setFocus(Qt::OtherFocusReason);
        registerView_->edit(name);
    });
}

void MainWindow::beginFieldRename(const std::string& id)
{
    const std::string registerId = selectedRegisterId_;
    const std::filesystem::path manifestPath = controller_.manifestPath();
    QTimer::singleShot(0, this, [this, id, registerId, manifestPath] {
        if (registerId.empty() || controller_.workspace() == nullptr ||
            controller_.manifestPath() != manifestPath ||
            selectedRegisterId_ != registerId || openFieldsRegisterId_ != registerId) {
            return;
        }
        const auto* reg = findRegister(registerId);
        if (reg == nullptr || findField(*reg, id) == nullptr) {
            return;
        }
        const QModelIndex current = fieldView_->currentIndex();
        if (!current.isValid()) {
            return;
        }
        const QModelIndex name = fieldModel_->index(current.row(), fieldNameColumn);
        if (name.data(objectIdRole).toString().toUtf8().toStdString() != id) {
            return;
        }
        fieldView_->setCurrentIndex(name);
        fieldView_->scrollTo(name);
        fieldView_->setFocus(Qt::OtherFocusReason);
        fieldView_->edit(name);
    });
}

void MainWindow::beginEnumRename(const std::string& id, const std::string& ownerId,
                                 bool fieldOwner)
{
    const std::filesystem::path manifestPath = controller_.manifestPath();
    const std::string registerId = selectedRegisterId_;
    const std::string fieldId = selectedFieldId_;
    const std::string fieldsRegisterId = openFieldsRegisterId_;
    QTimer::singleShot(
        0, this,
        [this, id, ownerId, fieldOwner, manifestPath, registerId, fieldId, fieldsRegisterId] {
            if (registerId.empty() || controller_.workspace() == nullptr ||
                controller_.manifestPath() != manifestPath ||
                selectedRegisterId_ != registerId || selectedFieldId_ != fieldId ||
                openFieldsRegisterId_ != fieldsRegisterId) {
                return;
            }
            const auto* reg = findRegister(registerId);
            if (reg == nullptr) {
                return;
            }
            const std::vector<regmap::EnumValue>* values = nullptr;
            if (fieldOwner) {
                if (fieldId != ownerId || fieldsRegisterId != registerId) {
                    return;
                }
                const auto* field = findField(*reg, ownerId);
                if (field == nullptr) {
                    return;
                }
                values = &field->enumValues;
            } else {
                if (reg->id != ownerId || !fieldId.empty()) {
                    return;
                }
                values = &reg->enumValues;
            }
            if (!std::ranges::any_of(*values, [&id](const regmap::EnumValue& value) {
                    return value.id == id;
                })) {
                return;
            }
            for (int row = 0; row < enumModel_->rowCount(); ++row) {
                const QModelIndex name = enumModel_->index(row, enumNameColumn);
                if (name.data(objectIdRole).toString().toUtf8().toStdString() != id) {
                    continue;
                }
                enumView_->setCurrentIndex(name);
                enumView_->scrollTo(name);
                enumView_->setFocus(Qt::OtherFocusReason);
                enumView_->edit(name);
                return;
            }
        });
}

void MainWindow::openSource(const regmap::SourceLocation& source)
{
    if (source.empty()) {
        return;
    }
    if (!SourceNavigation::open(source)) {
        QMessageBox::warning(this, QStringLiteral("Open Source"),
                             QStringLiteral("Could not open source: %1").arg(sourceText(source)));
    }
}
