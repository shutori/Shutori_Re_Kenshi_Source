#include "ArenaCombatProfile.h"
#include <kenshi/Character.h>
#include <kenshi/CharStats.h>
#include <kenshi/Gear.h>
#include <kenshi/Inventory.h>
#include <iomanip>
#include <limits>
#include <sstream>

namespace ArenaCombatProfile {
namespace {
bool Finite(double value) {
    return value == value && value <= (std::numeric_limits<double>::max)() &&
        value >= -(std::numeric_limits<double>::max)();
}

bool CoreStatsFinite(const Profile& profile) {
    return Finite(profile.attack) && Finite(profile.defence) &&
        Finite(profile.strength) && Finite(profile.toughness) &&
        Finite(profile.dexterity);
}

double SkillFor(CharStats* stats, WeaponCategory category) {
    switch (category) {
    case SKILL_KATANAS: return stats->katanas;
    case SKILL_SABRES: return stats->sabres;
    case SKILL_BLUNT: return stats->blunt;
    case SKILL_HEAVY: return stats->heavyWeapons;
    case SKILL_HACKERS: return stats->hackers;
    case SKILL_UNARMED: return stats->unarmed;
    case SKILL_BOW: return stats->bows;
    case SKILL_TURRET: return stats->turrets;
    case ATTACK_POLEARMS: return stats->polearms;
    default: return stats->getEquippedWeaponSkill();
    }
}

const char* StyleFor(WeaponCategory category) {
    switch (category) {
    case SKILL_KATANAS: return "Katana";
    case SKILL_SABRES: return "Sabre";
    case SKILL_BLUNT: return "Blunt";
    case SKILL_HEAVY: return "Heavy weapon";
    case SKILL_HACKERS: return "Hacker";
    case SKILL_UNARMED: return "Martial arts";
    case SKILL_BOW: return "Crossbow";
    case SKILL_TURRET: return "Turret";
    case ATTACK_POLEARMS: return "Polearm";
    default: return "Weapon";
    }
}

void Append(std::ostringstream& out, const char* label, double value) {
    if (out.tellp() > 0) out << '\n';
    out << label << ' ' << value;
}
}

Profile::Profile() : valid(false), martial(false), attack(0), defence(0),
    strength(0), toughness(0), dexterity(0), weaponSkill(0) {}

Profile Read(Character* character) {
    Profile profile;
    if (!character || !character->isValid()) return profile;
    CharStats* stats = character->getStats();
    if (!stats) return profile;

    // Combat's preferred weapon can be the natural/unarmed weapon while an
    // NPC is idle. Inspect equipment slots so sheathing never changes the card.
    Inventory* inventory = character->getInventory();
    Weapon* weapon = inventory ? inventory->getPrimaryWeapon() : NULL;
    if (!weapon || !weapon->isValid() || weapon->getCategory() == SKILL_UNARMED)
        weapon = inventory ? inventory->getSecondaryWeapon() : NULL;
    const bool usableWeapon = weapon && weapon->isValid();
    const WeaponCategory category = usableWeapon ? weapon->getCategory() : SKILL_UNARMED;
    profile.martial = !usableWeapon || category == SKILL_UNARMED;
    profile.style = profile.martial ? "Martial arts" : usableWeapon ? StyleFor(category) : "Weapon";
    profile.attack = profile.martial ? stats->getMeleeAttack_unarmed(true) : stats->getMeleeAttack_melee();
    profile.defence = profile.martial ? stats->getDodge(true) : stats->getMeleeDefence_melee(false);
    profile.strength = stats->strengthActual();
    profile.toughness = stats->toughness();
    profile.dexterity = stats->dexterityActual();
    profile.weaponSkill = profile.martial ? 0 : usableWeapon ?
        SkillFor(stats, category) : stats->getEquippedWeaponSkill();
    profile.valid = CoreStatsFinite(profile) && (profile.martial || Finite(profile.weaponSkill));
    return profile;
}

double Evaluate(const Profile& profile) {
    if (!profile.valid || !CoreStatsFinite(profile) ||
        (!profile.martial && !Finite(profile.weaponSkill))) return -1;
    const double score = profile.martial ?
        10 + .30 * profile.attack + .30 * profile.defence + .15 * profile.strength +
            .15 * profile.toughness + .10 * profile.dexterity :
        10 + .25 * profile.attack + .25 * profile.defence + .15 * profile.strength +
            .15 * profile.toughness + .10 * profile.dexterity + .10 * profile.weaponSkill;
    return Finite(score) && score > 0 ? score : -1;
}

std::string StatText(const Profile& profile) {
    if (!profile.valid || !CoreStatsFinite(profile) ||
        (!profile.martial && !Finite(profile.weaponSkill))) return "Combat stats unavailable";
    std::ostringstream out;
    out << std::fixed << std::setprecision(0);
    Append(out, profile.martial ? "Martial arts" : "Attack", profile.attack);
    Append(out, profile.martial ? "Dodge" : "Defence", profile.defence);
    Append(out, "Strength", profile.strength);
    Append(out, "Toughness", profile.toughness);
    Append(out, "Dexterity", profile.dexterity);
    if (!profile.martial) {
        const std::string skill = profile.style.empty() || profile.style == "Weapon" ?
            "Weapon skill" : profile.style + " skill";
        Append(out, skill.c_str(), profile.weaponSkill);
    }
    return out.str();
}
}
