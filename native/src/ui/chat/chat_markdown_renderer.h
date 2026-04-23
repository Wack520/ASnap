#pragma once

#include <QHash>
#include <QString>

namespace ais::ui {

struct RenderedMarkdown {
    QString html;
    QHash<QString, QString> copyPayloads;
};

enum class MarkdownRenderMode {
    Full,
    PlainTextPreview,
};

[[nodiscard]] RenderedMarkdown renderMarkdownWithCodeTools(const QString& markdown,
                                                           const QString& theme,
                                                           int* copyCounter,
                                                           MarkdownRenderMode mode =
                                                               MarkdownRenderMode::Full);

void resetMarkdownRenderCallCountForTest();
[[nodiscard]] int markdownRenderCallCountForTest();

}  // namespace ais::ui
