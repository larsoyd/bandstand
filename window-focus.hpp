#pragma once

#include <QByteArray>
#include <QString>

// the id of this process's mapped window in a window-rules/list-views reply, -1 if it has none
qint64 own_view_id(const QByteArray& views, qint64 pid);

// wayfire's session has no xdg activation, so a window can only come forward over its ipc socket
bool focus_own_window(const QString& socket_path);
