// #include "Multiplayer/logger.hpp"
// #include "Scene/StageSceneStateModConfig.hpp"

// // Patch for DoorWarpStageChange::init - Address 0x262850
// // Returns false (0) when costume doors should be unlocked
// bool costumeDoorPatch1(void* thisPtr) {
//     bool shouldUnlock = StageSceneStateModConfig::isCostumeDoorsUnlocked();
//     if (shouldUnlock) {
//         Logger::log("DoorWarpStageChange: Door unlocked\n");
//     }
//     return !shouldUnlock;  // Return false to unlock
// }

// // Patch for DoorWarp::init - Address 0x2609b4
// // Returns false (0) when costume doors should be unlocked
// bool costumeDoorPatch2(void* thisPtr) {
//     bool shouldUnlock = StageSceneStateModConfig::isCostumeDoorsUnlocked();
//     if (shouldUnlock) {
//         Logger::log("DoorWarp: Door unlocked\n");
//     }
//     return !shouldUnlock;  // Return false to unlock
// }

// // Patch for DoorCity::init first check - Address 0x25ff74
// // Returns 1 (true) when costume doors should be unlocked
// uint64_t costumeDoorPatch3(void* thisPtr, void* param1) {
//     bool shouldUnlock = StageSceneStateModConfig::isCostumeDoorsUnlocked();
//     if (shouldUnlock) {
//         Logger::log("DoorCity (check 1): Door unlocked\n");
//         return 1;  // MOV X0, #1 - Door is unlocked
//     }
//     return 0;  // Keep door locked
// }

// // Patch for DoorCity::init second check - Address 0x25ffac
// // Returns 0 (false) when costume doors should be unlocked
// uint64_t costumeDoorPatch4(void* thisPtr, void* param1) {
//     bool shouldUnlock = StageSceneStateModConfig::isCostumeDoorsUnlocked();
//     if (shouldUnlock) {
//         Logger::log("DoorCity (check 2): Door unlocked\n");
//         return 0;  // MOV X0, #0 - Door is unlocked
//     }
//     return 1;  // Keep door locked
// }