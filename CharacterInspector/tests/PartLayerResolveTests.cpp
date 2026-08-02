#include "../CharacterInspector/src/PartLayerResolve.h"
#include <cassert>
#include <string>

void runPartLayerResolveTests()
{
    using namespace PartLayer;

    assert(Resolve(Head, Neither, -1, "") == "head");
    assert(Resolve(Head, Neither, -1, "\xe5\xa4\xb4\xe9\x83\xa8") == "head");

    assert(Resolve(Arm, Left, -1, "") == "left_arm");
    assert(Resolve(Arm, Right, -1, "") == "right_arm");
    assert(Resolve(Leg, Left, -1, "") == "left_leg");
    assert(Resolve(Leg, Right, -1, "") == "right_leg");

    // Localized names must not be required when side is known.
    assert(Resolve(Arm, Left, -1, "\xe5\xb7\xa6\xe8\x87\x82") == "left_arm");

    assert(Resolve(Torso, Neither, 0, "") == "chest");
    assert(Resolve(Torso, Neither, 1, "") == "stomach");
    assert(Resolve(Torso, Neither, 2, "") == "");
    assert(Resolve(Torso, Neither, -1, "") == "");

    // Ambiguous side: English fallback.
    assert(Resolve(Arm, Neither, -1, "Left Arm") == "left_arm");
    assert(Resolve(Arm, Both, -1, "Right Arm") == "right_arm");

    // Ambiguous side + non-English -> unmapped.
    assert(Resolve(Arm, Neither, -1, "\xe5\xb7\xa6\xe8\x87\x82") == "");

    assert(FromName("Head") == "head");
    assert(FromName("Stomach") == "stomach");
    assert(FromName("Left Leg") == "left_leg");
    assert(FromName("\xe5\xa4\xb4\xe9\x83\xa8") == "");
}
