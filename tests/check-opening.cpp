#include "check-opening.hpp"

#include <QFile>
#include <QImage>
#include <QLocalServer>
#include <QProcess>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QtTest>

#include "../local-songs.hpp"
#include "../song-inbox.hpp"
#include "../window-focus.hpp"

static QString make_file(const QTemporaryDir& folder, const QString& name)
{
    const QString path = folder.filePath(name);
    return QFile(path).open(QIODevice::WriteOnly) ? path : QString();
}

// real encoded files, so the tags are read the way they are from a user's music
static QString make_tagged(const QTemporaryDir& folder, const QString& name, bool with_cover)
{
    const QString cover = folder.filePath("source.png");
    QStringList arguments{"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=3"};
    if (with_cover)
    {
        QImage(16, 16, QImage::Format_RGB32).save(cover);
        arguments << "-i" << cover << "-map" << "0" << "-map" << "1" << "-c:v" << "png" << "-disposition:v" << "attached_pic";
    }
    const QString path = folder.filePath(name);
    arguments << "-metadata" << "title=Blå Time" << "-metadata" << "artist=Ane" << "-metadata" << "album=Sjø" << path;
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", arguments);
    ffmpeg.waitForFinished(20000);
    if (ffmpeg.exitCode() != 0)
    {
        qWarning("[check-opening] ffmpeg failed: %s", ffmpeg.readAllStandardError().constData());
        return {};
    }
    QFile::remove(cover);
    return path;
}

static void compare_tags(const QVariantMap& song)
{
    QCOMPARE(song.value("title").toString(), QString("Blå Time"));
    QCOMPARE(song.value("artist").toString(), QString("Ane"));
    QCOMPARE(song.value("album").toString(), QString("Sjø"));
    QCOMPARE(song.value("durationSeconds").toInt(), 3);
}

static void write_message(QLocalSocket *socket, const QByteArray& body)
{
    const quint32 length = qToLittleEndian<quint32>(body.size());
    socket->write(reinterpret_cast<const char *>(&length), sizeof length);
    socket->write(body);
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

void CheckOpening::tagsReplaceTheFileName_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("mp3") << "a.mp3";
    QTest::newRow("flac") << "a.flac";
    QTest::newRow("ogg") << "a.ogg";
    QTest::newRow("m4a") << "a.m4a";
}

void CheckOpening::tagsReplaceTheFileName()
{
    QFETCH(QString, name);
    QTemporaryDir folder;
    const QString path = make_tagged(folder, name, false);
    QVERIFY(!path.isEmpty());
    compare_tags(only_song(local_songs({path}, "/", folder.filePath("covers"))));
}

void CheckOpening::fileWithoutTagsKeepsItsName()
{
    QTemporaryDir folder;
    const QString path = folder.filePath("Bare.flac");
    QProcess ffmpeg;
    ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "sine=duration=2", "-map_metadata", "-1", path});
    QVERIFY(ffmpeg.waitForFinished(20000) && ffmpeg.exitCode() == 0);
    const QVariantMap song = only_song(local_songs({path}, "/", folder.filePath("covers")));
    QCOMPARE(song.value("title").toString(), QString("Bare"));
    QCOMPARE(song.value("artist").toString(), QString());
    QCOMPARE(song.value("durationSeconds").toInt(), 2);
}

void CheckOpening::embeddedCoverBecomesAFile_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("mp3") << "a.mp3";
    QTest::newRow("flac") << "a.flac";
    QTest::newRow("m4a") << "a.m4a";
}

void CheckOpening::embeddedCoverBecomesAFile()
{
    QFETCH(QString, name);
    QTemporaryDir folder;
    const QString path = make_tagged(folder, name, true);
    QVERIFY(!path.isEmpty());
    const QVariantMap song = only_song(local_songs({path}, "/", folder.filePath("covers")));
    const QImage cover(QUrl(song.value("thumbnailUrl").toString()).toLocalFile());
    QCOMPARE(cover.size(), QSize(16, 16));
}

void CheckOpening::folderCoverIsTheFallback()
{
    QTemporaryDir folder;
    const QString path = make_tagged(folder, "a.flac", false);
    QVERIFY(!path.isEmpty());
    const QString picture = folder.filePath("Cover.JPG");
    QVERIFY(QImage(8, 8, QImage::Format_RGB32).save(picture, "JPG"));
    const QVariantMap song = only_song(local_songs({path}, "/", folder.filePath("covers")));
    QCOMPARE(song.value("thumbnailUrl").toString(), QUrl::fromLocalFile(picture).toString());
}

void CheckOpening::fileNamedLikeAudioButIsNotIsStillListed()
{
    QTemporaryDir folder;
    const QString path = make_file(folder, "empty.mp3");
    const QVariantMap song = only_song(local_songs({path}, "/", folder.filePath("covers")));
    QCOMPARE(song.value("title").toString(), QString("empty"));
    QCOMPARE(song.value("thumbnailUrl").toString(), QString());
}

void CheckOpening::ownViewIsFoundInTheList()
{
    const QByteArray views = R"([
        {"id": 1, "pid": 7, "role": "toplevel", "mapped": true},
        {"id": 2, "pid": 9, "role": "desktop-environment", "mapped": true},
        {"id": 3, "pid": 9, "role": "toplevel", "mapped": false},
        {"id": 4, "pid": 9, "role": "toplevel", "mapped": true}])";
    QCOMPARE(own_view_id(views, 9), 4);
    QCOMPARE(own_view_id(views, 8), -1);
    QCOMPARE(own_view_id("not json", 9), -1);
}

void CheckOpening::focusAsksForTheOwnView()
{
    QTemporaryDir folder;
    QLocalServer compositor;
    QVERIFY(compositor.listen(folder.filePath("wayfire.socket")));
    QByteArray seen;
    connect(&compositor, &QLocalServer::newConnection, this, [&] {
        QLocalSocket *socket = compositor.nextPendingConnection();
        connect(socket, &QLocalSocket::readyRead, this, [&seen, socket] {
            const QByteArray message = socket->readAll().mid(4);
            seen += message + '\n';
            write_message(socket, message.contains("list-views")
                ? QByteArray(R"([{"id": 42, "pid": )") + QByteArray::number(QCoreApplication::applicationPid())
                    + R"(, "role": "toplevel", "mapped": true}])"
                : QByteArray(R"({"result": "ok"})"));
        });
    });
    bool focused = false;
    QThread *thread = QThread::create([&] { focused = focus_own_window(compositor.fullServerName()); });
    thread->start();
    QTRY_VERIFY_WITH_TIMEOUT(thread->isFinished(), 3000);
    delete thread;
    QVERIFY(focused);
    QVERIFY2(seen.contains("window-rules/focus-view") && seen.contains("\"id\":42"), seen.constData());
}

void CheckOpening::focusWithoutCompositorIsARefusal()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("focus failed reason=no_compositor"));
    QVERIFY(!focus_own_window("/nonexistent/wayfire.socket"));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("focus failed reason=no_compositor"));
    QVERIFY(!focus_own_window(""));
}

QTEST_GUILESS_MAIN(CheckOpening)
