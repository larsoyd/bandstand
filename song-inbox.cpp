#include "song-inbox.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include "local-songs.hpp"

static constexpr int HAND_OVER_MS = 2000;
static constexpr int LOCK_POLL_MS = 20;

// a blank line ends the names, the listener answers once it has them
static const QByteArray END_OF_NAMES = QByteArrayLiteral("\n\n");

StartLock::StartLock(const QString& path, int wait_ms)
{
    // flock, so that a program that dies lets go of it
    descriptor = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor < 0)
    {
        qWarning("[bandstand] start lock failed path=%s reason=%s", qPrintable(path), std::strerror(errno));
        return;
    }

    QElapsedTimer waited;
    waited.start();
    while (!is_held && waited.elapsed() < wait_ms)
    {
        is_held = ::flock(descriptor, LOCK_EX | LOCK_NB) == 0;
        if (!is_held)
        {
            QThread::msleep(LOCK_POLL_MS);
        }
    }
    if (!is_held)
    {
        qWarning("[bandstand] start lock busy path=%s waited_ms=%d", qPrintable(path), wait_ms);
    }
}

StartLock::~StartLock()
{
    release();
}

bool StartLock::held() const
{
    return is_held;
}

void StartLock::release()
{
    if (descriptor >= 0)
    {
        ::close(descriptor);
    }
    descriptor = -1;
    is_held = false;
}

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

    socket.write(names.join(QLatin1Char('\n')).toUtf8() + END_OF_NAMES);
    return socket.waitForBytesWritten(HAND_OVER_MS) && socket.waitForReadyRead(HAND_OVER_MS);
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
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read_names(socket); });
        read_names(socket);
    }
}

void SongInbox::read_names(QLocalSocket *socket)
{
    if (!socket->peek(socket->bytesAvailable()).endsWith(END_OF_NAMES))
    {
        return;
    }

    const QString names = QString::fromUtf8(socket->readAll());
    socket->write("ok\n");
    socket->flush();
    const QVariantList songs = local_songs(names.split(QLatin1Char('\n')), QDir::homePath());
    if (!songs.isEmpty())
    {
        emit songsOpened(songs);
    }
}
