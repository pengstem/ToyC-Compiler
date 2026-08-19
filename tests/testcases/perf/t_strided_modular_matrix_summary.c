// A constant-trip modular recurrence whose induction advances by more than one.
// The final update lands exactly on the bound, so termination and int32 safety
// are both provable without assuming signed wraparound.
int global_a = 13;
int global_b = 14;
int global_c = 15;

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
  int second = (a + b + c + j) % 251;

  a = 7;
  b = 8;
  c = 9;
  int k = 1;
  while (10000000 > k) {
    int next_a = (a * 7 + b * 11 + c * 13) % 10007;
    int next_b = (a * 17 + b * 19 + c * 23) % 10007;
    int next_c = (a * 29 + b * 31 + c * 37) % 10007;
    a = next_a;
    b = next_b;
    c = next_c;
    k = k + 3;
  }
  int third = (a + b + c + k) % 251;

  a = 10;
  b = 11;
  c = 12;
  int l = 10000000;
  while (1 <= l) {
    int next_a = (a * 7 + b * 11 + c * 13) % 10007;
    int next_b = (a * 17 + b * 19 + c * 23) % 10007;
    int next_c = (a * 29 + b * 31 + c * 37) % 10007;
    a = next_a;
    b = next_b;
    c = next_c;
    l = l - 3;
  }
  int fourth = (a + b + c + l) % 251;

  int m = 0;
  while (m < 10000000) {
    int next_a = (global_a * 7 + global_b * 11 + global_c * 13) % 10007;
    int next_b = (global_a * 17 + global_b * 19 + global_c * 23) % 10007;
    int next_c = (global_a * 29 + global_b * 31 + global_c * 37) % 10007;
    global_a = next_a;
    global_b = next_b;
    global_c = next_c;
    m = m + 1;
  }
  return (first + second + third + fourth + global_a + global_b + global_c + m) % 251;
}
