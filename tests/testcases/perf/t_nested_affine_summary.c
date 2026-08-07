int main() {
    int i = 0;
    int j = 0;
    int sum = 7;
    int other = -3;
    while (i < 17) {
        j = 5;
        while (j < 3) {
            sum = sum + 99;
            j = j + 1;
        }
        j = -2;
        while (j <= 5) {
            sum = sum + i * 2 + j + 3;
            other = other - i + j;
            j = j + 1;
        }
        i = i + 1;
    }
    return sum + other + j;
}
