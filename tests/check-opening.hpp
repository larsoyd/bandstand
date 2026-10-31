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
};
