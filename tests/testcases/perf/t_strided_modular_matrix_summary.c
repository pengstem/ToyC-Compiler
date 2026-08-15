// A constant-trip modular recurrence whose induction advances by more than one.
// The final update lands exactly on the bound, so termination and int32 safety
// are both provable without assuming signed wraparound.
int main() {
  int a = 1;
  int b = 2;
  int c = 3;
  int i = 1;
  while (i < 10000000) {
    int next_a = (a * 3 + b * 5 + c * 7) % 10007;
    int next_b = (a * 11 + b * 13 + c * 17) % 10007;
    int next_c = (a * 19 + b * 23 + c * 29) % 10007;
    a = next_a;
    b = next_b;
    c = next_c;
    i = i + 3;
  }
  int first = (a + b + c + i) % 251;

  a = 4;
  b = 5;
  c = 6;
  int j = 10000000;
  while (j > 1) {
    int next_a = (a * 7 + b * 11 + c * 13) % 10007;
    int next_b = (a * 17 + b * 19 + c * 23) % 10007;
    int next_c = (a * 29 + b * 31 + c * 37) % 10007;
    a = next_a;
    b = next_b;
    c = next_c;
    j = j - 3;
  }
  return (first + a + b + c + j) % 251;
}
