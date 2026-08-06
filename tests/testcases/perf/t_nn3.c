// if 分支内变量被赋负值（应阻止非负传播），循环外除法
int main() {
    int i = 0;
    int s = 0;
    while (i < 100) {
        if (i == 50) {
            i = -3;  // 循环内非自增赋值：计数变量不再恒非负
        } else {
            s = s + i / 4;
            i = i + 1;
        }
        if (i < 0) {
            break;
        }
    }
    s = s + i / 4 + i % 8;
    return s;
}
