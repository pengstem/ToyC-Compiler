// Exercise the zero-, one-, two- and odd-trip paths of guarded pair unrolling.
// The recursive branch prevents call-site constant folding of sum_range.
int sum_range(int current, int bound) {
    if (current < -1000) {
        return sum_range(current + 1, bound);
    }

    int sum = 0;
    while (current < bound) {
        sum = sum + current;
        current = current + 1;
    }
    return sum;
}

int main() {
    int result = sum_range(5, 5);
    result = result + sum_range(5, 6);
    result = result + sum_range(5, 7);
    result = result + sum_range(5, 8);
    result = result + sum_range(-3, 2);
    result = result + sum_range(0, -2147483647 - 1);
    return result;
}
