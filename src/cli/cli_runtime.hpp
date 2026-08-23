#pragma once

namespace regmap::cli {

[[nodiscard]] bool commandRequiresGuiApplication(
    int argc,
    char** argv);

} // namespace regmap::cli
