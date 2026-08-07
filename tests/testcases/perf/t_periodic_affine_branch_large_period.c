int main() {
    int index = 0;
    int sum = 0;
    while (index < 100000000) {
        if (index % 100 < 50) {
            sum = sum + 1;
        } else {
            sum = sum + 2;
        }
        index = index + 1;
    }
    return sum % 251;
}
