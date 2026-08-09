#include "domain/workbench/ScriptDefinition.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>

namespace
{

constexpr std::size_t maximumIdBytes = 64;
constexpr std::size_t maximumNameBytes = 128;
constexpr std::size_t maximumDescriptionBytes = 1024;
constexpr std::size_t maximumVariableLabelBytes = 128;
constexpr std::size_t maximumVariableValueBytes = 4096;
constexpr std::size_t maximumChoiceCount = 32;
constexpr std::size_t maximumChoiceBytes = 256;

[[nodiscard]] bool containsDisallowedControl(const std::string_view value, const bool multiline) noexcept
{
    return std::ranges::any_of(value, [multiline](const unsigned char character) {
        if (character == 0x7F || character == 0x1B)
        {
            return true;
        }
        if (character >= 0x20)
        {
            return false;
        }
        return !multiline || (character != '\t' && character != '\n' && character != '\r');
    });
}

[[nodiscard]] bool validText(const std::string_view value, const std::size_t maximumBytes,
                             const bool multiline = false) noexcept
{
    return value.size() <= maximumBytes && !containsDisallowedControl(value, multiline);
}

[[nodiscard]] bool validInteger(const std::string_view value) noexcept
{
    if (value.empty())
    {
        return false;
    }
    std::int64_t parsed = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size();
}

[[nodiscard]] bool uniqueChoices(const std::vector<std::string> &choices) noexcept
{
    for (std::size_t index = 0; index < choices.size(); ++index)
    {
        if (std::ranges::find(choices | std::views::drop(index + 1), choices[index]) != choices.end())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool uniqueVariableNames(const std::vector<ztermy::workbench::ScriptVariable> &variables) noexcept
{
    for (std::size_t index = 0; index < variables.size(); ++index)
    {
        if (std::ranges::find(variables | std::views::drop(index + 1), variables[index].name,
                              &ztermy::workbench::ScriptVariable::name)
            != variables.end())
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::expected<std::string, ztermy::workbench::ScriptRenderError>
renderTemplate(const std::string_view source, const ztermy::workbench::ScriptVariableValues &values)
{
    std::string result;
    result.reserve(source.size());
    std::size_t cursor = 0;
    while (cursor < source.size())
    {
        const std::size_t opening = source.find("${", cursor);
        if (opening == std::string_view::npos)
        {
            result.append(source.substr(cursor));
            break;
        }
        result.append(source.substr(cursor, opening - cursor));
        const std::size_t closing = source.find('}', opening + 2);
        if (closing == std::string_view::npos)
        {
            return std::unexpected(ztermy::workbench::ScriptRenderError::invalidTemplate);
        }
        const std::string name(source.substr(opening + 2, closing - opening - 2));
        if (!ztermy::workbench::validScriptVariableName(name))
        {
            return std::unexpected(ztermy::workbench::ScriptRenderError::invalidTemplate);
        }
        const auto value = values.find(name);
        if (value == values.end())
        {
            return std::unexpected(ztermy::workbench::ScriptRenderError::unknownTemplateVariable);
        }
        result.append(value->second);
        cursor = closing + 1;
    }
    return result;
}

} // namespace

namespace ztermy::workbench
{

bool validScriptVariableName(const std::string &name) noexcept
{
    if (name.empty() || name.size() > 64
        || !((name.front() >= 'A' && name.front() <= 'Z') || (name.front() >= 'a' && name.front() <= 'z')
             || name.front() == '_'))
    {
        return false;
    }
    return std::ranges::all_of(name | std::views::drop(1), [](const unsigned char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
               || (character >= '0' && character <= '9') || character == '_';
    });
}

bool validScriptVariableValue(const ScriptVariable &variable, const std::string &value) noexcept
{
    if (!validText(value, maximumVariableValueBytes, true) || (variable.required && value.empty()))
    {
        return false;
    }
    if (value.empty())
    {
        return !variable.required;
    }
    switch (variable.type)
    {
        case ScriptVariableType::integer:
            return validInteger(value);
        case ScriptVariableType::boolean:
            return value == "true" || value == "false";
        case ScriptVariableType::choice:
            return std::ranges::find(variable.choices, value) != variable.choices.end();
        case ScriptVariableType::text:
        default:
            return true;
    }
}

bool validScriptDefinition(const ScriptDefinition &script) noexcept
{
    if (script.id.empty() || !validText(script.id, maximumIdBytes) || script.name.empty()
        || !validText(script.name, maximumNameBytes) || !validText(script.description, maximumDescriptionBytes)
        || script.variables.size() > maximumScriptVariableCount || script.steps.empty()
        || script.steps.size() > maximumScriptStepCount || script.createdUtcMs < 0
        || script.modifiedUtcMs < script.createdUtcMs || !uniqueVariableNames(script.variables))
    {
        return false;
    }
    const bool validVariables = std::ranges::all_of(script.variables, [](const ScriptVariable &variable) {
        if (!validScriptVariableName(variable.name) || variable.label.empty()
            || !validText(variable.label, maximumVariableLabelBytes) || variable.choices.size() > maximumChoiceCount
            || !std::ranges::all_of(variable.choices,
                                    [](const std::string &choice) {
                                        return !choice.empty() && validText(choice, maximumChoiceBytes);
                                    })
            || !uniqueChoices(variable.choices)
            || (variable.type == ScriptVariableType::choice && variable.choices.empty())
            || (variable.type != ScriptVariableType::choice && !variable.choices.empty()))
        {
            return false;
        }
        return variable.defaultValue.empty() || validScriptVariableValue(variable, variable.defaultValue);
    });
    if (!validVariables)
    {
        return false;
    }
    return std::ranges::all_of(script.steps, [](const ScriptStep &step) {
        if (step.command.empty() || !validText(step.command, maximumScriptStepBytes, true))
        {
            return false;
        }
        if (step.continuation == ScriptContinuation::immediate)
        {
            return step.outputMarker.empty();
        }
        return !step.outputMarker.empty() && validText(step.outputMarker, maximumOutputMarkerBytes, true)
               && step.timeoutMs >= minimumOutputTimeoutMs && step.timeoutMs <= maximumOutputTimeoutMs;
    });
}

std::expected<RenderedScript, ScriptRenderError> renderScript(const ScriptDefinition &script,
                                                              const ScriptVariableValues &values)
{
    if (!validScriptDefinition(script))
    {
        return std::unexpected(ScriptRenderError::invalidDefinition);
    }
    ScriptVariableValues resolved;
    resolved.reserve(script.variables.size());
    for (const ScriptVariable &variable : script.variables)
    {
        const auto supplied = values.find(variable.name);
        const std::string &value = supplied == values.end() ? variable.defaultValue : supplied->second;
        if (supplied == values.end() && variable.required && value.empty())
        {
            return std::unexpected(ScriptRenderError::missingVariable);
        }
        if (!validScriptVariableValue(variable, value))
        {
            return std::unexpected(ScriptRenderError::invalidVariableValue);
        }
        resolved.emplace(variable.name, value);
    }

    RenderedScript rendered{.id = script.id, .name = script.name};
    rendered.steps.reserve(script.steps.size());
    std::size_t renderedBytes = 0;
    for (const ScriptStep &step : script.steps)
    {
        auto command = renderTemplate(step.command, resolved);
        if (!command)
        {
            return std::unexpected(command.error());
        }
        auto marker = renderTemplate(step.outputMarker, resolved);
        if (!marker)
        {
            return std::unexpected(marker.error());
        }
        renderedBytes += command->size() + marker->size();
        if (renderedBytes > maximumRenderedScriptBytes || marker->size() > maximumOutputMarkerBytes)
        {
            return std::unexpected(ScriptRenderError::renderedTooLarge);
        }
        rendered.steps.push_back({.command = std::move(*command),
                                  .continuation = step.continuation,
                                  .outputMarker = std::move(*marker),
                                  .timeoutMs = step.timeoutMs});
    }
    return rendered;
}

ScriptDefinition scriptFromQuickCommand(const QuickCommand &quickCommand)
{
    return {
        .id = quickCommand.id,
        .name = quickCommand.name,
        .description = quickCommand.description,
        .shellScope = quickCommand.shellScope,
        .variables = {},
        .steps = {{.command = quickCommand.command}},
        .createdUtcMs = quickCommand.createdUtcMs,
        .modifiedUtcMs = quickCommand.modifiedUtcMs,
    };
}

} // namespace ztermy::workbench
