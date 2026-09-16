#include "preferencesdialog.h"
#include "editorlanguage.h"
#include "codeeditorsettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent) : EditorDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    auto *layout = new QVBoxLayout(bodyWidget());
    auto *tabs = new QTabWidget;
    layout->addWidget(tabs);
    auto *generalPage = new QWidget;
    auto *generalLayout = new QVBoxLayout(generalPage);
    auto *generalGroup = new QGroupBox(tr("General"));
    auto *form = new QFormLayout(generalGroup);
    auto *languageCombo = new QComboBox;
    languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageCombo->addItem(QString::fromUtf8("简体中文"), QStringLiteral("zh_CN"));
    languageCombo->addItem(QString::fromUtf8("日本語"), QStringLiteral("ja_JP"));
    languageCombo->addItem(QString::fromUtf8("한국어"), QStringLiteral("ko_KR"));
    languageCombo->setCurrentIndex(languageCombo->findData(EditorLanguage::selectedLanguage()));
    form->addRow(tr("Language:"), languageCombo);
    auto *hint = new QLabel(tr("Language changes take effect the next time you start QtGMS."));
    hint->setWordWrap(true);
    form->addRow(hint);
    generalLayout->addWidget(generalGroup);
    auto *roomGroup = new QGroupBox(tr("Room Editor"));
    auto *roomLayout = new QVBoxLayout(roomGroup);
    auto *selectRoomObject = new QCheckBox(tr("Select the placement object by clicking the resource tree"));
    selectRoomObject->setChecked(QSettings().value(QStringLiteral("roomEditor/selectObjectFromTree"), true).toBool());
    roomLayout->addWidget(selectRoomObject);
    auto *roomHint = new QLabel(tr("Applies immediately to the most recently active room editor."));
    roomHint->setWordWrap(true); roomLayout->addWidget(roomHint);
    generalLayout->addWidget(roomGroup);
    generalLayout->addStretch();
    tabs->addTab(generalPage, tr("General"));

    auto *codePage = new QWidget;
    auto *codeLayout = new QVBoxLayout(codePage);
    auto *fontGroup = new QGroupBox(tr("Font"));
    auto *fontLayout = new QGridLayout(fontGroup);
    auto *fontCombo = new QFontComboBox;
    fontCombo->setMinimumWidth(160);
    auto *fontSize = new QSpinBox; fontSize->setRange(8, 32); fontSize->setSuffix(QStringLiteral(" px"));
    fontLayout->addWidget(new QLabel(tr("Font:")), 0, 0);
    fontLayout->addWidget(fontCombo, 0, 1);
    fontLayout->addWidget(new QLabel(tr("Size:")), 0, 2);
    fontLayout->addWidget(fontSize, 0, 3);
    auto *preview = new QLabel(QStringLiteral("var speed = 4; // GML"));
    preview->setMinimumHeight(42);
    fontLayout->addWidget(preview, 1, 0, 1, 4);
    codeLayout->addWidget(fontGroup);
    const auto updatePreview = [fontCombo, fontSize, preview] {
        QFont font = fontCombo->currentFont(); font.setPixelSize(fontSize->value());
        preview->setFont(font);
    };
    connect(fontCombo, &QFontComboBox::currentFontChanged, this, updatePreview);
    connect(fontSize, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, updatePreview);

    auto *columns = new QHBoxLayout;
    auto *editingGroup = new QGroupBox(tr("Editing"));
    auto *editing = new QFormLayout(editingGroup);
    auto *automaticIndentation = new QCheckBox(tr("Automatic indentation"));
    auto *indentSize = new QSpinBox; indentSize->setRange(1, 16);
    auto *automaticBrackets = new QCheckBox(tr("Automatically complete brackets"));
    auto *automaticCompletion = new QCheckBox(tr("Show auto-completion options"));
    auto *completionDelay = new QSpinBox; completionDelay->setRange(0, 2000); completionDelay->setSingleStep(10);
    auto *functionHelp = new QCheckBox(tr("Show function argument help"));
    editing->addRow(automaticIndentation);
    editing->addRow(tr("Indent amount:"), indentSize);
    editing->addRow(automaticBrackets);
    editing->addRow(automaticCompletion);
    editing->addRow(tr("Completion delay (ms):"), completionDelay);
    editing->addRow(functionHelp);
    connect(automaticCompletion, &QCheckBox::toggled, completionDelay, &QWidget::setEnabled);
    columns->addWidget(editingGroup);

    auto *displayGroup = new QGroupBox(tr("Display"));
    auto *display = new QVBoxLayout(displayGroup);
    auto *lineNumbers = new QCheckBox(tr("Show line numbers"));
    auto *matchingBrackets = new QCheckBox(tr("Show matching brackets"));
    auto *indentGuides = new QCheckBox(tr("Show indentation guides"));
    auto *currentLine = new QCheckBox(tr("Highlight the current line"));
    auto *searchHighlights = new QCheckBox(tr("Highlight search matches"));
    for (auto *check : {lineNumbers, matchingBrackets, indentGuides, currentLine, searchHighlights}) display->addWidget(check);
    display->addStretch();
    columns->addWidget(displayGroup);
    codeLayout->addLayout(columns);
    auto *codeHint = new QLabel(tr("Code settings apply to open editors when you click OK. Ctrl+Space always opens completion."));
    codeHint->setWordWrap(true); codeLayout->addWidget(codeHint);
    auto *restore = new QPushButton(tr("Restore Code Defaults"));
    codeLayout->addWidget(restore, 0, Qt::AlignLeft);
    codeLayout->addStretch();
    tabs->addTab(codePage, tr("Scripts and Code"));

    const auto showOptions = [=](const CodeEditorOptions &options) {
        fontCombo->setCurrentFont(QFont(options.fontFamily)); fontSize->setValue(options.fontPixelSize);
        indentSize->setValue(options.indentSize); completionDelay->setValue(options.completionDelay);
        automaticIndentation->setChecked(options.automaticIndentation);
        automaticBrackets->setChecked(options.automaticBrackets);
        automaticCompletion->setChecked(options.automaticCompletion);
        completionDelay->setEnabled(options.automaticCompletion);
        functionHelp->setChecked(options.functionHelp); lineNumbers->setChecked(options.lineNumbers);
        matchingBrackets->setChecked(options.matchingBrackets); indentGuides->setChecked(options.indentGuides);
        currentLine->setChecked(options.currentLine); searchHighlights->setChecked(options.searchHighlights);
        updatePreview();
    };
    showOptions(CodeEditorSettings::instance().options());
    connect(restore, &QPushButton::clicked, this, [showOptions] { showOptions(CodeEditorOptions()); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [=] {
        EditorLanguage::setSelectedLanguage(languageCombo->currentData().toString());
        QSettings().setValue(QStringLiteral("roomEditor/selectObjectFromTree"), selectRoomObject->isChecked());
        CodeEditorOptions options;
        options.fontFamily = fontCombo->currentFont().family(); options.fontPixelSize = fontSize->value();
        options.indentSize = indentSize->value(); options.completionDelay = completionDelay->value();
        options.automaticIndentation = automaticIndentation->isChecked();
        options.automaticBrackets = automaticBrackets->isChecked();
        options.automaticCompletion = automaticCompletion->isChecked();
        options.functionHelp = functionHelp->isChecked(); options.lineNumbers = lineNumbers->isChecked();
        options.matchingBrackets = matchingBrackets->isChecked(); options.indentGuides = indentGuides->isChecked();
        options.currentLine = currentLine->isChecked(); options.searchHighlights = searchHighlights->isChecked();
        CodeEditorSettings::instance().setOptions(options);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(620, 450);
}
