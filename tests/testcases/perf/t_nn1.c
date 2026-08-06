// 非负优化正确性边界：循环外变量被赋负值后再除法
int main() {
    int i = 0;
    int s = 0;
    while (i < 100) {
        s = s + i / 4 + i % 8;
        i = i + 1;
    }
    i = -5;
    s = s + i / 4;   // C: -5/4 = -1，若误用 srai 得 -2
    s = s + i % 8;   // C: -5%8 = -5
    return s;
}
