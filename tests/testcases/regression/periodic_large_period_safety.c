int main() {
    int index = 0;
    int sum = 0;
    while (index < 10000) {
        sum = sum + index % 4099;
        index = index + 1;
    }
    return sum % 251;
}
