int spin(int value) {
    while (1) {
        value = value + 1;
        value = value - 2;
        value = value + 3;
        value = value - 4;
        value = value + 5;
        value = value - 6;
        value = value + 7;
        value = value - 8;
        value = value + 9;
        value = value - 10;
        value = value + 11;
        value = value - 12;
        value = value + 13;
        value = value - 14;
        value = value + 15;
        value = value - 16;
        value = value + 17;
        value = value - 18;
        value = value + 19;
        value = value - 20;
    }
    return value;
}

int main() {
    spin(1);
    return 7;
}
