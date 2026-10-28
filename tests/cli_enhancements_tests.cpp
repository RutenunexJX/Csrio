#include "cli_app.hpp"
#include "regmap/core/project.hpp"
#include "regmap/core/serialization.hpp"
#include "regmap/core/three_way_merge.hpp"
#include "regmap/core/workspace_store.hpp"
#include "regmap/core/document_layout.hpp"
#include <QDirIterator>
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>
#include <xlsxdocument.h>

namespace {
using namespace regmap;
struct Reply { int code; QJsonObject root; QByteArray bytes; QString error; QJsonObject result() const { return root.value("result").toObject(); } };
Reply call(QStringList args, const QJsonObject& patch = {})
{
    args.prepend("--json");
    QString input = QString::fromUtf8(QJsonDocument(patch).toJson()), out, err;
    QTextStream in(&input,QIODevice::ReadOnly), output(&out), error(&err);
    const auto code = regmap::cli::run(args,in,output,error);
    return {code,QJsonDocument::fromJson(out.toUtf8()).object(),out.toUtf8(),err};
}
QByteArray read(const QString& path) { QFile file(path); if(!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
bool write(const QString& path, const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(); }
QString revision(const QString& path) { return call({"summary",path}).root.value("revision").toString(); }
QJsonObject set(const QString& id,const QString& key,const QJsonValue& value) { return {{"op","set"},{"id",id},{"property",key},{"value",value}}; }
struct Fixture {
    QTemporaryDir directory;
    QString path, markdown, header, xlsx;
    Fixture() {
        path=directory.filePath("project.regmap.yaml"); markdown=directory.filePath("out/map.md");
        header=directory.filePath("out/map.h"); xlsx=directory.filePath("out/map.xlsx");
        Workspace workspace; workspace.id="workspace"; workspace.name="CLI enhancements";
        workspace.manifestPath=std::filesystem::path(path.toStdWString());
        for (const auto* suffix : {"a","b"}) {
            AddressSpace page; page.id=std::string("page-")+suffix; page.name=std::string("Page_")+suffix;
            RegisterBlock block; block.id=std::string("block-")+suffix; block.name="Common"; block.size=256;
            Register reg; reg.id=std::string("version-")+suffix; reg.name="VERSION"; reg.type=FieldType::structure;
            reg.addressFixed=true; reg.tags={"common"}; reg.initialValue=UnsignedValue(0); reg.resetValue=UnsignedValue(0);
            for (unsigned i=0;i<6;++i) {
                Field field; field.id=std::string("field-")+suffix+"-"+std::to_string(i); field.name="F"+std::to_string(i);
                field.type=FieldType::enumeration; field.lsb=i==5?12:i*2; field.msb=field.lsb+1;
                for(unsigned code=0;code<2;++code) {
                    EnumValue value; value.id=field.id+"-e"+std::to_string(code); value.name=code?"ON":"OFF";
                    value.value=UnsignedValue(code); value.description=i==3?"different meaning":"shared meaning";
                    field.enumValues.push_back(value);
                }
                reg.fields.push_back(field);
            }
            Register status; status.id=std::string("status-")+suffix; status.name="STATUS"; status.offset=4; status.addressFixed=true;
            block.registers={reg,status}; page.blocks={block}; workspace.addressSpaces.push_back(page);
        }
        ProjectManifest manifest; manifest.manifestPath=workspace.manifestPath; manifest.workspaceId=workspace.id; manifest.workspaceName=workspace.name;
        manifest.rtl.path.declared="rtl/map.sv"; manifest.rtl.path.resolved=std::filesystem::path(directory.filePath("rtl/map.sv").toStdWString()); manifest.rtl.moduleName="map";
        manifest.outputDirectory.declared="out"; manifest.outputDirectory.resolved=std::filesystem::path(directory.filePath("out").toStdWString());
        for(const auto& [kind,name] : std::vector<std::pair<GenerationTargetKind,std::string>>{
            {GenerationTargetKind::xlsx,"map.xlsx"},{GenerationTargetKind::markdown,"map.md"},{GenerationTargetKind::cHeader,"map.h"}}) {
            GenerationTargetConfig target; target.kind=kind; target.path.declared=name; target.path.resolved=manifest.outputDirectory.resolved/name; manifest.targets.push_back(target);
        }
        const auto errors=saveProjectFile(manifest,workspace);
        if(!errors.empty()) qFatal("Fixture save failed");
    }
    ~Fixture() {
        QDirIterator files(directory.path(),QDir::Files,QDirIterator::Subdirectories);
        while(files.hasNext()) QFile::setPermissions(files.next(),QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    }
    Reply apply(const QJsonArray& operations, QStringList options={}) {
        QStringList args{"apply",path,"-","--expect",revision(path)}; args.append(options);
        return call(args,{{"api_version",1},{"operations",operations}});
    }
    QJsonObject get(const QString& id) { return call({"get",path,id}).result(); }
};
}
class CliEnhancementsTests : public QObject {
    Q_OBJECT
private slots:
    void projectsWithoutChangingQuerySemantics();
    void projectsPublicPropertiesThroughExecutable();
    void batchesSelectExactlyAndRollback();
    void summarizesSuccessAndFailureWithoutLosingReferences();
    void validatesScopedRulesAndExceptions();
    void defersAndSelectsOutputsWithRevisionGuards();
    void reordersDocumentsAndPreservesHistory();
    void referencesOnlyConsecutiveIdenticalEnums();
    void reportsStatsForSuccessFailureAndExecutable();
    void exposesCapabilitiesInSchemaAndHelp();
    void preflightsWithoutRejectingEarlierStateChanges();
    void preservesWorkbookOrderAndRejectsInvalidOutputOptions();
};
void CliEnhancementsTests::projectsWithoutChangingQuerySemantics()
{
    Fixture f;
    const auto full=call({"list",f.path,"--kind","register","--limit","1"});
    const auto small=call({"list",f.path,"--kind","register","--limit","1","--properties","name,address"});
    QCOMPARE(small.code,0); QCOMPARE(full.root.value("result_metadata"),small.root.value("result_metadata"));
    const auto item=small.root.value("result").toArray().first().toObject(); QCOMPARE(item.size(),3); QVERIFY(item.contains("id"));
    const auto many=call({"get-many",f.path,"version-a","missing","version-a","--properties","name"});
    const auto items=many.result().value("items").toArray(); QCOMPARE(items.size(),3);
    QCOMPARE(items[0].toObject().value("object").toObject().size(),2);
    QVERIFY(items[1].toObject().value("object").isNull()); QCOMPARE(items[2].toObject().value("request_index").toInteger(),2);
    QCOMPARE(call({"get",f.path,"version-a","--properties","typo"}).code,2);
    const auto stale=call({"get",f.path,"version-a","--expect","sha256:"+QString(64,'0'),"--properties","name"});
    QCOMPARE(stale.code,3); QVERIFY(!stale.root.value("result").toObject().contains("name"));
}
void CliEnhancementsTests::projectsPublicPropertiesThroughExecutable()
{
    Fixture f;
    QCOMPARE(f.apply({set("page-a","base","0x40000000"),
        set("field-a-0","software_access","rw"), set("field-a-0","hardware_access","ro")},
        {"--defer-generation"}).code, 0);
    const auto original = read(f.path);
    const auto invoke = [](QStringList arguments) {
        arguments.prepend("--json");
        QProcess process;
        process.start(QString::fromUtf8(REGMAP_CLI_EXECUTABLE), arguments);
        if (!process.waitForFinished(30000))
            return Reply{-1, {}, {}, process.errorString()};
        const auto bytes = process.readAllStandardOutput();
        return Reply{process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1,
            QJsonDocument::fromJson(bytes).object(), bytes, QString::fromUtf8(process.readAllStandardError())};
    };
    const auto schemaReply = invoke({"schema"});
    QCOMPARE(schemaReply.code, 0);
    const auto schema = schemaReply.result();
    const auto contract = schema.value("projection_contract").toObject();
    const auto allowed = contract.value("allowed_properties").toArray();
    QStringList propertyNames;
    for (const auto& property : allowed) propertyNames.append(property.toString());
    QVERIFY(!propertyNames.isEmpty());
    for (const auto* name : {"software_access","hardware_access","absolute_base","pages"})
        QVERIFY(propertyNames.contains(name));
    const auto properties = propertyNames.join(',');
    const auto projectedObject = [&](const QJsonObject& object) {
        QJsonObject expected{{"id", object.value("id")}};
        for (const auto& name : propertyNames)
            expected.insert(name, object.contains(name) ? object.value(name) : QJsonValue(QJsonValue::Null));
        return expected;
    };
    const std::vector<QStringList> queries{
        {"get",f.path}, {"get",f.path,"page-a"}, {"get",f.path,"block-a"},
        {"get",f.path,"version-a"}, {"get",f.path,"field-a-0"}, {"get",f.path,"field-a-0-e0"},
        {"get-many",f.path,"workspace","page-a","block-a","version-a","field-a-0","field-a-0-e0","missing","field-a-0"},
        {"list",f.path,"--all"}, {"list",f.path,"--kind","field","--offset","1","--limit","2"},
        {"find",f.path,"F","--kind","field","--offset","1","--limit","2"}
    };
    for (const auto& arguments : queries) {
        const auto full = invoke(arguments);
        QCOMPARE(full.code, arguments.front() == "get-many" ? 1 : 0);
        QVERIFY2(!full.root.isEmpty(), full.bytes.constData());
        auto requested = arguments;
        requested.append({"--properties", properties});
        const auto projected = invoke(requested);
        QCOMPARE(projected.code, full.code);
        auto expected = full.root;
        const auto result = full.root.value("result");
        if (result.isArray()) {
            QJsonArray objects;
            for (const auto& value : result.toArray()) {
                const auto object = value.toObject();
                for (const auto& key : object.keys()) QVERIFY2(propertyNames.contains(key), qPrintable(key));
                objects.append(projectedObject(object));
            }
            expected.insert("result", objects);
        } else if (arguments.front() == "get-many") {
            auto batch = result.toObject();
            QJsonArray items;
            for (const auto& value : batch.value("items").toArray()) {
                auto item = value.toObject();
                if (item.value("found").toBool()) {
                    const auto object = item.value("object").toObject();
                    for (const auto& key : object.keys()) QVERIFY2(propertyNames.contains(key), qPrintable(key));
                    item.insert("object", projectedObject(object));
                }
                items.append(item);
            }
            batch.insert("items", items);
            expected.insert("result", batch);
        } else {
            const auto object = result.toObject();
            for (const auto& key : object.keys()) QVERIFY2(propertyNames.contains(key), qPrintable(key));
            expected.insert("result", projectedObject(object));
        }
        QCOMPARE(projected.root, expected);
    }
    const auto field = invoke({"get",f.path,"field-a-0","--properties","software_access,hardware_access"});
    QCOMPARE(field.code, 0);
    QCOMPARE(field.result(), (QJsonObject{{"id","field-a-0"},{"software_access","rw"},{"hardware_access","ro"}}));
    const auto block = invoke({"get",f.path,"block-a","--properties","name,absolute_base"});
    QCOMPARE(block.code, 0);
    QCOMPARE(block.result(), (QJsonObject{{"id","block-a"},{"name","Common"},{"absolute_base","0x40000000"}}));
    const auto aliases = invoke({"get",f.path,"field-a-0","--properties","sw_access,hw_access"});
    QCOMPARE(aliases.code, 0);
    QCOMPARE(aliases.result(), (QJsonObject{{"id","field-a-0"},{"sw_access",QJsonValue::Null},{"hw_access",QJsonValue::Null}}));

    const std::vector<QStringList> boundaries{
        {"list",f.path}, {"find",f.path,"F"}, {"get",f.path,"field-a-0"}, {"get-many",f.path,"field-a-0"}
    };
    for (const auto& arguments : boundaries) {
        for (const auto* invalid : {"not_a_public_property","software_access,software_access"}) {
            auto requested = arguments;
            requested.append({"--properties",invalid});
            const auto failed = invoke(requested);
            QCOMPARE(failed.code, 2);
            QCOMPARE(failed.root.value("error_code").toString(), "RMC1000");
        }
        auto stale = arguments;
        stale.append({"--expect","sha256:"+QString(64,'0')});
        const auto failed = invoke(stale);
        QCOMPARE(failed.code, 3);
        stale.append({"--properties",properties});
        const auto projected = invoke(stale);
        QCOMPARE(projected.code, 3);
        auto expectedFailure = failed.root;
        if (arguments.front() == "get-many") {
            // Existing projected revision failures include an empty items array.
            auto result = expectedFailure.value("result").toObject();
            result.insert("items", QJsonArray{});
            expectedFailure.insert("result", result);
        }
        QCOMPARE(projected.root, expectedFailure);

        const auto help = invoke({"help",arguments.front()});
        QCOMPARE(help.code, 0);
        QCOMPARE(help.result().value("projection_contract"), QJsonValue(contract));
        auto schemaOptions = schema.value("command_argument_schemas").toObject().value(arguments.front()).toObject().value("options").toArray();
        auto helpOptions = help.result().value("argument_schema").toObject().value("options").toArray();
        QCOMPARE(helpOptions, schemaOptions);
        bool documented = false;
        for (const auto& option : helpOptions) if (option.toObject().value("name") == "--properties") {
            QCOMPARE(option.toObject().value("allowed_values").toArray(), allowed);
            documented = true;
        }
        QVERIFY(documented);
        QProcess process;
        process.start(QString::fromUtf8(REGMAP_CLI_EXECUTABLE), {"help",arguments.front()});
        QVERIFY(process.waitForFinished(30000));
        QCOMPARE(process.exitCode(), 0);
        const auto text = process.readAllStandardOutput();
        QVERIFY(text.contains(properties.toUtf8()));
    }
    QCOMPARE(read(f.path), original);
}

void CliEnhancementsTests::batchesSelectExactlyAndRollback()
{
    Fixture f; const auto before=read(f.path);
    const QJsonObject selector{{"kind","register"},{"names",QJsonArray{"VERSION"}},{"parents",QJsonArray{"page-a"}},{"recursive",true},{"tags",QJsonArray{"common"}}};
    QJsonObject batch{{"op","set-many"},{"select",selector},{"expect_count",1},{"values",QJsonObject{{"description","updated"},{"name","VERSION_UPDATED"}}}};
    const auto preview=f.apply({batch},{"--dry-run","--defer-generation"}); QCOMPARE(preview.code,0); QCOMPARE(read(f.path),before);
    QCOMPARE(preview.result().value("changes").toArray()[0].toObject().value("matched_count").toInt(),1);
    const auto actual=f.apply({batch},{"--defer-generation"}); QCOMPARE(actual.code,0);
    QCOMPARE(f.get("version-a").value("description").toString(),"updated"); QCOMPARE(f.get("version-b").value("description").toString(),"");
    const auto saved=read(f.path); batch.insert("expect_count",2);
    QCOMPARE(f.apply({batch},{"--defer-generation"}).code,2); QCOMPARE(read(f.path),saved);
    QJsonObject fields{{"op","set-many"},{"select",QJsonObject{{"kind","field"},{"parents",QJsonArray{"version-a"}},{"tags",QJsonArray{"common"}}}},
        {"expect_count",6},{"values",QJsonObject{{"description","fields"}}}};
    QCOMPARE(f.apply({fields},{"--defer-generation"}).code,0);
    const auto beforeDelete=read(f.path);
    const QJsonObject remove{{"op","remove-many"},{"select",QJsonObject{{"ids",QJsonArray{"version-b"}}}},{"expect_count",1}};
    QCOMPARE(f.apply({set("version-a","description","must rollback"),remove},{"--defer-generation"}).code,2);
    QCOMPARE(read(f.path),beforeDelete);
    auto cascade=remove;cascade.insert("cascade",true);QCOMPARE(f.apply({cascade},{"--defer-generation"}).code,0);
    QCOMPARE(call({"get",f.path,"version-b"}).code,1);
}
void CliEnhancementsTests::summarizesSuccessAndFailureWithoutLosingReferences()
{
    Fixture f;
    QJsonObject first=set("version-a","description","one"); first.insert("ref","first");
    QJsonObject next=set("version-a","description","two");next.insert("id",QJsonObject{{"ref","first"}});
    const auto full=f.apply({first,next},{"--dry-run","--defer-generation"});
    const auto compact=f.apply({first,next},{"--dry-run","--defer-generation","--changes","summary"});
    QCOMPARE(compact.code,0); QCOMPARE(compact.result().value("new_revision"),full.result().value("new_revision"));
    QVERIFY(!compact.result().value("changes").toArray()[0].toObject().contains("before"));
    const auto failed=f.apply({first,set("missing","description","bad")},{"--dry-run","--changes","summary"});
    QCOMPARE(failed.code,2);const auto failure=failed.result().value("failure").toObject();
    QCOMPARE(failure.value("operation_index").toInt(),1);QCOMPARE(failure.value("json_pointer").toString(),"/operations/1");
    QVERIFY(failure.value("completed_operations_rolled_back").toBool());QVERIFY(!failure.value("writes_performed").toBool());
    const auto copy=f.directory.filePath("before.yaml");QVERIFY(write(copy,read(f.path)));
    QCOMPARE(f.apply({first},{"--defer-generation"}).code,0);
    const auto diff=call({"diff",copy,f.path,"--changes","summary"});QCOMPARE(diff.code,0);QVERIFY(diff.result().value("changed").toBool());
    QVERIFY(!diff.result().value("changes").toArray()[0].toObject().contains("after"));
}
void CliEnhancementsTests::validatesScopedRulesAndExceptions()
{
    Fixture f;const auto rules=f.directory.filePath("rules.json");
    QJsonObject rule{{"id","common-version"},{"scope",QJsonObject{{"kind","block"},{"names",QJsonArray{"Common"}}}},
        {"select",QJsonObject{{"kind","register"},{"names",QJsonArray{"VERSION"}}}},
        {"assert",QJsonObject{{"count",1},{"same",QJsonArray{"layout","description"}},{"properties",QJsonObject{{"description",""}}}}}};
    auto saveRules=[&] { return write(rules,QJsonDocument(QJsonObject{{"version",1},{"rules",QJsonArray{rule}}}).toJson()); };
    QVERIFY(saveRules());QCOMPARE(call({"validate",f.path,"--rules",rules}).code,0);
    const auto before=read(f.path);const auto failed=f.apply({set("version-b","description","board-specific")},{"--rules",rules,"--defer-generation"});
    QCOMPARE(failed.code,1);QCOMPARE(read(f.path),before);QVERIFY(!failed.result().value("rule_findings").toArray().isEmpty());
    const auto finding=failed.result().value("rule_findings").toArray().first().toObject();
    QCOMPARE(finding.value("page_id").toString(),"page-b");QVERIFY(finding.contains("expected"));QVERIFY(finding.contains("actual"));
    rule.insert("severity","warning");QVERIFY(saveRules());QCOMPARE(f.apply({set("version-b","description","board-specific")},{"--rules",rules,"--defer-generation"}).code,0);
    rule.insert("severity","error");rule.insert("scope",QJsonObject{{"kind","block"},{"exclude_ids",QJsonArray{"page-b"}}});
    QVERIFY(saveRules());QCOMPARE(call({"validate",f.path,"--rules",rules}).code,0);
    rule=QJsonObject{{"id","enum-required"},{"select",QJsonObject{{"kind","field"},{"names",QJsonArray{"F0"}}}},
        {"assert",QJsonObject{{"enum_values",QJsonArray{"0x0","0x1","0x2"}}}}};
    QVERIFY(saveRules());const auto invalid=call({"validate",f.path,"--rules",rules});QCOMPARE(invalid.code,1);
    QCOMPARE(invalid.result().value("rule_findings").toArray().size(),2);
    rule=QJsonObject{{"id","literal-description"},{"select",QJsonObject{{"ids",QJsonArray{"version-a"}}}},
        {"assert",QJsonObject{{"properties",QJsonObject{{"description","1"}}}}}};
    QVERIFY(saveRules());QCOMPARE(f.apply({set("version-a","description","0x1")},{"--defer-generation"}).code,0);
    QCOMPARE(call({"validate",f.path,"--rules",rules}).code,1);
}
void CliEnhancementsTests::defersAndSelectsOutputsWithRevisionGuards()
{
    Fixture f;QCOMPARE(call({"generate",f.path}).code,0);const auto md=read(f.markdown), h=read(f.header), x=read(f.xlsx);const auto previous=revision(f.path);
    const auto saved=f.apply({set("version-a","description","changed")},{"--defer-generation","--stats"});
    QCOMPARE(saved.code,0);QVERIFY(saved.result().value("saved").toBool());QCOMPARE(saved.result().value("generation_state").toString(),"deferred");
    QVERIFY(saved.result().value("outputs_current").isNull());QCOMPARE(saved.result().value("pending_outputs").toArray().size(),3);
    QCOMPARE(read(f.markdown),md);QCOMPARE(read(f.header),h);QCOMPARE(read(f.xlsx),x);
    QCOMPARE(call({"generate",f.path,"--expect",previous}).code,3);QCOMPARE(read(f.markdown),md);
    const auto partial=f.apply({set("version-a","description","changed again")},{"--target","markdown"});
    QCOMPARE(partial.code,0);QCOMPARE(partial.result().value("generation_state").toString(),"partial");
    QCOMPARE(partial.result().value("pending_outputs").toArray().size(),2);QVERIFY(read(f.markdown)!=md);QCOMPARE(read(f.xlsx),x);
    QCOMPARE(call({"generate",f.path,"--expect",revision(f.path)}).code,0);
    QCOMPARE(call({"status",f.path,"--require-current"}).code,0);
    QCOMPARE(f.apply({}, {"--defer-generation","--target","xlsx"}).code,2);
}
void CliEnhancementsTests::reordersDocumentsAndPreservesHistory()
{
    Fixture f;const auto base=openProject(std::filesystem::path(f.path.toStdWString()));QVERIFY(base.workspace);
    const QJsonObject reorder{{"op","reorder"},{"id","status-a"},{"before_id","version-a"}};
    QCOMPARE(f.apply({reorder},{"--target","markdown"}).code,0);
    const auto after=openProject(std::filesystem::path(f.path.toStdWString()));QVERIFY(after.workspace);
    const auto* status=findRegister(*after.workspace,"status-a");QVERIFY(status);QCOMPARE(status->offset,std::uint64_t(4));QVERIFY(status->addressFixed);
    QCOMPARE(documentRegisters(after.workspace->addressSpaces[0].blocks[0])[0]->id,std::string("status-a"));
    const auto markdown=read(f.markdown);QVERIFY(markdown.indexOf("#### STATUS")<markdown.indexOf("#### VERSION"));
    const auto restored=parseWorkspaceState(serializeWorkspaceState(*after.workspace));QVERIFY(restored.workspace);
    QVERIFY(diffWorkspaces(*after.workspace,*restored.workspace).empty());
    WorkspaceStore store(*base.workspace);QVERIFY(store.transact("Reorder",[&](Workspace& workspace){workspace=*after.workspace;}));
    QVERIFY(store.undo());QVERIFY(!findRegister(*store.workspace(),"status-a")->displayOrder);
    QVERIFY(store.redo());QCOMPARE(findRegister(*store.workspace(),"status-a")->displayOrder,std::optional<std::uint64_t>(0));
    auto rtl=*base.workspace;findRegister(rtl,"version-a")->description="RTL description";
    const auto merged=mergeWorkspaces(*base.workspace,*after.workspace,rtl);QVERIFY(merged.merged);
    QCOMPARE(findRegister(*merged.merged,"status-a")->displayOrder,std::optional<std::uint64_t>(0));
    const auto saved=read(f.path);QCOMPARE(f.apply({QJsonObject{{"op","reorder"},{"id","status-a"},{"before_id","version-b"}}},{"--defer-generation"}).code,2);QCOMPARE(read(f.path),saved);
}
void CliEnhancementsTests::referencesOnlyConsecutiveIdenticalEnums()
{
    Fixture f;QCOMPARE(call({"generate",f.path}).code,0);const auto full=read(f.markdown), header=read(f.header);
    const auto compact=f.apply({}, {"--enum-tables","consecutive"});QCOMPARE(compact.code,0);QVERIFY(compact.result().value("saved").toBool());
    const auto markdown=read(f.markdown);QCOMPARE(full.count("| Name | Value | Description |"),12);QCOMPARE(markdown.count("| Name | Value | Description |"),8);
    QVERIFY(markdown.contains("**F1**: see **F0**"));QVERIFY(markdown.contains("**F2**: see **F0**"));QCOMPARE(read(f.header),header);
    const auto project=openProject(std::filesystem::path(f.path.toStdWString()));QVERIFY(project.manifest);
    for(const auto& target:project.manifest->targets) if(target.kind!=GenerationTargetKind::cHeader) QCOMPARE(target.options.at("enum_tables"),std::string("consecutive"));
    QXlsx::Document book(f.xlsx);bool checked=false;
    for(const auto& sheet:book.sheetNames()) { book.selectSheet(sheet);
        QMap<QString,int> rows;for(int row=1;row<=book.dimension().lastRow();++row) { const auto name=book.read(row,3).toString();if(name.startsWith('F')) rows.insert(name,row); }
        if(!rows.contains("F0")) continue;
        QVERIFY(book.read(rows["F0"],10).toString().contains("OFF=0x0"));
        QVERIFY(book.read(rows["F1"],10).toString().contains(QStringLiteral("See J%1").arg(rows["F0"])));
        QVERIFY(!book.read(rows["F3"],10).toString().startsWith("See"));QVERIFY(!book.read(rows["F5"],10).toString().startsWith("See"));checked=true;
    }
    QVERIFY(checked);QCOMPARE(call({"status",f.path,"--require-current"}).code,0);
    QCOMPARE(f.apply({}, {"--enum-tables","full"}).code,0);QCOMPARE(read(f.markdown),full);
}
void CliEnhancementsTests::reportsStatsForSuccessFailureAndExecutable()
{
    Fixture f;const auto ordinary=call({"generate",f.path,"--dry-run","--target","markdown"});
    const auto measured=call({"generate",f.path,"--dry-run","--target","markdown","--stats"});
    QCOMPARE(measured.code,0);QCOMPARE(measured.result(),ordinary.result());
    QCOMPARE(measured.root.value("stats").toObject().value("response_bytes").toInteger(),measured.bytes.size());
    QVERIFY(measured.root.value("stats").toObject().value("generated_bytes").toInteger()>0);
    const auto failed=f.apply({set("missing","name","bad")},{"--stats","--dry-run"});QCOMPARE(failed.code,2);
    QCOMPARE(failed.root.value("stats").toObject().value("operation_count").toInt(),1);
    const auto badRules=call({"validate",f.path,"--rules",f.directory.filePath("missing.json"),"--stats"});
    QCOMPARE(badRules.code,2);QVERIFY(badRules.root.contains("stats"));
    bool patch=false;for(const auto& stage:failed.root.value("stats").toObject().value("stages").toArray()) if(stage.toObject().value("name")=="patch") patch=true;QVERIFY(patch);
    QProcess process;process.start(QString::fromUtf8(REGMAP_CLI_EXECUTABLE),{"--json","list",f.path,"--properties","name","--stats"});
    QVERIFY(process.waitForFinished(30000));QCOMPARE(process.exitCode(),0);const auto bytes=process.readAllStandardOutput();
    const auto root=QJsonDocument::fromJson(bytes).object();QCOMPARE(root.value("stats").toObject().value("response_bytes").toInteger(),bytes.size());QVERIFY(process.readAllStandardError().isEmpty());
    process.start(QString::fromUtf8(REGMAP_CLI_EXECUTABLE),{"validate",f.path,"--rules",f.directory.filePath("missing.json"),"--stats"});
    QVERIFY(process.waitForFinished(30000));QCOMPARE(process.exitCode(),2);
    const auto errors=process.readAllStandardError();QVERIFY(errors.contains("Stats: {"));QVERIFY(errors.contains("rules_load_parse"));
}
void CliEnhancementsTests::exposesCapabilitiesInSchemaAndHelp()
{
    const auto schema=call({"schema"}).result();QVERIFY(schema.value("operation_schemas").toObject().contains("set-many"));
    QVERIFY(schema.contains("stats_contract"));QVERIFY(schema.contains("consistency_rules_contract"));
    const auto help=call({"help","apply"}).result();QVERIFY(help.value("operation_schemas").toObject().value("move").toObject().contains("register_before_id_restriction"));
    QVERIFY(help.value("usage").toString().contains("--enum-tables"));
}
void CliEnhancementsTests::preflightsWithoutRejectingEarlierStateChanges()
{
    Fixture f;const auto original=read(f.path);
    const QJsonObject move{{"op","move"},{"id","status-a"},{"parent_id","block-a"},{"before_id","version-a"}};
    const auto failed=f.apply({set("version-a","description","not executed"),move},{"--preflight","--defer-generation"});
    QCOMPARE(failed.code,2);QCOMPARE(read(f.path),original);const auto failure=failed.result().value("failure").toObject();
    QCOMPARE(failure.value("stage").toString(),"preflight");QCOMPARE(failure.value("completed_operation_count").toInt(),0);
    QCOMPARE(failure.value("operation_index").toInt(),1);
    const auto allowed=f.apply({set("status-a","fixed",false),set("version-a","fixed",false),move},{"--preflight","--defer-generation"});
    QCOMPARE(allowed.code,0);
}
void CliEnhancementsTests::preservesWorkbookOrderAndRejectsInvalidOutputOptions()
{
    Fixture f;
    auto project=openProject(std::filesystem::path(f.path.toStdWString()));QVERIFY(project.workspace);QVERIFY(project.manifest);
    auto& fields=project.workspace->addressSpaces[0].blocks[0].registers[0].fields;
    std::reverse(fields.begin(),fields.end());
    auto& registers=project.workspace->addressSpaces[0].blocks[0].registers;
    std::reverse(registers.begin(),registers.end());
    QVERIFY(saveProjectFile(*project.manifest,*project.workspace).empty());
    QCOMPARE(call({"generate",f.path,"--target","xlsx"}).code,0);
    QXlsx::Document full(f.xlsx);QString page;
    for(const auto& sheet:full.sheetNames()) if(sheet.contains("Page_a")) page=sheet;
    QVERIFY(!page.isEmpty());QVERIFY(full.selectSheet(page));
    int statusRow=0,versionRow=0,firstField=0;
    for(int row=1;row<=full.dimension().lastRow();++row) {
        const auto name=full.read(row,3).toString();
        if(name=="STATUS") statusRow=row;
        if(name=="VERSION") versionRow=row;
        if(name.startsWith('F') && firstField==0) firstField=row;
    }
    QVERIFY(statusRow<versionRow);QCOMPARE(full.read(firstField,3).toString(),"F5");
    QCOMPARE(f.apply({}, {"--enum-tables","consecutive","--target","xlsx"}).code,0);
    QXlsx::Document compact(f.xlsx);QVERIFY(compact.selectSheet(page));QCOMPARE(compact.read(firstField,3).toString(),"F5");
    QMap<QString,int> rows;for(int row=1;row<=compact.dimension().lastRow();++row) rows.insert(compact.read(row,3).toString(),row);
    QVERIFY(compact.read(rows["F1"],10).toString().contains(QStringLiteral("See J%1").arg(rows["F2"])));
    project=openProject(std::filesystem::path(f.path.toStdWString()));project.manifest->targets[0].options["enum_tables"]="unknown";
    QVERIFY(saveProjectFile(*project.manifest,*project.workspace).empty());const auto before=read(f.xlsx);
    const auto failed=call({"generate",f.path,"--target","xlsx","--stats"});QCOMPARE(failed.code,1);QCOMPARE(read(f.xlsx),before);
    QVERIFY(failed.root.value("stats").toObject().value("stages").toArray().size()>0);
}
QTEST_MAIN(CliEnhancementsTests)
#include "cli_enhancements_tests.moc"
