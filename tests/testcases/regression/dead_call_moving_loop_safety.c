int drift(int limit) {
    int i = 0;
    int dead = 1;
    while (i < limit) {
        dead = (dead * 13 + i * 7 + 19) % 997;
        dead = (dead * 17 + i * 5 + 23) % 991;
        dead = (dead * 19 + i * 3 + 29) % 983;
        dead = (dead * 23 + i * 2 + 31) % 977;
        limit = limit - 1;
        i = i + 1;
    }
    return dead;
}

int main() {
    int ignored = drift(1000);
    return 61;
}
