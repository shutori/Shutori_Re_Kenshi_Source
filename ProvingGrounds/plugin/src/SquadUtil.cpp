#include "SquadUtil.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/Enums.h>
#include <kenshi/Faction.h>
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Platoon.h>
#include <kenshi/RootObject.h>
#include <kenshi/gui/ForgottenGUI.h>
#pragma warning(pop)

#ifndef NULL
#define NULL 0
#endif

namespace SquadUtil
{
    void CollectPlayerSquad(std::vector<Character*>& out)
    {
        out.clear();
        if (!ou || !ou->player)
            return;

        Faction* faction = ou->player->getFaction();
        if (!faction)
            return;

        const lektor<Platoon*>* squads = faction->getAllActiveSquads();
        if (!squads || !squads->valid())
            return;

        for (uint32_t i = 0; i < squads->size(); ++i)
        {
            Platoon* platoon = (*squads)[i];
            if (!platoon)
                continue;

            ActivePlatoon* active = platoon->getActivePlatoon();
            if (!active)
                continue;

            lektor<RootObject*>* things = active->getThings();
            if (!things || !things->valid())
                continue;

            for (uint32_t t = 0; t < things->size(); ++t)
            {
                RootObject* obj = (*things)[t];
                if (!obj || !obj->isValid())
                    continue;

                itemType type = obj->getDataType();
                if (type != CHARACTER && type != HUMAN_CHARACTER && type != ANIMAL_CHARACTER)
                    continue;

                Character* character = static_cast<Character*>(obj);
                if (!character || character->isDead())
                    continue;

                out.push_back(character);
            }
        }
    }

    void CollectSelectedCharacters(std::vector<Character*>& out)
    {
        out.clear();
        if (!ou || !ou->player)
            return;

        PlayerInterface* pi = ou->player;
        ogre_unordered_set<hand>::type::iterator it = pi->selectedCharacters.begin();
        for (; it != pi->selectedCharacters.end(); ++it)
        {
            Character* character = it->getCharacter();
            if (!character || !character->isValid() || character->isDead())
                continue;
            out.push_back(character);
        }

        // RMB / some UI paths leave selectedCharacters empty but keep the active PC.
        if (out.empty() && gui)
        {
            Character* character = gui->selectedPlayerCharacter.getCharacter();
            if (character && character->isValid() && !character->isDead())
                out.push_back(character);
        }
    }
}
