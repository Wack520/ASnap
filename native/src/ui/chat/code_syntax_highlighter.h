#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class QTextDocument;

namespace ais::ui {

class CodeSyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

public:
    CodeSyntaxHighlighter(QTextDocument* document,
                          QString language,
                          bool darkTheme);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void initializeRules(bool darkTheme);
    [[nodiscard]] bool isCppLikeLanguage() const;
    [[nodiscard]] bool isPythonLikeLanguage() const;
    [[nodiscard]] bool isJsonLikeLanguage() const;

    QList<HighlightRule> rules_;
    QRegularExpression multiLineCommentStart_;
    QRegularExpression multiLineCommentEnd_;
    QTextCharFormat multiLineCommentFormat_;
    QString language_;
};

}  // namespace ais::ui
