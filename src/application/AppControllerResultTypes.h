#pragma once

#include <QMetaType>
#include "domain/ai/AiContextBroker.h"
#include "domain/ai/AiProviderTypes.h"
#include "domain/ai/AiUserSkill.h"
#include "domain/workbench/ShellHistory.h"
#include <vector>

namespace ztermy
{
using ShellHistoryEntries = std::vector<workbench::ShellHistoryEntry>;
using AiTextAttachments = std::vector<ai::AiExplicitContext>;
using AiImageAttachments = std::vector<ai::AiImageAttachment>;
using AiUserSkills = std::vector<ai::AiUserSkill>;
} // namespace ztermy
Q_DECLARE_METATYPE(ztermy::ShellHistoryEntries)
Q_DECLARE_METATYPE(ztermy::AiTextAttachments)
Q_DECLARE_METATYPE(ztermy::AiImageAttachments)
Q_DECLARE_METATYPE(ztermy::AiUserSkills)
