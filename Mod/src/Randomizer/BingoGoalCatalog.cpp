#include "Randomizer/BingoGoalCatalog.h"

#include "Randomizer/LogicData.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/WorldMeta.h"

#include <cstring>

namespace Bingo {
    namespace {

#define G(id, type, diff, target, subject, uid, shortText, fullText) \
    { id, GoalType::type, GoalDifficulty::diff, target, subject, uid, shortText, fullText }

        static constexpr GoalDefinition sGoals[] = {
            G(1, PurpleCoins, Easy, 10, 1, 0, "10 Cascade purples", "Collect 10 purple coins in Cascade Kingdom"),
            G(2, PurpleCoins, Easy, 10, 4, 0, "10 Lake purples", "Collect 10 purple coins in Lake Kingdom"),
            G(3, PurpleCoins, Easy, 10, 6, 0, "10 Lost purples", "Collect 10 purple coins in Lost Kingdom"),
            G(4, PurpleCoins, Easy, 10, 9, 0, "10 Snow purples", "Collect 10 purple coins in Snow Kingdom"),
            G(5, PurpleCoins, Easy, 10, 13, 0, "10 Moon purples", "Collect 10 purple coins in Moon Kingdom"),
            G(6, PurpleCoins, Easy, 20, 2, 0, "20 Sand purples", "Collect 20 purple coins in Sand Kingdom"),
            G(7, PurpleCoins, Easy, 20, 3, 0, "20 Wooded purples", "Collect 20 purple coins in Wooded Kingdom"),
            G(8, PurpleCoins, Easy, 20, 7, 0, "20 Metro purples", "Collect 20 purple coins in Metro Kingdom"),
            G(9, PurpleCoins, Easy, 20, 8, 0, "20 Seaside purples", "Collect 20 purple coins in Seaside Kingdom"),
            G(10, PurpleCoins, Easy, 20, 10, 0, "20 Luncheon purples", "Collect 20 purple coins in Luncheon Kingdom"),
            G(11, PurpleCoins, Easy, 20, 12, 0, "20 Bowser's purples", "Collect 20 purple coins in Bowser's Kingdom"),
            G(12, PurpleCoins, Easy, 20, 14, 0, "20 Mushroom purples", "Collect 20 purple coins in Mushroom Kingdom"),
            G(13, PurpleCoins, Hard, 40, 1, 0, "40 Cascade purples", "Collect 40 purple coins in Cascade Kingdom"),
            G(14, PurpleCoins, Hard, 40, 4, 0, "40 Lake purples", "Collect 40 purple coins in Lake Kingdom"),
            G(15, PurpleCoins, Hard, 40, 6, 0, "40 Lost purples", "Collect 40 purple coins in Lost Kingdom"),
            G(16, PurpleCoins, Hard, 40, 9, 0, "40 Snow purples", "Collect 40 purple coins in Snow Kingdom"),
            G(17, PurpleCoins, Hard, 40, 13, 0, "40 Moon purples", "Collect 40 purple coins in Moon Kingdom"),
            G(18, PurpleCoins, Hard, 80, 2, 0, "80 Sand purples", "Collect 80 purple coins in Sand Kingdom"),
            G(19, PurpleCoins, Hard, 80, 3, 0, "80 Wooded purples", "Collect 80 purple coins in Wooded Kingdom"),
            G(20, PurpleCoins, Hard, 80, 7, 0, "80 Metro purples", "Collect 80 purple coins in Metro Kingdom"),
            G(21, PurpleCoins, Hard, 80, 8, 0, "80 Seaside purples", "Collect 80 purple coins in Seaside Kingdom"),
            G(22, PurpleCoins, Hard, 80, 10, 0, "80 Luncheon purples", "Collect 80 purple coins in Luncheon Kingdom"),
            G(23, PurpleCoins, Hard, 80, 12, 0, "80 Bowser's purples", "Collect 80 purple coins in Bowser's Kingdom"),
            G(24, PurpleCoins, Hard, 80, 14, 0, "80 Mushroom purples", "Collect 80 purple coins in Mushroom Kingdom"),

            G(25, SpecificStoryMoon, Easy, 1, 1, 205, "Our First Power Moon", "Collect Our First Power Moon"),
            G(26, SpecificStoryMoon, Easy, 1, 2, 497, "Atop the Highest Tower", "Collect Atop the Highest Tower"),
            G(27, SpecificStoryMoon, Easy, 1, 3, 129, "Road to Sky Garden", "Collect Road to Sky Garden"),
            G(28, SpecificStoryMoon, Easy, 1, 7, 41, "Guitarist on Board!", "Collect Guitarist on Board!"),
            G(29, SpecificStoryMoon, Easy, 1, 9, 17, "The Icicle Barrier", "Collect The Icicle Barrier"),
            G(30, SpecificStoryMoon, Easy, 1, 10, 251, "Under the Cheese Rocks", "Collect Under the Cheese Rocks"),
            G(31, SpecificStoryMoon, Hard, 1, 2, 496, "Moon Shards in the Sand", "Collect Moon Shards in the Sand"),
            G(32, SpecificStoryMoon, Hard, 1, 3, 159, "Secret Flower Field path", "Collect Path to the Secret Flower Field"),
            G(33, SpecificStoryMoon, Hard, 1, 7, 107, "Powering Up the Station", "Collect Powering Up the Station"),
            G(34, SpecificStoryMoon, Hard, 1, 8, 441, "The Lighthouse Seal", "Collect The Lighthouse Seal"),
            G(35, SpecificStoryMoon, Hard, 1, 12, 314, "Big Broodal Battle", "Collect Big Broodal Battle"),
            G(36, SpecificStoryMoon, Hard, 1, 12, 325, "Infiltrate Bowser's Castle!", "Collect Infiltrate Bowser's Castle!"),
            G(37, StoryMoons, Easy, 2, -1, 0, "2 story moons", "Collect 2 story moon locations"),
            G(38, StoryMoons, Hard, 4, -1, 0, "4 story moons", "Collect 4 story moon locations"),

            G(39, SpecificTripleMoon, Easy, 1, 1, 218, "Multi Moon Atop the Falls", "Collect Multi Moon Atop the Falls"),
            G(42, SpecificTripleMoon, Easy, 1, 4, 424, "Broodals Over the Lake", "Collect Broodals Over the Lake"),
            G(40, SpecificTripleMoon, Hard, 1, 2, 495, "Inverted Pyramid", "Collect Showdown on the Inverted Pyramid"),
            G(41, SpecificTripleMoon, Hard, 1, 3, 130, "Flower Thieves", "Collect Flower Thieves of Sky Garden"),
            G(43, SpecificTripleMoon, Hard, 1, 7, 37, "New Donk City pest", "Collect New Donk City's Pest Problem"),
            G(44, SpecificTripleMoon, Hard, 1, 7, 95, "A Traditional Festival!", "Collect A Traditional Festival!"),
            G(45, SpecificTripleMoon, Hard, 1, 9, 1020, "Bound Bowl Grand Prix", "Collect The Bound Bowl Grand Prix"),
            G(46, SpecificTripleMoon, Hard, 1, 10, 290, "Cookatiel Showdown!", "Collect Cookatiel Showdown!"),
            G(47, SpecificTripleMoon, Hard, 1, 11, 795, "Lord of Lightning", "Collect Battle with the Lord of Lightning!"),
            G(48, SpecificTripleMoon, Hard, 1, 12, 332, "Bowser's Castle showdown", "Collect Showdown at Bowser's Castle"),
            G(49, TripleMoons, Easy, 2, -1, 0, "2 triple moons", "Collect 2 triple moon locations"),
            G(50, TripleMoons, Hard, 4, -1, 0, "4 triple moons", "Collect 4 triple moon locations"),

            G(51, ChestMoons, Easy, 1, -1, 0, "1 chest moon", "Collect 1 chest moon"),
            G(52, ChestMoons, Hard, 3, -1, 0, "3 chest moons", "Collect 3 chest moons"),
            G(53, TimerMoons, Easy, 1, -1, 0, "1 timer moon", "Collect 1 timer challenge moon"),
            G(54, TimerMoons, Hard, 3, -1, 0, "3 timer moons", "Collect 3 timer challenge moons"),
            G(55, KoopaRaceMoons, Easy, 2, -1, 0, "2 Koopa race moons", "Collect 2 Koopa Free Running moons"),
            G(56, KoopaRaceMoons, Hard, 6, -1, 0, "6 Koopa race moons", "Collect 6 Koopa Free Running moons"),
            G(57, CoinsEarned, Easy, 3000, -1, 0, "Earn 3000 coins", "Earn 3000 coins"),
            G(58, CoinsEarned, Hard, 6000, -1, 0, "Earn 6000 coins", "Earn 6000 coins"),
            G(59, LifeUpHearts, Easy, 1, -1, 0, "1 Life-Up Heart", "Collect 1 unique Life-Up Heart"),
            G(60, LifeUpHearts, Hard, 3, -1, 0, "3 Life-Up Hearts", "Collect 3 unique Life-Up Hearts"),
            G(61, CrateMoons, Easy, 1, -1, 0, "1 crate moon", "Collect 1 crate moon"),
            G(62, CrateMoons, Hard, 3, -1, 0, "3 crate moons", "Collect 3 crate moons"),
            G(63, ShardMoons, Easy, 1, -1, 0, "1 shard moon", "Collect 1 moon-shard moon"),
            G(64, ShardMoons, Hard, 3, -1, 0, "3 shard moons", "Collect 3 moon-shard moons"),
            G(65, NoteMoons, Easy, 1, -1, 0, "1 note moon", "Collect 1 music-note moon"),
            G(66, NoteMoons, Hard, 3, -1, 0, "3 note moons", "Collect 3 music-note moons"),
            G(67, NutMoons, Easy, 2, -1, 0, "2 nut moons", "Collect 2 nut moons"),
            G(68, NutMoons, Hard, 4, -1, 0, "4 nut moons", "Collect 4 nut moons"),
            G(69, TwoDMoons, Easy, 1, -1, 0, "1 2D moon", "Collect 1 2D moon"),
            G(70, TwoDMoons, Hard, 3, -1, 0, "3 2D moons", "Collect 3 2D moons"),
            G(71, MoonRockMoons, Hard, 10, -1, 0, "10 Moon Rock moons", "Collect 10 Moon Rock moons"),
            G(72, CaptainToadMoons, Easy, 1, -1, 0, "1 Captain Toad moon", "Collect 1 Captain Toad moon"),
            G(73, CaptainToadMoons, Hard, 3, -1, 0, "3 Captain Toad moons", "Collect 3 Captain Toad moons"),
            G(74, GroundPoundMoons, Easy, 1, -1, 0, "1 ground-pound moon", "Collect 1 ground-pound moon"),
            G(75, GroundPoundMoons, Hard, 3, -1, 0, "3 ground-pound moons", "Collect 3 ground-pound moons"),
            G(76, SeedMoons, Easy, 2, -1, 0, "2 seed moons", "Collect 2 seed moons"),
            G(77, SeedMoons, Hard, 6, -1, 0, "6 seed moons", "Collect 6 seed moons"),
            G(78, BossEncounters, Easy, 1, -1, 0, "Beat 1 boss", "Beat 1 unique boss encounter"),
            G(79, BossEncounters, Hard, 3, -1, 0, "Beat 3 bosses", "Beat 3 unique boss encounters"),
            G(80, SpecificBoss, Easy, 1, 1, 0, "Beat Madame Broode", "Beat Madame Broode"),
            G(81, SpecificBoss, Easy, 1, 2, 0, "Beat Topper", "Beat Topper"),
            G(82, SpecificBoss, Easy, 1, 3, 0, "Beat Hariet", "Beat Hariet"),
            G(83, SpecificBoss, Easy, 1, 4, 0, "Beat Rango", "Beat Rango"),
            G(84, SpecificBoss, Hard, 1, 5, 0, "Beat Knucklotec", "Beat Knucklotec"),
            G(85, SpecificBoss, Hard, 1, 6, 0, "Beat Torkdrift", "Beat Torkdrift"),
            G(86, SpecificBoss, Hard, 1, 7, 0, "Beat Mechawiggler", "Beat Mechawiggler"),
            G(87, SpecificBoss, Hard, 1, 8, 0, "Beat Mollusque-Lanceur", "Beat Mollusque-Lanceur"),
            G(88, SpecificBoss, Hard, 1, 9, 0, "Beat Cookatiel", "Beat Cookatiel"),
            G(89, SpecificBoss, Hard, 1, 10, 0, "Beat Lord of Lightning", "Beat Lord of Lightning"),
            G(90, SpecificBoss, Hard, 1, 11, 0, "Beat RoboBrood", "Beat RoboBrood"),
            G(91, SphinxQuestions, Easy, 2, -1, 0, "2 Sphinx questions", "Answer 2 unique Sphinx questions correctly"),
            G(92, SphinxQuestions, Hard, 8, -1, 0, "8 Sphinx questions", "Answer 8 unique Sphinx questions correctly"),
            G(93, Checkpoints, Easy, 5, -1, 0, "5 checkpoints", "Activate 5 unique checkpoints"),
            G(94, Checkpoints, Hard, 25, -1, 0, "25 checkpoints", "Activate 25 unique checkpoints"),
            G(95, Paintings, Easy, 1, -1, 0, "Use 1 painting", "Travel through 1 unique painting"),
            G(96, Paintings, Hard, 4, -1, 0, "Use 4 paintings", "Travel through 4 unique paintings"),

            G(97, WorldPeace, Hard, 1, 1, 0, "Peace in Cascade", "Get World Peace in Cascade Kingdom"),
            G(98, WorldPeace, Hard, 1, 2, 0, "Peace in Sand", "Get World Peace in Sand Kingdom"),
            G(99, WorldPeace, Hard, 1, 3, 0, "Peace in Wooded", "Get World Peace in Wooded Kingdom"),
            G(100, WorldPeace, Hard, 1, 4, 0, "Peace in Lake", "Get World Peace in Lake Kingdom"),
            G(101, WorldPeace, Hard, 1, 7, 0, "Peace in Metro", "Get World Peace in Metro Kingdom"),
            G(102, WorldPeace, Hard, 1, 8, 0, "Peace in Seaside", "Get World Peace in Seaside Kingdom"),
            G(103, WorldPeace, Hard, 1, 9, 0, "Peace in Snow", "Get World Peace in Snow Kingdom"),
            G(104, WorldPeace, Hard, 1, 10, 0, "Peace in Luncheon", "Get World Peace in Luncheon Kingdom"),
            G(105, WorldPeace, Hard, 1, 11, 0, "Peace in Ruined", "Get World Peace in Ruined Kingdom"),
            G(106, WorldPeace, Hard, 1, 12, 0, "Peace in Bowser's", "Get World Peace in Bowser's Kingdom"),
            G(107, WorldPeace, Hard, 1, 16, 0, "Peace in Darker Side", "Get World Peace in Darker Side"),

            G(108, KingdomMoonLocations, Easy, 4, 0, 0, "4 moons in Cap", "Collect 4 moon locations in Cap Kingdom"),
            G(109, KingdomMoonLocations, Hard, 7, 0, 0, "7 moons in Cap", "Collect 7 moon locations in Cap Kingdom"),
            G(110, KingdomMoonLocations, Easy, 4, 1, 0, "4 moons in Cascade", "Collect 4 moon locations in Cascade Kingdom"),
            G(111, KingdomMoonLocations, Hard, 8, 1, 0, "8 moons in Cascade", "Collect 8 moon locations in Cascade Kingdom"),
            G(112, KingdomMoonLocations, Easy, 9, 2, 0, "9 moons in Sand", "Collect 9 moon locations in Sand Kingdom"),
            G(113, KingdomMoonLocations, Hard, 18, 2, 0, "18 moons in Sand", "Collect 18 moon locations in Sand Kingdom"),
            G(114, KingdomMoonLocations, Easy, 8, 3, 0, "8 moons in Wooded", "Collect 8 moon locations in Wooded Kingdom"),
            G(115, KingdomMoonLocations, Hard, 16, 3, 0, "16 moons in Wooded", "Collect 16 moon locations in Wooded Kingdom"),
            G(116, KingdomMoonLocations, Easy, 5, 4, 0, "5 moons in Lake", "Collect 5 moon locations in Lake Kingdom"),
            G(117, KingdomMoonLocations, Hard, 9, 4, 0, "9 moons in Lake", "Collect 9 moon locations in Lake Kingdom"),
            G(118, KingdomMoonLocations, Easy, 1, 5, 0, "1 moon in Cloud", "Collect 1 moon location in Cloud Kingdom"),
            G(119, KingdomMoonLocations, Hard, 2, 5, 0, "2 moons in Cloud", "Collect 2 moon locations in Cloud Kingdom"),
            G(120, KingdomMoonLocations, Easy, 4, 6, 0, "4 moons in Lost", "Collect 4 moon locations in Lost Kingdom"),
            G(121, KingdomMoonLocations, Hard, 7, 6, 0, "7 moons in Lost", "Collect 7 moon locations in Lost Kingdom"),
            G(122, KingdomMoonLocations, Easy, 9, 7, 0, "9 moons in Metro", "Collect 9 moon locations in Metro Kingdom"),
            G(123, KingdomMoonLocations, Hard, 17, 7, 0, "17 moons in Metro", "Collect 17 moon locations in Metro Kingdom"),
            G(124, KingdomMoonLocations, Easy, 8, 8, 0, "8 moons in Seaside", "Collect 8 moon locations in Seaside Kingdom"),
            G(125, KingdomMoonLocations, Hard, 15, 8, 0, "15 moons in Seaside", "Collect 15 moon locations in Seaside Kingdom"),
            G(126, KingdomMoonLocations, Easy, 6, 9, 0, "6 moons in Snow", "Collect 6 moon locations in Snow Kingdom"),
            G(127, KingdomMoonLocations, Hard, 11, 9, 0, "11 moons in Snow", "Collect 11 moon locations in Snow Kingdom"),
            G(128, KingdomMoonLocations, Easy, 7, 10, 0, "7 moons in Luncheon", "Collect 7 moon locations in Luncheon Kingdom"),
            G(129, KingdomMoonLocations, Hard, 14, 10, 0, "14 moons in Luncheon", "Collect 14 moon locations in Luncheon Kingdom"),
            G(130, KingdomMoonLocations, Easy, 1, 11, 0, "1 moon in Ruined", "Collect 1 moon location in Ruined Kingdom"),
            G(131, KingdomMoonLocations, Hard, 2, 11, 0, "2 moons in Ruined", "Collect 2 moon locations in Ruined Kingdom"),
            G(132, KingdomMoonLocations, Easy, 7, 12, 0, "7 moons in Bowser's", "Collect 7 moon locations in Bowser's Kingdom"),
            G(133, KingdomMoonLocations, Hard, 13, 12, 0, "13 moons in Bowser's", "Collect 13 moon locations in Bowser's Kingdom"),
            G(134, KingdomMoonLocations, Easy, 4, 13, 0, "4 moons in Moon", "Collect 4 moon locations in Moon Kingdom"),
            G(135, KingdomMoonLocations, Hard, 8, 13, 0, "8 moons in Moon", "Collect 8 moon locations in Moon Kingdom"),
            G(136, KingdomMoonLocations, Easy, 5, 14, 0, "5 moons in Mushroom", "Collect 5 moon locations in Mushroom Kingdom"),
            G(137, KingdomMoonLocations, Hard, 9, 14, 0, "9 moons in Mushroom", "Collect 9 moon locations in Mushroom Kingdom"),
            G(138, KingdomMoonLocations, Easy, 3, 15, 0, "3 moons in Dark Side", "Collect 3 moon locations in Dark Side"),
            G(139, KingdomMoonLocations, Hard, 5, 15, 0, "5 moons in Dark Side", "Collect 5 moon locations in Dark Side"),

            G(140, ShopMoons, Easy, 3, -1, 0, "Buy 3 shop moons", "Buy 3 shop moons"),
            G(141, ShopMoons, Hard, 8, -1, 0, "Buy 8 shop moons", "Buy 8 shop moons"),
            G(142, NpcConversations, Easy, 4, -1, 0, "Talk to 4 NPCs", "Speak with 4 unique NPCs"),
            G(143, NpcConversations, Hard, 8, -1, 0, "Talk to 8 NPCs", "Speak with 8 unique NPCs"),

            G(144, KingdomMoonItems, Easy, 4, 0, 0, "4 Cap moons", "Collect 4 Cap Kingdom moons"),
            G(145, KingdomMoonItems, Hard, 7, 0, 0, "7 Cap moons", "Collect 7 Cap Kingdom moons"),
            G(146, KingdomMoonItems, Easy, 4, 1, 0, "4 Cascade moons", "Collect 4 Cascade Kingdom moons"),
            G(147, KingdomMoonItems, Hard, 8, 1, 0, "8 Cascade moons", "Collect 8 Cascade Kingdom moons"),
            G(148, KingdomMoonItems, Easy, 9, 2, 0, "9 Sand moons", "Collect 9 Sand Kingdom moons"),
            G(149, KingdomMoonItems, Hard, 18, 2, 0, "18 Sand moons", "Collect 18 Sand Kingdom moons"),
            G(150, KingdomMoonItems, Easy, 8, 3, 0, "8 Wooded moons", "Collect 8 Wooded Kingdom moons"),
            G(151, KingdomMoonItems, Hard, 16, 3, 0, "16 Wooded moons", "Collect 16 Wooded Kingdom moons"),
            G(152, KingdomMoonItems, Easy, 5, 4, 0, "5 Lake moons", "Collect 5 Lake Kingdom moons"),
            G(153, KingdomMoonItems, Hard, 9, 4, 0, "9 Lake moons", "Collect 9 Lake Kingdom moons"),
            G(154, KingdomMoonItems, Easy, 1, 5, 0, "1 Cloud moon", "Collect 1 Cloud Kingdom moon"),
            G(155, KingdomMoonItems, Hard, 2, 5, 0, "2 Cloud moons", "Collect 2 Cloud Kingdom moons"),
            G(156, KingdomMoonItems, Easy, 4, 6, 0, "4 Lost moons", "Collect 4 Lost Kingdom moons"),
            G(157, KingdomMoonItems, Hard, 7, 6, 0, "7 Lost moons", "Collect 7 Lost Kingdom moons"),
            G(158, KingdomMoonItems, Easy, 9, 7, 0, "9 Metro moons", "Collect 9 Metro Kingdom moons"),
            G(159, KingdomMoonItems, Hard, 17, 7, 0, "17 Metro moons", "Collect 17 Metro Kingdom moons"),
            G(160, KingdomMoonItems, Easy, 8, 8, 0, "8 Seaside moons", "Collect 8 Seaside Kingdom moons"),
            G(161, KingdomMoonItems, Hard, 15, 8, 0, "15 Seaside moons", "Collect 15 Seaside Kingdom moons"),
            G(162, KingdomMoonItems, Easy, 6, 9, 0, "6 Snow moons", "Collect 6 Snow Kingdom moons"),
            G(163, KingdomMoonItems, Hard, 11, 9, 0, "11 Snow Kingdom moons", "Collect 11 Snow Kingdom moons"),
            G(164, KingdomMoonItems, Easy, 7, 10, 0, "7 Luncheon moons", "Collect 7 Luncheon Kingdom moons"),
            G(165, KingdomMoonItems, Hard, 14, 10, 0, "14 Luncheon moons", "Collect 14 Luncheon Kingdom moons"),
            G(166, KingdomMoonItems, Easy, 1, 11, 0, "1 Ruined moon", "Collect 1 Ruined Kingdom moon"),
            G(167, KingdomMoonItems, Hard, 2, 11, 0, "2 Ruined moons", "Collect 2 Ruined Kingdom moons"),
            G(168, KingdomMoonItems, Easy, 7, 12, 0, "7 Bowser's moons", "Collect 7 Bowser's Kingdom moons"),
            G(169, KingdomMoonItems, Hard, 13, 12, 0, "13 Bowser's moons", "Collect 13 Bowser's Kingdom moons"),
            G(170, KingdomMoonItems, Easy, 4, 13, 0, "4 Moon moons", "Collect 4 Moon Kingdom moons"),
            G(171, KingdomMoonItems, Hard, 8, 13, 0, "8 Moon moons", "Collect 8 Moon Kingdom moons"),
            G(172, KingdomMoonItems, Easy, 5, 14, 0, "5 Mushroom moons", "Collect 5 Mushroom Kingdom moons"),
            G(173, KingdomMoonItems, Hard, 9, 14, 0, "9 Mushroom moons", "Collect 9 Mushroom Kingdom moons"),
            G(174, KingdomMoonItems, Easy, 3, 15, 0, "3 Dark Side moons", "Collect 3 Dark Side moons"),
            G(175, KingdomMoonItems, Hard, 5, 15, 0, "5 Dark Side moons", "Collect 5 Dark Side moons"),

            G(176, SheepHerdingMoons, Easy, 1, -1, 0, "1 sheep moon", "Herd 1 sheep moon"),
            G(177, GoombetteMoons, Easy, 1, -1, 0, "1 Goombette moon", "Collect 1 power moon from Goombette"),
            G(178, LakituMoons, Easy, 1, -1, 0, "1 Lakitu moon", "Collect 1 Lakitu moon"),
            G(179, RabbitMoons, Easy, 2, -1, 0, "2 rabbit moons", "Collect 2 rabbit moons"),
            G(180, BirdMoons, Easy, 2, -1, 0, "2 bird moons", "Collect 2 moons from birds"),
            G(181, HideAndSeekMoons, Easy, 1, -1, 0, "1 hide-and-seek moon", "Collect 1 hat-and-seek moon"),
            G(182, HintArtMoons, Easy, 1, -1, 0, "1 Hint Art moon", "Collect 1 Hint Art moon"),
            G(183, SlotsMoons, Easy, 1, -1, 0, "1 Slots moon", "Collect 1 Slots moon"),
            G(184, KoopaRaceMoons, Easy, 3, -1, 0, "3 Koopa race moons", "Collect 3 Koopa Free Running moons"),
            G(185, KoopaTraceWalkingMoons, Easy, 1, -1, 0, "1 Trace-Walking moon", "Collect 1 Koopa Trace-Walking moon"),
            G(186, SheepHerdingMoons, Easy, 2, -1, 0, "2 sheep moons", "Herd 2 sheep moons"),
            G(187, GoombetteMoons, Easy, 2, -1, 0, "2 Goombette moons", "Collect 2 power moons from Goombette"),
            G(188, LakituMoons, Easy, 2, -1, 0, "2 Lakitu moons", "Collect 2 Lakitu moons"),
            G(189, HideAndSeekMoons, Easy, 2, -1, 0, "2 hide-and-seek moons", "Collect 2 hat-and-seek moons"),
            G(190, HintArtMoons, Easy, 2, -1, 0, "2 Hint Art moons", "Collect 2 Hint Art moons"),
            G(191, SlotsMoons, Easy, 2, -1, 0, "2 Slots moons", "Collect 2 Slots moons"),
            G(192, KoopaTraceWalkingMoons, Easy, 2, -1, 0, "2 Trace-Walking moons", "Collect 2 Koopa Trace-Walking moons"),
        };

#undef G
        static_assert(sizeof(sGoals) / sizeof(sGoals[0]) == 192, "Unexpected Bingo catalog size");

    } // namespace

    const GoalDefinition* getGoals() { return sGoals; }
    s32 getGoalCount() { return static_cast<s32>(sizeof(sGoals) / sizeof(sGoals[0])); }

    const GoalDefinition* findGoal(u16 id) {
        for (s32 i = 0; i < getGoalCount(); i++)
            if (sGoals[i].id == id)
                return &sGoals[i];
        return nullptr;
    }

    bool isEligible(const GoalDefinition& goal) {
        if (goal.type != GoalType::WorldPeace)
            return true;
        const s32 worldId = goal.subject;
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return false;
        // Darker Side has no kingdom-completion objective for board play.
        if (worldId == 16)
            return false;
        if (Randomizer::isKingdomForcedPeace(worldId))
            return false;
        if (Randomizer::LogicData::sStoryTotalsByKingdom[worldId] <= 0)
            return false;
        const s32 start = Randomizer::getInitialKingdomScenario(worldId);
        const s32 peace = Randomizer::LogicData::sPeaceScenarioByKingdom[worldId];
        return peace > 0 && start < peace;
    }

} // namespace Bingo
