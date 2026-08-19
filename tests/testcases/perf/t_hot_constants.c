int main() {
    int index = 0;
    int state = 17;
    int sum = 0;
    while (index < 10000) {
        state = (state * 1103515245 + 12345) % 1000000007;
        sum = sum + state % 97;
        index = index + 1;
    }
    return sum;
}
