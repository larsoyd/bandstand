#include "check-queue.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

#include "../library.hpp"
#include "../play-queue.hpp"
#include "../track-model.hpp"
#include "../track.hpp"

// a song in the helper's shape
static QVariantMap helper_song(const QString& id, const QString& title = QString())
{
    return {
        {"video_id", id},
        {"title", title.isEmpty() ? id : title},
        {"artist", "Artist " + id},
        {"album", "Album " + id},
        {"duration_seconds", 215},
        {"thumbnail_url", "https://example.test/" + id + ".jpg"},
    };
}

// songs a, b, c, ...
static void fill_results(TrackModel& results, int count)
{
    QVariantList songs;
    for (int index = 0; index < count; ++index)
    {
        songs.append(helper_song(QString(QChar('a' + index))));
    }
    results.set_tracks(songs);
}

static QString ids_of(const PlayQueue& queue)
{
    QString ids;
    for (int row = 0; row < queue.rowCount(); ++row)
    {
        ids += queue.trackAt(row).value("videoId").toString();
    }
    return ids;
}

void CheckQueue::trackRoundTrip()
{
    const Track track = Track::from_helper(helper_song("x", "Moonlit Cartography"));
    QCOMPARE(track.video_id, QString("x"));
    QCOMPARE(track.title, QString("Moonlit Cartography"));
    QCOMPARE(track.duration_seconds, 215);

    const QVariantMap for_qml = track.to_qml();
    QCOMPARE(for_qml.value("videoId").toString(), QString("x"));
    QCOMPARE(for_qml.value("durationSeconds").toInt(), 215);
    QCOMPARE(for_qml.value("thumbnailUrl").toString(), QString("https://example.test/x.jpg"));

    const Track back = Track::from_qml(for_qml);
    QCOMPARE(back.video_id, track.video_id);
    QCOMPARE(back.artist, track.artist);
    QCOMPARE(back.album, track.album);
    QCOMPARE(back.thumbnail_url, track.thumbnail_url);
}

void CheckQueue::trackNeedsVideoId()
{
    QVERIFY(!Track::from_helper({{"title", "No id"}}).is_valid());
    QVERIFY(Track::from_helper(helper_song("x")).is_valid());
}

void CheckQueue::fileSongHasOneIdHoweverItIsWritten_data()
{
    QTest::addColumn<QString>("written");
    QTest::addColumn<QString>("id");
    const QString odd = "file:///music/a%20b/Bj%C3%B6rk%20%28live%29%20%5B2014%5D%20%231%20100%25%20%2B%26%2C%3B%3D%27%21~%3F%40%24%2A.mp3";
    QTest::newRow("as the folder scan writes it") << odd << odd;
    QTest::newRow("as qt prints a file url")
        << QString::fromUtf8("file:///music/a b/Björk (live) %5B2014%5D %231 100%25 +&,;='!~%3F@$*.mp3") << odd;
    QTest::newRow("sub delimiters left alone")
        << QString::fromUtf8("file:///music/a%20b/Bj%C3%B6rk%20(live)%20%5B2014%5D%20%231%20100%25%20+&,;='!~%3F@$*.mp3") << odd;
    QTest::newRow("other scripts") << QString::fromUtf8("file:///music/皇后大道東 (feat. 蔣志光).mp3")
        << "file:///music/%E7%9A%87%E5%90%8E%E5%A4%A7%E9%81%93%E6%9D%B1%20%28feat.%20%E8%94%A3%E5%BF%97%E5%85%89%29.mp3";
    QTest::newRow("plain") << "file:///music/a.mp3" << "file:///music/a.mp3";
    QTest::newRow("not a file") << "dQw4w9WgXcQ" << "dQw4w9WgXcQ";
}

void CheckQueue::fileSongHasOneIdHoweverItIsWritten()
{
    QFETCH(QString, written);
    QFETCH(QString, id);
    QCOMPARE(Track::from_helper({{"video_id", written}}).video_id, id);
    QCOMPARE(Track::from_qml({{"videoId", written}}).video_id, id);
}

void CheckQueue::setTracksDropsSongsWithoutId()
{
    TrackModel results;
    results.set_tracks({helper_song("a"), QVariantMap{{"title", "No id"}}, helper_song("b")});
    QCOMPARE(results.rowCount(), 2);
    QCOMPARE(results.trackAt(1).value("videoId").toString(), QString("b"));
    QVERIFY(results.trackAt(5).isEmpty());
}

void CheckQueue::totalSeconds()
{
    TrackModel results;
    QSignalSpy changed(&results, &TrackModel::changed);
    QCOMPARE(results.total_seconds(), 0);

    fill_results(results, 3);
    QCOMPARE(results.total_seconds(), 3 * 215);

    // same count, different total: qml relies on `changed` firing here
    changed.clear();
    results.removeAt(2);
    results.append(Track::from_helper({{"video_id", "long"}, {"duration_seconds", 3600}}));
    QCOMPARE(results.total_seconds(), 2 * 215 + 3600);
    QVERIFY(changed.count() >= 1);
}

void CheckQueue::playFromQueuesWholeList()
{
    TrackModel results;
    fill_results(results, 4);
    PlayQueue queue;

    queue.playFrom(&results, 2);

    QCOMPARE(ids_of(queue), QString("abcd"));
    QCOMPARE(queue.current(), 2);
    QVERIFY(queue.has_previous());
    QVERIFY(queue.has_next());
    QCOMPARE(queue.up_next_count(), 1);
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("c"));

    // the queue keeps its own copy
    results.set_tracks({helper_song("z")});
    QCOMPARE(ids_of(queue), QString("abcd"));
}

void CheckQueue::playFromBadRowDoesNothing()
{
    TrackModel results;
    fill_results(results, 2);
    PlayQueue queue;

    queue.playFrom(&results, 5);
    queue.playFrom(&results, -1);
    queue.playFrom(nullptr, 0);

    QCOMPARE(queue.rowCount(), 0);
    QCOMPARE(queue.current(), -1);
    QVERIFY(!queue.has_next());
    QVERIFY(!queue.has_previous());
    QVERIFY(queue.currentTrack().isEmpty());
}

void CheckQueue::stepStaysInBounds()
{
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    queue.playFrom(&results, 0);

    QVERIFY(!queue.step(-1));
    QCOMPARE(queue.current(), 0);
    QVERIFY(queue.step(1));
    QVERIFY(queue.step(1));
    QCOMPARE(queue.current(), 2);
    QVERIFY(!queue.step(1));
    QCOMPARE(queue.current(), 2);
    QVERIFY(!queue.has_next());
    QVERIFY(queue.step(-1));
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("b"));
}

void CheckQueue::playNextGoesAfterCurrent()
{
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    queue.playFrom(&results, 1);

    queue.playNext(Track::from_helper(helper_song("x")).to_qml());

    QCOMPARE(ids_of(queue), QString("abxc"));
    QCOMPARE(queue.current(), 1);
    QCOMPARE(queue.up_next_count(), 2);

    // the most recent play next goes first
    queue.playNext(Track::from_helper(helper_song("y")).to_qml());
    QCOMPARE(ids_of(queue), QString("abyxc"));
}

void CheckQueue::playNextOnEmptyQueueBecomesCurrent()
{
    PlayQueue queue;
    queue.playNext(Track::from_helper(helper_song("x")).to_qml());

    QCOMPARE(ids_of(queue), QString("x"));
    QCOMPARE(queue.current(), 0);
    QVERIFY(!queue.has_next());
}

void CheckQueue::addToEndKeepsCurrent()
{
    TrackModel results;
    fill_results(results, 2);
    PlayQueue queue;
    queue.playFrom(&results, 0);

    queue.addToEnd(Track::from_helper(helper_song("x")).to_qml());

    QCOMPARE(ids_of(queue), QString("abx"));
    QCOMPARE(queue.current(), 0);
    QCOMPARE(queue.up_next_count(), 2);

    queue.addToEnd({{"title", "No id"}});
    QCOMPARE(queue.rowCount(), 3);
}

void CheckQueue::removeBeforeCurrentKeepsCurrent()
{
    TrackModel results;
    fill_results(results, 4);
    PlayQueue queue;
    queue.playFrom(&results, 2);

    queue.removeAt(0);

    QCOMPARE(ids_of(queue), QString("bcd"));
    QCOMPARE(queue.current(), 1);
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("c"));

    queue.removeAt(2);
    QCOMPARE(ids_of(queue), QString("bc"));
    QCOMPARE(queue.current(), 1);
}

void CheckQueue::currentSongCannotBeRemoved()
{
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    queue.playFrom(&results, 1);

    queue.removeAt(1);
    queue.removeAt(-1);
    queue.removeAt(9);

    QCOMPARE(ids_of(queue), QString("abc"));
    QCOMPARE(queue.current(), 1);
}

void CheckQueue::clearUpNextKeepsPlayedSongs()
{
    TrackModel results;
    fill_results(results, 4);
    PlayQueue queue;
    queue.playFrom(&results, 1);

    queue.clearUpNext();

    QCOMPARE(ids_of(queue), QString("ab"));
    QCOMPARE(queue.current(), 1);
    QVERIFY(!queue.has_next());
    QVERIFY(queue.has_previous());

    QSignalSpy changed(&queue, &PlayQueue::placeChanged);
    queue.clearUpNext();
    QCOMPARE(changed.count(), 0);
}

void CheckQueue::jumpTo()
{
    TrackModel results;
    fill_results(results, 4);
    PlayQueue queue;
    queue.playFrom(&results, 0);

    QVERIFY(queue.jumpTo(3));
    QCOMPARE(queue.current(), 3);
    QVERIFY(!queue.jumpTo(4));
    QVERIFY(!queue.jumpTo(-1));
    QCOMPARE(queue.current(), 3);
}

void CheckQueue::playShuffledQueuesEverySongOnce()
{
    TrackModel results;
    fill_results(results, 12);
    PlayQueue queue;
    QSignalSpy changed(&queue, &PlayQueue::placeChanged);

    queue.playShuffled(&results);

    // order is random, so only check it's the same set
    QCOMPARE(queue.rowCount(), 12);
    QString ids = ids_of(queue);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, QString("abcdefghijkl"));
    QCOMPARE(queue.current(), 0);
    QVERIFY(queue.has_next());
    QVERIFY(!queue.has_previous());
    QCOMPARE(changed.count(), 1);

    // the source list isn't shuffled in place
    QCOMPARE(results.trackAt(0).value("videoId").toString(), QString("a"));
}

void CheckQueue::playShuffledEmptyDoesNothing()
{
    TrackModel empty;
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    queue.playFrom(&results, 1);
    QSignalSpy changed(&queue, &PlayQueue::placeChanged);

    queue.playShuffled(&empty);
    queue.playShuffled(nullptr);

    QCOMPARE(ids_of(queue), QString("abc"));
    QCOMPARE(queue.current(), 1);
    QCOMPARE(changed.count(), 0);
}

void CheckQueue::placeChangedOnlyOnRealChanges()
{
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    QSignalSpy changed(&queue, &PlayQueue::placeChanged);

    queue.playFrom(&results, 0);
    QCOMPARE(changed.count(), 1);
    queue.step(1);
    QCOMPARE(changed.count(), 2);
    queue.addToEnd(Track::from_helper(helper_song("x")).to_qml());
    QCOMPARE(changed.count(), 3);
    queue.removeAt(3);
    QCOMPARE(changed.count(), 4);

    queue.jumpTo(1);
    QCOMPARE(changed.count(), 4);
}

// hides songs by video id, in a library of its own
static void hide(Library& library, const QString& id)
{
    library.setHidden(Track::from_helper(helper_song(id)).to_qml(), true);
}

void CheckQueue::playFromLeavesOutHiddenButNotThePickedSong()
{
    QTemporaryDir directory;
    Library library(directory.path());
    TrackModel results;
    fill_results(results, 5);
    PlayQueue queue;
    queue.set_library(&library);
    hide(library, "b");
    hide(library, "d");

    queue.playFrom(&results, 1);
    QCOMPARE(ids_of(queue), QString("abce"));
    QCOMPARE(queue.current(), 1);

    queue.playFrom(&results, 2);
    QCOMPARE(ids_of(queue), QString("ace"));
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("c"));
}

void CheckQueue::playListLeavesOutHidden()
{
    QTemporaryDir directory;
    Library library(directory.path());
    PlayQueue queue;
    queue.set_library(&library);
    hide(library, "b");

    // a home shelf: a card has no video id and is skipped anyway
    const QVariantList shelf{
        Track::from_helper(helper_song("a")).to_qml(),
        Track::from_helper(helper_song("b")).to_qml(),
        QVariantMap{{"type", "card"}, {"browseId", "MPREb_1"}},
        Track::from_helper(helper_song("c")).to_qml(),
    };
    queue.playList(shelf, 3);
    QCOMPARE(ids_of(queue), QString("ac"));
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("c"));
}

void CheckQueue::radioKeepsItsHiddenSeed()
{
    QTemporaryDir directory;
    Library library(directory.path());
    TrackModel radio;
    fill_results(radio, 4);
    PlayQueue queue;
    queue.set_library(&library);
    hide(library, "a");
    hide(library, "c");

    // the radio was started from a, by hand
    queue.playFrom(&radio, 0);
    QCOMPARE(ids_of(queue), QString("abd"));
    QCOMPARE(queue.current(), 0);
}

void CheckQueue::playAllStartsAtFirstShownSong()
{
    QTemporaryDir directory;
    Library library(directory.path());
    TrackModel results;
    fill_results(results, 3);
    PlayQueue queue;
    queue.set_library(&library);
    hide(library, "a");

    queue.playAll(&results);
    QCOMPARE(ids_of(queue), QString("bc"));
    QCOMPARE(queue.current(), 0);

    // nothing but hidden songs: asked for, so they play
    hide(library, "b");
    hide(library, "c");
    queue.playAll(&results);
    QCOMPARE(ids_of(queue), QString("abc"));
    QCOMPARE(queue.current(), 0);

    TrackModel empty;
    queue.playAll(&empty);
    queue.playAll(nullptr);
    QCOMPARE(ids_of(queue), QString("abc"));
}

void CheckQueue::playShuffledLeavesOutHidden()
{
    QTemporaryDir directory;
    Library library(directory.path());
    TrackModel results;
    fill_results(results, 6);
    PlayQueue queue;
    queue.set_library(&library);
    hide(library, "c");

    queue.playShuffled(&results);
    QString ids = ids_of(queue);
    std::sort(ids.begin(), ids.end());
    QCOMPARE(ids, QString("abdef"));
}

void CheckQueue::hidingDropsSongFromQueueButNotThePlayingOne()
{
    QTemporaryDir directory;
    Library library(directory.path());
    TrackModel results;
    fill_results(results, 5);
    PlayQueue queue;
    queue.set_library(&library);
    // played twice: as the next song and again later
    queue.playFrom(&results, 1);
    queue.addToEnd(Track::from_helper(helper_song("c")).to_qml());
    QCOMPARE(ids_of(queue), QString("abcdec"));
    QSignalSpy changed(&queue, &PlayQueue::placeChanged);

    hide(library, "c");
    QCOMPARE(ids_of(queue), QString("abde"));
    QCOMPARE(queue.current(), 1);
    QCOMPARE(changed.count(), 1);

    // played already: gone too, so repeat all doesn't bring it back
    hide(library, "a");
    QCOMPARE(ids_of(queue), QString("bde"));
    QCOMPARE(queue.current(), 0);

    // the playing song stays until it's over
    hide(library, "b");
    QCOMPARE(ids_of(queue), QString("bde"));
    QCOMPARE(queue.currentTrack().value("videoId").toString(), QString("b"));

    // shown again doesn't put it back, nor touch the queue
    changed.clear();
    library.setHidden(Track::from_helper(helper_song("c")).to_qml(), false);
    QCOMPARE(ids_of(queue), QString("bde"));
    QCOMPARE(changed.count(), 0);
}

// a library uses timers, so this needs an event loop
QTEST_GUILESS_MAIN(CheckQueue)
