int main() {
    int i = 0;
    int a = 3;
    int b = 5;
    int total = 7;
    while (i < 17) {
        int next = a + b;
        a = b + i;
        b = next - a;
        total = total + a - b;
        i = i + 1;
    }
    return (a + b + total) % 251;
}
