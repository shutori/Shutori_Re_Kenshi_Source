#pragma once

class Character;

namespace StatsOverviewUI
{
    void open(Character* character);
    void close();
    bool isOpen();
    Character* currentCharacter();
    void handleEscape();
}

