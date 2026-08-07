int dead_state = 7;

int main() {
    int i = 0;
    while (i < 100000000) {
        dead_state = (dead_state * 13 + i * 7 + 19) % 997;
        if (dead_state < 0) {
            dead_state = -dead_state;
        }
        i = i + 1;
    }
    return 73;
}
