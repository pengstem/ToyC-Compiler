int state_step(int value) {
    return (value * 17 + 23) % 1009;
}

int score_state(int value) {
    return (value % 17) * 2 + 3;
}

int main() {
    int index = 0;
    int state = 7;
    int sum = 11;
    while (index < 60000000) {
        state = state_step(state);
        sum = sum + score_state(state);
        index = index + 1;
    }
    return (sum + state + index) % 251;
}
