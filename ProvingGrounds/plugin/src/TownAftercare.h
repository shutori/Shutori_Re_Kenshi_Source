#pragma once
#include <vector>
#include <string>
#include <ogre/OgreVector3.h>
class Character;
class Building;
namespace TownAftercare {
    bool InstallHooks();
    bool ControlAvailable();
    // Share the installed order-ownership hook with short-lived town systems
    // such as the arena announcer.
    bool ReserveExternalOrders(void* receiver);
    void ReleaseExternalOrders(void* receiver);
    // Separate capacity/lifetime from medics and other short-lived town actors.
    bool ReserveBenchOrders(void* receiver);
    void ReleaseBenchOrders(void* receiver);
    // Completed patients are released individually while other patients finish.
    bool IsManaged(Character* character);
    // Observe native callbacks without changing their timing or result.
    void RecordTreatment(Character* medic, Character* patient, bool doctoring,
        bool blocked, bool complete, float frameTime);
    void MarkWinner(Character* fighter);
    void Begin(const std::vector<Character*>& medics, const std::vector<Character*>& patients, Building* arena, const Ogre::Vector3& standby);
    void Standby();
    bool Treat(float elapsed, bool eliminatedOnly = false);
    // Give the hospital's beds back once their occupant is healed. Runs outside
    // the care session, because a delivered patient stops being managed while
    // they are still lying in the bed.
    void TickBedRecovery();
    void Release();
    void AbandonWorldState();
    const std::string& GetStatus();
}
