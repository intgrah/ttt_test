/* ttt_test - Speed test for random games of tic tac toe
 * Released into the public domain by Brian Chen (differental) and Jeremy Chen (intgrah), 2025.
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute
 * this software, either in source code form or as a compiled binary, for any
 * purpose, commercial or non-commercial, and by any means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors of
 * this software dedicate any and all copyright interest in the software to
 * the public domain. We make this dedication for the benefit of the public at
 * large and to the detriment of our heirs and successors. We intend this
 * dedication to be an overt act of relinquishment in perpetuity of all
 * present and future rights to this software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <https://unlicense.org/>
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>

constexpr int BOARD_SIZE = 20;
constexpr int BOARD_PADDED = BOARD_SIZE + 2;
constexpr int BOARD_CELLS = BOARD_PADDED * BOARD_PADDED;
constexpr int BOARD_SIZE_SQUARED = BOARD_SIZE * BOARD_SIZE;
constexpr int WIN_CONDITION = 10;

// Neighbour deltas on the flattened (y * BOARD_PADDED + x) board.
constexpr int D_S = BOARD_PADDED;
constexpr int D_E = 1;
constexpr int D_SE = BOARD_PADDED + 1;
constexpr int D_SW = BOARD_PADDED - 1;

struct Cell
{
    uint8_t s, n, e, w, se, nw, ne, sw;
};

static_assert(sizeof(Cell) == 8, "Cell must pack into a single 64-bit word");

// The same deltas measured in bytes, for direct addressing into the board.
constexpr intptr_t BYTES_S = D_S * intptr_t(sizeof(Cell));
constexpr intptr_t BYTES_E = D_E * intptr_t(sizeof(Cell));
constexpr intptr_t BYTES_SE = D_SE * intptr_t(sizeof(Cell));
constexpr intptr_t BYTES_SW = D_SW * intptr_t(sizeof(Cell));

// The four run lengths live in the even bytes of the SWAR word below, so a
// win is "some 16-bit lane reached WIN_CONDITION". Bias each lane by
// 0x80 - WIN_CONDITION and test bit 7: run lengths never exceed BOARD_SIZE,
// so the biased value cannot overflow its byte.
constexpr uint64_t LANE_LO = 0x00FF00FF00FF00FFULL;
constexpr uint64_t LANE_ONE = 0x0001000100010001ULL;
constexpr uint64_t LANE_BIAS = (0x80 - WIN_CONDITION) * LANE_ONE;
constexpr uint64_t LANE_TEST = 0x0080008000800080ULL;

class Board
{
    Cell b[BOARD_CELLS];

public:
    void clear() { std::memset(b, 0, sizeof(b)); }
    bool check_win(int p);
};

inline bool Board::check_win(int p)
{
    Cell &q = b[p];

    // One 64-bit read feeds the SWAR win test; the individual runs are read
    // back as single byte loads (same cache line) to skip 8 shift+mask pairs.
    uint64_t v;
    std::memcpy(&v, &q, sizeof(v));

    // lanes: (s,n) (e,w) (se,nw) (ne,sw) -> col, row, diag, anti
    uint64_t sums = (v & LANE_LO) + ((v >> 8) & LANE_LO) + LANE_ONE;

    if ((sums + LANE_BIAS) & LANE_TEST)
    {
        return true;
    }

    uint8_t col = uint8_t(sums);
    uint8_t row = uint8_t(sums >> 16);
    uint8_t diag = uint8_t(sums >> 32);
    uint8_t anti = uint8_t(sums >> 48);

    // Byte offsets from the played cell to the two run endpoints, in
    // 64-bit arithmetic so each store folds into one movzx + imul + mov.
    uint8_t *base = (uint8_t *)&q;
    base[-(intptr_t)q.n * BYTES_S - BYTES_S + 0] = col;
    base[+(intptr_t)q.s * BYTES_S + BYTES_S + 1] = col;
    base[-(intptr_t)q.w * BYTES_E - BYTES_E + 2] = row;
    base[+(intptr_t)q.e * BYTES_E + BYTES_E + 3] = row;
    base[-(intptr_t)q.nw * BYTES_SE - BYTES_SE + 4] = diag;
    base[+(intptr_t)q.se * BYTES_SE + BYTES_SE + 5] = diag;
    base[+(intptr_t)q.sw * BYTES_SW + BYTES_SW + 6] = anti;
    base[-(intptr_t)q.ne * BYTES_SW - BYTES_SW + 7] = anti;

    return false;
}

enum class Result
{
    Circle,
    Cross,
    Draw
};

static uint64_t rng_state = 1729163UL;

Result do_game()
{
    // Positions are pre-flattened to y * BOARD_PADDED + x, so the shuffle
    // touches one array instead of two.
    uint16_t free_p[BOARD_SIZE_SQUARED];

    uint64_t rng = rng_state;
    for (int y = 1, i = 0; y <= BOARD_SIZE; y++)
        for (int x = 1, p = y * BOARD_PADDED + 1; x <= BOARD_SIZE; x++, i++, p++)
        {
            // Xorshift
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            rng &= 0xffffffffU;
            int j = int(rng * uint64_t(i + 1) >> 32);

            free_p[i] = free_p[j];
            free_p[j] = uint16_t(p);
        }
    rng_state = rng;

    static Board circle, cross;
    circle.clear();
    cross.clear();

    for (int i = 0; i < BOARD_SIZE_SQUARED; i += 2)
    {
        if (circle.check_win(free_p[i]))
            return Result::Circle;
        if (cross.check_win(free_p[i + 1]))
            return Result::Cross;
    }
    return Result::Draw;
}

int main()
{
    constexpr int n = 10000;
    int o = 0, x = 0, draw = 0;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < n; i++)
    {
        switch (do_game())
        {
        case Result::Circle:
            o++;
            break;
        case Result::Cross:
            x++;
            break;
        case Result::Draw:
            draw++;
            break;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "O/X/Draw: " << o << "/" << x << "/" << draw << std::endl;
    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    return 0;
}
