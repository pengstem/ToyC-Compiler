int main() {
    int i = 0;
    int live = 1;
    while (i < 50) {
        live = (live * 13 + i * 7 + 19) % 997;
        i = i + 1;
    }
    return live;
}
