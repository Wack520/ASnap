#include "ui/chat/code_syntax_highlighter.h"

#include <QColor>
#include <QFont>
#include <QTextCharFormat>
#include <QTextDocument>

namespace ais::ui {

namespace {

[[nodiscard]] QTextCharFormat makeFormat(const QColor& color, bool bold = false, bool italic = false) {
    QTextCharFormat format;
    format.setForeground(color);
    if (bold) {
        format.setFontWeight(QFont::Bold);
    }
    format.setFontItalic(italic);
    return format;
}

[[nodiscard]] QColor pick(bool darkTheme, const QColor& dark, const QColor& light) {
    return darkTheme ? dark : light;
}

}  // namespace

CodeSyntaxHighlighter::CodeSyntaxHighlighter(QTextDocument* document,
                                             QString language,
                                             const bool darkTheme)
    : QSyntaxHighlighter(document),
      language_(std::move(language).trimmed().toLower()) {
    initializeRules(darkTheme);
}

void CodeSyntaxHighlighter::highlightBlock(const QString& text) {
    for (const HighlightRule& rule : rules_) {
        auto iterator = rule.pattern.globalMatch(text);
        while (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    if (!multiLineCommentStart_.pattern().isEmpty() &&
        !multiLineCommentEnd_.pattern().isEmpty()) {
        setCurrentBlockState(0);

        int startIndex = previousBlockState() == 1 ? 0 : text.indexOf(multiLineCommentStart_);
        while (startIndex >= 0) {
            const QRegularExpressionMatch endMatch = multiLineCommentEnd_.match(text, startIndex);
            int endIndex = endMatch.capturedStart();
            int commentLength = 0;
            if (endIndex < 0) {
                setCurrentBlockState(1);
                commentLength = text.length() - startIndex;
            } else {
                commentLength = endIndex - startIndex + endMatch.capturedLength();
            }

            setFormat(startIndex, commentLength, multiLineCommentFormat_);
            startIndex = text.indexOf(multiLineCommentStart_, startIndex + commentLength);
        }
    }
}

void CodeSyntaxHighlighter::initializeRules(const bool darkTheme) {
    const QTextCharFormat keywordFormat = makeFormat(
        pick(darkTheme, QColor("#ff7b72"), QColor("#cf222e")), true);
    const QTextCharFormat stringFormat = makeFormat(
        pick(darkTheme, QColor("#a5d6ff"), QColor("#0a3069")));
    const QTextCharFormat commentFormat = makeFormat(
        pick(darkTheme, QColor("#8b949e"), QColor("#6e7781")), false, true);
    const QTextCharFormat numberFormat = makeFormat(
        pick(darkTheme, QColor("#79c0ff"), QColor("#0550ae")));
    const QTextCharFormat functionFormat = makeFormat(
        pick(darkTheme, QColor("#d2a8ff"), QColor("#8250df")));
    const QTextCharFormat booleanFormat = makeFormat(
        pick(darkTheme, QColor("#ffa657"), QColor("#953800")), true);

    QStringList keywords;
    if (isCppLikeLanguage()) {
        keywords = {
            QStringLiteral("auto"), QStringLiteral("bool"), QStringLiteral("break"), QStringLiteral("case"),
            QStringLiteral("catch"), QStringLiteral("class"), QStringLiteral("const"), QStringLiteral("constexpr"),
            QStringLiteral("continue"), QStringLiteral("default"), QStringLiteral("delete"), QStringLiteral("do"),
            QStringLiteral("else"), QStringLiteral("enum"), QStringLiteral("explicit"), QStringLiteral("false"),
            QStringLiteral("final"), QStringLiteral("for"), QStringLiteral("if"), QStringLiteral("inline"),
            QStringLiteral("namespace"), QStringLiteral("new"), QStringLiteral("noexcept"), QStringLiteral("nullptr"),
            QStringLiteral("override"), QStringLiteral("private"), QStringLiteral("protected"), QStringLiteral("public"),
            QStringLiteral("return"), QStringLiteral("static"), QStringLiteral("struct"), QStringLiteral("switch"),
            QStringLiteral("template"), QStringLiteral("this"), QStringLiteral("throw"), QStringLiteral("true"),
            QStringLiteral("try"), QStringLiteral("typename"), QStringLiteral("using"), QStringLiteral("virtual"),
            QStringLiteral("void"), QStringLiteral("while"),
        };
        multiLineCommentStart_ = QRegularExpression(QStringLiteral(R"(/\*)"));
        multiLineCommentEnd_ = QRegularExpression(QStringLiteral(R"(\*/)"));
    } else if (isPythonLikeLanguage()) {
        keywords = {
            QStringLiteral("and"), QStringLiteral("as"), QStringLiteral("assert"), QStringLiteral("async"),
            QStringLiteral("await"), QStringLiteral("break"), QStringLiteral("class"), QStringLiteral("continue"),
            QStringLiteral("def"), QStringLiteral("elif"), QStringLiteral("else"), QStringLiteral("except"),
            QStringLiteral("False"), QStringLiteral("finally"), QStringLiteral("for"), QStringLiteral("from"),
            QStringLiteral("if"), QStringLiteral("import"), QStringLiteral("in"), QStringLiteral("is"),
            QStringLiteral("lambda"), QStringLiteral("None"), QStringLiteral("nonlocal"), QStringLiteral("not"),
            QStringLiteral("or"), QStringLiteral("pass"), QStringLiteral("raise"), QStringLiteral("return"),
            QStringLiteral("True"), QStringLiteral("try"), QStringLiteral("while"), QStringLiteral("with"),
            QStringLiteral("yield"),
        };
    } else if (isJsonLikeLanguage()) {
        keywords = {
            QStringLiteral("true"),
            QStringLiteral("false"),
            QStringLiteral("null"),
        };
    } else {
        keywords = {
            QStringLiteral("const"), QStringLiteral("let"), QStringLiteral("var"), QStringLiteral("function"),
            QStringLiteral("return"), QStringLiteral("if"), QStringLiteral("else"), QStringLiteral("for"),
            QStringLiteral("while"), QStringLiteral("class"), QStringLiteral("extends"), QStringLiteral("new"),
            QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("true"), QStringLiteral("false"),
            QStringLiteral("null"), QStringLiteral("undefined"), QStringLiteral("async"), QStringLiteral("await"),
            QStringLiteral("try"), QStringLiteral("catch"), QStringLiteral("finally"),
        };
        multiLineCommentStart_ = QRegularExpression(QStringLiteral(R"(/\*)"));
        multiLineCommentEnd_ = QRegularExpression(QStringLiteral(R"(\*/)"));
    }

    if (!keywords.isEmpty()) {
        rules_.append(HighlightRule{
            .pattern = QRegularExpression(QStringLiteral(R"(\b(%1)\b)").arg(keywords.join('|'))),
            .format = keywordFormat,
        });
    }

    rules_.append(HighlightRule{
        .pattern = QRegularExpression(QStringLiteral(R"("(?:[^"\\]|\\.)*")")),
        .format = stringFormat,
    });
    rules_.append(HighlightRule{
        .pattern = QRegularExpression(QStringLiteral(R"('(?:[^'\\]|\\.)*')")),
        .format = stringFormat,
    });
    rules_.append(HighlightRule{
        .pattern = QRegularExpression(QStringLiteral(R"(\b\d+(?:\.\d+)?\b)")),
        .format = numberFormat,
    });
    rules_.append(HighlightRule{
        .pattern = QRegularExpression(QStringLiteral(R"(\b[A-Za-z_][A-Za-z0-9_]*(?=\())")),
        .format = functionFormat,
    });

    if (isPythonLikeLanguage()) {
        rules_.append(HighlightRule{
            .pattern = QRegularExpression(QStringLiteral(R"(#.*$)")),
            .format = commentFormat,
        });
    } else if (!isJsonLikeLanguage()) {
        rules_.append(HighlightRule{
            .pattern = QRegularExpression(QStringLiteral(R"(//.*$)")),
            .format = commentFormat,
        });
    }

    if (isJsonLikeLanguage()) {
        rules_.append(HighlightRule{
            .pattern = QRegularExpression(QStringLiteral(R"(\b(true|false|null)\b)")),
            .format = booleanFormat,
        });
    }

    multiLineCommentFormat_ = commentFormat;
}

bool CodeSyntaxHighlighter::isCppLikeLanguage() const {
    return language_ == QStringLiteral("c") ||
           language_ == QStringLiteral("cc") ||
           language_ == QStringLiteral("cpp") ||
           language_ == QStringLiteral("cxx") ||
           language_ == QStringLiteral("h") ||
           language_ == QStringLiteral("hpp") ||
           language_ == QStringLiteral("java") ||
           language_ == QStringLiteral("csharp") ||
           language_ == QStringLiteral("cs") ||
           language_ == QStringLiteral("rust") ||
           language_ == QStringLiteral("go");
}

bool CodeSyntaxHighlighter::isPythonLikeLanguage() const {
    return language_ == QStringLiteral("py") ||
           language_ == QStringLiteral("python") ||
           language_ == QStringLiteral("bash") ||
           language_ == QStringLiteral("sh");
}

bool CodeSyntaxHighlighter::isJsonLikeLanguage() const {
    return language_ == QStringLiteral("json") ||
           language_ == QStringLiteral("yaml") ||
           language_ == QStringLiteral("yml");
}

}  // namespace ais::ui
