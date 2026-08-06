int main() {
    int i = 0;
    int sum = 0;
    while (i < 100) {
        sum = sum + i / 4;
        sum = sum + i % 8;
        if (i / 2 == 10) {
            sum = sum + 1;
        }
        i = i + 1;
    }
    return sum;
}
