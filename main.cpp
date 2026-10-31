// theme and config arrive as the json file in DESKTOP_SHELL_SETTINGS (session/make_shell_config.py)

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>

#include "library.hpp"
#include "local-songs.hpp"
#include "media-controls.hpp"
#include "music-client.hpp"
#include "play-queue.hpp"
#include "song-inbox.hpp"

static QString expanded(const QString& path)
{
    if (path == QLatin1String("~"))
    {
        return QDir::homePath();
    }

    return path.startsWith(QLatin1String("~/")) ? QDir::homePath() + path.mid(1) : path;
}

static QVariantMap read_settings()
{
    const QString path = qEnvironmentVariable("DESKTOP_SHELL_SETTINGS");
    if (path.isEmpty())
    {
        std::fprintf(stderr, "bandstand: DESKTOP_SHELL_SETTINGS is not set. "
            "Start it from a Mavernix session, "
            "which sets it.\n");
        std::exit(1);
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        std::fprintf(stderr, "bandstand: cannot read the settings file %s\n",
            qPrintable(path));
        std::exit(1);
    }

    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

// an installed or bundled copy keeps qml and helper in ../share/bandstand next to its binary;
// a build straight from the source tree uses the tree's
static QString data_directory()
{
    const QString installed = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/bandstand");
    if (QFile::exists(installed + QStringLiteral("/qml/Main.qml")))
    {
        return QDir::cleanPath(installed);
    }

    return QStringLiteral(SOURCE_DIRECTORY);
}

static void prepend_to_path_variable(const char *name, const QString& directory)
{
    if (!QFileInfo(directory).isDir())
    {
        return;
    }

    const QByteArray current = qgetenv(name);
    qputenv(name, current.isEmpty() ? directory.toUtf8() : directory.toUtf8() + ':' + current);
}

static QStringList urls_of(const QVariantList& songs)
{
    QStringList urls;
    for (const QVariant& song : songs)
    {
        urls.append(song.toMap().value(QStringLiteral("videoId")).toString());
    }
    return urls;
}

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    // the dock and panel match windows to apps by this
    QGuiApplication::setDesktopFileName(QStringLiteral("bandstand"));

    StartLock start_lock(SongInbox::default_path() + QStringLiteral(".lock"));
    const QVariantList songs_to_open = local_songs(QCoreApplication::arguments().mid(1), QDir::currentPath());
    if (!songs_to_open.isEmpty() && SongInbox::hand_over(SongInbox::default_path(), urls_of(songs_to_open)))
    {
        return 0;
    }

    const QVariantMap settings = read_settings();

    const QString data = data_directory();
    const QString qml_directory = data + QStringLiteral("/qml");
    const QString helper_directory = data + QStringLiteral("/helper");
    // a bundle (from the Mavernix App Store) brings ytmusicapi, yt-dlp and deno along
    const QString bundle_libraries = QCoreApplication::applicationDirPath() + QStringLiteral("/../lib/bandstand");
    prepend_to_path_variable("PYTHONPATH", QDir::cleanPath(bundle_libraries + QStringLiteral("/python")));
    prepend_to_path_variable("PATH", QDir::cleanPath(bundle_libraries + QStringLiteral("/bin")));

    const QVariantMap music_config = settings.value(QStringLiteral("config")).toMap()
        .value(QStringLiteral("music")).toMap();
    const QString folder = expanded(music_config.value(QStringLiteral("folder"),
        QStringLiteral("~/.local/share/bandstand")).toString());

    // the helper keeps played songs in ~/.cache/bandstand/audio unless the config says otherwise
    QStringList helper_arguments;
    if (music_config.contains(QStringLiteral("cache_folder")))
    {
        helper_arguments << QStringLiteral("--cache-dir")
                         << expanded(music_config.value(QStringLiteral("cache_folder")).toString());
    }
    if (music_config.contains(QStringLiteral("cache_limit_mb")))
    {
        helper_arguments << QStringLiteral("--cache-limit-mb")
                         << QString::number(music_config.value(QStringLiteral("cache_limit_mb")).toInt());
    }

    // local songs: config.music.local_folders, a folder or a list of them, else the music folder
    QStringList music_folders;
    const QVariant local_folders = music_config.value(QStringLiteral("local_folders"));
    if (!local_folders.isValid())
    {
        music_folders << QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    else if (local_folders.typeId() == QMetaType::QVariantList)
    {
        for (const QVariant& entry : local_folders.toList())
        {
            music_folders << expanded(entry.toString());
        }
    }
    else
    {
        music_folders << expanded(local_folders.toString());
    }
    for (const QString& music_folder : std::as_const(music_folders))
    {
        if (!music_folder.isEmpty())
        {
            helper_arguments << QStringLiteral("--music-folder") << music_folder;
        }
    }

    MusicClient client(helper_directory + QStringLiteral("/music_helper.py"), helper_arguments);
    Library library(folder);
    PlayQueue queue;
    queue.set_library(&library);
    SongInbox inbox;

    // a type rather than a context property, so qml can bind the playback state to it
    qmlRegisterType<MediaControls>("Bandstand", 1, 0, "MediaControls");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), settings);
    engine.rootContext()->setContextProperty(QStringLiteral("client"), &client);
    engine.rootContext()->setContextProperty(QStringLiteral("queue"), &queue);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("songInbox"), &inbox);
    engine.rootContext()->setContextProperty(QStringLiteral("songsToOpen"), songs_to_open);
    engine.load(qml_directory + QStringLiteral("/Main.qml"));
    if (engine.rootObjects().isEmpty())
    {
        std::fprintf(stderr, "bandstand: could not load %s/Main.qml (messages above)\n",
            qPrintable(qml_directory));
        return 1;
    }

    inbox.listen(SongInbox::default_path());
    start_lock.release();

    return application.exec();
}
