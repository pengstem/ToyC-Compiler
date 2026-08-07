int main() {
    int i = 0;
    int state = 3;
    while (i < 17) {
        state = state * 3 + i * i;
        i = i + 1;
    }
    return (state + i) % 251;
}
