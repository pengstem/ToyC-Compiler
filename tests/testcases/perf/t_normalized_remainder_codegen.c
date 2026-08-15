int normalize_unknown(int value) {
    return ((value % 1000) + 1000) % 1000;
}

int main() {
    int state = 21222;
    int index = 0;
    int sum = 0;
    while (index < 100000) {
        state = ((1103515245 * state + 12345) % 1073741824 + 1073741824) % 1073741824;
        int residue = ((state % 1000) + 1000) % 1000;
        sum = sum + residue;
        index = index + 1;
    }
    return (sum + normalize_unknown(0 - state)) % 251;
}
