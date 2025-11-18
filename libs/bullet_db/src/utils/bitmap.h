#ifndef BITMAP_H
#define BITMAP_H

#include <array>
#include <cstddef>
#include <cstdint>

class Bitmap {
public:
    static constexpr size_t BITMAP_SIZE = 32 * 8; // 32 bytes = 256 bits

    Bitmap();

    // Check if a bit is set
    bool is_set(size_t bit) const;

    // Set a bit to 1
    void set(size_t bit);

    // Clear a bit to 0
    void clear(size_t bit);

    size_t count();

    // Optional: toggle a bit
    void toggle(size_t bit);

private:
    std::array<uint8_t, 32> data;

    void check_index(size_t bit) const;
};

#endif // BITMAP_H

