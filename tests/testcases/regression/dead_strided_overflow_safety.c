int main() {
    int i = 2147483646;
    int dead = 0;
    while (i < 2147483647) {
        dead = dead + i;
        i = i + 2;
    }
    return 89;
}
