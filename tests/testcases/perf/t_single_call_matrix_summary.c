int evolve(int limit) {
  int a00 = 1;
  int a01 = 2;
  int a02 = 3;
  int a10 = 4;
  int a11 = 5;
  int a12 = 6;
  int a20 = 7;
  int a21 = 8;
  int a22 = 9;
  int i = 0;
  while (i < limit) {
    int n00 = (a00 * 3 + a01 * 5 + a02 * 7) % 10007;
    int n01 = (a00 * 11 + a01 * 13 + a02 * 17) % 10007;
    int n02 = (a00 * 19 + a01 * 23 + a02 * 29) % 10007;
    int n10 = (a10 * 3 + a11 * 5 + a12 * 7) % 10007;
    int n11 = (a10 * 11 + a11 * 13 + a12 * 17) % 10007;
    int n12 = (a10 * 19 + a11 * 23 + a12 * 29) % 10007;
    int n20 = (a20 * 3 + a21 * 5 + a22 * 7) % 10007;
    int n21 = (a20 * 11 + a21 * 13 + a22 * 17) % 10007;
    int n22 = (a20 * 19 + a21 * 23 + a22 * 29) % 10007;
    a00 = n00;
    a01 = n01;
    a02 = n02;
    a10 = n10;
    a11 = n11;
    a12 = n12;
    a20 = n20;
    a21 = n21;
    a22 = n22;
    i = i + 1;
  }
  return (a00 + a01 + a02 + a10 + a11 + a12 + a20 + a21 + a22) % 251;
}

int main() {
  return evolve(1000000);
}
