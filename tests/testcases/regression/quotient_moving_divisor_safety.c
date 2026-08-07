int main() {
    int i = 0;
    int divisor = 2;
    int sum = 3;
    while (i < 1000) {
        sum = sum + i / divisor;
        divisor = divisor + 1;
        i = i + 1;
    }
    return sum % 251;
}
