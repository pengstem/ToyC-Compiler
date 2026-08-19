int state = 1;

int impure(int value) {
    if (value < 0) {
        return impure(-value);
    }
    state = state * 3 + value;
    return state;
}

int main() {
    int i = 0;
    int ignored = 0;
    while (i < 10) {
        ignored = impure(i);
        i = i + 1;
    }
    return state;
}
