int main() {
    int index = 0;
    int state = 3;
    int sum = 0;
    while (index < 10000) {
        state = (state * 7 + index % 13 + 1) % 997;
        sum = sum + state % 17;
        index = index + 1;
    }
    return (sum + state) % 251;
}
