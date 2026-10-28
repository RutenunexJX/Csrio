#pragma once
#include "regmap/core/model.hpp"
#include <map>

namespace regmap {
// Document order is independent of allocation and the editable model's vector order.
[[nodiscard]] std::vector<const Register*> documentRegisters(const RegisterBlock& block, bool preserveUnrankedOrder = false);
[[nodiscard]] std::vector<const Field*> documentFields(const std::vector<Field>& fields);
// References are confined to adjacent bit ranges under the same parent.
[[nodiscard]] std::map<ObjectId, ObjectId> consecutiveEnumReferences(const std::vector<const Field*>& orderedFields);
} // namespace regmap
