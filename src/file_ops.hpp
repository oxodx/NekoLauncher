#pragma once
#include <QString>
namespace neko {
// Publish a fully written sibling file without opening/truncating the old inode.
void publishFile(const QString &source, const QString &destination);
} // namespace neko
