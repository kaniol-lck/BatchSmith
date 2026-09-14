#include "editor/ExpressionBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ExpressionBar::ExpressionBar(QWidget* parent) : QWidget(parent) {
    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("expressionInput"));
    // 提示要写当前语法（区段里是 Lua），而不是早期"只认列表名"的那套写法
    m_input->setPlaceholderText(QStringLiteral(
            "$ 里写 Lua 表达式，例如 mv $list1[i]$ out/$list2[i]$；按 F1 查看语法与函数"));
    m_input->setClearButtonEnabled(true);
    connect(m_input, &QLineEdit::returnPressed, this, &ExpressionBar::emitSubmitted);
    connect(m_input, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_confirmButton->setEnabled(!text.isEmpty());
    });

    m_confirmButton = new QPushButton(QStringLiteral("确定"), this);
    m_confirmButton->setObjectName(QStringLiteral("confirmButton"));
    m_confirmButton->setDefault(true);
    m_confirmButton->setEnabled(false);  // 空表达式没什么可算的
    connect(m_confirmButton, &QPushButton::clicked, this, &ExpressionBar::emitSubmitted);

    auto* inputLayout = new QHBoxLayout;
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->addWidget(m_input, 1);
    inputLayout->addWidget(m_confirmButton);

    m_message = new QLabel(this);
    m_message->setObjectName(QStringLiteral("messageLabel"));
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_message->setWordWrap(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addLayout(inputLayout);
    layout->addWidget(m_message);

    showHint(QStringLiteral("回车或点「确定」计算输出列表；按 F1 查看语法与函数"));
}

QString ExpressionBar::expression() const {
    return m_input->text().trimmed();
}

void ExpressionBar::showError(const QString& message) {
    m_message->setStyleSheet(QStringLiteral("color: #c0392b;"));
    m_message->setText(message);
}

void ExpressionBar::showHint(const QString& message) {
    m_message->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_message->setText(message);
}

void ExpressionBar::emitSubmitted() {
    const QString text = expression();
    if (text.isEmpty()) {
        return;
    }
    emit submitted(text);
}
