#include "compilepanel.h"

#include <QFont>
#include <QPlainTextEdit>
#include <QTabWidget>

CompilePanel::CompilePanel(QWidget *parent)
    : QDockWidget(tr("Compile Messages"), parent),
      m_tabs(new QTabWidget(this)),
      m_outputEdit(new QPlainTextEdit(m_tabs))
{
    setObjectName(QStringLiteral("compilePanel"));
    // CompileForm.dfm specifies Tahoma at 11 pixels.
    QFont panelFont(QStringLiteral("Tahoma"));
    panelFont.setPixelSize(11);
    setFont(panelFont);
    setAllowedAreas(Qt::BottomDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                | QDockWidget::DockWidgetFloatable);
    setMinimumHeight(100);

    m_tabs->setObjectName(QStringLiteral("compileTabs"));
    auto *sourceControlEdit = new QPlainTextEdit(m_tabs);
    for (QPlainTextEdit *outputEdit : {m_outputEdit, sourceControlEdit}) {
        outputEdit->setReadOnly(true);
        outputEdit->setUndoRedoEnabled(false);
        outputEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    m_outputEdit->setAccessibleName(tr("Compile"));
    sourceControlEdit->setAccessibleName(tr("Source Control"));
    m_tabs->addTab(m_outputEdit, tr("Compile"));
    m_tabs->addTab(sourceControlEdit, tr("Source Control"));
    setWidget(m_tabs);
}

void CompilePanel::setMessages(const QStringList &messages)
{
    m_outputEdit->setPlainText(messages.join(QLatin1Char('\n')));
    m_tabs->setCurrentWidget(m_outputEdit);
}

QSize CompilePanel::sizeHint() const
{
    return QSize(640, 200);
}

void CompilePanel::appendMessage(const QString &message)
{ m_outputEdit->appendPlainText(message); }
