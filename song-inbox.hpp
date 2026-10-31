#pragma once

#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// held from the start of the program until its window listens, so that of
// several starts at once one opens the window and the others hand over
class StartLock
{
  public:
    explicit StartLock(const QString& path, int wait_ms = 10000);
    ~StartLock();
    StartLock(const StartLock&) = delete;
    StartLock& operator=(const StartLock&) = delete;

    bool held() const;
    void release();

  private:
    int descriptor = -1;
    bool is_held = false;
};

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
    void read_names(QLocalSocket *socket);

    QLocalServer server;
};
