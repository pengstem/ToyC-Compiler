// Stresses a leaf function larger than the normal inline budget. The hot paths
// return after one or two comparisons, so loop-call overhead dominates.
int classify(int x) {
    if (x < 1) {
        return x + 1;
    }
    if (x < 2) {
        return x + 2;
    }
    if (x < 3) {
        return x + 3;
    }
    if (x < 4) {
        return x + 4;
    }
    if (x < 5) {
        return x + 5;
    }
    if (x < 6) {
        return x + 6;
    }
    if (x < 7) {
        return x + 7;
    }
    if (x < 8) {
        return x + 8;
    }
    if (x < 9) {
        return x + 9;
    }
    if (x < 10) {
        return x + 10;
    }
    if (x < 11) {
        return x + 11;
    }
    if (x < 12) {
        return x + 12;
    }
    return x;
}

int main() {
    int i = 0;
    int x = 0;
    int sum = 0;
    while (i < 5000000) {
        sum = sum + classify(x);
        x = 1 - x;
        i = i + 1;
    }
    return sum;
}
