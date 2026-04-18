#pragma once

#include <QHash>
#include <QString>

namespace ais::ui {

struct RenderedMarkdown {
    QString html;
    QHash<QString, QString> copyPayloads;
};

[[nodiscard]] RenderedMarkdown renderMarkdownWithCodeTools(const QString& markdown,
                                                           const QString& theme,
                                                           int* copyCounter);

}  // namespace ais::ui
