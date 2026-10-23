// #include "factoryPatches.h"

// #include "hk/hook/Trampoline.h"

// #include "game/Scene/ProjectActorFactory.h"
// #include "game/Scene/ProjectCameraPoserFactory.h"

// HkTrampoline<void, ProjectActorFactory*> actorFactoryHook = hk::hook::trampoline([](ProjectActorFactory* actorFactory) -> void {
//     actorFactoryHook.orig(actorFactory);
//     s32 customActorEntriesCount = sizeof(sCustomActorFactoryEntries) / sizeof(sCustomActorFactoryEntries[0]);
//     al::NameToCreator<al::ActorCreatorFunction>* factoryEntries =
//         new al::NameToCreator<al::ActorCreatorFunction>[actorFactory->mNumFactoryEntries + customActorEntriesCount];

//     for (s32 i = 0; i < actorFactory->mNumFactoryEntries; i++) {
//         factoryEntries[i] = actorFactory->mFactoryEntries[i];
//     }
//     for (s32 i = 0; i < customActorEntriesCount; i++) {
//         factoryEntries[actorFactory->mNumFactoryEntries + i] = sCustomActorFactoryEntries[i];
//     }

//     actorFactory->mFactoryEntries = factoryEntries;
//     actorFactory->mNumFactoryEntries += customActorEntriesCount;
// });
// // HkTrampoline<void, ProjectCameraPoserFactory*> cameraFactoryHook =
// //     hk::hook::trampoline([](ProjectCameraPoserFactory* cameraFactory) -> void {
// //         cameraFactoryHook.orig(cameraFactory);
// //         s32 customActorEntriesCount =
// //             sizeof(sCustomCameraFactoryEntries) / sizeof(sCustomCameraFactoryEntries[0]);
// //         al::NameToCreator<al::CameraPoserCreatorFunction>* factoryEntries = new
// //         al::NameToCreator<
// //             al::CameraPoserCreatorFunction>[cameraFactory->mNumFactoryEntries +
// //                                             customActorEntriesCount];

// //         for (s32 i = 0; i < cameraFactory->mNumFactoryEntries; i++) {
// //             factoryEntries[i] = cameraFactory->mFactoryEntries[i];
// //         }
// //         for (s32 i = 0; i < customActorEntriesCount; i++) {
// //             factoryEntries[cameraFactory->mNumFactoryEntries + i] =
// //             sCustomCameraFactoryEntries[i];
// //         }

// //         cameraFactory->mFactoryEntries = factoryEntries;
// //         cameraFactory->mNumFactoryEntries += customActorEntriesCount;
// //     });

// void insertCustomThingsInFactory() {
//     actorFactoryHook.installAtSym<"_ZN19ProjectActorFactoryC2Ev">();
//     // cameraFactoryHook.installAtSym<"_ZN25ProjectCameraPoserFactoryC2Ev">();
// }
