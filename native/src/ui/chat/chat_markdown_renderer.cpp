#include "ui/chat/chat_markdown_renderer.h"

#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

#include "ui/chat/code_syntax_highlighter.h"

namespace ais::ui {

namespace {

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

[[nodiscard]] QString plainTextPreviewHtml(QString markdown) {
    if (markdown.isEmpty()) {
        return {};
    }

    markdown.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    markdown.replace(QChar::CarriageReturn, QChar::LineFeed);
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

}  // namespace

RenderedMarkdown renderMarkdownWithCodeTools(const QString& markdown,
                                             const QString& theme,
                                             int* copyCounter,
                                             const MarkdownRenderMode mode) {
    RenderedMarkdown rendered;
    if (mode == MarkdownRenderMode::PlainTextPreview) {
        rendered.html = plainTextPreviewHtml(markdown);
        return rendered;
    }

    static const QRegularExpression fencePattern(
        QStringLiteral(R"(```([^\n`]*)\n([\s\S]*?)\n?```)")
    );

    int cursor = 0;
    const auto matches = fencePattern.globalMatch(markdown);
    auto iterator = matches;
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const int start = match.capturedStart();
        const int end = match.capturedEnd();

        rendered.html += markdownFragmentToHtml(markdown.mid(cursor, start - cursor));

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

    rendered.html += markdownFragmentToHtml(markdown.mid(cursor));
    return rendered;
}

}  // namespace ais::ui
