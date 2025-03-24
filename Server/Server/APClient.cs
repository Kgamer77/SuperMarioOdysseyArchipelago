using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using static System.Collections.Specialized.BitVector32;
using System.Net;
using System.Collections;
using Shared;
using Shared.Packet;
using Shared.Packet.Packets;
using Archipelago;
using Archipelago.MultiClient;
using Archipelago.MultiClient.Net;
using Archipelago.MultiClient.Net.Enums;
using Archipelago.MultiClient.Net.Helpers;
using Archipelago.MultiClient.Net.Packets;
using Archipelago.MultiClient.Net.Models;

namespace Server
{
    
    public class APClient
    {
        public ArchipelagoSession session;
        public LoginResult result;
        public LoginSuccessful loginSuccessful;
        public ItemInfo lastReceivedItem;
        public static readonly Dictionary<string, int> shopItems = new Dictionary<string, int>()
        {
            {"MarioInvisibleCap", 2502},
            {"MarioCaptainCap", 2503},
            {"MarioTailCoatCap", 2504},
            {"MarioTailCoatClothes", 2505},
            {"StickerCap", 2506},
            {"SouvenirHat1", 2507},
            {"SouvenirHat2", 2508},
            {"MarioPrimitiveManCap", 2509},
            {"MarioPrimitiveManClothes", 2510},
            {"StickerWaterfall", 2511},
            {"SouvenirFall1", 2512},
            {"SouvenirFall2", 2513},
            {"MarioPonchoCap", 2514},
            {"MarioPonchoClothes", 2515},
            {"MarioGunmanCap", 2516},
            {"MarioGunmanClothes", 2517},
            {"StickerSand", 2518},
            {"SouvenirSand2", 2519},
            {"SouvenirSand1", 2520},
            {"MarioSwimwearCap", 2521},
            {"MarioSwimwearClothes", 2522},
            {"StickerLake", 2523},
            {"SouvenirLake2", 2524},
            {"SouvenirLake1", 2525},
            {"MarioExplorerCap", 2526},
            {"MarioExplorerClothes", 2527},
            {"MarioScientistCap", 2528},
            {"MarioScientistClothes", 2529},
            {"StickerForest", 2530},
            {"SouvenirForest1", 2531},
            {"SouvenirForest2", 2532},
            {"MarioPilotCap", 2533},
            {"MarioPilotClothes", 2534},
            {"StickerClash", 2535},
            {"SouvenirCrash1", 2536},
            {"SouvenirCrash2", 2537},
            {"MarioMakerCap", 2538},
            {"MarioMakerClothes", 2539},
            {"MarioGolfCap", 2540},
            {"MarioGolfClothes", 2541},
            {"StickerCity", 2542},
            {"SouvenirCity2", 2543},
            {"SouvenirCity1", 2544},
            {"MarioSnowSuitCap", 2545},
            {"MarioSnowSuitClothes", 2546},
            {"StickerSnow", 2547},
            {"SouvenirSnow1", 2548},
            {"SouvenirSnow2", 2549},
            {"MarioAlohaCap", 2550},
            {"MarioAlohaClothes", 2551},
            {"MarioSailorCap", 2552},
            {"MarioSailorClothes", 2553},
            {"StickerSea", 2554},
            {"SouvenirSea2", 2555},
            {"SouvenirSea1", 2556},
            {"MarioCookCap", 2557},
            {"MarioCookClothes", 2558},
            {"MarioPainterCap", 2559},
            {"MarioPainterClothes", 2560},
            {"StickerLava", 2561},
            {"SouvenirLava1", 2562},
            {"SouvenirLava2", 2563},
            {"MarioArmorCap", 2564},
            {"MarioArmorClothes", 2565},
            {"MarioHappiCap", 2566},
            {"MarioHappiClothes", 2567},
            {"StickerSky", 2568},
            {"SouvenirSky1", 2569},
            {"SouvenirSky2", 2570},
            {"MarioSpaceSuitCap", 2571},
            {"MarioSpaceSuitClothes", 2572},
            {"StickerMoon", 2573},
            {"SouvenirMoon1", 2574},
            {"SouvenirMoon2", 2575},
            {"Mario64Cap", 2576},
            {"Mario64Clothes", 2577},
            {"StickerPeachDokan", 2578},
            {"StickerPeachCoin", 2579},
            {"StickerPeachBlock", 2580},
            {"StickerPeachBlockQuestion", 2581},
            {"StickerPeach", 2582},
            {"SouvenirPeach1", 2583},
            {"SouvenirPeach2", 2584},
            {"MarioShopmanCap", 2585},
            {"MarioShopmanClothes", 2586},
            {"MarioUnderwearClothes", 2587},
            {"MarioNew3DSCap", 2588},
            {"MarioNew3DSClothes", 2589},
            {"MarioMechanicCap", 2590},
            {"MarioMechanicClothes", 2591},
            {"MarioSuitCap", 2592},
            {"MarioSuitClothes", 2593},
            {"MarioPirateCap", 2594},
            {"MarioPirateClothes", 2595},
            {"MarioClownCap", 2596},
            {"MarioClownClothes", 2597},
            {"MarioFootballCap", 2598},
            {"MarioFootballClothes", 2599},
            {"MarioColorClassicCap", 2600},
            {"MarioColorClassicClothes", 2601},
            {"MarioBoneClothes", 2602},
            {"MarioColorLuigiCap", 2603},
            {"MarioColorLuigiClothes", 2604},
            {"MarioDoctorCap", 2605},
            {"MarioDoctorClothes", 2606},
            {"MarioColorWaluigiCap", 2607},
            {"MarioColorWaluigiClothes", 2608},
            {"MarioDiddyKongCap", 2609},
            {"MarioDiddyKongClothes", 2610},
            {"MarioColorWarioCap", 2611},
            {"MarioColorWarioClothes", 2612},
            {"MarioHakamaClothes", 2613},
            {"MarioKoopaCap", 2614},
            {"MarioKoopaClothes", 2615},
            {"MarioPeachCap", 2616},
            {"MarioPeachClothes", 2617},
            {"MarioColorGoldCap", 2618},
            {"MarioColorGoldClothes", 2619},
            {"Mario64MetalCap", 2620},
            {"Mario64MetalClothes", 2621},
            {"MarioKingCap", 2622},
            {"MarioKingClothes", 2623},
            {"Tuxedo Cap", 2624},
            {"Tuxedo Clothes", 2625}
        };
        
        public static readonly Dictionary<int, string> inverseShopItems = new Dictionary<int, string>()
        {
            {2502, "MarioInvisibleCap"},
            {2503 , "MarioCaptainCap"},
            {2504 , "MarioTailCoatCap"},
            {2505 , "MarioTailCoatClothes"},
            {2506 , "StickerCap"},
            {2507 , "SouvenirHat1"},
            {2508 , "SouvenirHat2"},
            {2509 , "MarioPrimitiveManCap"},
            {2510 , "MarioPrimitiveManClothes"},
            {2511 , "StickerWaterfall"},
            {2512 , "SouvenirFall1"},
            {2513 , "SouvenirFall2"},
            {2514 , "MarioPonchoCap"},
            {2515 , "MarioPonchoClothes"},
            {2516 , "MarioGunmanCap"},
            {2517 , "MarioGunmanClothes"},
            {2518 , "StickerSand"},
            {2519 , "SouvenirSand2"},
            {2520 , "SouvenirSand1"},
            {2521 , "MarioSwimwearCap"},
            {2522 , "MarioSwimwearClothes"},
            {2523 , "StickerLake"},
            {2524 , "SouvenirLake2"},
            {2525 , "SouvenirLake1"},
            {2526 , "MarioExplorerCap"},
            {2527 , "MarioExplorerClothes"},
            {2528 , "MarioScientistCap"},
            {2529 , "MarioScientistClothes"},
            {2530 , "StickerForest"},
            {2531 , "SouvenirForest1"},
            {2532 , "SouvenirForest2"},
            {2533 , "MarioPilotCap"},
            {2534 , "MarioPilotClothes"},
            {2535 , "StickerClash"},
            {2536 , "SouvenirCrash1"},
            {2537 , "SouvenirCrash2"},
            {2538 , "MarioMakerCap"},
            {2539 , "MarioMakerClothes"},
            {2540 , "MarioGolfCap"},
            {2541 , "MarioGolfClothes"},
            {2542 , "StickerCity"},
            {2543 , "SouvenirCity2"},
            {2544 , "SouvenirCity1"},
            {2545 , "MarioSnowSuitCap"},
            {2546 , "MarioSnowSuitClothes"},
            {2547 , "StickerSnow"},
            {2548 , "SouvenirSnow1"},
            {2549 , "SouvenirSnow2"},
            {2550 , "MarioAlohaCap"},
            {2551 , "MarioAlohaClothes"},
            {2552 , "MarioSailorCap"},
            {2553 , "MarioSailorClothes"},
            {2554 , "StickerSea"},
            {2555 , "SouvenirSea2"},
            {2556 , "SouvenirSea1"},
            {2557 , "MarioCookCap"},
            {2558 , "MarioCookClothes"},
            {2559 , "MarioPainterCap"},
            {2560 , "MarioPainterClothes"},
            {2561 , "StickerLava"},
            {2562 , "SouvenirLava1"},
            {2563 , "SouvenirLava2"},
            {2564 , "MarioArmorCap"},
            {2565 , "MarioArmorClothes"},
            {2566 , "MarioHappiCap"},
            {2567 , "MarioHappiClothes"},
            {2568 , "StickerSky"},
            {2569 , "SouvenirSky1"},
            {2570 , "SouvenirSky2"},
            {2571 , "MarioSpaceSuitCap"},
            {2572 , "MarioSpaceSuitClothes"},
            {2573 , "StickerMoon"},
            {2574 , "SouvenirMoon1"},
            {2575 , "SouvenirMoon2"},
            {2576 , "Mario64Cap"},
            {2577 , "Mario64Clothes"},
            {2578 , "StickerPeachDokan"},
            {2579 , "StickerPeachCoin"},
            {2580 , "StickerPeachBlock"},
            {2581 , "StickerPeachBlockQuestion"},
            {2582 , "StickerPeach"},
            {2583 , "SouvenirPeach1"},
            {2584 , "SouvenirPeach2"},
            {2585 , "MarioShopmanCap"},
            {2586 , "MarioShopmanClothes"},
            {2587 , "MarioUnderwearClothes"},
            {2588 , "MarioNew3DSCap"},
            {2589 , "MarioNew3DSClothes"},
            {2590 , "MarioMechanicCap"},
            {2591 , "MarioMechanicClothes"},
            {2592 , "MarioSuitCap"},
            {2593 , "MarioSuitClothes"},
            {2594 , "MarioPirateCap"},
            {2595 , "MarioPirateClothes"},
            {2596 , "MarioClownCap"},
            {2597 , "MarioClownClothes"},
            {2598 , "MarioFootballCap"},
            {2599 , "MarioFootballClothes"},
            {2600 , "MarioColorClassicCap"},
            {2601 , "MarioColorClassicClothes"},
            {2602 , "MarioBoneClothes"},
            {2603 , "MarioColorLuigiCap"},
            {2604 , "MarioColorLuigiClothes"},
            {2605 , "MarioDoctorCap"},
            {2606 , "MarioDoctorClothes"},
            {2607 , "MarioColorWaluigiCap"},
            {2608 , "MarioColorWaluigiClothes"},
            {2609 , "MarioDiddyKongCap"},
            {2610 , "MarioDiddyKongClothes"},
            {2611 , "MarioColorWarioCap"},
            {2612 , "MarioColorWarioClothes"},
            {2613 , "MarioHakamaClothes"},
            {2614 , "MarioKoopaCap"},
            {2615 , "MarioKoopaClothes"},
            {2616 , "MarioPeachCap"},
            {2617 , "MarioPeachClothes"},
            {2618 , "MarioColorGoldCap"},
            {2619 , "MarioColorGoldClothes"},
            {2620 , "Mario64MetalCap"},
            {2621 , "Mario64MetalClothes"},
            {2622 , "MarioKingCap"},
            {2623 , "MarioKingClothes"},
            {2624 , "MarioTuxedoCap"},
            {2625 , "MarioTuxedoClothes"}
        };



        public static readonly Dictionary<long, string> goals = new Dictionary<long, string>()
        {
            {4, "Sand Multi-Moon (2)"},
            {5, "Lake Multi-Moon"},
            {9, "Metro Multi-Moon (2)"},
            {12, "Luncheon Multi-Moon (2)"},
            {15, "Beat the Game"},
            {17, "Dark Side Multi-Moon"},
            {18, "Darker Side Multi-Moon"}
        };

        public void Connect(string server, string user, string pass, ushort port)
        {
            session = ArchipelagoSessionFactory.CreateSession(server, port);

            try
            {
                // handle TryConnectAndLogin attempt here and save the returned object to `result`
                result = session.TryConnectAndLogin("Super Mario Odyssey", user, ItemsHandlingFlags.AllItems, null, null, null, pass);
            }
            catch (Exception e)
            {
                result = new LoginFailure(e.GetBaseException().Message);
            }

            if (!result.Successful)
            {
                LoginFailure failure = (LoginFailure)result;
                string errorMessage = $"Failed to Connect to {server} as {user}:";
                foreach (string error in failure.Errors)
                {
                    errorMessage += $"\n    {error}";
                }
                foreach (ConnectionRefusedError error in failure.ErrorCodes)
                {
                    errorMessage += $"\n    {error}";
                }

                Console.WriteLine(errorMessage);
                // Did not connect, show the user the contents of `errorMessage`
            }
            // Successfully connected, `ArchipelagoSession` (assume statically defined as `session` from now on) can now be used to interact with the server

            // returned `LoginSuccessful` contains some useful information about the initial connection (e.g. a copy of the slot data as `loginSuccess.SlotData`)
            Console.WriteLine($"Successfully Connected to {server} as {user}");
            loginSuccessful = (LoginSuccessful)result;
       }



        
        public void send_location(int location)
        {
            session.Locations.CompleteLocationChecks(location);
        }

        public string get_goal()
        {
            return goals[(long)(loginSuccessful.SlotData["goal"])];
        }
    }
}
