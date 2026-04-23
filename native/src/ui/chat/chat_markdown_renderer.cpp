#include "ui/chat/chat_markdown_renderer.h"

#include <atomic>
#include <optional>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

#include "ui/chat/code_syntax_highlighter.h"

namespace ais::ui {

namespace {

std::atomic<int> g_markdownRenderCallCount = 0;

struct TrailingOpenFence {
    qsizetype fenceStart = 0;
    QString language;
    QString code;
};

[[nodiscard]] QString extractBodyHtml(QString html) {
    static const QRegularExpression bodyPattern(
        QStringLiteral(R"(<body[^>]*>([\s\S]*)</body>)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = bodyPattern.match(html);
    if (match.hasMatch()) {
        html = match.captured(1).trimmed();
    }
    return html;
}

[[nodiscard]] QString markdownFragmentToHtml(const QString& markdown) {
    if (markdown.trimmed().isEmpty()) {
        return {};
    }

    QTextDocument document;
    document.setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
    return extractBodyHtml(document.toHtml());
}

[[nodiscard]] QString normalizedMarkdown(QString markdown) {
    markdown.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    markdown.replace(QChar::CarriageReturn, QChar::LineFeed);
    return markdown;
}

[[nodiscard]] QString plainTextPreviewHtml(QString markdown) {
    if (markdown.isEmpty()) {
        return {};
    }

    markdown = normalizedMarkdown(std::move(markdown));
    return QStringLiteral("<div class=\"streaming-plain\">%1</div>")
        .arg(markdown.toHtmlEscaped());
}

[[nodiscard]] QString languageLabel(QString language) {
    language = language.trimmed();
    if (language.isEmpty()) {
        return QStringLiteral("code");
    }
    return language.toLower();
}

[[nodiscard]] QString inlineStyleForFragment(const QTextCharFormat& format) {
    QStringList rules;
    if (format.foreground().style() != Qt::NoBrush) {
        const QColor color = format.foreground().color();
        if (color.isValid()) {
            rules.append(QStringLiteral("color:%1").arg(color.name()));
        }
    }
    if (format.fontWeight() >= QFont::Bold) {
        rules.append(QStringLiteral("font-weight:700"));
    }
    if (format.fontItalic()) {
        rules.append(QStringLiteral("font-style:italic"));
    }
    return rules.join(QStringLiteral(";"));
}

[[nodiscard]] QString fragmentHtml(const QTextFragment& fragment) {
    if (!fragment.isValid()) {
        return {};
    }

    const QString text = fragment.text().toHtmlEscaped();
    if (text.isEmpty()) {
        return {};
    }

    const QString style = inlineStyleForFragment(fragment.charFormat());
    if (style.isEmpty()) {
        return text;
    }

    return QStringLiteral("<span style=\"%1\">%2</span>").arg(style, text);
}

[[nodiscard]] QString highlightedCodeHtml(const QString& code,
                                          const QString& language,
                                          const QString& theme) {
    QTextDocument document;
    document.setPlainText(code);
    CodeSyntaxHighlighter highlighter(
        &document,
        language,
        theme != QStringLiteral("light"));
    Q_UNUSED(highlighter);
    highlighter.rehighlight();

    QString html = QStringLiteral("<pre><code>");
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            html += fragmentHtml(it.fragment());
        }
        if (block.next().isValid()) {
            html += QChar::LineFeed;
        }
    }
    html += QStringLiteral("</code></pre>");
    return html;
}

[[nodiscard]] QString codeBlockHtml(const QString& blockId,
                                    const QString& code,
                                    const QString& language,
                                    const QString& theme) {
    const QString languageText = languageLabel(language);
    return QStringLiteral(
               "<div class=\"code-card\">"
               "<div class=\"code-toolbar\">"
               "<span class=\"code-language\">%1</span>"
               "<a class=\"code-copy\" href=\"copy-code://%2\">Copy</a>"
               "</div>"
               "<div class=\"code-body\">%3</div>"
               "</div>")
        .arg(languageText, blockId, highlightedCodeHtml(code, language, theme));
}

[[nodiscard]] std::optional<TrailingOpenFence> findTrailingOpenFence(const QString& markdown) {
    static const QRegularExpression openFencePattern(
        QStringLiteral(R"((^|\n)```([^\n`]*)\n)")
    );

    QRegularExpressionMatch lastMatch;
    auto iterator = openFencePattern.globalMatch(markdown);
    while (iterator.hasNext()) {
        lastMatch = iterator.next();
    }

    if (!lastMatch.hasMatch()) {
        return std::nullopt;
    }

    return TrailingOpenFence{
        .fenceStart = lastMatch.capturedStart(0),
        .language = lastMatch.captured(2).trimmed(),
        .code = markdown.mid(lastMatch.capturedEnd(0)),
    };
}

}  // namespace

RenderedMarkdown renderMarkdownWithCodeTools(const QString& markdown,
                                             const QString& theme,
                                             int* copyCounter,
                                             const MarkdownRenderMode mode) {
    g_markdownRenderCallCount.fetch_add(1, std::memory_order_relaxed);
    RenderedMarkdown rendered;
    if (mode == MarkdownRenderMode::PlainTextPreview) {
        rendered.html = plainTextPreviewHtml(markdown);
        return rendered;
    }

    const QString normalized = normalizedMarkdown(markdown);
    static const QRegularExpression fencePattern(
        QStringLiteral(R"(```([^\n`]*)\n([\s\S]*?)\n?```)")
    );

    int cursor = 0;
    const auto matches = fencePattern.globalMatch(normalized);
    auto iterator = matches;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const int start = match.capturedStart();
        const int end = match.capturedEnd();

        rendered.html += markdownFragmentToHtml(normalized.mid(cursor, start - cursor));

        const QString language = match.captured(1).trimmed();
        QString code = match.captured(2);
        if (code.endsWith(QChar::LineFeed)) {
            code.chop(1);
        }

        const QString blockId = QStringLiteral("code-%1").arg(copyCounter ? (*copyCounter)++ : 0);
        rendered.copyPayloads.insert(blockId, code);
        rendered.html += codeBlockHtml(blockId, code, language, theme);
        cursor = end;
    }

    const QString trailingMarkdown = normalized.mid(cursor);
    if (const auto trailingFence = findTrailingOpenFence(trailingMarkdown); trailingFence.has_value()) {
        rendered.html += markdownFragmentToHtml(
            trailingMarkdown.left(trailingFence->fenceStart));
        const QString blockId = QStringLiteral("code-%1").arg(copyCounter ? (*copyCounter)++ : 0);
        rendered.copyPayloads.insert(blockId, trailingFence->code);
        rendered.html += codeBlockHtml(blockId,
                                       trailingFence->code,
                                       trailingFence->language,
                                       theme);
    } else {
        rendered.html += markdownFragmentToHtml(trailingMarkdown);
    }
    return rendered;
}

void resetMarkdownRenderCallCountForTest() {
    g_markdownRenderCallCount.store(0, std::memory_order_relaxed);
}

int markdownRenderCallCountForTest() {
    return g_markdownRenderCallCount.load(std::memory_order_relaxed);
}

}  // namespace ais::ui
