#pragma once

// The hat a captured enemy wears.
//
// The cap on a captured enemy is not part of the capture's archive: HackCap
// hides its own model on the first frame of a hack and shows a second actor -
// the game calls it the equipment hat - posed from HackCap's own transform.
// HackCap::init builds that actor from the hardcoded "MarioCap" archive, so
// capturing anything while wearing any other cap still puts Mario's red hat on
// the enemy's head.
//
// Remote players never had that problem: their worn cap is drawn by
// PuppetCapActor, built from the costume their CostumeInf packet carried, so
// everyone else already sees the cap the player is actually wearing. This draws
// that same hat for the player who owns the capture.
namespace CaptureHat {

    void installHooks();

} // namespace CaptureHat
