#pragma once

#include <basis/seadTypes.h>

class HakoniwaSequence;

/**
 * Custom staff roll.
 *
 * The vanilla credits are entirely data-driven: StaffRollScene reads one
 * message ("StaffRoll" in the SystemMessage archive), splits it on newlines and
 * walks the result.  A line is one of
 *
 *   "@title TEXT"   section header
 *   "@Key Name"     a name, with the role text looked up from the message
 *                   label "Role_Key" in the same archive
 *   "Name"          a name with no role, centred
 *   "@space"        extra spacing
 *   ""              a gap
 *
 * Rather than shipping a patched message archive for all fourteen languages,
 * this module intercepts the three message lookups StaffRollScene makes and
 * answers them itself: our sections are prepended to whatever the game's own
 * text is, and four extra "Role_Rando*" labels are published so the run-summary
 * lines can use the same label-above-value layout the real credits use.
 *
 * Because the scene derives its scroll speed from the total content height, a
 * longer roll would simply scroll faster end to end.  Two more hooks split that
 * into a readable pass over our sections and a quicker one over Nintendo's,
 * without changing how long the whole roll takes.
 */
namespace CreditsRoll {

    void installHooks();

    /// Returns a customized message for the credits roll, or nullptr when the
    /// requested archive/label is not owned by this feature.
    const char16_t* tryBuildMessage(const char* archive, const char* label,
        const char16_t* vanilla);

    /// Per-frame driver for the Mod Menu's temporary "Jump to Credits" entry.
    /// Safe to call every frame; does nothing unless a jump was requested.
    void updateSequence(HakoniwaSequence* sequence);

    /// Requested by the Mod Menu. Fades out, tears the current scene down and
    /// hands the sequence to the staff roll. TEMPORARY - test aid only.
    void requestJumpToCredits();

    bool isJumpRequested();

} // namespace CreditsRoll
