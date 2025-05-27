#pragma once
#include <ostream>
#include <vector>

const uint16_t PKT_Magic = 0x1e4a;

struct PKTHeader
{
    // skip 1 byte
    uint16_t unknown1 = 0;
    uint16_t itemCount = 0;
    uint16_t unknown2 = 0;
    uint16_t magic = 0;
};

struct ItemHeader
{
    uint8_t recipeId = 0;
    uint8_t materialId = 0;
    uint8_t unknown1 = 0;
    uint8_t unknown2 = 0;
    uint8_t flags = 0;
    uint8_t modifierCount = 0;
    uint8_t itemLength = 0;
};

struct Item
{
    uint8_t quality = 0;
    uint8_t materialId = 0;
    uint16_t recipeId = 0;
    uint8_t flags;
    uint32_t price = 0;
    std::vector<std::pair<uint8_t, uint8_t>> modifiers;

    friend std::ostream & operator<<( std::ostream & os, const Item & it );
};

void parsePkt( const char * filename );
