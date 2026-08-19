int main() {
    int i = 0;
    int bound = 10;
    int sum = 0;
    while (i < bound) {
        sum = sum + i;
        bound = bound - 1;
        i = i + 1;
    }
    return sum + bound + i;
}
