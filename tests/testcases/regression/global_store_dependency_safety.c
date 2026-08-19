int observed_state = 3;

int read_observed_state() {
    return observed_state;
}

int main() {
    int i = 0;
    while (i < 12) {
        observed_state = observed_state * 3 + i * 5 + 7;
        i = i + 1;
    }
    return (read_observed_state() + i) % 251;
}
