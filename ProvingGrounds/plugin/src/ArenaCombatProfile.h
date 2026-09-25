#pragma once
#include <string>

class Character;

namespace ArenaCombatProfile {
struct Profile {
    bool valid, martial;
    std::string style;
    double attack, defence, strength, toughness, dexterity, weaponSkill;
    Profile();
};

Profile Read(Character* character);
double Evaluate(const Profile& profile);
std::string StatText(const Profile& profile);
}
