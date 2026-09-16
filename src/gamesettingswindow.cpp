#include "gamesettingswindow.h"
#include "gamesettingsdocument.h"
#include "editorstandarddialogs.h"
#include "editordialog.h"
#include <QRadioButton>
#include <QButtonGroup>
#include <QPlainTextEdit>
#include <QTextCodec>
#include <QTabBar>
#include <QDateTime>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>
#include <climits>

GameSettingsWindow::GameSettingsWindow(GameSettingsDocument *document, QWidget *parent)
    : ResourceEditorWindow(parent), m_document(document)
{
    document->setParent(this); setAttribute(Qt::WA_DeleteOnClose); editorMenuBar()->hide();
    resize(700, 530); setMinimumSize(690, 525);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(7, 3, 7, 6); layout->setSpacing(6);
    auto *tabs = new QTabWidget; tabs->setObjectName(QStringLiteral("gameSettingsTabs")); layout->addWidget(tabs, 1);
    body->setStyleSheet(QStringLiteral("QTabWidget#gameSettingsTabs::pane, QTabWidget#gameSettingsWindows::pane { border: 1px solid #888679; }"));
    createGeneral(tabs); createGroups(tabs, false); createGroups(tabs, true); createProjectInfo(tabs); createWindows(tabs);
    auto *bottom = new QHBoxLayout; auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("&OK"));
    auto *cancel = new QPushButton(QIcon(QStringLiteral(":/object/controls/delete.png")), tr("&Cancel"));
    ok->setFixedSize(74, 24); cancel->setFixedSize(74, 24); bottom->addWidget(ok); bottom->addStretch(); bottom->addWidget(cancel); layout->addLayout(bottom); setCentralWidget(body);
    connect(ok, &QPushButton::clicked, this, [this] { if (save()) close(); });
    connect(cancel, &QPushButton::clicked, this, [this] { m_discard = true; close(); });
    connect(document, &GameSettingsDocument::changed, this, [this] { setWindowTitle(tr("Global Game Settings: %1%2").arg(m_document->name(), isModified() ? QStringLiteral(" *") : QString())); });
    auto *saveShortcut = new QShortcut(QKeySequence::Save, this); connect(saveShortcut, &QShortcut::activated, this, &GameSettingsWindow::saveProjectRequested);
    setWindowTitle(tr("Global Game Settings: %1").arg(document->name()));
}
GameSettingsWindow::~GameSettingsWindow()
{
    for (auto *child : findChildren<QObject *>()) QObject::disconnect(child, nullptr, this, nullptr);
    delete takeCentralWidget();
}
QVBoxLayout *GameSettingsWindow::page(QTabWidget *tabs, const QString &title)
{
    auto *scroll = new QScrollArea; scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget; auto *layout = new QVBoxLayout(body); layout->setContentsMargins(10, 10, 10, 10); layout->setSpacing(9);
    scroll->setWidget(body); tabs->addTab(scroll, title); return layout;
}
QLineEdit *GameSettingsWindow::textField(QFormLayout *form, const QString &title, const QString &key, const QString &fallback)
{
    auto *edit = new QLineEdit(m_document->value(key, fallback)); edit->setFixedHeight(19); form->addRow(title, edit);
    connect(edit, &QLineEdit::textEdited, this, [this, key](const QString &value) { m_document->setValue(key, value); }); return edit;
}
QSpinBox *GameSettingsWindow::numberField(QFormLayout *form, const QString &title, const QString &key, int fallback, int minimum, int maximum)
{
    auto *spin = new QSpinBox; spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setFixedHeight(19); spin->setRange(minimum, maximum); spin->setValue(m_document->value(key, QString::number(fallback)).toInt()); spin->setMaximumWidth(110); form->addRow(title, spin);
    connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this, key](int value) { m_document->setValue(key, QString::number(value)); }); return spin;
}
QCheckBox *GameSettingsWindow::check(QVBoxLayout *layout, const QString &title, const QString &key, bool fallback)
{
    auto *box = new QCheckBox(title); const QString value = m_document->value(key, fallback ? QStringLiteral("true") : QStringLiteral("false"));
    box->setChecked(value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 || value == QStringLiteral("-1") || value == QStringLiteral("1")); layout->addWidget(box);
    connect(box, &QCheckBox::toggled, this, [this, key](bool checked) { m_document->setValue(key, checked ? QStringLiteral("true") : QStringLiteral("false")); }); return box;
}
QComboBox *GameSettingsWindow::choice(QFormLayout *form, const QString &title, const QString &key, const QStringList &labels, const QStringList &values, const QString &fallback)
{
    auto *combo = new QComboBox;
    for (int i = 0; i < labels.size(); ++i) combo->addItem(labels.at(i), values.at(i));
    const QString value = m_document->value(key, fallback); int index = combo->findData(value);
    if (index < 0) { combo->addItem(value, value); index = combo->count() - 1; }
    combo->setCurrentIndex(index); form->addRow(title, combo);
    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this, [this, combo, key](int index) { m_document->setValue(key, combo->itemData(index).toString()); }); return combo;
}
static QColor settingsColor(const QString &text, bool delphi)
{
    if (!delphi || text.startsWith(QLatin1Char('#'))) return QColor(text);
    if (text.startsWith(QStringLiteral("cl"))) return QColor(text.mid(2));
    bool valid = false; const uint color = text.startsWith(QLatin1Char('$')) ? text.mid(1).toUInt(&valid, 16) : text.toUInt(&valid);
    return valid ? QColor(color & 255, (color >> 8) & 255, (color >> 16) & 255) : QColor();
}
void GameSettingsWindow::colorField(QFormLayout *form, const QString &title, const QString &key, bool delphi)
{
    auto *button = new QPushButton; button->setFixedSize(90, 20); form->addRow(title, button);
    auto refresh = [this, button, key, delphi] {
        const QColor color = settingsColor(m_document->value(key, delphi ? QStringLiteral("clBlack") : QStringLiteral("#000000")), delphi);
        button->setStyleSheet(QStringLiteral("QPushButton { background-color: %1; border: 1px solid #777; }").arg(color.isValid() ? color.name() : QStringLiteral("#000000")));
    }; refresh();
    connect(button, &QPushButton::clicked, this, [this, key, delphi, refresh] {
        const QColor color = EditorColorDialog::getColor(settingsColor(m_document->value(key, QStringLiteral("#000000")), delphi), this);
        if (!color.isValid()) return;
        m_document->setValue(key, delphi ? QStringLiteral("$%1").arg(color.red() | (color.green() << 8) | (color.blue() << 16), 8, 16, QLatin1Char('0')).toUpper() : color.name()); refresh();
    });
}
// Reference dimensions are logical pixels; Qt applies the desktop DPI scale.
static QWidget *settingsCanvas(QVBoxLayout *layout, int width = 640, int height = 410)
{
    auto *canvas = new QWidget; canvas->setMinimumSize(width, height); layout->addWidget(canvas, 1); return canvas;
}
static void settingsPlace(QWidget *widget, QWidget *parent, int x, int y, int width, int height)
{ widget->setParent(parent); widget->setGeometry(x, y, width, height); }
static QLabel *settingsLabel(QWidget *parent, const QString &text, int x, int y, int width, int height = 19)
{
    auto *label = new QLabel(text, parent); label->setGeometry(x, y, width, height); return label;
}
static QPushButton *settingsButton(QWidget *parent, const QString &text, int x, int y, int width = 74)
{
    auto *button = new QPushButton(text, parent); button->setAutoDefault(false); button->setGeometry(x, y, width, 23); return button;
}
static QFormLayout *settingsForm(QWidget *parent, int x, int y, int width, int height)
{
    auto *body = new QWidget(parent); body->setGeometry(x, y, width, height); auto *form = new QFormLayout(body);
    form->setContentsMargins(0, 0, 0, 0); form->setHorizontalSpacing(7); form->setVerticalSpacing(5);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); return form;
}
void GameSettingsWindow::createGeneral(QTabWidget *tabs)
{
    auto *layout = page(tabs, tr("General")); auto *group = new QGroupBox(tr("Game Settings")); group->setFixedHeight(225); layout->addWidget(group); layout->addStretch();
    auto *form = settingsForm(group, 15, 22, 179, 23); auto *id = numberField(form, tr("Game Identifier:"), QStringLiteral("gameid"), 0, 0, INT_MAX);
    auto *generate = settingsButton(group, tr("Generate new GUID and Id"), 15, 48, 148);
    auto *guid = settingsLabel(group, tr("Game GUID: %1").arg(m_document->value(QStringLiteral("gameguid"))), 280, 22, 350);
    guid->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copy = settingsButton(group, tr("Copy To Clipboard"), 280, 48, 98);
    connect(generate, &QPushButton::clicked, this, [this, guid, id] {
        const auto uuid = QUuid::createUuid(); m_document->setValue(QStringLiteral("gameguid"), uuid.toString().toUpper());
        id->setValue(int(uuid.data1 & INT_MAX)); guid->setText(tr("Game GUID: %1").arg(m_document->value(QStringLiteral("gameguid"))));
    });
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(m_document->value(QStringLiteral("gameguid"))); });
    auto *flagsBody = new QWidget(group); flagsBody->setGeometry(15, 88, 265, 92); auto *flags = new QVBoxLayout(flagsBody); flags->setContentsMargins(0, 0, 0, 0); flags->setSpacing(6);
    check(flags, tr("Use New Audio Engine"), QStringLiteral("use_new_audio"), true);
    check(flags, tr("Short-Circuit evaluations"), QStringLiteral("shortcircuit"), true);
    check(flags, tr("Use Fast Collision System"), QStringLiteral("use_fast_collision"));
    check(flags, tr("Fast Collision System Compatibility Mode"), QStringLiteral("fast_collision_compatibility"));
    colorField(settingsForm(group, 280, 91, 280, 22), tr("Color outside the room region:"), QStringLiteral("windowcolor"), true);
}
void GameSettingsWindow::createProjectInfo(QTabWidget *tabs)
{
    auto *layout = page(tabs, tr("Project Info")); auto *form = new QFormLayout; form->setHorizontalSpacing(7); form->setVerticalSpacing(10); form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter); layout->addLayout(form);
    textField(form, tr("Author:"), QStringLiteral("author")); textField(form, tr("Version:"), QStringLiteral("version"), QStringLiteral("100"));
    auto *changed = new QLineEdit(m_document->value(QStringLiteral("lastchanged"))); changed->setReadOnly(true); changed->setFixedHeight(19); form->addRow(tr("Last changed:"), changed);
    layout->addWidget(new QLabel(tr("Information:"))); auto *information = new QTextEdit; information->setAcceptRichText(false); information->setPlainText(m_document->value(QStringLiteral("information"))); layout->addWidget(information, 1); layout->addSpacing(25);
    connect(information, &QTextEdit::textChanged, this, [this, information] { m_document->setValue(QStringLiteral("information"), information->toPlainText()); });
}
void GameSettingsWindow::createGroups(QTabWidget *tabs, bool audio)
{
    auto *layout = page(tabs, audio ? tr("Audio Groups") : tr("Texture Groups")); auto *canvas = settingsCanvas(layout);
    settingsLabel(canvas, audio ? tr("Audio Groups:") : tr("Texture groups:"), 4, 0, 150);
    auto *list = new QListWidget; list->addItems(m_document->groups(audio)); settingsPlace(list, canvas, 4, 22, 150, 305);
    settingsLabel(canvas, audio ? tr("Audio Group sound list:") : tr("Texture group contents:"), 178, 0, 165);
    auto *members = new QListWidget; settingsPlace(members, canvas, 178, 22, 150, 305);
    auto *add = settingsButton(canvas, tr("Add"), 4, 346, 43); auto *rename = settingsButton(canvas, tr("Rename"), 55, 346, 47);
    auto *details = new QWidget; settingsPlace(details, canvas, 352, audio ? 0 : 160, 256, audio ? 327 : 250);
    auto *detailLayout = new QVBoxLayout(details); detailLayout->setContentsMargins(0, 0, 0, 0); detailLayout->setSpacing(8);
    QLabel *preview = nullptr;
    if (!audio) {
        settingsLabel(canvas, tr("Image preview:"), 352, 0, 150);
        preview = new QLabel; settingsPlace(preview, canvas, 352, 22, 256, 133); preview->setFrameShape(QFrame::Box); preview->setAlignment(Qt::AlignCenter);
    }
    connect(members, &QListWidget::currentItemChanged, this, [preview](QListWidgetItem *item) {
        if (!preview) return; preview->clear();
        if (item) { QPixmap pixmap(item->data(Qt::UserRole).toString()); if (!pixmap.isNull()) preview->setPixmap(pixmap.scaled(preview->size() - QSize(2, 2), Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
    });
    auto refresh = [this, audio, list, rename, detailLayout, members] {
        while (auto *item = detailLayout->takeAt(0)) { if (item->widget()) delete item->widget(); delete item; }
        const int index = list->currentRow(); rename->setEnabled(index > 0); if (index < 0) return;
        members->clear();
        for (const auto &resource : m_document->groupContents(audio, index)) { auto *item = new QListWidgetItem(resource.name, members); item->setData(Qt::UserRole, resource.thumbnailPath); item->setToolTip(resource.filePath); }
        auto *group = new QGroupBox(audio ? tr("Audio Group Settings") : tr("Texture Group Settings")); auto *fields = new QVBoxLayout(group); detailLayout->addWidget(group);
        if (!audio) {
            fields->setContentsMargins(12, 14, 7, 7); fields->setSpacing(4);
            check(fields, tr("Texture group NOT scaled"), QStringLiteral("textureGroup%1_scaled").arg(index));
            check(fields, tr("No cropping"), QStringLiteral("textureGroup%1_nocropping").arg(index));
            auto *form = new QFormLayout; form->setHorizontalSpacing(6); form->setVerticalSpacing(18); fields->addLayout(form);
            auto *border = numberField(form, tr("Texture border width (texels):"), QStringLiteral("textureGroup%1_border").arg(index), 2, 0, 256); border->setFixedWidth(46);
            fields->addSpacing(10); auto *parentForm = new QFormLayout; parentForm->setHorizontalSpacing(18); fields->addLayout(parentForm);
            const auto parents = m_document->textureParents(index);
            choice(parentForm, tr("Parent:"), QStringLiteral("textureGroup%1_parent").arg(index), parents, parents, QStringLiteral("<none>"));
            group->setFixedHeight(132);
        }
        auto *note = new QLabel(audio ? tr("Assign sounds to this group in the Sound editor. Audio group names are shared by all configurations.") : tr("Assign sprites, backgrounds and fonts to this group in their resource editors. Texture group settings apply to this configuration.")); note->setWordWrap(true); if (audio) fields->addWidget(note); else { group->setToolTip(note->text()); delete note; } detailLayout->addStretch();
    };
    auto editName = [this, list, audio, refresh](bool adding) {
        const int index = list->currentRow(); if (!adding && index <= 0) return;
        EditorInputDialog dialog(this); dialog.setWindowTitle(adding ? tr("Add Group") : tr("Rename Group")); dialog.setLabelText(tr("Name:")); dialog.setInputMode(QInputDialog::TextInput);
        dialog.setTextValue(adding ? (audio ? QStringLiteral("audiogroup%1") : QStringLiteral("TextureGroup%1")).arg(list->count()) : list->currentItem()->text());
        if (dialog.exec() != QDialog::Accepted) return;
        QString error; const QString name = dialog.textValue().trimmed();
        if (!(adding ? m_document->addGroup(audio, name, error) : m_document->renameGroup(audio, index, name, error))) { EditorMessageBox::warning(this, tr("Cannot Change Group"), error); return; }
        { const QSignalBlocker blocker(list); list->clear(); list->addItems(m_document->groups(audio)); list->setCurrentRow(adding ? list->count() - 1 : index); }
        refresh();
    };
    connect(add, &QPushButton::clicked, this, [editName] { editName(true); }); connect(rename, &QPushButton::clicked, this, [editName] { editName(false); });
    connect(list, &QListWidget::currentRowChanged, this, [refresh] { refresh(); }); list->setCurrentRow(0);
}
void GameSettingsWindow::bindImage(QLabel *preview, QPushButton *update, const QString &key, const QString &fileName, const QString &filter, const QSize &requiredSize)
{
    auto refresh = [this, key, preview] {
        preview->setToolTip(m_document->value(key));
        { QPixmap pixmap; pixmap.loadFromData(m_document->assetBytes(key)); if (!pixmap.isNull()) preview->setPixmap(pixmap.scaled(preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); else preview->setText(tr("No image")); }
    }; refresh();
    connect(update, &QPushButton::clicked, this, [this, key, fileName, filter, requiredSize, refresh] {
        const QString source = QFileDialog::getOpenFileName(this, tr("Select %1").arg(fileName), QString(), filter); if (source.isEmpty()) return;
        {
            QImageReader reader(source); const QSize size = reader.size();
            if (!size.isValid() || qint64(size.width()) * size.height() > 32 * 1024 * 1024 || !reader.canRead()) { EditorMessageBox::warning(this, tr("Invalid Image"), tr("Choose a valid image no larger than 32 megapixels.")); return; }
            const QByteArray expected = fileName.section(QLatin1Char('.'), -1).toLatin1();
            if (reader.format().toLower() != expected) { EditorMessageBox::warning(this, tr("Invalid Image"), tr("Choose a %1 file.").arg(QString::fromLatin1(expected).toUpper())); return; }
            if (requiredSize.isValid() && size != requiredSize) { EditorMessageBox::warning(this, tr("Invalid Image Size"), tr("This image must be %1 x %2 pixels.").arg(requiredSize.width()).arg(requiredSize.height())); return; }
        }
        QString error; if (!m_document->importAsset(key, source, fileName, error)) { EditorMessageBox::warning(this, tr("Cannot Import File"), error); return; } refresh();
    });
}

QGroupBox *GameSettingsWindow::radioGroup(const QString &title, const QString &key, const QStringList &labels, const QStringList &values, const QString &fallback)
{
    auto *group = new QGroupBox(title); auto *layout = new QVBoxLayout(group); layout->setContentsMargins(10, 8, 10, 8); layout->setSpacing(6);
    // Propagate the complete option list as a hard minimum, including the title
    // and margins, so a containing layout cannot compress the last radio row.
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *buttons = new QButtonGroup(group); const auto value = m_document->value(key, fallback);
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QRadioButton(labels.at(i)); buttons->addButton(button, i); button->setChecked(value == values.at(i)); layout->addWidget(button);
    }
    connect(buttons, static_cast<void (QButtonGroup::*)(int)>(&QButtonGroup::buttonClicked), this, [this, key, values](int index) { m_document->setValue(key, values.at(index)); }); return group;
}
void GameSettingsWindow::editTextAsset(const QString &title, const QString &key, const QString &fileName)
{
    EditorDialog dialog(this); dialog.setWindowTitle(title); dialog.resize(670, 460);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *edit = new QPlainTextEdit; layout->addWidget(edit, 1);
    QByteArray original = m_document->assetBytes(key); QTextCodec *codec = QTextCodec::codecForUtfText(original, QTextCodec::codecForLocale());
    edit->setPlainText(codec->toUnicode(original)); edit->document()->setModified(false);
    auto *buttons = new QHBoxLayout; layout->addLayout(buttons); auto *load = new QPushButton(tr("Load...")); auto *ok = new QPushButton(QIcon(QStringLiteral(":/images/editor/ok.png")), tr("OK")); auto *cancel = new QPushButton(tr("Cancel")); buttons->addWidget(load); buttons->addStretch(); buttons->addWidget(ok); buttons->addWidget(cancel);
    connect(load, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog, title, QString(), fileName.endsWith(QStringLiteral(".nsi")) ? tr("NSIS scripts (*.nsi)") : tr("Text files (*.txt)")); if (path.isEmpty()) return;
        QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) { EditorMessageBox::warning(&dialog, tr("Cannot Load File"), tr("Choose a readable text file smaller than 32 MB.")); return; }
        original = file.readAll(); codec = QTextCodec::codecForUtfText(original, QTextCodec::codecForLocale()); edit->setPlainText(codec->toUnicode(original)); edit->document()->setModified(true);
    });
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept); connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted && edit->document()->isModified()) m_document->setAssetBytes(key, fileName, codec->fromUnicode(edit->toPlainText()));
}
void GameSettingsWindow::createWindows(QTabWidget *tabs)
{
    auto *windows = new QTabWidget; windows->setObjectName(QStringLiteral("gameSettingsWindows")); windows->setTabPosition(QTabWidget::West); windows->tabBar()->setObjectName(QStringLiteral("gameSettingsWindowsTabBar")); tabs->addTab(windows, tr("Windows"));
    auto *generalLayout = page(windows, tr("General")); generalLayout->setContentsMargins(9, 8, 5, 5); auto *general = settingsCanvas(generalLayout, 626, 410);
    textField(settingsForm(general, 7, 0, 297, 22), tr("Display Name:"), QStringLiteral("display_name"), QStringLiteral("GameMaker: Studio"));
    auto *product = new QGroupBox(tr("Version Information")); settingsPlace(product, general, 8, 28, 301, 157);
    auto *info = settingsForm(product, 13, 21, 280, 132);
    auto *version = new QWidget; auto *versionRow = new QHBoxLayout(version); versionRow->setContentsMargins(0, 0, 0, 0); versionRow->setSpacing(7);
    const QStringList versionKeys = {QStringLiteral("major"), QStringLiteral("mainor"), QStringLiteral("release"), QStringLiteral("build")};
    for (int i = 0; i < versionKeys.size(); ++i) {
        const QString key = QStringLiteral("windows_%1_version").arg(versionKeys.at(i)); auto *spin = new QSpinBox; spin->setButtonSymbols(QAbstractSpinBox::NoButtons); spin->setRange(0, 65535); spin->setFixedSize(33, 19); spin->setValue(m_document->value(key, i == 0 ? QStringLiteral("1") : QStringLiteral("0")).toInt()); versionRow->addWidget(spin);
        connect(spin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, [this, key](int value) { m_document->setValue(key, QString::number(value)); });
    }
    versionRow->addStretch(); info->addRow(tr("Version:"), version);
    textField(info, tr("Company:"), QStringLiteral("windows_company_info")); textField(info, tr("Product:"), QStringLiteral("windows_product_info")); textField(info, tr("Copyright:"), QStringLiteral("windows_copyright_info")); textField(info, tr("Description:"), QStringLiteral("windows_description_info"));
    auto *options = new QGroupBox(tr("Options"), general);
    auto *optionsLayout = new QVBoxLayout(options); optionsLayout->setContentsMargins(8, 10, 8, 10); optionsLayout->setSpacing(9);
    check(optionsLayout, tr("Display the cursor"), QStringLiteral("showcursor"), true);
    auto *iconRow = new QHBoxLayout; iconRow->setSpacing(7); iconRow->addWidget(new QLabel(tr("Game icon:")));
    auto *icon = new QLabel; icon->setAlignment(Qt::AlignCenter); icon->setFixedSize(48, 48); iconRow->addWidget(icon);
    auto *updateIcon = new QPushButton(tr("Update")); updateIcon->setFixedSize(47, 23); iconRow->addWidget(updateIcon, 0, Qt::AlignTop); iconRow->addStretch(); optionsLayout->addLayout(iconRow);
    bindImage(icon, updateIcon, QStringLiteral("windows_game_icon"), QStringLiteral("runner_icon.ico"), tr("Windows icons (*.ico)"));
    auto *saveLocation = radioGroup(tr("Save Data location"), QStringLiteral("windows_save_location"), {QStringLiteral("%localappdata%\\<GameName>"), QStringLiteral("%appdata%\\<GameName>")}, {QStringLiteral("0"), QStringLiteral("1")}, QStringLiteral("0")); optionsLayout->addWidget(saveLocation, 0, Qt::AlignLeft);
    auto *sleepRow = new QHBoxLayout; auto *sleepForm = new QFormLayout; sleepForm->setContentsMargins(0, 0, 0, 0); sleepForm->setHorizontalSpacing(7);
    auto *sleep = numberField(sleepForm, tr("Sleep Margin (advanced):"), QStringLiteral("windows_sleep_margin"), 1, 0, 30); sleep->setFixedWidth(30); sleepRow->addLayout(sleepForm); sleepRow->addWidget(new QLabel(tr("ms"))); sleepRow->addStretch(); optionsLayout->addLayout(sleepRow);
    const int optionsHeight = qMax(209, options->sizeHint().height());
    options->setGeometry(8, 197, qMax(301, options->minimumSizeHint().width()), optionsHeight);
    general->setMinimumHeight(qMax(410, 197 + optionsHeight + 4));
    auto *splash = new QGroupBox(tr("Splash Screen")); settingsPlace(splash, general, 318, 0, 300, 257);
    auto *splashImage = new QLabel; splashImage->setAlignment(Qt::AlignCenter); splashImage->setStyleSheet(QStringLiteral("background: #000;")); settingsPlace(splashImage, splash, 6, 17, 238, 164);
    bindImage(splashImage, settingsButton(splash, tr("Update"), 251, 17, 44), QStringLiteral("windows_splash_screen"), QStringLiteral("splash.png"), tr("PNG images (*.png)"));
    auto *splashFlags = new QWidget(splash); splashFlags->setGeometry(15, 215, 225, 21); auto *splashFlagLayout = new QVBoxLayout(splashFlags); splashFlagLayout->setContentsMargins(0, 0, 0, 0); check(splashFlagLayout, tr("Display Splash Screen"), QStringLiteral("windows_use_splash"), true);
    // Keep the existing background-color option available below the reference preview.
    colorField(settingsForm(splash, 15, 188, 220, 21), tr("Background color:"), QStringLiteral("windows_splash_background_colour"), false);

    auto *graphicsLayout = page(windows, tr("Graphics")); graphicsLayout->setContentsMargins(9, 8, 5, 5); auto *graphics = settingsCanvas(graphicsLayout, 626, 410);
    auto *graphicsColumns = new QHBoxLayout(graphics); graphicsColumns->setContentsMargins(8, 6, 0, 9); graphicsColumns->setSpacing(9);
    graphicsColumns->setSizeConstraint(QLayout::SetMinimumSize);
    auto *graphicsOptions = new QVBoxLayout; graphicsOptions->setSpacing(12); graphicsColumns->addLayout(graphicsOptions);
    auto *flagsBox = new QGroupBox(tr("Options")); flagsBox->setMinimumWidth(253); graphicsOptions->addWidget(flagsBox); auto *flags = new QVBoxLayout(flagsBox); flags->setContentsMargins(8, 10, 8, 10); flags->setSpacing(6);
    flags->setSizeConstraint(QLayout::SetMinimumSize);
    check(flags, tr("Start in fullscreen mode"), QStringLiteral("fullscreen")); check(flags, tr("Interpolate colors between pixels"), QStringLiteral("interpolate"), true);
    auto syncFlag = [this, flags](const QString &title, quint32 mask) {
        auto *box = new QCheckBox(title); box->setChecked((quint32(m_document->value(QStringLiteral("sync_vertex"), QStringLiteral("0")).toLongLong()) & mask) != 0); flags->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [this, mask](bool checked) { quint32 value = quint32(m_document->value(QStringLiteral("sync_vertex"), QStringLiteral("0")).toLongLong()); value = checked ? value | mask : value & ~mask; m_document->setValue(QStringLiteral("sync_vertex"), QString::number(qint32(value))); });
    };
    syncFlag(tr("Force software vertex processing"), 0x80000000u); syncFlag(tr("Use synchronization to avoid tearing"), 1);
    check(flags, tr("Allow the player to resize the game window"), QStringLiteral("sizeable"));
    auto *scaling = radioGroup(tr("Scaling"), QStringLiteral("scale"), {tr("Keep aspect ratio"), tr("Full scale")}, {QStringLiteral("-1"), QStringLiteral("0")}, QStringLiteral("-1")); scaling->setMinimumWidth(140); flags->addWidget(scaling, 0, Qt::AlignLeft);
    check(flags, tr("Allow switching to fullscreen"), QStringLiteral("screenkey"), true); check(flags, tr("Borderless Window"), QStringLiteral("borderless"));
    auto *advancedBox = new QGroupBox(tr("Advanced")); graphicsOptions->addWidget(advancedBox); auto *advanced = new QVBoxLayout(advancedBox); advanced->setContentsMargins(8, 10, 8, 10); advanced->setSpacing(6);
    advanced->setSizeConstraint(QLayout::SetMinimumSize);
    check(advanced, tr("Create textures on demand"), QStringLiteral("windows_create_textures_on_demand"));
    auto *vertex = radioGroup(tr("Vertex buffer method"), QStringLiteral("windows_vertex_buffer_method2"), {tr("Fast"), tr("Compatible"), tr("Most compatible")}, {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2")}, QStringLiteral("1")); vertex->setMinimumWidth(190); advanced->addWidget(vertex, 0, Qt::AlignLeft);
    check(advanced, tr("Alternate synchronization method"), QStringLiteral("windows_alternate_sync_method"));
    graphicsOptions->addStretch();
    auto *texture = new QGroupBox(tr("Texture Pages")); texture->setFixedSize(188, 114); graphicsColumns->addWidget(texture, 0, Qt::AlignTop); graphicsColumns->addStretch();
    settingsLabel(texture, tr("Size:"), 9, 20, 120);
    const QStringList sizes = {QStringLiteral("256"), QStringLiteral("512"), QStringLiteral("1024"), QStringLiteral("2048"), QStringLiteral("4096"), QStringLiteral("8192")}; QStringList sizeLabels; for (const auto &size : sizes) sizeLabels.append(size + QLatin1Char('x') + size);
    choice(settingsForm(texture, 9, 39, 145, 23), QString(), QStringLiteral("windows_texture_page"), sizeLabels, sizes, QStringLiteral("2048"));

    auto *installerLayout = page(windows, tr("Installer")); installerLayout->setContentsMargins(9, 8, 5, 5); auto *installer = settingsCanvas(installerLayout, 626, 410);
    auto *images = new QGroupBox(tr("Graphics")); settingsPlace(images, installer, 0, 0, 338, 374);
    settingsLabel(images, tr("Finished:"), 13, 19, 130); settingsLabel(images, tr("Header:"), 177, 19, 120);
    auto *finished = new QLabel; finished->setAlignment(Qt::AlignCenter); finished->setStyleSheet(QStringLiteral("background: #000;")); settingsPlace(finished, images, 14, 39, 140, 279);
    bindImage(finished, settingsButton(images, tr("Update"), 14, 330, 87), QStringLiteral("windows_runner_finished"), QStringLiteral("Runner_finish.bmp"), tr("Bitmap images (*.bmp)"), QSize(164, 314)); settingsLabel(images, QStringLiteral("164x314"), 104, 330, 64);
    auto *header = new QLabel; header->setAlignment(Qt::AlignCenter); header->setStyleSheet(QStringLiteral("background: #000;")); settingsPlace(header, images, 178, 39, 150, 57);
    bindImage(header, settingsButton(images, tr("Update"), 178, 105, 96), QStringLiteral("windows_runner_header"), QStringLiteral("Runner_header.bmp"), tr("Bitmap images (*.bmp)"), QSize(150, 57)); settingsLabel(images, QStringLiteral("150x57"), 280, 105, 50);
    auto *scripts = new QGroupBox(tr("Scripts")); settingsPlace(scripts, installer, 362, 0, 200, 98);
    settingsLabel(scripts, tr("Installer NSI script"), 12, 22, 100); settingsLabel(scripts, tr("License agreement"), 12, 62, 100);
    connect(settingsButton(scripts, tr("Edit"), 110, 19, 70), &QPushButton::clicked, this, [this] { editTextAsset(tr("Installer NSI script"), QStringLiteral("windows_nsis_file"), QStringLiteral("RunnerInstaller.nsi")); });
    connect(settingsButton(scripts, tr("Edit"), 110, 59, 70), &QPushButton::clicked, this, [this] { editTextAsset(tr("License agreement"), QStringLiteral("windows_license"), QStringLiteral("License.txt")); });
}
bool GameSettingsWindow::save()
{
    if (QWidget *focus = QApplication::focusWidget()) if (isAncestorOf(focus)) focus->clearFocus();
    const bool changed = isModified(); QString error;
    if (!m_document->save(error)) { EditorMessageBox::warning(this, tr("Cannot Save Global Game Settings"), error); return false; }
    if (changed) emit resourceSaved(ResourceType::GameSettings, filePath(), QString()); return true;
}
QString GameSettingsWindow::filePath() const { return m_document->filePath(); }
bool GameSettingsWindow::isModified() const { return m_document->isModified(); }
void GameSettingsWindow::relocate(const QString &oldDirectory, const QString &newDirectory) { m_document->relocate(oldDirectory, newDirectory); }
void GameSettingsWindow::closeEvent(QCloseEvent *event)
{
    if (!m_discard && isModified()) {
        const auto answer = EditorMessageBox::question(this, tr("Global Game Settings"), tr("Save changes to %1?").arg(m_document->name()), EditorMessageBox::Save | EditorMessageBox::Discard | EditorMessageBox::Cancel, EditorMessageBox::Save);
        if (answer == EditorMessageBox::Cancel || (answer == EditorMessageBox::Save && !save())) { event->ignore(); return; }
    }
    event->accept();
}
