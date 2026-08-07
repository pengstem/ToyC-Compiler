int main() {
    int index = 0;
    int state = 3;
    while (index < 10000) {
        if (index < 5000) {
            state = 11;
        } else {
            state = state + index;
        }
        index = index + 1;
    }
    return (state + index) % 251;
}
