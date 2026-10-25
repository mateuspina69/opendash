// Owned file bytes and references into them. File readers retain their backing block;
// replacing a bank never invalidates a reader already in use. Copies share unchanged
// blocks between snapshots; readers follow writes in their owning snapshot.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "engine/game_file.hpp"

namespace od {

// Readers retain this binding, not the byte buffer's snapshot ownership. A
// copied block gets its own binding so detaching it also updates its readers.
struct FileDataStorage {
    std::shared_ptr<std::vector<uint8_t>> bytes;
};

class FileDataReader {
public:
    FileDataReader() = default;
    FileDataReader(std::shared_ptr<const FileDataStorage> storage, size_t offset)
        : storage_(std::move(storage)), offset_(offset) {}
    template <class T> T read(size_t offset) const {
        static_assert(std::is_integral_v<T>);
        const auto* data = storage_ ? storage_->bytes.get() : nullptr;
        if (!data || offset_ > data->size() || offset > data->size() - offset_ ||
            sizeof(T) > data->size() - offset_ - offset)
            throw std::out_of_range("file data: record outside loaded block");
        T value;
        std::memcpy(&value, data->data() + offset_ + offset, sizeof(T));
        return value;
    }
    FileDataReader sub(size_t offset) const { return {storage_, offset_ + offset}; }
    FileDataReader relative(int64_t offset) const {
        if (offset < 0 && uint64_t(-offset) > offset_)
            throw std::out_of_range("file data: frame before loaded block");
        return {storage_, size_t(int64_t(offset_) + offset)};
    }
private:
    std::shared_ptr<const FileDataStorage> storage_;
    size_t offset_ = 0;
};

// A typed reference to a record in a loaded file, independent of the image and
// the lifetime of the loader that resolved the file's pointer encoding.
template <class Record> class FileDataRef {
public:
    explicit FileDataRef(FileDataReader reader) : reader_(std::move(reader)) {}
    Record get() const { return Record::load(reader_); }
private:
    FileDataReader reader_;
};

class FileDataBlock {
public:
    FileDataBlock() : FileDataBlock(std::vector<uint8_t>{}) {}
    FileDataBlock(std::vector<uint8_t> bytes)
        : storage_(std::make_shared<FileDataStorage>(
              FileDataStorage{std::make_shared<std::vector<uint8_t>>(std::move(bytes))})) {}
    template <class It> FileDataBlock(It first, It last)
        : FileDataBlock(std::vector<uint8_t>(first, last)) {}
    FileDataBlock(std::span<const uint8_t> bytes) : FileDataBlock(bytes.begin(), bytes.end()) {}
    FileDataBlock(const FileDataBlock& other)
        : storage_(std::make_shared<FileDataStorage>(*other.storage_)) {}
    FileDataBlock& operator=(const FileDataBlock& other) {
        if (this != &other) storage_ = std::make_shared<FileDataStorage>(*other.storage_);
        return *this;
    }
    FileDataBlock(FileDataBlock&&) noexcept = default;
    FileDataBlock& operator=(FileDataBlock&&) noexcept = default;
    size_t size() const { return storage_->bytes->size(); }
    bool empty() const { return storage_->bytes->empty(); }
    const uint8_t* data() const { return storage_->bytes->data(); }
    auto begin() const { return storage_->bytes->cbegin(); }
    auto end() const { return storage_->bytes->cend(); }
    const uint8_t& operator[](size_t i) const { return (*storage_->bytes)[i]; }
    // Only an explicit write can copy bytes, once after a snapshot copy. Reader
    // creation and all read accessors leave both the binding and buffer intact.
    uint8_t* mutableData() { detach(); return storage_->bytes->data(); }
    FileDataReader reader(size_t offset) const { return {storage_, offset}; }
    // the same block, not a copy: a write through either is a write of both (an image that keeps a view of the store)
    FileDataBlock share() const { return FileDataBlock(storage_); }
private:
    explicit FileDataBlock(std::shared_ptr<FileDataStorage> storage) : storage_(std::move(storage)) {}
    void detach() {
        if (storage_->bytes.use_count() != 1)
            storage_->bytes = std::make_shared<std::vector<uint8_t>>(*storage_->bytes);
    }
    std::shared_ptr<FileDataStorage> storage_;
};

// Loaded payloads belong to this store. The image can retain a compatibility
// copy for the unconverted state/tests, but file readers only use these blocks.
class LoadedFileData {
public:
    // PS1 RAM 0 in the PC process (FUN_0040eb90): a file's code parts load at this + their PS1 address
    static constexpr uint32_t kPsxBase = 0x630000;
    // the file's code parts at their load addresses
    void mapCode(const GameFile& file);
    // (overlay: the bytes written into every block they overlap, as one memory takes a store -- a newer block over part
    // of the range keeps no stale bytes there -- and a block of their own on top when no block holds them whole)
    void map(uint32_t base, std::span<const uint8_t> bytes, bool overlay = true) {
        if (overlay) {
            bool held = false;
            const uint64_t end = uint64_t(base) + bytes.size();
            for (auto& block : blocks_) {
                const uint64_t blockEnd = uint64_t(block.base) + block.bytes.size();
                const uint64_t from = std::max<uint64_t>(base, block.base), to = std::min(end, blockEnd);
                if (from >= to) continue;
                std::copy(bytes.begin() + std::ptrdiff_t(from - base), bytes.begin() + std::ptrdiff_t(to - base),
                          block.bytes.mutableData() + (from - block.base));
                held |= from == base && to == end;
            }
            if (held) return;
        }
        Block block{base, FileDataBlock(bytes.begin(), bytes.end())};
        if (overlay) blocks_.insert(blocks_.begin(), std::move(block));
        else blocks_.push_back(std::move(block));
    }
    // a block shared with its owner (FileDataBlock::share), on top of the ones mapped before (overlay) or under them
    void mapShared(uint32_t base, FileDataBlock block, bool overlay = true) {
        if (overlay) blocks_.insert(blocks_.begin(), Block{base, std::move(block)});
        else blocks_.push_back(Block{base, std::move(block)});
    }
    void zero(uint32_t base, uint32_t size) {
        blocks_.insert(blocks_.begin(), Block{base, FileDataBlock(std::vector<uint8_t>(size, 0))});
    }
    // Legacy writes to mutable loaded records (UVs, grid switches), including
    // test/host snapshot imports. Match the image's first containing region,
    // including executable-backed banks, without modifying shadowed blocks.
    void write(uint32_t address, const void* data, uint32_t size) {
        for (auto& block : blocks_) {
            if (contains(block, address, size)) {
                std::memcpy(block.bytes.mutableData() + (address - block.base), data, size);
                return;
            }
        }
    }
    static std::out_of_range unresolved(uint32_t address, uint32_t size) {
        char text[96];
        std::snprintf(text, sizeof text, "file data: unresolved loaded-file reference 0x%08x (+%u)", address, size);
        return std::out_of_range(text);
    }
    bool mapped(uint32_t address, uint32_t size = 1) const {
        for (const auto& block : blocks_) if (contains(block, address, size)) return true;
        return false;
    }
    // the bytes at address (valid until the next write or map of the store)
    std::span<const uint8_t> view(uint32_t address, uint32_t size) const {
        for (const auto& block : blocks_)
            if (contains(block, address, size)) return {block.bytes.data() + (address - block.base), size};
        throw unresolved(address, size);
    }
    FileDataReader reader(uint32_t address, uint32_t size = 1) const {
        for (const auto& block : blocks_)
            if (contains(block, address, size)) return block.bytes.reader(address - block.base);
        throw unresolved(address, size);
    }
private:
    struct Block { uint32_t base; FileDataBlock bytes; };
    static bool contains(const Block& block, uint32_t address, size_t size) {
        return address >= block.base && uint64_t(address) + size <= uint64_t(block.base) + block.bytes.size();
    }
    std::vector<Block> blocks_;
};

// ---- the bound store -----------------------------------------------------------------------------------------------
// The loaded files the game reads and writes (the stage's banks, the player's files, the messages ...): its host (the
// app's session, each test) creates the store and binds it before the game runs, as it binds the data source.

// the bound store; throws std::logic_error when none is bound
LoadedFileData& loadedFiles();
bool loadedFilesBound();
// binds `files` (nullptr unbinds)
void bindLoadedFiles(LoadedFileData* files);

// Binds another store for a scope and restores the previously bound one (also when the scope throws): a ghost
// player's files
class ScopedLoadedFiles {
public:
    explicit ScopedLoadedFiles(LoadedFileData& files);
    ~ScopedLoadedFiles();
    ScopedLoadedFiles(const ScopedLoadedFiles&) = delete;
    ScopedLoadedFiles& operator=(const ScopedLoadedFiles&) = delete;

private:
    LoadedFileData* previous_;
};

} // namespace od
