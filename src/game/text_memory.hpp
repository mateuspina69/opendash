// The bytes a text pointer names (a window's read / layout pointers, a bank's messages, the inserted texts): the
// game's built texts (game/loaded_data BuiltData: the texts fb 21 / fb 23 insert, the options page's copies), a loaded
// bank (the stage's message bank, a language pack's banks) or a bank of the program's data. A code that patches a
// program bank (fb 22's key glyph) keeps its byte here (patchedProgramText); the pointers keep the exe's encoding.
#pragma once

#include <cstdint>
#include <map>

namespace od::port {

// the text byte at an address
uint8_t textByte(uint32_t address);
// the n text bytes from the address are readable (a scan that stops at the data's end)
bool textMapped(uint32_t address, uint32_t n = 1);
// a write into text: the game's built text, a loaded bank's byte, else a patch of a program bank's byte
void storeTextByte(uint32_t address, uint8_t v);
// the program banks' bytes a code patched (the exe writes its own data; the translator lays them out)
std::map<uint32_t, uint8_t>& patchedProgramText();

// The bytes of a message's text from an address on (glyphs, control codes and their operands; the operands' 16 / 32-bit
// values are big endian, the page header little endian)
class TextBytes {
public:
    // one byte, read and written where it lives
    class Byte {
    public:
        explicit Byte(uint32_t address) : a_(address) {}
        operator uint8_t() const { return textByte(a_); }
        Byte& operator=(uint8_t v) {
            storeTextByte(a_, v);
            return *this;
        }
        Byte& operator=(const Byte& o) { return *this = uint8_t(o); }

    private:
        uint32_t a_;
    };

    explicit TextBytes(uint32_t address) : at(address) {}

    // the byte at i (writable: an inserted text's buffer, a key name patched)
    Byte byte(uint32_t i) { return Byte(at + i); }
    uint8_t byte(uint32_t i) const { return textByte(at + i); }
    // the byte at i as a signed operand
    int8_t signedByte(uint32_t i) const { return int8_t(byte(i)); }
    // the big-endian 16-bit operand at i (the exe's CONCAT11(p[i], p[i + 1]))
    uint16_t be16(uint32_t i) const { return uint16_t(byte(i) << 8 | byte(i + 1)); }
    // the big-endian 32-bit operand at i
    int32_t be32(uint32_t i) const {
        return int32_t(uint32_t(byte(i)) << 24 | uint32_t(byte(i + 1)) << 16 | uint32_t(byte(i + 2)) << 8 | byte(i + 3));
    }
    // the little-endian 16-bit word at i (a page header, an offset table entry)
    uint16_t word(uint32_t i) const { return uint16_t(byte(i) | byte(i + 1) << 8); }
    // the n bytes from i / from the start are readable
    bool mappedAt(uint32_t i, uint32_t n) const { return textMapped(at + i, n); }
    bool mapped(uint32_t n) const { return textMapped(at, n); }

    uint32_t at;
};

// A message bank: u16 offsets from the bank's start by message id, then the messages (a u16 page header, the bytes)
class MessageBank {
public:
    explicit MessageBank(uint32_t address) : at(address) {}
    // the address of message `id`'s first page (its header)
    uint32_t message(int32_t id) const { return at + TextBytes(at + uint32_t(id * 2)).word(0); }
    // message `id`'s offset entry is readable (a host that has only part of the data)
    bool entryMapped(int32_t id) const { return textMapped(at + uint32_t(id * 2), 2); }

    uint32_t at;
};

} // namespace od::port
