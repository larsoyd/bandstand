#include "library.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cstdio>

// short enough to feel instant, long enough that adding 20 songs is one write
static constexpr int WRITE_DELAY_MS = 300;
// a failed write is retried this often until it goes through
static constexpr int RETRY_DELAY_MS = 2000;

static const QString LIKED = QStringLiteral("liked");
static const QString HISTORY = QStringLiteral("history");
static const QString HIDDEN = QStringLiteral("hidden");
static const QString ALBUMS = QStringLiteral("albums");
static const QString ARTISTS = QStringLiteral("artists");
static const QString PLAYLIST_PREFIX = QStringLiteral("playlist:");

template <typename Model>
static QJsonArray to_json(const Model& model)
{
    QJsonArray array;
    for (const auto& item : model.all())
    {
        array.append(QJsonObject::fromVariantMap(item.to_helper()));
    }
    return array;
}

// each song once, as two spellings of a file's id are read as one
static QVector<Track> songs_of(const QVariantList& helper_tracks)
{
    QVector<Track> songs;
    QSet<QString> seen;
    for (const QVariant& entry : helper_tracks)
    {
        const Track track = Track::from_helper(entry.toMap());
        if (!seen.contains(track.video_id))
        {
            seen.insert(track.video_id);
            songs.append(track);
        }
    }
    return songs;
}

static int index_of_song(const TrackModel& tracks, const QString& video_id)
{
    const QVector<Track>& all = tracks.all();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Track& track) { return track.video_id == video_id; });
    return it == all.end() ? -1 : int(it - all.begin());
}

// youtube has one song under several video ids (the song, its music video, an upload of it),
// so the same title by the same artist is the same song. empty when either is missing,
// such a song only matches by its id
static QString song_key(const Track& track)
{
    const QString title = track.title.simplified().toCaseFolded();
    QString artist = track.artist.simplified().toCaseFolded();
    // the auto-made channel of an artist is "NAME - Topic"
    if (artist.endsWith(QLatin1String(" - topic")))
    {
        artist.chop(8);
    }
    return title.isEmpty() || artist.isEmpty() ? QString() : title + QLatin1Char('\n') + artist;
}

static bool same_song(const Track& a, const Track& b)
{
    const QString key = song_key(a);
    return a.video_id == b.video_id || (!key.isEmpty() && key == song_key(b));
}

Library::Library(const QString& folder, QObject *parent)
    : QObject(parent)
    , folder(folder)
{
    write_timer.setSingleShot(true);
    connect(&write_timer, &QTimer::timeout, this, &Library::flush);

    // connect after loading, or loading would mark every file dirty
    load();
    watch_for_changes();
}

Library::~Library()
{
    flush();
}

QAbstractItemModel *Library::playlists()
{
    return &playlist_list;
}

TrackModel *Library::liked()
{
    return &liked_songs;
}

TrackModel *Library::history()
{
    return &history_songs;
}

CardModel *Library::albums()
{
    return &saved_albums;
}

CardModel *Library::artists()
{
    return &saved_artists;
}

int Library::liked_version() const
{
    return liked_counter;
}

int Library::saved_version() const
{
    return saved_counter;
}

int Library::hidden_version() const
{
    return hidden_counter;
}

QString Library::problem() const
{
    return problem_text;
}

QString Library::createPlaylist(const QString& name)
{
    const QString clean = name.trimmed().isEmpty() ? tr("Playlist") : name.trimmed();
    const QString id = new_playlist_id();
    playlist_list.add(id, clean);
    return id;
}

QString Library::createPlaylistFrom(const QString& name, TrackModel *songs)
{
    const QString id = createPlaylist(name);
    addAllToPlaylist(id, songs);
    return id;
}

void Library::renamePlaylist(const QString& id, const QString& name)
{
    const QString clean = name.trimmed();
    if (!clean.isEmpty())
    {
        playlist_list.rename(id, clean);
    }
}

void Library::deletePlaylist(const QString& id)
{
    playlist_list.remove(id);
}

TrackModel *Library::playlistTracks(const QString& id) const
{
    return playlist_list.tracks_of(id);
}

QString Library::playlistName(const QString& id) const
{
    return playlist_list.name_of(id);
}

bool Library::addToPlaylist(const QString& id, const QVariantMap& song)
{
    TrackModel *tracks = playlist_list.tracks_of(id);
    const Track track = Track::from_qml(song);
    if (tracks == nullptr || !track.is_valid() || index_of_song(*tracks, track.video_id) >= 0)
    {
        return false;
    }

    tracks->append(track);
    return true;
}

int Library::addAllToPlaylist(const QString& id, TrackModel *songs)
{
    if (songs == nullptr)
    {
        return 0;
    }

    int added = 0;
    // copy, songs may be the playlist we're appending to
    const QVector<Track> copy = songs->all();
    for (const Track& track : copy)
    {
        if (addToPlaylist(id, track.to_qml()))
        {
            ++added;
        }
    }
    return added;
}

bool Library::playlistHas(const QString& id, const QString& video_id) const
{
    const TrackModel *tracks = playlist_list.tracks_of(id);
    return tracks != nullptr && index_of_song(*tracks, video_id) >= 0;
}

bool Library::isLiked(const QString& video_id) const
{
    return liked_ids.contains(video_id);
}

void Library::setLiked(const QVariantMap& song, bool liked)
{
    const Track track = Track::from_qml(song);
    if (!track.is_valid())
    {
        return;
    }

    if (liked && !liked_ids.contains(track.video_id))
    {
        liked_songs.insert(0, track);
    }
    else if (!liked)
    {
        liked_songs.remove_at(index_of_song(liked_songs, track.video_id));
    }
}

void Library::recordPlayed(const QVariantMap& song)
{
    const Track track = Track::from_qml(song);
    if (!track.is_valid() || is_hidden(track))
    {
        return;
    }

    const int row = index_of_song(history_songs, track.video_id);
    // already on top, don't touch the file
    if (row == 0)
    {
        return;
    }

    history_songs.remove_at(row);
    history_songs.insert(0, track);
    while (history_songs.rowCount() > HISTORY_LIMIT)
    {
        history_songs.remove_at(history_songs.rowCount() - 1);
    }
}

void Library::removeFromHistory(const QString& video_id)
{
    history_songs.remove_at(index_of_song(history_songs, video_id));
}

void Library::clearHistory()
{
    history_songs.clear();
}

bool Library::isHidden(const QVariantMap& song) const
{
    return is_hidden(Track::from_qml(song));
}

bool Library::is_hidden(const Track& track) const
{
    const QString key = song_key(track);
    return hidden_ids.contains(track.video_id) || (!key.isEmpty() && hidden_titles.contains(key));
}

void Library::setHidden(const QVariantMap& song, bool hidden)
{
    const Track track = Track::from_qml(song);
    if (!track.is_valid())
    {
        return;
    }

    if (hidden)
    {
        if (!is_hidden(track))
        {
            hidden_songs.insert(0, track);
        }
        remove_from_history(track);
        return;
    }

    // whatever hides it, also the song under another video id
    for (int row = hidden_songs.rowCount() - 1; row >= 0; --row)
    {
        if (same_song(hidden_songs.all().at(row), track))
        {
            hidden_songs.remove_at(row);
        }
    }
}

QVariantList Library::withoutHidden(const QVariantList& items) const
{
    QVariantList shown;
    for (const QVariant& item : items)
    {
        // cards have no video id, and stay
        const Track track = Track::from_qml(item.toMap());
        if (!track.is_valid() || !is_hidden(track))
        {
            shown.append(item);
        }
    }
    return shown;
}

void Library::remove_from_history(const Track& track)
{
    for (int row = history_songs.rowCount() - 1; row >= 0; --row)
    {
        if (same_song(history_songs.all().at(row), track))
        {
            history_songs.remove_at(row);
        }
    }
}

bool Library::isSaved(const QString& browse_id) const
{
    return saved_albums.contains(browse_id) || saved_artists.contains(browse_id);
}

void Library::save(const QVariantMap& card)
{
    const Card saved = Card::from_qml(card);
    if (saved.kind == QLatin1String("album"))
    {
        saved_albums.add_first(saved);
    }
    else if (saved.kind == QLatin1String("artist"))
    {
        saved_artists.add_first(saved);
    }
}

void Library::unsave(const QString& browse_id)
{
    saved_albums.remove_id(browse_id);
    saved_artists.remove_id(browse_id);
}

void Library::load()
{
    load_playlists();

    auto list = [this](const QString& what, const char *key)
    {
        return read_object(path_of(what)).value(QLatin1String(key)).toArray().toVariantList();
    };
    liked_songs.set_all(songs_of(list(LIKED, "tracks")));
    history_songs.set_all(songs_of(list(HISTORY, "tracks")));
    hidden_songs.set_all(songs_of(list(HIDDEN, "tracks")));
    saved_albums.set_cards(list(ALBUMS, "albums"));
    saved_artists.set_cards(list(ARTISTS, "artists"));
    rebuild_liked_ids();
    rebuild_hidden_ids();

    // the two files are written one after the other, a crash in between can leave a hidden
    // song in the history; it's left out here and the next write of the history drops it
    QVector<Track> played = history_songs.all();
    played.erase(std::remove_if(played.begin(), played.end(), [this](const Track& track) { return is_hidden(track); }),
        played.end());
    if (played.size() != history_songs.rowCount())
    {
        history_songs.set_all(played);
    }
}

void Library::load_playlists()
{
    QDir directory(folder + QStringLiteral("/playlists"));
    QStringList files = directory.entryList({QStringLiteral("*.json")}, QDir::Files);

    // ids are creation timestamps in ms, so numeric order is creation order
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b)
    {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });

    for (const QString& file : files)
    {
        const QJsonObject object = read_object(directory.filePath(file));
        if (object.isEmpty())
        {
            continue;
        }

        const QString id = QFileInfo(file).completeBaseName();
        const QString name = object.value(QStringLiteral("name")).toString(id);
        TrackModel *tracks = playlist_list.add(id, name);
        tracks->set_all(songs_of(object.value(QStringLiteral("tracks")).toArray().toVariantList()));
    }
}

QJsonObject Library::read_object(const QString& path) const
{
    QFile file(path);
    if (!file.exists())
    {
        return {};
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "bandstand: cannot read %s (%s)\n", qPrintable(path),
            qPrintable(file.errorString()));
        return {};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        // keep the broken file around for the user, the next save writes a fresh one
        file.close();
        const QString aside = path + QStringLiteral(".corrupt");
        QFile::remove(aside);
        QFile::rename(path, aside);
        std::fprintf(stderr, "bandstand: %s is not valid JSON (%s); it was moved to %s\n",
            qPrintable(path), qPrintable(error.errorString()), qPrintable(aside));
        return {};
    }
    return document.object();
}

void Library::watch_for_changes()
{
    connect(&playlist_list, &PlaylistList::playlistChanged, this,
        [this](const QString& id) { mark_dirty(PLAYLIST_PREFIX + id); });
    connect(&playlist_list, &PlaylistList::playlistRemoved, this, &Library::delete_playlist_file);

    connect(&liked_songs, &TrackModel::changed, this, [this]
    {
        rebuild_liked_ids();
        ++liked_counter;
        emit likedChanged();
        mark_dirty(LIKED);
    });
    connect(&history_songs, &TrackModel::changed, this, [this] { mark_dirty(HISTORY); });
    connect(&hidden_songs, &TrackModel::changed, this, [this]
    {
        rebuild_hidden_ids();
        ++hidden_counter;
        emit hiddenChanged();
        mark_dirty(HIDDEN);
    });

    connect(&saved_albums, &CardModel::changed, this, [this]
    {
        ++saved_counter;
        emit savedChanged();
        mark_dirty(ALBUMS);
    });
    connect(&saved_artists, &CardModel::changed, this, [this]
    {
        ++saved_counter;
        emit savedChanged();
        mark_dirty(ARTISTS);
    });
}

void Library::rebuild_liked_ids()
{
    liked_ids.clear();
    for (const Track& track : liked_songs.all())
    {
        liked_ids.insert(track.video_id);
    }
}

void Library::rebuild_hidden_ids()
{
    hidden_ids.clear();
    hidden_titles.clear();
    for (const Track& track : hidden_songs.all())
    {
        hidden_ids.insert(track.video_id);
        const QString key = song_key(track);
        if (!key.isEmpty())
        {
            hidden_titles.insert(key);
        }
    }
}

void Library::mark_dirty(const QString& what)
{
    dirty.insert(what);
    write_timer.start(WRITE_DELAY_MS);
}

void Library::flush()
{
    write_timer.stop();

    const QSet<QString> to_write = dirty;
    for (const QString& what : to_write)
    {
        if (write_file(what))
        {
            dirty.remove(what);
        }
    }

    // whatever is still dirty failed; keep its problem showing until it saves
    if (dirty.isEmpty())
    {
        set_problem({});
    }
    else
    {
        write_timer.start(RETRY_DELAY_MS);
    }
}

QString Library::path_of(const QString& what) const
{
    if (what.startsWith(PLAYLIST_PREFIX))
    {
        return folder + QStringLiteral("/playlists/") + what.mid(PLAYLIST_PREFIX.size()) + QStringLiteral(".json");
    }
    return folder + QLatin1Char('/') + what + QStringLiteral(".json");
}

// false only when the file couldn't be written
bool Library::write_file(const QString& what)
{
    QJsonObject object;
    if (what == LIKED)
    {
        object.insert(QStringLiteral("tracks"), to_json(liked_songs));
    }
    else if (what == HISTORY)
    {
        object.insert(QStringLiteral("tracks"), to_json(history_songs));
    }
    else if (what == HIDDEN)
    {
        object.insert(QStringLiteral("tracks"), to_json(hidden_songs));
    }
    else if (what == ALBUMS)
    {
        object.insert(QStringLiteral("albums"), to_json(saved_albums));
    }
    else if (what == ARTISTS)
    {
        object.insert(QStringLiteral("artists"), to_json(saved_artists));
    }
    else if (what.startsWith(PLAYLIST_PREFIX))
    {
        const QString id = what.mid(PLAYLIST_PREFIX.size());
        const TrackModel *tracks = playlist_list.tracks_of(id);
        if (tracks == nullptr)
        {
            // deleted after it was marked dirty
            return true;
        }
        object.insert(QStringLiteral("name"), playlist_list.name_of(id));
        object.insert(QStringLiteral("tracks"), to_json(*tracks));
    }
    else
    {
        return true;
    }

    const QString path = path_of(what);
    QDir().mkpath(QFileInfo(path).absolutePath());

    // QSaveFile writes to a temp file and renames, so a crash never leaves half a file
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0
        || !file.commit())
    {
        set_problem(tr("Could not save %1 (%2). Check that the folder exists, has room, and that you may "
                       "write in it.").arg(path, file.errorString()));
        return false;
    }
    return true;
}

void Library::delete_playlist_file(const QString& id)
{
    dirty.remove(PLAYLIST_PREFIX + id);
    QFile::remove(path_of(PLAYLIST_PREFIX + id));
}

QString Library::new_playlist_id() const
{
    // also check the disk: a playlist file that failed to load still owns its id
    qint64 candidate = QDateTime::currentMSecsSinceEpoch();
    while (playlist_list.contains(QString::number(candidate))
           || QFile::exists(path_of(PLAYLIST_PREFIX + QString::number(candidate))))
    {
        ++candidate;
    }
    return QString::number(candidate);
}

void Library::set_problem(const QString& text)
{
    if (text != problem_text)
    {
        problem_text = text;
        emit problemChanged();
    }
}
