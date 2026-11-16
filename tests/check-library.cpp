#include "check-library.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "../card-model.hpp"
#include "../library.hpp"
#include "../playlist-list.hpp"
#include "../track-model.hpp"

// a song in the shape qml passes it in
static QVariantMap song(const QString& id)
{
    Track track;
    track.video_id = id;
    track.title = "Song " + id;
    track.artist = "Artist " + id;
    track.album = "Album " + id;
    track.duration_seconds = 215;
    track.thumbnail_url = "https://example.test/" + id + ".jpg";
    return track.to_qml();
}

static Track track_of(const QString& id)
{
    return Track::from_qml(song(id));
}

static QVariantMap card(const QString& kind, const QString& id)
{
    Card made;
    made.kind = kind;
    made.browse_id = id;
    made.title = "Title " + id;
    made.subtitle = "Subtitle " + id;
    made.thumbnail_url = "https://example.test/" + id + ".jpg";
    return made.to_qml();
}

// "abc" for songs a, b, c
static QString ids_of(const TrackModel *tracks)
{
    QString ids;
    for (const Track& track : tracks->all())
    {
        ids += track.video_id;
    }
    return ids;
}

static QJsonObject read_json(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void CheckLibrary::trackModelEdits()
{
    TrackModel tracks;
    QSignalSpy changed(&tracks, &TrackModel::changed);

    tracks.append(track_of("a"));
    tracks.append(track_of("b"));
    tracks.append(track_of("c"));
    QCOMPARE(ids_of(&tracks), QString("abc"));
    QCOMPARE(changed.count(), 3);

    tracks.move_row(0, 2);
    QCOMPARE(ids_of(&tracks), QString("bca"));
    tracks.move_row(2, 0);
    QCOMPARE(ids_of(&tracks), QString("abc"));

    tracks.remove_at(1);
    QCOMPARE(ids_of(&tracks), QString("ac"));
    tracks.insert(1, track_of("x"));
    QCOMPARE(ids_of(&tracks), QString("axc"));
    const int changes_so_far = changed.count();

    // out of range edits are ignored and don't emit
    tracks.remove_at(9);
    tracks.remove_at(-1);
    tracks.insert(9, track_of("y"));
    tracks.move_row(0, 9);
    tracks.move_row(1, 1);
    QCOMPARE(ids_of(&tracks), QString("axc"));
    QCOMPARE(changed.count(), changes_so_far);

    tracks.clear();
    QCOMPARE(tracks.rowCount(), 0);
    QCOMPARE(changed.count(), changes_so_far + 1);
}

void CheckLibrary::songWithoutIdNotAdded()
{
    TrackModel tracks;
    QSignalSpy changed(&tracks, &TrackModel::changed);

    tracks.append(Track());
    QCOMPARE(tracks.rowCount(), 0);
    QCOMPARE(changed.count(), 0);
}

void CheckLibrary::cardsListedOnceNewestFirst()
{
    CardModel cards;
    cards.add_first(Card::from_qml(card("album", "A")));
    cards.add_first(Card::from_qml(card("album", "B")));
    cards.add_first(Card::from_qml(card("album", "A")));

    QCOMPARE(cards.rowCount(), 2);
    QCOMPARE(cards.cardAt(0).value("browseId").toString(), QString("A"));
    QCOMPARE(cards.cardAt(1).value("browseId").toString(), QString("B"));
    QVERIFY(cards.contains("B"));

    cards.add_first(Card());
    QCOMPARE(cards.rowCount(), 2);

    QVERIFY(cards.remove_id("B"));
    QVERIFY(!cards.remove_id("B"));
    QVERIFY(!cards.contains("B"));
}

void CheckLibrary::newLibraryIsEmptyAndWritesNothing()
{
    QTemporaryDir directory;
    const QString folder = directory.path() + "/music";
    {
        Library library(folder);
        QCOMPARE(library.playlists()->rowCount(), 0);
        QCOMPARE(library.liked()->rowCount(), 0);
        QCOMPARE(library.history()->rowCount(), 0);
        QCOMPARE(library.albums()->rowCount(), 0);
        QCOMPARE(library.artists()->rowCount(), 0);
    }
    QVERIFY(!QDir(folder).exists());
}

void CheckLibrary::createPlaylistKeepsOrderAndNamesBlank()
{
    QTemporaryDir directory;
    Library library(directory.path());

    const QString first = library.createPlaylist("Road Trip");
    const QString second = library.createPlaylist("   ");

    QVERIFY(first != second);
    QCOMPARE(library.playlists()->rowCount(), 2);
    QCOMPARE(library.playlists()->index(0, 0).data(PlaylistList::IdRole).toString(), first);
    QCOMPARE(library.playlists()->index(0, 0).data(PlaylistList::NameRole).toString(), QString("Road Trip"));
    QCOMPARE(library.playlists()->index(1, 0).data(PlaylistList::NameRole).toString(), QString("Playlist"));
    QCOMPARE(library.playlistName(first), QString("Road Trip"));
    QVERIFY(library.playlistTracks(first) != nullptr);
    QVERIFY(library.playlistTracks("no such playlist") == nullptr);
}

void CheckLibrary::duplicateSongNotAddedToPlaylist()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");

    QVERIFY(library.addToPlaylist(id, song("a")));
    QVERIFY(!library.addToPlaylist(id, song("a")));
    QVERIFY(library.addToPlaylist(id, song("b")));
    QCOMPARE(ids_of(library.playlistTracks(id)), QString("ab"));
    QVERIFY(library.playlistHas(id, "a"));
    QVERIFY(!library.playlistHas(id, "z"));

    QVERIFY(!library.addToPlaylist("no such playlist", song("a")));
    QVERIFY(!library.addToPlaylist(id, QVariantMap{{"title", "no id"}}));
}

void CheckLibrary::addAllCountsOnlyNewSongs()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");
    library.addToPlaylist(id, song("b"));

    TrackModel results;
    results.append(track_of("a"));
    results.append(track_of("b"));
    results.append(track_of("c"));

    QCOMPARE(library.addAllToPlaylist(id, &results), 2);
    QCOMPARE(ids_of(library.playlistTracks(id)), QString("bac"));
    QCOMPARE(library.addAllToPlaylist(id, &results), 0);
    QCOMPARE(library.addAllToPlaylist(id, nullptr), 0);

    // adding a playlist to itself must not loop
    QCOMPARE(library.addAllToPlaylist(id, library.playlistTracks(id)), 0);
}

void CheckLibrary::playlistFromListIsACopy()
{
    QTemporaryDir directory;
    Library library(directory.path());

    TrackModel album;
    album.append(track_of("a"));
    album.append(track_of("b"));
    const QString id = library.createPlaylistFrom("From an album", &album);
    album.clear();

    QCOMPARE(library.playlistName(id), QString("From an album"));
    QCOMPARE(ids_of(library.playlistTracks(id)), QString("ab"));
}

void CheckLibrary::playlistCountFollowsSongs()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");
    QSignalSpy changed(library.playlists(), &QAbstractItemModel::dataChanged);

    library.addToPlaylist(id, song("a"));

    QVERIFY(changed.count() >= 1);
    QCOMPARE(library.playlists()->index(0, 0).data(PlaylistList::CountRole).toInt(), 1);
}

void CheckLibrary::renameIgnoresBlankName()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");

    library.renamePlaylist(id, "  Evening Jazz ");
    QCOMPARE(library.playlistName(id), QString("Evening Jazz"));
    library.renamePlaylist(id, "   ");
    QCOMPARE(library.playlistName(id), QString("Evening Jazz"));
    library.renamePlaylist("no such playlist", "x");
}

void CheckLibrary::deletePlaylistRemovesFile()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");
    library.addToPlaylist(id, song("a"));
    library.flush();
    const QString file = directory.path() + "/playlists/" + id + ".json";
    QVERIFY(QFile::exists(file));

    library.deletePlaylist(id);

    QVERIFY(!QFile::exists(file));
    QCOMPARE(library.playlists()->rowCount(), 0);
    QVERIFY(library.playlistTracks(id) == nullptr);
    // a pending write must not bring the file back
    library.flush();
    QVERIFY(!QFile::exists(file));
}

void CheckLibrary::likeAndUnlike()
{
    QTemporaryDir directory;
    Library library(directory.path());
    QSignalSpy changed(&library, &Library::likedChanged);
    const int version_before = library.liked_version();

    library.setLiked(song("a"), true);
    library.setLiked(song("b"), true);
    library.setLiked(song("a"), true);  // already liked

    QVERIFY(library.isLiked("a"));
    QVERIFY(library.isLiked("b"));
    QVERIFY(!library.isLiked("c"));
    QCOMPARE(ids_of(library.liked()), QString("ba"));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(library.liked_version(), version_before + 2);

    library.setLiked(song("a"), false);
    QVERIFY(!library.isLiked("a"));
    QCOMPARE(ids_of(library.liked()), QString("b"));
    library.setLiked(song("zzz"), false);
    library.setLiked(QVariantMap{{"title", "no id"}}, true);
    QCOMPARE(ids_of(library.liked()), QString("b"));
}

void CheckLibrary::historyNewestFirstNoDuplicates()
{
    QTemporaryDir directory;
    Library library(directory.path());

    library.recordPlayed(song("a"));
    library.recordPlayed(song("b"));
    library.recordPlayed(song("c"));
    library.recordPlayed(song("a"));

    QCOMPARE(ids_of(library.history()), QString("acb"));

    library.clearHistory();
    QCOMPARE(library.history()->rowCount(), 0);
}

void CheckLibrary::historyIsCapped()
{
    QTemporaryDir directory;
    Library library(directory.path());

    const int total = Library::HISTORY_LIMIT + 5;
    for (int number = 1; number <= total; ++number)
    {
        library.recordPlayed(song(QString("s%1").arg(number)));
    }

    QCOMPARE(library.history()->rowCount(), Library::HISTORY_LIMIT);
    QCOMPARE(library.history()->trackAt(0).value("videoId").toString(), QString("s%1").arg(total));
    // s1..s5 fell off the end
    QCOMPARE(library.history()->trackAt(Library::HISTORY_LIMIT - 1).value("videoId").toString(), QString("s6"));
}

void CheckLibrary::replayingTopSongLeavesHistoryAlone()
{
    QTemporaryDir directory;
    Library library(directory.path());
    library.recordPlayed(song("a"));
    QSignalSpy changed(library.history(), &TrackModel::changed);

    library.recordPlayed(song("a"));

    QCOMPARE(changed.count(), 0);
}

void CheckLibrary::saveAndUnsave()
{
    QTemporaryDir directory;
    Library library(directory.path());
    QSignalSpy changed(&library, &Library::savedChanged);
    const int version_before = library.saved_version();

    library.save(card("album", "MPREb_1"));
    library.save(card("artist", "UC1"));
    library.save(card("playlist", "VLPL1"));  // playlists can't be saved, only copied
    library.save(card("album", "MPREb_2"));
    library.save(card("album", "MPREb_1"));   // moves to the front

    QCOMPARE(library.albums()->rowCount(), 2);
    QCOMPARE(library.albums()->cardAt(0).value("browseId").toString(), QString("MPREb_1"));
    QCOMPARE(library.artists()->rowCount(), 1);
    QVERIFY(library.isSaved("MPREb_1"));
    QVERIFY(library.isSaved("UC1"));
    QVERIFY(!library.isSaved("VLPL1"));
    QVERIFY(library.saved_version() > version_before);
    QVERIFY(changed.count() >= 4);

    library.unsave("MPREb_1");
    library.unsave("UC1");
    QVERIFY(!library.isSaved("MPREb_1"));
    QCOMPARE(library.albums()->rowCount(), 1);
    QCOMPARE(library.artists()->rowCount(), 0);
}

void CheckLibrary::removeFromHistory()
{
    QTemporaryDir directory;
    Library library(directory.path());
    library.recordPlayed(song("a"));
    library.recordPlayed(song("b"));
    library.recordPlayed(song("c"));

    library.removeFromHistory("b");
    QCOMPARE(ids_of(library.history()), QString("ca"));

    QSignalSpy changed(library.history(), &TrackModel::changed);
    library.removeFromHistory("x");
    QCOMPARE(changed.count(), 0);

    // played again, it's back on top
    library.recordPlayed(song("b"));
    QCOMPARE(ids_of(library.history()), QString("bca"));
}

void CheckLibrary::hiddenSongLeavesHistoryAndShelves()
{
    QTemporaryDir directory;
    Library library(directory.path());
    library.recordPlayed(song("a"));
    library.recordPlayed(song("b"));
    library.recordPlayed(song("c"));
    QSignalSpy changed(&library, &Library::hiddenChanged);
    const int version_before = library.hidden_version();

    library.setHidden(song("b"), true);

    QVERIFY(library.isHidden(song("b")));
    QVERIFY(!library.isHidden(song("a")));
    QCOMPARE(ids_of(library.history()), QString("ca"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(library.hidden_version() > version_before);

    // playing it by hand doesn't put it back
    library.recordPlayed(song("b"));
    QCOMPARE(ids_of(library.history()), QString("ca"));

    // a home shelf: songs and cards, marked with their type
    QVariantMap first = song("a");
    first.insert("type", "track");
    QVariantMap hidden = song("b");
    hidden.insert("type", "track");
    QVariantMap album = card("album", "MPREb_1");
    album.insert("type", "card");
    const QVariantList shown = library.withoutHidden({hidden, first, album, hidden});
    QCOMPARE(shown.size(), 2);
    QCOMPARE(shown.at(0).toMap().value("videoId").toString(), QString("a"));
    QCOMPARE(shown.at(0).toMap().value("type").toString(), QString("track"));
    QCOMPARE(shown.at(1).toMap().value("browseId").toString(), QString("MPREb_1"));

    // hiding it twice keeps one entry
    library.setHidden(song("b"), true);
    library.flush();
    QCOMPARE(read_json(directory.path() + "/hidden.json").value("tracks").toArray().size(), 1);
}

void CheckLibrary::hiddenSongStaysInLikedAndPlaylists()
{
    QTemporaryDir directory;
    Library library(directory.path());
    const QString id = library.createPlaylist("Jokes");
    library.addToPlaylist(id, song("v"));
    library.setLiked(song("v"), true);

    library.setHidden(song("v"), true);

    QVERIFY(library.isLiked("v"));
    QCOMPARE(ids_of(library.liked()), QString("v"));
    QVERIFY(library.playlistHas(id, "v"));
    QCOMPARE(ids_of(library.playlistTracks(id)), QString("v"));
}

void CheckLibrary::hidingMatchesOtherVersionsOfTheSong()
{
    QTemporaryDir directory;
    Library library(directory.path());

    QVariantMap valhalla = song("v1");
    valhalla.insert("title", "VALHALLA CALLING");
    valhalla.insert("artist", "Ragal Ironbull");
    // the same song as a music video, from the artist's topic channel
    QVariantMap video = song("v2");
    video.insert("title", "Valhalla  Calling");
    video.insert("artist", "Ragal Ironbull - Topic");
    // someone else's song of the same name isn't it
    QVariantMap cover = song("v3");
    cover.insert("title", "VALHALLA CALLING");
    cover.insert("artist", "Someone Else");

    library.recordPlayed(video);
    library.recordPlayed(cover);
    library.recordPlayed(song("a"));
    library.setHidden(valhalla, true);

    QVERIFY(library.isHidden(video));
    QVERIFY(!library.isHidden(cover));
    QCOMPARE(ids_of(library.history()), QString("av3"));

    // showing either version again shows the song
    library.setHidden(video, false);
    QVERIFY(!library.isHidden(valhalla));
    QVERIFY(!library.isHidden(video));

    // without a title or artist only the video id counts
    QVariantMap nameless = song("n1");
    nameless.insert("title", "");
    nameless.insert("artist", "");
    library.setHidden(nameless, true);
    QVariantMap other_nameless = nameless;
    other_nameless.insert("videoId", "n2");
    QVERIFY(library.isHidden(nameless));
    QVERIFY(!library.isHidden(other_nameless));
}

void CheckLibrary::showingAgain()
{
    QTemporaryDir directory;
    Library library(directory.path());
    library.setHidden(song("a"), true);
    QSignalSpy changed(&library, &Library::hiddenChanged);

    library.setHidden(song("a"), false);
    QVERIFY(!library.isHidden(song("a")));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(library.withoutHidden({song("a")}).size(), 1);
    library.recordPlayed(song("a"));
    QCOMPARE(ids_of(library.history()), QString("a"));

    // the history doesn't get back what hiding took out of it
    library.setHidden(song("a"), true);
    library.setHidden(song("a"), false);
    QCOMPARE(library.history()->rowCount(), 0);

    // showing a song that isn't hidden changes nothing
    changed.clear();
    library.setHidden(song("z"), false);
    QCOMPARE(changed.count(), 0);
}

void CheckLibrary::roundTripThroughFiles()
{
    QTemporaryDir directory;
    QString first_id;
    QString second_id;
    {
        Library library(directory.path());
        first_id = library.createPlaylist("Road Trip");
        second_id = library.createPlaylist("Focus");
        library.addToPlaylist(first_id, song("a"));
        library.addToPlaylist(first_id, song("b"));
        library.addToPlaylist(second_id, song("c"));
        library.setLiked(song("l"), true);
        library.recordPlayed(song("h1"));
        library.recordPlayed(song("h2"));
        library.save(card("album", "MPREb_1"));
        library.save(card("artist", "UC1"));
        library.flush();
    }

    Library again(directory.path());

    QCOMPARE(again.playlists()->rowCount(), 2);
    QCOMPARE(again.playlistName(first_id), QString("Road Trip"));
    QCOMPARE(again.playlists()->index(1, 0).data(PlaylistList::IdRole).toString(), second_id);
    QCOMPARE(ids_of(again.playlistTracks(first_id)), QString("ab"));
    QCOMPARE(ids_of(again.playlistTracks(second_id)), QString("c"));
    QCOMPARE(again.playlistTracks(first_id)->trackAt(0).value("thumbnailUrl").toString(),
        QString("https://example.test/a.jpg"));
    QVERIFY(again.isLiked("l"));
    QCOMPARE(ids_of(again.history()), QString("h2h1"));
    QCOMPARE(again.history()->trackAt(0).value("videoId").toString(), QString("h2"));
    QCOMPARE(again.albums()->cardAt(0).value("title").toString(), QString("Title MPREb_1"));
    QVERIFY(again.isSaved("UC1"));
}

void CheckLibrary::playlistOrderSurvivesReload()
{
    QTemporaryDir directory;
    QString id;
    {
        Library library(directory.path());
        id = library.createPlaylist("Mix");
        library.addToPlaylist(id, song("a"));
        library.addToPlaylist(id, song("b"));
        library.addToPlaylist(id, song("c"));
        library.playlistTracks(id)->move_row(2, 0);
        library.flush();
    }

    Library again(directory.path());
    QCOMPARE(ids_of(again.playlistTracks(id)), QString("cab"));
}

void CheckLibrary::writesAreBatchedAndFlushedOnExit()
{
    QTemporaryDir directory;
    const QString file = directory.path() + "/playlists/";

    {
        Library library(directory.path());
        const QString id = library.createPlaylist("Mix");
        for (int number = 0; number < 20; ++number)
        {
            library.addToPlaylist(id, song(QString("s%1").arg(number)));
        }
        QVERIFY(!QFile::exists(file + id + ".json"));
        // one batched write after the delay
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(file + id + ".json"), 3000);
        QCOMPARE(read_json(file + id + ".json").value("tracks").toArray().size(), 20);
    }

    QString left_at_once;
    {
        Library library(directory.path());
        left_at_once = library.createPlaylist("Written on leaving");
        library.addToPlaylist(left_at_once, song("z"));
        // destroyed with the write still pending
    }
    QVERIFY(QFile::exists(file + left_at_once + ".json"));
    QCOMPARE(read_json(file + left_at_once + ".json").value("name").toString(), QString("Written on leaving"));
}

void CheckLibrary::corruptFileIsMovedAside()
{
    QTemporaryDir directory;
    {
        QFile broken(directory.path() + "/liked.json");
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("this is not json");
    }

    Library library(directory.path());

    QCOMPARE(library.liked()->rowCount(), 0);
    QVERIFY(QFile::exists(directory.path() + "/liked.json.corrupt"));
    QVERIFY(!QFile::exists(directory.path() + "/liked.json"));

    library.setLiked(song("a"), true);
    library.flush();
    QCOMPARE(read_json(directory.path() + "/liked.json").value("tracks").toArray().size(), 1);
}

void CheckLibrary::unwritableFolderIsReported()
{
    QTemporaryDir directory;
    {
        // a file where the folder should be; works even when running as root
        QFile blocker(directory.path() + "/blocker");
        QVERIFY(blocker.open(QIODevice::WriteOnly));
    }

    Library library(directory.path() + "/blocker/inner");
    QSignalSpy problems(&library, &Library::problemChanged);

    library.createPlaylist("Mix");
    library.flush();

    QCOMPARE(problems.count(), 1);
    QVERIFY(library.problem().contains("Could not save"));
}

void CheckLibrary::failedWriteIsRetried()
{
    QTemporaryDir directory;
    const QString blocker = directory.path() + "/playlists";
    {
        // block the playlists folder only, so other files still save
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
    }

    Library library(directory.path());
    const QString id = library.createPlaylist("Mix");
    library.flush();
    QVERIFY(library.problem().contains("Could not save"));

    // another file saving fine must not hide the playlist's problem
    library.setLiked(song("a"), true);
    library.flush();
    QVERIFY(QFile::exists(directory.path() + "/liked.json"));
    QVERIFY(library.problem().contains("Could not save"));

    // unblock it; the retry timer should save it and clear the problem
    QVERIFY(QFile::remove(blocker));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(blocker + "/" + id + ".json"), 5000);
    QCOMPARE(read_json(blocker + "/" + id + ".json").value("name").toString(), QString("Mix"));
    QVERIFY(library.problem().isEmpty());
}

void CheckLibrary::hiddenSongsSurviveReload()
{
    QTemporaryDir directory;
    {
        Library library(directory.path());
        library.recordPlayed(song("a"));
        library.setHidden(song("h1"), true);
        library.setHidden(song("h2"), true);
        library.setHidden(song("h3"), true);
        library.setHidden(song("h3"), false);
    }

    // same shape as liked.json, newest first
    const QJsonArray tracks = read_json(directory.path() + "/hidden.json").value("tracks").toArray();
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.at(0).toObject().value("video_id").toString(), QString("h2"));
    QCOMPARE(tracks.at(0).toObject().value("title").toString(), QString("Song h2"));

    // as if it crashed between writing hidden.json and history.json
    QFile history(directory.path() + "/history.json");
    QVERIFY(history.open(QIODevice::WriteOnly));
    QJsonArray played{QJsonObject::fromVariantMap(track_of("h1").to_helper()),
                      QJsonObject::fromVariantMap(track_of("a").to_helper())};
    history.write(QJsonDocument(QJsonObject{{"tracks", played}}).toJson());
    history.close();

    Library again(directory.path());
    QVERIFY(again.isHidden(song("h1")));
    QVERIFY(again.isHidden(song("h2")));
    QVERIFY(!again.isHidden(song("h3")));
    QCOMPARE(ids_of(again.history()), QString("a"));
}

void CheckLibrary::fileIdsWrittenAnotherWayAreReadAsOne()
{
    QTemporaryDir directory;
    const QString id = "file:///music/a%20b/Bj%C3%B6rk%20%28live%29.mp3";
    const QString printed = QString::fromUtf8("file:///music/a b/Björk (live).mp3");
    auto tracks = [](const QStringList& ids)
    {
        QJsonArray array;
        for (const QString& each : ids)
        {
            array.append(QJsonObject{{"video_id", each}, {"title", "Song"}, {"artist", "Artist"}});
        }
        return QJsonDocument(QJsonObject{{"tracks", array}}).toJson();
    };
    QFile history(directory.filePath("history.json"));
    QVERIFY(history.open(QIODevice::WriteOnly) && history.write(tracks({"x", printed, id, "y"})) > 0);
    history.close();
    QFile liked(directory.filePath("liked.json"));
    QVERIFY(liked.open(QIODevice::WriteOnly) && liked.write(tracks({printed})) > 0);
    liked.close();

    Library library(directory.path());
    QCOMPARE(library.history()->rowCount(), 3);
    QCOMPARE(library.history()->trackAt(1).value("videoId").toString(), id);
    QCOMPARE(library.history()->trackAt(2).value("videoId").toString(), QString("y"));
    QVERIFY(library.isLiked(id));

    library.recordPlayed(library.history()->trackAt(1));
    QCOMPARE(library.history()->rowCount(), 3);
    QCOMPARE(library.history()->trackAt(0).value("videoId").toString(), id);
}

QTEST_GUILESS_MAIN(CheckLibrary)
