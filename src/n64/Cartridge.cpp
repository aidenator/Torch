#include "Cartridge.h"

#include "lib/binarytools/BinaryReader.h"
#include <Companion.h>

void N64::Cartridge::Initialize() {
    // Read straight from the ROM: a BinaryReader would copy all of it
    const std::vector<uint8_t>& rom = *this->gRomData;
    auto byteAt = [&rom](size_t offset) -> uint8_t { return offset < rom.size() ? rom[offset] : 0; };
    const uint32_t crc = (uint32_t)byteAt(0x10) << 24 | (uint32_t)byteAt(0x11) << 16 | (uint32_t)byteAt(0x12) << 8 |
                         (uint32_t)byteAt(0x13);
#ifdef ROM_CRC_BSWAP
    this->gRomCRC = BSWAP32(crc);
#else
    this->gRomCRC = crc;
#endif
    // The name at 0x20, up to its null
    this->gGameTitle.clear();
    for (size_t offset = 0x20; offset < rom.size() && rom[offset] != '\0'; offset++) {
        this->gGameTitle += (char)rom[offset];
    }
    uint8_t country = byteAt(0x3E);
    this->gVersion = byteAt(0x3F);
    this->gHash = Companion::CalculateHash(rom);
    switch (country) {
        case 'J':
            this->gCountryCode = CountryCode::Japan;
            break;
        case 'E':
            this->gCountryCode = CountryCode::NorthAmerica;
            break;
        case 'P':
            this->gCountryCode = CountryCode::Europe;
            break;
        default:
            this->gCountryCode = CountryCode::Unknown;
            break;
    }
    // Everything needed from the ROM is read now
    this->gRomData = nullptr;
}

const std::string& N64::Cartridge::GetGameTitle() {
    return this->gGameTitle;
}

N64::CountryCode N64::Cartridge::GetCountry() {
    return this->gCountryCode;
}

uint8_t N64::Cartridge::GetVersion() const {
    return this->gVersion;
}

std::string N64::Cartridge::GetHash() {
    return this->gHash;
}

std::string N64::Cartridge::GetCountryCode() {
    switch (this->gCountryCode) {
        case CountryCode::Japan:
            return "jp";
        case CountryCode::NorthAmerica:
            return "us";
        case CountryCode::Europe:
            return "eu";
        default:
            return "unk";
    }
}

uint32_t N64::Cartridge::GetCRC() {
    return this->gRomCRC;
}
