#include "music-client.hpp"

#include <QJsonArray>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>

#include <cstdio>
#include <utility>

// starting python takes milliseconds, so hitting this means python3 is missing
static constexpr int HELPER_START_TIMEOUT_MS = 5000;
static constexpr int RECENT_ERRORS_LIMIT_CHARS = 2000;
// the first scan of a big music folder reads every file's tags. nothing on the network,
// so it can't hang the way youtube can
static constexpr int LOCAL_SCAN_TIMEOUT_MS = 10 * 60 * 1000;
// it may be mid network request when we quit
static constexpr int HELPER_END_TIMEOUT_MS = 500;

MusicClient::MusicClient(const QString& helper_script, const QStringList& helper_arguments, QObject *parent)
    : QObject(parent)
{
    helper.setProgram(QStringLiteral("python3"));
    helper.setArguments(QStringList{helper_script} + helper_arguments);

    connect(&helper, &QProcess::readyReadStandardOutput, this, &MusicClient::read_output);
    connect(&helper, &QProcess::readyReadStandardError, this, &MusicClient::read_errors);
    connect(&helper, &QProcess::finished, this, &MusicClient::helper_ended);
}

MusicClient::~MusicClient()
{
    if (helper.state() == QProcess::NotRunning)
    {
        return;
    }

    // closing stdin is the helper's cue to exit
    helper.closeWriteChannel();
    if (!helper.waitForFinished(HELPER_END_TIMEOUT_MS))
    {
        helper.kill();
        helper.waitForFinished(HELPER_END_TIMEOUT_MS);
    }
}

TrackModel *MusicClient::results()
{
    return &song_results;
}

CardModel *MusicClient::album_results()
{
    return &album_cards;
}

CardModel *MusicClient::artist_results()
{
    return &artist_cards;
}

CardModel *MusicClient::playlist_results()
{
    return &playlist_cards;
}

TrackModel *MusicClient::radio_tracks()
{
    return &radio_songs;
}

bool MusicClient::searching() const
{
    return is_searching;
}

QString MusicClient::problem() const
{
    return problem_text;
}

void MusicClient::search(const QString& query, const QString& kind)
{
    const QString text = query.trimmed();
    if (text.isEmpty())
    {
        return;
    }

    const QString error = start_helper();
    if (!error.isEmpty())
    {
        set_problem(error);
        return;
    }

    const int id = send({{"command", "search"}, {"query", text}, {"kind", kind}}, {Kind::Search, {}, kind, nullptr});
    latest_search_id.insert(kind, id);
    update_searching();
}

bool MusicClient::is_local(const QString& video_id)
{
    return video_id.startsWith(QLatin1String("file:"));
}

void MusicClient::findAudio(const QString& video_id)
{
    // played straight from disk. answered from the event loop like a helper answer,
    // so qml sees the same order of events
    if (is_local(video_id))
    {
        const QUrl url(video_id);
        const bool there = QFileInfo::exists(url.toLocalFile());
        QTimer::singleShot(0, this, [this, video_id, url, there] {
            if (there)
            {
                emit audioFound(video_id, url);
            }
            else
            {
                emit audioNotFound(video_id, tr("%1 isn't there anymore.").arg(url.toLocalFile()));
            }
        });
        return;
    }

    const QString error = start_helper();
    if (!error.isEmpty())
    {
        emit audioNotFound(video_id, error);
        return;
    }
    send({{"command", "stream"}, {"video_id", video_id}}, {Kind::Audio, video_id, {}, nullptr});
}

void MusicClient::prepareAudio(const QString& video_id)
{
    if (!is_local(video_id) && start_helper().isEmpty())
    {
        send({{"command", "prepare"}, {"video_id", video_id}}, {Kind::Prepare, video_id, {}, nullptr});
    }
}

void MusicClient::startRadio(const QString& video_id)
{
    if (is_local(video_id))
    {
        QTimer::singleShot(0, this, [this, video_id] {
            emit radioFailed(video_id, tr("Radio needs a song from YouTube Music."));
        });
        return;
    }

    const QString error = start_helper();
    if (!error.isEmpty())
    {
        emit radioFailed(video_id, error);
        return;
    }
    send({{"command", "radio"}, {"video_id", video_id}}, {Kind::Radio, video_id, {}, nullptr});
}

void MusicClient::startRadioLike(const QString& video_id, const QString& query)
{
    const QString error = start_helper();
    if (!error.isEmpty())
    {
        emit radioFailed(video_id, error);
        return;
    }
    send({{"command", "radio_like"}, {"query", query}}, {Kind::Radio, video_id, {}, nullptr});
}

ContentPage *MusicClient::page(const QString& kind, const QString& browse_id)
{
    const QString key = kind + QLatin1Char(':') + browse_id;
    ContentPage *page = pages.value(key, nullptr);
    if (page == nullptr)
    {
        page = new ContentPage(kind, browse_id, this);
        pages.insert(key, page);
        request_page(page);
    }
    // keep showing the old content while it refreshes
    else if (!page->loading() && (!page->problem().isEmpty() || page->is_stale()))
    {
        request_page(page);
    }
    return page;
}

void MusicClient::request_page(ContentPage *page)
{
    // always go through loading so qml sees loadingChanged even if we fail right away
    page->start_loading();

    const QString error = start_helper();
    if (!error.isEmpty())
    {
        page->fail(error);
        return;
    }

    const bool local = page->kind() == QLatin1String("local");
    QJsonObject message{{"command", page->kind()}};
    if (page->kind() != QLatin1String("home") && !local)
    {
        message.insert(QStringLiteral("browse_id"), page->browse_id());
    }
    send(message, {Kind::Page, {}, {}, page}, local ? LOCAL_SCAN_TIMEOUT_MS : REQUEST_TIMEOUT_MS);
}

QString MusicClient::start_helper()
{
    if (helper.state() == QProcess::Running)
    {
        return {};
    }

    recent_errors.clear();
    unread_output.clear();
    helper.start();
    if (!helper.waitForStarted(HELPER_START_TIMEOUT_MS))
    {
        return tr("The music helper could not be started (%1). Check that python3 is installed, and that "
                  "python-ytmusicapi and yt-dlp are too.").arg(helper.errorString());
    }
    return {};
}

int MusicClient::send(QJsonObject message, const Request& request, int timeout_ms)
{
    const int id = next_id++;
    message.insert(QStringLiteral("id"), id);
    waiting.insert(id, request);
    helper.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    QTimer::singleShot(timeout_ms, this, [this, id] { time_out(id); });
    return id;
}

void MusicClient::read_output()
{
    unread_output += helper.readAllStandardOutput();

    int newline = unread_output.indexOf('\n');
    while (newline >= 0)
    {
        const QByteArray line = unread_output.left(newline);
        unread_output.remove(0, newline + 1);

        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (document.isObject())
        {
            handle_answer(document.object());
        }
        else if (!line.trimmed().isEmpty())
        {
            std::fprintf(stderr, "bandstand: the helper said something that is not JSON: %s\n",
                line.constData());
        }
        newline = unread_output.indexOf('\n');
    }
}

void MusicClient::read_errors()
{
    const QString text = QString::fromUtf8(helper.readAllStandardError());
    std::fprintf(stderr, "bandstand: helper: %s", qPrintable(text));

    recent_errors += text;
    if (recent_errors.size() > RECENT_ERRORS_LIMIT_CHARS)
    {
        recent_errors = recent_errors.right(RECENT_ERRORS_LIMIT_CHARS);
    }
}

void MusicClient::handle_answer(const QJsonObject& answer)
{
    const int id = answer.value(QStringLiteral("id")).toInt(-1);
    const bool ok = answer.value(QStringLiteral("ok")).toBool();
    const QString error = answer.value(QStringLiteral("error")).toString();

    // no id: the helper couldn't parse our line. unknown id: we already timed it out.
    if (!waiting.contains(id))
    {
        if (!ok)
        {
            std::fprintf(stderr, "bandstand: the helper refused a request: %s\n", qPrintable(error));
        }
        return;
    }

    const Request request = waiting.take(id);
    if (!ok)
    {
        fail(id, request, error);
        return;
    }

    switch (request.kind)
    {
    case Kind::Search:
        handle_search_answer(id, request, answer);
        break;
    case Kind::Page:
        if (request.page != nullptr)
        {
            request.page->fill(answer.toVariantMap());
        }
        break;
    case Kind::Radio:
        radio_songs.set_tracks(answer.value(QStringLiteral("tracks")).toArray().toVariantList());
        emit radioFound(request.video_id);
        break;
    case Kind::Audio:
        emit audioFound(request.video_id, QUrl(answer.value(QStringLiteral("url")).toString()));
        break;
    case Kind::Prepare:
        break;
    }
}

void MusicClient::handle_search_answer(int id, const Request& request, const QJsonObject& answer)
{
    if (latest_search_id.value(request.search_kind) == id)
    {
        const char *key = request.search_kind == QLatin1String("songs") ? "tracks" : "cards";
        show_search_results(request.search_kind, answer.value(QLatin1String(key)).toArray().toVariantList());
        // one problem for all kinds: qml only reads the client-wide `problem`, and we can't
        // tell which tab is showing (switching to an already searched tab sends nothing)
        set_problem({});
    }
    update_searching();
}

void MusicClient::show_search_results(const QString& kind, const QVariantList& found)
{
    if (kind == QLatin1String("songs"))
    {
        song_results.set_tracks(found);
    }
    else if (kind == QLatin1String("albums"))
    {
        album_cards.set_cards(found);
    }
    else if (kind == QLatin1String("artists"))
    {
        artist_cards.set_cards(found);
    }
    else if (kind == QLatin1String("playlists"))
    {
        playlist_cards.set_cards(found);
    }
}

void MusicClient::fail(int id, const Request& request, const QString& message)
{
    switch (request.kind)
    {
    case Kind::Search:
        if (latest_search_id.value(request.search_kind) == id)
        {
            show_search_results(request.search_kind, {});
            set_problem(message);
        }
        update_searching();
        break;
    case Kind::Page:
        if (request.page != nullptr)
        {
            request.page->fail(message);
        }
        break;
    case Kind::Radio:
        emit radioFailed(request.video_id, message);
        break;
    case Kind::Audio:
        emit audioNotFound(request.video_id, message);
        break;
    case Kind::Prepare:
        // it gets looked up again when it's its turn, and fails visibly then
        std::fprintf(stderr, "bandstand: could not prepare %s: %s\n",
            qPrintable(request.video_id), qPrintable(message));
        break;
    }
}

void MusicClient::time_out(int id)
{
    if (!waiting.contains(id))
    {
        return;
    }
    const Request request = waiting.take(id);
    if (request.kind == Kind::Page && request.page != nullptr && request.page->kind() == QLatin1String("local"))
    {
        fail(id, request, tr("Reading your music folders took too long. Try again, the songs read so far are kept."));
        return;
    }
    fail(id, request,
        tr("The music helper did not answer in %1 seconds. Check the network connection, or try again.")
            .arg(REQUEST_TIMEOUT_MS / 1000));
}

void MusicClient::helper_ended(int exit_code, QProcess::ExitStatus status)
{
    // nothing waiting means we closed it on purpose
    if (waiting.isEmpty())
    {
        return;
    }

    QString message = tr("The music helper stopped (%1).").arg(
        status == QProcess::CrashExit ? tr("it crashed") : tr("exit code %1").arg(exit_code));
    if (!recent_errors.trimmed().isEmpty())
    {
        message += QLatin1Char(' ') + recent_errors.trimmed();
    }

    // take them all first, failing one can trigger new requests from qml
    const QHash<int, Request> ended = std::exchange(waiting, {});
    for (auto it = ended.constBegin(); it != ended.constEnd(); ++it)
    {
        fail(it.key(), it.value(), message);
    }
}

void MusicClient::set_problem(const QString& text)
{
    if (text != problem_text)
    {
        problem_text = text;
        emit problemChanged();
    }
}

void MusicClient::update_searching()
{
    bool now_searching = false;
    for (auto it = waiting.constBegin(); it != waiting.constEnd(); ++it)
    {
        if (it.value().kind == Kind::Search && latest_search_id.value(it.value().search_kind) == it.key())
        {
            now_searching = true;
        }
    }

    if (now_searching != is_searching)
    {
        is_searching = now_searching;
        emit searchingChanged();
    }
}
