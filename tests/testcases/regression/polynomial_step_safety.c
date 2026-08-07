int main() {
    int i = 0;
    int sum = 3;
    while (i < 18) {
        sum = sum + i * i;
        i = i + 2;
    }
    return (sum + i) % 251;
}
