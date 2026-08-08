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
#include <cstddef>
#include <cstdint>
#include <iostream>

constexpr int BOARD_SIZE = 20;
constexpr int BOARD_PADDED = BOARD_SIZE + 2;
constexpr int BOARD_SIZE_SQUARED = BOARD_SIZE * BOARD_SIZE;
constexpr int BOARD_CELLS = BOARD_PADDED * BOARD_PADDED;
constexpr int WIN_CONDITION = 10;

// The board is stored flat, so a move is a single index and the eight
// neighbour writes in check_win are constant strides from it.
constexpr int STEP_S = BOARD_PADDED;
constexpr int STEP_E = 1;
constexpr int STEP_SE = BOARD_PADDED + 1;
constexpr int STEP_NE = 1 - BOARD_PADDED;

struct Cell
{
    uint8_t s, n, e, w, se, nw, ne, sw;
};

class Board
{
    Cell b[BOARD_CELLS] = {};

public:
    bool check_win(int i);
};

bool Board::check_win(int i)
{
    Cell *p = b + i;
    // Held as ptrdiff_t so the offsets below need no sign extension.
    const ptrdiff_t s = p->s, n = p->n, e = p->e, w = p->w;
    const ptrdiff_t se = p->se, nw = p->nw, ne = p->ne, sw = p->sw;

    const uint8_t col = s + 1 + n;
    const uint8_t row = w + 1 + e;
    const uint8_t diag = nw + 1 + se;
    const uint8_t anti = ne + 1 + sw;

    if (col >= WIN_CONDITION || row >= WIN_CONDITION || diag >= WIN_CONDITION || anti >= WIN_CONDITION)
    {
        return true;
    }

    p[(s + 1) * STEP_S].n = col;
    p[-(n + 1) * STEP_S].s = col;
    p[(e + 1) * STEP_E].w = row;
    p[-(w + 1) * STEP_E].e = row;
    p[(se + 1) * STEP_SE].nw = diag;
    p[-(nw + 1) * STEP_SE].se = diag;
    p[(ne + 1) * STEP_NE].sw = anti;
    p[-(sw + 1) * STEP_NE].ne = anti;

    return false;
}

enum class Result
{
    Circle,
    Cross,
    Draw
};

// Xorshift
uint32_t xorshift_step(uint32_t &state)
{
    uint64_t x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return state = uint32_t(x);
}

// The xorshift step is GF(2)-linear, so advancing the stream by a whole game's
// worth of draws is a matrix-vector product. Tabulating that product per input
// byte lets us start several games from known stream offsets and interleave
// their shuffles, which hides the latency of the serial xorshift chain while
// producing exactly the same sequence as drawing the games one after another.
constexpr int LANES = 4;

class Jump
{
    uint32_t t[4][256];

public:
    Jump()
    {
        uint32_t basis[32];
        for (int j = 0; j < 32; j++)
        {
            uint32_t s = 1u << j;
            for (int k = 0; k < BOARD_SIZE_SQUARED; k++)
                xorshift_step(s);
            basis[j] = s;
        }
        for (int b = 0; b < 4; b++)
            for (int v = 0; v < 256; v++)
            {
                uint32_t acc = 0;
                for (int k = 0; k < 8; k++)
                    if (v >> k & 1)
                        acc ^= basis[8 * b + k];
                t[b][v] = acc;
            }
    }

    uint32_t operator()(uint32_t x) const
    {
        return t[0][x & 255] ^ t[1][x >> 8 & 255] ^ t[2][x >> 16 & 255] ^ t[3][x >> 24];
    }
};

Result play(const uint16_t *order)
{
    Board circle, cross;

    for (int i = 0; i < BOARD_SIZE_SQUARED; i += 2)
    {
        if (circle.check_win(order[i]))
            return Result::Circle;
        if (cross.check_win(order[i + 1]))
            return Result::Cross;
    }
    return Result::Draw;
}

int main()
{
    constexpr int n = 10000;
    static_assert(n % LANES == 0, "game count must divide into lanes");

    const Jump jump;
    uint32_t state = 1729163U;
    int o = 0, x = 0, draw = 0;

    auto start = std::chrono::high_resolution_clock::now();

    for (int g = 0; g < n; g += LANES)
    {
        uint32_t st[LANES];
        st[0] = state;
        for (int k = 1; k < LANES; k++)
            st[k] = jump(st[k - 1]);

        uint16_t order[LANES][BOARD_SIZE_SQUARED];

        for (int y = 1, i = 0; y <= BOARD_SIZE; y++)
            for (int c = 1; c <= BOARD_SIZE; c++, i++)
            {
                const uint16_t v = uint16_t(y * BOARD_PADDED + c);
                for (int k = 0; k < LANES; k++)
                {
                    int j = int(uint64_t(xorshift_step(st[k])) * uint32_t(i + 1) >> 32);
                    order[k][i] = order[k][j];
                    order[k][j] = v;
                }
            }

        state = st[LANES - 1];

        for (int k = 0; k < LANES; k++)
        {
            switch (play(order[k]))
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
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "O/X/Draw: " << o << "/" << x << "/" << draw << std::endl;
    std::cout << "Time taken: " << duration.count() << " ms" << std::endl;
    return 0;
}
