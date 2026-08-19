int main() {
    int i = 0;
    int dead = 1;
    while (i < 100) {
        if (i == 0) {
            continue;
        }
        dead = dead * 3 + i;
        i = i + 1;
    }
    return 97;
}
