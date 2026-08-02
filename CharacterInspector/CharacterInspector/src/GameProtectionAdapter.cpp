#include "GameProtectionAdapter.h"
#include "PartLayerResolve.h"

#pragma warning(push)
#pragma warning(disable: 4091)
#include <kenshi/Character.h>
#include <kenshi/MedicalSystem.h>
#include <kenshi/Gear.h>
#include <kenshi/GameData.h>
#include <kenshi/Item.h>
#include <kenshi/util/iVector2.h>
#include <kenshi/gui/InventoryGUI.h>
#pragma warning(pop)

#include <algorithm>
#include <string>
#include <vector>

static PartLayer::Kind MapKind(MedicalSystem::HealthPartStatus::PartType t)
{
    switch (t)
    {
    case MedicalSystem::HealthPartStatus::PART_HEAD: return PartLayer::Head;
    case MedicalSystem::HealthPartStatus::PART_TORSO: return PartLayer::Torso;
    case MedicalSystem::HealthPartStatus::PART_ARM: return PartLayer::Arm;
    case MedicalSystem::HealthPartStatus::PART_LEG: return PartLayer::Leg;
    default: return PartLayer::Torso;
    }
}

static PartLayer::Side MapSide(LeftRight s)
{
    switch (s)
    {
    case SIDE_LEFT: return PartLayer::Left;
    case SIDE_RIGHT: return PartLayer::Right;
    case SIDE_BOTH: return PartLayer::Both;
    case SIDE_NEITHER:
    default: return PartLayer::Neither;
    }
}

struct TorsoIndexByHitChance
{
    const std::vector<MedicalSystem::HealthPartStatus*>* statuses;

    bool operator()(size_t a, size_t b) const
    {
        float ha = (*statuses)[a]->hitChance;
        float hb = (*statuses)[b]->hitChance;
        if (ha != hb)
            return ha > hb;
        return a < b;
    }
};

static float coverageFor(Armour* armour, GameData* partData)
{
    if (!armour || !partData)
        return 0.f;

    ogre_unordered_map<GameData*, float>::type::iterator it =
        armour->bodypartCoverage.find(partData);
    if (it == armour->bodypartCoverage.end())
        return 0.f;
    return it->second;
}

CharacterProtectionView buildFromCharacter(Character* character)
{
    if (!character)
        return buildProtectionView("Unknown", std::vector<BodyPartInput>());

    std::vector<BodyPartInput> parts;
    std::vector<MedicalSystem::HealthPartStatus*> statuses;
    MedicalSystem& med = character->medical;

    for (uint32_t i = 0; i < med.anatomy.size(); ++i)
    {
        MedicalSystem::HealthPartStatus* part = med.anatomy[i];
        if (!part || !part->data)
            continue;

        BodyPartInput bp;
        bp.name = part->data->name;
        bp.partKey = part->data;

        for (uint32_t a = 0; a < med.armourList.size(); ++a)
        {
            Armour* armour = med.armourList[a];
            if (!armour)
                continue;

            float cov = coverageFor(armour, part->data);
            if (normalizeCoverage(cov) <= 0.f)
                continue;

            ArmourPieceInput piece;
            piece.name = armour->getName();
            piece.coverage = cov;
            piece.cutResistance = armour->cutResistance;
            piece.bluntResistance = armour->bluntResistance;
            piece.pierceResistance = armour->pierceResistance;
            piece.cutEfficiency = armour->cutToStun;
            piece.minCutResistance = armour->minCutResistance;

            // Inventory thumbnails for part-mode rows; leave empty on failure.
            {
                std::string iconName;
                iVector2 iconSize;
                InventoryIcon::createIconImage(armour, iconName, iconSize);
                piece.iconTexture = iconName;
            }

            bp.covering.push_back(piece);
        }

        parts.push_back(bp);
        statuses.push_back(part);
    }

    std::vector<size_t> torsoIndices;
    for (size_t i = 0; i < statuses.size(); ++i)
    {
        if (statuses[i]->whatAmI == MedicalSystem::HealthPartStatus::PART_TORSO)
            torsoIndices.push_back(i);
    }

    TorsoIndexByHitChance torsoCmp;
    torsoCmp.statuses = &statuses;
    std::sort(torsoIndices.begin(), torsoIndices.end(), torsoCmp);

    std::vector<int> torsoRankByPartIndex(parts.size(), -1);
    for (size_t r = 0; r < torsoIndices.size(); ++r)
        torsoRankByPartIndex[torsoIndices[r]] = (int)r;

    for (size_t i = 0; i < parts.size(); ++i)
    {
        int torsoRank = -1;
        if (statuses[i]->whatAmI == MedicalSystem::HealthPartStatus::PART_TORSO)
            torsoRank = torsoRankByPartIndex[i];

        parts[i].layerId = PartLayer::Resolve(
            MapKind(statuses[i]->whatAmI),
            MapSide(statuses[i]->side),
            torsoRank,
            parts[i].name);
    }

    return buildProtectionView(character->getName(), parts);
}
