int main() {
    int i = -20;
    int sum = 3;
    while (i < 20) {
        sum = sum + (i + 1) / 7;
        i = i + 1;
    }
    return sum % 251;
}
