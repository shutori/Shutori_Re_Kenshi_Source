#include "ArenaSnapshotJson.h"
#include "ArenaPerformance.h"
#include "ArenaRewards.h"
#include "PGConfig.h"
#include "TownFighterCatalog.h"
#include "../third_party/rapidjson/include/rapidjson/document.h"
#include "../third_party/rapidjson/include/rapidjson/error/en.h"
#include "../third_party/rapidjson/include/rapidjson/memorystream.h"
#include "../third_party/rapidjson/include/rapidjson/prettywriter.h"
#include "../third_party/rapidjson/include/rapidjson/stringbuffer.h"
#include <algorithm>
#include <cfloat>
#include <climits>
#include <set>
#include <sstream>

namespace ArenaPersistence {
namespace {
    typedef rapidjson::Value Value;
    typedef rapidjson::PrettyWriter<rapidjson::StringBuffer> Writer;
    // Resource guards, not gameplay limits. The schema itself needs at most 7 levels.
    const size_t kMaxDepth = 32;
    struct Failure {
        LoadCode code;
        std::string field, message;
        Failure(LoadCode c, const std::string& p, const std::string& m) : code(c), field(p), message(m) {}
    };
    void Fail(const std::string& field, const std::string& message) { throw Failure(InvalidData, field, message); }
    std::string Index(const std::string& path, size_t i) { std::ostringstream s; s << path << '[' << i << ']'; return s.str(); }
    std::string Member(const std::string& path, const std::string& key) { return path + "." + key; }

    // Validate while streaming, before allocating a DOM. Iterative parsing prevents
    // recursive stack exhaustion. Keeping every object key catches duplicates before
    // lookup can hide an edit; decoded keys also catch escaped-key aliases.
    struct SyntaxGuard : rapidjson::BaseReaderHandler<rapidjson::UTF8<>, SyntaxGuard> {
        struct Frame {
            std::string path, key;
            bool object;
            size_t index;
            std::set<std::string> keys;
            Frame(const std::string& p, bool o) : path(p), object(o), index(0) {}
        };
        std::vector<Frame> frames;
        std::string errorField, error;
        LoadCode code;
        SyntaxGuard() : code(InvalidJson) {}
        std::string Path() const {
            if (frames.empty()) return "$";
            const Frame& f = frames.back();
            return f.object ? (f.key.empty() ? f.path : Member(f.path, f.key)) : Index(f.path, f.index);
        }
        void Done() { if (!frames.empty()) { ++frames.back().index; frames.back().key.clear(); } }
        bool Stop(LoadCode c, const std::string& p, const char* message) { code=c; errorField=p; error=message; return false; }
        bool Utf8(const char* s, rapidjson::SizeType n) {
            rapidjson::MemoryStream input(s,n);
            while (input.Tell() < n) {
                unsigned cp;
                if (!rapidjson::UTF8<>::Decode(input,&cp) || (cp >= 0xD800 && cp <= 0xDFFF))
                    return Stop(InvalidJson,Path(),"invalid Unicode scalar or UTF-8 encoding");
            }
            return true;
        }
        bool Key(const char* s, rapidjson::SizeType n, bool) {
            if (!Utf8(s,n)) return false;
            Frame& f = frames.back(); f.key.assign(s,n);
            if (!f.keys.insert(f.key).second) return Stop(InvalidData,Path(),"duplicate object member");
            return true;
        }
        bool String(const char* s, rapidjson::SizeType n, bool) { if (!Utf8(s,n)) return false; Done(); return true; }
        bool RawNumber(const char* s, rapidjson::SizeType n, bool) {
            // The DOM conversion can underflow before Number sees the value.
            // Inspect the original mantissa, excluding exponent digits, so exact
            // zero forms such as -0.000e-400 remain valid edits.
            bool nonzero = false, floating = false, exponent = false;
            for (rapidjson::SizeType i=0; i<n; ++i) {
                if (s[i]=='e' || s[i]=='E') { exponent=true; floating=true; }
                else if (s[i]=='.') floating=true;
                else if (!exponent && s[i]>='1' && s[i]<='9') nonzero=true;
            }
            if (nonzero && floating) {
                rapidjson::Document number;
                number.Parse<rapidjson::kParseFullPrecisionFlag>(s,n);
                if (number.HasParseError())
                    return Stop(InvalidJson,Path(),rapidjson::GetParseError_En(number.GetParseError()));
                if (number.GetDouble()==0.0)
                    return Stop(InvalidData,Path(),"nonzero number is too small to represent as a double");
            }
            Done(); return true;
        }
        bool Default() { Done(); return true; }
        bool Start(bool object) {
            if (frames.size() >= kMaxDepth) return Stop(InvalidJson,Path(),"JSON nesting exceeds 32 levels");
            frames.push_back(Frame(Path(),object)); return true;
        }
        bool StartObject() { return Start(true); }
        bool StartArray() { return Start(false); }
        bool EndObject(rapidjson::SizeType) { frames.pop_back(); Done(); return true; }
        bool EndArray(rapidjson::SizeType) { frames.pop_back(); Done(); return true; }
    };
    void Object(const Value& v, const std::string& p, const char* allowed) {
        if (!v.IsObject()) Fail(p,"expected an object");
        const std::string names = std::string("|") + allowed + "|";
        for (Value::ConstMemberIterator i=v.MemberBegin(); i!=v.MemberEnd(); ++i) {
            const std::string key(i->name.GetString(),i->name.GetStringLength());
            if (key.find('\0') != std::string::npos || key.find('|') != std::string::npos || names.find("|"+key+"|") == std::string::npos)
                Fail(Member(p,key),"unknown field");
        }
    }
    const Value& At(const Value& v, const char* key, const std::string& p) {
        Value::ConstMemberIterator i=v.FindMember(key);
        if (i==v.MemberEnd()) Fail(Member(p,key),"required field is missing");
        return i->value;
    }
    void Array(const Value& v, const std::string& p) { if (!v.IsArray()) Fail(p,"expected an array"); }
    std::string String(const Value& v, const std::string& p, bool nonempty=false) {
        if (!v.IsString()) Fail(p,"expected a string");
        const std::string result(v.GetString(),v.GetStringLength());
        if (result.find('\0') != std::string::npos) Fail(p,"embedded NUL is not supported");
        if (nonempty && result.empty()) Fail(p,"expected a nonempty identifier");
        return result;
    }
    std::string Text(const Value& v, const char* key, const std::string& p, bool nonempty=false) { return String(At(v,key,p),Member(p,key),nonempty); }
    int Integer(const Value& v, const std::string& p, int lo, int hi) {
        if (!v.IsInt() || v.GetInt()<lo || v.GetInt()>hi) { std::ostringstream s; s << "expected an integer in [" << lo << ", " << hi << ']'; Fail(p,s.str()); }
        return v.GetInt();
    }
    int Int(const Value& v, const char* key, const std::string& p, int lo, int hi) { return Integer(At(v,key,p),Member(p,key),lo,hi); }
    int OptionalInt(const Value& v, const char* key, const std::string& p, int lo, int hi, int fallback) {
        Value::ConstMemberIterator i=v.FindMember(key);
        return i==v.MemberEnd() ? fallback : Integer(i->value,Member(p,key),lo,hi);
    }
    unsigned Uint(const Value& v, const char* key, const std::string& p, unsigned hi) {
        const Value& n=At(v,key,p);
        if (!n.IsUint() || n.GetUint()>hi) { std::ostringstream s; s << "expected an unsigned integer in [0, " << hi << ']'; Fail(Member(p,key),s.str()); }
        return n.GetUint();
    }
    unsigned OptionalUint(const Value& v, const char* key, const std::string& p, unsigned hi, unsigned fallback) {
        Value::ConstMemberIterator i=v.FindMember(key);
        if (i==v.MemberEnd()) return fallback;
        if (!i->value.IsUint() || i->value.GetUint()>hi) { std::ostringstream s; s << "expected an unsigned integer in [0, " << hi << ']'; Fail(Member(p,key),s.str()); }
        return i->value.GetUint();
    }
    double Number(const Value& v, const char* key, const std::string& p, double hi) {
        const Value& n=At(v,key,p);
        if (!n.IsNumber() || !(n.GetDouble()>=0.0) || n.GetDouble()>hi) { std::ostringstream s; s << "expected a finite number in [0, " << hi << ']'; Fail(Member(p,key),s.str()); }
        return n.GetDouble();
    }
    bool Bool(const Value& v, const char* key, const std::string& p) {
        const Value& b=At(v,key,p); if (!b.IsBool()) Fail(Member(p,key),"expected a boolean"); return b.GetBool();
    }
    void BoolArray(const Value& v, const std::string& p, bool* out, int count) {
        Array(v,p); if (static_cast<int>(v.Size())!=count) Fail(p,"unexpected array length");
        for (int i=0;i<count;++i) { if (!v[i].IsBool()) Fail(Index(p,i),"expected a boolean"); out[i]=v[i].GetBool(); }
    }
    void IntArray(const Value& v, const std::string& p, int* out, int count) {
        Array(v,p); if (static_cast<int>(v.Size())!=count) Fail(p,"unexpected array length");
        for (int i=0;i<count;++i) out[i]=Integer(v[i],Index(p,i),0,INT_MAX);
    }
    void NumberArray(const Value& v, const std::string& p, double* out, int count, double hi) {
        Array(v,p); if (static_cast<int>(v.Size())!=count) Fail(p,"unexpected array length");
        for (int i=0;i<count;++i) {
            if (!v[i].IsNumber() || !(v[i].GetDouble()>=0.0) || v[i].GetDouble()>hi) Fail(Index(p,i),"expected a finite nonnegative number");
            out[i]=v[i].GetDouble();
        }
    }
    void Unique(std::set<std::string>& seen, const std::string& id, const std::string& p) { if (!seen.insert(id).second) Fail(p,"duplicate identifier"); }
    void Standings(const Value& list, const std::string& p, std::vector<LeaderboardData::Standing>& rows) {
        Array(list,p); std::set<std::string> seen;
        for (rapidjson::SizeType i=0; i<list.Size(); ++i) {
            const Value& v=list[i]; const std::string row=Index(p,i);
            Object(v,row,"id|name|mmr|wins|losses|matches");
            LeaderboardData::Standing s; s.id=Text(v,"id",row,true); Unique(seen,s.id,Member(row,"id"));
            s.name=Text(v,"name",row); const double rating=Number(v,"mmr",row,FLT_MAX); s.mmr=static_cast<float>(rating);
            if (rating>0.0 && s.mmr==0.0f) Fail(Member(row,"mmr"),"positive rating is too small to represent as a float");
            s.wins=Int(v,"wins",row,0,INT_MAX); s.losses=Int(v,"losses",row,0,INT_MAX); s.matches=Int(v,"matches",row,0,INT_MAX);
            rows.push_back(s);
        }
    }
    ArenaRewards::Piece Reward(const std::string& id, const std::string& p) {
        for (int i=0; i<ArenaRewards::PieceCount; ++i)
            if (id==ArenaRewards::StableId(static_cast<ArenaRewards::Piece>(i))) return static_cast<ArenaRewards::Piece>(i);
        Fail(p,"unknown reward ID"); return ArenaRewards::PieceCount;
    }
    bool KnownUnlock(const std::string& id) {
        if (id==ArenaRewards::GetWeaponLicence().stableId) return true;
        for (int i=0; i<ArenaRewards::ArmourSetNone; ++i)
            if (id==ArenaRewards::GetLicence(static_cast<ArenaRewards::ArmourSet>(i)).stableId) return true;
        for (int i=0; i<ArenaRewards::GeneralItemCount; ++i) {
            const char* unlock=ArenaRewards::GetGeneral(static_cast<ArenaRewards::GeneralItem>(i)).grantsUnlockId;
            if (unlock && id==unlock) return true;
        }
        return false;
    }
    void Progression(const Value& list, const std::string& p, std::vector<LeaderboardData::Progression>& rows) {
        Array(list,p); std::set<std::string> seen;
        for (rapidjson::SizeType i=0; i<list.Size(); ++i) {
            const Value& v=list[i]; const std::string row=Index(p,i);
            Object(v,row,"id|name|marks|rewardTiers");
            LeaderboardData::Progression s; s.id=Text(v,"id",row,true); Unique(seen,s.id,Member(row,"id"));
            s.name=Text(v,"name",row); s.marks=Int(v,"marks",row,0,INT_MAX);
            const Value& tiers=At(v,"rewardTiers",row); const std::string tp=Member(row,"rewardTiers");
            if (!tiers.IsObject()) Fail(tp,"expected an object mapping reward IDs to numeric tiers");
            for (Value::ConstMemberIterator t=tiers.MemberBegin(); t!=tiers.MemberEnd(); ++t) {
                const std::string id=String(t->name,tp,true), path=Member(tp,id);
                const ArenaRewards::Piece piece=Reward(id,path);
                const int tier=Integer(t->value,path,0,ArenaRewards::TierMaxCount-1);
                if (!ArenaRewards::IsValidTier(piece,static_cast<ArenaRewards::Tier>(tier))) Fail(path,"tier is not supported for this reward");
                s.rewardTiers.push_back(RewardProgression::TierEntry(id,tier));
            }
            rows.push_back(s);
        }
    }
    void History(const Value& list, const std::string& p, std::vector<std::vector<std::string> >& history) {
        Array(list,p); if (list.Size()>5) Fail(p,"at most 5 recent bouts are supported");
        for (rapidjson::SizeType i=0; i<list.Size(); ++i) {
            const std::string row=Index(p,i); Array(list[i],row);
            if (list[i].Empty() || list[i].Size()>8) Fail(row,"a bout must contain 1 to 8 opponent IDs");
            std::set<std::string> seen; std::vector<std::string> ids;
            for (rapidjson::SizeType j=0; j<list[i].Size(); ++j) { const std::string id=String(list[i][j],Index(row,j),true); Unique(seen,id,Index(row,j)); ids.push_back(id); }
            history.push_back(ids);
        }
    }
    bool KnownRole(const std::string& id) {
        if (TownFighterCatalog::Find(id.c_str())) return true;
        for (int i=0; i<=TownChallengePolicy::EncounterVeyr; ++i) if (id==TownChallengePolicy::Role(i)) return true;
        return false;
    }
    void Offer(const Value& v, const std::string& p, bool skarn, TownChallengePolicy::Offer& o) {
        Object(v,p,"epoch|encounter|format|mode|state|generated|draw|division|playerCount|enemyCount|context|opponents");
        o.epoch=Int(v,"epoch",p,-2,1999999);
        o.encounter=Int(v,"encounter",p,0,skarn?TownChallengePolicy::EncounterSkarn:TownChallengePolicy::EncounterVeyr);
        o.format=Int(v,"format",p,0,skarn?6:5);
        const int savedMode=OptionalInt(v,"mode",p,0,2,TownChallengePolicy::ModeTeams);
        o.mode=savedMode==2 ? TownChallengePolicy::ModeTeams : savedMode;
        o.state=static_cast<TownArenaPolicy::OfferState>(Int(v,"state",p,0,2)); o.generated=Bool(v,"generated",p);
        o.draw=Uint(v,"draw",p,9999); o.division=Int(v,"division",p,0,2);
        o.playerCount=Int(v,"playerCount",p,0,3); o.enemyCount=Int(v,"enemyCount",p,0,4); o.context=Text(v,"context",p);
        const Value& opponents=At(v,"opponents",p); const std::string op=Member(p,"opponents"); Array(opponents,op);
        if (opponents.Size()!=3 && opponents.Size()!=4) Fail(op,"expected 3 or 4 opponent slots");
        if (o.enemyCount>static_cast<int>(opponents.Size())) Fail(Member(p,"enemyCount"),"opponent count exceeds stored opponent slots");
        std::set<std::string> seen;
        for (int i=0; i<static_cast<int>(opponents.Size()); ++i) {
            const std::string slot=Index(op,i); const Value& member=opponents[i]; Object(member,slot,"id|name|role");
            std::string& id=TownChallengePolicy::OpponentId(o,i); std::string& name=TownChallengePolicy::OpponentName(o,i);
            id=Text(member,"id",slot); name=Text(member,"name",slot); o.roles[i]=Text(member,"role",slot);
            if (!id.empty()) Unique(seen,id,Member(slot,"id"));
            if (!o.roles[i].empty() && !KnownRole(o.roles[i])) Fail(Member(slot,"role"),"unknown opponent role ID");
            if (o.generated) {
                if (i<o.enemyCount && (id.empty() || o.roles[i].empty())) Fail(slot,"generated opponent requires an ID and role");
                if (i>=o.enemyCount && (!id.empty() || !name.empty() || !o.roles[i].empty())) Fail(slot,"unused generated opponent slot must be empty");
            } else if (i>=TownChallengePolicy::Opponents(o) && (!id.empty() || !name.empty())) Fail(slot,"opponent slot is unused by this format");
        }
        if (o.generated && (o.playerCount==0)!=(o.enemyCount==0)) Fail(Member(p,"playerCount"),"generated team counts must both be zero or both be positive");
        if (o.generated && o.enemyCount>0 && o.context.empty()) Fail(Member(p,"context"),"resolved generated offer requires matchup context");
        if (o.generated && o.state==TownArenaPolicy::OfferBooked && !TownChallengePolicy::Resolved(o)) Fail(p,"booked generated offer requires a complete lineup");
    }
    void Card(const Value& v, const std::string& p, TownChallengePolicy::Card& c) {
        Object(v,p,"seed|ambientSeed|division|milestones|uniqueWins|challengeWins|playerHistory|ambientHistory|offers|skarnOffer|skarnGenerated|skarnWon|skarnAttempted|skarnInProgress|skarnLastEnd|lastChallengeEndHours|hasChallengeEnd|paidRefreshCount|paidRefreshNonce");
        c.seed=Uint(v,"seed",p,UINT_MAX); c.ambientSeed=Uint(v,"ambientSeed",p,UINT_MAX); c.division=Int(v,"division",p,0,2);
        c.milestones=Uint(v,"milestones",p,7); c.uniqueWins=OptionalUint(v,"uniqueWins",p,TownChallengePolicy::AllUniqueVictories,0); c.challengeWins=Uint(v,"challengeWins",p,1000000000u);
        History(At(v,"playerHistory",p),Member(p,"playerHistory"),c.playerHistory); History(At(v,"ambientHistory",p),Member(p,"ambientHistory"),c.ambientHistory);
        const Value& offers=At(v,"offers",p); const std::string op=Member(p,"offers"); Array(offers,op);
        if (offers.Size()!=5) Fail(op,"expected 5 regular challenge offers");
        for (int i=0; i<5; ++i) Offer(offers[i],Index(op,i),false,c.offers[i]);
        for (int i=0; i<5; ++i)
            if (c.offers[i].encounter==TownChallengePolicy::EncounterSkarn && !TownChallengePolicy::SkarnUnlocked(c))
                Fail(Member(Index(op,i),"encounter"),"Skarn requires all five unique-fighter victories");
        Offer(At(v,"skarnOffer",p),Member(p,"skarnOffer"),true,c.skarnOffer);
        c.skarnGenerated=Bool(v,"skarnGenerated",p); c.skarnWon=Bool(v,"skarnWon",p); c.skarnAttempted=Bool(v,"skarnAttempted",p); c.skarnInProgress=Bool(v,"skarnInProgress",p);
        c.skarnLastEnd=Number(v,"skarnLastEnd",p,24000000.0);
        c.lastChallengeEndHours=v.HasMember("lastChallengeEndHours") ?
            Number(v,"lastChallengeEndHours",p,24000000.0) : 0.0;
        c.hasChallengeEnd=v.HasMember("hasChallengeEnd") ? Bool(v,"hasChallengeEnd",p) : false;
        c.paidRefreshCount=v.HasMember("paidRefreshCount") ? Int(v,"paidRefreshCount",p,0,INT_MAX) : 0;
        c.paidRefreshNonce=OptionalUint(v,"paidRefreshNonce",p,UINT_MAX,1u);
        if (c.skarnGenerated && c.skarnOffer.encounter!=5) Fail(Member(p,"skarnOffer.encounter"),"generated Skarn offer must refer to Skarn (5)");
    }
    void Diagnostic(const Value& v, const std::string& p, TownDiagnosticData::State& d) {
        Object(v,p,"status|runId|nextSequence|config|counters|pending");
        d.status=Int(v,"status",p,TownDiagnosticData::Off,TownDiagnosticData::Ended);
        d.runId=Uint(v,"runId",p,UINT_MAX); d.nextSequence=Uint(v,"nextSequence",p,UINT_MAX);
        const Value& cfg=At(v,"config",p); const std::string cp=Member(p,"config");
        Object(cfg,cp,"target|models|coverage|reducedInfluence|underdogPercent|balanceStyleTier");
        d.config.target=Int(cfg,"target",cp,1,10000);
        BoolArray(At(cfg,"models",cp),Member(cp,"models"),d.config.models,TownDiagnosticData::ModelCount);
        BoolArray(At(cfg,"coverage",cp),Member(cp,"coverage"),d.config.coverage,TownDiagnosticData::CoverageCount);
        d.config.reducedInfluence=Number(cfg,"reducedInfluence",cp,1.0);
        d.config.underdogPercent=Int(cfg,"underdogPercent",cp,0,100);
        d.config.balanceStyleTier=Bool(cfg,"balanceStyleTier",cp);
        bool anyModel=false, anyCoverage=false;
        for (int i=0;i<TownDiagnosticData::ModelCount;++i) anyModel=anyModel||d.config.models[i];
        for (int i=0;i<TownDiagnosticData::CoverageCount;++i) anyCoverage=anyCoverage||d.config.coverage[i];
        if (!anyModel) Fail(Member(cp,"models"),"at least one model must be enabled");
        if (!anyCoverage) Fail(Member(cp,"coverage"),"at least one coverage bucket must be enabled");
        const Value& c=At(v,"counters",p); const std::string xp=Member(p,"counters");
        Object(c,xp,"opened|completed|cancelled|aborted|teamAWins|teamBWins|largerWins|smallerWins|equalWins|favoriteWins|modelCompleted|coverageCompleted|schedulingHours|lastCompletedHours|schedulingSamples");
        d.counters.opened=Int(c,"opened",xp,0,INT_MAX); d.counters.completed=Int(c,"completed",xp,0,INT_MAX);
        d.counters.cancelled=Int(c,"cancelled",xp,0,INT_MAX); d.counters.aborted=Int(c,"aborted",xp,0,INT_MAX);
        d.counters.teamAWins=Int(c,"teamAWins",xp,0,INT_MAX); d.counters.teamBWins=Int(c,"teamBWins",xp,0,INT_MAX);
        d.counters.largerWins=Int(c,"largerWins",xp,0,INT_MAX); d.counters.smallerWins=Int(c,"smallerWins",xp,0,INT_MAX);
        d.counters.equalWins=Int(c,"equalWins",xp,0,INT_MAX); d.counters.favoriteWins=Int(c,"favoriteWins",xp,0,INT_MAX);
        IntArray(At(c,"modelCompleted",xp),Member(xp,"modelCompleted"),d.counters.modelCompleted,TownDiagnosticData::ModelCount);
        IntArray(At(c,"coverageCompleted",xp),Member(xp,"coverageCompleted"),d.counters.coverageCompleted,TownDiagnosticData::CoverageCount);
        d.counters.schedulingHours=Number(c,"schedulingHours",xp,DBL_MAX);
        d.counters.lastCompletedHours=Number(c,"lastCompletedHours",xp,24000000.0);
        d.counters.schedulingSamples=Int(c,"schedulingSamples",xp,0,INT_MAX);
        const Value& q=At(v,"pending",p); const std::string qp=Member(p,"pending");
        Object(q,qp,"active|underdog|sequence|model|requestedCoverage|actualCoverage|relaxation|sizeA|sizeB|scoreA|scoreB|probabilityA|announcedHours");
        d.pending.active=Bool(q,"active",qp); d.pending.underdog=Bool(q,"underdog",qp);
        d.pending.sequence=Uint(q,"sequence",qp,UINT_MAX); d.pending.model=Int(q,"model",qp,0,TownDiagnosticData::ModelCount-1);
        d.pending.requestedCoverage=Int(q,"requestedCoverage",qp,0,TownDiagnosticData::CoverageCount-1);
        d.pending.actualCoverage=Int(q,"actualCoverage",qp,0,TownDiagnosticData::CoverageCount-1);
        d.pending.relaxation=Int(q,"relaxation",qp,0,3); d.pending.sizeA=Int(q,"sizeA",qp,0,4); d.pending.sizeB=Int(q,"sizeB",qp,0,4);
        NumberArray(At(q,"scoreA",qp),Member(qp,"scoreA"),d.pending.scoreA,TownDiagnosticData::ModelCount,DBL_MAX);
        NumberArray(At(q,"scoreB",qp),Member(qp,"scoreB"),d.pending.scoreB,TownDiagnosticData::ModelCount,DBL_MAX);
        NumberArray(At(q,"probabilityA",qp),Member(qp,"probabilityA"),d.pending.probabilityA,TownDiagnosticData::ModelCount,1.0);
        d.pending.announcedHours=Number(q,"announcedHours",qp,24000000.0);
        if (d.pending.active && (d.status!=TownDiagnosticData::Active || !d.pending.sequence)) Fail(qp,"pending assignment requires an active run and nonzero sequence");
        if (d.counters.completed>d.config.target) Fail(Member(xp,"completed"),"completed fights exceed target");
    }
    void Read(const Value& v, int sourceVersion, Snapshot& s) {
        const char* const fields = sourceVersion >= 15 ?
            "version|saveKey|generation|playerStandings|townStandings|progression|factionUnlocks|challenges|diagnostic|bookieCredit|challengeCredit" :
            sourceVersion >= 12 ?
            "version|saveKey|generation|performanceProfile|performanceWelcomeSeen|playerStandings|townStandings|progression|factionUnlocks|challenges|diagnostic|bookieCredit|challengeCredit" :
            "version|saveKey|generation|playerStandings|townStandings|progression|factionUnlocks|challenges|diagnostic|bookieCredit|challengeCredit";
        Object(v,"$",fields);
        s.saveKey=Text(v,"saveKey","$",true); s.generation=Text(v,"generation","$",true);
        if (sourceVersion >= 12 && sourceVersion < 15) {
            const std::string value=Text(v,"performanceProfile","$",true);
            ArenaPerformance::Profile legacyProfile = ArenaPerformance::High;
            if (!ArenaPerformance::Parse(value,legacyProfile))
                Fail("$.performanceProfile","expected high, normal, or potato");
            (void)Bool(v,"performanceWelcomeSeen","$");
        }
        Standings(At(v,"playerStandings","$"),"$.playerStandings",s.fighters.playerStandings);
        Standings(At(v,"townStandings","$"),"$.townStandings",s.fighters.townStandings);
        Progression(At(v,"progression","$"),"$.progression",s.fighters.progression);
        const Value& unlocks=At(v,"factionUnlocks","$"); Array(unlocks,"$.factionUnlocks"); std::set<std::string> seen;
        for (rapidjson::SizeType i=0; i<unlocks.Size(); ++i) {
            const std::string p=Index("$.factionUnlocks",i), id=String(unlocks[i],p,true); Unique(seen,id,p);
            if (!KnownUnlock(id)) Fail(p,"unknown faction unlock ID"); s.fighters.factionUnlocks.push_back(id);
        }
        Card(At(v,"challenges","$"),"$.challenges",s.challenges);
        try {
            Diagnostic(At(v,"diagnostic","$"),"$.diagnostic",s.diagnostic);
        } catch (const Failure& f) {
            if (f.field.find("$.diagnostic") != 0) throw;
            s.diagnostic=TownDiagnosticData::State();
            s.diagnosticError=f.field+": "+f.message;
        }
        // A bookie credit is one pending winning payout, so its ceiling follows
        // the configured stake range: a raised maximum must not let the writer
        // produce a value this reader rejects, because rejecting it discards the
        // save's whole arena progression. The challenge credit ceiling is the
        // quote's own payout cap (TownChallengeBuyInPolicy::Quote::Valid).
        s.bookieCredit=Int(v,"bookieCredit","$",0,PGConfig::BookieCreditCeiling()); s.challengeCredit=Int(v,"challengeCredit","$",0,20000);
    }
    void Str(Writer& w, const std::string& s) { w.String(s.c_str(),static_cast<rapidjson::SizeType>(s.size())); }
    void Text(Writer& w, const char* key, const std::string& s) { w.Key(key); Str(w,s); }
    void Int(Writer& w, const char* key, int n) { w.Key(key); w.Int(n); }
    void Uint(Writer& w, const char* key, unsigned n) { w.Key(key); w.Uint(n); }
    void Bool(Writer& w, const char* key, bool b) { w.Key(key); w.Bool(b); }
    void Number(Writer& w, const char* key, double n, const std::string& p) { w.Key(key); if (!w.Double(n)) Fail(p,"expected a finite number"); }
    template<class T> struct ById { bool operator()(const T& a, const T& b) const { return a.id<b.id; } };
    void BoolArray(Writer& w,const char* key,const bool* values,int count) { w.Key(key); w.StartArray(); for(int i=0;i<count;++i) w.Bool(values[i]); w.EndArray(); }
    void IntArray(Writer& w,const char* key,const int* values,int count) { w.Key(key); w.StartArray(); for(int i=0;i<count;++i) w.Int(values[i]); w.EndArray(); }
    void NumberArray(Writer& w,const char* key,const double* values,int count,const std::string& p) { w.Key(key); w.StartArray(); for(int i=0;i<count;++i) { if(!w.Double(values[i])) Fail(Index(p,i),"expected a finite number"); } w.EndArray(); }
    void Standings(Writer& w, const char* key, std::vector<LeaderboardData::Standing> rows) {
        std::sort(rows.begin(),rows.end(),ById<LeaderboardData::Standing>()); w.Key(key); w.StartArray();
        for (size_t i=0; i<rows.size(); ++i) {
            const LeaderboardData::Standing& s=rows[i]; w.StartObject(); Text(w,"id",s.id); Text(w,"name",s.name);
            Number(w,"mmr",s.mmr,Member(Index(Member("$",key),i),"mmr")); Int(w,"wins",s.wins); Int(w,"losses",s.losses); Int(w,"matches",s.matches); w.EndObject();
        }
        w.EndArray();
    }
    void History(Writer& w, const char* key, const std::vector<std::vector<std::string> >& history) {
        w.Key(key); w.StartArray(); for (size_t i=0; i<history.size(); ++i) { w.StartArray(); for (size_t j=0; j<history[i].size(); ++j) Str(w,history[i][j]); w.EndArray(); } w.EndArray();
    }
    void Offer(Writer& w, const TownChallengePolicy::Offer& o) {
        w.StartObject(); Int(w,"epoch",o.epoch); Int(w,"encounter",o.encounter); Int(w,"format",o.format); Int(w,"mode",o.mode); Int(w,"state",o.state);
        Bool(w,"generated",o.generated); Uint(w,"draw",o.draw); Int(w,"division",o.division); Int(w,"playerCount",o.playerCount); Int(w,"enemyCount",o.enemyCount); Text(w,"context",o.context);
        w.Key("opponents"); w.StartArray(); for (int i=0; i<4; ++i) { w.StartObject(); Text(w,"id",TownChallengePolicy::OpponentId(o,i)); Text(w,"name",TownChallengePolicy::OpponentName(o,i)); Text(w,"role",o.roles[i]); w.EndObject(); } w.EndArray(); w.EndObject();
    }
    void Diagnostic(Writer& w,const TownDiagnosticData::State& d) {
        w.Key("diagnostic"); w.StartObject(); Int(w,"status",d.status); Uint(w,"runId",d.runId); Uint(w,"nextSequence",d.nextSequence);
        w.Key("config"); w.StartObject(); Int(w,"target",d.config.target); BoolArray(w,"models",d.config.models,TownDiagnosticData::ModelCount);
        BoolArray(w,"coverage",d.config.coverage,TownDiagnosticData::CoverageCount); Number(w,"reducedInfluence",d.config.reducedInfluence,"$.diagnostic.config.reducedInfluence");
        Int(w,"underdogPercent",d.config.underdogPercent); Bool(w,"balanceStyleTier",d.config.balanceStyleTier); w.EndObject();
        w.Key("counters"); w.StartObject(); Int(w,"opened",d.counters.opened); Int(w,"completed",d.counters.completed); Int(w,"cancelled",d.counters.cancelled); Int(w,"aborted",d.counters.aborted);
        Int(w,"teamAWins",d.counters.teamAWins); Int(w,"teamBWins",d.counters.teamBWins); Int(w,"largerWins",d.counters.largerWins); Int(w,"smallerWins",d.counters.smallerWins);
        Int(w,"equalWins",d.counters.equalWins); Int(w,"favoriteWins",d.counters.favoriteWins); IntArray(w,"modelCompleted",d.counters.modelCompleted,TownDiagnosticData::ModelCount);
        IntArray(w,"coverageCompleted",d.counters.coverageCompleted,TownDiagnosticData::CoverageCount); Number(w,"schedulingHours",d.counters.schedulingHours,"$.diagnostic.counters.schedulingHours");
        Number(w,"lastCompletedHours",d.counters.lastCompletedHours,"$.diagnostic.counters.lastCompletedHours");
        Int(w,"schedulingSamples",d.counters.schedulingSamples); w.EndObject();
        w.Key("pending"); w.StartObject(); Bool(w,"active",d.pending.active); Bool(w,"underdog",d.pending.underdog); Uint(w,"sequence",d.pending.sequence);
        Int(w,"model",d.pending.model); Int(w,"requestedCoverage",d.pending.requestedCoverage); Int(w,"actualCoverage",d.pending.actualCoverage); Int(w,"relaxation",d.pending.relaxation);
        Int(w,"sizeA",d.pending.sizeA); Int(w,"sizeB",d.pending.sizeB); NumberArray(w,"scoreA",d.pending.scoreA,TownDiagnosticData::ModelCount,"$.diagnostic.pending.scoreA");
        NumberArray(w,"scoreB",d.pending.scoreB,TownDiagnosticData::ModelCount,"$.diagnostic.pending.scoreB"); NumberArray(w,"probabilityA",d.pending.probabilityA,TownDiagnosticData::ModelCount,"$.diagnostic.pending.probabilityA");
        Number(w,"announcedHours",d.pending.announcedHours,"$.diagnostic.pending.announcedHours"); w.EndObject(); w.EndObject();
    }
    void Write(Writer& w, const Snapshot& s) {
        w.SetIndent(' ',2); w.StartObject(); Int(w,"version",s.version); Text(w,"saveKey",s.saveKey); Text(w,"generation",s.generation);
        Standings(w,"playerStandings",s.fighters.playerStandings); Standings(w,"townStandings",s.fighters.townStandings);
        std::vector<LeaderboardData::Progression> progression=s.fighters.progression; std::sort(progression.begin(),progression.end(),ById<LeaderboardData::Progression>());
        w.Key("progression"); w.StartArray();
        for (size_t i=0; i<progression.size(); ++i) {
            LeaderboardData::Progression& p=progression[i]; w.StartObject(); Text(w,"id",p.id); Text(w,"name",p.name); Int(w,"marks",p.marks); w.Key("rewardTiers"); w.StartObject();
            std::sort(p.rewardTiers.begin(),p.rewardTiers.end(),ById<RewardProgression::TierEntry>());
            for (size_t j=0; j<p.rewardTiers.size(); ++j) { w.Key(p.rewardTiers[j].id.c_str(),static_cast<rapidjson::SizeType>(p.rewardTiers[j].id.size())); w.Int(p.rewardTiers[j].tier); }
            w.EndObject(); w.EndObject();
        }
        w.EndArray(); std::vector<std::string> unlocks=s.fighters.factionUnlocks; std::sort(unlocks.begin(),unlocks.end()); w.Key("factionUnlocks"); w.StartArray();
        for (size_t i=0; i<unlocks.size(); ++i) Str(w,unlocks[i]); w.EndArray();
        const TownChallengePolicy::Card& c=s.challenges; w.Key("challenges"); w.StartObject(); Uint(w,"seed",c.seed); Uint(w,"ambientSeed",c.ambientSeed); Int(w,"division",c.division); Uint(w,"milestones",c.milestones); Uint(w,"uniqueWins",c.uniqueWins); Uint(w,"challengeWins",c.challengeWins);
        History(w,"playerHistory",c.playerHistory); History(w,"ambientHistory",c.ambientHistory);
        w.Key("offers"); w.StartArray(); for (int i=0; i<5; ++i) Offer(w,c.offers[i]); w.EndArray(); w.Key("skarnOffer"); Offer(w,c.skarnOffer);
        Bool(w,"skarnGenerated",c.skarnGenerated); Bool(w,"skarnWon",c.skarnWon); Bool(w,"skarnAttempted",c.skarnAttempted); Bool(w,"skarnInProgress",c.skarnInProgress); Number(w,"skarnLastEnd",c.skarnLastEnd,"$.challenges.skarnLastEnd"); Number(w,"lastChallengeEndHours",c.lastChallengeEndHours,"$.challenges.lastChallengeEndHours"); Bool(w,"hasChallengeEnd",c.hasChallengeEnd); Int(w,"paidRefreshCount",c.paidRefreshCount); Uint(w,"paidRefreshNonce",c.paidRefreshNonce); w.EndObject();
        Diagnostic(w,s.diagnostic); Int(w,"bookieCredit",s.bookieCredit); Int(w,"challengeCredit",s.challengeCredit); w.EndObject();
    }
}
LoadResult DecodeSnapshot(const std::string& json) {
    LoadResult result;
    try {
        if (json.size()>kMaxSnapshotBytes) throw Failure(InvalidJson,"$","JSON exceeds the 16 MiB safety limit");
        if (json.find('\0')!=std::string::npos) throw Failure(InvalidJson,"$","unescaped NUL byte in JSON");
        rapidjson::MemoryStream input(json.data(),json.size()); rapidjson::Reader reader; SyntaxGuard guard;
        const unsigned flags=rapidjson::kParseValidateEncodingFlag | rapidjson::kParseFullPrecisionFlag | rapidjson::kParseIterativeFlag;
        if (!reader.Parse<flags | rapidjson::kParseNumbersAsStringsFlag>(input,guard)) {
            if (!guard.error.empty()) throw Failure(guard.code,guard.errorField,guard.error);
            std::ostringstream message; message << rapidjson::GetParseError_En(reader.GetParseErrorCode()) << " at byte " << reader.GetErrorOffset();
            throw Failure(InvalidJson,guard.Path(),message.str());
        }
        rapidjson::Document doc; doc.Parse<flags>(json.data(),json.size());
        if (doc.HasParseError()) throw Failure(InvalidJson,"$",rapidjson::GetParseError_En(doc.GetParseError()));
        if (!doc.IsObject()) Fail("$","expected a snapshot object");
        const int version=Int(doc,"version","$",0,INT_MAX);
        if (version<11) { result.code=LegacyReset; result.field="$.version"; result.message="legacy arena progression must reset; preserve the original sidecar"; return result; }
        if (version>15) { result.code=UnsupportedVersion; result.field="$.version"; result.message="snapshot schema is newer than supported version 15"; return result; }
        Snapshot parsed; Read(doc,version,parsed); result.snapshot=parsed; result.code=Ready;
    } catch (const Failure& f) { result.code=f.code; result.field=f.field; result.message=f.message; }
    return result;
}
bool EncodeSnapshot(const Snapshot& value, std::string& json, std::string& error) {
    try {
        if (value.version!=15) Fail("$.version","only schema version 15 can be written");
        rapidjson::StringBuffer buffer; Writer writer(buffer); Write(writer,value);
        std::string encoded(buffer.GetString(),buffer.GetSize()); encoded += '\n';
        // One validation contract for edited files and runtime snapshots. Never
        // publish malformed live state, including duplicate IDs or tier entries.
        const LoadResult checked=DecodeSnapshot(encoded);
        if (checked.code!=Ready || !checked.snapshot.diagnosticError.empty()) {
            error=checked.code!=Ready ? checked.field+": "+checked.message : checked.snapshot.diagnosticError;
            return false;
        }
        json.swap(encoded); error.clear(); return true;
    } catch (const Failure& f) { error=f.field+": "+f.message; return false; }
}
}
