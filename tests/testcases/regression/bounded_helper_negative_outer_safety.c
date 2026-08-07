int signed_bucket(int value) {
    int bound = value % 10;
    int index = 0;
    int total = 0;
    while (index < bound) {
        total = total + index;
        index = index + 1;
    }
    return total;
}

int main() {
    int index = -20;
    int sum = 0;
    while (index < 20) {
        sum = sum + signed_bucket(index);
        index = index + 1;
    }
    return sum % 251;
}
