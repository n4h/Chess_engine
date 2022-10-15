/*
Copyright 2022, Narbeh Mouradian

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

#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>

#include "divide.hpp"
#include "board.hpp"
#include "constants.hpp"
#include "auxiliary.hpp"
#include "perft.hpp"


namespace divide
{
	std::string prettyPrintMove(board::Move m)
	{
		std::size_t from = board::getMoveInfo<constants::fromMask>(m);
		std::size_t to = board::getMoveInfo<constants::toMask>(m);
		std::ostringstream p;

		auto fromFile = aux::file(from);
		auto toFile = aux::file(to);
		auto fromRank = aux::rank(from);
		auto toRank = aux::rank(to);

		p << aux::file2char(fromFile);
		p << fromRank + 1;
		p << aux::file2char(toFile);
		p << toRank + 1;

		switch (board::getPromoPiece(m))
		{
		case board::queens:
			p << 'Q';
			return p.str();
		case board::rooks:
			p << 'R';
			return p.str();
		case board::bishops:
			p << 'B';
			return p.str();
		case board::knights:
			p << 'N';
			return p.str();
		default:
			return p.str();
		}
	}
}