int main() {
    int i = 0;
    int dead = 1;
    int live = 3;
    while (i < 1000) {
        dead = (dead * 13 + i * 7 + 19) % 997;
        live = live + i;
        i = i + 1;
    }
    return live;
}
