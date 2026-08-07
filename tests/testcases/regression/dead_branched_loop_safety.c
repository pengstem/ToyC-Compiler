int main() {
    int i = 0;
    int live = 0;
    while (i < 12) {
        if (i < 6 && live < 1000) {
            live = live + i * 3;
        } else {
            live = live - i;
        }
        i = i + 1;
    }

    int j = 0;
    while (j < 10) {
        j = j + 1;
        if (j % 2 == 0) {
            continue;
        }
        live = live + j;
    }
    return live;
}
