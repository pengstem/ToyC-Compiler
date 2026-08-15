int main() {
    int selector = 0;
    int column = 0;
    int inner = 0;
    int sum = 0;
    while (selector < 8) {
        column = 0;
        while (column < 400) {
            int product = 0;
            inner = 0;
            while (inner < 20) {
                product = product + (selector + inner) * (column + inner);
                inner = inner + 1;
            }
            if (selector == column) {
                sum = sum + product;
            }
            column = column + 1;
        }
        selector = selector + 1;
    }
    return sum;
}
