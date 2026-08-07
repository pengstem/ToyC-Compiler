int main() {
    int i = 0;
    int sum = 3;
    while (i < 17) {
        sum = sum + i * i * i * i;
        i = i + 1;
    }
    return (sum + i) % 251;
}
