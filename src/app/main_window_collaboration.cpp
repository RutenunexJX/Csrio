#include "main_window.hpp"
#include "workspace_inspection.hpp"
#include "regmap/core/consistency.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QStatusBar>
#include <QTableView>
#include <QTableWidget>
#include <QTimer>
#include <QTreeView>
#include <QUuid>
#include <QVBoxLayout>

namespace {
QString advice(const QString& code)
{
    if (code == "dirty") return "Save or resolve local edits, then recover GUI synchronization without repeating apply.";
    if (code == "active_editor") return "Finish or cancel the active cell editor, then retry GUI synchronization.";
    if (code == "modal") return "Close the modal dialog, then retry GUI synchronization.";
    if (code == "gui_generation_changed") return "Local edits occurred during the commit. Review the retained edits and disk changes before adopting.";
    if (code == "session_expired" || code == "project_mismatch") return "Discover the project's live sessions and recover against the saved revision.";
    if (code == "commit_in_progress") return "Wait for the current commit to finish, or reconnect after its reservation expires.";
    return "Inspect the current disk revision and GUI state before retrying.";
}
}

QString MainWindow::sessionEpoch()
{
    if (guiSessionEpoch_.isEmpty()) guiSessionEpoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return guiSessionEpoch_;
}

void MainWindow::resetGuiSession()
{
    if (pendingGuiCommit_ && controller_.modelRevision() != pendingGuiCommit_->generation)
        blockedAutoRevision_ = controller_.diskRevision();
    pendingGuiCommit_.reset();
    guiSessionEpoch_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    lastGuiCommitToken_.clear(); lastGuiCommitResult_ = {};
}

bool MainWindow::hasActiveGuiEditor() const
{
    for (const auto* view : {static_cast<QAbstractItemView*>(hierarchyView_),
             static_cast<QAbstractItemView*>(registerView_), static_cast<QAbstractItemView*>(fieldView_),
             static_cast<QAbstractItemView*>(enumView_)}) {
        for (const auto* editor : view->viewport()->findChildren<QWidget*>()) {
            if (editor->isVisibleTo(this) &&
                (qobject_cast<const QLineEdit*>(editor) || qobject_cast<const QComboBox*>(editor))) return true;
        }
    }
    for (const auto* edit : {pageBaseEdit_, pageWidthEdit_, pageDescriptionEdit_,
                            blockBaseEdit_, blockSizeEdit_, blockDescriptionEdit_})
        if (edit && edit->isModified()) return true;
    return false;
}

QString MainWindow::selectedSessionObjectId() const
{
    const QAbstractItemView* view = lastCommandView_ == enumView_ ? enumView_
        : lastCommandView_ == fieldView_ ? fieldView_
        : lastCommandView_ == registerView_ ? registerView_ : static_cast<QAbstractItemView*>(hierarchyView_);
    return view->currentIndex().data(objectIdRole).toString();
}

QString MainWindow::adoptGuiRevision(const QString& revision, const QString& selectedId, QString* reason)
{
    const auto position = captureNavigationPosition();
    QStringList ancestors;
    if (const auto* workspace = controller_.workspace()) {
        const auto model = regmap::inspectModel(*workspace);
        std::string id = selectedSessionObjectId().toStdString();
        while (!id.empty() && model.contains(id)) {
            ancestors.append(QString::fromStdString(id));
            id = regmap::inspectedProperty(model.at(id), "parent");
        }
    }
    const auto before = controller_.adoptedRevision();
    const auto error = controller_.adoptDiskRevision(revision, selectedId, reason);
    if (!error.isEmpty() || before == controller_.adoptedRevision()) return error;
    restoreNavigationPosition(position);
    if (selectedId.isEmpty() && controller_.workspace()) {
        const auto objects = regmap::ui::inspectWorkspace(*controller_.workspace());
        for (const auto& id : ancestors) {
            if (objects.contains(id)) {
                if (id != selectedSessionObjectId()) navigateToObjectImpl(id.toStdString(), false);
                break;
            }
        }
    }
    blockedAutoRevision_.clear();
    return {};
}

void MainWindow::adoptExternalRevisionIfClean()
{
    if (pendingGuiCommit_ || !controller_.hasExternalProjectChange() || !controller_.workspace() ||
        controller_.isDirty() || hasActiveGuiEditor() || QApplication::activeModalWidget() ||
        controller_.hasConflicts() || controller_.requiresInitialSyncChoice() || controller_.recoveryDraftAvailable()) return;
    const auto revision = controller_.diskRevision();
    if (revision.isEmpty() || revision == blockedAutoRevision_) return;
    QString reason;
    const auto code = adoptGuiRevision(revision, {}, &reason);
    if (!code.isEmpty()) {
        blockedAutoRevision_ = revision;
        statusBar()->showMessage(reason);
    }
}

QJsonObject MainWindow::guiCommit(const QJsonObject& parameters)
{
    const auto project = parameters.value("project").toString();
    const auto phase = parameters.value("phase").toString();
    const auto expected = parameters.value("expect").toString();
    const auto token = parameters.value("token").toString();
    const auto state = [&] { return guiSession({{"project",project}}); };
    const auto fail = [&](const QString& code, const QString& message) {
        auto result = state();
        result.insert("ok",false); result.insert("error_code",code); result.insert("error",message);
        result.insert("status","blocked");
        result.insert("suggestion",advice(code)); result.insert("gui_adopted",false);
        return result;
    };
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        if (it.key() == "preview" && it.value().isBool()) continue;
        if (QStringList{"project","phase","expect","token","session_epoch"}.contains(it.key()) && it.value().isString()) continue;
        return fail("invalid_request","Unknown or incorrectly typed commit parameter.");
    }
    if (project.isEmpty() || !currentWorkspaceForSuite(project))
        return fail("project_mismatch","The target session no longer has this project open.");
    if (phase == "finish" && token == lastGuiCommitToken_ && !token.isEmpty() &&
        parameters.value("session_epoch").toString() == sessionEpoch()) {
        if (expected != lastGuiCommitResult_.value("commit_revision").toString())
            return fail("revision_conflict","The repeated finish request names a different saved revision.");
        return lastGuiCommitResult_;
    }
    if (pendingGuiCommit_ && pendingGuiCommit_->deadline < QDateTime::currentMSecsSinceEpoch()) {
        if (controller_.modelRevision() != pendingGuiCommit_->generation)
            blockedAutoRevision_ = controller_.diskRevision();
        pendingGuiCommit_.reset();
    }
    const auto guard = [&]() -> QJsonObject {
        auto result = state();
        if (QApplication::activeModalWidget()) return fail("modal","A modal dialog is active.");
        if (hasActiveGuiEditor()) return fail("active_editor","An uncommitted editor is active.");
        if (controller_.isDirty()) return fail("dirty","Local Workbench edits were retained.");
        if (controller_.hasConflicts() || controller_.requiresInitialSyncChoice() || controller_.recoveryDraftAvailable())
            return fail("pending_conflict","Resolve synchronization conflicts or recovery drafts first.");
        return result;
    };
    if (phase == "begin") {
        if (pendingGuiCommit_) return fail("commit_in_progress","Another coordinated commit is in progress.");
        auto checked = guard();
        if (!checked.value("ok").toBool()) return checked;
        if (expected.isEmpty() || controller_.diskRevision() != expected)
            return fail("revision_conflict","The disk revision changed before commit preparation.");
        if (controller_.adoptedRevision() != expected) {
            if (parameters.value("preview").toBool()) return fail("revision_conflict","The GUI has not adopted the preview's base revision.");
            QString reason;
            const auto code = adoptGuiRevision(expected, {}, &reason);
            if (!code.isEmpty()) return fail(code,reason);
        }
        const auto created = QUuid::createUuid().toString(QUuid::WithoutBraces);
        pendingGuiCommit_ = PendingGuiCommit{created,project,expected,controller_.modelRevision(),
            QDateTime::currentMSecsSinceEpoch() + 60000};
        QTimer::singleShot(60000,this,[this,created] {
            if (!pendingGuiCommit_ || pendingGuiCommit_->token != created) return;
            if (controller_.modelRevision() != pendingGuiCommit_->generation)
                blockedAutoRevision_ = controller_.diskRevision();
            pendingGuiCommit_.reset();
            adoptExternalRevisionIfClean();
        });
        checked = state(); checked.insert("token",created); checked.insert("session_epoch",sessionEpoch());
        checked.insert("prepared_generation",static_cast<qint64>(pendingGuiCommit_->generation));
        return checked;
    }
    if (!pendingGuiCommit_ || token != pendingGuiCommit_->token ||
        parameters.value("session_epoch").toString() != sessionEpoch() ||
        currentWorkspaceForSuite(pendingGuiCommit_->project) == nullptr)
        return fail("session_expired","The commit reservation is missing, expired, or belongs to an earlier session.");
    if (phase == "cancel") {
        if (controller_.modelRevision() != pendingGuiCommit_->generation) blockedAutoRevision_ = controller_.diskRevision();
        pendingGuiCommit_.reset();
        QTimer::singleShot(0,this,&MainWindow::adoptExternalRevisionIfClean);
        return state();
    }
    if (phase != "confirm" && phase != "finish") return fail("invalid_request","Unknown commit phase.");
    QJsonObject result;
    if (controller_.modelRevision() != pendingGuiCommit_->generation)
        result = fail("gui_generation_changed","The GUI edit generation changed after preparation; new local edits were retained.");
    else result = guard();
    if (result.value("ok").toBool()) {
        const auto required = phase == "confirm" ? pendingGuiCommit_->revision : expected;
        if (required.isEmpty() || controller_.diskRevision() != required)
            result = fail("revision_conflict","The disk revision no longer matches this commit.");
        else if (phase == "finish") {
            QScopedValueRollback<bool> finishing(completingGuiCommit_,true);
            result = guiSession({{"project",project},{"expect",required},{"adopt",true}});
            result.insert("gui_adopted",result.value("ok").toBool() && controller_.adoptedRevision() == required);
        }
    }
    result.insert("token",token);
    result.insert("prepared_generation",static_cast<qint64>(pendingGuiCommit_->generation));
    if (!result.value("ok").toBool()) result.insert("suggestion",advice(result.value("error_code").toString()));
    if (phase == "finish") {
        if (!result.value("ok").toBool()) blockedAutoRevision_ = expected;
        lastGuiCommitToken_ = token;
        pendingGuiCommit_.reset();
        result.insert("commit_pending",false);
        result.insert("commit_revision",expected);
        lastGuiCommitResult_ = result;
    }
    return result;
}

void MainWindow::showReferenceChecks()
{
    if (!controller_.workspace() || !commitActiveEditor()) return;
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("referenceChecksDialog");
    dialog->setWindowTitle("References and pending rename / deletion checks");
    auto* layout = new QVBoxLayout(dialog);
    auto* note = new QLabel("Structured rows are exact parent relationships. Text rows are suggestions only. Double-click a row to locate its source; edit descriptions explicitly before saving.",dialog);
    note->setWordWrap(true); layout->addWidget(note);
    auto* table = new QTableWidget(dialog);
    table->setObjectName("referenceChecksTable");
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"Kind","Target path / stable ID","Source path / stable ID","Property","Text excerpt","State"});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    const auto refresh = [this,table] {
        if (!controller_.workspace()) { table->setRowCount(0); return; }
        const auto selected = selectedSessionObjectId().toStdString();
        auto findings = selected.empty() ? std::vector<regmap::ReferenceFinding>{}
            : regmap::findReferences(*controller_.workspace(),{selected});
        auto pending = controller_.pendingReferenceChanges();
        findings.insert(findings.end(),pending.begin(),pending.end());
        table->setRowCount(static_cast<int>(findings.size()));
        for (int row = 0; row < static_cast<int>(findings.size()); ++row) {
            const auto& finding = findings[static_cast<std::size_t>(row)];
            const QStringList values{QString::fromStdString(finding.kind),
                QString::fromStdString(finding.targetPath + "\n" + finding.targetId),
                QString::fromStdString(finding.path + "\n" + finding.objectId),
                QString::fromStdString(finding.property),QString::fromStdString(finding.snippet),
                QString::fromStdString(finding.status) + (finding.ambiguous ? " (ambiguous scope)" : "")};
            QStringList candidates;
            for (const auto& id : finding.candidates) candidates.append(QString::fromStdString(id));
            for (int column = 0; column < values.size(); ++column) {
                auto* item = new QTableWidgetItem(values[column]);
                item->setData(Qt::UserRole,QString::fromStdString(finding.objectId));
                item->setToolTip(values[column] + "\nCandidates: " + candidates.join(", "));
                table->setItem(row,column,item);
            }
        }
        table->setColumnWidth(0,90); table->setColumnWidth(1,220); table->setColumnWidth(2,220);
        table->setColumnWidth(3,100); table->setColumnWidth(4,350); table->resizeRowsToContents();
    };
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close,dialog);
    auto* refreshButton = buttons->addButton("Refresh",QDialogButtonBox::ActionRole);
    connect(refreshButton,&QPushButton::clicked,dialog,refresh);
    connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    connect(table,&QTableWidget::cellDoubleClicked,dialog,[this,table](int row,int) {
        if (const auto* item = table->item(row,0)) navigateToObject(item->data(Qt::UserRole).toString().toStdString(),true);
    });
    layout->addWidget(buttons); refresh(); dialog->resize(1100,480); dialog->show();
}
