// The inner remainder sequence has an outer-loop-dependent stride. Its zero
// base permits a bounded residue lookup; the polynomial terms share the same
// induction prefix sums.
int main() {
  int total = 0;
  int i = 0;
  while (i < 20) {
    int j = 0;
    while (j < 20) {
      int stride = i * j;
      int k = 0;
      while (k < 60) {
        total = total + stride + j * k + i * k + (stride * k) % 97;
        k = k + 1;
      }
      j = j + 1;
    }
    i = i + 1;
  }
  return total % 251;
}
