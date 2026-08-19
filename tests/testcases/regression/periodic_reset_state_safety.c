int main() {
    int i = 0;
    int state = 7;
    while (i < 17) {
        if (i % 4 == 1) {
            state = 5;
        } else {
            state = state + i;
        }
        i = i + 1;
    }
    return (state + i) % 251;
}
