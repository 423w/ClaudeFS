// fss/synth/held_notes.h: the keys the player is physically holding, in the order they were pressed (L2.5).
//
// Mono and legato modes need it to answer "which note should sound now?": when you hold C, press E, then let
// go of E, a mono synth should fall back to C (it's still held). That needs a memory of every held key, not
// just the last one. Note priority picks from it: the LAST pressed (most synths), the LOWEST (Minimoog-style
// bass lines: holding a low pedal note while trilling above it), or the HIGHEST (lead lines).
//
// Fixed capacity, no allocation: it lives on the audio thread. With more than kCapacity keys down (ten fingers
// plus a forearm), the oldest press is forgotten, which in practice never matters.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fss::synth {

enum class NotePriority : std::uint8_t { Last, Low, High };

class HeldNotes {
public:
    static constexpr std::size_t kCapacity = 16;

    struct Note {
        std::uint8_t key = 0;
        float velocity = 0.0f;
    };

    /// A key went down. Pressing a key that's already listed moves it to the top (it's now the latest).
    void press(std::uint8_t key, float velocity) noexcept
    {
        remove(key);
        if (size_ == kCapacity)
            eraseAt(0); // forget the oldest
        notes_[size_++] = {key, velocity};
    }

    /// A key came up. Returns false if it wasn't held (e.g. a note-off for a key pressed before a panic).
    bool release(std::uint8_t key) noexcept { return remove(key); }

    void clear() noexcept { size_ = 0; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] bool contains(std::uint8_t key) const noexcept
    {
        for (std::size_t i = 0; i < size_; ++i)
            if (notes_[i].key == key)
                return true;
        return false;
    }

    /// The note that should sound under `priority`. Precondition: !empty().
    [[nodiscard]] Note pick(NotePriority priority) const noexcept
    {
        Note best = notes_[size_ - 1]; // Last: the top of the stack
        if (priority == NotePriority::Last)
            return best;
        for (std::size_t i = 0; i < size_; ++i) {
            const Note& n = notes_[i];
            if ((priority == NotePriority::Low && n.key < best.key) || (priority == NotePriority::High && n.key > best.key))
                best = n;
        }
        return best;
    }

private:
    bool remove(std::uint8_t key) noexcept
    {
        for (std::size_t i = 0; i < size_; ++i)
            if (notes_[i].key == key) {
                eraseAt(i);
                return true;
            }
        return false;
    }

    void eraseAt(std::size_t i) noexcept
    {
        for (; i + 1 < size_; ++i) // keep the press order: shift the newer entries down by one
            notes_[i] = notes_[i + 1];
        --size_;
    }

    std::array<Note, kCapacity> notes_{};
    std::size_t size_ = 0;
};

} // namespace fss::synth
