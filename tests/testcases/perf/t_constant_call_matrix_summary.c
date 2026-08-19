int evolve(int seed, int limit) {
  int a = seed;
  int b = seed + 1;
  int c = seed + 2;
  int i = 0;
  while (i < limit) {
    int next_a = (a * 3 + b * 5 + c * 7) % 10007;
    int next_b = (a * 11 + b * 13 + c * 17) % 10007;
    int next_c = (a * 19 + b * 23 + c * 29) % 10007;
    a = next_a;
    b = next_b;
    c = next_c;
    i = i + 1;
  }
  return (a + b + c) % 251;
}

int main() {
  int iterations = 1000000;
  int first = evolve(1, iterations);
  int second = evolve(2, iterations);
  return (first + second) % 251;
}
