#include <array>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>

constexpr uint32_t a2cFileType = 0x04507989;

constexpr size_t BufferSizeLimit = 2560;
char readBuffer[BufferSizeLimit] = {};

constexpr std::array<uint32_t, 6> DataBlocks = {
    0xAAAAAAAA, // player info
    0x55555555, // state
    0x40A40A40, // serialized item names
    0xDE0DE0DE, // unknown
    0x41392521, // char stats
    0x3A5A3A5A, // unknown
};
constexpr size_t ExpectedStatBlockSize = 52;

struct SectionHeader
{
    uint32_t magic = 0;
    uint32_t length = 0;
    uint16_t unused = 0;
    uint16_t crypt = 0;
    uint32_t checksum = 0;
};

constexpr size_t ModifiersCount = 16;
static const std::array<std::function<uint32_t( uint32_t, uint32_t, uint32_t )>, ModifiersCount> Modifiers = {
    // 4 byte values
    []( auto x, auto s, auto p ) { return x ^ 0x1529251; },
    []( auto x, auto s, auto p ) { return x + s * 5 + 0x13141516; },
    []( auto x, auto s, auto p ) { return x + p * 7 + 0xabcdef; },
    []( auto x, auto s, auto p ) { return x ^ 0x17ff12aa; },
    []( auto x, auto s, auto p ) { return x + s * 3 + 0xDEADBABE; },
    // 1 byte, attributes
    []( auto x, auto s, auto p ) { return x + s * 19 + p * 17; }, // keep the same streak as before
    []( auto x, auto s, auto p ) { return x + p * 3; },
    []( auto x, auto s, auto p ) { return x + p * 5 + s; },
    []( auto x, auto s, auto p ) { return x + s * 7 + p * 9; },
    // 4 bytes with late mask
    []( auto x, auto s, auto p ) { return x - 0x10121974; },
    []( auto x, auto s, auto p ) { return x; },
    // exp table
    []( auto x, auto s, auto p ) { return x ^ 0xdadedade; },
    []( auto x, auto s, auto p ) { return x + p * 0xFFFFF88F; },
    []( auto x, auto s, auto p ) { return x + p * 0xFFFFF88F; },
    []( auto x, auto s, auto p ) { return x + p * 0xFFFFF88F; },
    []( auto x, auto s, auto p ) { return x + p * 0xFFFFF88F; },
};

enum class ReadingState
{
    MALFORMED,
    MISMATCH,
    VALID
};

void decryptData( char * sectionData, SectionHeader header )
{
    const uint32_t seed = header.crypt;

    uint32_t checksum = seed & 0xffff | ( seed & 0xffff ) << 0x10;
    // std::cout << "Checksum " << std::hex << checksum << std::endl;

    for ( uint32_t idx = 0; idx < header.length; idx++ ) {
        const uint8_t edx = static_cast<uint8_t>( checksum >> 0x10 );

        // std::cout << "Symbol " << (int)(sectionData[idx]);
        // std::cout << " XOR " << edx;

        sectionData[idx] = sectionData[idx] ^ edx;

        // std::cout << " into " << (int)(sectionData[idx]) << std::endl;
        checksum = checksum << 1;

        if ( ( idx & 0xf ) == 0xf ) {
            checksum = checksum | seed & ( 0xffff );
        }
    }
    // std::cout << std::dec;
}

template <typename T, typename F>
T deobfuscateValue( T value, char * buffer, T start, T previous, F && modifier )
{
    std::cout << "Converting " << std::hex << (uint32_t)value;
    value = static_cast<T>( modifier( value, start, previous ) );
    std::cout << " to " << (uint32_t)value << std::endl << std::dec;

    T * output = reinterpret_cast<T *>( buffer );
    output[0] = value;
    return value;
}

template <typename T>
T decryptStatsSection( std::ifstream & infile, char * sectionData, int & sectionOffset, uint16_t & mask, uint16_t crypt, size_t modIndex, size_t modCount, T start = 0,
                          T previous = 0 )
{
    T value = 0;
    char * valuePtr = reinterpret_cast<char *>( &value );
    bool initialized = false;

    for ( int i = 0; i < modCount; i++ ) {
        if ( crypt & mask ) {
            infile.ignore( 1 );
        }

        infile.read( valuePtr, sizeof( value ) );

        previous = deobfuscateValue<T>( value, sectionData + sectionOffset, start, previous, Modifiers[i + modIndex] );
        if ( !initialized ) {
            start = previous;
            initialized = true;
        }

        sectionOffset += sizeof( value );
        mask <<= 1;
    }
    return previous;
}

uint32_t processStatsBlock( std::ifstream & infile, char * sectionData, SectionHeader header )
{
    std::cout << "Processing special block " << std::hex << header.magic << std::endl;

    uint16_t mask = 0x1;
    int sectionOffset = 0;

    uint32_t lastValue = decryptStatsSection<uint32_t>( infile, sectionData, sectionOffset, mask, header.crypt, 0, 5 );
    uint32_t startingValue = *reinterpret_cast<uint32_t *>( sectionData );

    decryptStatsSection<uint8_t>( infile, sectionData, sectionOffset, mask, header.crypt, 5, 4, startingValue & 0xFF, lastValue & 0xFF );

    // Fields are not in order of processing
    mask = 0x4000;
    decryptStatsSection<uint32_t>( infile, sectionData, sectionOffset, mask, header.crypt, 9, 1 );
    mask = 0x2000;
    decryptStatsSection<uint32_t>( infile, sectionData, sectionOffset, mask, header.crypt, 10, 1 );

    // Return to existing ordering
    mask = 0x200;
    decryptStatsSection<uint32_t>( infile, sectionData, sectionOffset, mask, header.crypt, 11, 5 );

    std::cout << "Stats block shrunk to size " << std::dec << sectionOffset << " vs " << header.length << std::endl;
    assert( sectionOffset == ExpectedStatBlockSize );
    return sectionOffset;
}

bool verifyChecksum( char * sectionData, SectionHeader header )
{
    uint32_t sectionChecksum = 0;
    for ( uint32_t idx = 0; idx < header.length; idx++ ) {
        sectionChecksum = sectionChecksum * 2 + static_cast<uint8_t>( sectionData[idx] );
    }
    if ( sectionChecksum != header.checksum ) {
        std::cerr << "Invalid checksum! " << sectionChecksum << " expected " << header.checksum << ". Len is " << header.length << std::endl;
        return false;
    }
    return true;
}

ReadingState processBlock( std::ifstream & infile, std::ofstream & outfile, uint32_t blockHeading )
{
    std::cout << "Reading " << std::hex << blockHeading << std::dec << " block." << std::endl;

    SectionHeader header;
    infile.read( reinterpret_cast<char *>( &header ), sizeof( header ) );

    const auto headerPos = infile.tellg();
    if ( header.magic != blockHeading ) {
        std::cerr << "Unknown data section: " << header.magic << std::endl;
        return ReadingState::MALFORMED;
    }

    if ( header.length > BufferSizeLimit ) {
        infile.ignore( header.length );
        const auto offset = infile.tellg() - headerPos;
        std::cerr << "Section to big to process! Skipping " << offset << " bytes." << std::endl;
        return ReadingState::MISMATCH;
    }

    if ( blockHeading == DataBlocks[4] ) {
        const int startPos = infile.tellg();
        const int blockEndPosition = startPos + header.length;

        // This block omits decoding bytes so len should be smaller
        const uint32_t updatedLen = processStatsBlock( infile, readBuffer, header );
        assert( updatedLen <= header.length );
        header.length = updatedLen;

        // Original header len is wrong at times; skip
        // assert( pp == blockEndPosition );
        // infile.seekg( blockEndPosition );
    }
    else {
        infile.read( reinterpret_cast<char *>( &readBuffer ), header.length );
        decryptData( readBuffer, header );
    }

    if ( !verifyChecksum( readBuffer, header ) ) {
        return ReadingState::MISMATCH;
    }

    outfile.write( reinterpret_cast<const char *>( &header ), sizeof( header ) );
    outfile.write( reinterpret_cast<const char *>( readBuffer ), header.length );

    return ReadingState::VALID;
}

int main()
{
    std::cout << "Start a2c -> bin conversion" << std::endl;

    std::ifstream infile( "25239459341741.a2c", std::ios::binary );
    if ( !infile ) {
        std::cerr << "Cannot open a2c file for reading!\n";
        return 2;
    }

    uint32_t fileType = 0;
    infile.read( reinterpret_cast<char *>( &fileType ), sizeof( fileType ) );

    if ( fileType != a2cFileType ) {
        std::cerr << "It's not an a2c file!\n";
        return 2;
    }

    std::ofstream outfile( "output.bin", std::ios::binary );
    if ( !outfile ) {
        std::cerr << "Cannot create output bin file!\n";
        return 2;
    }
    outfile.write( reinterpret_cast<const char *>( &fileType ), sizeof( fileType ) );

    for ( const uint32_t blockHeading : DataBlocks ) {
        if ( processBlock( infile, outfile, blockHeading ) == ReadingState::MALFORMED ) {
            break;
        }
    }

    std::cout << "Wrote " << outfile.tellp() << " bytes to output.bin\n";

    infile.close();
    outfile.close();

    return 0;
}
