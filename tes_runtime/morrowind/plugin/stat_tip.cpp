#include "stat_tip.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include "dialogue_state.h"
#include "leveling.h"
#include "log.h"
#include "menu.h"
#include "script_tables.h"
#include "stats_layout.h"
#include "store.h"

namespace tesruntime::mw {

namespace {

namespace sl = stats_layout;

// The attribute names' GMSTs and Morrowind.esm's text for them.
constexpr const char* kAttributeGmst[][2] = {
    {"sAttributeStrength", "Strength"}, {"sAttributeIntelligence", "Intelligence"},
    {"sAttributeWillpower", "Willpower"}, {"sAttributeAgility", "Agility"},
    {"sAttributeSpeed", "Speed"}, {"sAttributeEndurance", "Endurance"},
    {"sAttributePersonality", "Personality"}, {"sAttributeLuck", "Luck"}};
constexpr int kAttributes = 8;

// Skyrim's skills by actor value from 6: the English names, for an install
// packaged without skyrim_skills.txt.
constexpr const char* kSkyrimSkillNames[] = {
    "One-handed", "Two-handed", "Archery", "Block", "Smithing", "Heavy Armor",
    "Light Armor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech",
    "Alteration", "Conjuration", "Destruction", "Illusion", "Restoration", "Enchanting"};
constexpr int kFirstSkill = 6;
constexpr int kSkills = 18;

// Skyrim's own name and description per skill, in the install's language:
// skyrim_skills.txt (`av=name|description`), packaged with the menus.
// See: docs/commentary/morrowind_runtime.md#skyrim-skill-list
struct SkyrimSkill {
    std::string name;
    std::string description;
};
SkyrimSkill g_skills[kSkills];

// A skill's hover key: its actor value past the attributes, so one number
// names either.
constexpr int kSkillKey = 100;

bool IsSkill(int av) { return av >= kFirstSkill && av < kFirstSkill + kSkills; }

// Each attribute's description: Morrowind's own (sStrDesc and kin) where it
// still holds, so a translated Morrowind keeps its language, and Morrowind's
// words cut down to what the attribute does here where it does not. Fatigue
// is Skyrim's Stamina.
// See: docs/commentary/morrowind_runtime.md#attribute-tooltips
constexpr const char* kDescription[][2] = {
    {nullptr, "Affects how much you can carry."},
    {nullptr, "Affects your maximum amount of Magicka."},
    {nullptr, "Affects how quickly your Magicka returns."},
    {nullptr, "Affects your maximum Stamina."},
    {nullptr, "Affects how quickly your Stamina returns."},
    {nullptr, "Affects your Health gain per level."},
    {"sPerDesc", "Affects your ability to deal with other characters and how much they like you."},
    {nullptr, "Affects your chance of a critical hit, and every other action in a small way."}};

std::string Description(int attribute) {
    const auto& row = kDescription[attribute];
    return row[0] ? GmstText(row[0], row[1]) : row[1];
}

// ToolTips::position: 32 px below the pointer, shifted left by the pointer's
// share of the screen, kept on it, and above the pointer at the bottom edge.
constexpr double kBelow = 32;
constexpr double kAboveGap = 8;

// The progress bar's fill sits inside its 2 px box border, as the stat bars'.
constexpr double kBarInset = 2;
constexpr int kMaxed = 100;

constexpr const char* kPlayer = "player";

constexpr const char* kPieces[] = {"TipTop", "TipMid", "TipBottom", "TipName", "TipAttr",
                                   "TipText", "TipLabel", "TipProgress", "TipBar",
                                   "TipBarCover"};

std::string Path(const std::string& name, const char* property) {
    return "_root." + name + property;
}

// How far the player's skill is toward its next point, 0..99; kMaxed at 100;
// -1 for a progress that cannot be read.
int Progress(int av) {
    const char* name = SkillName(av);
    if (!name || !Hooks().baseActorValue) return -1;
    if (Hooks().baseActorValue(kPlayer, name) >= kMaxed) return kMaxed;
    const float fraction = Hooks().skillProgress ? Hooks().skillProgress(name) : -1.0f;
    if (fraction < 0.0f) return -1;
    return std::clamp(static_cast<int>(std::lround(fraction * 100.0f)), 0, kMaxed - 1);
}

}  // namespace

std::string AttributeName(int attribute) {
    if (attribute < 0 || attribute >= kAttributes) return std::string();
    return GmstText(kAttributeGmst[attribute][0], kAttributeGmst[attribute][1]);
}

std::string SkyrimSkillName(int av) {
    if (!IsSkill(av)) return std::string();
    const std::string& name = g_skills[av - kFirstSkill].name;
    return name.empty() ? kSkyrimSkillNames[av - kFirstSkill] : name;
}

void LoadSkyrimSkills(const std::string& path) {
    std::ifstream in(path);
    std::string line;
    int loaded = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t eq = line.find('=');
        const std::size_t bar = line.find('|', eq);
        const int av = eq == std::string::npos ? -1 : std::atoi(line.substr(0, eq).c_str());
        if (!IsSkill(av) || bar == std::string::npos) continue;
        g_skills[av - kFirstSkill] = {line.substr(eq + 1, bar - eq - 1),
                                      Unescape(line.substr(bar + 1))};
        ++loaded;
    }
    Log("sheet: %d Skyrim skill name(s) from %s", loaded, path.c_str());
}

void StatTip::Hide() {
    for (const char* piece : kPieces) {
        mMenu.SetNumber(Path(piece, "._visible").c_str(), 0);
    }
    for (int i = 0; i < sl::kTipIcons; ++i) {
        mMenu.SetNumber(Path("TipIcon" + std::to_string(i), "._visible").c_str(), 0);
    }
    for (int i = 0; i < sl::kTipSkillIcons; ++i) {
        const std::string icon = "TipSkill" + std::to_string(sl::kTipSkillFirst + i);
        mMenu.SetNumber(Path(icon, "._visible").c_str(), 0);
    }
    mShown = -1;
    mDirty = false;
}

void StatTip::HoverAttribute(int attribute, double x, double y) {
    Hover(attribute >= 0 && attribute < kAttributes ? attribute : -1, x, y);
}

void StatTip::HoverSkill(int av, double x, double y) {
    Hover(IsSkill(av) ? kSkillKey + av : -1, x, y);
}

void StatTip::Hover(int key, double x, double y) {
    if (key != mShown) {
        Hide();
        if (key < 0) return;
        mShown = key;
        if (key >= kSkillKey) {
            FillSkill(key - kSkillKey);
        } else {
            FillAttribute(key);
        }
    }
    if (mShown < 0) return;
    mX = x;
    mY = y;
    mDirty = true;
}

void StatTip::FillAttribute(int attribute) {
    mIcon = "TipIcon" + std::to_string(attribute);
    mProgress = -1;
    mMenu.SetText("_root.TipName.text", AttributeName(attribute).c_str());
    mMenu.SetText("_root.TipText.text", Description(attribute).c_str());
}

// SkillToolTip's lines for a Skyrim skill: its name, "Governing Attribute: X"
// under it (the attribute the cap and the level-up credit use), Skyrim's own
// description, and the progress label.
void StatTip::FillSkill(int av) {
    mIcon = "TipSkill" + std::to_string(av);
    mProgress = Progress(av);
    const std::string governing = AttributeName(GoverningAttribute(av));
    const std::string attr = governing.empty()
        ? std::string()
        : GmstText("sGoverningAttribute", "Governing Attribute") + ": " + governing;
    mMenu.SetText("_root.TipName.text", SkyrimSkillName(av).c_str());
    mMenu.SetText("_root.TipAttr.text", attr.c_str());
    mMenu.SetText("_root.TipText.text", g_skills[av - kFirstSkill].description.c_str());
    const std::string label = mProgress >= kMaxed
        ? GmstText("sSkillMaxReached", "Maximum proficiency has been reached.")
        : GmstText("sSkillProgress", "Progress towards skill increase");
    mMenu.SetText("_root.TipLabel.text", label.c_str());
    mMenu.SetText("_root.TipProgress.text", (std::to_string(std::max(0, mProgress)) + "/100").c_str());
}

void StatTip::Tick() {
    double height = 0;
    if (!mDirty || !mMenu.GetNumber("_root.TipText.textHeight", &height)) return;
    Place(height);
    mDirty = false;
}

// The label, and below it the bar with its number and the cover over the part
// past the progress; or the label alone once the skill is at 100.
void StatTip::PlaceProgress(double left, double top) {
    const auto put = [this](const char* name, double x, double y) {
        mMenu.SetNumber(Path(name, "._x").c_str(), x);
        mMenu.SetNumber(Path(name, "._y").c_str(), y);
        mMenu.SetNumber(Path(name, "._visible").c_str(), 1);
    };
    const double labelY = top + sl::kTipGap;
    put("TipLabel", left + sl::kTipPad, labelY + sl::kTipLabelDy);
    if (mProgress >= kMaxed) return;
    const double barX = left + (sl::kTipW - sl::kTipBarW) / 2.0;
    const double barY = labelY + sl::kTipLabelH;
    const double fill = sl::kTipBarW - 2 * kBarInset;
    const double filled = fill * mProgress / kMaxed;
    put("TipBar", barX, barY);
    put("TipBarCover", barX + kBarInset + filled, barY + kBarInset);
    mMenu.SetNumber("_root.TipBarCover._width", std::max(1.0, fill - filled));
    put("TipProgress", barX, barY + sl::kTipBarTextDy);
}

void StatTip::Place(double textHeight) {
    const bool skill = mShown >= kSkillKey;
    const double extra = mProgress < 0 ? 0
                         : mProgress >= kMaxed ? sl::kTipGap + sl::kTipLabelH
                                               : sl::kTipGap + sl::kTipLabelH + sl::kTipBarH;
    const double w = sl::kTipW;
    const double h = sl::kTipTop + textHeight + extra + sl::kTipPad;
    double left = mX - mX / sl::kStageW * w;
    double top = mY + kBelow;
    left = std::min(left, sl::kStageW - w);
    if (top + h > sl::kStageH) top = mY - h - kAboveGap;
    const auto put = [this](const std::string& name, double x, double y) {
        mMenu.SetNumber(Path(name, "._x").c_str(), x);
        mMenu.SetNumber(Path(name, "._y").c_str(), y);
        mMenu.SetNumber(Path(name, "._visible").c_str(), 1);
    };
    put("TipTop", left, top);
    put("TipMid", left, top + sl::kTipTop);
    mMenu.SetNumber("_root.TipMid._height", std::max(1.0, textHeight + extra));
    put("TipBottom", left, top + sl::kTipTop + textHeight + extra);
    put(mIcon, left + sl::kTipPad, top + sl::kTipPad);
    put("TipName", left + sl::kTipNameX, top + (skill ? sl::kTipSkillNameDy : sl::kTipNameDy));
    if (skill) put("TipAttr", left + sl::kTipNameX, top + sl::kTipAttrDy);
    put("TipText", left + sl::kTipPad, top + sl::kTipTop);
    if (mProgress >= 0) PlaceProgress(left, top + sl::kTipTop + textHeight);
}

}  // namespace tesruntime::mw
