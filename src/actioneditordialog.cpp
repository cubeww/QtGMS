#include "resourceselector.h"
#include "editorstandarddialogs.h"
#include "actionxml.h"
#include "actioneditordialog.h"
#include "actionlibrary.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

ActionEditorDialog::ActionEditorDialog(QDomElement source, const LibraryAction *definition, const Project &project, QWidget *parent)
    : EditorDialog(parent)
{
    m_xml.appendChild(m_xml.importNode(source, true)); const QDomElement action = m_xml.documentElement();
    setWindowTitle(definition ? definition->description : tr("Action %1/%2").arg(ActionXml::text(action, QStringLiteral("libid")), ActionXml::text(action, QStringLiteral("id"))));
    if (windowTitle().isEmpty() && definition) setWindowTitle(definition->name);
    auto *layout = new QVBoxLayout(bodyWidget()); layout->setContentsMargins(10, 8, 8, 8); layout->setSpacing(0);
    if (!definition) { auto *note = new QLabel(tr("This action's library is missing. Its stored definition is preserved; you can edit its arguments below.")); note->setWordWrap(true); layout->addWidget(note); }
    auto *header = new QWidget; header->setFixedHeight(80);
    auto *headerLayout = new QHBoxLayout(header); headerLayout->setContentsMargins(0, 0, 8, 0); headerLayout->setSpacing(16);
    auto *icon = new QLabel; icon->setFixedSize(24, 40); icon->setAlignment(Qt::AlignBottom);
    icon->setPixmap((definition ? definition->icon : QIcon(QStringLiteral(":/images/script.png"))).pixmap(24, 24));
    headerLayout->addWidget(icon, 0, Qt::AlignTop);
    if (ActionXml::text(action, QStringLiteral("useapplyto")).toInt()) {
        auto *group = new QGroupBox(tr("Applies to")); headerLayout->addWidget(group, 1);
        auto *options = new QVBoxLayout(group); options->setContentsMargins(8, 15, 8, 12); options->setSpacing(0);
        m_self = new QRadioButton(tr("Self"), group); m_other = new QRadioButton(tr("Other"), group);
        auto *objectChoice = new QRadioButton(tr("Object:"), group);
        for (QRadioButton *button : {m_self, m_other, objectChoice}) button->setFixedHeight(16);
        options->addWidget(m_self); options->addWidget(m_other);
        auto *objectRow = new QHBoxLayout; objectRow->setSpacing(4); objectRow->addWidget(objectChoice);
        m_appliesTo = new ResourceComboBox(group); m_appliesTo->setFixedHeight(19); m_appliesTo->setMinimumWidth(0);
        m_appliesTo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_appliesTo->setMinimumContentsLength(6);
        const QString who = ActionXml::text(action, QStringLiteral("whoName"));
        const bool objectSelected = who != QStringLiteral("self") && who != QStringLiteral("other");
        m_appliesTo->setResources(project, ResourceType::Object, objectSelected ? who : QStringLiteral("<undefined>"), tr("<no object>"));
        m_self->setChecked(who == QStringLiteral("self")); m_other->setChecked(who == QStringLiteral("other")); objectChoice->setChecked(objectSelected);
        objectRow->addWidget(m_appliesTo, 1); options->addLayout(objectRow);
        // Retain the row's height and alignment when no object picker is shown.
        QSizePolicy policy = m_appliesTo->sizePolicy(); policy.setRetainSizeWhenHidden(true); m_appliesTo->setSizePolicy(policy);
        m_appliesTo->setVisible(objectSelected); connect(objectChoice, &QRadioButton::toggled, m_appliesTo, &QWidget::setVisible);
    } else {
        headerLayout->addStretch();
    }
    layout->addWidget(header); layout->addSpacing(10);
    auto *content = new QWidget; auto *contentLayout = new QVBoxLayout(content); contentLayout->setContentsMargins(8, 16, 38, 8); contentLayout->setSpacing(8);
    auto *form = new QFormLayout; form->setContentsMargins(0, 0, 0, 0); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter); form->setHorizontalSpacing(6);
    const bool motion = definition && definition->interfaceKind == 2;
    form->setVerticalSpacing(motion ? 16 : 6); contentLayout->addLayout(form); contentLayout->addStretch();
    auto *flags = new QHBoxLayout; flags->setContentsMargins(87, 0, 0, 0); flags->setSpacing(8);
    if (ActionXml::text(action, QStringLiteral("userelative")).toInt()) {
        m_relative = new QCheckBox(tr("Relative")); m_relative->setChecked(ActionXml::text(action, QStringLiteral("relative")).toInt() != 0); flags->addWidget(m_relative);
    }
    if (ActionXml::text(action, QStringLiteral("isquestion")).toInt()) {
        m_not = new QCheckBox(tr("NOT")); m_not->setChecked(ActionXml::text(action, QStringLiteral("isnot")).toInt() != 0); flags->addWidget(m_not);
    }
    flags->addStretch(); contentLayout->addLayout(flags);
    const auto args = ActionXml::elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"));
    for (int i = 0; i < args.size(); ++i) {
        const int kind = ActionXml::text(args.at(i), QStringLiteral("kind")).toInt();
        const QString value = ActionXml::text(args.at(i), ActionXml::argumentTag(kind));
        const LibraryArgument *arg = definition && i < definition->arguments.size() ? &definition->arguments.at(i) : nullptr;
        QString caption = arg ? arg->caption : tr("Argument %1").arg(i + 1);
        if (motion && i < 2) caption = i == 0 ? tr("Directions:") : tr("Speed:");
        else if (!caption.endsWith(QLatin1Char(':'))) caption += QLatin1Char(':');
        if (i == 0 && motion) {
            auto *arrows = new QWidget; arrows->setFixedSize(71, 71);
            auto *grid = new QGridLayout(arrows); grid->setContentsMargins(0, 0, 0, 0); grid->setSpacing(1); QList<QToolButton *> buttons;
            arrows->setStyleSheet(QStringLiteral("QToolButton { border: 1px solid #202020; border-top-color: #777774; border-left-color: #777774; background: #505050; padding: 0; } QToolButton:checked, QToolButton:pressed { background: #383838; border: 1px solid #202020; border-right-color: #777774; border-bottom-color: #777774; }"));
            const QStringList directions = {tr("Down left"), tr("Down"), tr("Down right"), tr("Left"), tr("Stop"), tr("Right"), tr("Up left"), tr("Up"), tr("Up right")};
            for (int n = 0; n < 9; ++n) {
                const QPixmap glyphs(QStringLiteral(":/object/controls/direction%1.png").arg(n + 1));
                QIcon glyph; glyph.addPixmap(glyphs.copy(0, 0, 16, 16), QIcon::Normal, QIcon::Off);
                glyph.addPixmap(glyphs.copy(16, 0, 16, 16), QIcon::Disabled, QIcon::Off);
                glyph.addPixmap(glyphs.copy(48, 0, 16, 16), QIcon::Normal, QIcon::On);
                auto *button = new QToolButton; button->setIcon(glyph); button->setIconSize(QSize(16, 16)); button->setToolTip(directions.at(n)); button->setAccessibleName(directions.at(n));
                button->setFixedSize(23, 23); button->setCheckable(true); button->setChecked(n < value.size() && value.at(n) == QLatin1Char('1'));
                grid->addWidget(button, 2 - n / 3, n % 3); buttons.append(button);
            }
            form->addRow(caption, arrows); m_values.append([buttons] { QString value; for (auto *button : buttons) value += button->isChecked() ? QLatin1Char('1') : QLatin1Char('0'); return value; });
        } else if ((kind >= 5 && kind <= 12) || kind == 14) {
            const ResourceType types[] = {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background, ResourceType::Path, ResourceType::Script, ResourceType::Object, ResourceType::Room, ResourceType::Font};
            auto *combo = new ResourceComboBox; combo->setResources(project, kind == 14 ? ResourceType::Timeline : types[kind - 5], value, tr("<none>"));
            form->addRow(caption, combo); m_values.append([combo] { return combo->currentData().toString(); });
        } else if (kind == 3 || (kind == 4 && arg)) {
            auto *combo = new QComboBox; const QStringList choices = kind == 3 ? QStringList({tr("false"), tr("true")}) : arg->menu.split(QLatin1Char('|'));
            for (int n = 0; n < choices.size(); ++n) combo->addItem(choices.at(n), QString::number(n));
            if (combo->findData(value) < 0) combo->addItem(value, value);
            combo->setCurrentIndex(combo->findData(value)); form->addRow(caption, combo); m_values.append([combo] { return combo->currentData().toString(); });
        } else if (kind == 13) {
            auto *row = new QWidget; auto *box = new QHBoxLayout(row); box->setContentsMargins(0, 0, 0, 0);
            auto *edit = new QLineEdit(value); auto *choose = new QPushButton(tr("Color...")); box->addWidget(edit); box->addWidget(choose);
            connect(choose, &QPushButton::clicked, this, [this, edit] {
                const uint bgr = edit->text().toUInt(); const QColor color = EditorColorDialog::getColor(QColor(bgr & 255, (bgr >> 8) & 255, (bgr >> 16) & 255), this);
                if (color.isValid()) edit->setText(QString::number(color.red() | (color.green() << 8) | (color.blue() << 16)));
            }); form->addRow(caption, row); m_values.append([edit] { return edit->text(); });
        } else if (definition && definition->interfaceKind == 6) {
            auto *edit = new QPlainTextEdit(value); form->addRow(caption, edit); m_values.append([edit] { return edit->toPlainText(); });
        } else if (kind == 15) {
            auto *combo = new QFontComboBox; combo->setEditable(true); combo->setEditText(value); form->addRow(caption, combo); m_values.append([combo] { return combo->currentText(); });
        } else {
            auto *edit = new QLineEdit(value); edit->setFixedHeight(19); form->addRow(caption, edit); m_values.append([edit] { return edit->text(); });
        }
    }
    for (int row = 0; row < form->rowCount(); ++row) {
        auto *field = form->itemAt(row, QFormLayout::FieldRole);
        m_argumentWidgets.append(field ? field->widget() : nullptr);
        QLayoutItem *item = form->itemAt(row, QFormLayout::LabelRole);
        if (item) { auto *label = qobject_cast<QLabel *>(item->widget()); if (label) { label->setFixedWidth(81); label->setWordWrap(true); label->setAlignment(Qt::AlignRight | Qt::AlignVCenter); } }
    }
    auto *scroll = new QScrollArea; scroll->setObjectName(QStringLiteral("actionParameters"));
    scroll->setWidgetResizable(true); scroll->setWidget(content); scroll->setFrameStyle(QFrame::Box | QFrame::Plain);
    scroll->setMinimumHeight(185); layout->addWidget(scroll, 1); layout->addSpacing(4);
    auto *buttons = new QHBoxLayout; buttons->setContentsMargins(0, 0, 0, 0);
    auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK"));
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("Cancel"));
    for (auto *button : {ok, cancel}) { button->setFixedSize(73, 23); button->setIconSize(QSize(16, 16)); button->setAutoDefault(false); }
    ok->setDefault(true); buttons->addWidget(ok); buttons->addStretch(); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &ActionEditorDialog::accept); connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    const int height = qBound(185, content->sizeHint().height() + 4, 430);
    setFixedSize(322, 350 + height - 185);
    if (parent) move(parent->mapToGlobal(parent->rect().center()) - rect().center());
}
void ActionEditorDialog::accept()
{
    QDomElement action = m_xml.documentElement();
    if (m_appliesTo) ActionXml::setText(action, QStringLiteral("whoName"), m_self->isChecked() ? QStringLiteral("self")
        : m_other->isChecked() ? QStringLiteral("other") : m_appliesTo->currentData().toString());
    if (m_relative) ActionXml::setText(action, QStringLiteral("relative"), m_relative->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    if (m_not) ActionXml::setText(action, QStringLiteral("isnot"), m_not->isChecked() ? QStringLiteral("-1") : QStringLiteral("0"));
    const auto args = ActionXml::elements(action.firstChildElement(QStringLiteral("arguments")), QStringLiteral("argument"));
    for (int i = 0; i < args.size(); ++i) ActionXml::setText(args.at(i), ActionXml::argumentTag(ActionXml::text(args.at(i), QStringLiteral("kind")).toInt()), m_values.at(i)());
    QDialog::accept();
}

void ActionEditorDialog::selectArgument(int argument, int offset, int length)
{
    QWidget *widget = argument >= 0 && argument < m_argumentWidgets.size()
        ? m_argumentWidgets.at(argument) : static_cast<QWidget *>(m_appliesTo);
    if (!widget) return;
    widget->setFocus();
    if (auto *scroll = findChild<QScrollArea *>()) scroll->ensureWidgetVisible(widget);
    auto *edit = qobject_cast<QLineEdit *>(widget);
    if (!edit) edit = widget->findChild<QLineEdit *>();
    if (edit) { edit->setFocus(); edit->setSelection(offset, length); }
    if (auto *text = qobject_cast<QPlainTextEdit *>(widget)) {
        QTextCursor cursor = text->textCursor();
        cursor.setPosition(qMin(offset, text->toPlainText().size()));
        cursor.setPosition(qMin(offset + length, text->toPlainText().size()), QTextCursor::KeepAnchor);
        text->setTextCursor(cursor);
        text->ensureCursorVisible();
    }
}
