int main() {
    int i = 2147483645;
    int state = 7;
    while (i < 2147483647) {
        if (i % 64 == 0) {
            state = state + 3;
        } else {
            state = state - 2;
        }
        i = i + 1;
    }
    return (state + i) % 251;
}
