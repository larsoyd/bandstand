#include "media-controls.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>

#include <algorithm>
#include <cstdio>

#include "local-songs.hpp"

static const QString OBJECT_PATH = QStringLiteral("/org/mpris/MediaPlayer2");
static const QString SERVICE_NAME = QStringLiteral("org.mpris.MediaPlayer2.bandstand");
static const QString PLAYER_INTERFACE = QStringLiteral("org.mpris.MediaPlayer2.Player");
// mpris wants an object path per track; ours aren't real objects
static const QString TRACK_PATH_PREFIX = QStringLiteral("/com/mavernix/bandstand/track/");
static const QString NO_TRACK_PATH = QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack");

static constexpr qint64 US_PER_MS = 1000;

MediaControls::MediaControls(QObject *parent)
    : QObject(parent)
{
    new MediaPlayerAdaptor(this);
    new PlayerAdaptor(this);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
    {
        std::fprintf(stderr, "bandstand: no session bus, so the media keys won't reach us\n");
        return;
    }

    bus.registerObject(OBJECT_PATH, this);
    // a second bandstand gets its own name, as the spec suggests
    service_name = SERVICE_NAME;
    if (bus.interface()->isServiceRegistered(service_name))
    {
        service_name += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
    }
    if (!bus.registerService(service_name))
    {
        std::fprintf(stderr, "bandstand: could not take %s on the session bus: %s\n",
            qPrintable(service_name), qPrintable(bus.lastError().message()));
        service_name.clear();
    }
}

MediaControls::~MediaControls()
{
    if (!service_name.isEmpty())
    {
        QDBusConnection::sessionBus().unregisterService(service_name);
    }
}

bool MediaControls::playing() const
{
    return is_playing;
}

QVariantMap MediaControls::track() const
{
    return current_track;
}

qint64 MediaControls::duration_ms() const
{
    return length_ms;
}

qint64 MediaControls::position_ms() const
{
    return at_ms;
}

bool MediaControls::can_next() const
{
    return next_possible;
}

bool MediaControls::can_previous() const
{
    return previous_possible;
}

double MediaControls::volume() const
{
    return current_volume;
}

QString MediaControls::repeat() const
{
    return repeat_mode;
}

void MediaControls::set_playing(bool value)
{
    if (value != is_playing)
    {
        is_playing = value;
        announce({QStringLiteral("PlaybackStatus")});
    }
}

void MediaControls::set_track(const QVariantMap& value)
{
    if (value != current_track)
    {
        const bool had_song = has_song();
        current_track = value;
        QStringList names{QStringLiteral("Metadata")};
        if (had_song != has_song())
        {
            names << QStringLiteral("PlaybackStatus") << QStringLiteral("CanPlay") << QStringLiteral("CanPause")
                  << QStringLiteral("CanSeek") << QStringLiteral("CanGoPrevious");
        }
        announce(names);
    }
}

void MediaControls::set_duration_ms(qint64 value)
{
    if (value != length_ms)
    {
        length_ms = value;
        announce({QStringLiteral("Metadata")});
    }
}

void MediaControls::set_position_ms(qint64 value)
{
    // changes all the time, mpris clients ask for it rather than being told
    at_ms = value;
}

void MediaControls::set_can_next(bool value)
{
    if (value != next_possible)
    {
        next_possible = value;
        announce({QStringLiteral("CanGoNext")});
    }
}

void MediaControls::set_can_previous(bool value)
{
    if (value != previous_possible)
    {
        previous_possible = value;
        announce({QStringLiteral("CanGoPrevious")});
    }
}

void MediaControls::set_volume(double value)
{
    if (value != current_volume)
    {
        current_volume = value;
        announce({QStringLiteral("Volume")});
    }
}

void MediaControls::set_repeat(const QString& value)
{
    if (value != repeat_mode)
    {
        repeat_mode = value;
        // repeat all can make the last song have a next one
        announce({QStringLiteral("LoopStatus"), QStringLiteral("CanGoNext")});
    }
}

void MediaControls::seeked(qint64 position_ms)
{
    at_ms = position_ms;
    emit announceSeeked(position_ms * US_PER_MS);
}

bool MediaControls::has_song() const
{
    return !current_track.value(QStringLiteral("videoId")).toString().isEmpty();
}

QString MediaControls::playback_status() const
{
    if (!has_song())
    {
        return QStringLiteral("Stopped");
    }
    return is_playing ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

QVariantMap MediaControls::metadata() const
{
    if (!has_song())
    {
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(track_path())}};
    }

    const QString video_id = current_track.value(QStringLiteral("videoId")).toString();
    QVariantMap map{
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(track_path())},
        {QStringLiteral("xesam:title"), current_track.value(QStringLiteral("title")).toString()},
        {QStringLiteral("xesam:url"), song_url(video_id)},
    };

    const QString artist = current_track.value(QStringLiteral("artist")).toString();
    if (!artist.isEmpty())
    {
        map.insert(QStringLiteral("xesam:artist"), QStringList{artist});
    }
    const QString album = current_track.value(QStringLiteral("album")).toString();
    if (!album.isEmpty())
    {
        map.insert(QStringLiteral("xesam:album"), album);
    }
    const QString art = current_track.value(QStringLiteral("thumbnailUrl")).toString();
    if (!art.isEmpty())
    {
        map.insert(QStringLiteral("mpris:artUrl"), art);
    }
    if (length_ms > 0)
    {
        map.insert(QStringLiteral("mpris:length"), qlonglong(length_ms * US_PER_MS));
    }
    return map;
}

QDBusObjectPath MediaControls::track_path() const
{
    if (!has_song())
    {
        return QDBusObjectPath(NO_TRACK_PATH);
    }
    // video ids have - and _, object paths only allow letters, digits and _
    const QByteArray id = current_track.value(QStringLiteral("videoId")).toString().toUtf8().toHex();
    return QDBusObjectPath(TRACK_PATH_PREFIX + QString::fromLatin1(id));
}

void MediaControls::announce(const QStringList& names)
{
    if (service_name.isEmpty())
    {
        return;
    }

    PlayerAdaptor *player = findChild<PlayerAdaptor *>();
    QVariantMap changed;
    for (const QString& name : names)
    {
        changed.insert(name, player->property(qPrintable(name)));
    }

    QDBusMessage signal = QDBusMessage::createSignal(OBJECT_PATH,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    signal << PLAYER_INTERFACE << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

// --- org.mpris.MediaPlayer2

MediaPlayerAdaptor::MediaPlayerAdaptor(MediaControls *controls)
    : QDBusAbstractAdaptor(controls)
    , controls(controls)
{
}

bool MediaPlayerAdaptor::can_quit() const
{
    return true;
}

bool MediaPlayerAdaptor::can_raise() const
{
    return true;
}

bool MediaPlayerAdaptor::has_track_list() const
{
    return false;
}

QString MediaPlayerAdaptor::identity() const
{
    return QStringLiteral("Bandstand");
}

QString MediaPlayerAdaptor::desktop_entry() const
{
    return QStringLiteral("bandstand");
}

QStringList MediaPlayerAdaptor::supported_uri_schemes() const
{
    return {};
}

QStringList MediaPlayerAdaptor::supported_mime_types() const
{
    return {};
}

void MediaPlayerAdaptor::Raise()
{
    emit controls->raiseRequested();
}

void MediaPlayerAdaptor::Quit()
{
    QCoreApplication::quit();
}

// --- org.mpris.MediaPlayer2.Player

PlayerAdaptor::PlayerAdaptor(MediaControls *controls)
    : QDBusAbstractAdaptor(controls)
    , controls(controls)
{
    connect(controls, &MediaControls::announceSeeked, this, &PlayerAdaptor::Seeked);
}

QString PlayerAdaptor::playback_status() const
{
    return controls->playback_status();
}

// mpris names repeat all "Playlist" and repeat one "Track"
QString PlayerAdaptor::loop_status() const
{
    const QString repeat = controls->repeat();
    if (repeat == QLatin1String("all"))
    {
        return QStringLiteral("Playlist");
    }
    return repeat == QLatin1String("one") ? QStringLiteral("Track") : QStringLiteral("None");
}

void PlayerAdaptor::set_loop_status(const QString& value)
{
    if (value == QLatin1String("Playlist"))
    {
        emit controls->repeatRequested(QStringLiteral("all"));
    }
    else if (value == QLatin1String("Track"))
    {
        emit controls->repeatRequested(QStringLiteral("one"));
    }
    else if (value == QLatin1String("None"))
    {
        emit controls->repeatRequested(QStringLiteral("off"));
    }
}

double PlayerAdaptor::rate() const
{
    return 1.0;
}

QVariantMap PlayerAdaptor::metadata() const
{
    return controls->metadata();
}

double PlayerAdaptor::volume() const
{
    return controls->volume();
}

void PlayerAdaptor::set_volume(double value)
{
    emit controls->volumeRequested(std::clamp(value, 0.0, 1.0));
}

qlonglong PlayerAdaptor::position() const
{
    return controls->position_ms() * US_PER_MS;
}

bool PlayerAdaptor::can_go_next() const
{
    return controls->can_next();
}

bool PlayerAdaptor::can_go_previous() const
{
    // previous restarts the song when there's nothing before it
    return controls->can_previous() || controls->has_song();
}

bool PlayerAdaptor::can_play() const
{
    return controls->has_song();
}

bool PlayerAdaptor::can_pause() const
{
    return controls->has_song();
}

bool PlayerAdaptor::can_seek() const
{
    return controls->has_song();
}

bool PlayerAdaptor::can_control() const
{
    return true;
}

void PlayerAdaptor::Next()
{
    emit controls->nextRequested();
}

void PlayerAdaptor::Previous()
{
    emit controls->previousRequested();
}

void PlayerAdaptor::Pause()
{
    emit controls->pauseRequested();
}

void PlayerAdaptor::PlayPause()
{
    emit controls->playPauseRequested();
}

void PlayerAdaptor::Stop()
{
    emit controls->stopRequested();
}

void PlayerAdaptor::Play()
{
    emit controls->playRequested();
}

void PlayerAdaptor::Seek(qlonglong Offset)
{
    if (!controls->has_song())
    {
        return;
    }

    const qint64 target = controls->position_ms() + Offset / US_PER_MS;
    // the spec: past the end acts like next, before the start goes to the start
    if (controls->duration_ms() > 0 && target >= controls->duration_ms())
    {
        emit controls->nextRequested();
        return;
    }
    emit controls->seekRequested(std::max<qint64>(0, target));
}

void PlayerAdaptor::SetPosition(const QDBusObjectPath& TrackId, qlonglong Position)
{
    // a stale request for the song before is ignored, as the spec asks
    if (TrackId != controls->track_path() || Position < 0)
    {
        return;
    }

    const qint64 target = Position / US_PER_MS;
    if (controls->duration_ms() > 0 && target > controls->duration_ms())
    {
        return;
    }
    emit controls->seekRequested(target);
}

void PlayerAdaptor::OpenUri(const QString& Uri)
{
    // SupportedUriSchemes is empty, so nobody should ask
    Q_UNUSED(Uri);
}
