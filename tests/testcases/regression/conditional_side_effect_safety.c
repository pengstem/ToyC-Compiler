int state = 1;

int touch(int value) {
    state = state * 3 + value;
    return state;
}

int main() {
    int i = 0;
    int ignored = 0;
    while (i < 10) {
        if (i % 2 == 0) {
            ignored = touch(i);
        } else {
            state = state + 1;
        }
        i = i + 1;
    }
    return state;
}
