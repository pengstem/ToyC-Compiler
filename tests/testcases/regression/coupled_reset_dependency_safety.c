int main() {
    int i = 0;
    int reset = 3;
    int total = 7;
    int other = 11;
    while (i < 1000) {
        total = total + reset;
        reset = 5;
        other = other + total;
        i = i + 1;
    }
    return (reset + total + other + i) % 251;
}
