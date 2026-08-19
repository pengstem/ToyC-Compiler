// Signed C remainder is not periodic when a shifted induction crosses zero:
// -3,-2,-1,0,1,2,3 modulo 7 sums to zero, rather than one nonnegative cycle's 21.
int main() {
  int base = -3;
  int sum = 0;
  int i = 0;
  while (i < 7) {
    sum = sum + (base + i) % 7;
    i = i + 1;
  }
  return sum;
}
