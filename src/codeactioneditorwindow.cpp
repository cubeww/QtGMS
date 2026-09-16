#include "codeactioneditorwindow.h"
#include "resourceselector.h"
#include "actionxml.h"
#include "codeeditorpanel.h"
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QTextDocument>
#include <QToolBar>

static QString actionSourceCode(const QDomElement &source)
{
    const QDomElement argument = source.firstChildElement(QStringLiteral("arguments")).firstChildElement(QStringLiteral("argument"));
    return ActionXml::text(argument, ActionXml::argumentTag(ActionXml::text(argument, QStringLiteral("kind")).toInt()));
}
CodeActionEditorWindow::CodeActionEditorWindow(QDomElement source, const Project &project,
                                             const QString &contextName, QWidget *parent)
    : CodeSnippetEditorWindow(tr("Event: %1").arg(contextName), tr("action"), actionSourceCode(source), contextName, parent)
{
    setResources(project);
    m_xml.appendChild(m_xml.importNode(source, true));
    const QDomElement original = m_xml.documentElement();
    auto *toolbar = m_codePanel->toolBar(); toolbar->addSeparator();
    auto *options = new QWidget(toolbar);
    auto *optionsLayout = new QHBoxLayout(options);
    optionsLayout->setContentsMargins(0, 0, 0, 0); optionsLayout->setSpacing(5);
    if (ActionXml::text(original, QStringLiteral("useapplyto")).toInt()) {
        optionsLayout->addWidget(new QLabel(tr("Applies To:")));
        m_self = new QRadioButton(tr("Self"), options);
        m_other = new QRadioButton(tr("Other"), options);
        auto *objectChoice = new QRadioButton(tr("Object:"), options);
        m_objectChoice = objectChoice;
        m_object = new ResourceComboBox(options); m_object->setFixedSize(115, 20);
        const QString who = ActionXml::text(original, QStringLiteral("whoName"));
        const bool objectSelected = who != QStringLiteral("self") && who != QStringLiteral("other");
        m_object->setResources(project, ResourceType::Object,
            objectSelected ? who : QStringLiteral("<undefined>"), tr("<no object>"));
        m_self->setChecked(who == QStringLiteral("self")); m_other->setChecked(who == QStringLiteral("other"));
        objectChoice->setChecked(objectSelected); m_object->setVisible(objectSelected);
        for (QRadioButton *button : {m_self, m_other, objectChoice}) {
            button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            optionsLayout->addWidget(button, 0, Qt::AlignVCenter);
        }
        optionsLayout->addWidget(m_object);
        connect(objectChoice, &QRadioButton::toggled, m_object, &QWidget::setVisible);
    }
    if (ActionXml::text(original, QStringLiteral("userelative")).toInt()) {
        m_relative = new QCheckBox(tr("Relative")); m_relative->setChecked(ActionXml::text(original, QStringLiteral("relative")).toInt() != 0);
        optionsLayout->addWidget(m_relative);
    }
    if (ActionXml::text(original, QStringLiteral("isquestion")).toInt()) {
        m_not = new QCheckBox(tr("NOT")); m_not->setChecked(ActionXml::text(original, QStringLiteral("isnot")).toInt() != 0);
        optionsLayout->addWidget(m_not);
    }
    toolbar->addWidget(options);
}

QDomElement CodeActionEditorWindow::action() const
{
    QDomDocument xml = m_xml.cloneNode(true).toDocument();
    QDomElement result = xml.documentElement();
    QDomElement arguments = ActionXml::child(result, QStringLiteral("arguments"));
    QDomElement argument = arguments.firstChildElement(QStringLiteral("argument"));
    if (argument.isNull()) {
        argument = xml.createElement(QStringLiteral("argument")); arguments.appendChild(argument);
        ActionXml::setText(argument, QStringLiteral("kind"), QStringLiteral("1"));
    }
    // Preserve original text/line endings until the user actually edits the code.
    if (m_document->isModified())
        ActionXml::setText(argument, ActionXml::argumentTag(ActionXml::text(argument, QStringLiteral("kind")).toInt()), m_document->toPlainText());
    if (m_self) ActionXml::setText(result, QStringLiteral("whoName"), m_self->isChecked() ? QStringLiteral("self")
        : m_other->isChecked() ? QStringLiteral("other") : m_object->currentData().toString());
    if (m_relative && m_relative->isChecked() != (ActionXml::text(result, QStringLiteral("relative")).toInt() != 0))
        ActionXml::setText(result, QStringLiteral("relative"), m_relative->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    if (m_not && m_not->isChecked() != (ActionXml::text(result, QStringLiteral("isnot")).toInt() != 0))
        ActionXml::setText(result, QStringLiteral("isnot"), m_not->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    return result;
}

bool CodeActionEditorWindow::hasChanges() const
{
    QDomDocument edited; edited.appendChild(edited.importNode(action(), true));
    return edited.toByteArray() != m_xml.toByteArray();
}

void CodeActionEditorWindow::setCommitHandler(const std::function<bool(QDomElement, QDomElement)> &handler)
{ m_commitHandler = handler; }

bool CodeActionEditorWindow::commitChanges()
{
    if (!m_commitHandler || !hasChanges()) return true;
    const auto edited = action();
    if (!m_commitHandler(m_xml.documentElement(), edited)) return false;
    m_xml.clear();
    m_xml.appendChild(m_xml.importNode(edited, true));
    resetCode(actionSourceCode(edited));
    return true;
}

void CodeActionEditorWindow::refreshAction(QDomElement source)
{
    if (hasChanges()) return;
    m_xml.clear();
    m_xml.appendChild(m_xml.importNode(source, true));
    resetCode(actionSourceCode(source));
    if (m_self) {
        const QString who = ActionXml::text(source, QStringLiteral("whoName"));
        m_self->setChecked(who == QStringLiteral("self"));
        m_other->setChecked(who == QStringLiteral("other"));
        m_objectChoice->setChecked(who != QStringLiteral("self") && who != QStringLiteral("other"));
        m_object->setCurrentIndex(m_object->findData(who));
    }
    if (m_relative) m_relative->setChecked(ActionXml::text(source, QStringLiteral("relative")).toInt() != 0);
    if (m_not) m_not->setChecked(ActionXml::text(source, QStringLiteral("isnot")).toInt() != 0);
}
