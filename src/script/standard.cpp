// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "key/pubkey.h"
#include "script/script.h"
#include "utils/util.h"
#include "utils/utilstrencodings.h"


typedef std::vector<unsigned char> valtype;

bool fAcceptDatacarrier = DEFAULT_ACCEPT_DATACARRIER;
unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

MCScriptID::MCScriptID(const MCScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
    case TX_WITNESS_V0_KEYHASH: return "witness_v0_keyhash";
    case TX_WITNESS_V0_SCRIPTHASH: return "witness_v0_scripthash";
	case TX_CREATE_BRANCH: return "create_branch";
	case TX_TRANS_BRANCH: return "trans_branch";
    case TX_SEND_BRANCH: return "send_branch";
    case TX_MINE_MORTGAGE: return "mine_mortgage";
    case TX_MORTGAGE_COIN: return "mortgage_coin";
    case TX_REDEEM_MORTGAGE: return "redeem_mortgage";
    }
    return nullptr;
}

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const MCScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
    // Templates
    static std::multimap<txnouttype, MCScript> mTemplates;
    if (mTemplates.empty())
    {
        // Standard tx, sender provides pubkey, receiver adds signature
        mTemplates.insert(std::make_pair(TX_PUBKEY, MCScript() << OP_PUBKEY << OP_CHECKSIG));

        // MagnaChain address tx, sender provides hash of pubkey, receiver provides signature and pubkey
        mTemplates.insert(std::make_pair(TX_PUBKEYHASH, MCScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        // Sender provides N pubkeys, receivers provides M signatures
        mTemplates.insert(std::make_pair(TX_MULTISIG, MCScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));

        //create branch mortgage
		mTemplates.insert(std::make_pair(TX_CREATE_BRANCH, MCScript() << OP_CREATE_BRANCH << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

		//cross chain send
        mTemplates.insert(std::make_pair(TX_TRANS_BRANCH, MCScript() << OP_RETURN << OP_TRANS_BRANCH));
        
        //send branch tx
        mTemplates.insert(std::make_pair(TX_SEND_BRANCH, MCScript() << OP_TRANS_BRANCH << OP_HASH256_DATA));

        //mine branch chain mortgage(OP_HASH256_DATA is branch id of which chain will be mined)
        //前部分是数据，后部分是pay to pubkeyhash
        mTemplates.insert(std::make_pair(TX_MINE_MORTGAGE, MCScript() << OP_MINE_BRANCH_MORTGAGE << OP_HASH256_DATA << OP_BLOCK_HIGH << OP_2DROP <<
            OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        //mortgage coin(OP_HASH256_DATA is txid of TX_MINE_MORTGAGE tx) 用来标识挖矿币来个抵押币vout所在的交易的txid, OP_BLOCK_HIGH: pre coin height
        mTemplates.insert(std::make_pair(TX_MORTGAGE_COIN, MCScript() << OP_MINE_BRANCH_COIN << OP_HASH256_DATA << OP_BLOCK_HIGH << OP_2DROP <<
            OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

        //redeeem mortgage coin 赎回抵押挖矿的币 OP_HASH256_DATA 是主链上 txid, vout nValue为0
        mTemplates.insert(std::make_pair(TX_REDEEM_MORTGAGE, MCScript() << OP_RETURN << OP_REDEEM_MORTGAGE << OP_HASH256_DATA));
    }

    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TX_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    int witnessversion;
    std::vector<unsigned char> witnessprogram;
    if (scriptPubKey.IsWitnessProgram(witnessversion, witnessprogram)) {
        if (witnessversion == 0 && witnessprogram.size() == 20) {
            typeRet = TX_WITNESS_V0_KEYHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        if (witnessversion == 0 && witnessprogram.size() == 32) {
            typeRet = TX_WITNESS_V0_SCRIPTHASH;
            vSolutionsRet.push_back(witnessprogram);
            return true;
        }
        return false;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        typeRet = TX_NULL_DATA;
        return true;
    }

    // Scan templates
    const MCScript& script1 = scriptPubKey;
    for (const std::pair<txnouttype, MCScript>& tplate : mTemplates)
    {
        const MCScript& script2 = tplate.second;
        vSolutionsRet.clear();

        opcodetype opcode1, opcode2;
        std::vector<unsigned char> vch1, vch2;

        // Compare
        MCScript::const_iterator pc1 = script1.begin();
        MCScript::const_iterator pc2 = script2.begin();
        while (true)
        {
            if (pc1 == script1.end() && pc2 == script2.end())
            {
                // Found a match
                typeRet = tplate.first;
                if (typeRet == TX_MULTISIG)
                {
                    // Additional checks for TX_MULTISIG:
                    unsigned char m = vSolutionsRet.front()[0];
                    unsigned char n = vSolutionsRet.back()[0];
                    if (m < 1 || n < 1 || m > n || vSolutionsRet.size()-2 != n)
                        return false;
                }
                return true;
            }
            if (!script1.GetOp(pc1, opcode1, vch1))
                break;
            if (!script2.GetOp(pc2, opcode2, vch2))
                break;

            // Template matching opcodes:
            if (opcode2 == OP_PUBKEYS)
            {
                while (vch1.size() >= 33 && vch1.size() <= 65)
                {
                    vSolutionsRet.push_back(vch1);
                    if (!script1.GetOp(pc1, opcode1, vch1))
                        break;
                }
                if (!script2.GetOp(pc2, opcode2, vch2))
                    break;
                // Normal situation is to fall through
                // to other if/else statements
            }

            if (opcode2 == OP_PUBKEY)
            {
                if (vch1.size() < 33 || vch1.size() > 65)
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_PUBKEYHASH)
            {
                if (vch1.size() != sizeof(uint160))
                    break;
                vSolutionsRet.push_back(vch1);
            }
            else if (opcode2 == OP_SMALLINTEGER)
            {   // Single-byte small integer pushed onto vSolutions
                if (opcode1 == OP_0 ||
                    (opcode1 >= OP_1 && opcode1 <= OP_16))
                {
                    char n = (char)MCScript::DecodeOP_N(opcode1);
                    vSolutionsRet.push_back(valtype(1, n));
                }
                else
                    break;
            }
			else if (opcode2 == OP_HASH256_DATA)
            {
                if (vch1.size() != sizeof(uint256))
                {
                    break;
                }
            }
            else if (opcode2 == OP_BLOCK_HIGH)
            {
                if (vch1.size() > 5)//
                {
                    return false;
                }
            }
            else if (opcode1 != opcode2 || vch1 != vch2)
            {
                // Others must match exactly
                break;
            }
        }
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

bool ExtractDestination(const MCScript& scriptPubKey, MCTxDestination& addressRet)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        MCPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;
    }
    else if (whichType == TX_PUBKEYHASH)
    {
        addressRet = MCKeyID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_SCRIPTHASH)
    {
        addressRet = MCScriptID(uint160(vSolutions[0]));
        return true;
    }
    else if (whichType == TX_CREATE_BRANCH || whichType == TX_MINE_MORTGAGE || whichType == TX_MORTGAGE_COIN)
    {
        addressRet = MCKeyID(uint160(vSolutions[0]));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool ExtractDestinations(const MCScript& scriptPubKey, txnouttype& typeRet, std::vector<MCTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA){
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG)
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size()-1; i++)
        {
            MCPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            MCTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;
    }
    else
    {
        nRequiredRet = 1;
        MCTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
           return false;
        addressRet.push_back(address);
    }

    return true;
}

namespace
{
    class CScriptVisitor : public boost::static_visitor<bool>
    {
    private:
        MCScript* script;
    public:
        CScriptVisitor(MCScript *scriptin) { script = scriptin; }

        bool operator()(const MCNoDestination &dest) const {
            script->clear();
            return false;
        }

        bool operator()(const MCContractID &contractID) const {
            script->clear();
            *script << OP_CONTRACT << ToByteVector(contractID);
            return true;
        }

        bool operator()(const MCKeyID &keyID) const {
            script->clear();
            *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
            return true;
        }

        bool operator()(const MCScriptID &scriptID) const {
            script->clear();
            *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
            return true;
        }
    };
} // namespace

MCScript GetScriptForDestination(const MCTxDestination& dest)
{
    MCScript script;
    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

MCScript GetScriptForRawPubKey(const MCPubKey& pubKey)
{
    return MCScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

MCScript GetScriptForMultisig(int nRequired, const std::vector<MCPubKey>& keys)
{
    MCScript script;

    script << MCScript::EncodeOP_N(nRequired);
    for (const MCPubKey& key : keys)
        script << ToByteVector(key);
    script << MCScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}

MCScript GetScriptForWitness(const MCScript& redeemscript)
{
    MCScript ret;

    txnouttype typ;
    std::vector<std::vector<unsigned char> > vSolutions;
    if (Solver(redeemscript, typ, vSolutions)) {
        if (typ == TX_PUBKEY) {
            unsigned char h160[20];
            CHash160().Write(&vSolutions[0][0], vSolutions[0].size()).Finalize(h160);
            ret << OP_0 << std::vector<unsigned char>(&h160[0], &h160[20]);
            return ret;
        } else if (typ == TX_PUBKEYHASH) {
           ret << OP_0 << vSolutions[0];
           return ret;
        }
    }
    uint256 hash;
    CSHA256().Write(&redeemscript[0], redeemscript.size()).Finalize(hash.begin());
    ret << OP_0 << ToByteVector(hash);
    return ret;
}

class CoinCacheVisitor : public boost::static_visitor<bool>
{
private:
    uint160 & key;

public:
    CoinCacheVisitor(uint160& cache) : key(cache) {}

    bool operator()(const MCContractID& id) const {
        key = id;
        return true;
    }
    bool operator()(const MCKeyID& id) const {
        key = id;
        return true;
    }
    bool operator()(const MCScriptID& id) const { return false; }
    bool operator()(const MCNoDestination& no) const { return false; }
};

uint160 GetUint160(const MCTxDestination& dest)
{
    uint160 key;
    boost::apply_visitor(CoinCacheVisitor(key), dest);
    return key;
}
