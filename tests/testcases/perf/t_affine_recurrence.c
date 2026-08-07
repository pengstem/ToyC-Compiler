int main() {
    int i = 0;
    int a = 3;
    int b = 5;
    int sum = 7;
    while (i < 25) {
        int next = a * 2 + b * 3 + 1;
        a = b;
        b = next;
        sum = sum + a - b;
        i = i + 1;
    }
    return (a + b + sum) % 251;
}
