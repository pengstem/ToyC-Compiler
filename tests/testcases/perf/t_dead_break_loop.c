int main() {
    int answer = 67;
    int i = 0;
    int dead = 1;
    while (i < 100000000) {
        if (i >= 50000000) {
            break;
        }
        dead = dead * 3 + i;
        i = i + 1;
    }
    return answer;
}
