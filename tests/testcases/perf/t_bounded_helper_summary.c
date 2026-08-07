int bounded_bucket(int value) {
    int bound = value % 10;
    int index = 0;
    int total = 3;
    while (index < bound) {
        total = total + index * 2 + 1;
        index = index + 1;
    }
    return total;
}

int main() {
    int index = 0;
    int sum = 0;
    while (index < 60000000) {
        sum = sum + bounded_bucket(index);
        index = index + 1;
    }
    return sum % 251;
}
