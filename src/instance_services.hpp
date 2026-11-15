#pragma once
#include "core.hpp"

namespace neko {
// These services work on one instance at a time. They never serialize account credentials.
enum class CheckState { Pass, Warning, Error };
struct InstanceCheck {
    CheckState state;
    QString title;
    QString detail;
};
QList<InstanceCheck> inspectInstance(Engine &engine, const Json &instance);
QString formatDuration(qint64 seconds);
int commandScore(const QString &query, const QString &title, const QString &keywords = {});

class WorldSnapshots {
  public:
    explicit WorldSnapshots(QString root, std::function<void()> cancellation = {});
    QJsonArray list(const QString &instanceId) const;
    Json create(const QString &instanceId, bool automatic = false, const QString &reason = {});
    // Restores only saves. A separate recovery snapshot is created before the folder is replaced.
    void restore(const QString &instanceId, const QString &snapshotId);

  private:
    QString root;
    std::function<void()> check;
    QString directory(const QString &instanceId) const;
};

struct GameSession {
    qint64 startedAt = 0;
    qint64 durationSeconds = 0;
    int exitCode = 0;
    bool crashed = false;
    QString java;
    bool stopped = false;
};
void recordSession(const QString &root, const QString &instanceId, const GameSession &session);
Json sessionStatistics(const QString &root, const QString &instanceId);
QJsonArray sessionHistory(const QString &root, const QString &instanceId);
} // namespace neko
