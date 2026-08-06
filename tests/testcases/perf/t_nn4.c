// 负数除法混合：确认非负优化不影响负数语义
int main() {
    int s = 0;
    int x = -100;
    while (x < 100) {
        s = s + x / 8 + x % 8;
        x = x + 1;
    }
    return s;
}
