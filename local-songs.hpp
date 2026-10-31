#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>

// songs for the files named by paths or file urls, other names are left out
// a song's id is its file url, relative names start in folder
QVariantList local_songs(const QStringList& names, const QString& folder);

// where a song's id leads: its file, or its page on youtube music
QString song_url(const QString& video_id);
