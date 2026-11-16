#include "track.hpp"

#include <QUrl>

namespace
{
// the same fields under the helper's names and under qml's
struct Keys
{
    QString video_id;
    QString title;
    QString artist;
    QString album;
    QString duration_seconds;
    QString thumbnail_url;
};

const Keys helper_keys{"video_id", "title", "artist", "album", "duration_seconds", "thumbnail_url"};
const Keys qml_keys{"videoId", "title", "artist", "album", "durationSeconds", "thumbnailUrl"};

// a file's url can be spelled several ways and only one is its id
QString one_spelling(const QString& id)
{
    return id.startsWith(QLatin1String("file:")) ? Track::file_id(QUrl(id).toLocalFile()) : id;
}

Track read(const QVariantMap& map, const Keys& keys)
{
    Track track;
    track.video_id = one_spelling(map.value(keys.video_id).toString());
    track.title = map.value(keys.title).toString();
    track.artist = map.value(keys.artist).toString();
    track.album = map.value(keys.album).toString();
    track.duration_seconds = map.value(keys.duration_seconds).toInt();
    track.thumbnail_url = map.value(keys.thumbnail_url).toString();
    return track;
}
} // namespace

QString Track::file_id(const QString& path)
{
    return QStringLiteral("file://") + QString::fromLatin1(QUrl::toPercentEncoding(path, "/"));
}

Track Track::from_helper(const QVariantMap& map)
{
    return read(map, helper_keys);
}

Track Track::from_qml(const QVariantMap& map)
{
    return read(map, qml_keys);
}

QVariantMap Track::to_helper() const
{
    return {
        {helper_keys.video_id, video_id},
        {helper_keys.title, title},
        {helper_keys.artist, artist},
        {helper_keys.album, album},
        {helper_keys.duration_seconds, duration_seconds},
        {helper_keys.thumbnail_url, thumbnail_url},
    };
}

QVariantMap Track::to_qml() const
{
    QVariantMap map;
    const QHash<int, QByteArray> names = role_names();
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
    {
        map.insert(QString::fromLatin1(it.value()), role_value(it.key()));
    }
    return map;
}

QHash<int, QByteArray> Track::role_names()
{
    return {
        {VideoIdRole, qml_keys.video_id.toLatin1()},
        {TitleRole, qml_keys.title.toLatin1()},
        {ArtistRole, qml_keys.artist.toLatin1()},
        {AlbumRole, qml_keys.album.toLatin1()},
        {DurationSecondsRole, qml_keys.duration_seconds.toLatin1()},
        {ThumbnailUrlRole, qml_keys.thumbnail_url.toLatin1()},
    };
}

QVariant Track::role_value(int role) const
{
    switch (role)
    {
    case VideoIdRole: return video_id;
    case TitleRole: return title;
    case ArtistRole: return artist;
    case AlbumRole: return album;
    case DurationSecondsRole: return duration_seconds;
    case ThumbnailUrlRole: return thumbnail_url;
    default: return {};
    }
}

bool Track::is_valid() const
{
    return !video_id.isEmpty();
}
