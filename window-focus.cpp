#include "window-focus.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtEndian>

static constexpr int SOCKET_MS = 500;

static bool read_exactly(QLocalSocket& socket, qint64 size, QByteArray& bytes)
{
    while (socket.bytesAvailable() < size)
    {
        if (!socket.waitForReadyRead(SOCKET_MS))
        {
            return false;
        }
    }
    bytes = socket.read(size);
    return true;
}

// a message is a 4 byte little endian length and then json
static bool call(QLocalSocket& socket, const QString& method, const QJsonObject& data, QByteArray& reply)
{
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("method"), method}, {QStringLiteral("data"), data}}).toJson(QJsonDocument::Compact);
    const quint32 length = qToLittleEndian<quint32>(body.size());
    socket.write(reinterpret_cast<const char *>(&length), sizeof length);
    socket.write(body);
    if (!socket.waitForBytesWritten(SOCKET_MS))
    {
        return false;
    }

    QByteArray header;
    return read_exactly(socket, sizeof length, header)
        && read_exactly(socket, qFromLittleEndian<quint32>(header.constData()), reply);
}

qint64 own_view_id(const QByteArray& views, qint64 pid)
{
    const QJsonArray list = QJsonDocument::fromJson(views).array();
    for (const QJsonValue& view : list)
    {
        if (view[QLatin1String("pid")].toInteger() == pid && view[QLatin1String("mapped")].toBool()
            && view[QLatin1String("role")].toString() == QLatin1String("toplevel"))
        {
            return view[QLatin1String("id")].toInteger();
        }
    }
    return -1;
}

bool focus_own_window(const QString& socket_path)
{
    QLocalSocket socket;
    socket.connectToServer(socket_path);
    QByteArray views;
    QByteArray answer;
    if (!socket.waitForConnected(SOCKET_MS) || !call(socket, QStringLiteral("window-rules/list-views"), {}, views))
    {
        qWarning("[bandstand] focus failed reason=no_compositor socket=%s", qPrintable(socket_path));
        return false;
    }

    const qint64 view = own_view_id(views, QCoreApplication::applicationPid());
    if (view < 0 || !call(socket, QStringLiteral("window-rules/focus-view"), {{QStringLiteral("id"), view}}, answer))
    {
        qWarning("[bandstand] focus failed reason=no_view pid=%lld", QCoreApplication::applicationPid());
        return false;
    }
    return true;
}
