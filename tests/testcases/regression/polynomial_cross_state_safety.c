int main() {
    int i = 0;
    int first = 3;
    int second = 5;
    while (i < 17) {
        first = first + second + i * i;
        second = second + first * i;
        i = i + 1;
    }
    return (first + second + i) % 251;
}
