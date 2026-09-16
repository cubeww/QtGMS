#include "texturecompiler.h"
#include "compilertarget.h"
#include "compilercache.h"
#include "compileprofile.h"
#include "fasthash.h"
#include <QBuffer>
#include <QPainter>
#include <QSet>
#include <algorithm>

int TextureCompiler::add(const QImage &source, int group, const TextureSettings &settings)
{
    if (source.isNull())
        return -1;
    const int groupCount = m_options.value("option_textureGroup_count", "1").toInt();
    if (group < 0 || group >= groupCount)
        throw CompileError(QStringLiteral("Invalid texture group: %1").arg(group));
    QString mask = m_options.value(QStringLiteral("option_textureGroup%1_targets").arg(group), "$7fffffffffffffff");
    const quint64 targets = mask.startsWith('$') ? mask.mid(1).toULongLong(nullptr, 16) : mask.toULongLong();
    if (!(targets & WindowsVmTargetMask))
        return -1;
    QSet<int> parents;
    for (;;) {
        if (parents.contains(group))
            throw CompileError(QStringLiteral("Texture group parent cycle"));
        parents.insert(group);
        const QString parent = m_options.value(QStringLiteral("option_textureGroup%1_parent").arg(group));
        if (parent.isEmpty() || parent == "<none>")
            break;
        int parentIndex = -1;
        for (int i = 0; i < groupCount; ++i)
            if (m_options.value(QStringLiteral("option_textureGroups%1").arg(i)) == parent) {
                parentIndex = i;
                break;
            }
        if (parentIndex < 0)
            throw CompileError(QStringLiteral("Unknown texture group parent: ") + parent);
        group = parentIndex;
    }
    const QString prefix = QStringLiteral("option_textureGroup%1_").arg(group);
    const bool crop = settings.crop && !settings.separate && m_options.value(prefix + "nocropping") != "true";
    const int border = qBound(0, m_options.value(prefix + "border", "2").toInt(), 32);
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        throw CompileError(
            QStringLiteral("Cannot allocate texture pixels (%1 x %2).").arg(source.width()).arg(source.height()));
    const QByteArray pixels
        = QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()), image.byteCount());
    const QByteArray context = QByteArray::number(image.width()) + ":" + QByteArray::number(group)
        + (crop ? ":crop" : ":full") + ":" + QByteArray::number(settings.separate)
        + QByteArray::number(settings.tileHorizontal) + QByteArray::number(settings.tileVertical)
        + QByteArray::number(settings.emptyBorder);
    const QByteArray key = fastHash(pixels, context);
    const auto duplicate = m_duplicates.constFind(key);
    if (duplicate != m_duplicates.cend())
        return duplicate.value();
    CompiledTextureEntry entry;
    entry.original = image.size();
    entry.group = group;
    entry.crop = image.rect();
    entry.border = border;
    entry.settings = settings;
    if (crop) {
        int left = image.width(), right = -1, top = image.height(), bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(row[x])) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
        }
        entry.crop = right < left ? QRect(0, 0, 1, 1) : QRect(QPoint(left, top), QPoint(right, bottom));
    }
    entry.image = image.copy(entry.crop);
    if (entry.image.isNull())
        throw CompileError(QStringLiteral("Cannot allocate a cropped texture (%1 x %2).")
                .arg(entry.crop.width())
                .arg(entry.crop.height()));
    const int index = m_entries.size();
    m_entries.append(entry);
    m_duplicates.insert(key, index);
    return index;
}
void TextureCompiler::reference(DataWriter &file, int entry)
{
    if (entry < 0)
        file.u32(0);
    else
        m_entries[entry].patches.append(file.reserve());
}
void TextureCompiler::pack(int pageSize)
{
    QVector<int> order;
    for (int i = 0; i < m_entries.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        const auto &left = m_entries.at(a), &right = m_entries.at(b);
        if (left.settings.separate != right.settings.separate)
            return left.settings.separate;
        return left.group != right.group ? left.group < right.group : left.image.height() > right.image.height();
    });
    int currentGroup = -1, page = -1, x = 0, y = 0, rowHeight = 0;
    for (int index : order) {
        auto &entry = m_entries[index];
        const int border = entry.border, width = entry.image.width() + 2 * border,
                  height = entry.image.height() + 2 * border;
        int size = pageSize;
        while (size < qMax(width, height) && size < 8192)
            size *= 2;
        if (size < qMax(width, height))
            throw CompileError(QStringLiteral("Texture exceeds the 8192-pixel page limit."));
        bool fresh = entry.settings.separate || page < 0 || currentGroup != entry.group
            || size != m_pages.at(page).size.width();
        if (!fresh && x + width > size) {
            x = 0;
            y += rowHeight;
            rowHeight = 0;
        }
        if (!fresh && y + height > size)
            fresh = true;
        if (fresh) {
            CompiledTexturePage atlas;
            atlas.size = QSize(size, size);
            atlas.flags
                = m_options.value(QStringLiteral("option_textureGroup%1_scaled").arg(entry.group)) == "true" ? 1 : 0;
            m_pages.append(atlas);
            page = m_pages.size() - 1;
            currentGroup = entry.group;
            x = 0;
            y = 0;
            rowHeight = 0;
        }
        entry.page = page;
        entry.position = QPoint(x + border, y + border);
        m_pages[page].entries.append(index);
        x += width;
        rowHeight = qMax(rowHeight, height);
        if (entry.settings.separate) {
            page = -1;
            currentGroup = -1;
        }
    }
    // Determine final dimensions before allocating any page pixels.
    QVector<QSize> used(m_pages.size(), QSize(1, 1));
    for (const auto &entry : m_entries)
        used[entry.page] = used.at(entry.page)
                               .expandedTo(QSize(entry.position.x() + entry.image.width() + entry.border,
                                   entry.position.y() + entry.image.height() + entry.border));
    for (int i = 0; i < m_pages.size(); ++i) {
        int width = 1, height = 1;
        while (width < used.at(i).width())
            width *= 2;
        while (height < used.at(i).height())
            height *= 2;
        m_pages[i].size = QSize(width, height);
    }
}

QImage TextureCompiler::renderPage(int pageIndex)
{
    const auto &page = m_pages.at(pageIndex);
    QImage image(page.size, QImage::Format_ARGB32);
    if (image.isNull())
        throw CompileError(QStringLiteral("Cannot allocate texture page %1 (%2 x %3, %4 MiB). "
                                          "The 32-bit process has insufficient available memory.")
                .arg(pageIndex + 1)
                .arg(page.size.width())
                .arg(page.size.height())
                .arg(qint64(page.size.width()) * page.size.height() * 4 / (1024.0 * 1024.0), 0, 'f', 1));
    image.fill(Qt::transparent);
    for (int index : page.entries) {
        auto &entry = m_entries[index];
        const int border = entry.border;
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(entry.position, entry.image);
        // Tiled axes wrap; other axes repeat the nearest edge. Draw only the
        // border so the hot path remains a single image copy per frame.
        const int w = entry.image.width(), h = entry.image.height();
        painter.end();
        const auto sourceX = [&](int coordinate) {
            return entry.settings.tileHorizontal ? (coordinate % w + w) % w : qBound(0, coordinate, w - 1);
        };
        const auto sourceY = [&](int coordinate) {
            return entry.settings.tileVertical ? (coordinate % h + h) % h : qBound(0, coordinate, h - 1);
        };
        for (int row = -border; !entry.settings.emptyBorder && row < h + border; ++row) {
            auto *destination = reinterpret_cast<QRgb *>(image.scanLine(entry.position.y() + row)) + entry.position.x();
            const auto *source = reinterpret_cast<const QRgb *>(entry.image.constScanLine(sourceY(row)));
            if (row < 0 || row >= h) {
                for (int column = -border; column < w + border; ++column)
                    destination[column] = source[sourceX(column)];
            } else
                for (int column = 1; column <= border; ++column) {
                    destination[-column] = source[sourceX(-column)];
                    destination[w + column - 1] = source[sourceX(w + column - 1)];
                }
        }
        // TPAG has already been written; this source image is no longer needed.
        entry.image = QImage();
    }
    return image;
}
void TextureCompiler::write(DataWriter &file, int pageSize)
{
    pack(pageSize);
    file.chunk("TPAG", [&] {
        file.list(m_entries.size(), [&](int i) {
            const auto &entry = m_entries.at(i);
            for (int patch : entry.patches)
                file.patch(patch, file.position());
            for (int value : { entry.position.x(), entry.position.y(), entry.image.width(), entry.image.height(),
                     entry.crop.x(), entry.crop.y(), entry.image.width(), entry.image.height(), entry.original.width(),
                     entry.original.height(), entry.page })
                file.u16(value);
        });
    });
}

void TextureCompiler::writePages(DataWriter &file, CompileProfile &profile)
{
    file.chunk("TXTR", [&] {
        QVector<int> pointers;
        file.list(m_pages.size(), [&](int i) {
            file.u32(m_pages.at(i).flags);
            pointers.append(file.reserve());
        });
        for (int i = 0; i < m_pages.size(); ++i) {
            profile.count(QStringLiteral("Texture pages"));
            QImage page;
            {
                CompileDetailScope timing(profile, QStringLiteral("Texture page rendering"));
                page = renderPage(i);
            }
            const QByteArray pixels
                = QByteArray::fromRawData(reinterpret_cast<const char *>(page.constBits()), page.byteCount());
            QByteArray png = cachedCompilerAsset(
                m_cacheDirectory, QByteArray("png-fast:") + QByteArray::number(page.width()), pixels,
                [&] {
                    QByteArray bytes;
                    QBuffer buffer(&bytes);
                    buffer.open(QIODevice::WriteOnly);
                    if (!page.save(&buffer, "PNG", 85))
                        throw CompileError(QStringLiteral("Cannot encode texture page."));
                    return bytes;
                },
                profile);
            CompileDetailScope timing(profile, QStringLiteral("PNG data serialization"));
            file.align(128);
            file.patch(pointers.at(i), file.position());
            file.bytes.append(png);
        }
    });
}
