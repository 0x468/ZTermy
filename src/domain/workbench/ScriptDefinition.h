#pragma once

#include "domain/workbench/QuickCommand.h"

#include <cstdint>
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace ztermy::workbench
{

inline constexpr std::size_t maximumScriptCount = 256;
inline constexpr std::size_t maximumScriptVariableCount = 32;
inline constexpr std::size_t maximumScriptStepCount = 32;
inline constexpr std::size_t maximumScriptStepBytes = std::size_t{8} * 1024;
inline constexpr std::size_t maximumRenderedScriptBytes = std::size_t{64} * 1024;
inline constexpr std::size_t maximumOutputMarkerBytes = 1024;
inline constexpr std::size_t maximumOutputMatchWindowBytes = std::size_t{64} * 1024;
inline constexpr std::uint32_t minimumOutputTimeoutMs = 1'000;
inline constexpr std::uint32_t maximumOutputTimeoutMs = 300'000;

enum class ScriptVariableType : std::uint8_t
{
    text,
    integer,
    boolean,
    choice,
};

enum class ScriptContinuation : std::uint8_t
{
    immediate,
    literalOutput,
};

struct ScriptVariable final
{
    std::string name;
    std::string label;
    ScriptVariableType type = ScriptVariableType::text;
    std::string defaultValue;
    std::vector<std::string> choices;
    bool required = false;

    bool operator==(const ScriptVariable &) const = default;
};

struct ScriptStep final
{
    std::string command;
    ScriptContinuation continuation = ScriptContinuation::immediate;
    std::string outputMarker;
    std::uint32_t timeoutMs = 30'000;

    bool operator==(const ScriptStep &) const = default;
};

struct ScriptDefinition final
{
    std::string id;
    std::string name;
    std::string description;
    ShellScope shellScope = ShellScope::any;
    std::vector<ScriptVariable> variables;
    std::vector<ScriptStep> steps;
    std::int64_t createdUtcMs = 0;
    std::int64_t modifiedUtcMs = 0;

    bool operator==(const ScriptDefinition &) const = default;
};

struct RenderedScriptStep final
{
    std::string command;
    ScriptContinuation continuation = ScriptContinuation::immediate;
    std::string outputMarker;
    std::uint32_t timeoutMs = 30'000;

    bool operator==(const RenderedScriptStep &) const = default;
};

struct RenderedScript final
{
    std::string id;
    std::string name;
    std::vector<RenderedScriptStep> steps;

    bool operator==(const RenderedScript &) const = default;
};

enum class ScriptRenderError : std::uint8_t
{
    invalidDefinition,
    missingVariable,
    invalidVariableValue,
    invalidTemplate,
    unknownTemplateVariable,
    renderedTooLarge,
};

using ScriptVariableValues = std::unordered_map<std::string, std::string>;

[[nodiscard]] bool validScriptVariableName(const std::string &name) noexcept;
[[nodiscard]] bool validScriptVariableValue(const ScriptVariable &variable, const std::string &value) noexcept;
[[nodiscard]] bool validScriptDefinition(const ScriptDefinition &script) noexcept;
[[nodiscard]] std::expected<RenderedScript, ScriptRenderError> renderScript(const ScriptDefinition &script,
                                                                            const ScriptVariableValues &values);
[[nodiscard]] ScriptDefinition scriptFromQuickCommand(const QuickCommand &quickCommand);

} // namespace ztermy::workbench
