int main() {
    int i = 0;
    int sum = 0;
    while (i < 100) {
        if (i >= 50) {
            break;
        }
        sum = sum + i;
        i = i + 1;
    }
    return sum % 251;
}
