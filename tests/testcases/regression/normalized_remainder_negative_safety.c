int normalize(int value) {
    return ((value % 1000) + 1000) % 1000;
}

int main() {
    int state = -1234567;
    int index = 0;
    int sum = 0;
    while (index < 1000) {
        sum = sum + normalize(state);
        state = state + 97;
        index = index + 1;
    }
    return sum % 251;
}
