#include "Randomizer/SphinxRando.h"

#include <cstring>

#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"

namespace SphinxRando {

    struct SphinxQuestion {
        const char16_t* question;
        const char16_t* answers[4];
        s32 correctAnswer;
    };

    struct VanillaQuizSlot {
        const char* questionNeedle;
        const char* answers[4];
        s32 correctAnswer;
    };

    // Add new questions here. correctAnswer is the index in answers, 0 through 3.
    static constexpr SphinxQuestion sQuestionPool[] = {
        { u"How many Souvenirs are there in total?", { u"39", u"43", u"40", u"41" }, 1 }, // 1_im_
        { u"Which of these Captures\ncan Activate Ground Pound Moons?", { u"Frog", u"Hammer Bro", u"Coin Coffer", u"Uproot" }, 3 }, // 1tsAJ
        { u"How Many Moons does Toadette Give?", { u"61", u"60", u"62", u"63" }, 0 }, // SecretDev
        { u"In what kingdom is there a brochure\nwith the mentioning of a goddess?", { u"Cloud Kingdom", u"Seaside Kingdom", u"Lake Kingdom", u"Moon Kingdom" }, 3 }, // AdiDude
        { u"Who is the best SMO modder?", { u"CraftyBoss", u"MaliciousMajora", u"Secret Dev", u"All of Us" }, 3 }, // ALEX
        //      { u"How many windows are there \non the exterior of New Donk City Hall?", { u"2108", u"1940", u"2002", u"1996" }, 2 }, // LavaBlizzard
        { u"What date did this game release?", { u"October 27th, 2017", u"November 8th, 2017", u"November 15th, 2017", u"October 20th, 2017" }, 0 }, // TheGlitchyGuy
        //    { u"How many checkpoint flags are there \nacross every Kingdoms?", { u"85", u"70", u"90", u"105" }, 0 }, // Giano
        { u"What is the Species, that look like Forks called?", { u"Valbanon", u"Volbonan", u"Volbanon", u"Volbonon" }, 1 }, // JuZockt
        { u"How many Captures are listed in the Capture List?", { u"54", u"51", u"52", u"53" }, 2 }, // JuZockt
        //      { u"What is the answer to today's horsle?", { u"Horse", u"Dino", u"Shire", u"Jaxi" }, 0 }, // bert_milar
        //     { u"How much wood could a woodchuck chuck\nif a woodchuck could chuck wood", {u"1", u"I don't know", u"Infinity", u"5"}, 1}, // Umm12many
        { u"What is the meaning of CRC?", { u"Cappy Roll Cancel", u"Cappy Return Cancel", u"Cappy Rebound Cancel", u"Cappy Recall Cancel" }, 1 }, // LeHobbit181
        { u"What was the first Mario Game where\nLuigi was the hero?", { u"Luigi's Mansion", u"Mario Is Missing!", u"Mario Bros.", u"Luigi's Mansion Arcade" }, 1 }, // cob76454
        { u"What kingdom's Hint Art\n was changed in an update?", { u"Sand Kingdom", u"Wooded Kingdom", u"Dark Side", u"Moon Kingdom" }, 0 }, // BaconFearMC
        //      { u"How many Moons are possible to obtain in\n Minimum Captures Maximum Moons (1.0)?", { u"523", u"524", u"522", u"520" }, 0 }, // source: https://docs.google.com/spreadsheets/d/1v949kBdqYd3r3LX7EdQ8SHwlMQgX3JbfIG5AXS4oLjU/edit?gid=1264215742#gid=1264215742
        //      { u"What is Mount Volbono about?", { u"The bubbling burg that heats the meats.", u"The bubbling meats that heats the burg", u"The land of meats.", u"The Mountain of Lava." }, 0 },
        //      { u"What is the Average Temperature\nin Mount Volbono", { u"90°F", u"91°F", u"92°F", u"93°F" }, 3 },
        //      { u"What is the name of the boss\nsurrounding Seaside Kingdom?", { u"Mollusque-Lanceur", u"Astro-Lanceurs", u"Mollusque-Lanceur IV", u"Brigadier" }, 0 },
        //      { u"Who came toodling with a friend?", { u"a Bonneter", u"a Volbonan", u"a Bubblainian", u"Captain Toad" }, 0 },
        { u"Where can you find \nA Miraculous Mixture?", { u"Luncheon Kingdom", u"Seaside Kingdom", u"Mushroom Kingdom", u"Wooded Kingdom" }, 1 },
        { u"With which capture can you extend?", { u"Zipper", u"Yoshi", u"Lakitu", u"Chain Chomp" }, 1 },
        { u"Which souvenir can be\nStacked Up like Blocks?", { u"Vegetable Plate", u"Souvenir Forks", u"Shiverian Nesting Dolls", u"Shiverian Rug" }, 0 },
        { u"Where can the moon\nShining on High\nbe found?", { u"Moon Kingdom", u"Wooded Kingdom", u"Metro Kingdom", u"Snow Kingdom" }, 3 },
        { u"How many Cactuses can be found\n in the Tostarena Town?", { u"10", u"12", u"15", u"16" }, 3 },
        { u"How many Jaxi Statues can be found?", { u"11", u"10", u"6", u"8" }, 0 }, // Smallant
        //      { u"Who said Astounding to Mario\nin Darker Side?", { u"a Toad", u"a Steam Gardener", u"a New Donker", u"a Bonneter" }, 3 },
        //      { u"How many Bowser's Face are there\nin Bowser's Kingdom?", { u"118", u"213", u"82", u"None of the Above" }, 3 },
        { u"How many Purple Coins are there\nacross every Kingdom?", { u"950", u"1000", u"1050", u"1100" }, 1 }, // cjya
        { u"How many settings are\nin the Control Tab?", { u"8", u"9", u"10", u"7" }, 0 },
        { u"Which Kingdom has Moon Mysteries?", { u"Moon Kingdom", u"Dark Side", u"Ruined Kingdom", u"Darker Side" }, 3 },
        { u"Which Music Track is number 64?", { u"Run, Jump, Throw! 1", u"Run, Jump, Throw! 2", u"Run, Jump, Throw! 2 (8-bit)", u"Run, Jump, Throw! 3" }, 2 },
        //      { u"Which Souvenir is number 30?", { u"Lake Kingdom Sticker", u"Wooded Kingdom Sticker", u"Lost Kingdom Sticker", u"Metro Kingdom Sticker" }, 0 },
        // Second batch of Sphynx Questions below
        { u"Which Kingdom has\n the most Moons?\n Excluding Toadette.", { u"Metro Kingdom", u"Luncheon Kingdom", u"Sand Kingdom", u"Seaside Kingdom" }, 2 }, // cjya
        { u"Where can the moon\nCaught on a Big Horn\nbe found?", { u"Cascade Kingdom", u"Cap Kingdom", u"Bowser's Kingdom", u"Ruined Kingdom" }, 3 }, // BaconFearMC otf
        { u"Where can the moon\nCaught on a Giant Horn\nbe found?", { u"Cascade Kingdom", u"Cap Kingdom", u"Bowser's Kingdom", u"Ruined Kingdom" }, 2 }, // BaconFearMC otf
        { u"Which Kingdom has\n the least Moons?", { u"Ruined Kingdom", u"Cloud Kingdom", u"Cap Kingdom", u"Lost Kingdom" }, 1 }, // Selina
        { u"Which of these can\n be found out of bounds?", { u"Fork", u"Moon", u"Rocket Flower", u"Goomba" }, 2 }, // BaconFearMC
        { u"How many Multimoons are\n there across every Kingdom?", { u"20", u"21", u"22", u"19" }, 2 }, // patrick
        { u"Which Capture is number 23?", { u"Sherm", u"Hammer Bro", u"Cheep Cheep", u"Tropical Wiggler" }, 0 }, // BaconFearMC otf
        { u"Which Outfit is altered in the\n Chinese release of the game?", { u"Pirate Outfit", u"Boxer Shorts", u"Football Uniform", u"Satellaview Suit" }, 0 }, // BaconFearMC otf
        { u"In New Donk City, what is the name\n of the park where Mario signs his name?", { u"Mayor Pauline Park", u"Main Street Park", u"Signature Park", u"Squawks Park" }, 3 }, // Smallant
        { u"Which Outfit was removed from\n the boxart before release?", { u"Pirate Outfit", u"Sombrero Suit", u"Builder Outfit", u"Boxer Shorts" }, 1 }, // cjya
        //      { u"What Nintendo IP had an Outfit\n planned for the game before release?", { u"The Legend of Zelda", u"Metroid", u"Kirby", u"Splatoon" }, 0 }, // BaconFearMC otf
        //      { u"What are the New Donkers\n referred to internally?", { u"Todd", u"Jim", u"Joe", u"Fred" }, 2 }, // BaconFearMC otf
        { u"What Shape was the Koopa Trace Walking\n originally planned for Moon Kingdom?", { u"Octagon", u"Star", u"Hexagon", u"Crescent Moon" }, 1 }, // BaconFearMC otf
        { u"What is written inside the Mario Odyssey boxart?", { u"Break Free Lyrics", u"Jump Up Superstar Lyrics", u"Bowser's Wedding Registry", u"Cappy's Origin Story" }, 1 }, // BaconFearMC otf
        // 30 since the Movement Abilities joined the list. They are listed
        // unconditionally - a dial that is off renders its row as already
        // acquired rather than removing it - so the count is settings-independent
        // and this stays a question with one answer.
        { u"How many Abilities are listed in the Ability List?", { u"28", u"29", u"30", u"31" }, 2 }, // BaconFearMC otf
        { u"How many Birds are in Cloud Kingdom?", { u"80", u"78", u"81", u"83" }, 2 }, // BaconFearMC otf
        { u"How many Posters are on the\n Bullet Bill Maze in Sand Kingdom?", { u"14", u"15", u"17", u"16" }, 2 }, // cjya
        { u"How many times can you find\n Captain Toad in the game?", { u"14", u"12", u"13", u"15" }, 0 }, // cjya
        { u"What was GrandPooBear's\n first World Record?", { u"Super Dram World", u"Kaizo Mario 3", u"GPB World1", u"SMB3" }, 1 }, // GPB
        { u"What will you remember\n most from your journey?", { u"My splendid stache", u"Talking to a Sphynx", u"A strange talking hat", u"The Sand Kingdom" }, 1 },
        { u"How many places do I,\n the Sphynx, ask questions?", { u"4", u"5", u"6", u"7" }, 1 },
        { u"How many 2D Moons are in the game?", { u"26", u"27", u"28", u"29" }, 2 }, // zerohundred.coal
        { u"How many Captures are in the Moon Cave?", { u"7", u"9", u"11", u"13" }, 2 }, // gmo
        { u"What is the Notes Moon called\n in the Jaxi Poison Subarea?", { u"Taking Notes: Jaxi Driving", u"Taking Notes: Jaxi Sharpshooting", u"Jaxi Stunt Driving", u"Jaxi Under Siege" }, 2 }, // zerohundred.coal

    };

    static constexpr VanillaQuizSlot sVanillaSlots[] = {
        { "desire from this land", { "Ring", "Maracas", "Freezy Treats", "Sand" }, 0 },
        { "gallops across the sand", { "1", "30", "100", "Like a billion" }, 1 },
        { "what stopped his journey", { "Ice", "Water", "Sand", "The moon" }, 0 },
        { "statues of the sand gallopers", { "4", "8", "6", "5" }, 3 },
        { "What is my name", { "Mr Questionface", "Suffix Wizard", "Quiz Kitty", "Sphynx" }, 3 },
        { "want from this kingdom", { "Water", "Wood", "Flowers", "Soil" }, 2 },
        { "name of this land", { "Fossil Falls", "Forgotten Isle", "Lake Lamode", "Bubblaine" }, 3 },
        { "fountains did", { "Two", "Three", "Four", "Five" }, 2 },
        { "what ability did you get", { "Cheep Cheep", "Attack", "Tremble", "Gushen" }, 1 },
        { "chosen clothing", { "Swimsuit", "Pirate Outfit", "Nothing but undies", "Resort Outfit" }, 3 },
        { "Why am I here", { "Got lost", "Fell off Dorrie", "To ask questions", "Wanted to swim" }, 2 },
        { "answering questions right now", { "Hmm", "Well", "No", "Stingy" }, 2 },
        { "offer the bride", { "Hat Flower", "Fire Piranha Plant", "Rocket Flower", "Poison Piranha Plant" }, 2 },
        { "found in this land", { "Rabbits", "Frogs", "Dogs", "Sheep" }, 3 },
        { "what kind of rock", { "Sun", "Moon", "Fire", "Water" }, 1 },
        { "secret entrance", { "Above the shadow", "Below the bell", "Inside the seed", "The side door" }, 1 },
        { "travel to this place", { "Flew on your wings", "Floated by airship sail", "Rode in a rocket", "Warped here" }, 1 },
        { "only kingdom that I", { "Wooded Kingdom", "Mushroom Kingdom", "Sand Kingdom", "Seaside Kingdom" }, 1 },
        { "unique attributes", { "Sand resistance", "Waterproofness", "Gardening skill", "Zero gravity adept" }, 2 },
        { "remember most", { "My splendid stache", "Talking to a Sphynx", "A strange talking hat", "The Sand Kingdom" }, 1 },
        { "ask people questions", { "Its exciting", "It leads to insights", "It passes the time", "Its a chance to talk" }, 3 },
        { "final words", { "You got lucky", "Thank you", "You faked it", "You cheated" }, 1 },
    };

    static constexpr s32 kQuestionPoolCount = static_cast<s32>(sizeof(sQuestionPool) / sizeof(sQuestionPool[0]));
    static constexpr s32 kVanillaSlotCount = static_cast<s32>(sizeof(sVanillaSlots) / sizeof(sVanillaSlots[0]));
    static constexpr s32 kRememberedChoiceNodeCount = 32;

    static s32 sQuestionMap[kVanillaSlotCount] = {};
    static s32 sAnswerPermutation[kVanillaSlotCount][4] = {};
    static s32 sBranchPermutation[kVanillaSlotCount][4] = {};
    static bool sIsBuilt = false;
    static u32 sBuiltSeed = 0;
    static s32 sLastVanillaSlot = -1;
    static s32 sCurrentVanillaSlot = -1;
    static s32 sLastSelectedAnswerCorrect = -1;
    static s32 sLastSelectedQuestionId = -1;
    static u8 sLastAnswerMask = 0;

    static void rebuildQuestionMap();

    s32 getCurrentQuestionId() {
        rebuildQuestionMap();
        if (sCurrentVanillaSlot >= 0 && sCurrentVanillaSlot < kVanillaSlotCount) {
            return Randomizer::isSphinxRandoEnabled()
                ? sQuestionMap[sCurrentVanillaSlot]
                : kQuestionPoolCount + sCurrentVanillaSlot;
        }
        // Selecting an answer clears the active slot before SphinxQuiz receives
        // its RecordSuccess/RecordFailed event. Preserve that selected question
        // long enough for the board-progress hook to consume it there.
        return sLastSelectedQuestionId;
    }

    struct RememberedChoiceNode {
        const void* node;
        const void* choiceInfo;
        s32 slot;
        const char16_t* const* choiceMessages;
        s32 choiceMessageCount;
    };

    static RememberedChoiceNode sRememberedChoiceNodes[kRememberedChoiceNodeCount] = {};
    static s32 sRememberedChoiceNodeCursor = 0;

    static char toLowerAscii(char ch) {
        if (ch >= 'A' && ch <= 'Z')
            return static_cast<char>(ch - 'A' + 'a');
        return ch;
    }

    static bool isAsciiAlphaNum(char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
    }

    static s32 normalizeAscii(const char* text, char* out, s32 outSize) {
        if (!out || outSize <= 0)
            return 0;

        s32 count = 0;
        if (text) {
            for (s32 i = 0; text[i] && count < outSize - 1; i++) {
                if (!isAsciiAlphaNum(text[i]))
                    continue;
                out[count++] = toLowerAscii(text[i]);
            }
        }
        out[count] = '\0';
        return count;
    }

    static s32 normalizeAscii(const char16_t* text, char* out, s32 outSize) {
        if (!out || outSize <= 0)
            return 0;

        s32 count = 0;
        if (text) {
            for (s32 i = 0; text[i] && count < outSize - 1; i++) {
                if (text[i] > 0x7f)
                    continue;
                char ch = static_cast<char>(text[i]);
                if (!isAsciiAlphaNum(ch))
                    continue;
                out[count++] = toLowerAscii(ch);
            }
        }
        out[count] = '\0';
        return count;
    }

    static bool containsAsciiNormalized(const char* text, const char* needle) {
        char haystack[256];
        char normalizedNeedle[96];
        normalizeAscii(text, haystack, static_cast<s32>(sizeof(haystack)));
        normalizeAscii(needle, normalizedNeedle, static_cast<s32>(sizeof(normalizedNeedle)));
        return normalizedNeedle[0] && std::strstr(haystack, normalizedNeedle) != nullptr;
    }

    static bool containsAsciiNormalized(const char16_t* text, const char* needle) {
        char haystack[768];
        char normalizedNeedle[128];
        normalizeAscii(text, haystack, static_cast<s32>(sizeof(haystack)));
        normalizeAscii(needle, normalizedNeedle, static_cast<s32>(sizeof(normalizedNeedle)));
        return normalizedNeedle[0] && std::strstr(haystack, normalizedNeedle) != nullptr;
    }

    static bool answerMatches(const char16_t* text, const char* answer) {
        char normalizedText[256];
        char normalizedAnswer[96];
        const s32 textLength = normalizeAscii(text, normalizedText, static_cast<s32>(sizeof(normalizedText)));
        const s32 answerLength = normalizeAscii(answer, normalizedAnswer, static_cast<s32>(sizeof(normalizedAnswer)));
        if (answerLength <= 0 || textLength <= 0)
            return false;
        return std::strcmp(normalizedText, normalizedAnswer) == 0;
    }

    static bool messageEquals(const char16_t* lhs, const char16_t* rhs) {
        if (!lhs || !rhs)
            return false;

        for (s32 i = 0;; i++) {
            if (lhs[i] != rhs[i])
                return false;
            if (lhs[i] == 0)
                return true;
        }
    }

    static bool isCurrentGeneratedMessage(const char16_t* message) {
        if (sCurrentVanillaSlot < 0 || sCurrentVanillaSlot >= kVanillaSlotCount)
            return false;

        rebuildQuestionMap();
        const SphinxQuestion& question = sQuestionPool[sQuestionMap[sCurrentVanillaSlot]];
        if (messageEquals(message, question.question))
            return true;
        for (s32 i = 0; i < 4; i++) {
            if (messageEquals(message, question.answers[i]))
                return true;
        }
        return false;
    }

    static bool hasExplicitSphinxContext(const char* fileName, const char* key) {
        return containsAsciiNormalized(fileName, "Sphinx") || containsAsciiNormalized(fileName, "Sphynx") || containsAsciiNormalized(fileName, "Quiz") || containsAsciiNormalized(key, "Sphinx") || containsAsciiNormalized(key, "Sphynx") || containsAsciiNormalized(key, "Quiz");
    }

    static s32 identifyQuestionSlot(const char16_t* originalMessage) {
        for (s32 i = 0; i < kVanillaSlotCount; i++) {
            if (containsAsciiNormalized(originalMessage, sVanillaSlots[i].questionNeedle))
                return i;
        }
        return -1;
    }

    static s32 identifyAnswerIndex(s32 slotIndex, const char16_t* originalMessage) {
        if (slotIndex < 0 || slotIndex >= kVanillaSlotCount)
            return -1;

        const VanillaQuizSlot& slot = sVanillaSlots[slotIndex];
        for (s32 i = 0; i < 4; i++) {
            if (answerMatches(originalMessage, slot.answers[i]))
                return i;
        }
        return -1;
    }

    static s32 identifyQuestionSlotFromAnswers(const char16_t* const* answers, s32 answerCount) {
        if (!answers || answerCount != 4)
            return -1;

        for (s32 slotIndex = 0; slotIndex < kVanillaSlotCount; slotIndex++) {
            bool allAnswersMatch = true;
            for (s32 answerIndex = 0; answerIndex < 4; answerIndex++) {
                if (answerMatches(answers[answerIndex], sVanillaSlots[slotIndex].answers[answerIndex]))
                    continue;
                allAnswersMatch = false;
                break;
            }

            if (allAnswersMatch)
                return slotIndex;
        }

        return -1;
    }

    static bool findUniqueAnswerSlot(s32* slotIndex, s32* answerIndex, const char16_t* originalMessage) {
        s32 foundSlot = -1;
        s32 foundAnswer = -1;
        s32 foundCount = 0;

        for (s32 slot = 0; slot < kVanillaSlotCount; slot++) {
            for (s32 answer = 0; answer < 4; answer++) {
                if (!answerMatches(originalMessage, sVanillaSlots[slot].answers[answer]))
                    continue;
                foundSlot = slot;
                foundAnswer = answer;
                foundCount++;
            }
        }

        if (foundCount != 1)
            return false;

        if (slotIndex)
            *slotIndex = foundSlot;
        if (answerIndex)
            *answerIndex = foundAnswer;
        return true;
    }

    static s32 getWrongAnswerOrdinal(const SphinxQuestion& question, s32 answerIndex) {
        s32 ordinal = 0;
        for (s32 i = 0; i < 4; i++) {
            if (i == question.correctAnswer)
                continue;
            if (i == answerIndex)
                return ordinal;
            ordinal++;
        }
        return 0;
    }

    static s32 getWrongAnswerIndex(const VanillaQuizSlot& slot, s32 ordinal) {
        s32 seen = 0;
        for (s32 i = 0; i < 4; i++) {
            if (i == slot.correctAnswer)
                continue;
            if (seen == ordinal)
                return i;
            seen++;
        }
        return 0;
    }

    static void rebuildQuestionMap() {
        const u32 seed = Randomizer::getSeed();
        if (sIsBuilt && sBuiltSeed == seed)
            return;

        s32 order[kQuestionPoolCount];
        for (s32 i = 0; i < kQuestionPoolCount; i++)
            order[i] = i;

        Randomizer::Rng rng = Randomizer::makeRng(seed, Randomizer::RngStream::Sphinx);
        for (s32 i = kQuestionPoolCount - 1; i > 0; i--) {
            const s32 j = static_cast<s32>(rng.nextModulo(static_cast<u32>(i + 1)));
            const s32 tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }

        for (s32 slotIndex = 0; slotIndex < kVanillaSlotCount; slotIndex++) {
            sQuestionMap[slotIndex] = order[slotIndex % kQuestionPoolCount];

            for (s32 i = 0; i < 4; i++)
                sAnswerPermutation[slotIndex][i] = i;

            for (s32 i = 3; i > 0; i--) {
                const s32 j = static_cast<s32>(rng.nextModulo(static_cast<u32>(i + 1)));
                const s32 tmp = sAnswerPermutation[slotIndex][i];
                sAnswerPermutation[slotIndex][i] = sAnswerPermutation[slotIndex][j];
                sAnswerPermutation[slotIndex][j] = tmp;
            }

            const VanillaQuizSlot& slot = sVanillaSlots[slotIndex];
            const SphinxQuestion& question = sQuestionPool[sQuestionMap[slotIndex]];
            s32 correctChoiceIndex = -1;
            for (s32 i = 0; i < 4; i++) {
                if (sAnswerPermutation[slotIndex][i] == question.correctAnswer) {
                    correctChoiceIndex = i;
                    break;
                }
            }
            if (correctChoiceIndex == question.correctAnswer) {
                const s32 swapIndex = (correctChoiceIndex + 1 + static_cast<s32>(rng.nextModulo(3))) % 4;
                const s32 tmp = sAnswerPermutation[slotIndex][correctChoiceIndex];
                sAnswerPermutation[slotIndex][correctChoiceIndex] = sAnswerPermutation[slotIndex][swapIndex];
                sAnswerPermutation[slotIndex][swapIndex] = tmp;
            }

            for (s32 choiceIndex = 0; choiceIndex < 4; choiceIndex++) {
                const s32 answerIndex = sAnswerPermutation[slotIndex][choiceIndex];
                if (answerIndex == question.correctAnswer) {
                    sBranchPermutation[slotIndex][choiceIndex] = slot.correctAnswer;
                    continue;
                }

                const s32 ordinal = getWrongAnswerOrdinal(question, answerIndex);
                sBranchPermutation[slotIndex][choiceIndex] = getWrongAnswerIndex(slot, ordinal);
            }
        }

        sBuiltSeed = seed;
        sIsBuilt = true;
        Randomizer::logPush("sphinx questions=%d vanilla_slots=%d", kQuestionPoolCount, kVanillaSlotCount);
    }

    static s32 mapTemplateAnswerIndex(s32 slotIndex, s32 vanillaAnswerIndex) {
        return sAnswerPermutation[slotIndex][vanillaAnswerIndex];
    }

    static RememberedChoiceNode* findRememberedChoiceNodeEntry(const void* choiceNode) {
        if (!choiceNode)
            return nullptr;

        for (s32 i = 0; i < kRememberedChoiceNodeCount; i++) {
            if (sRememberedChoiceNodes[i].node == choiceNode)
                return &sRememberedChoiceNodes[i];
        }
        return nullptr;
    }

    static RememberedChoiceNode* findRememberedChoiceInfoEntry(const void* choiceInfo) {
        if (!choiceInfo)
            return nullptr;

        for (s32 i = 0; i < kRememberedChoiceNodeCount; i++) {
            if (sRememberedChoiceNodes[i].choiceInfo == choiceInfo)
                return &sRememberedChoiceNodes[i];
        }
        return nullptr;
    }

    static void rememberChoiceNode(const void* choiceNode, s32 slotIndex,
        const void* choiceInfo = nullptr, const char16_t* const* choiceMessages = nullptr,
        s32 choiceMessageCount = 0) {
        if (!choiceNode || slotIndex < 0 || slotIndex >= kVanillaSlotCount)
            return;

        if (RememberedChoiceNode* entry = findRememberedChoiceNodeEntry(choiceNode)) {
            entry->slot = slotIndex;
            entry->choiceInfo = choiceInfo;
            if (choiceMessages) {
                entry->choiceMessages = choiceMessages;
                entry->choiceMessageCount = choiceMessageCount;
            }
            return;
        }

        sRememberedChoiceNodes[sRememberedChoiceNodeCursor].node = choiceNode;
        sRememberedChoiceNodes[sRememberedChoiceNodeCursor].choiceInfo = choiceInfo;
        sRememberedChoiceNodes[sRememberedChoiceNodeCursor].slot = slotIndex;
        sRememberedChoiceNodes[sRememberedChoiceNodeCursor].choiceMessages = choiceMessages;
        sRememberedChoiceNodes[sRememberedChoiceNodeCursor].choiceMessageCount = choiceMessageCount;
        sRememberedChoiceNodeCursor = (sRememberedChoiceNodeCursor + 1) % kRememberedChoiceNodeCount;
    }

    static void forgetChoiceEntry(RememberedChoiceNode* entry) {
        if (!entry)
            return;

        entry->node = nullptr;
        entry->choiceInfo = nullptr;
        entry->slot = -1;
        entry->choiceMessages = nullptr;
        entry->choiceMessageCount = 0;
    }

    static const char16_t* tryMakeQuestionImpl(const char16_t* originalMessage, bool trackCurrentSlot) {
        if (!Randomizer::isSphinxRandoEnabled()) {
            return nullptr;
        }

        const s32 questionSlot = identifyQuestionSlot(originalMessage);
        if (questionSlot >= 0) {
            rebuildQuestionMap();
            if (trackCurrentSlot) {
                sLastVanillaSlot = questionSlot;
                sCurrentVanillaSlot = questionSlot;
                sLastAnswerMask = 0;
            }
            return sQuestionPool[sQuestionMap[questionSlot]].question;
        }

        return nullptr;
    }

    const char16_t* tryMakeQuestion(const char16_t* originalMessage) {
        return tryMakeQuestionImpl(originalMessage, true);
    }

    const char16_t* tryMakeSourceQuestion(const char16_t* originalMessage) {
        return tryMakeQuestionImpl(originalMessage, false);
    }

    static bool tryMakeChoiceForSlot(const void* choiceNode, const void* choiceInfo, s32 questionSlot,
        const char16_t** outQuestion, const char16_t** outAnswers,
        const char16_t* const* choiceMessages = nullptr, s32 choiceMessageCount = 0) {
        if (questionSlot < 0 || questionSlot >= kVanillaSlotCount)
            return false;

        if (!Randomizer::isSphinxRandoEnabled())
            return false;

        rebuildQuestionMap();
        const SphinxQuestion& question = sQuestionPool[sQuestionMap[questionSlot]];
        if (outQuestion)
            *outQuestion = question.question;
        if (outAnswers) {
            for (s32 i = 0; i < 4; i++)
                outAnswers[i] = question.answers[sAnswerPermutation[questionSlot][i]];
        }

        sLastVanillaSlot = questionSlot;
        sCurrentVanillaSlot = questionSlot;
        sLastSelectedAnswerCorrect = -1;
        sLastSelectedQuestionId = -1;
        sLastAnswerMask = 0;
        rememberChoiceNode(choiceNode, questionSlot, choiceInfo, choiceMessages, choiceMessageCount);
        return true;
    }

    bool tryMakeChoice(const void* choiceNode, const char16_t* originalQuestion,
        const char16_t** outQuestion, const char16_t** outAnswers) {
        if (!Randomizer::isSphinxRandoEnabled())
            return false;

        return tryMakeChoiceForSlot(choiceNode, nullptr, identifyQuestionSlot(originalQuestion),
            outQuestion, outAnswers);
    }

    bool tryMakeChoiceFromAnswers(const void* choiceNode, const void* choiceInfo, const char16_t* originalQuestion,
        const char16_t* const* originalAnswers, s32 answerCount,
        const char16_t** outQuestion, const char16_t** outAnswers) {
        if (!Randomizer::isSphinxRandoEnabled())
            return false;

        s32 questionSlot = identifyQuestionSlot(originalQuestion);
        if (questionSlot < 0)
            questionSlot = identifyQuestionSlotFromAnswers(originalAnswers, answerCount);

        return tryMakeChoiceForSlot(choiceNode, choiceInfo, questionSlot, outQuestion, outAnswers,
            originalAnswers, answerCount);
    }

    const char16_t* tryMakeMessage(const char* fileName, const char* key, const char16_t* originalMessage) {
        if (!Randomizer::isSphinxRandoEnabled()) {
            sLastVanillaSlot = -1;
            sCurrentVanillaSlot = -1;
            sLastAnswerMask = 0;
            return nullptr;
        }

        if (isCurrentGeneratedMessage(originalMessage))
            return nullptr;

        if (const char16_t* question = tryMakeQuestion(originalMessage))
            return question;

        s32 answerSlot = sLastVanillaSlot;
        s32 answerIndex = identifyAnswerIndex(answerSlot, originalMessage);
        if (answerIndex < 0 && hasExplicitSphinxContext(fileName, key)) {
            findUniqueAnswerSlot(&answerSlot, &answerIndex, originalMessage);
            if (answerSlot >= 0 && sLastVanillaSlot < 0) {
                sLastVanillaSlot = answerSlot;
                sCurrentVanillaSlot = answerSlot;
                sLastAnswerMask = 0;
            }
        }
        if (answerSlot < 0 || answerIndex < 0)
            return nullptr;

        rebuildQuestionMap();
        const SphinxQuestion& question = sQuestionPool[sQuestionMap[answerSlot]];
        const char16_t* answer = question.answers[mapTemplateAnswerIndex(answerSlot, answerIndex)];
        if (answerSlot == sLastVanillaSlot) {
            sLastAnswerMask |= static_cast<u8>(1 << answerIndex);
            if ((sLastAnswerMask & 0x0f) == 0x0f) {
                sLastVanillaSlot = -1;
                sLastAnswerMask = 0;
            }
        }
        return answer;
    }

    const char16_t* tryGetCurrentQuestion() {
        if (!Randomizer::isSphinxRandoEnabled() || sCurrentVanillaSlot < 0 || sCurrentVanillaSlot >= kVanillaSlotCount)
            return nullptr;

        rebuildQuestionMap();
        return sQuestionPool[sQuestionMap[sCurrentVanillaSlot]].question;
    }

    static s32 tryRemapSelectedChoice(RememberedChoiceNode* entry, s32 selectedIndex) {
        if (!entry)
            return selectedIndex;

        if (!Randomizer::isSphinxRandoEnabled() || selectedIndex < 0 || selectedIndex >= 4) {
            sLastSelectedAnswerCorrect = -1;
            sLastSelectedQuestionId = -1;
            return selectedIndex;
        }

        const s32 slotIndex = entry->slot;
        if (slotIndex < 0 || slotIndex >= kVanillaSlotCount) {
            sLastSelectedAnswerCorrect = -1;
            sLastSelectedQuestionId = -1;
            return selectedIndex;
        }

        rebuildQuestionMap();
        const SphinxQuestion& question = sQuestionPool[sQuestionMap[slotIndex]];
        s32 generatedAnswerIndex = -1;
        if (entry->choiceMessages && selectedIndex < entry->choiceMessageCount) {
            const char16_t* selectedAnswer = entry->choiceMessages[selectedIndex];
            for (s32 i = 0; i < 4; i++) {
                if (!messageEquals(selectedAnswer, question.answers[i]))
                    continue;
                generatedAnswerIndex = i;
                break;
            }
        }

        s32 remappedIndex = sBranchPermutation[slotIndex][selectedIndex];
        if (generatedAnswerIndex >= 0) {
            if (generatedAnswerIndex == question.correctAnswer)
                remappedIndex = sVanillaSlots[slotIndex].correctAnswer;
            else
                remappedIndex = getWrongAnswerIndex(sVanillaSlots[slotIndex],
                    getWrongAnswerOrdinal(question, generatedAnswerIndex));
        }
        if (generatedAnswerIndex >= 0) {
            sLastSelectedAnswerCorrect = generatedAnswerIndex == question.correctAnswer;
            sLastSelectedQuestionId = sQuestionMap[slotIndex];
        } else {
            sLastSelectedAnswerCorrect = -1;
            sLastSelectedQuestionId = -1;
        }

        sLastVanillaSlot = -1;
        sCurrentVanillaSlot = -1;
        sLastAnswerMask = 0;

        static s32 sLogCount = 0;
        if (sLogCount < 32) {
            Randomizer::logPush("sphinx remapped answer: slot=%d q=%d choice=%d answer=%d correct=%d branch=%d",
                slotIndex, sQuestionMap[slotIndex], selectedIndex, generatedAnswerIndex,
                sLastSelectedAnswerCorrect, remappedIndex);
            sLogCount++;
        }

        return remappedIndex;
    }

    s32 tryRemapSelectedChoiceIndex(const void* choiceNode, s32 selectedIndex) {
        return tryRemapSelectedChoice(findRememberedChoiceNodeEntry(choiceNode), selectedIndex);
    }

    s32 tryRemapSelectedChoiceInfo(const void* choiceInfo, s32 selectedIndex) {
        return tryRemapSelectedChoice(findRememberedChoiceInfoEntry(choiceInfo), selectedIndex);
    }

    s32 getLastSelectedAnswerCorrectState() {
        return sLastSelectedAnswerCorrect;
    }

    void clearLastSelectedAnswerResult() {
        sLastSelectedAnswerCorrect = -1;
        sLastSelectedQuestionId = -1;
    }

    void clear() {
        sIsBuilt = false;
        sBuiltSeed = 0;
        sLastVanillaSlot = -1;
        sCurrentVanillaSlot = -1;
        sLastSelectedAnswerCorrect = -1;
        sLastSelectedQuestionId = -1;
        sLastAnswerMask = 0;
        for (s32 i = 0; i < kVanillaSlotCount; i++) {
            sQuestionMap[i] = 0;
            for (s32 j = 0; j < 4; j++) {
                sAnswerPermutation[i][j] = j;
                sBranchPermutation[i][j] = j;
            }
        }
        for (s32 i = 0; i < kRememberedChoiceNodeCount; i++) {
            sRememberedChoiceNodes[i].node = nullptr;
            sRememberedChoiceNodes[i].choiceInfo = nullptr;
            sRememberedChoiceNodes[i].slot = -1;
            sRememberedChoiceNodes[i].choiceMessages = nullptr;
            sRememberedChoiceNodes[i].choiceMessageCount = 0;
        }
        sRememberedChoiceNodeCursor = 0;
    }

    s32 getQuestionPoolCount() {
        return kQuestionPoolCount;
    }

} // namespace SphinxRando
