int main() {
    int i = 0;
    int dead = 1;
    int live = 3;
    while (i < 1000) {
        if (dead % 3 == 0) {
            dead = (dead * 13 + i * 7 + 19) % 997;
        } else {
            dead = (dead * 17 - i * 5 + 23) % 991;
        }
        if (dead < 0) {
            dead = -dead;
        }
        live = live + i;
        i = i + 1;
    }
    return live;
}
