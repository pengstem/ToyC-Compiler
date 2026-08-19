int periodic_value(int left, int right) {
    int difference = left - right;
    return difference * difference + left + 17;
}

int main() {
    int index = 0;
    int result = 0;
    while (index < 100000) {
        int left = (index + 69) % 128;
        int right = (index + 99) % 128;
        int delta = periodic_value(left, right) % 1000003;
        result = ((result + delta) % 1000003 - index % 1000003) % 1000003;
        index = index + 1;
    }
    return result;
}
