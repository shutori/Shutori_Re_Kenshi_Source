#pragma once

// Pure data types for the tactical defense feature.
//
// IMPORTANT: This header must have ZERO KenshiLib includes. It is consumed by
// both the game plugin and the standalone unit test project, so it can only
// depend on the standard library.

#include <stdint.h>

struct Vec3
{
    float x;
    float y;
    float z;
};

// Kenshi-free snapshot of a `hand` (index/serial/type/container). Turrets
// mounted on walls are furniture: their handle's container fields matter, so
// packing only index+serial and forcing type=BUILDING fails to resolve them
// later (see resolve=missing remount failures).
struct ObjectRef
{
    uint32_t index;
    uint32_t serial;
    uint32_t container;
    uint32_t containerSerial;
    int32_t type; // Kenshi itemType, stored as int to keep this header pure

    bool isEmpty() const
    {
        return index == 0 && serial == 0 && container == 0 && containerSerial == 0 && type == 0;
    }

    bool operator==(const ObjectRef& other) const
    {
        return index == other.index
            && serial == other.serial
            && container == other.container
            && containerSerial == other.containerSerial
            && type == other.type;
    }

    bool operator!=(const ObjectRef& other) const
    {
        return !(*this == other);
    }
};

struct DefenseAssignment
{
    uint64_t characterId;
    Vec3 position;
    float facingYaw;
    bool hasTurret;
    ObjectRef turret;
};
