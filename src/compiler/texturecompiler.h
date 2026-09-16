#ifndef QTGMS_TEXTURECOMPILER_H
#define QTGMS_TEXTURECOMPILER_H
#include "datawriter.h"
#include <QImage>
#include <QRect>
#include <QMap>
class CompileProfile;

struct TextureSettings
{
    bool crop = true;
    bool separate = false;
    bool tileHorizontal = false;
    bool tileVertical = false;
    bool emptyBorder = false;
};

struct CompiledTextureEntry
{
    QImage image;
    QSize original;
    QRect crop;
    QPoint position;
    int page = -1;
    int group = 0;
    int border = 2;
    TextureSettings settings;
    QVector<int> patches;
};
struct CompiledTexturePage
{
    QSize size;
    QVector<int> entries;
    int flags = 0;
};
class TextureCompiler
{
public:
    void configure(const QMap<QString, QString> &options, const QString &cacheDirectory)
    {
        m_options = options;
        m_cacheDirectory = cacheDirectory;
    }
    int add(const QImage &image, int group = 0, const TextureSettings &settings = TextureSettings());
    void reference(DataWriter &file, int entry);
    void write(DataWriter &file, int pageSize);
    void writePages(DataWriter &file, CompileProfile &profile);

private:
    QVector<CompiledTextureEntry> m_entries;
    QVector<CompiledTexturePage> m_pages;
    QHash<QByteArray, int> m_duplicates;
    QMap<QString, QString> m_options;
    QString m_cacheDirectory;
    void pack(int pageSize);
    QImage renderPage(int pageIndex);
};
#endif
