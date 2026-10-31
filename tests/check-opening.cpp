#include "check-opening.hpp"

#include <QFile>
#include <QLocalServer>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest>

#include "../local-songs.hpp"
#include "../song-inbox.hpp"

static QString make_file(const QTemporaryDir& folder, const QString& name)
{
    const QString path = folder.filePath(name);
    return QFile(path).open(QIODevice::WriteOnly) ? path : QString();
}

// hand_over blocks until the listener answers, and the listener lives on this thread
static bool hand_over_elsewhere(const QString& path, const QStringList& names)
{
    bool taken = false;
    QThread *thread = QThread::create([&] { taken = SongInbox::hand_over(path, names); });
    thread->start();
    for (int waited_ms = 0; !thread->isFinished() && waited_ms < 5000; waited_ms += 10)
    {
        QTest::qWait(10);
    }
    thread->wait();
    delete thread;
    return taken;
}

static QVariantMap only_song(const QVariantList& songs)
{
    return songs.size() == 1 ? songs.first().toMap() : QVariantMap();
}

void CheckOpening::fileUrlBecomesSong()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "Moonlit Cartography.flac");
    const QVariantMap song = only_song(local_songs({QUrl::fromLocalFile(path).toString()}, "/"));
    QCOMPARE(song.value("videoId").toString(), QUrl::fromLocalFile(path).toString());
    QCOMPARE(song.value("title").toString(), QString("Moonlit Cartography"));
    QCOMPARE(song.value("durationSeconds").toInt(), 0);
}

void CheckOpening::pathBecomesSong()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "a.mp3");
    QCOMPARE(only_song(local_songs({path}, "/")).value("videoId").toString(), QUrl::fromLocalFile(path).toString());
}

void CheckOpening::relativePathStartsAtFolder()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "a.ogg");
    QCOMPARE(only_song(local_songs({"a.ogg"}, folder.path())).value("videoId").toString(),
        QUrl::fromLocalFile(path).toString());
}

void CheckOpening::spacesAndUnicodeSurvive()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, QStringLiteral("blå bær #1 100%.opus"));
    const QVariantMap song = only_song(local_songs({path}, "/"));
    QCOMPARE(QUrl(song.value("videoId").toString()).toLocalFile(), path);
    QCOMPARE(song.value("title").toString(), QStringLiteral("blå bær #1 100%"));
}

void CheckOpening::orderIsKept()
{
    QTemporaryDir folder;
    const QString second = make_file(folder, "2.wav");
    const QString first = make_file(folder, "1.wav");
    const QVariantList songs = local_songs({second, first}, "/");
    QCOMPARE(songs.size(), 2);
    QCOMPARE(songs.at(0).toMap().value("title").toString(), QString("2"));
    QCOMPARE(songs.at(1).toMap().value("title").toString(), QString("1"));
}

void CheckOpening::missingFolderAndRemoteAreLeftOut()
{
    QTemporaryDir folder;
    const QString kept = make_file(folder, "kept.mp3");
    const QVariantList songs = local_songs(
        {folder.filePath("gone.mp3"), folder.path(), "https://example.test/a.mp3", "", kept}, "/");
    QCOMPARE(only_song(songs).value("title").toString(), QString("kept"));
}

void CheckOpening::songUrlIsTheFileOrItsPage()
{
    QCOMPARE(song_url("file:///music/Ane%20Bl%C3%A5.flac"), QString("file:///music/Ane%20Bl%C3%A5.flac"));
    QCOMPARE(song_url("dQw4w9WgXcQ"), QString("https://music.youtube.com/watch?v=dQw4w9WgXcQ"));
}

void CheckOpening::handOverWithNobodyListening()
{
    QTest::failOnWarning();
    QTemporaryDir folder;
    QVERIFY(!SongInbox::hand_over(folder.filePath("inbox"), {"file:///a.mp3"}));
}

void CheckOpening::handOverReachesListener()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "blå bær.mp3");
    SongInbox inbox;
    QVERIFY(inbox.listen(folder.filePath("inbox")));
    QSignalSpy opened(&inbox, &SongInbox::songsOpened);

    QVERIFY(hand_over_elsewhere(folder.filePath("inbox"), {QUrl::fromLocalFile(path).toString()}));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 2000);
    const QVariantMap song = only_song(opened.first().first().toList());
    QCOMPARE(QUrl(song.value("videoId").toString()).toLocalFile(), path);
}

void CheckOpening::handOverKeepsEverySongInOrder()
{
    QTemporaryDir folder;
    const QString second = make_file(folder, "2.mp3");
    const QString first = make_file(folder, "1.mp3");
    SongInbox inbox;
    QVERIFY(inbox.listen(folder.filePath("inbox")));
    QSignalSpy opened(&inbox, &SongInbox::songsOpened);

    QVERIFY(hand_over_elsewhere(folder.filePath("inbox"),
        {QUrl::fromLocalFile(second).toString(), QUrl::fromLocalFile(first).toString()}));
    QTRY_COMPARE_WITH_TIMEOUT(opened.size(), 1, 2000);
    const QVariantList songs = opened.first().first().toList();
    QCOMPARE(songs.size(), 2);
    QCOMPARE(songs.at(0).toMap().value("title").toString(), QString("2"));
    QCOMPARE(songs.at(1).toMap().value("title").toString(), QString("1"));
}

void CheckOpening::handOverDropsMissingFiles()
{
    QTemporaryDir folder;
    SongInbox inbox;
    QVERIFY(inbox.listen(folder.filePath("inbox")));
    QSignalSpy opened(&inbox, &SongInbox::songsOpened);

    QVERIFY(hand_over_elsewhere(folder.filePath("inbox"), {"file:///no/such/song.mp3"}));
    QVERIFY(!opened.wait(300));
}

void CheckOpening::secondListenerIsRefused()
{
    QTemporaryDir folder;
    SongInbox first;
    SongInbox second;
    QVERIFY(first.listen(folder.filePath("inbox")));
    QVERIFY(!second.listen(folder.filePath("inbox")));
}

void CheckOpening::handOverNeedsAnAnswer()
{
    QTemporaryDir folder;
    QLocalServer silent;
    QVERIFY(silent.listen(folder.filePath("inbox")));
    QVERIFY(!hand_over_elsewhere(folder.filePath("inbox"), {"file:///a.mp3"}));
}

void CheckOpening::startLockKeepsOthersOut()
{
    QTemporaryDir folder;
    StartLock first(folder.filePath("lock"));
    QVERIFY(first.held());
    StartLock second(folder.filePath("lock"), 100);
    QVERIFY(!second.held());
    first.release();
    StartLock third(folder.filePath("lock"), 100);
    QVERIFY(third.held());
}

void CheckOpening::startLockGoesWithItsHolder()
{
    QTemporaryDir folder;
    {
        StartLock first(folder.filePath("lock"));
        QVERIFY(first.held());
    }
    StartLock second(folder.filePath("lock"), 100);
    QVERIFY(second.held());
}

void CheckOpening::startLockFailsLoudlyOnBadPath()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("start lock failed"));
    StartLock lock("/nonexistent/folder/lock", 100);
    QVERIFY(!lock.held());
}

// a program that crashed leaves its socket file behind
void CheckOpening::staleSocketIsReplaced()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "inbox");
    SongInbox inbox;
    QVERIFY(!SongInbox::hand_over(path, {"file:///a.mp3"}));
    QVERIFY(inbox.listen(path));
}

QTEST_GUILESS_MAIN(CheckOpening)
