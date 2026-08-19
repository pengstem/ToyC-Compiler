int main() {
    int i = 0;
    int state = 7;
    while (i < 19) {
        if (state % 3 == 0) {
            state = state + i + 1;
        } else {
            state = state * 2 - i;
        }
        i = i + 1;
    }
    return (state + i) % 251;
}
