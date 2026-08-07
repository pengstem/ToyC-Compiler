int observable = 3;

int main() {
    int i = 0;
    int state = 7;
    while (i < 17) {
        if (i % 4 == 1) {
            observable = observable + state;
        } else {
            state = state + observable + i;
        }
        i = i + 1;
    }
    return (state + observable + i) % 251;
}
