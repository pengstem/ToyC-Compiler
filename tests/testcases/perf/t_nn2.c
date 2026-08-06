// 嵌套循环 + (i+j)/4 + i*3：验证非负传播在嵌套循环内正确
int main() {
    int i = 0;
    int s = 0;
    while (i < 1000) {
        int j = 0;
        while (j < 100) {
            s = s + (i + j) / 4 + (i + j) % 16 + i * 3 + j * 5;
            j = j + 1;
        }
        i = i + 1;
    }
    return s;
}
