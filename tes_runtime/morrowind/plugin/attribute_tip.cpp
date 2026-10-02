#include "attribute_tip.h"

#include <algorithm>

#include "menu.h"
#include "script_tables.h"
#include "stats_layout.h"

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

constexpr const char* kBoxPieces[] = {"TipTop", "TipMid", "TipBottom"};

std::string Path(const std::string& name, const char* property) {
    return "_root." + name + property;
}

std::string Icon(int attribute) { return "TipIcon" + std::to_string(attribute); }

}  // namespace

std::string AttributeName(int attribute) {
    if (attribute < 0 || attribute >= kAttributes) return std::string();
    return GmstText(kAttributeGmst[attribute][0], kAttributeGmst[attribute][1]);
}

void AttributeTip::Hide() {
    for (const char* piece : kBoxPieces) {
        mMenu.SetNumber(Path(piece, "._visible").c_str(), 0);
    }
    for (int i = 0; i < sl::kTipIcons; ++i) {
        mMenu.SetNumber(Path(Icon(i), "._visible").c_str(), 0);
    }
    mMenu.SetNumber("_root.TipName._visible", 0);
    mMenu.SetNumber("_root.TipText._visible", 0);
    mShown = -1;
    mDirty = false;
}

void AttributeTip::Hover(int attribute, double x, double y) {
    if (attribute != mShown) {
        Hide();
        if (attribute < 0 || attribute >= kAttributes) return;
        mShown = attribute;
        mMenu.SetText("_root.TipName.text", AttributeName(attribute).c_str());
        mMenu.SetText("_root.TipText.text", Description(attribute).c_str());
    }
    if (mShown < 0) return;
    mX = x;
    mY = y;
    mDirty = true;
}

void AttributeTip::Tick() {
    double height = 0;
    if (!mDirty || !mMenu.GetNumber("_root.TipText.textHeight", &height)) return;
    Place(height);
    mDirty = false;
}

void AttributeTip::Place(double textHeight) {
    const double w = sl::kTipW;
    const double h = sl::kTipTop + textHeight + sl::kTipPad;
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
    mMenu.SetNumber("_root.TipMid._height", std::max(1.0, textHeight));
    put("TipBottom", left, top + sl::kTipTop + textHeight);
    put(Icon(mShown), left + sl::kTipPad, top + sl::kTipPad);
    put("TipName", left + sl::kTipNameX, top + sl::kTipNameDy);
    put("TipText", left + sl::kTipPad, top + sl::kTipTop);
}

}  // namespace tesruntime::mw
