// The user's dash2.exe read as a FILE (a PE32 image): its section headers, and the bytes at a virtual address turned
// into a file offset through them. Nothing is mapped as memory: a section's bytes are the file's, its zero-filled
// tail (.bss, a virtual size past the raw size) reads as zeros. The one parser of the exe's headers (the PC data
// source reads the exe through it; so do the tests' exe image and stage table check).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace od {

class PeImage {
public:
    struct Section {
        uint32_t va = 0;      // absolute (image base + RVA)
        uint32_t vsize = 0;   // the section's size in memory
        uint32_t rawOff = 0;  // its bytes' offset in the file
        uint32_t rawSize = 0; // its bytes in the file (the rest of vsize: zero)
        bool text = false;    // the code section (.text)
        // the bytes of [va, va + max(vsize, rawSize)) the file holds (rawSize clipped to the file's end)
        uint32_t fileBytes = 0;
        uint32_t extent() const { return vsize > rawSize ? vsize : rawSize; }
    };

    explicit PeImage(std::vector<uint8_t> file); // throws std::runtime_error on a non-PE32 / truncated file
    static PeImage read(const std::filesystem::path& exe);

    uint32_t imageBase() const { return imageBase_; }
    const std::vector<Section>& sections() const { return sections_; }
    const Section* text() const;
    // the section holding [va, va + n) (nullptr: none)
    const Section* sectionOf(uint32_t va, uint32_t n) const;
    // [va, va + n) lies in a section's file bytes (not the zero tail)
    bool inFile(uint32_t va, uint32_t n) const;
    // the bytes [va, va + n) of a section (its zero tail included) copied into `out`; throws when no section holds
    // them. Marks the exe coverage (engine/exe_coverage) of the bytes read.
    void copy(uint32_t va, uint32_t n, uint8_t* out) const;
    // the file bytes [va, va + n) without a copy (throws when they are not all in the file)
    std::span<const uint8_t> fileView(uint32_t va, uint32_t n) const;
    // the section's whole extent, its zero tail included (the data source's block of it)
    std::vector<uint8_t> sectionBytes(const Section& s) const;
    // the file itself
    std::span<const uint8_t> file() const { return file_; }

private:
    std::vector<uint8_t> file_;
    uint32_t imageBase_ = 0;
    std::vector<Section> sections_;
};

} // namespace od
