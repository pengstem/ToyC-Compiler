int target(int p0, int p1, int p2, int p3, int p4,
           int p5, int p6, int p7, int p8, int p9) {
    return p0 + p1 * 2 + p2 * 3 + p3 * 4 + p4 * 5 +
           p5 * 6 + p6 * 7 + p7 * 8 + p8 * 9 + p9 * 10;
}

int wrapper(int p0, int p1, int p2, int p3, int p4,
            int p5, int p6, int p7, int p8, int p9) {
    if (p0 < -1000) {
        return wrapper(p0 + 1, p1, p2, p3, p4,
                       p5, p6, p7, p8, p9);
    }
    p1 = p1 + 3;
    p9 = p9 - 2;
    return target(p7 - 2, p4 + 2, p5 - 3, p9 + 2, p1 - 3,
                  p6 - 3, p2 - 3, p0, p3 - 1, p8 + 2);
}

int main() {
    return wrapper(15, 16, 8, -4, 0, -10, 9, 5, -5, 14);
}
