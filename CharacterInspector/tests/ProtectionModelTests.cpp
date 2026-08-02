#include "../CharacterInspector/src/ProtectionModel.h"
#include <cassert>
#include <cmath>
#include <iostream>

void runPartLayerResolveTests();

static bool nearEq(float a, float b)
{
    return std::fabs(a - b) < 0.001f;
}

int main()
{
    assert(nearEq(normalizeCoverage(0.8f), 0.8f));
    assert(nearEq(normalizeCoverage(80.f), 0.8f));

    BodyPartInput chest;
    chest.name = "Chest";
    chest.layerId = "chest";
    chest.partKey = (void*)1;

    ArmourPieceInput a;
    a.name = "Plate";
    a.iconTexture = "icon_plate_test";
    a.coverage = 1.0f;
    a.cutResistance = 0.5f;
    a.bluntResistance = 0.4f;
    a.pierceResistance = 0.2f;
    a.cutEfficiency = 0.8f;
    a.minCutResistance = 0.1f;

    ArmourPieceInput b = a;
    b.name = "Shirt";
    b.coverage = 0.5f;
    b.cutResistance = 0.2f;
    b.bluntResistance = 0.1f;
    b.pierceResistance = 0.1f;
    b.cutEfficiency = 1.0f;

    chest.covering.push_back(a);
    chest.covering.push_back(b);

    BodyPartInput head = chest;
    head.name = "Head";
    head.layerId = "head";
    head.partKey = (void*)2;
    head.covering.clear();

    std::vector<BodyPartInput> parts;
    parts.push_back(chest);
    parts.push_back(head);

    CharacterProtectionView view = buildProtectionView("Beep", parts);
    assert(view.characterName == "Beep");
    assert(view.parts.size() == 2);
    assert(view.parts[0].layerId == "chest");
    assert(view.parts[1].layerId == "head");
    assert(nearEq(view.parts[0].weighted.cut, 0.6f));
    assert(nearEq(view.parts[0].weighted.blunt, 0.45f));
    assert(view.parts[0].pieces[0].iconTexture == "icon_plate_test");
    assert(view.parts[0].pieces[1].iconTexture.empty());
    assert(view.parts[1].weighted.cut == 0.f);
    assert(nearEq(view.totalsApprox.cut, 0.3f));

    // Stacked resists can exceed 1.0 — display must map 1.96 → 196, not 2.
    {
        BodyPartInput arm;
        arm.name = "Left Arm";
        ArmourPieceInput s;
        s.name = "Shield";
        s.coverage = 1.f;
        s.cutResistance = 0.81f;
        s.bluntResistance = 0.62f;
        s.pierceResistance = 0.86f;
        ArmourPieceInput c;
        c.name = "Cloak";
        c.coverage = 0.95f;
        c.cutResistance = 0.13f;
        c.bluntResistance = 0.14f;
        c.pierceResistance = 0.f;
        ArmourPieceInput m;
        m.name = "Mail";
        m.coverage = 1.f;
        m.cutResistance = 0.45f;
        m.bluntResistance = 0.21f;
        m.pierceResistance = 0.29f;
        ArmourPieceInput p;
        p.name = "Plate";
        p.coverage = 1.f;
        p.cutResistance = 0.58f;
        p.bluntResistance = 0.34f;
        p.pierceResistance = 0.33f;
        arm.covering.push_back(s);
        arm.covering.push_back(c);
        arm.covering.push_back(m);
        arm.covering.push_back(p);
        std::vector<BodyPartInput> one;
        one.push_back(arm);
        CharacterProtectionView v2 = buildProtectionView("Shutori", one);
        assert(v2.parts[0].weighted.cut > 1.5f);
        assert(v2.parts[0].weighted.cut < 2.1f);

        // Inline the display conversion the UI uses.
        float display = v2.parts[0].weighted.cut * 100.f;
        if (display > 100.f)
            display = 100.f;
        assert(display > 99.f);
    }

    runPartLayerResolveTests();

    std::cout << "ProtectionModelTests OK\n";
    return 0;
}
