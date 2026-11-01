#include "local-songs.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <taglib/fileref.h>
#include <taglib/tvariant.h>

static QString text_of(const TagLib::String& text)
{
    return QString::fromStdString(text.to8Bit(true)).trimmed();
}

// named by content, so every song of an album shares one file
static QString saved_cover(const TagLib::ByteVector& data, const QString& covers)
{
    const QByteArray bytes(data.data(), data.size());
    const QString path = QDir(covers).filePath(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (QFile::exists(path))
    {
        return path;
    }
    QFile file(path);
    if (!QDir().mkpath(covers) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
    {
        qWarning("[bandstand] cover not saved path=%s reason=%s", qPrintable(path), qPrintable(file.errorString()));
        return {};
    }
    return path;
}

static QString embedded_cover(const TagLib::FileRef& tags, const QString& covers)
{
    const auto pictures = tags.complexProperties("PICTURE");
    return pictures.isEmpty() ? QString() : saved_cover(pictures.front().value("data").toByteVector(), covers);
}

static QString folder_cover(const QFileInfo& file)
{
    QDir folder = file.dir();
    folder.setNameFilters({"cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.png", "front.jpg", "front.png"});
    const QStringList found = folder.entryList(QDir::Files);
    return found.isEmpty() ? QString() : folder.filePath(found.first());
}

static QVariantMap song_of(const QFileInfo& file, const QString& covers)
{
    QVariantMap song{
        {QStringLiteral("videoId"), QUrl::fromLocalFile(file.filePath()).toString()},
        {QStringLiteral("title"), file.completeBaseName()},
        {QStringLiteral("artist"), QString()},
        {QStringLiteral("album"), QString()},
        {QStringLiteral("durationSeconds"), 0},
        {QStringLiteral("thumbnailUrl"), QString()},
    };

    const TagLib::FileRef tags(QFile::encodeName(file.filePath()).constData());
    QString cover;
    if (!tags.isNull())
    {
        const QString title = text_of(tags.tag()->title());
        song[QStringLiteral("title")] = title.isEmpty() ? file.completeBaseName() : title;
        song[QStringLiteral("artist")] = text_of(tags.tag()->artist());
        song[QStringLiteral("album")] = text_of(tags.tag()->album());
        if (tags.audioProperties() != nullptr)
        {
            song[QStringLiteral("durationSeconds")] = tags.audioProperties()->lengthInSeconds();
        }
        cover = embedded_cover(tags, covers);
    }
    cover = cover.isEmpty() ? folder_cover(file) : cover;
    if (!cover.isEmpty())
    {
        song[QStringLiteral("thumbnailUrl")] = QUrl::fromLocalFile(cover).toString();
    }
    return song;
}

QString default_covers_folder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/bandstand/covers");
}

QVariantList local_songs(const QStringList& names, const QString& folder, const QString& covers)
{
    QVariantList songs;
    for (const QString& name : names)
    {
        const QFileInfo file(QUrl::fromUserInput(name, folder, QUrl::AssumeLocalFile).toLocalFile());
        if (file.isFile())
        {
            songs.append(song_of(file, covers));
        }
    }
    return songs;
}

QString song_url(const QString& video_id)
{
    return video_id.startsWith(QLatin1String("file:")) ? video_id
                                                       : QStringLiteral("https://music.youtube.com/watch?v=") + video_id;
}
