int main() {
    int i = 0;
    int sum = 0;
    while (i < 100) {
        i = i + 1;
        if (i % 2 == 0) {
            continue;
        }
        sum = sum + i;
    }
    return sum % 251;
}
