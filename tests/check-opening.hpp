#pragma once

#include <QObject>

class CheckOpening : public QObject
{
    Q_OBJECT

  private slots:
    void fileUrlBecomesSong();
    void pathBecomesSong();
    void relativePathStartsAtFolder();
    void spacesAndUnicodeSurvive();
    void orderIsKept();
    void missingFolderAndRemoteAreLeftOut();
    void songUrlIsTheFileOrItsPage();

    void handOverWithNobodyListening();
    void handOverReachesListener();
    void handOverKeepsEverySongInOrder();
    void handOverDropsMissingFiles();
    void handOverNeedsAnAnswer();
    void secondListenerIsRefused();
    void startLockKeepsOthersOut();
    void startLockGoesWithItsHolder();
    void startLockFailsLoudlyOnBadPath();
    void staleSocketIsReplaced();

    void tagsReplaceTheFileName_data();
    void tagsReplaceTheFileName();
    void fileWithoutTagsKeepsItsName();
    void embeddedCoverBecomesAFile_data();
    void embeddedCoverBecomesAFile();
    void folderCoverIsTheFallback();
    void fileNamedLikeAudioButIsNotIsStillListed();

    void ownViewIsFoundInTheList();
    void focusAsksForTheOwnView();
    void focusWithoutCompositorIsARefusal();
};
