#pragma once
#include "core.hpp"
namespace neko {
// Provider requests are injected so dependency resolution is testable offline.
using ModMetadata = std::function<QJsonValue(const QString &)>;
using ModDownload = std::function<void(const Json &, const QString &)>;
Json installResolvedMods(
    const QString &root, const Json &instance, const Json &plan, const ModDownload &download,
    const std::function<void()> &checkpoint = [] {});
Json resolveMods(const Json &instance, const Json &project, bool curse, const ModMetadata &fetch);
} // namespace neko
