int combine(int x, int y) {
  int sum1 = x + y;
  int sum2 = y + x;
  int product1 = x * y;
  int product2 = y * x;
  int difference1 = x - y;
  int difference2 = x - y;
  return sum1 + sum2 + product1 + product2 + difference1 + difference2;
}

int main() {
  return combine(11, 17) % 251;
}
