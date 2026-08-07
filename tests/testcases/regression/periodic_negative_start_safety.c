int main() {
    int i = -5;
    int state = 7;
    while (i < 11) {
        if (i % 3 == 0) {
            state = state + i;
        } else {
            state = state - i;
        }
        i = i + 1;
    }
    return (state + i) % 251;
}
