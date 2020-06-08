#include <gtest/gtest.h>
#include <chainparams.h>
#include <coins.h>
#include "tx_creation_utils.h"
#include <main.h>
#include <undo.h>
#include <consensus/validation.h>

class CeasedSidechainsTestSuite: public ::testing::Test {

public:
    CeasedSidechainsTestSuite():
        dummyBackingView(nullptr)
        , view(nullptr) {};

    ~CeasedSidechainsTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        dummyBackingView = new CCoinsView();
        view = new CCoinsViewCache(dummyBackingView);
    };

    void TearDown() override {
        delete view;
        view = nullptr;

        delete dummyBackingView;
        dummyBackingView = nullptr;
    };

protected:
    CCoinsView        *dummyBackingView;
    CCoinsViewCache   *view;
};


///////////////////////////////////////////////////////////////////////////////
/////////////////////////// isSidechainCeased /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, UnknownSidechainIsNeitherAliveNorCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    ASSERT_FALSE(view->HaveSidechain(scId));

    CSidechain::State state = view->isCeasedAtHeight(scId, creationHeight);
    EXPECT_TRUE(state == CSidechain::State::NOT_APPLICABLE)
        <<"sc is in state "<<int(state);
}

TEST_F(CeasedSidechainsTestSuite, SidechainInItsFirstEpochIsNotCeased) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1912;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*height*/10);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int endEpochHeight = scInfo.StartHeightForEpoch(currentEpoch+1)-1;

    for(int height = creationHeight; height <= endEpochHeight; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, SidechainIsNotCeasedBeforeNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1945;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10), /*epochLength*/11);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);

    for(int height = nextEpochStart; height <= nextEpochStart + scInfo.SafeguardMargin(); ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, SidechainIsCeasedAftereNextEpochSafeguard) {
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10),/*epochLength*/100);
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochEnd = scInfo.StartHeightForEpoch(currentEpoch+2)-1;

    for(int height = nextEpochStart + scInfo.SafeguardMargin()+1; height <= nextEpochEnd; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::CEASED)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, FullCertMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/2, /*bwtAmount*/CAmount(0), /*numBwt*/2);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, PureBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/10, /*bwtAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite, NoBwtCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/1, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}

TEST_F(CeasedSidechainsTestSuite,EmptyCertificateMovesSidechainTerminationToNextEpochSafeguard) {
    //Create Sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 1968;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);

    //Prove it would expire without certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int nextEpochStart = scInfo.StartHeightForEpoch(currentEpoch+1);
    int nextEpochSafeguard = nextEpochStart + scInfo.SafeguardMargin();

    CSidechain::State state = view->isCeasedAtHeight(scId, nextEpochSafeguard+1);
    ASSERT_TRUE(state == CSidechain::State::CEASED)
        <<"sc is in state "<<int(state)<<" at height "<<nextEpochSafeguard+1;

    //Prove that certificate reception keeps Sc alive for another epoch
    CBlock CertBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, CertBlock.GetHash(), /*numChangeOut*/0, /*bwtAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo blockUndo;
    view->UpdateScInfo(cert, blockUndo);

    int certReceptionHeight = nextEpochSafeguard-1;
    for(int height = certReceptionHeight; height < certReceptionHeight +scInfo.creationData.withdrawalEpochLength; ++height) {
        CSidechain::State state = view->isCeasedAtHeight(scId, height);
        EXPECT_TRUE(state == CSidechain::State::ALIVE)
            <<"sc is in state "<<int(state)<<" at height "<<height;
    }
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////// Ceasing Sidechain updates /////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForScCreation) {
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight));

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UpdateCeasingScs(scCreationOut));

    //Checks
    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int ceasingHeight = scInfo.StartHeightForEpoch(1)+scInfo.SafeguardMargin()+1;
    CCeasingSidechains ceasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForFullCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/2,/*bwtAmount*/CAmount(0));

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForPureBwtCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/4);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForNoBwtCert) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/3,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, CeasingHeightUpdateForEmptyCertificate) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    EXPECT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);

    uint256 epochZeroEndBlockHash = uint256S("aaa");
    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, epochZeroEndBlockHash,/*numChangeOut*/0,/*bwtAmount*/CAmount(0), /*numBwt*/0);

    CBlockUndo dummyUndo;
    EXPECT_TRUE(view->UpdateScInfo(cert, dummyUndo));

    //test
    view->UpdateCeasingScs(cert);

    //Checks
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    EXPECT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////////// HandleCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, FullCertCoinsHaveBwtStrippedOutWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    CCoins updatedCoin;
    unsigned int changeCounter = 0;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    for (const CTxOut& out: updatedCoin.vout) {//outputs in coin are changes
        EXPECT_TRUE(out.isFromBackwardTransfer == false);
        ++changeCounter;
    }

    unsigned int bwtCounter = 0;
    ASSERT_TRUE(coinsBlockUndo.vVoidedCertUndo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE(out == coinsBlockUndo.vVoidedCertUndo[0].voidedOuts[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == changeCounter+bwtCounter); //all cert outputs are handled
}

TEST_F(CeasedSidechainsTestSuite, PureBwtCoinsAreRemovedWhenSidechainCeases) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    unsigned int bwtCounter = 0;
    ASSERT_TRUE(coinsBlockUndo.vVoidedCertUndo.size() == 1);
    for(const CTxOut& out: cert.GetVout()) { //outputs in blockUndo are bwt
        if (out.isFromBackwardTransfer) {
            EXPECT_TRUE( (coinsBlockUndo.vVoidedCertUndo[0].voidedOuts[bwtCounter].nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))
                         <<coinsBlockUndo.vVoidedCertUndo[0].voidedOuts[bwtCounter].nVersion;
            EXPECT_TRUE(coinsBlockUndo.vVoidedCertUndo[0].voidedOuts[bwtCounter].nBwtMaturityHeight == coinFromCert.nBwtMaturityHeight);
            EXPECT_TRUE(out == coinsBlockUndo.vVoidedCertUndo[0].voidedOuts[bwtCounter].txout);
            ++bwtCounter;
        }
    }

    EXPECT_TRUE(cert.GetVout().size() == bwtCounter); //all cert outputs are handled
}

TEST_F(CeasedSidechainsTestSuite, NoBwtCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight);
    EXPECT_TRUE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    CCoins updatedCoin;
    unsigned int changeCounter = 0;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),updatedCoin));
    for (const CTxOut& out: updatedCoin.vout) {//outputs in coin are changes
        EXPECT_TRUE(out.isFromBackwardTransfer == false);
        ++changeCounter;
    }

    unsigned int bwtCounter = 0;
    EXPECT_TRUE(coinsBlockUndo.vVoidedCertUndo.size() == 0);
    EXPECT_TRUE(cert.GetVout().size() == changeCounter+bwtCounter); //all cert outputs are handled
}

TEST_F(CeasedSidechainsTestSuite, EmptyCertificatesCoinsAreNotAffectedByCeasedSidechainHandling) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1987;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scCreationHeight);
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);
    CBlockUndo coinsBlockUndo;
    EXPECT_TRUE(view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo));

    //Checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
///////////////////////////////////////////////////////////////////////////////
////////////////////////////// RevertCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, RestoreFullCertCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/2, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);

    //test
    for (const CVoidedCertUndo& voidCertUndo: coinsBlockUndo.vVoidedCertUndo)
        view->RevertCeasingScs(voidCertUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
}

TEST_F(CeasedSidechainsTestSuite, RestorePureBwtCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);
    ASSERT_FALSE(view->HaveCoins(cert.GetHash()));

    //test
    for (const CVoidedCertUndo& voidCertUndo: coinsBlockUndo.vVoidedCertUndo)
        view->RevertCeasingScs(voidCertUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
}

TEST_F(CeasedSidechainsTestSuite, RestoreNoBwtCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scInfo.StartHeightForEpoch(1));
    CCoins originalCoins;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),originalCoins));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);

    //test
    for (const CVoidedCertUndo& voidCertUndo: coinsBlockUndo.vVoidedCertUndo)
        view->RevertCeasingScs(voidCertUndo);

    //checks
    CCoins rebuiltCoin;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),rebuiltCoin));
    EXPECT_TRUE(rebuiltCoin.nHeight            == originalCoins.nHeight);
    EXPECT_TRUE((rebuiltCoin.nVersion & 0x7f)  == (originalCoins.nVersion& 0x7f));
    EXPECT_TRUE(rebuiltCoin.nBwtMaturityHeight == originalCoins.nBwtMaturityHeight);
    EXPECT_TRUE(rebuiltCoin.vout.size()        == originalCoins.vout.size());
    for (unsigned int pos = 0; pos < cert.GetVout().size(); ++pos) {
        EXPECT_TRUE(rebuiltCoin.vout[pos] == originalCoins.vout[pos]);
    }
}

TEST_F(CeasedSidechainsTestSuite, RestoreEmptyCertCeasedCoins) {
    //Create sidechain
    uint256 scId = uint256S("aaa");
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock scCreationBlock;
    view->UpdateScInfo(scCreationTx, scCreationBlock, /*height*/1789);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);

    //Generate certificate
    CSidechain scInfo;
    view->GetSidechain(scId, scInfo);
    int certReferencedEpoch = 0;
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, certReferencedEpoch, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo certBlockUndo;
    view->UpdateScInfo(cert, certBlockUndo);
    view->UpdateCeasingScs(cert);

    //Generate coin from certificate
    CValidationState state;
    CTxUndo txundo;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, txundo, scInfo.StartHeightForEpoch(1));
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));

    //Make the sidechain cease
    int minimalCeaseHeight = scInfo.StartHeightForEpoch(certReferencedEpoch+2)+scInfo.SafeguardMargin()+1;
    EXPECT_TRUE(view->isCeasedAtHeight(scId, minimalCeaseHeight) == CSidechain::State::CEASED);

    // Null the coins
    CBlockUndo coinsBlockUndo;
    view->HandleCeasingScs(minimalCeaseHeight, coinsBlockUndo);

    //test
    for (const CVoidedCertUndo& voidCertUndo: coinsBlockUndo.vVoidedCertUndo)
        view->RevertCeasingScs(voidCertUndo);

    //checks
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// UndoCeasingScs ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, UndoCeasingScs) {
    uint256 scId = uint256S("aaa");
    int scCreationHeight = 1492;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    ASSERT_TRUE(view->UpdateScInfo(scCreationTx, creationBlock, scCreationHeight));

    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UpdateCeasingScs(scCreationOut));

    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int ceasingHeight = scInfo.StartHeightForEpoch(1)+scInfo.SafeguardMargin()+1;
    CCeasingSidechains ceasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(ceasingHeight, ceasingScIds));
    EXPECT_TRUE(ceasingScIds.ceasingScs.count(scId) != 0);

    //test
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        EXPECT_TRUE(view->UndoCeasingScs(scCreationOut));

    //checks
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_FALSE(view->HaveCeasingScs(ceasingHeight));
}

TEST_F(CeasedSidechainsTestSuite, UndoFullCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoPureBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/3);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoNoBwtCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/4, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}

TEST_F(CeasedSidechainsTestSuite, UndoEmptyCertUpdatesToCeasingScs) {
    //Create and register sidechain
    uint256 scId = uint256S("aaa");
    int creationHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock creationBlock;
    view->UpdateScInfo(scCreationTx, creationBlock, creationHeight);
    for(const CTxScCreationOut& scCreationOut: scCreationTx.GetVscCcOut())
        view->UpdateCeasingScs(scCreationOut);


    CSidechain scInfo;
    ASSERT_TRUE(view->GetSidechain(scId, scInfo));
    int currentEpoch = scInfo.EpochFor(creationHeight);
    int initialCeasingHeight = scInfo.StartHeightForEpoch(currentEpoch+1)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains initialCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(initialCeasingHeight, initialCeasingScIds));
    ASSERT_TRUE(initialCeasingScIds.ceasingScs.count(scId) != 0);


    CScCertificate cert = txCreationUtils::createCertificate(scId, currentEpoch, uint256S("aaa"), /*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);
    CBlockUndo dummyUndo;
    view->UpdateScInfo(cert, dummyUndo);
    view->UpdateCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);
    int newCeasingHeight = scInfo.StartHeightForEpoch(cert.epochNumber+2)+scInfo.SafeguardMargin() +1;
    CCeasingSidechains updatedCeasingScIds;
    ASSERT_TRUE(view->GetCeasingScs(newCeasingHeight, updatedCeasingScIds));
    ASSERT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
    ASSERT_TRUE(!view->HaveCeasingScs(initialCeasingHeight));

    //test
    view->UndoCeasingScs(cert);

    //Checks
    view->GetSidechain(scId, scInfo);

    EXPECT_FALSE(view->HaveCeasingScs(newCeasingHeight));
    CCeasingSidechains restoredCeasingScIds;
    EXPECT_TRUE(view->GetCeasingScs(initialCeasingHeight,restoredCeasingScIds));
    EXPECT_TRUE(updatedCeasingScIds.ceasingScs.count(scId) != 0);
}
///////////////////////////////////////////////////////////////////////////////
//////////////////////////////// ApplyTxInUndo ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(CeasedSidechainsTestSuite, SpendChangeOutput_CoinReconstructionFromBlockUndo)
{
    //Create sidechain
    uint256 scId = uint256S("aaa");
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateScInfo(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(CeasedSidechainsTestSuite, SpendBwtOutput_CoinReconstructionFromBlockUndo)
{
    //Create sidechain
    uint256 scId = uint256S("aaa");
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateScInfo(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending only the bwt from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 1;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(CeasedSidechainsTestSuite, SpendFullCoinsByChangeOutput_CoinReconstructionFromBlockUndo)
{
    //Create sidechain
    uint256 scId = uint256S("aaa");
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateScInfo(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/1, /*bwtTotalAmount*/CAmount(0), /*numBwt*/0);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the change output (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}

TEST_F(CeasedSidechainsTestSuite, SpendFullCoinsByBwt_CoinReconstructionFromBlockUndo)
{
    //Create sidechain
    uint256 scId = uint256S("aaa");
    static const int dummyHeight = 100;
    CTransaction scCreationTx = txCreationUtils::createNewSidechainTxWith(scId, CAmount(10));
    CBlock dummyCreationBlock;
    EXPECT_TRUE(view->UpdateScInfo(scCreationTx, dummyCreationBlock, dummyHeight));

    //Generate certificate
    CBlock endEpochBlock;
    CScCertificate cert = txCreationUtils::createCertificate(scId, /*epochNumber*/0, endEpochBlock.GetHash(),/*numChangeOut*/0, /*bwtTotalAmount*/CAmount(0), /*numBwt*/1);

    //Generate coin from cert, to check it is fully reconstructed from BlockUndo
    CTxUndo dummyTxUndo;
    static const int certHeight = 1987;
    EXPECT_FALSE(view->HaveCoins(cert.GetHash()));
    UpdateCoins(cert, *view, dummyTxUndo, certHeight);
    CCoins coinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),coinFromCert));

    //Create Tx spending the bwt (the only output) from the certificate
    CMutableTransaction txSpendingCert;
    txSpendingCert.vin.resize(1);
    txSpendingCert.vin.at(0).prevout.hash = cert.GetHash();
    txSpendingCert.vin.at(0).prevout.n = 0;

    //Create block undo to rebuild cert output
    CTxUndo certTxUndo;
    static const int spendTxHeight = 2020;
    UpdateCoins(txSpendingCert, *view, certTxUndo, spendTxHeight);

    //Test
    for (unsigned int inPos = txSpendingCert.vin.size(); inPos-- > 0;)
    {
        const COutPoint &out = txSpendingCert.vin[inPos].prevout;
        const CTxInUndo &undo = certTxUndo.vprevout[inPos];
        EXPECT_TRUE(ApplyTxInUndo(undo, *view, out));
    }

    //Checks
    CCoins reconstructedCoinFromCert;
    EXPECT_TRUE(view->GetCoins(cert.GetHash(),reconstructedCoinFromCert));
    EXPECT_TRUE(coinFromCert == reconstructedCoinFromCert);
}