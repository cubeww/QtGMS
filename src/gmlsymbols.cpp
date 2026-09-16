#include "gmlsymbols.h"
#include "project.h"
#include "textfiledocument.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QTextBlock>
#include <QTextDocument>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>

GmlSymbols &GmlSymbols::instance()
{
    static GmlSymbols Symbols;
    return Symbols;
}

GmlSymbols::GmlSymbols()
{
    QFile file(QStringLiteral(":/gmlbuiltins.txt"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot load built-in GML symbols");
    const QRegularExpression entry(QStringLiteral("^([A-Za-z_][A-Za-z_0-9]*)(.*)$"));
    QMap<QString, CodeCompletionItem> catalogue;
    for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        const auto match = entry.match(line.trimmed());
        if (!match.hasMatch()) continue;
        CodeCompletionItem item;
        item.name = match.captured(1);
        QString suffix = match.captured(2).trimmed();
        while (!suffix.isEmpty() && QStringLiteral("&*#;").contains(suffix.right(1))) {
            const QChar marker = suffix.at(suffix.size() - 1);
            if (marker == QLatin1Char('&')) item.obsolete = true;
            if (marker == QLatin1Char('*')) item.readOnly = true;
            if (marker == QLatin1Char('#')) item.kind = CodeCompletionItem::Kind::Constant;
            suffix.chop(1); suffix = suffix.trimmed();
        }
        if (suffix.startsWith(QLatin1Char('('))) item.kind = CodeCompletionItem::Kind::Function;
        else if (!suffix.isEmpty() && !suffix.startsWith(QLatin1Char('['))) continue;
        item.detail = item.name + suffix;
        if (catalogue.contains(item.name)) {
            auto &existing = catalogue[item.name];
            if (!existing.detail.split(QLatin1Char('\n')).contains(item.detail)) existing.detail += QLatin1Char('\n') + item.detail;
            existing.obsolete = existing.obsolete && item.obsolete;
        } else catalogue.insert(item.name, item);
    }
    for (const QString &word : QStringLiteral("if then else begin end while do for repeat until with switch case default break continue exit return var globalvar div mod and or xor not enum").split(QLatin1Char(' '))) {
        CodeCompletionItem item; item.name = word; item.kind = CodeCompletionItem::Kind::Keyword;
        catalogue.insert(word, item);
    }
    for (const QString &word : QStringLiteral("undefined pointer_null pointer_invalid").split(QLatin1Char(' '))) {
        CodeCompletionItem item; item.name = word; item.kind = CodeCompletionItem::Kind::Constant;
        catalogue.insert(word, item);
    }
    for (const CodeCompletionItem &item : catalogue) {
        items.append(item);
        switch (item.kind) {
        case CodeCompletionItem::Kind::Keyword: keywords.insert(item.name); break;
        case CodeCompletionItem::Kind::Function: functions.insert(item.name); break;
        case CodeCompletionItem::Kind::Constant: constants.insert(item.name); break;
        default: variables.insert(item.name); break;
        }
    }
}

void GmlSymbols::watchScript(TextFileDocument *document)
{
    if (m_openScripts.contains(document)) return;
    m_openScripts.insert(document, document->textDocument()->firstBlock().text());
    connect(document, &TextFileDocument::saved, document, [this, document] {
        m_scriptHeaders.remove(document->filePath());
    });
    // The QTextDocument is a child of the file document. Stop observing it
    // before that child is torn down, rather than keeping its owner captured
    // by a connection whose context is the application-wide symbol catalogue.
    connect(document->textDocument(), &QTextDocument::contentsChanged, document, [this, document] {
        const QString header = document->textDocument()->firstBlock().text();
        if (m_openScripts.value(document) == header) return;
        m_openScripts[document] = header;
        emit scriptSignaturesChanged();
    });
    connect(document, &QObject::destroyed, this, [this, document] {
        m_openScripts.remove(document);
        emit scriptSignaturesChanged();
    });
    emit scriptSignaturesChanged();
}

QString GmlSymbols::scriptSignature(const QString &name, const QString &path)
{
    QString header;
    bool open = false;
    for (auto it = m_openScripts.constBegin(); it != m_openScripts.constEnd(); ++it) {
        if (it.key()->filePath() == path) { header = it.value(); open = true; break; }
    }
    if (!open) {
        const QFileInfo info(path);
        auto &cached = m_scriptHeaders[path];
        if (cached.size != info.size() || cached.modified != info.lastModified()) {
            cached.size = info.size(); cached.modified = info.lastModified(); cached.text.clear();
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                QTextStream stream(&file); stream.setCodec("UTF-8"); stream.setAutoDetectUnicode(true);
                cached.text = stream.readLine(4096);
                // A declaration must fit on the first line; never read the script body.
                if (cached.text.size() == 4096) cached.text.clear();
            }
        }
        header = cached.text;
    }
    static const QRegularExpression Declaration(QStringLiteral("^\\s*///\\s*[A-Za-z_][A-Za-z_0-9]*\\s*(\\([^\\r\\n]*\\))\\s*$"));
    const auto match = Declaration.match(header);
    // Resource renaming intentionally leaves code alone. Bind the declaration's
    // parameters to the actual resource name, including after a rename or copy.
    return match.hasMatch() ? name + match.captured(1) : name;
}

static void appendCompletionResources(const QList<ResourceNode> &nodes, QVector<CodeCompletionItem> &items)
{
    for (const auto &node : nodes) {
        if (node.isGroup) { appendCompletionResources(node.children, items); continue; }
        CodeCompletionItem item; item.name = node.name;
        item.kind = node.type == ResourceType::Script ? CodeCompletionItem::Kind::Script
                  : node.type == ResourceType::Macro ? CodeCompletionItem::Kind::Constant : CodeCompletionItem::Kind::Resource;
        item.detail = node.type == ResourceType::Macro ? node.value : node.name;
        if (node.type == ResourceType::Script) item.detail = GmlSymbols::instance().scriptSignature(node.name, node.filePath);
        items.append(item);
    }
}

QVector<CodeCompletionItem> GmlSymbols::completionItems(const Project &project)
{
    QVector<CodeCompletionItem> items = instance().items;
    for (ResourceType type : {ResourceType::Sprite, ResourceType::Sound, ResourceType::Background,
                             ResourceType::Path, ResourceType::Script, ResourceType::Shader, ResourceType::Font,
                             ResourceType::Timeline, ResourceType::Object, ResourceType::Room, ResourceType::Macro})
        appendCompletionResources(project.resources(type), items);
    return items;
}
