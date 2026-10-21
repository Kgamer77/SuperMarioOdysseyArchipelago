#pragma once

// Used for Cappy Message Building
__attribute__((used)) static const char* SMOItems[] = { "Connect", "Power Moon", "Multi-Moon", "Regional Coin", "Cap", "Clothes", "Sticker",
    "Souvenir", "Capture", "Moon Rock", "Ability", "Missing1", "Missing2", "Coins" };
__attribute__((used)) static const char* kingdoms[] = { "Cap", "Cascade", "Sand", "Wooded", "Lake", "Lost", "Cloud", "Metro", "Seaside",
    "Snow", "Luncheon", "Ruined", "Bowser", "Moon", "Mushroom", "Dark Side", "Darker Side" };

__attribute__((used)) static const char* abilities[] = { "Jump",
    "Double Jump",
    "Triple Jump",
    "Side Flip",
    "Back Flip",
    "Long Jump",
    "Wall Jump",
    "Spin",
    "Spin Jump",
    "Ground Pound",
    "Ground Pound Jump",
    "Cap Throw",
    "Up Throw",
    "Down Throw",
    "Spin Throw",
    "Crouch",
    "Roll",
    "Roll Boost",
    "Vault",
    "Dive",
    "Climb",
    "Ledge Grab",
    "Cappy Return Jump",
    "Jaxi",
    "Scooter" };
__attribute__((used)) static const char16_t regionalIcons1[] = { 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0000, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x0000, 0x001a, 0x001b, 0x001c, 0x001b, 0x001b };
__attribute__((used)) static const char16_t regionalIcons2[] = {
    0x0050,
    0x0051,
    0x0052,
    0x0053,
    0x0054,
    0x0001,
    0x0055,
    0x0056,
    0x0057,
    0x0058,
    0x0059,
    0x0001,
    0x005a,
    0x005b,
    0x005c,
    0x005b,
    0x005b,
};

__attribute__((used)) static const char16_t miscIcons1[] = { 0x0031, 0xf777 };
// Lifeup, Coin

__attribute__((used)) static const char16_t miscIcons2[] = { 0x0037, 0x0040 };

__attribute__((used)) static const char16_t color1[] = { 0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0xffff };
__attribute__((used)) static const char16_t color2[] = { 0x0006, 0x0005, 0x0004, 0x0005, 0x0005, 0x0004, 0x0005, 0x0000, 0x0000 };

__attribute__((used)) static const char16_t colors[] = { 0xf777, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0xffff };

enum ProjectTextColors
{
    Black = 0,
    Yellow = 1,
    White = 2,
    Red = 3,
    Green = 4,
    Blue = 5,
    Gray = 6,
    Rough = 7,
    ResetColor = 8,
};

__attribute__((used)) static const char* itemSystemMessageFiles[] = {
    "ItemCap",
    "ItemCloth",
    "ItemSticker",
    "ItemGift",
    "HackList"
    "ItemMoon",
};