int product(int a, int b) {
  return a * b;
}

int main() {
  int x = 11;
  int y = 17;
  int sum = 0;
  int i = 0;
  while (i < 750) {
    int repeated = (x * y) + (x * y) + (x * y);
    int square = (x + y) * (x + y);
    int distance = (y - x) * (y - x) + (x - y) * (x - y);
    int call1 = product(x, y);
    int call2 = product(x, y);
    sum = sum + repeated + square + distance + call1 + call2;
    i = i + 1;
  }
  return sum % 251;
}
