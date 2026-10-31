#include "local-songs.hpp"

#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

static QVariantMap song_of(const QFileInfo& file)
{
    return {
        {QStringLiteral("videoId"), QUrl::fromLocalFile(file.filePath()).toString()},
        {QStringLiteral("title"), file.completeBaseName()},
        {QStringLiteral("artist"), QString()},
        {QStringLiteral("album"), QString()},
        {QStringLiteral("durationSeconds"), 0},
        {QStringLiteral("thumbnailUrl"), QString()},
    };
}

QVariantList local_songs(const QStringList& names, const QString& folder)
{
    QVariantList songs;
    for (const QString& name : names)
    {
        const QFileInfo file(QUrl::fromUserInput(name, folder, QUrl::AssumeLocalFile).toLocalFile());
        if (file.isFile())
        {
            songs.append(song_of(file));
        }
    }
    return songs;
}

QString song_url(const QString& video_id)
{
    return video_id.startsWith(QLatin1String("file:")) ? video_id
                                                       : QStringLiteral("https://music.youtube.com/watch?v=") + video_id;
}
