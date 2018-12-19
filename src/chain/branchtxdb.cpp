// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "branchtxdb.h"
#include "rpc/branchchainrpc.h"
#include "transaction/txmempool.h"
#include "validation/validation.h"

namespace {
    class MineCoinEntry {
    public:
        char key;
        uint256 hash;// coin prevout tx hash
                     //uint32_t n; // n default is zero, so no need
        MineCoinEntry(const uint256& txid) :hash(txid), key(DB_MINE_COIN_LOCK) {}

        template <typename Stream>
        void Serialize(Stream& s) const
        {
            s << key;
            s << hash;
        }

        template <typename Stream>
        void Unserialize(Stream& s)
        {
            s >> key;
            s >> hash;
        }
    };
}

BranchChainTxRecordsDb* pBranchChainTxRecordsDb = nullptr;

void BranchChainTxRecordsCache::AddBranchChainTxRecord(const MCTransactionRef& tx, const uint256& blockhash, uint32_t txindex)
{
    if (!tx->IsPregnantTx() && !tx->IsBranchCreate())
        return;

    BranchChainTxEntry key(tx->GetHash(), DB_BRANCH_CHAIN_TX_DATA);
    BranchChainTxInfo& sendinfo = m_mapChainTxInfos[key];
    sendinfo.blockhash = blockhash;
    sendinfo.txindex = txindex;
    sendinfo.txnVersion = tx->nVersion;
    if (tx->IsBranchCreate()) {
        sendinfo.createchaininfo.txid = tx->GetHash();
        sendinfo.createchaininfo.branchSeedSpec6 = tx->branchSeedSpec6;
        sendinfo.createchaininfo.branchVSeeds = tx->branchVSeeds;
        sendinfo.createchaininfo.blockhash = blockhash;
    }
    sendinfo.flags = DbDataFlag::eADD;
}

void BranchChainTxRecordsCache::DelBranchChainTxRecord(const MCTransactionRef& tx)
{
    if (!tx->IsPregnantTx() && !tx->IsBranchCreate())
        return;

    BranchChainTxEntry key(tx->GetHash(), DB_BRANCH_CHAIN_TX_DATA); // may be delete one entry which not in m_mapChainTxInfos any more
    BranchChainTxInfo& sendinfo = m_mapChainTxInfos[key];
    if (tx->IsBranchCreate()) {
        sendinfo.createchaininfo.txid = tx->GetHash();
        sendinfo.createchaininfo.branchSeedSpec6 = tx->branchSeedSpec6;
        sendinfo.createchaininfo.branchVSeeds = tx->branchVSeeds;
    }
    sendinfo.flags = DbDataFlag::eDELETE;
}

void BranchChainTxRecordsCache::AddBranchChainRecvTxRecord(const MCTransactionRef& tx, const uint256& blockhash)
{
    if (tx->IsBranchChainTransStep2() == false)
        return;

    uint256 txid = mempool.GetOriTxHash(*tx);
    BranchChainTxEntry key(txid, DB_BRANCH_CHAIN_RECV_TX_DATA);
    BranchChainTxRecvInfo& data = m_mapRecvRecord[key];
    data.blockhash = blockhash;
    data.flags = DbDataFlag::eADD;
}

void BranchChainTxRecordsCache::DelBranchChainRecvTxRecord(const MCTransactionRef& tx)
{
    if (tx->IsBranchChainTransStep2() == false)
        return;

    uint256 txid = mempool.GetOriTxHash(*tx);
    BranchChainTxEntry key(txid, DB_BRANCH_CHAIN_RECV_TX_DATA);
    BranchChainTxRecvInfo& data = m_mapRecvRecord[key];
    data.flags = DbDataFlag::eDELETE;
}

void BranchChainTxRecordsCache::UpdateLockMineCoin(const MCTransactionRef& ptx, bool fBlockConnect)
{
    if (fBlockConnect){
        if (ptx->IsLockMortgageMineCoin()) { // 锁定
            std::vector<CoinReportInfo>& vec = m_mapCoinBeReport[ptx->coinpreouthash];
            for (CoinReportInfo& info : vec)
            {
                if (info.reporttxid == ptx->reporttxid){
                    info.flags = DbDataFlag::eADD;
                    return;
                }
            }
            vec.push_back(CoinReportInfo(ptx->reporttxid, DbDataFlag::eADD));
        }
        if (ptx->IsUnLockMortgageMineCoin()) { // 解锁
            std::vector<CoinReportInfo>& vec = m_mapCoinBeReport[ptx->coinpreouthash];
            for (CoinReportInfo& info : vec)
            {
                if (info.reporttxid == ptx->reporttxid) {
                    info.flags = DbDataFlag::eDELETE;
                    return;
                }
            }
            vec.push_back(CoinReportInfo(ptx->reporttxid, DbDataFlag::eDELETE));
        }
    }
    else{// block disconnect
        if (ptx->IsLockMortgageMineCoin()) {// 锁定回滚
            std::vector<CoinReportInfo>& vec = m_mapCoinBeReport[ptx->coinpreouthash];
            for (CoinReportInfo& info : vec)
            {
                if (info.reporttxid == ptx->reporttxid) {
                    info.flags = DbDataFlag::eDELETE;
                    return;
                }
            }
            vec.push_back(CoinReportInfo(ptx->reporttxid, DbDataFlag::eDELETE));
        }
        if (ptx->IsUnLockMortgageMineCoin()) {// 解锁回滚
            std::vector<CoinReportInfo>& vec = m_mapCoinBeReport[ptx->coinpreouthash];
            for (CoinReportInfo& info : vec)
            {
                if (info.reporttxid == ptx->reporttxid) {
                    info.flags = DbDataFlag::eADD;
                    return;
                }
            }
            vec.push_back(CoinReportInfo(ptx->reporttxid, DbDataFlag::eADD));
        }
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
BranchChainTxRecordsDb::BranchChainTxRecordsDb(const fs::path& path, size_t nCacheSize, bool fMemory, bool fWipe)
    : m_db(path, nCacheSize, fMemory, fWipe, true)
{
    m_db.Read(DB_BRANCH_CHAIN_LIST, m_vCreatedBranchTxs);
}

BranchChainTxInfo BranchChainTxRecordsDb::GetBranchChainTxInfo(const uint256& txid)
{
    BranchChainTxEntry key(txid, DB_BRANCH_CHAIN_TX_DATA);
    BranchChainTxInfo sendinfo;
    if (!m_db.Read(key, sendinfo)) {
        sendinfo.blockhash.SetNull();
        return sendinfo;
    }
    return sendinfo;
}

//param tx
//param pBlock which block contain tx
bool BranchChainTxRecordsDb::IsTxRecvRepeat(const MCTransaction& tx, const MCBlock* pBlock /*=nullptr*/)
{
    if (tx.IsBranchChainTransStep2() == false)
        return false;

    uint256 txid = mempool.GetOriTxHash(tx);
    BranchChainTxEntry keyentry(txid, DB_BRANCH_CHAIN_RECV_TX_DATA);
    BranchChainTxRecvInfo recvInfo;
    if (!m_db.Read(keyentry, recvInfo))
        return false;

    if (pBlock && pBlock->GetHash() == recvInfo.blockhash) {//same block is not duplicate
        return false;
    }

    return true;
}

void BranchChainTxRecordsDb::Flush(BranchChainTxRecordsCache& cache)
{
    LogPrint(BCLog::COINDB, "flush branch chain tx data to db");
    MCDBBatch batch(m_db);
    size_t batch_size = (size_t)gArgs.GetArg("-dbbatchsize", nDefaultDbBatchSize);
    bool bCreatedChainTxChanged = false;
    for (auto mit = cache.m_mapChainTxInfos.begin(); mit != cache.m_mapChainTxInfos.end(); mit++) {
        const BranchChainTxEntry& keyentry = mit->first;
        const BranchChainTxInfo& txinfo = mit->second;
        if (txinfo.flags == DbDataFlag::eADD)
            batch.Write(keyentry, txinfo);
        else if (txinfo.flags == DbDataFlag::eDELETE)
            batch.Erase(keyentry);

        if (batch.SizeEstimate() > batch_size) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db.WriteBatch(batch);
            batch.Clear();
        }
        //update create branch chain vector
        if (txinfo.txnVersion == MCTransaction::CREATE_BRANCH_VERSION) {
            if (txinfo.flags == DbDataFlag::eADD) {
                // add before then (del and add) in same case
                if (std::find(m_vCreatedBranchTxs.begin(), m_vCreatedBranchTxs.end(), txinfo.createchaininfo) == m_vCreatedBranchTxs.end()) {
                    m_vCreatedBranchTxs.push_back(txinfo.createchaininfo);
                    bCreatedChainTxChanged = true;
                }
            }
            else if (txinfo.flags == DbDataFlag::eDELETE) {
                for (CREATE_BRANCH_TX_CONTAINER::iterator it = m_vCreatedBranchTxs.begin(); it != m_vCreatedBranchTxs.end(); it++) {
                    if (it->txid == txinfo.createchaininfo.txid) {
                        m_vCreatedBranchTxs.erase(it);
                        bCreatedChainTxChanged = true;
                        break;
                    }
                }
            }
        }
    }
    cache.m_mapChainTxInfos.clear();

    for (auto mit = cache.m_mapRecvRecord.begin(); mit != cache.m_mapRecvRecord.end(); mit++) {
        const BranchChainTxEntry& keyentry = mit->first;
        const BranchChainTxRecvInfo& txinfo = mit->second;
        if (txinfo.flags == DbDataFlag::eADD)
            batch.Write(keyentry, txinfo);
        else if (txinfo.flags == DbDataFlag::eDELETE)
            batch.Erase(keyentry);

        if (batch.SizeEstimate() > batch_size) {
            LogPrint(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db.WriteBatch(batch);
            batch.Clear();
        }
    }

    if (bCreatedChainTxChanged) {
        batch.Write(DB_BRANCH_CHAIN_LIST, m_vCreatedBranchTxs);
    }

    bool ret = m_db.WriteBatch(batch);
    cache.m_mapRecvRecord.clear();

    for (auto mit = cache.m_mapCoinBeReport.begin(); mit != cache.m_mapCoinBeReport.end(); mit++)
    {
        const uint256& coinprehash = mit->first;
        const std::vector<CoinReportInfo>& vec = mit->second;
        
        MineCoinEntry key(coinprehash);
        //merge with db
        std::vector<CoinReportInfo> vecDb;
        if (m_db.Read(key, vecDb)){
            for (auto nv : vec){// cache data
                bool found = false;
                for (auto it = vecDb.begin(); it != vecDb.end(); it++) {
                    if ((*it).reporttxid == nv.reporttxid){
                        if (nv.flags == DbDataFlag::eDELETE){
                            vecDb.erase(it); 
                        }
                        else if (nv.flags == DbDataFlag::eADD) {
                            // duplicate add
                        }
                        found = true;
                        break;
                    }
                }
                if (!found){
                    if (nv.flags == DbDataFlag::eADD)
                        vecDb.push_back(nv);
                    //else if (nv.flags == DbDataFlag::eDELETE)
                    // delete not exist ??
                }
            }
        }
        // write back to db
        if (vecDb.size() > 0)
            m_db.Write(key, vecDb);
        else
            m_db.Erase(key);
    }

    LogPrint(BCLog::COINDB, "finsh flush branch tx data.");
}

bool BranchChainTxRecordsDb::IsBranchCreated(const uint256 &branchid) const
{
    for (auto v: m_vCreatedBranchTxs)
    {
        if (v.txid == branchid)
        {
            return true;
        }
    }
    return false;
}

bool BranchChainTxRecordsDb::IsMineCoinLock(const uint256& coinhash) const
{
    MineCoinEntry key(coinhash);
    //merge with db
    std::vector<CoinReportInfo> vecDb;
    if (m_db.Read(key, vecDb))
    {
        if (vecDb.size() > 0){
            return true;
        }
    }
    return false;
}