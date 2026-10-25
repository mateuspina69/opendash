// A block whose own bytes each of its owners lays out its own way (the script block's bytes for the running script,
// the stage block for the stage): the parts of the block an owner uses, each a struct of named members (deriving
// from the block's part base), made with every member zero the first time the owner asks for it and kept until the
// owner ends. The read map proves no owner reads a byte of the block it did not write
// since it started: a part starts at zero, as the block's bytes the owner's start clears. Two parts of one owner
// share no byte (the test translator, which lays each part out at its offsets in the exe's block, checks it).
#pragma once

#include <memory>
#include <type_traits>
#include <typeinfo>
#include <vector>

namespace od::port {

// What the test translator (tests/translator) is told of a block's parts (nothing in the game observes them): a part
// made, a part dropped (before it is destroyed), and bytes of the block cleared (`which`: the block's own enum of its
// clears; the parts' members there are cleared by the block)
template <class Base> class BlockPartsObserver {
public:
    virtual ~BlockPartsObserver() = default;
    virtual void created(Base& part) = 0;
    virtual void dropped(Base& part) = 0;
    virtual void bytesCleared(unsigned which) = 0;
};

// The parts of a block the current owner uses (Base: the block's part base, polymorphic)
template <class Base> class BlockParts {
public:
    BlockParts() = default;
    BlockParts(const BlockParts&) = delete;
    BlockParts& operator=(const BlockParts&) = delete;

    // the part T (made zero on the owner's first ask)
    template <class T> T& part() {
        static_assert(std::is_base_of_v<Base, T>, "BlockParts: a part of the block");
        for (auto& p : parts_)
            if (T* t = dynamic_cast<T*>(p.get()); t && typeid(*p) == typeid(T)) return *t;
        parts_.push_back(std::make_unique<T>());
        T& t = static_cast<T&>(*parts_.back());
        if (observer_) observer_->created(t);
        return t;
    }
    // the part T when the owner made it (nullptr: not yet): a reader that must not make one (a draw)
    template <class T> T* held() {
        for (auto& p : parts_)
            if (typeid(*p) == typeid(T)) return static_cast<T*>(p.get());
        return nullptr;
    }
    // the owner's parts dropped and the block's bytes `which` cleared (the owner ended, or a new one starts): the
    // parts leave their members in the bytes before the clear zeroes them
    void dropAll(unsigned which) {
        if (observer_) {
            for (auto& p : parts_) observer_->dropped(*p);
            observer_->bytesCleared(which);
        }
        parts_.clear();
    }
    // the owner's parts dropped, the block's bytes kept (a new owner starts; the exe leaves the bytes, no owner reads
    // them)
    void drop() {
        if (observer_)
            for (auto& p : parts_) observer_->dropped(*p);
        parts_.clear();
    }
    // the block's bytes `which` cleared (the parts' members there: the caller clears them)
    void bytesCleared(unsigned which) {
        if (observer_) observer_->bytesCleared(which);
    }
    // each part held
    template <class F> void forEach(F&& f) {
        for (auto& p : parts_) f(*p);
    }

    static void setObserver(BlockPartsObserver<Base>* o) { observer_ = o; }

private:
    std::vector<std::unique_ptr<Base>> parts_;
    static inline BlockPartsObserver<Base>* observer_ = nullptr;
};

} // namespace od::port
