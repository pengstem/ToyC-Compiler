int main() {
    int i = 0;
    int sum = 0;
    int overwritten = 0;
    while (i < 1000) {
        overwritten = (i * 12345 + 6789) % 997;
        overwritten = 5;
        sum = sum + i % 7;
        i = i + 1;
    }
    return (sum + overwritten) % 251;
}
