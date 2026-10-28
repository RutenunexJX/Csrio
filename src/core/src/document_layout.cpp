#include "regmap/core/document_layout.hpp"
#include <algorithm>
#include <limits>
#include <tuple>

namespace regmap {
std::vector<const Register*> documentRegisters(const RegisterBlock& block, bool preserveUnrankedOrder)
{
    std::vector<const Register*> result;
    for (const auto& reg : block.registers) result.push_back(&reg);
    if (preserveUnrankedOrder && std::ranges::none_of(result, [](const Register* reg) { return reg->displayOrder.has_value(); })) return result;
    std::ranges::sort(result, [](const Register* a, const Register* b) {
        return std::tuple{a->displayOrder.value_or(std::numeric_limits<std::uint64_t>::max()), a->offset, a->id}
             < std::tuple{b->displayOrder.value_or(std::numeric_limits<std::uint64_t>::max()), b->offset, b->id};
    });
    return result;
}
std::vector<const Field*> documentFields(const std::vector<Field>& fields)
{
    std::vector<const Field*> result;
    for (const auto& field : fields) result.push_back(&field);
    std::ranges::sort(result, [](const Field* a, const Field* b) {
        return std::tuple{a->lsb, a->msb, a->id} < std::tuple{b->lsb, b->msb, b->id};
    });
    return result;
}
namespace {
bool sameEnums(const Field& a, const Field& b)
{
    if (a.type != b.type || a.width() != b.width() || a.enumValues.empty() ||
        a.enumValues.size() != b.enumValues.size() || !a.members.empty() || !b.members.empty() ||
        a.minimumValue || a.maximumValue || b.minimumValue || b.maximumValue) return false;
    using Value = std::tuple<std::string, std::string, std::string>;
    auto values = [](const Field& field) {
        std::vector<Value> result;
        for (const auto& value : field.enumValues)
            result.emplace_back(value.name, value.value.toHexString(), value.description);
        std::ranges::sort(result);
        return result;
    };
    return values(a) == values(b);
}
}
std::map<ObjectId, ObjectId> consecutiveEnumReferences(const std::vector<const Field*>& orderedFields)
{
    std::map<ObjectId, ObjectId> result;
    const Field* previous = nullptr;
    const Field* first = nullptr;
    for (const Field* field : orderedFields) {
        if (previous && (std::uint64_t(previous->msb) + 1 == field->lsb || std::uint64_t(field->msb) + 1 == previous->lsb) && sameEnums(*previous, *field)) {
            result.emplace(field->id, first->id);
        } else first = field;
        previous = field;
    }
    return result;
}
} // namespace regmap
