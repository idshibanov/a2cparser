#include <array>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>

constexpr uint32_t a2cFileType = 0x04507989;

constexpr size_t BufferSizeLimit = 2560;
char readBuffer[BufferSizeLimit] = {};

static std::random_device rd;
static std::mt19937 gen( rd() );
std::uniform_int_distribution<uint16_t> seedDistribution( 0, 0xFFFF );

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

struct StatsBlock
{
    uint32_t score[4] = { 0, 0, 0, 0 };
    uint32_t gold = 0;
    uint8_t stat[4] = { 0, 0, 0, 0 };
    uint32_t spells[2] = { 0, 0 };
    uint32_t exp[5] = { 0, 0, 0, 0, 0 };
};

constexpr size_t ModifiersCount = 16;
static const std::array<std::pair<uint16_t, std::function<uint32_t( uint32_t, uint32_t, uint32_t, bool )>>, ModifiersCount> Modifiers = {
    // 4 byte values
    {
        { 0x1, []( auto x, auto s, auto p, bool add = true ) { return x ^ 0x1529251; } },
        { 0x2, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + s * 5 + 0x13141516 : x - s * 5 - 0x13141516; } },
        { 0x4, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 7 + 0xabcdef : x - p * 7 - 0xabcdef; } },
        { 0x8, []( auto x, auto s, auto p, bool add = true ) { return x ^ 0x17ff12aa; } },
        { 0x10, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + s * 3 + 0xdeadbabe : x - s * 3 - 0xdeadbabe; } },
        // 1 byte, attributes
        { 0x20, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + s * 19 + p * 17 : x - s * 19 - p * 17; } }, // keep the same streak as before
        { 0x40, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 3 : x - p * 3; } },
        { 0x80, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 5 + s : x - p * 5 - s; } },
        { 0x100, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + s * 7 + p * 9 : x - s * 7 - p * 9; } },
        // 4 bytes with late mask
        { 0x4000, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x - 0x10121974 : x + 0x10121974; } },
        { 0x2000, []( auto x, auto s, auto p, bool add = true ) { return x; } },
        // exp table
        { 0x200, []( auto x, auto s, auto p, bool add = true ) { return x ^ 0xdadedade; } },
        { 0x400, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 0xFFFFF88F : x - p * 0xFFFFF88F; } },
        { 0x800, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 0xFFFFF88F : x - p * 0xFFFFF88F; } },
        { 0x1000, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 0xFFFFF88F : x - p * 0xFFFFF88F; } },
        { 0x2000, []( auto x, auto s, auto p, bool add = true ) { return ( add ) ? x + p * 0xFFFFF88F : x - p * 0xFFFFF88F; } },
    } };

enum class ReadingState
{
    MALFORMED,
    MISMATCH,
    VALID
};

uint32_t calculateChecksum( char * sectionData, const uint32_t length )
{
    uint32_t sectionChecksum = 0;
    for ( uint32_t idx = 0; idx < length; idx++ ) {
        sectionChecksum = sectionChecksum * 2 + static_cast<uint8_t>( sectionData[idx] );
    }
    return sectionChecksum;
}

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
T deobfuscateValue( T value, T start, T previous, F && modifier, bool add = true )
{
    std::cout << "Converting " << std::hex << (uint32_t)value;
    value = static_cast<T>( modifier( value, start, previous, add ) );
    std::cout << " to " << (uint32_t)value << std::endl << std::dec;

    return value;
}

template <typename T>
T decryptStatsSection( std::ifstream & infile, T * dataPtr, uint16_t crypt, size_t modIndex, size_t modCount, T start = 0, T previous = 0 )
{
    T value = 0;
    char * valuePtr = reinterpret_cast<char *>( &value );
    bool initialized = false;

    for ( int i = 0; i < modCount; i++ ) {
        const auto modifierPair = Modifiers[i + modIndex];

        if ( crypt & modifierPair.first ) {
            infile.ignore( 1 );
        }


        infile.read( valuePtr, sizeof( value ) );

        previous = deobfuscateValue<T>( value, start, previous, modifierPair.second );
        if ( !initialized ) {
            start = previous;
            initialized = true;
        }

        *dataPtr = previous;
        dataPtr++;
    }
    return previous;
}

template <typename T>
int obfuscateBlock( T * source, char * output, SectionHeader & header, size_t modIndex, size_t modCount, T start = 0, T previous = 0 )
{
    int offset = 0;
    bool initialized = false;

    for ( int i = 0; i < modCount; i++ ) {
        const auto modifierPair = Modifiers[modIndex + i];

        if ( header.crypt & modifierPair.first ) {
            *( output + offset ) = 0x7F;
            offset++;
            header.length++;
        }

        previous = deobfuscateValue<T>( *source, start, previous, modifierPair.second, false );
        std::swap( previous, *source );
        if ( !initialized ) {
            start = previous;
            initialized = true;
        }

        T * outputPtr = reinterpret_cast<T *>( output + offset );
        *outputPtr = *source;

        offset += sizeof( T );
        source++;
    }
    return offset;
}

void serializeStatsBlock( StatsBlock & stats, SectionHeader & header, char * output )
{
    std::cout << "Starting block length is " << header.length << std::endl;
    header.magic = DataBlocks[4];
    header.length = sizeof( StatsBlock );
    // header.crypt = seedDistribution( gen );
    header.checksum = calculateChecksum( reinterpret_cast<char *>( &stats ), header.length );

    /*
    *reinterpret_cast<SectionHeader *>( output ) = header;
    output += sizeof( header );
    */

    uint8_t s = stats.score[0] & 0xFF;
    uint8_t p = stats.gold & 0xFF;
    int offset = obfuscateBlock( stats.score, output, header, 0, 5 );
    offset += obfuscateBlock<uint8_t>( stats.stat, output + offset, header, 5, 4, s, p );
    offset += obfuscateBlock( stats.spells, output + offset, header, 9, 2 );
    offset += obfuscateBlock( stats.exp, output + offset, header, 11, 5 );

    assert( offset == header.length );

    std::cout << "Serialized block length is " << header.length << std::endl;
}

StatsBlock parseStatsBlock( std::ifstream & infile, SectionHeader header )
{
    std::cout << "Parsing stats block " << std::hex << header.magic << std::endl;

    StatsBlock stats = {};

    decryptStatsSection<uint32_t>( infile, stats.score, header.crypt, 0, 5 );

    // 1 byte values that rely on prev block
    decryptStatsSection<uint8_t>( infile, stats.stat, header.crypt, 5, 4, stats.score[0] & 0xFF, stats.gold & 0xFF );

    // Fields are not in order of processing
    decryptStatsSection<uint32_t>( infile, stats.spells, header.crypt, 9, 2 );

    decryptStatsSection<uint32_t>( infile, stats.exp, header.crypt, 11, 5 );

    return stats;
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
        StatsBlock stats = parseStatsBlock( infile, header );

        serializeStatsBlock( stats, header, readBuffer );

        // Original header len is wrong at times; skip
        // assert( pp == blockEndPosition );
        // infile.seekg( blockEndPosition );
    }
    else {
        infile.read( reinterpret_cast<char *>( &readBuffer ), header.length );
        decryptData( readBuffer, header );

        const uint32_t sectionChecksum = calculateChecksum( readBuffer, header.length );
        if ( sectionChecksum != header.checksum ) {
            std::cerr << "Invalid checksum! " << sectionChecksum << " expected " << header.checksum << ". Len is " << header.length << std::endl;
            return ReadingState::MISMATCH;
        }
    }


    outfile.write( reinterpret_cast<const char *>( &header ), sizeof( header ) );
    outfile.write( reinterpret_cast<const char *>( readBuffer ), header.length );

    return ReadingState::VALID;
}

int main()
{

    std::cout << "Start a2c -> bin conversion" << std::endl;

    std::ifstream infile( "342679700274.a2c", std::ios::binary );
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
