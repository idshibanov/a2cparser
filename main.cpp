#include <iostream>
#include <fstream>

const uint32_t a2cFileType = 0x04507989;
const uint32_t HeadPlayerInfo = 0xAAAAAAAA;
const uint32_t HeadStatsBlock = 0x55555555;

struct SectionHeader {
	uint32_t magic = 0;
	uint32_t length = 0;
	uint16_t unused = 0;
	uint16_t crypt = 0;
	uint32_t checksum = 0;
};

void decryptData(uint8_t * sectionData, SectionHeader header) {
	const uint32_t seed = header.crypt;

	uint32_t checksum = seed & 0xffff | (seed & 0xffff) << 0x10;
	//std::cout << "Checksum " << std::hex << checksum << std::endl;

	for (uint32_t idx = 0; idx < header.length; idx++) {
		const uint8_t edx = static_cast<uint8_t>(checksum >> 0x10);

		//std::cout << "Symbol " << std::hex << (int)(sectionData[idx]);
		//std::cout << " XOR " << std::hex << edx;

		sectionData[idx] = sectionData[idx] ^ edx;

		//std::cout << " into " << std::hex << (int)(sectionData[idx]) << std::endl;
		checksum = checksum << 1;

		if ((idx & 0xf) == 0xf) {
			checksum = checksum | seed & (0xffff);
		}
	}
}

bool verifyChecksum(uint8_t* sectionData, SectionHeader header) {
	uint32_t sectionChecksum = 0;
	for (uint32_t idx = 0; idx < header.length; idx++) {
		sectionChecksum = sectionChecksum * 2 + sectionData[idx];
	}
	std::cout << "Comparing checksum " << sectionChecksum << " expected " << header.checksum << std::endl;
	return sectionChecksum == header.checksum;
}

int main() {
	uint8_t buffer[256] = {};
	std::cout << "Start" << std::endl;

	std::ifstream infile("342679700273.a2c", std::ios::binary);
	std::ofstream outfile("output.bin", std::ios::binary);
	if (!infile) {
		std::cerr << "Cannot open a2c file for reading!\n";
		return 2;
	}

	uint32_t fileType = 0;
	infile.read(reinterpret_cast<char*>(&fileType), sizeof(fileType));

	if (fileType != a2cFileType) {
		std::cerr << "It's not an a2c file!\n";
		return 2;
	}
	outfile.write(reinterpret_cast<const char*>(&fileType), sizeof(fileType));

	std::streampos pos = infile.tellg();
	std::cout << "Current file position: " << pos << std::endl;

	// Process player info

	SectionHeader header;
	infile.read(reinterpret_cast<char*>(&header), sizeof(SectionHeader));
	std::cout << "Reading " << std::hex << header.magic << " header." << std::endl;

	if (header.magic == HeadPlayerInfo) {
		std::cout << "Parsing player info section. Seed is " << std::hex << header.crypt << std::endl;

		if (header.length > 256) {
			std::cerr << "Too big of a section! Skipping! " << header.length << " bytes." << std::endl;
			return 2;
		}
		infile.read(reinterpret_cast<char*>(&buffer), header.length);

		std::cout << "Data before: " << buffer << std::endl;
		decryptData(buffer, header);
		std::cout << "Data after: " << buffer << std::endl;

		if (!verifyChecksum(buffer, header)) {
			std::cerr << "Invalid checksum!" << std::endl;
			return 2;
		}

		outfile.write(reinterpret_cast<const char*>(&header), sizeof(header));
		outfile.write(reinterpret_cast<const char*>(buffer), header.length);
	}

	infile.read(reinterpret_cast<char*>(&header), sizeof(header));
	std::cout << "Reading " << std::hex << header.magic << " header." << std::endl;

	if (header.magic == HeadStatsBlock) {
		std::cout << "Parsing stats section. Seed is " << std::hex << header.crypt << std::endl;

		if (header.length > 256) {
			std::cerr << "Too big of a section! Skipping! " << header.length << " bytes." << std::endl;
			return 2;
		}
		infile.read(reinterpret_cast<char*>(&buffer), header.length);

		std::cout << "Data before: " << buffer << std::endl;
		decryptData(buffer, header);
		std::cout << "Data after: " << buffer << std::endl;

		if (!verifyChecksum(buffer, header)) {
			std::cerr << "Invalid checksum!" << std::endl;
			return 2;
		}

		outfile.write(reinterpret_cast<const char*>(&header), sizeof(header));
		outfile.write(reinterpret_cast<const char*>(buffer), header.length);
	}

	infile.close();
	outfile.close();

	std::cout << "Wrote " << header.length << " bytes to output.bin\n";

	return 0;
}