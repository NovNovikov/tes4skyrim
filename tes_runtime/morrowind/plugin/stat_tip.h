// OpenMW's AttributeToolTip and SkillToolTip: a stat's icon, name and what it
// does, in a box beside the pointer. A skill adds its governing attribute and
// how far it is toward its next point. The stats window and the level-up
// dialog each carry the movie's Tip* pieces (gen_morrowind_stats_swf.py), and
// each owns one of these over its own menu.
// See: docs/commentary/morrowind_runtime.md#attribute-tooltips
// See: docs/commentary/morrowind_runtime.md#skill-tooltips

#pragma once

#include <string>

namespace tesruntime::mw {

class CustomMenu;

// An attribute's name by TES3 index: its GMST, else Morrowind.esm's text.
std::string AttributeName(int attribute);

// A Skyrim skill's name by actor value (6..23): skyrim_skills.txt's, which is
// Skyrim's own in the install's language, else English.
// See: docs/commentary/morrowind_runtime.md#skyrim-skill-list
std::string SkyrimSkillName(int av);

// Reads skyrim_skills.txt (`av=name|description`); once, at install.
void LoadSkyrimSkills(const std::string& path);

class StatTip {
public:
    explicit StatTip(CustomMenu& menu) : mMenu(menu) {}

    // The pointer is over `attribute` (TES3 index) or Skyrim skill `av`
    // (actor value; -1 for either: over none) at stage pixels x, y.
    void HoverAttribute(int attribute, double x, double y);
    void HoverSkill(int av, double x, double y);

    // Hides every piece; the owner calls it whenever its menu opens.
    void Hide();

    // Lays the box out once the movie has measured the description. The
    // owner's menu tick.
    void Tick();

private:
    void Hover(int key, double x, double y);
    void FillAttribute(int attribute);
    void FillSkill(int av);
    void Place(double textHeight);
    void PlaceProgress(double left, double top);

    CustomMenu& mMenu;
    int mShown = -1;
    // The icon sprite shown, and the progress: -1 none, 0..99 a bar, 100 maxed.
    std::string mIcon;
    int mProgress = -1;
    double mX = 0;
    double mY = 0;
    bool mDirty = false;
};

}  // namespace tesruntime::mw
