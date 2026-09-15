#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

/// 表达式输入条：一行输入框 + 右侧「确定」，下方一行提示/报错。
///
/// 用单行 QLineEdit 而不是 QPlainTextEdit：**简单模式下表达式就是一行**
/// （技术方案 §1.1）。高级模式的多行 Lua 编辑器属于后续阶段，
/// 到那时再引入 QPlainTextEdit + 高亮 + 补全（ADR-4）。
class ExpressionBar : public QWidget {
    Q_OBJECT

public:
    explicit ExpressionBar(QWidget* parent = nullptr);

    [[nodiscard]] QString expression() const;

    /// 写入表达式（加载预设时用）。**不发 `submitted`** —— 装载不是"用户按了确定"，
    /// 不该顺手算一次（那会让"打开预设"变成"打开并立刻执行"）。
    void setExpression(const QString& expression);

    /// 求值失败时的提示（红色）
    void showError(const QString& message);

    /// 普通提示（灰色），例如算出了多少行
    void showHint(const QString& message);

signals:
    /// 用户按了「确定」或在输入框里回车
    void submitted(const QString& expression);

    /// 输入框里的文字变了（用来标记「有未保存的改动」）
    void textEdited();

private:
    void emitSubmitted();

    QLineEdit* m_input = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QLabel* m_message = nullptr;
};
