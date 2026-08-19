int main() {
    int index = 0;
    int first = 7;
    int second = 11;
    int sum = 0;
    while (index < 100000) {
        first = (first * 17 + second) % 1009;
        second = (second * 13 + first) % 1013;
        sum = sum + first + second;
        index = index + 1;
    }
    return (sum + first + second) % 251;
}
