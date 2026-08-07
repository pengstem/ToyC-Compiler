int main() {
    int answer = 73;
    int i = 0;
    int dead = 1;
    while (i < 1000) {
        dead = dead * 3 + i;
        i = i + 1;
    }
    return answer;
}
