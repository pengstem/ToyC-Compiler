int main() {
    int i = 0;
    int a = 3;
    int b = 5;
    int total = 7;
    while (i < 100000000) {
        if (i >= 60000000) {
            break;
        }
        int next = a + b + i;
        a = b;
        b = next;
        total = total + a - b;
        i = i + 1;
    }
    return (a + b + total + i) % 251;
}
