int main() {
    int index = 0;
    int state = 7;
    int sum = 1;
    while (index < 100000) {
        state = (state * 17 + 23) % 1009;
        sum = sum * 2 + state;
        index = index + 1;
    }
    return (sum + state) % 251;
}
