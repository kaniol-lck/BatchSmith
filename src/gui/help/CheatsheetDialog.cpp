#include "help/CheatsheetDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

#include "batchsmith/core/dsl/cheatsheet.hpp"

using batchsmith::core::dsl::cheatsheet_html;
using batchsmith::core::dsl::cheatsheet_sections;

CheatsheetDialog::CheatsheetDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("cheatsheetDialog"));
    setWindowTitle(QStringLiteral("DSL 语法与函数速查"));
    resize(860, 660);

    // 目录下拉：章节多起来之后，靠滚动找是很浪费时间的
    m_toc = new QComboBox(this);
    m_toc->setObjectName(QStringLiteral("cheatsheetToc"));
    for (const auto& section : cheatsheet_sections()) {
        m_toc->addItem(section.title);
    }
    m_toc->setMinimumWidth(240);

    m_view = new QTextBrowser(this);
    m_view->setObjectName(QStringLiteral("cheatsheetView"));
    m_view->setHtml(cheatsheet_html());
    m_view->moveCursor(QTextCursor::Start);
    m_view->setOpenExternalLinks(false);  // 帮助里的链接只用于内部跳转
    m_view->setToolTip(QStringLiteral("可以选中复制；点击目录或章节标题里的链接可跳转"));

    connect(m_toc, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) {
            showSection(index);
        }
    });
    // 文档内部的锚点链接（目录里的条目）
    connect(m_view, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        m_view->scrollToAnchor(url.fragment());
    });

    auto* tocRow = new QHBoxLayout;
    tocRow->setContentsMargins(0, 0, 0, 0);
    tocRow->addWidget(new QLabel(QStringLiteral("跳到："), this));
    tocRow->addWidget(m_toc, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* saveButton =
            buttons->addButton(QStringLiteral("另存为 HTML(&S)…"), QDialogButtonBox::ActionRole);
    saveButton->setToolTip(QStringLiteral("导出成单个 HTML 文件，可用浏览器打开或分发"));
    connect(saveButton, &QPushButton::clicked, this, &CheatsheetDialog::saveAsHtml);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addLayout(tocRow);
    layout->addWidget(m_view, 1);
    layout->addWidget(buttons);
}

QString CheatsheetDialog::html() const {
    return m_view->toHtml();
}

QString CheatsheetDialog::visibleText() const {
    return m_view->toPlainText();
}

int CheatsheetDialog::sectionCount() const {
    return m_toc->count();
}

void CheatsheetDialog::showSection(int index) {
    const auto sections = cheatsheet_sections();
    if (index < 0 || index >= sections.size()) {
        return;
    }
    m_view->scrollToAnchor(sections.at(index).anchor);
    if (m_toc->currentIndex() != index) {
        m_toc->setCurrentIndex(index);  // 与目录保持一致（外部调用 showSection 时）
    }
}

void CheatsheetDialog::saveAsHtml() {
    const QString path = QFileDialog::getSaveFileName(this,
                                                      QStringLiteral("保存帮助手册"),
                                                      QStringLiteral("BatchSmith-帮助手册.html"),
                                                      QStringLiteral("HTML 文件 (*.html)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    if (!writeHtmlFile(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), error);
        return;
    }
    QMessageBox::information(
            this,
            QStringLiteral("已保存"),
            QStringLiteral("已写出：\n%1\n\n可以用浏览器打开，也可以直接分发给别人。")
                    .arg(QDir::toNativeSeparators(path)));
}

bool CheatsheetDialog::writeHtmlFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = QStringLiteral("无法写入 %1：%2")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);  // HTML 里声明的是 utf-8，别写错编码
    stream << cheatsheet_html();
    stream.flush();

    if (file.error() != QFileDevice::NoError) {
        if (error != nullptr) {
            *error = QStringLiteral("写入 %1 时出错：%2")
                             .arg(QDir::toNativeSeparators(path), file.errorString());
        }
        return false;
    }
    return true;
}
