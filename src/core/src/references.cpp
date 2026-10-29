#include "regmap/core/references.hpp"
#include "regmap/core/consistency.hpp"

#include <QString>
#include <QStringList>
#include <algorithm>
#include <set>

namespace regmap {
namespace {
bool identifierCharacter(QChar character)
{
    const auto value = character.unicode();
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
        (value >= '0' && value <= '9') || value == '_';
}

bool bounded(const QString& text, qsizetype start, qsizetype length)
{
    return (start == 0 || !identifierCharacter(text.at(start - 1)) || !identifierCharacter(text.at(start))) &&
        (start + length == text.size() || !identifierCharacter(text.at(start + length)) || !identifierCharacter(text.at(start + length - 1)));
}

std::vector<ObjectId> resolveName(const InspectedModel& symbols, const InspectedModel& sources,
    const ObjectId& source, const std::vector<ObjectId>& named, const QString& text, qsizetype position, qsizetype length)
{
    // A qualified path binds to its ancestor names, not the source's scope.
    auto prefix = position;
    while (prefix > 0 && text.at(prefix - 1).isSpace()) --prefix;
    if (prefix > 0 && (text.at(prefix - 1) == '.' || text.at(prefix - 1) == '/')) {
        std::vector<ObjectId> matches;
        for (const auto& candidate : named) {
            QStringList names;
            for (auto id = candidate; !id.empty() && symbols.contains(id); id = inspectedProperty(symbols.at(id), "parent"))
                names.prepend(QString::fromStdString(inspectedProperty(symbols.at(id), "name")));
            bool matched = false;
            for (qsizetype count = 2; count <= names.size() && !matched; ++count) {
                for (const auto* separator : {".","/"," / "}) {
                    const auto path = names.sliced(names.size() - count).join(QString::fromLatin1(separator));
                    const auto start = position + length - path.size();
                    auto preceding = start;
                    while (preceding > 0 && text.at(preceding - 1).isSpace()) --preceding;
                    if (start >= 0 && text.sliced(start,path.size()) == path && bounded(text,start,path.size()) &&
                        (preceding == 0 || (text.at(preceding - 1) != '.' && text.at(preceding - 1) != '/'))) {
                        matched = true; break;
                    }
                }
            }
            if (matched) matches.push_back(candidate);
        }
        return matches;
    }
    for (auto scope = source; !scope.empty();) {
        std::vector<ObjectId> matches;
        for (const auto& candidate : named)
            if (candidate == scope || inspectedProperty(symbols.at(candidate), "parent") == scope) matches.push_back(candidate);
        if (!matches.empty()) return matches;
        for (const auto& candidate : named) {
            for (auto parent = inspectedProperty(symbols.at(candidate),"parent");
                 !parent.empty() && symbols.contains(parent); parent = inspectedProperty(symbols.at(parent),"parent")) {
                if (parent == scope) { matches.push_back(candidate); break; }
            }
        }
        if (!matches.empty()) return matches;
        const auto found = sources.find(scope);
        scope = found == sources.end() ? std::string{} : inspectedProperty(found->second,"parent");
    }
    return named; // Cross-scope unqualified names remain explicitly ambiguous.
}

std::vector<ReferenceFinding> scan(const InspectedModel& symbols, const InspectedModel& sources,
    const std::vector<ObjectId>& targets)
{
    std::vector<ReferenceFinding> result;
    std::map<std::string,std::vector<ObjectId>,std::less<>> byName;
    for (const auto& [id, object] : symbols) byName[inspectedProperty(object,"name")].push_back(id);
    const std::set<ObjectId> selected(targets.begin(),targets.end());
    std::set<std::string> names;
    for (const auto& id : targets) if (symbols.contains(id)) names.insert(inspectedProperty(symbols.at(id),"name"));
    const auto finding = [&](const ObjectId& target, const ObjectId& source, const char* kind,
                             const char* property, const std::string& snippet, const std::vector<ObjectId>& candidates) {
        ReferenceFinding entry;
        entry.targetId = target; entry.targetPath = inspectedPath(symbols,target);
        entry.targetName = inspectedProperty(symbols.at(target),"name"); entry.change = "inspect";
        entry.objectId = source; entry.path = inspectedPath(sources,source);
        entry.property = property; entry.kind = kind; entry.status = "present"; entry.snippet = snippet;
        entry.candidates = candidates; entry.ambiguous = candidates.size() > 1;
        return entry;
    };
    for (const auto& [id, object] : sources) {
        const auto parent = inspectedProperty(object,"parent");
        if (selected.contains(parent)) result.push_back(finding(parent,id,"structured","parent",parent,{parent}));
        const auto text = QString::fromStdString(inspectedProperty(object,"description"));
        if (text.isEmpty()) continue;
        for (const auto& name : names) {
            if (name.empty()) continue;
            const auto needle = QString::fromStdString(name);
            qsizetype position = 0;
            while ((position = text.indexOf(needle,position)) >= 0) {
                const auto current = position;
                position += needle.size();
                if (!bounded(text,current,needle.size())) continue;
                const auto candidates = resolveName(symbols,sources,id,byName.at(name),text,current,needle.size());
                const auto start = std::max<qsizetype>(0,current - 40);
                const auto snippet = text.sliced(start,std::min<qsizetype>(text.size()-start,needle.size()+80)).toStdString();
                for (const auto& target : candidates)
                    if (target != id && selected.contains(target)) result.push_back(finding(target,id,"text","description",snippet,candidates));
            }
        }
    }
    return result;
}
}

std::vector<ReferenceFinding> findReferences(const Workspace& workspace, const std::vector<ObjectId>& targets)
{
    const auto model = inspectModel(workspace);
    return scan(model,model,targets);
}

std::vector<ReferenceFinding> checkReferenceChanges(const Workspace& before, const Workspace& after)
{
    const auto original = inspectModel(before);
    const auto current = inspectModel(after);
    std::vector<ObjectId> targets;
    for (const auto& [id, object] : original)
        if (!current.contains(id) || inspectedProperty(object,"name") != inspectedProperty(current.at(id),"name")) targets.push_back(id);
    if (targets.empty()) return {};
    auto symbols = original;
    for (const auto& [id, object] : current) if (!symbols.contains(id)) symbols.emplace(id,object);
    auto remaining = scan(symbols,current,targets);
    auto previous = scan(original,original,targets);
    for (auto& entry : remaining) {
        entry.change = current.contains(entry.targetId) ? "rename" : "delete";
        entry.status = entry.kind == "structured" ? "preserved" : "remaining";
    }
    for (auto& entry : previous) {
        const bool stillPresent = std::ranges::any_of(remaining,[&](const ReferenceFinding& item) {
            return item.targetId == entry.targetId && item.objectId == entry.objectId && item.property == entry.property;
        });
        if (stillPresent) continue;
        entry.change = current.contains(entry.targetId) ? "rename" : "delete";
        entry.status = current.contains(entry.objectId) ? "resolved" : "source_removed";
        remaining.push_back(std::move(entry));
    }
    return remaining;
}
} // namespace regmap
