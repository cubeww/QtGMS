#ifndef QTGMS_SCRIPTSEARCH_H
#define QTGMS_SCRIPTSEARCH_H

#include "project.h"
#include <QHash>
#include <QVector>

enum class ScriptSearchScope { Shader, Script, Object, Room, Instance, Timeline, Macro };
enum class ScriptSearchFilter {
    Variable, Constant, Function, Script, Sound, Sprite, Shader, Timeline, Room,
    Object, Path, Font, Extension
};

struct ScriptSearchOptions {
    QString text;
    bool caseSensitive = false;
    bool ignoreComments = false;
    bool wholeWord = false;
    quint32 scopes = (1u << 7) - 1;
    quint32 filters = (1u << 13) - 1;
};

// A snapshot of one editable code field, with enough identity to reopen it.
struct ScriptSearchSource {
    ResourceType type = ResourceType::Script;
    ScriptSearchScope scope = ScriptSearchScope::Script;
    QString path, name, text, description, identity;
    int stage = 0;
    int eventIndex = -1;
    int step = -1;
    int actionIndex = -1;
    int argumentIndex = -1;
    QString instanceId, macroName;
    int macroColumn = 1;
    bool code = true;
    bool literalString = false;
};

struct ScriptSearchMatch {
    ScriptSearchSource source;
    int offset = 0;
    int length = 0;
    int line = 1;
    int column = 1;
    QString found, description, context;
};

class ScriptSearch
{
public:
    explicit ScriptSearch(const Project &project);
    void addConstant(const QString &name);
    void search(const ScriptSearchSource &source, const ScriptSearchOptions &options,
                QVector<ScriptSearchMatch> &matches) const;
private:
    QHash<QString, ScriptSearchFilter> m_symbols;
};

#endif
