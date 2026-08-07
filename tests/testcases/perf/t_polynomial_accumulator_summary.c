int polynomial_sum(int seed) {
    int i = -7;
    int sum = seed;
    int other = seed - 3;
    while (i <= 102) {
        sum = sum + i * i * i * 2 - i * i * 3 + i * 5 + 11;
        other = other - i * i + i * 7 - 4;
        i = i + 1;
    }
    return sum + other + i;
}

int main() {
    return polynomial_sum(17) % 251;
}
