int main() {
    int i = 0;
    int sum = 3;
    while (i < 1000) {
        sum = sum + (sum + i) / 7;
        i = i + 1;
    }
    return sum % 251;
}
