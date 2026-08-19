int bias = 17;

int countdown(int n, int acc) {
    if (n <= 0) {
        return acc + bias;
    }
    return countdown(n - 1, (acc + n) % 251);
}

int main() {
    int index = 0;
    int result = 0;
    while (index < 1000) {
        result = result + countdown(200, 0);
        bias = bias + 1;
        index = index + 1;
    }
    return result;
}
