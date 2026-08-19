// The positive modulus alone is insufficient: these products overflow signed
// int before `%`, so modular matrix arithmetic is not semantics-preserving.
int main() {
  int a = 1000000000;
  int b = 900000000;
  int i = 0;
  while (i < 20) {
    int next_a = (a * 3 + b * 2) % 1000000007;
    int next_b = (a * 5 + b * 7) % 1000000007;
    a = next_a;
    b = next_b;
    i = i + 1;
  }
  return (a + b) % 251;
}
