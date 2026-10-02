// OpenMW's AttributeToolTip: an attribute's icon, name and what it does, in a
// box beside the pointer. The stats window and the level-up dialog each carry
// the movie's Tip* pieces (gen_morrowind_stats_swf.py), and each owns one of
// these over its own menu.
// See: docs/commentary/morrowind_runtime.md#attribute-tooltips

#pragma once

#include <string>

namespace tesruntime::mw {

class CustomMenu;

// An attribute's name by TES3 index: its GMST, else Morrowind.esm's text.
std::string AttributeName(int attribute);

class AttributeTip {
public:
    explicit AttributeTip(CustomMenu& menu) : mMenu(menu) {}

    // The pointer is over `attribute` (-1: over none) at stage pixels x, y.
    void Hover(int attribute, double x, double y);

    // Hides every piece; the owner calls it whenever its menu opens.
    void Hide();

    // Lays the box out once the movie has measured the description. The
    // owner's menu tick.
    void Tick();

private:
    void Place(double textHeight);

    CustomMenu& mMenu;
    int mShown = -1;
    double mX = 0;
    double mY = 0;
    bool mDirty = false;
};

}  // namespace tesruntime::mw
