int moving_bound_bucket(int value) {
    int bound = value % 10;
    int index = 0;
    int total = 0;
    while (index < bound) {
        total = total + index;
        bound = bound - 1;
        index = index + 1;
    }
    return total;
}

int main() {
    int index = 0;
    int sum = 0;
    while (index < 40) {
        sum = sum + moving_bound_bucket(index);
        index = index + 1;
    }
    return sum % 251;
}
