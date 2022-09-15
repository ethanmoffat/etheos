
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef WEDDING_HPP_INCLUDED
#define WEDDING_HPP_INCLUDED

#include "fwd/wedding.hpp"

#include "fwd/character.hpp"
#include "fwd/map.hpp"
#include "fwd/npc.hpp"
#include "fwd/timer.hpp"

#include <set>
#include <string>

class Wedding
{
	private:
		Map* map;
		unsigned char priest_idx;

		int state;
		int tick;

		TimeEvent* tick_timer;

		std::string partner1;
		std::string partner2;

		std::set<Character*> requests;

		NPC* GetPriest();
		Character* GetPartner1();
		Character* GetPartner2();

		void PriestSay(const std::string& message);

		void StartTimer();
		void StopTimer();

		void ChangeState(int state);
		void NextState();

		bool Check();

		void Reset();
		void ErrorOut();

	public:
		Wedding(Map* map, unsigned char priest_idx);

		void Tick();
		void RequestWedding(Character* requester);
		void CancelWeddingRequest(Character* requester);
		bool RequestedWedding(Character* requester);
		bool StartWedding(const std::string& player1, const std::string& player2);

		bool Busy();

		void IDo(Character* character);

		~Wedding();
};

#endif // WEDDING_HPP_INCLUDED
