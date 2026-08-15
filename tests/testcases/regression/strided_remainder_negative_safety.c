// The floor-sum lowering requires a nonnegative base and stride because ToyC
// remainder follows signed C truncation semantics.
int main() {
  int total = 0;
  int i = 1;
  while (i < 8) {
    int stride = 0 - i;
    int k = 0;
    while (k < 60) {
      total = total + (stride * k) % 17;
      k = k + 1;
    }
    i = i + 1;
  }
  return total % 251;
}
