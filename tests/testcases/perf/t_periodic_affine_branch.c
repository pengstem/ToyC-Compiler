int main() {
    int i = 0;
    int first = 7;
    int second = -3;
    int total = 11;
    while (i < 100000000) {
        if (i % 3 == 0) {
            first = first + second + i;
            total = total + first;
        } else {
            second = second - first + i * 2;
            total = total - second;
        }
        i = i + 1;
    }
    return (first + second + total) % 251;
}
