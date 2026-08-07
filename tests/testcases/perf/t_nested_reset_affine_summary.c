int main() {
    int i = 0;
    int total = 7;
    int a = 0;
    int b = 0;
    int j = 0;
    while (i < 100000000) {
        a = 3;
        b = 5;
        j = 0;
        while (j < 5) {
            int next = a + b + i;
            a = b;
            b = next;
            j = j + 1;
        }
        total = total + a - b;
        i = i + 1;
    }
    return (total + a + b + j) % 251;
}
