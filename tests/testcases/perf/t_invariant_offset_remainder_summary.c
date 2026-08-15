// The innermost induction is shifted by a nonnegative outer-loop product.
// Its remainder sequence has a constant period even though the phase is only
// known at run time; the guarded accumulator shares the same cycle delta.
int main() {
  int total = 0;
  int diagonal = 0;
  int i = 0;
  while (i < 20) {
    int j = 0;
    while (j < 30) {
      int base = i * j;
      int k = 0;
      while (k < 10000) {
        int value = (base + k) % 17;
        total = total + value;
        if (i == j) diagonal = diagonal + value;
        k = k + 1;
      }
      j = j + 1;
    }
    i = i + 1;
  }
  return (total + diagonal) % 251;
}
