#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// lets a second start of the program pass its files to the one already running
class SongInbox : public QObject
{
    Q_OBJECT

  public:
    using QObject::QObject;

    static QString default_path();
    // true when a running program took the names
    static bool hand_over(const QString& path, const QStringList& names);
    // false when another program holds the path
    bool listen(const QString& path);

  signals:
    void songsOpened(const QVariantList& songs);

  private:
    void accept_connections();

    QLocalServer server;
};
