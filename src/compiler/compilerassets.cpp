#include "compilerbuild.h"
#include "builddirectory.h"
#include "audiocompiler.h"
#include "spritedocument.h"
#include "backgrounddocument.h"
#include "sounddocument.h"
#include "fontdocument.h"
#include "fontatlas.h"
#include "compilercache.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

void CompilerBuild::assets(CompileProfile &profile)
{
    profile.start(QStringLiteral("Audio loading / conversion / cache"));
    const bool newAudio = option("use_new_audio", 1) != 0;
    file.chunk("SOND", [&] {
        const auto &sounds = resources[ResourceType::Sound];
        file.list(sounds.size(), [&](int i) {
            const auto &resource = sounds.at(i);
            SoundDocument document;
            QString error;
            SoundLoadTimings timings;
            const bool loaded = document.loadFromXml(resource.node.filePath, resource.document,
                resource.sourceBytes, request.configuration, error, &timings);
            profile.addDetail(QStringLiteral("Sound XML / configuration"), timings.metadataNanoseconds);
            profile.addDetail(QStringLiteral("Source audio file reading"), timings.readNanoseconds);
            profile.addDetail(QStringLiteral("Decoder format probe"), timings.decoderProbeNanoseconds);
            profile.count(QStringLiteral("Sound resources"));
            if (!loaded)
                throw CompileError(resource.node.name + ": " + error);
            const auto &state = document.state();
            const bool streamed = state.attributes == 3;
            // Legacy DirectSound loads PCM WAV from AUDO and streams MP3 from
            // disk. Its SOND preload field occupies the new engine's group slot.
            const AudioEncoding encoding = !newAudio
                ? (streamed ? AudioEncoding::Mp3 : AudioEncoding::Wave)
                : (state.attributes != 0 ? AudioEncoding::Vorbis : AudioEncoding::Wave);
            const bool compressed = encoding != AudioEncoding::Wave;
            const int group = !newAudio || streamed ? 0 : state.audioGroup;
            if (group < 0 || group >= request.project.audioGroups().size())
                throw CompileError(resource.node.name + ": invalid audio group");
            const QString extension = encoding == AudioEncoding::Mp3 ? QStringLiteral(".mp3")
                : encoding == AudioEncoding::Vorbis ? QStringLiteral(".ogg") : QStringLiteral(".wav"),
                          name = resource.node.name + extension;
            const QByteArray cacheKey = QByteArray(encoding == AudioEncoding::Mp3 ? "audio-mp3-lame-3.100-v1:" : "audio-v3:")
                + QByteArray::number(int(encoding)) + ":"
                + QByteArray::number(state.channelType) + ":" + QByteArray::number(state.sampleRate) + ":"
                + QByteArray::number(state.bitRate) + ":" + QByteArray::number(state.bitDepth);
            const auto encode = [&] {
                try {
                    return compileAudio(state.audio, encoding, state.channelType == 1 ? 2 : 1,
                        state.sampleRate, state.bitRate, state.bitDepth);
                } catch (const CompileError &failure) {
                    throw CompileError(resource.node.name + ": " + failure.message);
                }
            };
            const QByteArray bytes = state.audio.isEmpty()
                ? (newAudio ? QByteArray() : encode())
                : cachedCompilerAsset(
                      QDir(BuildDirectory::path(request.project)).filePath(QStringLiteral(".cache")), cacheKey,
                      state.audio,
                      encode,
                      profile);
            if (state.audio.isEmpty())
                profile.count(QStringLiteral("Empty sounds (no cache lookup)"));
            int id = -1;
            if (!bytes.isEmpty()) {
                if (streamed)
                    external(name, bytes);
                else
                    id = addAudio(group, bytes);
            }
            file.string(resource.node.name);
            file.u32(newAudio ? (streamed ? 100 : state.attributes == 2 ? 103 : compressed ? 102 : 101)
                              : (streamed ? 1 : 0));
            file.string(extension);
            file.string(name);
            file.u32(number(resource.xml, "effects"));
            file.f32(state.volume);
            file.f32(number(resource.xml, "pan"));
            file.u32(newAudio ? group : number(resource.xml, "preload") != 0);
            file.u32(id);
        });
    });
    file.chunk("AGRP", [&] {
        const auto &groups = request.project.audioGroups();
        file.list(groups.size(), [&](int i) { file.string(groups.at(i)); });
    });
    profile.start(QStringLiteral("Sprites / collision masks"));
    file.chunk("SPRT", [&] {
        const auto &sprites = resources[ResourceType::Sprite];
        file.list(sprites.size(), [&](int i) {
            file.align(4);
            const auto &resource = sprites.at(i);
            if (number(resource.xml, "type") != 0)
                throw CompileError(resource.node.name + ": SWF / skeletal sprites are not supported yet");
            SpriteDocument document;
            QString error;
            {
                CompileDetailScope timing(profile, QStringLiteral("Sprite XML / image file loading and decoding"));
                if (!document.loadFromXml(resource.node.filePath, resource.document, resource.sourceBytes,
                        request.configuration, error))
                    throw CompileError(resource.node.name + ": " + error);
            }
            profile.count(QStringLiteral("Sprite resources"));
            const auto &state = document.state();
            QRect bounds;
            {
                CompileDetailScope timing(profile, QStringLiteral("Bounding box calculation"));
                bounds = document.boundingBox();
            }
            file.string(resource.node.name);
            file.u32(state.size.width());
            file.u32(state.size.height());
            for (int value : { bounds.left(), bounds.right(), bounds.bottom(), bounds.top() })
                file.u32(value);
            file.u32(0);
            file.u32(0);
            file.u32(1);
            file.u32(state.boundingBoxMode);
            file.u32(state.collisionKind == 1 ? 0 : state.collisionKind == 5 ? 2 : 1);
            file.u32(state.origin.x());
            file.u32(state.origin.y());
            TextureSettings texture;
            texture.separate = state.for3D;
            texture.tileHorizontal = state.tileHorizontal;
            texture.tileVertical = state.tileVertical;
            file.u32(state.frames.size());
            {
                CompileDetailScope timing(profile, QStringLiteral("Texture cropping / deduplication / registration"));
                for (const auto &frame : state.frames) {
                    textures.reference(file, textures.add(frame.image, state.textureGroup, texture));
                    profile.count(QStringLiteral("Sprite frames"));
                }
            }
            const int count = state.frames.isEmpty() ? 0 : state.separateMasks ? state.frames.size() : 1;
            file.u32(count);
            for (int frame = 0; frame < count; ++frame) {
                QImage mask;
                {
                    CompileDetailScope timing(profile, QStringLiteral("Collision mask generation (including bounds)"));
                    mask = document.collisionMask(frame);
                }
                profile.count(QStringLiteral("Collision masks"));
                CompileDetailScope timing(profile, QStringLiteral("Collision mask bit packing / serialization"));
                const int stride = (mask.width() + 7) / 8;
                QByteArray bits(stride * mask.height(), 0);
                for (int y = 0; y < mask.height(); ++y) {
                    const auto *row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
                    for (int x = 0; x < mask.width(); ++x)
                        if (qAlpha(row[x]))
                            bits[y * stride + x / 8] = char(quint8(bits.at(y * stride + x / 8)) | (0x80 >> (x % 8)));
                }
                file.bytes.append(bits);
            }
            file.align(4);
        });
    });
    profile.start(QStringLiteral("Backgrounds"));
    file.chunk("BGND", [&] {
        const auto &backgrounds = resources[ResourceType::Background];
        file.list(backgrounds.size(), [&](int i) {
            const auto &resource = backgrounds.at(i);
            file.string(resource.node.name);
            file.u32(0);
            file.u32(0);
            file.u32(1);
            BackgroundDocument document;
            QString error;
            if (!document.loadFromXml(resource.node.filePath, resource.document, resource.sourceBytes,
                    request.configuration, error))
                throw CompileError(resource.node.name + ": " + error);
            const auto &state = document.state();
            if (state.image.isNull()) {
                file.u32(0);
                profile.count(QStringLiteral("Backgrounds without images"));
                return;
            }
            TextureSettings texture;
            texture.crop = !state.isTileSet;
            texture.separate = state.for3D;
            texture.tileHorizontal = state.tileHorizontal;
            texture.tileVertical = state.tileVertical;
            textures.reference(file, textures.add(state.image, state.textureGroup, texture));
        });
    });
    profile.start(QStringLiteral("Paths / script loading"));
    file.chunk("PATH", [&] {
        const auto &paths = resources[ResourceType::Path];
        file.list(paths.size(), [&](int i) {
            const auto &resource = paths.at(i);
            file.string(resource.node.name);
            file.u32(number(resource.xml, "kind"));
            file.u32(number(resource.xml, "closed") != 0);
            file.u32(number(resource.xml, "precision", 4));
            const auto points = children(resource.xml.firstChildElement("points"), "point");
            file.u32(points.size());
            for (const auto &point : points) {
                const auto values = point.text().split(',');
                if (values.size() != 3)
                    throw CompileError(resource.node.name + ": invalid path point");
                for (const auto &value : values)
                    file.f32(value.toFloat());
            }
        });
    });
    file.chunk("SCPT", [&] {
        const auto &scripts = resources[ResourceType::Script];
        file.list(scripts.size(), [&](int i) {
            const auto &resource = scripts.at(i);
            file.string(resource.node.name);
            QString source = resource.embeddedSource.isEmpty() ? QString::fromUtf8(read(resource.node.filePath))
                                                               : resource.embeddedSource;
            if (source.trimmed().isEmpty())
                source = "exit;";
            file.u32(code("gml_Script_" + resource.node.name, source));
        });
    });
    file.chunk("GLOB", [&] { file.u32(0); });
    profile.start(QStringLiteral("Shader compilation"));
    shaders();
    profile.start(QStringLiteral("Fonts / glyph atlases"));
    file.chunk("FONT", [&] {
        const auto &fonts = resources[ResourceType::Font];
        file.list(fonts.size(), [&](int i) {
            const auto &resource = fonts.at(i);
            const auto &root = resource.xml;
            FontDocument document;
            QString error;
            if (!document.loadFromXml(resource.node.filePath, resource.document, resource.sourceBytes,
                    request.configuration, error))
                throw CompileError(resource.node.name + ": " + error);
            const auto &state = document.state();
            FontAtlasData atlas;
            const auto glyphs = children(root.firstChildElement("glyphs"), "glyph");
            const QString png = assetPath(resource, resource.node.name + ".png");
            if (!glyphs.isEmpty() && QFileInfo::exists(png)) {
                atlas.image = QImage(png);
                if (atlas.image.isNull())
                    throw CompileError(resource.node.name + ": cannot read font atlas");
                for (const auto &glyph : glyphs) {
                    FontGlyph value;
                    value.character = attribute(glyph, "character");
                    value.rectangle = QRect(
                        attribute(glyph, "x"), attribute(glyph, "y"), attribute(glyph, "w"), attribute(glyph, "h"));
                    value.shift = attribute(glyph, "shift");
                    value.offset = attribute(glyph, "offset");
                    atlas.glyphs.append(value);
                }
            } else if (!FontAtlas::build(state, atlas, error))
                throw CompileError(resource.node.name + ": " + error);
            int first = 65535, last = 0;
            for (const auto &glyph : atlas.glyphs) {
                first = qMin(first, glyph.character);
                last = qMax(last, glyph.character);
            }
            TextureSettings texture;
            texture.crop = false;
            texture.emptyBorder = true;
            file.string(resource.node.name);
            file.string(state.family);
            file.u32(state.size);
            file.u32(state.bold);
            file.u32(state.italic);
            file.u32(first | (int(number(root, "charset", 1)) << 16) | (state.antiAlias << 24));
            file.u32(last);
            textures.reference(file, textures.add(atlas.image, state.textureGroup, texture));
            file.f32(1);
            file.f32(1);
            QMap<int, QVector<QPair<int, int>>> kerning;
            for (const auto &pair : children(root.firstChildElement("kerningPairs"), "pair"))
                kerning[int(attribute(pair, "first"))].append(
                    qMakePair(int(attribute(pair, "second")), int(attribute(pair, "amount"))));
            file.list(atlas.glyphs.size(), [&](int g) {
                const auto &glyph = atlas.glyphs.at(g);
                for (int value : { glyph.character, glyph.rectangle.x(), glyph.rectangle.y(), glyph.rectangle.width(),
                         glyph.rectangle.height(), glyph.shift, glyph.offset, kerning.value(glyph.character).size() })
                    file.u16(value);
                for (const auto &pair : kerning.value(glyph.character)) {
                    file.u16(pair.first);
                    file.u16(pair.second);
                }
            });
        });
        for (int i = 0; i < 256; ++i)
            file.u16(i < 128 ? i : 63);
    });
}
