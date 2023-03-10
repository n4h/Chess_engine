/*
Copyright 2022-2023, Narbeh Mouradian

Captain is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Captain is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.

*/

#include <intrin.h>
#include <algorithm>
#include <chrono>

#pragma intrinsic(_BitScanForward64)

#include "eval.hpp"
#include "board.hpp"
#include "movegen.hpp"
#include "auxiliary.hpp"
#include "constants.hpp"

namespace eval
{
    using namespace aux;

    Eval computeMaterialValue(board::Bitboard bb, const std::array<Eval, 64>& PSQT)
    {
        Eval mval = 0;

        unsigned long index;

        while (_BitScanForward64(&index, bb))
        {
            bb ^= setbit(index);

            mval += PSQT[index];
        }
        return mval;
    }

    std::uint32_t getLVA(const board::QBB& b, board::Bitboard attackers, board::Bitboard& least)
    {
        // TODO rank promoting pawns higher
        if (attackers & b.getPawns())
        {
            least = _blsi_u64(attackers & b.getPawns());
            return constants::pawnCode;
        }
        if (attackers & b.getKnights())
        {
            least = _blsi_u64(attackers & b.getKnights());
            return constants::knightCode;
        }
        if (attackers & b.getBishops())
        {
            least = _blsi_u64(attackers & b.getBishops());
            return constants::bishopCode;
        }
        if (attackers & b.getRooks())
        {
            least = _blsi_u64(attackers & b.getRooks());
            return constants::rookCode;
        }
        if (attackers & b.getQueens())
        {
            least = _blsi_u64(attackers & b.getQueens());
            return constants::queenCode;
        }
        if (attackers & b.getKings())
        {
            least = _blsi_u64(attackers & b.getKings());
            return constants::kingCode;
        }
        least = 0;
        return 0;
    }

    Eval getCaptureValue(const board::QBB& b, board::Move m)
    {
        Eval values[6] = { 100, 300, 300, 500, 900, 10000 };
        if (board::getMoveInfo<constants::moveTypeMask>(m) == constants::enPCap)
        {
            return 100;
        }
        else
        {
            return values[(b.getPieceType(board::getMoveToSq(m)) >> 1) - 1];
        }
    }

    Eval mvvlva(const board::QBB& b, board::Move m)
    {
        Eval values[6] = {100, 300, 300, 500, 900, 10000}; //PNBRQK
        board::square from = board::getMoveFromSq(m);
        board::square to = board::getMoveToSq(m);
        if (board::getMoveInfo<constants::moveTypeMask>(m) == constants::enPCap)
            return 0;
        return values[(b.getPieceType(to) >> 1) - 1] - values[(b.getPieceType(from) >> 1) - 1];
    }

    // adapted from iterative SEE
    // https://www.chessprogramming.org/SEE_-_The_Swap_Algorithm
    Eval see(const board::QBB& b, board::Move m)
    {
        const board::square target = board::getMoveToSq(m);
        auto targettype = (b.getPieceType(target) >> 1) - 1;
        const auto movetype = board::getMoveInfo<constants::moveTypeMask>(m);
        board::Bitboard attackers = movegen::getSqAttackers(b, target);
        board::Bitboard attacker = aux::setbit(board::getMoveInfo<board::fromMask>(m));
        auto attackertype = b.getPieceType(board::getMoveFromSq(m)) >> 1;

        board::Bitboard occ = b.getOccupancy();
        board::Bitboard orth = b.getOrthSliders();
        board::Bitboard diag = b.getDiagSliders();
        board::Bitboard side = b.side;

        std::array<Eval, 6> pieceval = {100, 300, 300, 500, 900, 10000};
        std::array<Eval, 32> scores;
        scores[0] = movetype == constants::enPCap ? pieceval[0] : pieceval[targettype];
        targettype = attackertype - 1;
        attackers ^= attacker;
        occ ^= attacker;
        diag &= ~attacker;
        orth &= ~attacker;
        side = ~side;
        attackers |= movegen::getSliderAttackers(occ, target, diag, orth);
        attackertype = getLVA(b, attackers & side, attacker);
        std::size_t i = 1;
        for (; i != 32 && attackertype; ++i)
        {
            scores[i] = pieceval[targettype] - scores[i - 1];
            if (scores[i] < 0) break;
            targettype = attackertype - 1;
            attackers ^= attacker;
            occ ^= attacker;
            diag &= ~attacker;
            orth &= ~attacker;
            side = ~side;
            attackers |= movegen::getSliderAttackers(occ, target, diag, orth);
            attackertype = getLVA(b, attackers & side, attacker);
        }
        while (--i)
            scores[i - 1] = std::min<Eval>(scores[i - 1], -scores[i]);
        return scores[0];
    }

    // TODO take into account X ray attacks
    Eval squareControl(const board::QBB& b, board::square s)
    {
        Eval control = 0;

        auto myAttackers = b.my(b.getPawns()) & movegen::enemyPawnAttacks(s);
        auto theirAttackers = b.their(b.getPawns()) & movegen::pawnAttacks(s);
        control += 900 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        myAttackers = b.my(b.getKnights()) & movegen::knightAttacks(s);
        theirAttackers = b.their(b.getKnights()) & movegen::knightAttacks(s);
        control += 500 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        const auto sliders = movegen::getSliderAttackers(b.getOccupancy(), s, b.getDiagSliders(), b.getOrthSliders());

        myAttackers = b.my(b.getBishops()) & sliders;
        theirAttackers = b.their(b.getBishops()) & sliders;
        control += 500 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        myAttackers = b.my(b.getRooks()) & sliders;
        theirAttackers = b.their(b.getRooks()) & sliders;
        control += 300 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        myAttackers = b.my(b.getQueens()) & sliders;
        theirAttackers = b.their(b.getQueens()) & sliders;
        control += 100 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        myAttackers = b.my(b.getKings()) & movegen::kingAttacks(s);
        theirAttackers = b.their(b.getKings()) & movegen::kingAttacks(s);
        control += 50 * (_popcnt64(myAttackers) - _popcnt64(theirAttackers));

        return control;
    }

    Eval evalCapture(const board::QBB& b, board::Move m)
    {
        Eval mvvlvaScore = mvvlva(b, m);
        return mvvlvaScore >= 0 ? mvvlvaScore : see(b, m);
    }

    Eval Evaluator::applyAggressionBonus(std::size_t type, board::square enemyKingSq, board::Bitboard pieces) const
    {
        unsigned long index = 0;
        Eval e = 0;
        while (_BitScanForward64(&index, pieces))
        {
            pieces = _blsr_u64(pieces);
            e += aggressionBonus(board::square(index), enemyKingSq, _aggressionBonuses[type]);
        }
        return e;
    }

    Eval Evaluator::apply7thRankBonus(board::Bitboard rooks, board::Bitboard rank) const
    {
        return rook7thRankBonus * (_popcnt64(rooks & rank));
    }

    unsigned Evaluator::totalMaterialValue(const board::QBB& b) const
    {
        unsigned materialVal = 0;
        unsigned piecevalues[6] = { 100, 300, 300, 500, 900, 0 };
        GetNextBit<board::square> currSquare(b.getOccupancy());
        while (currSquare())
        {
            auto sq = currSquare.next;
            auto pieceType = b.getPieceCodeIdx(sq);
            materialVal += piecevalues[pieceType];
        }
        return materialVal;
    }

    bool Evaluator::isEndgame(const board::QBB& b) const
    {
        auto materialValue = 900 * (b.getQueens() & ~b.side);
        materialValue += 500 * (b.getRooks() & ~b.side);
        materialValue += 300 * (b.getBishops() & ~b.side);
        materialValue += 300 * (b.getKnights() & ~b.side);
        return materialValue < 1900;
    }

    Eval Evaluator::bishopOpenDiagonalBonus(board::Bitboard occ, board::Bitboard bishops) const
    {
        unsigned long index = 0;
        Eval e = 0;
        while (_BitScanForward64(&index, bishops))
        {
            bishops = _blsr_u64(bishops);
            auto square = board::square(index);
            if (board::diagMask(square) == movegen::hypqDiag(occ, square))
            {
                e += _bishopOpenDiagonalBonus;
            }
            if (board::antiDiagMask(square) == movegen::hypqAntiDiag(occ, square))
            {
                e += _bishopOpenDiagonalBonus;
            }
        }
        return e;
    }

    Eval Evaluator::rookOpenFileBonus(board::Bitboard pawns, board::Bitboard rooks) const
    {
        unsigned long index = 0;
        Eval e = 0;
        while (_BitScanForward64(&index, rooks))
        {
            rooks = _blsr_u64(rooks);
            auto square = board::square(index);
            e += ((board::fileMask(square) & pawns) == 0) * _rookOpenFileBonus;
        }
        return e;
    }

    Eval Evaluator::evalPawns(const board::Bitboard myPawns, const board::Bitboard theirPawns) const noexcept
    {
        std::array<board::Bitboard, 8> files = {board::fileMask(board::a1), board::fileMask(board::b1), 
        board::fileMask(board::c1), board::fileMask(board::d1), board::fileMask(board::e1),
        board::fileMask(board::f1), board::fileMask(board::g1), board::fileMask(board::h1)};

        std::array<board::Bitboard, 8> neighboringFiles = {files[1], files[0] | files[2], 
        files[1] | files[3], files[2] | files[4], files[3] | files[5], files[4] | files[6], 
        files[5] | files[7], files[6]};

        Eval evaluation = 0;
        for (auto file : files)
        {
            evaluation -= doubledpawnpenalty * (_popcnt64(myPawns & file) == 2);
            evaluation -= tripledpawnpenalty * (_popcnt64(myPawns & file) > 2);
            evaluation += doubledpawnpenalty * (_popcnt64(theirPawns & file) == 2);
            evaluation += tripledpawnpenalty * (_popcnt64(theirPawns & file) > 2);
        }

        for (std::size_t i = 0; i != files.size(); ++i)
        {
            if (myPawns & files[i])
            {
                if (!(myPawns & neighboringFiles[i]))
                {
                    evaluation -= isolatedpawnpenalty;
                }
            }
            if (theirPawns & files[i])
            {
                if (!(theirPawns & neighboringFiles[i]))
                {
                    evaluation += isolatedpawnpenalty;
                }
            }
        }

        auto [myPassedPawns, theirPassedPawns] = detectPassedPawns(myPawns, theirPawns);

        aux::GetNextBit<board::square> ppSquare(myPassedPawns);
        while (ppSquare())
        {
            auto rank = aux::rank(ppSquare.next);
            evaluation += _passedPawnBonus[rank - 1];
        }

        ppSquare = aux::GetNextBit<board::square>{theirPassedPawns};
        while (ppSquare())
        {
            auto rank = 7 - aux::rank(ppSquare.next);
            evaluation -= _passedPawnBonus[rank - 1];
        }

        return evaluation;
    }

    Eval Evaluator::operator()(const board::QBB& b) const
    {
        Eval evaluation = 0;

        const std::array<board::Bitboard, 12> pieces = {
            b.my(b.getPawns()),
            b.my(b.getKnights()),
            b.my(b.getBishops()),
            b.my(b.getRooks()),
            b.my(b.getQueens()),
            b.my(b.getKings()),
            b.their(b.getPawns()),
            b.their(b.getKnights()),
            b.their(b.getBishops()),
            b.their(b.getRooks()),
            b.their(b.getQueens()),
            b.their(b.getKings()),
        };

        enum {myPawns, myKnights, myBishops, myRooks, myQueens, myKing,
            theirPawns, theirKnights, theirBishops, theirRooks, theirQueens, theirKing,};

        for (std::size_t i = 0; i != 5; ++i)
        {
            evaluation += piecevals[i] * (_popcnt64(pieces[i]) - _popcnt64(pieces[i + 6]));
        }

        const auto myKingSq = board::square(_tzcnt_u64(pieces[myKing]));
        const auto oppKingSq = board::square(_tzcnt_u64(pieces[theirKing]));

        for (std::size_t i = 0; i != 12; ++i)
        {
            evaluation += (i < 6 ? 1 : -1) * applyAggressionBonus(i, i < 6 ? oppKingSq : myKingSq, pieces[i]);
        }

        aux::GetNextBit<board::Bitboard> mobility(pieces[myKnights]);
        while (mobility())
        {
            movegen::AttackMap moves = movegen::knightAttacks(mobility.next);
            moves &= ~(movegen::enemyPawnAttacks(pieces[theirPawns]) | pieces[myKing] | pieces[myPawns]);
            evaluation += knightmobility*_popcnt64(moves);
        }
        mobility = GetNextBit<board::Bitboard>{ pieces[theirKnights] };
        while (mobility())
        {
            movegen::AttackMap moves = movegen::knightAttacks(mobility.next);
            moves &= ~(movegen::pawnAttacks(pieces[myPawns]) | pieces[theirKing] | pieces[theirPawns]);
            evaluation -= knightmobility*_popcnt64(moves);
        }
        mobility = GetNextBit<board::Bitboard>{ pieces[myBishops] };
        while (mobility())
        {
            movegen::AttackMap moves = movegen::hypqAllDiag(b.getOccupancy() & ~pieces[myQueens], mobility.next);
            moves &= ~(movegen::enemyPawnAttacks(pieces[theirPawns]) | pieces[myKing] | pieces[myPawns]);
            evaluation += bishopmobility*_popcnt64(moves);
        }
        mobility = GetNextBit<board::Bitboard>{ pieces[theirBishops] };
        while (mobility())
        {
            movegen::AttackMap moves = movegen::hypqAllDiag(b.getOccupancy() & ~pieces[theirQueens], mobility.next);
            moves &= ~(movegen::pawnAttacks(pieces[myPawns]) | pieces[theirKing] | pieces[theirPawns]);
            evaluation -= bishopmobility*_popcnt64(moves);
        }
        mobility = GetNextBit<board::Bitboard>{ pieces[myRooks] };
        while (mobility())
        {
            movegen::AttackMap moves = movegen::hypqRank(b.getOccupancy() & ~(pieces[myQueens] | pieces[myRooks]), mobility.next);
            moves &= ~(movegen::enemyPawnAttacks(pieces[theirPawns]) | pieces[myKing] | pieces[myPawns]);
            evaluation += rookhormobility*_popcnt64(moves);
            moves = movegen::hypqFile(b.getOccupancy() & ~(pieces[myQueens] | pieces[myRooks]), mobility.next);
            moves &= ~(movegen::enemyPawnAttacks(pieces[theirPawns]) | pieces[myKing] | pieces[myPawns]);
            evaluation += rookvertmobility*_popcnt64(moves);
        }
        mobility = GetNextBit<board::Bitboard>{ pieces[theirRooks] };
        while (mobility())
        {
            movegen::AttackMap moves = movegen::hypqRank(b.getOccupancy() & ~(pieces[theirQueens] | pieces[theirRooks]), mobility.next);
            moves &= ~(movegen::pawnAttacks(pieces[myPawns]) | pieces[theirKing] | pieces[theirPawns]);
            evaluation -= rookhormobility*_popcnt64(moves);
            moves = movegen::hypqFile(b.getOccupancy() & ~(pieces[theirQueens] | pieces[theirRooks]), mobility.next);
            moves &= ~(movegen::pawnAttacks(pieces[myPawns]) | pieces[theirKing] | pieces[theirPawns]);
            evaluation -= rookvertmobility*_popcnt64(moves);
        }

        evaluation += evalPawns(pieces[myPawns], pieces[theirPawns]); // TODO store this in pawn hash

        if (isEndgame(b))
        {
            evaluation += kingCentralization(myKingSq);
            evaluation -= kingCentralization(oppKingSq);
        }
        else
        {
            (void)0;// TODO King Safety
        }

        evaluation += this->bishopPairBonus((pieces[2] & constants::whiteSquares) && (pieces[2] & constants::blackSquares));
        evaluation -= this->bishopPairBonus((pieces[8] & constants::whiteSquares) && (pieces[8] & constants::blackSquares));

        evaluation += this->applyKnightOutPostBonus<OutpostType::MyOutpost>(pieces[1], pieces[0], pieces[6]);
        evaluation -= this->applyKnightOutPostBonus<OutpostType::OppOutpost>(pieces[7], pieces[0], pieces[6]);

        evaluation += this->rookOpenFileBonus(pieces[myPawns] | pieces[theirPawns], pieces[myRooks]);
        evaluation -= this->rookOpenFileBonus(pieces[myPawns] | pieces[theirPawns], pieces[theirRooks]);

        evaluation += this->apply7thRankBonus(pieces[myRooks], board::rankMask(board::a7));
        evaluation -= this->apply7thRankBonus(pieces[theirRooks], board::rankMask(board::a2));

        return evaluation;
    }

    const Evaluator& Evaluator::mutate(bool randomize)
    {
        std::bernoulli_distribution doMutate(randomize ? 0.85 : 1.0/2000.0);
        std::uniform_int_distribution positionalBonus(-50, 50);
        std::uniform_int_distribution gamePhase(-500, 500);
        std::uniform_int_distribution PSQT(-100, 100);
        std::uniform_int_distribution ZeroTo8(0, 8);

        auto mutate = [&](auto& mutator, int p = 0) {
            return doMutate(aux::seed) ? mutator(aux::seed) : p;
        };

        for (std::size_t i = 0; i != 12; ++i)
        {
            _aggressionBonuses[i].first = mutate(ZeroTo8, _aggressionBonuses[i].first);
            _aggressionBonuses[i].second += mutate(positionalBonus);
        }

        _pawnBishopPenalty.first = mutate(ZeroTo8, _pawnBishopPenalty.first);
        _pawnBishopPenalty.second += mutate(positionalBonus);

        _bishopOpenDiagonalBonus += mutate(positionalBonus);
        _rookOpenFileBonus += mutate(positionalBonus);
        _bishopPairBonus += mutate(positionalBonus);

        _knightOutpostBonus.first += mutate(positionalBonus);
        _knightOutpostBonus.second += mutate(positionalBonus);

        return *this;
    }

    std::string Evaluator::asString() const
    {
        std::ostringstream oss;
        /*
        std::array<std::string, 12> PSQTNames= {"wpawns", "wknights", "wbishops", "wrooks", "wqueens", "wking",
        "bpawns", "bknights", "bbishops", "brooks", "bqueens", "bking" };

        auto printPSQTName = [&](std::size_t i) {oss << PSQTNames[i] << "="; };
        
        auto printArrayVal = [&](std::size_t sq, Eval e) {
            if (sq % 8 == 0)
                oss << '\n';
            oss << e << ",";
        };

        
        auto printPSQTSet = [&](const auto& set) {
            for (std::size_t n = 0; n != 12; ++n)
            {
                oss << '\n';
                printPSQTName(n);
                oss << "{";
                for (std::size_t m = 0; m != 64; ++m)
                {
                    printArrayVal(m, set[n][m]);
                }
                oss << "}";
                oss << '\n';
            }
        };
        */
        oss << '\n';
        auto now = std::chrono::current_zone()->to_local(std::chrono::system_clock::now());
        oss << "Tuning completed date/time: " << now << '\n';

        oss << '\n';
        oss << "aggression";
        oss << '\n';

        for (const auto& [dist, bonus] : _aggressionBonuses)
        {
            oss << "<" << dist << "," << bonus << ">";
        }

        oss << '\n';
        oss << "pawnbishoppenalty";
        oss << '\n';

        oss << "<" << _pawnBishopPenalty.first << "," << _pawnBishopPenalty.second << ">";

        oss << '\n';
        oss << "opendiagonal,openfile,pair";
        oss << '\n';

        oss << "<" << _bishopOpenDiagonalBonus << "," << _rookOpenFileBonus << "," << _bishopPairBonus << ">";

        oss << '\n';
        oss << "knightoutpost";
        oss << '\n';

        oss << "<" << _knightOutpostBonus.first << "," << _knightOutpostBonus.second << ">";

        oss << '\n';
        return oss.str();

    }
}