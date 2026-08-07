int main() {
    int index = 0;
    int state = 7;
    int sum = 0;
    while (index < 1000) {
        state = (state * 17 + 23) % 1009;
        sum = sum + state;
        index = index + 1;
    }
    return (sum + state) % 251;
}
