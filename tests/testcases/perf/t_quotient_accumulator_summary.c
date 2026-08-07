int bucket(int value) {
    return (value + 3) / 7;
}

int main() {
    int i = 0;
    int sum = 5;
    while (i < 100000) {
        sum = sum + bucket(i) * 3 + 2;
        i = i + 1;
    }
    return sum % 251;
}
