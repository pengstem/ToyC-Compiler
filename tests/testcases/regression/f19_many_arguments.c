int bias = 3;

int sum17(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j,
          int k, int l, int m, int n, int o, int p, int q) {
  return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7 + h * 8 +
         i * 9 + j * 10 + k * 11 + l * 12 + m * 13 + n * 14 + o * 15 +
         p * 16 + q * 17;
}

int main() {
  int x = bias;
  return sum17(x, x + 1, x + 2, x + 3, x + 4, x + 5, x + 6, x + 7,
               x + 8, x + 9, x + 10, x + 11, x + 12, x + 13, x + 14,
               x + 15, x + 16);
}
