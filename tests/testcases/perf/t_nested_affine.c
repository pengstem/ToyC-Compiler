int main() {
    int outer = 0;
    int inner = 0;
    int sum = 11;
    int value = 2;
    while (outer < 21) {
        inner = 0;
        while (inner < 13) {
            sum = sum + outer * 2 + inner * 3 + value;
            value = value + outer + 1;
            inner = inner + 1;
        }
        outer = outer + 1;
    }
    return (sum + value) % 251;
}
