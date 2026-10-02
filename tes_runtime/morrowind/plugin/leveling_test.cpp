// Headless gate for leveling.cpp: skill increases credited to the governing
// attribute, level-ups queued, the step's multipliers and caps. The engine is
// three fake hooks; the SKIL and GMST rows are Morrowind.esm's, in testdata.
// See: docs/commentary/morrowind_runtime.md#leveling

#include <windows.h>

#include <cstdio>
#include <map>
#include <string>

#include "actor_stats.h"
#include "attribute_buffs.h"
#include "dialogue_state.h"
#include "leveling.h"
#include "script_tables.h"

using namespace tesruntime::mw;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::map<std::string, float> g_base;
int g_level = 1;
int g_raceA = 0, g_raceB = 0;
const void* g_race = &g_raceA;

float FakeBase(const std::string&, const char* name) {
    const auto found = g_base.find(name);
    return found == g_base.end() ? 15.0f : found->second;
}

void FakeSet(const std::string&, const char* name, float value) { g_base[name] = value; }

float FakeValue(const std::string& actor, const char* name) { return FakeBase(actor, name); }

float g_magic[8] = {};
float FakeMagic(const std::string&, int attribute) { return g_magic[attribute]; }

// Whether the fake engine's base of `name` is `want`, read without adding it.
bool BaseIs(const char* name, float want) {
    const float base = FakeBase("player", name);
    return base > want - 0.01f && base < want + 0.01f;
}

int FakeLevel() { return g_level; }
const void* FakeRace(const std::string&) { return g_race; }

std::string FixtureDir() {
    char exe[MAX_PATH] = {0};
    const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string path(exe, n);
    return path.substr(0, path.find_last_of('\\') + 1) + "testdata\\scripts\\";
}

// TES3 attribute indices, as SKIL rows name them.
constexpr int kStrength = 0, kIntelligence = 1, kWillpower = 2, kAgility = 3;
constexpr int kEndurance = 5, kPersonality = 6, kLuck = 7;
constexpr int kCombat = 0, kStealth = 2;

void CreditCases() {
    std::printf("the first read only records\n");
    SampleLeveling();
    Check(PendingLevelUps() == 0 && AttributeIncreases(kStrength) == 0,
          "nothing credited, nothing pending");

    std::printf("an increase goes to the namesake's governing attribute\n");
    g_base["OneHanded"] = 17.0f;
    g_base["Pickpocket"] = 16.0f;
    SampleLeveling();
    Check(AttributeIncreases(kStrength) == 2, "One-Handed +2 is Long Blade's Strength");
    Check(AttributeIncreases(kAgility) == 1, "Pickpocket +1 is Sneak's Agility");
    Check(SpecializationIncreases(kCombat) == 2 &&
              SpecializationIncreases(kStealth) == 1,
          "and their specializations");

    std::printf("a race change moves skills without crediting them\n");
    g_race = &g_raceB;
    g_base["Destruction"] = 25.0f;
    SampleLeveling();
    Check(AttributeIncreases(kWillpower) == 0, "racial Destruction is not an increase");

    std::printf("a skill that drops is followed from its new value\n");
    g_base["OneHanded"] = 15.0f;
    SampleLeveling();
    g_base["OneHanded"] = 16.0f;
    SampleLeveling();
    Check(AttributeIncreases(kStrength) == 3, "the drop took nothing, the rise +1");
}

void StepCases() {
    std::printf("a level gained is a pending step\n");
    g_level = 3;
    SampleLeveling();
    Check(PendingLevelUps() == 2, "two levels, two steps");
    Check(PendingStepLevel() == 2, "the first step is for level 2");

    std::printf("the step's gains\n");
    SetActorAttribute("player", kStrength, 40.0f);
    SetActorAttribute("player", kAgility, 99.0f);
    SetActorAttribute("player", kLuck, 40.0f);
    Check(AttributeGain(kStrength) == 2, "3 increases: iLevelUp03Mult is 2");
    Check(AttributeGain(kAgility) == 1, "never past 100");
    Check(AttributeGain(kLuck) == 1, "no increases: 1");

    CompleteLevelUp({kStrength, kAgility, kLuck});
    Check(ActorAttribute("player", kStrength) == 42.0f, "Strength 40 -> 42");
    Check(ActorAttribute("player", kAgility) == 100.0f, "Agility 99 -> 100");
    Check(ActorAttribute("player", kLuck) == 41.0f, "Luck 40 -> 41");
    Check(AttributeIncreases(kStrength) == 0 && SpecializationIncreases(kCombat) == 0,
          "the credits reset");
    Check(PendingLevelUps() == 1 && PendingStepLevel() == 3,
          "one step left, for level 3");
}

// One Health pick at Endurance 100 and two Magicka picks at Intelligence 0.
void PickCases() {
    std::printf("the picks since the last step earn their bonus\n");
    SetActorAttribute("player", kEndurance, 100.0f);
    SetActorAttribute("player", kIntelligence, 0.0f);
    SetActorAttribute("player", kWillpower, 70.0f);
    g_base["Health"] = 15.0f + 10.0f;
    g_base["Magicka"] = 15.0f + 20.0f;
    CompleteLevelUp({kLuck});
    HoldAttributeBuffs();
    Check(BaseIs("Health", 30.0f), "a Health pick at Endurance 100 gives 15");
    Check(BaseIs("Magicka", 25.0f), "two Magicka picks at Intelligence 0 give 5 each");
    Check(BaseIs("Stamina", 15.0f) && BaseIs("CarryWeight", 15.0f),
          "no Stamina pick, no Stamina or carry weight");
    Check(BaseIs("MagickaRateMult", 35.0f), "Willpower 70: magicka regen +20%");
    Check(BaseIs("CritChance", 14.2f), "Luck 42: critical chance -0.8");

    std::printf("a held buff moves by its change, never twice\n");
    HoldAttributeBuffs();
    Check(BaseIs("Health", 30.0f) && BaseIs("MagickaRateMult", 35.0f),
          "holding again changes nothing");
    CompleteLevelUp({kLuck});
    HoldAttributeBuffs();
    Check(BaseIs("Health", 30.0f), "the buff's own points are not a pick");
}

void MagicCases() {
    std::printf("active magic moves the attribute and its buffs while it lasts\n");
    Hooks().attributeEffect = FakeMagic;
    g_magic[kEndurance] = -20.0f;
    g_magic[kWillpower] = 10.0f;
    Check(ActorAttribute("player", kEndurance) == 80.0f &&
              ActorBaseAttribute("player", kEndurance) == 100.0f,
          "Drain Endurance 20: reads 80, base stays 100");
    HoldAttributeBuffs();
    Check(BaseIs("Health", 28.0f), "1 Health pick: 20 drained is -2 Health");
    Check(BaseIs("MagickaRateMult", 45.0f), "Fortify Willpower 10: +10% regen");
    g_magic[kEndurance] = 0.0f;
    g_magic[kWillpower] = 0.0f;
    HoldAttributeBuffs();
    Check(BaseIs("Health", 30.0f) && BaseIs("MagickaRateMult", 35.0f),
          "and gives it back when it ends");
}

void SheetOffCases() {
    std::printf("with the sheet off every buff is handed back\n");
    SetSheetEnabled(false);
    HoldAttributeBuffs();
    Check(BaseIs("Health", 25.0f) && BaseIs("Magicka", 35.0f) &&
              BaseIs("MagickaRateMult", 15.0f) && BaseIs("CritChance", 15.0f),
          "Health, Magicka, regen and critical chance back to vanilla");
    Check(ActorAttribute("player", kStrength) == 100.0f, "Strength reads 100");
    g_base["Speechcraft"] = 37.0f;
    Check(ActorAttribute("player", kPersonality) == 37.0f, "Personality reads Speech");
    Check(!SkillCapEnabled(), "the skill cap is off with it");
    SetSheetEnabled(true);
    HoldAttributeBuffs();
    Check(BaseIs("Health", 30.0f), "and back on, the earned bonus returns");
}

void GoverningCases() {
    std::printf("each Skyrim skill's governing attribute\n");
    Check(GoverningAttribute(6) == kStrength, "One-Handed: Long Blade's Strength");
    Check(GoverningAttribute(17) == kPersonality, "Speech: Speechcraft's Personality");
    Check(GoverningAttribute(24) == -1 && SkillName(24) == nullptr, "Health is no skill");
}

}  // namespace

int main() {
    ClearScriptTables();
    LoadScriptTables(FixtureDir());
    Hooks().baseActorValue = FakeBase;
    Hooks().setActorValue = FakeSet;
    Hooks().actorValue = FakeValue;
    Hooks().playerLevel = FakeLevel;
    Hooks().race = FakeRace;
    CreditCases();
    StepCases();
    PickCases();
    MagicCases();
    SheetOffCases();
    GoverningCases();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
