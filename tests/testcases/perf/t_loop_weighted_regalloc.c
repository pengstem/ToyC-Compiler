// Cold values stay live across a hot loop. A frequency-only allocator gives
// them the scarce callee-saved registers even though the loop state is much
// more expensive to spill.
int opaque(int value) {
    if (value <= 0) {
        return 1;
    }
    return opaque(value - 1) + value;
}

int consume(int a, int b, int c, int d, int e, int f,
            int g, int h, int i, int j, int k, int l) {
    if (a < 0) {
        return consume(a + 1, b, c, d, e, f, g, h, i, j, k, l);
    }
    return a + b + c + d + e + f + g + h + i + j + k + l;
}

int main() {
    int seed = opaque(5);
    int c0 = seed + 1;
    int c1 = seed + 2;
    int c2 = seed + 3;
    int c3 = seed + 4;
    int c4 = seed + 5;
    int c5 = seed + 6;
    int c6 = seed + 7;
    int c7 = seed + 8;
    int c8 = seed + 9;
    int c9 = seed + 10;
    int c10 = seed + 11;
    int c11 = seed + 12;

    c0 = c0 + seed;
    c1 = c1 + seed;
    c2 = c2 + seed;
    c3 = c3 + seed;
    c4 = c4 + seed;
    c5 = c5 + seed;
    c6 = c6 + seed;
    c7 = c7 + seed;
    c8 = c8 + seed;
    c9 = c9 + seed;
    c10 = c10 + seed;
    c11 = c11 + seed;

    c0 = c0 + seed;
    c1 = c1 + seed;
    c2 = c2 + seed;
    c3 = c3 + seed;
    c4 = c4 + seed;
    c5 = c5 + seed;
    c6 = c6 + seed;
    c7 = c7 + seed;
    c8 = c8 + seed;
    c9 = c9 + seed;
    c10 = c10 + seed;
    c11 = c11 + seed;

    int limit = seed * 6250000;
    int index = 0;
    int sum = 0;
    while (index < limit) {
        sum = sum + index;
        index = index + 1;
    }

    return sum + consume(c0, c1, c2, c3, c4, c5,
                         c6, c7, c8, c9, c10, c11);
}
