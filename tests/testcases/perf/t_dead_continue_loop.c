int main() {
    int answer = 79;
    int i = 0;
    int dead = 1;
    while (i < 100000000) {
        i = i + 1;
        if (i % 2 == 0) {
            continue;
        }
        dead = dead * 3 + i;
    }
    return answer;
}
