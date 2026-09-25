#pragma once
#include "TownArenaPolicy.h"
#include <sstream>
#include <string>
#include <stdint.h>
#include <iomanip>
#include <vector>

namespace TownChallengePolicy
{
    enum ChallengeMode { ModeTeams, ModeTeams1v1 };
    enum Encounter {
        EncounterSabre, EncounterKatana, EncounterChampion, EncounterSenn,
        EncounterTorka, EncounterSkarn, EncounterRessaVane, EncounterBumPer,
        EncounterVeyr
    };
    enum UniqueVictory {
        UniqueRessaVane = 1, UniqueBumPer = 2, UniqueSenn = 4,
        UniqueTorka = 8, UniqueVeyr = 16, AllUniqueVictories = 31
    };
    inline unsigned UniqueVictoryForEncounter(int encounter)
    {
        if (encounter == EncounterRessaVane) return UniqueRessaVane;
        if (encounter == EncounterBumPer) return UniqueBumPer;
        if (encounter == EncounterSenn) return UniqueSenn;
        if (encounter == EncounterTorka) return UniqueTorka;
        if (encounter == EncounterVeyr) return UniqueVeyr;
        return 0;
    }

    inline int NamedDivision(int encounter)
    {
        if (encounter == EncounterRessaVane) return 0;
        if (encounter == EncounterBumPer) return 1;
        if (encounter == EncounterSenn || encounter == EncounterTorka ||
            encounter == EncounterSkarn || encounter == EncounterVeyr) return 2;
        return -1;
    }
    inline bool NamedForDivision(int encounter, int division) {
        return NamedDivision(encounter) == division;
    }
    inline int NamedRoleDivision(const std::string& role)
    {
        if (role == "616-Proving Grounds.mod") return 0;
        if (role == "619-Proving Grounds.mod") return 1;
        if (role == "169-Proving Grounds.mod" || role == "168-Proving Grounds.mod" ||
            role == "167-Proving Grounds.mod" || role == "623-Proving Grounds.mod") return 2;
        return -1;
    }

    inline std::string OpponentKey(const std::string& uid, const std::string& role, const std::string& name)
    {
        if (!uid.empty()) return uid;
        return role.empty() || name.empty() ? std::string() : "name:" + role + ":" + name;
    }
    inline int RarityForRoll(unsigned roll)
    {
        return roll < 40 ? 0 : roll < 70 ? 1 : roll < 90 ? 2 : 3;
    }
    struct Offer
    {
        int epoch, encounter, format, mode;
        TownArenaPolicy::OfferState state;
        std::string opponent, name;
        std::string secondOpponent, secondName;
        std::string thirdOpponent, thirdName;
        std::string fourthOpponent, fourthName;
        bool generated;
        unsigned draw;
        int division, playerCount, enemyCount;
        std::string context, roles[4];
        Offer() : epoch(-2), encounter(0), format(0), mode(ModeTeams), state(TownArenaPolicy::OfferAvailable),
            generated(false), draw(0), division(0), playerCount(0), enemyCount(0) {}
    };
    inline const char* ModeName(int mode) {
        return mode == ModeTeams1v1 ? "Teams 1v1" : "Teams";
    }
    inline int Players(const Offer& o) { return o.generated ? o.playerCount : o.format == 6 ? 6 : o.format == 0 || o.format == 3 ? 1 : o.format == 1 || o.format == 2 ? 2 : 3; }
    inline int Opponents(const Offer& o) { return o.generated ? o.enemyCount : o.format == 6 ? 3 : o.format >= 2 && o.format <= 4 ? 2 : 1; }
    inline int SecondRole(const Offer& o)
    {
        return o.encounter == EncounterSabre ? EncounterKatana :
            o.encounter == EncounterKatana ? EncounterSabre :
            o.encounter == EncounterChampion ? EncounterKatana :
            o.encounter == EncounterSenn ? EncounterTorka :
            o.encounter == EncounterTorka || o.encounter == EncounterSkarn ? EncounterSenn :
            EncounterChampion;
    }
    inline std::string& OpponentId(Offer& o, int member) { return member == 0 ? o.opponent : member == 1 ? o.secondOpponent : member == 2 ? o.thirdOpponent : o.fourthOpponent; }
    inline const std::string& OpponentId(const Offer& o, int member) { return member == 0 ? o.opponent : member == 1 ? o.secondOpponent : member == 2 ? o.thirdOpponent : o.fourthOpponent; }
    inline std::string& OpponentName(Offer& o, int member) { return member == 0 ? o.name : member == 1 ? o.secondName : member == 2 ? o.thirdName : o.fourthName; }
    inline const std::string& OpponentName(const Offer& o, int member) { return member == 0 ? o.name : member == 1 ? o.secondName : member == 2 ? o.thirdName : o.fourthName; }
    inline int OpponentRole(const Offer& o, int member) { return member == 0 ? o.encounter : member == 1 ? SecondRole(o) : 4; }
    inline bool Resolved(const Offer& o) {
        if (Players(o) < 1 || Opponents(o) < 1) return false;
        for (int i = 0; i < Opponents(o); ++i) if (OpponentId(o, i).empty()) return false;
        return true;
    }
    // The reservation is session-owned. A migrated named source must survive
    // booking its regular counterpart; both views share the source attempt.
    inline bool ReserveOffer(Offer& source, const Offer& preview, Offer& reservation) {
        if (source.state != TownArenaPolicy::OfferAvailable || source.epoch != preview.epoch ||
            !Resolved(preview)) return false;
        reservation = preview;
        if (source.generated || !preview.generated) source = preview;
        TownArenaPolicy::BookOffer(source.state);
        reservation.state = source.state;
        return true;
    }
    inline bool SameEncounter(const Offer& a, const Offer& b)
    {
        if (Players(a) != Players(b) || Opponents(a) != Opponents(b)) return false;
        if (Opponents(a) == 1) return a.encounter == b.encounter;
        return (a.encounter == b.encounter && SecondRole(a) == SecondRole(b)) ||
            (a.encounter == SecondRole(b) && SecondRole(a) == b.encounter);
    }
    struct Card
    {
        uint32_t seed;
        unsigned ambientSeed;
        int division;
        std::vector<std::vector<std::string> > playerHistory, ambientHistory;
        unsigned milestones, uniqueWins;
        unsigned challengeWins;
        Offer offers[5];
        Offer skarnOffer;
        bool skarnGenerated, skarnWon, skarnAttempted, skarnInProgress;
        double skarnLastEnd;
        double lastChallengeEndHours;
        bool hasChallengeEnd;
        int paidRefreshCount;
        // Kept separate from seed, which also drives scheduled offers and Skarn.
        unsigned paidRefreshNonce;
        Card() : seed(1), ambientSeed(2463534242u), division(0), milestones(0), uniqueWins(0), challengeWins(0), skarnGenerated(false), skarnWon(false),
            skarnAttempted(false), skarnInProgress(false), skarnLastEnd(0.0), lastChallengeEndHours(0.0), hasChallengeEnd(false), paidRefreshCount(0), paidRefreshNonce(1) {}
    };
    inline bool ChallengeCooldownActive(const Card& card, double now, int cooldownHours)
    {
        return cooldownHours > 0 && card.hasChallengeEnd &&
            now >= card.lastChallengeEndHours &&
            now - card.lastChallengeEndHours < cooldownHours;
    }
    inline Offer& At(Card& card, int slot) { return slot == 5 ? card.skarnOffer : card.offers[slot]; }
    inline void RecordVictory(Card& card, bool challenge, bool won, unsigned defeated)
    {
        if (!won) return;
        card.uniqueWins |= defeated & AllUniqueVictories;
        if (challenge && card.challengeWins < 1000000000u) ++card.challengeWins;
    }
    inline bool SkarnUnlocked(const Card& card) {
        return (card.uniqueWins & AllUniqueVictories) == AllUniqueVictories;
    }
    inline uint32_t Draw(Card& card)
    {
        card.seed = card.seed * 1664525u + 1013904223u;
        return card.seed;
    }
    inline void EnsureSkarn(Card& card)
    {
        if (!SkarnUnlocked(card) || card.skarnGenerated || card.skarnInProgress) return;
        card.skarnOffer = Offer();
        card.skarnOffer.encounter = 5;
        card.skarnOffer.format = card.skarnWon ? (Draw(card) >> 8) % 7 : 0;
        card.skarnGenerated = true;
    }
    inline void BeginSkarn(Card& card)
    {
        if (TownArenaPolicy::BeginOffer(card.skarnOffer.state)) card.skarnInProgress = true;
    }
    inline void EndSkarn(Card& card, double now, bool won)
    {
        if (!card.skarnInProgress || !(now >= 0.0) || now > 24000000.0) return;
        card.skarnInProgress = false;
        card.skarnAttempted = true;
        card.skarnLastEnd = now;
        card.skarnWon = card.skarnWon || won;
        card.skarnGenerated = false;
    }
    inline void Refresh(Card& card, int epoch, int pinned)
    {
        if (epoch < -1) return;
        for (int i = 0; i < 5; ++i)
        {
            if (i == pinned || card.offers[i].epoch == epoch) continue;
            Offer& offer = card.offers[i];
            offer = Offer();
            offer.epoch = epoch;
            const unsigned roll = Draw(card) % 100;
            const int rarity = RarityForRoll(roll);
            offer.encounter = rarity < 3 ? rarity : (Draw(card) & 1u) ? 3 : 4;
            const int initialFormat = (Draw(card) >> 8) % 6;
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                offer.format = (initialFormat + attempt) % 6;
                bool duplicate = false;
                for (int j = 0; j < 5; ++j)
                    if (j != i && (j == pinned || card.offers[j].epoch == epoch) && SameEncounter(offer, card.offers[j]))
                        duplicate = true;
                if (!duplicate) break;
            }
        }
    }
    inline const char* Role(int encounter)
    {
        static const char* ids[] = { "96-Proving Grounds.mod", "97-Proving Grounds.mod",
            "98-Proving Grounds.mod", "169-Proving Grounds.mod", "168-Proving Grounds.mod",
            "167-Proving Grounds.mod", "616-Proving Grounds.mod", "619-Proving Grounds.mod",
            "623-Proving Grounds.mod" };
        return encounter >= 0 && encounter <= EncounterVeyr ? ids[encounter] : "";
    }
    inline const char* Rarity(int encounter)
    {
        return encounter == 0 ? "Common" : encounter == 1 ? "Uncommon" :
            encounter == 2 ? "Rare" : "Legendary";
    }
    inline const char* Title(int encounter)
    {
        static const char* names[] = { "Sabre fighter", "Katana fighter", "Champion", "Senn", "Torka", "Skarn",
            "Ressa Vane", "Bum-Per", "Veyr the Gilded" };
        return encounter >= 0 && encounter <= EncounterVeyr ? names[encounter] : "Unavailable";
    }
    inline std::string Hex(const std::string& value)
    {
        if (value.empty()) return "-";
        std::string result;
        for (size_t i = 0; i < value.size(); ++i)
        {
            const unsigned char c = value[i];
            result += "0123456789abcdef"[c >> 4];
            result += "0123456789abcdef"[c & 15];
        }
        return result;
    }
    inline bool Unhex(const std::string& value, std::string& result)
    {
        result.clear();
        if (value == "-") return true;
        if (value.size() > 2048 || value.size() % 2) return false;
        for (size_t i = 0; i < value.size(); i += 2)
        {
            unsigned byte = 0;
            for (int j = 0; j < 2; ++j)
            {
                const char c = value[i+j];
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
                byte = byte * 16 + (c <= '9' ? c - '0' : c - 'a' + 10);
            }
            result += static_cast<char>(byte);
        }
        return true;
    }
    inline std::string EncodeLegacy(const Card& card)
    {
        std::ostringstream out;
        out << std::setprecision(17) << 5 << ' ' << card.seed << ' ' << card.milestones << ' ' << card.uniqueWins << ' ' << card.challengeWins
            << ' ' << card.skarnGenerated << ' ' << card.skarnWon << ' ' << card.skarnAttempted
            << ' ' << card.skarnInProgress << ' ' << card.skarnLastEnd;
        for (int i = 0; i < 6; ++i)
        {
            const Offer& o = i == 5 ? card.skarnOffer : card.offers[i];
            out << ' ' << o.epoch << ' ' << o.encounter << ' ' << o.state << ' '
                << Hex(o.opponent) << ' ' << Hex(o.name) << ' ' << o.format << ' '
                << Hex(o.secondOpponent) << ' ' << Hex(o.secondName) << ' '
                << Hex(o.thirdOpponent) << ' ' << Hex(o.thirdName);
        }
        return out.str();
    }
    inline bool DecodeLegacy(const std::string& value, Card& card)
    {
        std::istringstream in(value);
        Card parsed;
        int version;
        if (!(in >> version >> parsed.seed) || version < 1 || version > 5) return false;
        if (version >= 5) {
            if (!(in >> parsed.milestones >> parsed.uniqueWins >> parsed.challengeWins) ||
                parsed.milestones > 7 || parsed.uniqueWins > AllUniqueVictories || parsed.challengeWins > 1000000000u) return false;
        } else if (version >= 2 && (!(in >> parsed.milestones >> parsed.challengeWins) ||
            parsed.milestones > 7 || parsed.challengeWins > 1000000000u)) return false;
        if (version >= 4 && (!(in >> parsed.skarnGenerated >> parsed.skarnWon >> parsed.skarnAttempted >>
            parsed.skarnInProgress >> parsed.skarnLastEnd) || !(parsed.skarnLastEnd >= 0.0) ||
            parsed.skarnLastEnd > 24000000.0)) return false;
        for (int i = 0; i < (version >= 4 ? 6 : 5); ++i)
        {
            Offer& o = At(parsed, i);
            int state;
            std::string id, name;
            if (!(in >> o.epoch >> o.encounter >> state >> id >> name) ||
                o.epoch < -2 || o.epoch > 1999999 || o.encounter < 0 || o.encounter > (i == 5 ? EncounterSkarn : EncounterVeyr) ||
                state < 0 || state > 2 || !Unhex(id, o.opponent) || !Unhex(name, o.name)) return false;
            o.state = static_cast<TownArenaPolicy::OfferState>(state);
            if (version >= 3)
            {
                if (!(in >> o.format >> id >> name) || o.format < 0 || o.format > (i == 5 ? 6 : 5) ||
                    !Unhex(id, o.secondOpponent) || !Unhex(name, o.secondName)) return false;
                if (Opponents(o) == 1 && (!o.secondOpponent.empty() || !o.secondName.empty())) return false;
                if (!o.opponent.empty() && o.opponent == o.secondOpponent) return false;
            }
            if (version >= 4)
            {
                if (!(in >> id >> name) || !Unhex(id, o.thirdOpponent) || !Unhex(name, o.thirdName)) return false;
                if (Opponents(o) < 3 && (!o.thirdOpponent.empty() || !o.thirdName.empty())) return false;
                if (!o.thirdOpponent.empty() && (o.thirdOpponent == o.opponent || o.thirdOpponent == o.secondOpponent)) return false;
                if (i == 5 && parsed.skarnGenerated && o.encounter != 5) return false;
            }
            if (i < 5 && o.encounter == EncounterSkarn && !SkarnUnlocked(parsed)) return false;
        }
        std::string extra;
        if (in >> extra) return false;
        card = parsed;
        return true;
    }

    inline void ClearPreview(Offer& o)
    {
        o.context.clear(); o.playerCount = 0; o.enemyCount = 0;
        for (int i = 0; i < 4; ++i) {
            OpponentId(o, i).clear(); OpponentName(o, i).clear(); o.roles[i].clear();
        }
    }
    inline Offer PreviewSource(const Card& card, int slot)
    {
        if (slot < 0 || slot > 5) return Offer();
        Offer source = slot == 5 ? card.skarnOffer : card.offers[slot];
        // A completed lineup was balanced for its original division. Keep the
        // saved source for that view, but never advertise its actors elsewhere.
        if (slot < 5 && source.generated && source.state == TownArenaPolicy::OfferConsumed &&
            source.division != card.division) {
            ClearPreview(source);
            source.division = card.division;
        }
        if (slot < 5 && !source.generated && NamedDivision(source.encounter) >= 0 &&
            !NamedForDivision(source.encounter, card.division)) {
            ClearPreview(source);
            source.generated = true;
            source.division = card.division;
            // Derive the counterpart without advancing or changing the saved card.
            uint32_t seed = card.seed ^ (0x9e3779b9u * static_cast<unsigned>(slot + 1));
            seed ^= seed >> 16;
            seed *= 0x85ebca6bu;
            seed ^= seed >> 13;
            source.draw = seed % 10000;
        }
        return source;
    }
    inline int OfferRarity(const Offer& o, int division)
    {
        if (!o.generated && o.encounter < 3) return o.encounter;
        if (!o.generated && division == 2) return 3;
        return o.draw < 4000 ? 0 : o.draw < 6500 ? 1 : o.draw < 8500 ? 2 : 3;
    }
    inline const char* RarityName(int rarity) {
        return rarity == 0 ? "Common" : rarity == 1 ? "Uncommon" : rarity == 2 ? "Rare" : "Legendary";
    }
    inline const char* DivisionName(int division) {
        return division == 0 ? "Easy" : division == 1 ? "Medium" : "Hard";
    }
    inline int LegendaryEncounter(int division, unsigned namedRoll, unsigned repeatRoll,
        unsigned regularRoll, unsigned uniqueWins, bool skarnUnlocked, bool skarnWon)
    {
        namedRoll %= 8;
        if (namedRoll >= 4) return static_cast<int>(namedRoll % 3);
        int encounter = division == 0 ? EncounterRessaVane :
            division == 1 ? EncounterBumPer :
            namedRoll == 0 ? EncounterSenn :
            namedRoll == 1 ? EncounterTorka :
            namedRoll == 2 || !skarnUnlocked ? EncounterVeyr : EncounterSkarn;
        const unsigned victory = UniqueVictoryForEncounter(encounter);
        const bool defeated = encounter == EncounterSkarn ? skarnWon :
            victory && (uniqueWins & victory) != 0;
        return defeated && (repeatRoll & 1u) ? static_cast<int>(regularRoll % 3) : encounter;
    }
    inline bool UniqueEncounter(int encounter)
    {
        return UniqueVictoryForEncounter(encounter) != 0;
    }
    inline int UniqueEncountersInOffers(const Offer (&offers)[5], int division, int exceptSlot)
    {
        int count = 0;
        for (int i = 0; i < 5; ++i)
            if (i != exceptSlot && offers[i].generated &&
                OfferRarity(offers[i], division) == 3 && UniqueEncounter(offers[i].encounter))
                ++count;
        return count;
    }
    inline int LegendaryEncounter(int division, unsigned namedRoll, bool skarnUnlocked)
    {
        return LegendaryEncounter(division, namedRoll, 0, namedRoll, 0,
            skarnUnlocked, false);
    }
    inline int EncounterForDivision(const Offer& offer, int slot, int division,
        unsigned uniqueWins, bool skarnUnlocked, bool skarnWon)
    {
        const int rarity = OfferRarity(offer, division);
        if (rarity < 3) return rarity;
        uint32_t seed = offer.draw ^ (static_cast<uint32_t>(offer.epoch) * 1664525u) ^
            (static_cast<uint32_t>(slot + 1) * 1013904223u);
        seed ^= seed >> 16; seed *= 0x85ebca6bu; seed ^= seed >> 13;
        return LegendaryEncounter(division, seed % 8, seed >> 16, seed >> 8,
            uniqueWins, skarnUnlocked, skarnWon);
    }
    inline bool UniqueEncounter(int encounter);
    inline int UniqueEncountersInOffers(const Offer (&offers)[5], int division, int exceptSlot);
    inline std::string UnavailableReason(const Offer& offer, bool playersReady,
        bool anchorReady, bool fitsWithoutMedics) {
        const bool named = OfferRarity(offer, offer.division) == 3 &&
            NamedForDivision(offer.encounter, offer.division);
        const std::string prefix = named ? std::string(Title(offer.encounter)) + " | " : "";
        if (!playersReady) return prefix + "Select 1-3 recovered fighters";
        if (named && !anchorReady) return prefix + "Named opponent unavailable or recovering";
        if (fitsWithoutMedics) return prefix + "Waiting for medical capacity";
        if (named) return prefix + "Out of range: no matching team in " + DivisionName(offer.division);
        return "No suitable recovered opponents for this team";
    }
    inline bool Visible(const Card& card, int slot) {
        return slot >= 0 && slot < 5;
    }
    inline bool CanBookDivision(const Card& card, int slot) {
        if (!Visible(card, slot)) return false;
        const Offer& o = slot == 5 ? card.skarnOffer : card.offers[slot];
        if (o.encounter == EncounterSkarn && !SkarnUnlocked(card)) return false;
        if (!o.generated) return NamedDivision(o.encounter) < 0 || NamedForDivision(o.encounter, card.division);
        if (o.division != card.division) return false;
        for (int i = 0; i < Opponents(o); ++i) {
            const int namedDivision = NamedRoleDivision(o.roles[i]);
            if (namedDivision >= 0 && namedDivision != card.division) return false;
        }
        return true;
    }
    inline bool SetDivision(Card& card, int division, bool locked) {
        if (locked || division < 0 || division > 2) return false;
        if (card.division == division) return true;
        card.division = division;
        for (int i = 0; i < 5; ++i)
            if (card.offers[i].generated && card.offers[i].state == TownArenaPolicy::OfferAvailable) {
                ClearPreview(card.offers[i]);
                card.offers[i].division = division;
                card.offers[i].encounter = EncounterForDivision(card.offers[i], i, division,
                    card.uniqueWins, SkarnUnlocked(card), card.skarnWon);
                if (OfferRarity(card.offers[i], division) == 3 && UniqueEncounter(card.offers[i].encounter) &&
                    UniqueEncountersInOffers(card.offers, division, i) > 0)
                    card.offers[i].encounter = EncounterChampion;
            }
        return true;
    }
    inline void RefreshGenerated(Card& card, int epoch, int pinned) {
        if (epoch < -1 || epoch > 1999999) return;
        int lastRefreshed = -1;
        for (int i = 0; i < 5; ++i) {
            Offer& o = card.offers[i];
            if (i == pinned || o.epoch == epoch) continue;
            o = Offer(); o.epoch = epoch; o.generated = true;
            o.draw = Draw(card) % 10000;
            o.mode = (Draw(card) >> 8) % 2;
            o.division = card.division;
            o.encounter = EncounterForDivision(o, i, o.division,
                card.uniqueWins, SkarnUnlocked(card), card.skarnWon);
            if (OfferRarity(o, o.division) == 3 && UniqueEncounter(o.encounter) &&
                UniqueEncountersInOffers(card.offers, o.division, i) > 0)
                o.encounter = EncounterChampion;
            lastRefreshed = i;
        }
        bool hasTeams = false, hasTeams1v1 = false;
        for (int i = 0; i < 5; ++i) {
            hasTeams = hasTeams || card.offers[i].mode == ModeTeams;
            hasTeams1v1 = hasTeams1v1 || card.offers[i].mode == ModeTeams1v1;
        }
        if (lastRefreshed >= 0 && (!hasTeams || !hasTeams1v1))
            card.offers[lastRefreshed].mode = hasTeams ? ModeTeams1v1 : ModeTeams;
    }
    inline void WriteHistory(std::ostream& out, const std::vector<std::vector<std::string> >& history) {
        out << ' ' << history.size();
        for (size_t i = 0; i < history.size(); ++i) {
            out << ' ' << history[i].size();
            for (size_t j = 0; j < history[i].size(); ++j) out << ' ' << Hex(history[i][j]);
        }
    }
    inline bool ReadHistory(std::istream& in, std::vector<std::vector<std::string> >& history) {
        int count;
        if (!(in >> count) || count < 0 || count > 5) return false;
        for (int i = 0; i < count; ++i) {
            int members;
            if (!(in >> members) || members < 1 || members > 8) return false;
            std::vector<std::string> bout;
            for (int j = 0; j < members; ++j) {
                std::string encoded, id;
                if (!(in >> encoded) || !Unhex(encoded, id) || id.empty()) return false;
                for (size_t k = 0; k < bout.size(); ++k) if (bout[k] == id) return false;
                bout.push_back(id);
            }
            history.push_back(bout);
        }
        return true;
    }
    inline std::string Encode(const Card& card) {
        Card carrier = card;
        for (int i = 0; i < 5; ++i)
            if (carrier.offers[i].generated) {
                ClearPreview(carrier.offers[i]); carrier.offers[i].generated = false;
                carrier.offers[i].format = 0;
            }
        std::ostringstream out;
        out << "8 " << EncodeLegacy(carrier) << " MM " << card.division << ' ' << card.ambientSeed;
        for (int i = 0; i < 5; ++i) {
            const Offer& o = card.offers[i];
            out << ' ' << o.generated << ' ' << o.draw << ' ' << o.division << ' '
                << o.playerCount << ' ' << o.enemyCount << ' ' << Hex(o.context);
            for (int j = 0; j < 4; ++j)
                out << ' ' << Hex(OpponentId(o,j)) << ' ' << Hex(OpponentName(o,j)) << ' ' << Hex(o.roles[j]);
        }
        WriteHistory(out, card.playerHistory); WriteHistory(out, card.ambientHistory);
        for (int i = 0; i < 5; ++i) out << ' ' << card.offers[i].mode;
        return out.str();
    }
    inline bool Decode(const std::string& value, Card& card) {
        const bool version5 = value.compare(0, 2, "5 ") == 0;
        const bool version6 = value.compare(0, 2, "6 ") == 0;
        const bool version7 = value.compare(0, 2, "7 ") == 0;
        const bool version8 = value.compare(0, 2, "8 ") == 0;
        if (!version5 && !version6 && !version7 && !version8) return DecodeLegacy(value, card);
        if (value.size() > 100000) return false;
        const size_t marker = value.find(" MM ");
        if (marker == std::string::npos) return false;
        Card parsed;
        if (!DecodeLegacy(value.substr(2, marker - 2), parsed)) return false;
        std::istringstream in(value.substr(marker + 4));
        if (!(in >> parsed.division >> parsed.ambientSeed) || parsed.division < 0 || parsed.division > 2) return false;
        for (int i = 0; i < 5; ++i) {
            Offer& o = parsed.offers[i];
            const Offer legacyOffer = o;
            int generated; std::string context;
            if (!(in >> generated >> o.draw >> o.division >> o.playerCount >> o.enemyCount >> context) ||
                generated < 0 || generated > 1 || o.draw >= 10000 || o.division < 0 || o.division > 2 ||
                o.playerCount < 0 || o.playerCount > 3 || o.enemyCount < 0 || o.enemyCount > (version8 ? 4 : 3) ||
                !Unhex(context, o.context)) return false;
            o.generated = generated != 0;
            for (int j = 0; j < (version8 ? 4 : 3); ++j) {
                std::string id, name, role;
                if (!(in >> id >> name >> role) || !Unhex(id, OpponentId(o,j)) ||
                    !Unhex(name, OpponentName(o,j)) || !Unhex(role, o.roles[j])) return false;
                if (!o.generated && (OpponentId(o,j) != OpponentId(legacyOffer,j) ||
                    OpponentName(o,j) != OpponentName(legacyOffer,j))) return false;
                if (o.generated) {
                    if (j < o.enemyCount && (OpponentId(o,j).empty() || o.roles[j].empty())) return false;
                    if (j >= o.enemyCount && (!OpponentId(o,j).empty() || !OpponentName(o,j).empty() || !o.roles[j].empty())) return false;
                }
                for (int k = 0; k < j; ++k)
                    if (!OpponentId(o,j).empty() && OpponentId(o,j) == OpponentId(o,k)) return false;
            }
            if (o.generated && ((o.playerCount == 0) != (o.enemyCount == 0) ||
                (o.enemyCount > 0 && o.context.empty()))) return false;
            if (o.generated && o.state == TownArenaPolicy::OfferBooked && !Resolved(o)) return false;
        }
        if (!ReadHistory(in, parsed.playerHistory) || !ReadHistory(in, parsed.ambientHistory)) return false;
        if (version6 || version7 || version8)
            for (int i = 0; i < 5; ++i) {
                int savedMode;
                if (!(in >> savedMode) || savedMode < ModeTeams || savedMode > 2) return false;
                // Version 6 briefly shipped FFA as value 2. Retire it without
                // rejecting those saves; the card becomes a normal team fight.
                parsed.offers[i].mode = savedMode == 2 ? ModeTeams : savedMode;
            }
        std::string extra;
        if (in >> extra) return false;
        card = parsed;
        return true;
    }
}
