#include "ui/chat/chat_markdown_renderer.h"

#include <QRegularExpression>
#include <QTextDocument>

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

[[nodiscard]] QString languageLabel(QString language) {
    language = language.trimmed();
    if (language.isEmpty()) {
        return QStringLiteral("code");
    }
    return language.toLower();
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
    return extractBodyHtml(document.toHtml());
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
               "<div class=\"code-body\"><pre>%3</pre></div>"
               "</div>")
        .arg(languageText, blockId, highlightedCodeHtml(code, language, theme));
}

}  // namespace

RenderedMarkdown renderMarkdownWithCodeTools(const QString& markdown,
                                             const QString& theme,
                                             int* copyCounter) {
    RenderedMarkdown rendered;

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
