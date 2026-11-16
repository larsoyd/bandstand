#pragma once

#include <QObject>

class CheckLibrary : public QObject
{
    Q_OBJECT

  private slots:
    void trackModelEdits();
    void songWithoutIdNotAdded();
    void cardsListedOnceNewestFirst();

    void newLibraryIsEmptyAndWritesNothing();
    void createPlaylistKeepsOrderAndNamesBlank();
    void duplicateSongNotAddedToPlaylist();
    void addAllCountsOnlyNewSongs();
    void playlistFromListIsACopy();
    void playlistCountFollowsSongs();
    void renameIgnoresBlankName();
    void deletePlaylistRemovesFile();

    void likeAndUnlike();
    void historyNewestFirstNoDuplicates();
    void historyIsCapped();
    void replayingTopSongLeavesHistoryAlone();
    void saveAndUnsave();

    void removeFromHistory();
    void hiddenSongLeavesHistoryAndShelves();
    void hiddenSongStaysInLikedAndPlaylists();
    void hidingMatchesOtherVersionsOfTheSong();
    void showingAgain();

    void roundTripThroughFiles();
    void playlistOrderSurvivesReload();
    void writesAreBatchedAndFlushedOnExit();
    void corruptFileIsMovedAside();
    void unwritableFolderIsReported();
    void failedWriteIsRetried();
    void hiddenSongsSurviveReload();
    void fileIdsWrittenAnotherWayAreReadAsOne();
};
