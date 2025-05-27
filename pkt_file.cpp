#include "pkt_file.h"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <tuple>

std::ostream & operator<<( std::ostream & os, const Item & it )
{
    os << "Item #" << it.recipeId << ", Q:" << (int)it.quality << " M:" << (int)it.materialId << ", Price: " << it.price;
    for ( auto & mod : it.modifiers ) {
        os << ", " << (int)mod.first << "-" << (int)mod.second;
    }
    os << std::endl;
    return os;
}

void serializeItem( const Item & item )
{
    ItemHeader header;
    header.flags = item.flags;
    header.modifierCount = item.modifiers.size() + 1;
    header.itemLength = header.modifierCount * 2 + 3;

    const uint16_t packedIds = item.recipeId | ( item.materialId << 12 ) | ( item.quality << 5 );

    uint16_t * itemId = reinterpret_cast<uint16_t *>( &header );
    *itemId = packedIds;
}

void parsePkt( const char * filename )
{
    std::cout << "Start pkt -> txt conversion" << std::endl;

    std::ifstream infile( filename, std::ios::binary );
    if ( !infile ) {
        std::cerr << "Cannot open pkt file for reading!\n";
        return;
    }

    // skip first byte
    infile.ignore( 1 );

    PKTHeader fileType;
    infile.read( reinterpret_cast<char *>( &fileType ), sizeof( fileType ) );

    if ( fileType.magic != PKT_Magic ) {
        std::cerr << "This isn't a pkt file!\n";
        return;
    }

    std::vector<Item> items;

    ItemHeader header;
    while ( infile.peek() != EOF ) {
        infile.read( reinterpret_cast<char *>( &header ), sizeof( header ) );

        std::cout << "Reading item " << std::hex << (int)header.recipeId << (int)header.materialId << std::dec;
        std::cout << ": " << (int)header.modifierCount << " modifiers, len is " << (int)header.itemLength << std::endl;

        assert( header.unknown1 == 0x1 );
        assert( header.itemLength == ( header.modifierCount * 2 + 3 ) );

        Item newItem;
        newItem.quality = ( header.recipeId & 0xE0 ) >> 5;
        newItem.materialId = ( header.materialId & 0xF0 ) >> 4;
        newItem.recipeId = ( header.materialId & 0xF ) << 8 | header.recipeId & 0x1F;
        newItem.flags = header.flags;

        for ( int idx = 0; idx < header.modifierCount; idx++ ) {
            uint8_t type = 0;
            infile.read( reinterpret_cast<char *>( &type ), sizeof( uint8_t ) );

            if ( type == 0x1 ) {
                infile.read( reinterpret_cast<char *>( &newItem.price ), sizeof( uint32_t ) );
            }
            else {
                uint8_t value = 0;
                infile.read( reinterpret_cast<char *>( &value ), sizeof( uint8_t ) );

                newItem.modifiers.emplace_back( type, value );
            }
        }

        serializeItem( newItem );

        items.push_back( newItem );
    }

    assert( items.size() == fileType.itemCount );

    std::sort( items.begin(), items.end(), []( Item & left, Item & right ) {
        return std::tie( left.recipeId, left.quality, left.materialId ) < std::tie( right.recipeId, right.quality, right.materialId );
    } );

    std::cout << "### " << items.size() << " Items ###" << std::endl;
    for ( auto & item : items ) {
        std::cout << item;
    }

    infile.close();
}
