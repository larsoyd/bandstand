#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QUrl>

#include "card-model.hpp"
#include "content-page.hpp"
#include "track-model.hpp"

// talks to helper/music_helper.py over stdin/stdout, one json object per line.
// the protocol is documented at the top of the helper.
class MusicClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TrackModel* results READ results CONSTANT)
    Q_PROPERTY(CardModel* albumResults READ album_results CONSTANT)
    Q_PROPERTY(CardModel* artistResults READ artist_results CONSTANT)
    Q_PROPERTY(CardModel* playlistResults READ playlist_results CONSTANT)
    Q_PROPERTY(TrackModel* radioTracks READ radio_tracks CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY searchingChanged)
    // shared by all four search kinds, see handle_search_answer()
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)

  public:
    // helper_arguments go after the script, e.g. where it keeps played songs
    MusicClient(const QString& helper_script, const QStringList& helper_arguments, QObject *parent = nullptr);
    ~MusicClient() override;

    TrackModel *results();
    CardModel *album_results();
    CardModel *artist_results();
    CardModel *playlist_results();
    TrackModel *radio_tracks();
    bool searching() const;
    QString problem() const;

    // kind is "songs", "albums", "artists" or "playlists"
    Q_INVOKABLE void search(const QString& query, const QString& kind = QStringLiteral("songs"));
    // the url it finds expires after a few hours
    Q_INVOKABLE void findAudio(const QString& video_id);
    // looks up a song that's coming up and has the helper download it into its cache,
    // so it starts right away. nothing comes back; a failure only gets logged.
    Q_INVOKABLE void prepareAudio(const QString& video_id);
    // kind is "album", "artist", "playlist", "home" or "local" (browse_id is ignored for
    // the last two).
    // pages are cached and only re-requested when they failed or went stale.
    Q_INVOKABLE ContentPage *page(const QString& kind, const QString& browse_id);
    // local songs never reach the helper; startRadio() fails for them
    Q_INVOKABLE void startRadio(const QString& video_id);
    // for a song with no id of its own, starts from the closest one found
    Q_INVOKABLE void startRadioLike(const QString& video_id, const QString& query);

    // a song from the music folders (the "local" page), whose id is its file:// url
    static bool is_local(const QString& video_id);

  signals:
    void searchingChanged();
    void problemChanged();
    void audioFound(const QString& video_id, const QUrl& url);
    void audioNotFound(const QString& video_id, const QString& message);
    void radioFound(const QString& video_id);
    void radioFailed(const QString& video_id, const QString& message);

  private:
    // the helper retries youtube a few times itself; past this we give up on it
    static constexpr int REQUEST_TIMEOUT_MS = 30000;

    enum class Kind
    {
        Search,
        Audio,
        Prepare,
        Page,
        Radio,
    };

    struct Request
    {
        Kind kind;
        QString video_id;     // audio, prepare, radio
        QString search_kind;  // search
        QPointer<ContentPage> page;
    };

    // returns an error message, or empty if the helper is running
    QString start_helper();
    int send(QJsonObject message, const Request& request, int timeout_ms = REQUEST_TIMEOUT_MS);
    void request_page(ContentPage *page);
    void read_output();
    void read_errors();
    void handle_answer(const QJsonObject& answer);
    void handle_search_answer(int id, const Request& request, const QJsonObject& answer);
    void show_search_results(const QString& kind, const QVariantList& found);
    void fail(int id, const Request& request, const QString& message);
    void time_out(int id);
    void helper_ended(int exit_code, QProcess::ExitStatus status);
    void set_problem(const QString& text);
    void update_searching();

    QProcess helper;
    // a partial line waiting for its newline
    QByteArray unread_output;
    // tail of the helper's stderr, shown if it dies
    QString recent_errors;
    int next_id = 1;
    QHash<int, Request> waiting;
    // answers to older searches of the same kind are dropped
    QHash<QString, int> latest_search_id;
    bool is_searching = false;
    TrackModel song_results;
    CardModel album_cards;
    CardModel artist_cards;
    CardModel playlist_cards;
    TrackModel radio_songs;
    QHash<QString, ContentPage *> pages;
    QString problem_text;
};
