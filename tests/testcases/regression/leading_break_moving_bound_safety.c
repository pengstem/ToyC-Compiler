int main() {
    int i = 0;
    int limit = 3;
    int a = 5;
    int b = 7;
    while (i < 20) {
        if (i >= limit) {
            break;
        }
        int next = a + b + i;
        a = b;
        b = next;
        limit = limit + 1;
        i = i + 1;
    }
    return (a + b + i + limit) % 251;
}
