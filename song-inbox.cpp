#include "song-inbox.hpp"

#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>

#include <cstdio>

#include "local-songs.hpp"

static constexpr int HAND_OVER_MS = 1000;

QString SongInbox::default_path()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/bandstand-inbox");
}

bool SongInbox::hand_over(const QString& path, const QStringList& names)
{
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(HAND_OVER_MS))
    {
        return false;
    }

    socket.write(names.join(QLatin1Char('\n')).toUtf8());
    if (!socket.waitForBytesWritten(HAND_OVER_MS))
    {
        return false;
    }
    socket.disconnectFromServer();
    return socket.state() == QLocalSocket::UnconnectedState || socket.waitForDisconnected(HAND_OVER_MS);
}

bool SongInbox::listen(const QString& path)
{
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(HAND_OVER_MS))
    {
        return false;
    }

    // this also replaces the socket a crashed program left behind
    server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server, &QLocalServer::newConnection, this, &SongInbox::accept_connections);
    const bool listening = server.listen(path);
    if (!listening)
    {
        std::fprintf(stderr, "[bandstand] inbox failed path=%s reason=%s\n", qPrintable(path),
            qPrintable(server.errorString()));
    }
    return listening;
}

void SongInbox::accept_connections()
{
    while (QLocalSocket *socket = server.nextPendingConnection())
    {
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            const QString names = QString::fromUtf8(socket->readAll());
            socket->deleteLater();
            const QVariantList songs = local_songs(names.split(QLatin1Char('\n')), QDir::homePath());
            if (!songs.isEmpty())
            {
                emit songsOpened(songs);
            }
        });
    }
}
