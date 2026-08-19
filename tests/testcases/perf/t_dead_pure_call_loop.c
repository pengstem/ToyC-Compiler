int heavy(int value) {
    int result = value + 1;
    result = (result * 3 + 1) % 997;
    result = (result * 5 + 2) % 997;
    result = (result * 7 + 3) % 997;
    result = (result * 11 + 4) % 997;
    result = (result * 13 + 5) % 997;
    result = (result * 17 + 6) % 997;
    result = (result * 19 + 7) % 997;
    result = (result * 23 + 8) % 997;
    result = (result * 29 + 9) % 997;
    result = (result * 31 + 10) % 997;
    result = (result * 37 + 11) % 997;
    result = (result * 41 + 12) % 997;
    result = (result * 43 + 13) % 997;
    result = (result * 47 + 14) % 997;
    result = (result * 53 + 15) % 997;
    result = (result * 59 + 16) % 997;
    result = (result * 61 + 17) % 997;
    result = (result * 67 + 18) % 997;
    result = (result * 71 + 19) % 997;
    result = (result * 73 + 20) % 997;
    result = (result * 79 + 21) % 997;
    result = (result * 83 + 22) % 997;
    result = (result * 89 + 23) % 997;
    result = (result * 97 + 24) % 997;
    result = (result * 3 + 25) % 997;
    result = (result * 5 + 26) % 997;
    result = (result * 7 + 27) % 997;
    result = (result * 11 + 28) % 997;
    return result;
}

int main() {
    int i = 0;
    int dead = 0;
    int live = 3;
    while (i < 1000) {
        dead = heavy(i);
        live = live + i;
        i = i + 1;
    }
    return live;
}
