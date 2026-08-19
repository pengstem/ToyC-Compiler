int main() {
    int answer = 91;
    int i = 100000000;
    int dead = 1;
    while (i > 0) {
        dead = (dead * 13 + i * 7 + 19) % 997;
        if (dead < 0) {
            dead = -dead;
        }
        i = i - 1;
    }
    return answer;
}
