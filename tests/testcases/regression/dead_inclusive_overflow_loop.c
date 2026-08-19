int unsafe_inclusive_loop() {
    int limit = -2147483647 - 1;
    int i = -2147483647;
    int dead = 0;
    while (limit <= i) {
        dead = dead + i;
        i = i - 1;
    }
    return 37;
}

int main() {
    return 37;
}
