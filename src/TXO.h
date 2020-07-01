//
// Fulcrum - A fast & nimble SPV Server for Bitcoin Cash
// Copyright (C) 2019-2020  Calin A. Culianu <calin.culianu@gmail.com>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program (see LICENSE.txt).  If not, see
// <https://www.gnu.org/licenses/>.
//
#pragma once

#include "BlockProcTypes.h"
#include "TXO_Compact.h"

#include "robin_hood/robin_hood.h"

#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstring> // for std::memcpy
#include <functional> // for std::hash
#include <optional>

/// A transaction output; A txHash:outN pair.
struct TXO {
    TxHash txHash;
    IONum  outN = 0;

    bool isValid() const { return txHash.length() == HashLen;  }
    QString toString() const { return isValid() ? QStringLiteral("%1:%2").arg(QString(txHash.toHex())).arg(outN) : QStringLiteral("<txo_invalid>"); }

    bool operator==(const TXO &o) const noexcept { return txHash == o.txHash && outN == o.outN; }
    bool operator<(const TXO &o) const noexcept { return txHash < o.txHash && outN < o.outN; }


    // serialization/deserialization
    QByteArray toBytes() const noexcept {
        QByteArray ret;
        if (!isValid()) return ret;
        const int hlen = txHash.length();
        ret.resize(int(serSize()));
        std::memcpy(ret.data(), txHash.data(), size_t(hlen));
        std::memcpy(ret.data() + hlen, &outN, sizeof(outN));
        return ret;
    }

    static TXO fromBytes(const QByteArray &ba) noexcept {
        TXO ret;
        if (ba.length() != int(serSize())) return ret;
        ret.txHash = QByteArray(ba.data(), HashLen);
        ret.outN = *reinterpret_cast<const IONum *>(ba.data()+HashLen);
        return ret;
    }

    static constexpr size_t serSize() noexcept { return HashLen + sizeof(IONum); }
};

namespace std {
/// specialization of std::hash to be able to add struct TXO to any unordered_set or unordered_map as a key
template<> struct hash<TXO> {
    size_t operator()(const TXO &txo) const noexcept {
        union {
            uint64_t u64;
            uint8_t  u8[8];
        } u;
        static_assert(sizeof(u) == sizeof(u.u64) && sizeof(u.u64) == sizeof(u.u8), "Unexpected union packing.");
        static_assert(sizeof(txo.outN) <= 4);
        constexpr int hash2copy = sizeof(u.u8) - sizeof(txo.outN);
        static_assert(hash2copy > 0);
        u.u64 = 0; // clear
        // take up to first hash2copy bytes (6 bytes) of hash
        std::memcpy(u.u8, txo.txHash.constData(), size_t(std::min(txo.txHash.size(), hash2copy)));
        // append 2-byte IONum
        std::memcpy(u.u8 + hash2copy, &txo.outN, sizeof(txo.outN));
        // hash the concatenation using std C++ (murmur2 hash)
        return hasher64(u.u64);
    }
private:
    std::hash<uint64_t> hasher64;
};
}

/// Spend info for a txo. Amount, scripthash, txNum, and possibly confirmedHeight
struct TXOInfo {
    bitcoin::Amount amount;
    HashX hashX; ///< the scripthash this output is sent to.  Note in most cases this can be compactified to be a shallow-copy of existing data (such that dupes point to the same underlying data in eg UTXOSet).
    std::optional<unsigned> confirmedHeight; ///< if unset, is mempool tx
    TxNum txNum = 0; ///< the globally mapped txNum (one for each TxHash). This is used to be able to delete the CompactTXO from the hashX's scripthash_unspent table

    bool isValid() const { return amount / bitcoin::Amount::satoshi() >= 0 && hashX.length() == HashLen; }

    /// for debug, etc
    bool operator==(const TXOInfo &o) const
        { return amount == o.amount && hashX == o.hashX && confirmedHeight == o.confirmedHeight && txNum == o.txNum; }

    QByteArray toBytes() const noexcept {
        QByteArray ret;
        if (!isValid()) return ret;
        const auto amt_sats = amount / bitcoin::Amount::satoshi();
        const int cheight = confirmedHeight.has_value() ? int(*confirmedHeight) : -1;
        ret.resize(int(serSize()));
        char *cur = ret.data();
        std::memcpy(cur, &amt_sats, sizeof(amt_sats));
        cur += sizeof(amt_sats);
        std::memcpy(cur, &cheight, sizeof(cheight));
        cur += sizeof(cheight);
        CompactTXO::txNumToCompactBytes(reinterpret_cast<uint8_t *>(cur), txNum);
        cur += CompactTXO::compactTxNumSize();
        std::memcpy(cur, hashX.constData(), size_t(hashX.length()));
        return ret;
    }
    static TXOInfo fromBytes(const QByteArray &ba) {
        TXOInfo ret;
        if (size_t(ba.length()) != serSize()) {
            return ret;
        }
        int64_t amt;
        int cheight;
        const char *cur = ba.constData();
        std::memcpy(&amt, cur, sizeof(amt));
        cur += sizeof(amt);
        std::memcpy(&cheight, cur, sizeof(cheight));
        cur += sizeof(cheight);
        ret.txNum = CompactTXO::txNumFromCompactBytes(reinterpret_cast<const uint8_t *>(cur));
        cur += CompactTXO::compactTxNumSize();
        ret.hashX = QByteArray(cur, HashLen);
        ret.amount = amt * bitcoin::Amount::satoshi();
        if (cheight > -1)
            ret.confirmedHeight.emplace(unsigned(cheight));
        return ret;
    }

    static constexpr size_t serSize() noexcept { return sizeof(int64_t) + sizeof(int) + CompactTXO::compactTxNumSize() + HashLen; }
};

