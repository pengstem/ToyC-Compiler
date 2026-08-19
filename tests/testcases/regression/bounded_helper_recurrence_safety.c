int recurrent_bucket(int value) {
    int bound = value % 10;
    int index = 0;
    int total = 1;
    while (index < bound) {
        total = total * 2 + index;
        index = index + 1;
    }
    return total;
}

int main() {
    int index = 0;
    int sum = 0;
    while (index < 40) {
        sum = sum + recurrent_bucket(index);
        index = index + 1;
    }
    return sum % 251;
}
