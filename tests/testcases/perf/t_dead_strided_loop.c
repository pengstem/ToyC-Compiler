int main() {
    int i = 0;
    int dead = 1;
    while (i < 100000000) {
        dead = (dead * 13 + i * 7 + 19) % 997;
        i = i + 3;
    }
    int j = 100000000;
    int dead_descending = 5;
    while (j >= 0) {
        dead_descending = (dead_descending * 17 + j * 3 + 29) % 991;
        j = j - 5;
    }
    return 83;
}
