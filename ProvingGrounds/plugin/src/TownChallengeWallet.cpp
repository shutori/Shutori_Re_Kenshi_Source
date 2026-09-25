#include "TownChallengeWallet.h"
#include <kenshi/GameWorld.h>
#include <kenshi/Globals.h>
#include <kenshi/PlayerInterface.h>
#include <kenshi/Faction.h>
#include <kenshi/Platoon.h>
#include <climits>

// Ownerships and Building.h each declare BuildingDesignation in this SDK.
// Keep wallet access in its own translation unit to avoid that collision.
namespace {
    Ownerships* Wallet() {
        Faction* faction = ou && ou->player ? ou->player->getFaction() : NULL;
        return faction ? faction->factionOwnerships : NULL;
    }
}
namespace TownChallengeWallet {
    int Balance() {
        Ownerships* wallet = Wallet();
        return wallet ? wallet->getMoney() : -1;
    }
    bool Take(int amount) {
        Ownerships* wallet = Wallet();
        return wallet && amount > 0 && wallet->getMoney() >= amount && wallet->takeMoney(amount);
    }
    bool Credit(int amount) {
        Ownerships* wallet = Wallet();
        if (!wallet || amount <= 0 || wallet->getMoney() < 0 || wallet->getMoney() > INT_MAX - amount) return false;
        wallet->addMoney(amount);
        return true;
    }
}
