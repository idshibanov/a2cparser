#include "pkt_file.h"
#include <cassert>
#include <fstream>
#include <iostream>

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
        newItem.recipeId = (header.recipeId & 0xE0) >> 5;
        newItem.materialId = (header.materialId & 0xF0) >> 4;
        newItem.itemType = (header.recipeId & 0x1F) << 8 | header.materialId & 0xF;
        newItem.flags = header.flags;

        std::cout << "### Item Type " << newItem.itemType << ";";
        for ( int idx = 0; idx < header.modifierCount; idx++ ) {
            uint8_t type = 0;
            infile.read( reinterpret_cast<char *>( &type ), sizeof( uint8_t ) );

            if ( type == 0x1 ) {
                infile.read( reinterpret_cast<char *>( &newItem.price ), sizeof( uint32_t ) );
                std::cout << " cost " << newItem.price;
            }
            else {
                uint8_t value = 0;
                infile.read( reinterpret_cast<char *>( &value ), sizeof( uint8_t ) );

                std::cout << ", mod " << (int)type << "-" << (int)value;
                newItem.modifiers.emplace_back( type, value );
            }
        }
        std::cout << std::endl;

        items.push_back( newItem );
    }

    assert( items.size() == fileType.itemCount );

    infile.close();



}
