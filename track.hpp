#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>

// the helper's shape is in helper/youtube_music.py, the library files use it too
struct Track
{
    enum Role
    {
        VideoIdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        DurationSecondsRole,
        ThumbnailUrlRole,
    };

    QString video_id;
    QString title;
    QString artist;
    QString album;
    int duration_seconds = 0;
    QString thumbnail_url;

    // the id of a song in a file, its url as the helper writes it
    static QString file_id(const QString& path);

    static Track from_helper(const QVariantMap& map);
    static Track from_qml(const QVariantMap& map);
    QVariantMap to_helper() const;
    // keys are the role names, so this matches what a delegate sees
    QVariantMap to_qml() const;

    static QHash<int, QByteArray> role_names();
    QVariant role_value(int role) const;

    bool is_valid() const;
};
