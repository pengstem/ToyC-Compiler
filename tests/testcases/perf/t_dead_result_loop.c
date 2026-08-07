int main() {
    int answer = 73;
    int i = 0;
    int dead = 1;
    while (i < 1000) {
        if (i % 3 == 0) {
            dead = dead * 3 + i;
        } else {
            dead = dead - i * 7;
        }
        if (dead < 0) {
            dead = -dead;
        }
        i = i + 1;
    }
    return answer;
}
