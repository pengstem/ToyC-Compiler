int main() {
    int index = 0;
    int state = 3;
    int total = 0;
    while (index < 10000) {
        if (state < 500) {
            state = state * 3 + 1;
        } else {
            state = state - 499;
        }
        total = total + state;
        index = index + 1;
    }
    return (total + state) % 251;
}
