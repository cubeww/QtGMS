#include "extensioneditordialogs.h"
#include "extensiondocument.h"
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include "actionxml.h"
#include "editordialog.h"
#include "editorstandarddialogs.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <functional>

static QDialogButtonBox *extensionButtons(EditorDialog &dialog, QVBoxLayout *layout)
{
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setIcon(QIcon(QStringLiteral(":/images/editor/ok.png")));
    buttons->button(QDialogButtonBox::Cancel)->setIcon(QIcon(QStringLiteral(":/object/controls/delete.png")));
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    return buttons;
}
static QLineEdit *extensionEdit(QFormLayout *form, const QString &label, QDomElement element, const QString &key)
{
    auto *edit = new QLineEdit(ActionXml::text(element, key)); form->addRow(label, edit); return edit;
}
static bool extensionIdentifier(const QString &value)
{ return QRegExp(QStringLiteral("[A-Za-z_][A-Za-z0-9_]*")).exactMatch(value); }
static QComboBox *extensionValueType(int value)
{
    auto *combo = new QComboBox; combo->addItem(QObject::tr("string"), 1); combo->addItem(QObject::tr("double"), 2);
    if (combo->findData(value) < 0) combo->addItem(QObject::tr("Type %1").arg(value), value);
    combo->setCurrentIndex(combo->findData(value)); return combo;
}
static void extensionClearChildren(QDomElement parent, const QString &tag)
{ for (auto child : ActionXml::elements(parent, tag)) parent.removeChild(child); }

// Masks are signed 64-bit values in GMX. Keep all existing platform bits,
// including targets unavailable on this host, without converting through double.
static QComboBox *extensionTargetMask(const QString &value)
{
    auto *combo = new QComboBox; combo->setEditable(true);
    combo->addItem(QObject::tr("All targets"), QStringLiteral("9223372036854775807"));
    combo->addItem(QObject::tr("No targets"), QStringLiteral("0"));
    combo->addItem(QObject::tr("Windows (VM + YYC)"), QStringLiteral("1048640"));
    int index = combo->findData(value);
    if (index < 0) { combo->addItem(value, value); index = combo->count() - 1; }
    combo->setCurrentIndex(index);
    combo->setToolTip(QObject::tr("Choose a target preset or enter a GMX target mask."));
    return combo;
}
static QString extensionMaskValue(QComboBox *combo)
{
    const int index = combo->findText(combo->currentText());
    return index < 0 ? combo->currentText().trimmed() : combo->itemData(index).toString();
}
static QTableWidget *extensionTargets(QDomElement owner)
{
    auto *table = new QTableWidget(0, 2); table->setHorizontalHeaderLabels({QObject::tr("Configuration"), QObject::tr("Copies To")});
    table->horizontalHeader()->setStretchLastSection(true); table->verticalHeader()->hide(); table->setMinimumHeight(85);
    auto configs = ActionXml::elements(owner.firstChildElement(QStringLiteral("ConfigOptions")), QStringLiteral("Config"));
    for (auto config : configs) {
        int row = table->rowCount(); table->insertRow(row); auto *name = new QTableWidgetItem(config.attribute(QStringLiteral("name"))); name->setFlags(name->flags() & ~Qt::ItemIsEditable); table->setItem(row, 0, name);
        table->setCellWidget(row, 1, extensionTargetMask(ActionXml::text(config, QStringLiteral("CopyToMask")))); table->setRowHeight(row, 24);
    }
    return table;
}
static bool extensionValidMasks(QTableWidget *table, int column)
{
    for (int row = 0; row < table->rowCount(); ++row) { bool valid; extensionMaskValue(static_cast<QComboBox *>(table->cellWidget(row, column))).toLongLong(&valid); if (!valid) return false; }
    return true;
}
static void extensionApplyTargets(QDomElement owner, QTableWidget *table)
{
    const auto configs = ActionXml::elements(owner.firstChildElement(QStringLiteral("ConfigOptions")), QStringLiteral("Config"));
    for (int row = 0; row < table->rowCount(); ++row)
        ActionXml::setText(configs.at(row), QStringLiteral("CopyToMask"), extensionMaskValue(static_cast<QComboBox *>(table->cellWidget(row, 1))));
}

bool ExtensionEditorDialogs::function(QDomElement function, int fileKind, QWidget *parent)
{
    EditorDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Edit Extension Function Properties")); dialog.resize(640, 340);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); layout->setContentsMargins(12, 10, 12, 8);
    auto *form = new QFormLayout; layout->addLayout(form);
    auto *name = extensionEdit(form, QObject::tr("Name:"), function, QStringLiteral("name"));
    auto *external = extensionEdit(form, QObject::tr("External Name:"), function, QStringLiteral("externalName"));
    auto *help = extensionEdit(form, QObject::tr("Help:"), function, QStringLiteral("help"));
    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(QObject::tr("Return Type:")));
    auto *result = extensionValueType(ActionXml::text(function, QStringLiteral("returnType")).toInt()); options->addWidget(result);
    auto *kind = new QComboBox;
    if (fileKind == 1) { kind->addItem(QStringLiteral("STDCALL"), 11); kind->addItem(QStringLiteral("CDECL"), 12); }
    else kind->addItem(fileKind == 2 ? QStringLiteral("GML") : fileKind == 5 ? QStringLiteral("JavaScript") : QObject::tr("Extension"), fileKind);
    const int existingKind = ActionXml::text(function, QStringLiteral("kind")).toInt();
    if (kind->findData(existingKind) < 0) kind->addItem(QObject::tr("Default (%1)").arg(existingKind), existingKind);
    kind->setCurrentIndex(kind->findData(existingKind)); options->addWidget(new QLabel(QObject::tr("Type:"))); options->addWidget(kind); form->addRow(options);
    auto *args = new QTableWidget(0, 1); args->setHorizontalHeaderLabels({QObject::tr("Argument type")}); args->horizontalHeader()->setStretchLastSection(true); layout->addWidget(args, 1);
    auto appendArg = [args](int type) { int row = args->rowCount(); args->insertRow(row); args->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row))); args->setCellWidget(row, 0, extensionValueType(type)); args->setRowHeight(row, 23); };
    for (auto arg : ActionXml::elements(function.firstChildElement(QStringLiteral("args")), QStringLiteral("arg"))) appendArg(arg.text().toInt());
    auto *actions = new QHBoxLayout; auto *add = new QPushButton(QStringLiteral("+")), *remove = new QPushButton(QStringLiteral("-"));
    add->setFixedWidth(25); remove->setFixedWidth(25); actions->addWidget(add); actions->addWidget(remove);
    auto *variable = new QCheckBox(QObject::tr("Variable Length Arguments")); variable->setChecked(ActionXml::text(function, QStringLiteral("argCount")).toInt() == -1); actions->addWidget(variable); actions->addStretch(); layout->addLayout(actions);
    QObject::connect(add, &QPushButton::clicked, &dialog, [appendArg, args] { if (args->rowCount() < 16) appendArg(2); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [args] { if (args->rowCount()) args->removeRow(args->currentRow() < 0 ? args->rowCount() - 1 : args->currentRow()); });
    auto *buttons = extensionButtons(dialog, layout);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!extensionIdentifier(name->text()) || external->text().trimmed().isEmpty()) { EditorMessageBox::warning(&dialog, QObject::tr("Function Properties"), QObject::tr("Enter a valid function name and a non-empty external name.")); return; }
        if (fileKind == 1 && args->rowCount() >= 4) for (int row = 0; row < args->rowCount(); ++row)
            if (static_cast<QComboBox *>(args->cellWidget(row, 0))->currentData().toInt() != 2) { EditorMessageBox::warning(&dialog, QObject::tr("Function Arguments"), QObject::tr("Native functions with four or more arguments require double arguments.")); return; }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    ActionXml::setText(function, QStringLiteral("name"), name->text()); ActionXml::setText(function, QStringLiteral("externalName"), external->text()); ActionXml::setText(function, QStringLiteral("help"), help->text());
    ActionXml::setText(function, QStringLiteral("kind"), kind->currentData().toString()); ActionXml::setText(function, QStringLiteral("returnType"), result->currentData().toString());
    ActionXml::setText(function, QStringLiteral("argCount"), variable->isChecked() ? QStringLiteral("-1") : QString::number(args->rowCount()));
    auto container = ActionXml::child(function, QStringLiteral("args")); extensionClearChildren(container, QStringLiteral("arg"));
    for (int row = 0; row < args->rowCount(); ++row) { auto arg = function.ownerDocument().createElement(QStringLiteral("arg")); arg.appendChild(function.ownerDocument().createTextNode(static_cast<QComboBox *>(args->cellWidget(row, 0))->currentData().toString())); container.appendChild(arg); }
    return true;
}
bool ExtensionEditorDialogs::constant(QDomElement constant, QWidget *parent)
{
    EditorDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Edit Extension Macros")); dialog.resize(600, 165);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout; layout->addLayout(form);
    auto *name = extensionEdit(form, QObject::tr("Name:"), constant, QStringLiteral("name")); auto *value = extensionEdit(form, QObject::tr("Value:"), constant, QStringLiteral("value"));
    auto *hidden = new QCheckBox(QObject::tr("Hidden")); hidden->setChecked(ActionXml::text(constant, QStringLiteral("hidden")).toInt() != 0); form->addRow(hidden);
    auto *buttons = extensionButtons(dialog, layout);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!extensionIdentifier(name->text())) { EditorMessageBox::warning(&dialog, QObject::tr("Macro Properties"), QObject::tr("Use letters, numbers and underscores; the name cannot start with a number.")); return; } dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    ActionXml::setText(constant, QStringLiteral("name"), name->text()); ActionXml::setText(constant, QStringLiteral("value"), value->text()); ActionXml::setText(constant, QStringLiteral("hidden"), hidden->isChecked() ? QStringLiteral("-1") : QStringLiteral("0")); return true;
}
bool ExtensionEditorDialogs::file(QDomElement file, ExtensionDocument *document, ExtensionState &state, QWidget *parent)
{
    EditorDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Edit Extension File Properties")); dialog.resize(720, 500);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *form = new QFormLayout; layout->addLayout(form);
    auto *name = extensionEdit(form, QObject::tr("Name:"), file, QStringLiteral("filename")); name->setReadOnly(true);
    auto *init = new QComboBox, *final = new QComboBox; init->setEditable(true); final->setEditable(true); init->addItem(QString()); final->addItem(QString());
    for (auto fn : ActionXml::elements(file.firstChildElement(QStringLiteral("functions")), QStringLiteral("function"))) { const QString name = ActionXml::text(fn, QStringLiteral("name")); init->addItem(name); final->addItem(name); }
    init->setEditText(ActionXml::text(file, QStringLiteral("init"))); final->setEditText(ActionXml::text(file, QStringLiteral("final")));
    form->addRow(QObject::tr("Init Function:"), init); form->addRow(QObject::tr("Final Function:"), final);
    auto *kind = new QComboBox; const QStringList types = {QObject::tr("Native library"), QStringLiteral("GML"), QObject::tr("Library"), QObject::tr("Other / Placeholder"), QStringLiteral("JavaScript")};
    for (int i = 0; i < types.size(); ++i) kind->addItem(types.at(i), i + 1);
    int originalKind = ActionXml::text(file, QStringLiteral("kind")).toInt();
    if (kind->findData(originalKind) < 0) kind->addItem(QObject::tr("Type %1").arg(originalKind), originalKind);
    kind->setCurrentIndex(kind->findData(originalKind)); form->addRow(QObject::tr("Kind:"), kind);
    auto *uncompress = new QCheckBox(QObject::tr("Uncompress as zip file")); uncompress->setChecked(ActionXml::text(file, QStringLiteral("uncompress")).toInt() != 0); form->addRow(uncompress);
    layout->addWidget(new QLabel(QObject::tr("Proxy Files:")));
    auto *proxies = new QTableWidget(0, 2); proxies->setHorizontalHeaderLabels({QObject::tr("File name"), QObject::tr("Target")}); proxies->horizontalHeader()->setStretchLastSection(true); proxies->setColumnWidth(0, 350); layout->addWidget(proxies, 1);
    const auto originalProxies = ActionXml::elements(file.firstChildElement(QStringLiteral("ProxyFiles")), QStringLiteral("ProxyFile"));
    auto addProxyRow = [proxies](const QString &name, const QString &mask, int original) {
        int row = proxies->rowCount(); proxies->insertRow(row); auto *item = new QTableWidgetItem(name); item->setData(Qt::UserRole, original); item->setFlags(item->flags() & ~Qt::ItemIsEditable); proxies->setItem(row, 0, item); proxies->setCellWidget(row, 1, extensionTargetMask(mask)); proxies->setRowHeight(row, 24);
    };
    for (int i = 0; i < originalProxies.size(); ++i) addProxyRow(ActionXml::text(originalProxies.at(i), QStringLiteral("Name")), ActionXml::text(originalProxies.at(i), QStringLiteral("TargetMask")), i);
    auto *proxyButtons = new QHBoxLayout; auto *addProxy = new QPushButton(QStringLiteral("+")), *removeProxy = new QPushButton(QStringLiteral("-"));
    addProxy->setFixedWidth(25); removeProxy->setFixedWidth(25); proxyButtons->addWidget(addProxy); proxyButtons->addWidget(removeProxy); proxyButtons->addStretch(); layout->addLayout(proxyButtons);
    QObject::connect(addProxy, &QPushButton::clicked, &dialog, [&] {
        const QString path = QFileDialog::getOpenFileName(&dialog, QObject::tr("Add Proxy File")); if (path.isEmpty()) return;
        const QString name = QFileInfo(path).fileName();
        for (int row = 0; row < proxies->rowCount(); ++row) if (proxies->item(row, 0)->text().compare(name, Qt::CaseInsensitive) == 0) { EditorMessageBox::warning(&dialog, QObject::tr("Proxy File"), QObject::tr("That proxy file is already included.")); return; }
        if (QFileInfo::exists(QDir(document->contentDirectory()).filePath(name)) || state.files.contains(name)) { EditorMessageBox::warning(&dialog, QObject::tr("Proxy File"), QObject::tr("That filename already exists in the extension directory.")); return; }
        QFile input(path); if (!input.open(QIODevice::ReadOnly)) { EditorMessageBox::critical(&dialog, QObject::tr("Proxy File"), input.errorString()); return; }
        QByteArray bytes = input.readAll(); QString error;
        if (input.error() != QFile::NoError || !document->stageFile(state, name, bytes, error)) { EditorMessageBox::critical(&dialog, QObject::tr("Proxy File"), error.isEmpty() ? input.errorString() : error); return; }
        addProxyRow(name, name.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive) ? QStringLiteral("1048640") : QStringLiteral("0"), -1);
    });
    QObject::connect(removeProxy, &QPushButton::clicked, &dialog, [proxies] { if (proxies->rowCount()) proxies->removeRow(proxies->currentRow() < 0 ? proxies->rowCount() - 1 : proxies->currentRow()); });
    auto *targets = extensionTargets(file); layout->addWidget(targets);
    auto *buttons = extensionButtons(dialog, layout); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!extensionValidMasks(targets, 1) || !extensionValidMasks(proxies, 1)) { EditorMessageBox::warning(&dialog, QObject::tr("Target Selection"), QObject::tr("Enter a valid 64-bit target mask.")); return; } dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    extensionApplyTargets(file, targets);
    auto container = ActionXml::child(file, QStringLiteral("ProxyFiles")); extensionClearChildren(container, QStringLiteral("ProxyFile"));
    for (int row = 0; row < proxies->rowCount(); ++row) {
        const int original = proxies->item(row, 0)->data(Qt::UserRole).toInt();
        auto proxy = original < 0 ? file.ownerDocument().createElement(QStringLiteral("ProxyFile")) : originalProxies.at(original).cloneNode(true).toElement();
        ActionXml::setText(proxy, QStringLiteral("Name"), proxies->item(row, 0)->text()); ActionXml::setText(proxy, QStringLiteral("TargetMask"), extensionMaskValue(static_cast<QComboBox *>(proxies->cellWidget(row, 1)))); container.appendChild(proxy);
    }
    ActionXml::setText(file, QStringLiteral("init"), init->currentText()); ActionXml::setText(file, QStringLiteral("final"), final->currentText()); ActionXml::setText(file, QStringLiteral("kind"), kind->currentData().toString()); ActionXml::setText(file, QStringLiteral("uncompress"), uncompress->isChecked() ? QStringLiteral("-1") : QStringLiteral("0")); return true;
}
static QWidget *extensionStringList(QDomElement root, const QString &containerName, const QString &itemName,
                                    bool weakReferences, QList<std::function<void()>> &apply)
{
    auto *widget = new QWidget; auto *layout = new QVBoxLayout(widget); layout->setContentsMargins(0, 0, 0, 0);
    auto *table = new QTableWidget(0, weakReferences ? 2 : 1);
    table->setHorizontalHeaderLabels(weakReferences ? QStringList({QObject::tr("Name"), QObject::tr("Weak reference")}) : QStringList({QObject::tr("Name")}));
    table->horizontalHeader()->setStretchLastSection(true); table->setColumnWidth(0, 340); layout->addWidget(table, 1);
    QList<QDomElement> original;
    auto container = root.firstChildElement(containerName);
    for (auto item = container.firstChildElement(); !item.isNull(); item = item.nextSiblingElement()) original.append(item);
    auto append = [table, weakReferences](const QString &name, bool weak, int index) {
        int row = table->rowCount(); table->insertRow(row); auto *text = new QTableWidgetItem(name); text->setData(Qt::UserRole, index); table->setItem(row, 0, text); table->setRowHeight(row, 23);
        if (weakReferences) { auto *check = new QTableWidgetItem; check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable); check->setCheckState(weak ? Qt::Checked : Qt::Unchecked); table->setItem(row, 1, check); }
    };
    for (int i = 0; i < original.size(); ++i) append(original.at(i).text(), original.at(i).attribute(QStringLiteral("weak")).toInt() != 0, i);
    auto *buttons = new QHBoxLayout; auto *add = new QPushButton(QObject::tr("Add")), *remove = new QPushButton(QObject::tr("Remove")); buttons->addWidget(add); buttons->addWidget(remove); buttons->addStretch(); layout->addLayout(buttons);
    QObject::connect(add, &QPushButton::clicked, widget, [append, table] { append(QString(), false, -1); table->setCurrentCell(table->rowCount() - 1, 0); table->editItem(table->currentItem()); });
    QObject::connect(remove, &QPushButton::clicked, widget, [table] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
    apply.append([root, containerName, itemName, weakReferences, table, original] {
        auto container = ActionXml::child(root, containerName);
        for (auto item : original) container.removeChild(item);
        for (int row = 0; row < table->rowCount(); ++row) {
            const QString text = table->item(row, 0)->text().trimmed(); if (text.isEmpty()) continue;
            const int index = table->item(row, 0)->data(Qt::UserRole).toInt();
            auto item = index < 0 ? root.ownerDocument().createElement(itemName) : original.at(index).cloneNode(true).toElement();
            while (!item.firstChild().isNull()) item.removeChild(item.firstChild());
            item.appendChild(root.ownerDocument().createTextNode(text));
            if (weakReferences) item.setAttribute(QStringLiteral("weak"), table->item(row, 1)->checkState() == Qt::Checked ? -1 : 0);
            container.appendChild(item);
        }
    });
    return widget;
}

bool ExtensionEditorDialogs::package(QDomElement root, QWidget *parent)
{
    EditorDialog dialog(parent); dialog.setWindowTitle(QObject::tr("Edit Extension Package Properties")); dialog.resize(690, 490);
    auto *layout = new QVBoxLayout(dialog.bodyWidget()); auto *tabs = new QTabWidget; layout->addWidget(tabs, 1);
    QList<std::function<void()>> apply;
    auto addPage = [tabs](const QString &name) { auto *page = new QWidget; auto *form = new QFormLayout(page); form->setContentsMargins(16, 14, 16, 14); tabs->addTab(page, name); return form; };
    auto add = [root, &apply](QFormLayout *form, const QString &label, const QString &key) {
        auto *edit = extensionEdit(form, label, root, key); apply.append([root, edit, key] { ActionXml::setText(root, key, edit->text()); }); return edit;
    };
    auto memo = [root, &apply](QFormLayout *form, const QString &label, const QString &key) {
        auto *edit = new QPlainTextEdit(ActionXml::text(root, key)); edit->setMinimumHeight(65); form->addRow(label, edit); apply.append([root, edit, key] { ActionXml::setText(root, key, edit->toPlainText()); });
    };
    auto *general = addPage(QObject::tr("General")); auto *name = add(general, QObject::tr("Name:"), QStringLiteral("name")); name->setReadOnly(true);
    auto *version = add(general, QObject::tr("Version:"), QStringLiteral("version")); add(general, QObject::tr("Package ID:"), QStringLiteral("packageID"));
    add(general, QObject::tr("Date:"), QStringLiteral("date")); add(general, QObject::tr("License:"), QStringLiteral("license")); add(general, QObject::tr("Help File:"), QStringLiteral("helpfile")); memo(general, QObject::tr("Description:"), QStringLiteral("description"));
    auto *ios = addPage(QStringLiteral("iOS")); add(ios, QObject::tr("Class Name:"), QStringLiteral("classname")); add(ios, QObject::tr("Source Directory:"), QStringLiteral("sourcedir")); add(ios, QObject::tr("Linker Flags:"), QStringLiteral("maclinkerflags")); add(ios, QObject::tr("Compiler Flags:"), QStringLiteral("maccompilerflags")); memo(ios, QObject::tr("Inject to Info.plist:"), QStringLiteral("iosplistinject"));
    auto *android = addPage(QStringLiteral("Android")); add(android, QObject::tr("Class Name:"), QStringLiteral("androidclassname")); add(android, QObject::tr("Source Directory:"), QStringLiteral("androidsourcedir"));
    memo(android, QObject::tr("Manifest Level:"), QStringLiteral("androidmanifestinject")); memo(android, QObject::tr("Application Level:"), QStringLiteral("androidinject")); memo(android, QObject::tr("Activity Level:"), QStringLiteral("androidactivityinject")); memo(android, QObject::tr("Gradle Dependencies:"), QStringLiteral("gradleinject"));
    auto *frameworkPage = new QTabWidget;
    frameworkPage->addTab(extensionStringList(root, QStringLiteral("iosSystemFrameworks"), QStringLiteral("framework"), true, apply), QObject::tr("System Frameworks"));
    frameworkPage->addTab(extensionStringList(root, QStringLiteral("iosThirdPartyFrameworks"), QStringLiteral("framework"), true, apply), QObject::tr("Third-party Frameworks"));
    tabs->addTab(frameworkPage, QObject::tr("iOS Frameworks"));
    tabs->addTab(extensionStringList(root, QStringLiteral("androidPermissions"), QStringLiteral("permission"), false, apply), QObject::tr("Android Permissions"));
    auto *targetsPage = addPage(QObject::tr("Copies To")); auto *targets = extensionTargets(root); targetsPage->addRow(targets);
    auto *buttons = extensionButtons(dialog, layout); QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!extensionValidMasks(targets, 1)) { EditorMessageBox::warning(&dialog, QObject::tr("Target Selection"), QObject::tr("Enter a valid 64-bit target mask.")); return; }
        if (!QRegExp(QStringLiteral("[0-9]+\\.[0-9]+\\.[0-9]+")).exactMatch(version->text())) { EditorMessageBox::warning(&dialog, QObject::tr("Extension Version"), QObject::tr("Enter a version such as 1.0.0.")); return; } dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    for (const auto &change : apply) change(); extensionApplyTargets(root, targets); return true;
}
