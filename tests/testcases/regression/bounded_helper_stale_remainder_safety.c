int initial_value(int depth) {
    if (depth == 0) {
        return -3;
    }
    return initial_value(depth - 1);
}

int main() {
    int outer = initial_value(1);
    outer = outer + 1;
    int bound = outer % 10;
    outer = 0;
    int sum = 0;
    while (outer < 20) {
        int inner = 0;
        while (inner < bound) {
            sum = sum + inner;
            inner = inner + 1;
        }
        outer = outer + 1;
    }
    return sum;
}
