int main() {
    int index = -5;
    int sum = 0;
    while (index < 10000) {
        sum = sum + index % 17;
        index = index + 1;
    }
    return sum % 251;
}
