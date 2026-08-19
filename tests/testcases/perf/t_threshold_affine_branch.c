int threshold_sum(int seed) {
    int index = 0;
    int first = seed;
    int second = seed + 2;
    while (index < 100000000) {
        if (index * 2 + 3 <= 80000001) {
            first = first + 1;
        } else {
            second = second - 1;
        }
        index = index + 1;
    }
    return first + second + index;
}

int main() {
    return threshold_sum(17) % 251;
}
