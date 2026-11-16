#pragma once

#include <QObject>

class CheckQueue : public QObject
{
    Q_OBJECT

  private slots:
    void trackRoundTrip();
    void trackNeedsVideoId();
    void setTracksDropsSongsWithoutId();
    void totalSeconds();
    void fileSongHasOneIdHoweverItIsWritten_data();
    void fileSongHasOneIdHoweverItIsWritten();

    void playFromQueuesWholeList();
    void playFromBadRowDoesNothing();
    void stepStaysInBounds();
    void playNextGoesAfterCurrent();
    void playNextOnEmptyQueueBecomesCurrent();
    void addToEndKeepsCurrent();
    void removeBeforeCurrentKeepsCurrent();
    void currentSongCannotBeRemoved();
    void clearUpNextKeepsPlayedSongs();
    void jumpTo();
    void playShuffledQueuesEverySongOnce();
    void playShuffledEmptyDoesNothing();
    void placeChangedOnlyOnRealChanges();

    void playFromLeavesOutHiddenButNotThePickedSong();
    void playListLeavesOutHidden();
    void radioKeepsItsHiddenSeed();
    void playAllStartsAtFirstShownSong();
    void playShuffledLeavesOutHidden();
    void hidingDropsSongFromQueueButNotThePlayingOne();
};
