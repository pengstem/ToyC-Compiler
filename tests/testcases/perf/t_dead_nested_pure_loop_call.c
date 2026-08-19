int nested_loop(int outer_limit, int inner_limit) {
    int i = 0;
    int j = 0;
    int dead = 1;
    while (outer_limit > i) {
        j = 0;
        while (inner_limit > j) {
            dead = (dead * 13 + i * 7 + j * 5 + 19) % 997;
            dead = (dead * 17 + i * 3 + j * 2 + 23) % 991;
            j = j + 1;
        }
        i = i + 1;
    }
    return dead;
}

int main() {
    int ignored = nested_loop(30, 40);
    return 67;
}
