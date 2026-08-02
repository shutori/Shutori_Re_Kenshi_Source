#include "ProtectionModel.h"

float normalizeCoverage(float raw)
{
    if (raw > 1.0f)
        return raw / 100.0f;
    if (raw < 0.0f)
        return 0.0f;
    return raw;
}

static float normalizeResistValue(float value)
{
    // Raw Kenshi fields may be 0..1 or percent (> 1.5). Stacked sums are NOT passed here.
    if (value > 1.5f)
        return value / 100.0f;
    if (value < 0.f)
        return 0.f;
    return value;
}

CharacterProtectionView buildProtectionView(
    const std::string& characterName,
    const std::vector<BodyPartInput>& parts)
{
    CharacterProtectionView out;
    out.characterName = characterName;
    ResistTriple sum;

    for (size_t i = 0; i < parts.size(); ++i)
    {
        PartProtectionView pv;
        pv.partName = parts[i].name;
        pv.layerId = parts[i].layerId;
        pv.weighted.cut = 0.f;
        pv.weighted.blunt = 0.f;
        pv.weighted.pierce = 0.f;

        for (size_t j = 0; j < parts[i].covering.size(); ++j)
        {
            ArmourPieceInput piece = parts[i].covering[j];
            float c = normalizeCoverage(piece.coverage);
            if (c <= 0.f)
                continue;
            piece.coverage = c;
            piece.cutResistance = normalizeResistValue(piece.cutResistance);
            piece.bluntResistance = normalizeResistValue(piece.bluntResistance);
            piece.pierceResistance = normalizeResistValue(piece.pierceResistance);
            pv.pieces.push_back(piece);
            pv.weighted.cut += piece.cutResistance * c;
            pv.weighted.blunt += piece.bluntResistance * c;
            pv.weighted.pierce += piece.pierceResistance * c;
        }

        sum.cut += pv.weighted.cut;
        sum.blunt += pv.weighted.blunt;
        sum.pierce += pv.weighted.pierce;
        out.parts.push_back(pv);
    }

    if (!out.parts.empty())
    {
        float n = (float)out.parts.size();
        out.totalsApprox.cut = sum.cut / n;
        out.totalsApprox.blunt = sum.blunt / n;
        out.totalsApprox.pierce = sum.pierce / n;
    }

    return out;
}
